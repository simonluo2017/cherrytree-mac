/*
 * ct_ai_provider_llama.cc
 *
 * CtAiProvider on llama.cpp (GGUF models, Metal on Apple Silicon).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "ct_ai_provider_llama.h"
#include "ct_logging.h"

#include <llama.h>
#include <algorithm>
#include <thread>

namespace {

void llama_log_to_spdlog(ggml_log_level level, const char* text, void*)
{
    std::string msg{text ? text : ""};
    while (not msg.empty() and (msg.back() == '\n' or msg.back() == '\r')) msg.pop_back();
    if (msg.empty()) return;
    switch (level) {
        case GGML_LOG_LEVEL_ERROR: spdlog::error("llama: {}", msg); break;
        case GGML_LOG_LEVEL_WARN:  spdlog::warn("llama: {}", msg); break;
        default:                   spdlog::debug("llama: {}", msg); break;
    }
}

struct BackendInit {
    BackendInit() {
        llama_log_set(llama_log_to_spdlog, nullptr);
        llama_backend_init();
    }
    ~BackendInit() { llama_backend_free(); }
};

void ensure_backend()
{
    static BackendInit init;
}

} // namespace

CtAiProviderLlama::CtAiProviderLlama(const Settings& settings)
 : _settings{settings}
{
}

CtAiProviderLlama::~CtAiProviderLlama()
{
    unload();
}

CtAiCapabilities CtAiProviderLlama::capabilities() const
{
    CtAiCapabilities caps;
    caps.text = true;
    caps.embedding = false;
    caps.context_size = _pCtx ? static_cast<int>(llama_n_ctx(_pCtx)) : _settings.n_ctx;
    return caps;
}

bool CtAiProviderLlama::is_loaded() const
{
    return nullptr != _pCtx;
}

CtAiModelInfo CtAiProviderLlama::model_info() const
{
    CtAiModelInfo info;
    info.path = _settings.model_path;
    if (_pModel) {
        char desc[256];
        llama_model_desc(_pModel, desc, sizeof(desc));
        info.description = desc;
        info.size_bytes = llama_model_size(_pModel);
        info.n_params = llama_model_n_params(_pModel);
        info.context_train = llama_model_n_ctx_train(_pModel);
        char name[256];
        if (llama_model_meta_val_str(_pModel, "general.name", name, sizeof(name)) > 0) info.name = name;
    }
    if (info.name.empty()) {
        const auto slash = _settings.model_path.find_last_of("/\\");
        info.name = slash == std::string::npos ? _settings.model_path : _settings.model_path.substr(slash + 1);
    }
    return info;
}

bool CtAiProviderLlama::load(std::string& error)
{
    std::lock_guard<std::mutex> lock{_mutex};
    if (_pCtx) return true;
    if (_settings.model_path.empty()) {
        error = "No model file configured";
        return false;
    }
    ensure_backend();
    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = _settings.n_gpu_layers < 0 ? 999 : _settings.n_gpu_layers;
    _pModel = llama_model_load_from_file(_settings.model_path.c_str(), mparams);
    if (not _pModel) {
        error = "Failed to load the model file: " + _settings.model_path;
        return false;
    }
    _pVocab = llama_model_get_vocab(_pModel);
    llama_context_params cparams = llama_context_default_params();
    const int n_ctx_train = llama_model_n_ctx_train(_pModel);
    cparams.n_ctx = static_cast<uint32_t>(_settings.n_ctx > 0 ? std::min(_settings.n_ctx, std::max(n_ctx_train, 512)) : n_ctx_train);
    cparams.n_batch = std::min<uint32_t>(cparams.n_ctx, 2048);
    const int hw_threads = static_cast<int>(std::thread::hardware_concurrency());
    const int n_threads = _settings.n_threads > 0 ? _settings.n_threads : std::max(1, hw_threads > 2 ? hw_threads - 2 : hw_threads);
    cparams.n_threads = n_threads;
    cparams.n_threads_batch = n_threads;
    _pCtx = llama_init_from_model(_pModel, cparams);
    if (not _pCtx) {
        llama_model_free(_pModel);
        _pModel = nullptr;
        _pVocab = nullptr;
        error = "Failed to create the inference context";
        return false;
    }
    spdlog::info("llama: loaded {} (ctx {}, threads {})", _settings.model_path, cparams.n_ctx, n_threads);
    return true;
}

void CtAiProviderLlama::unload()
{
    std::lock_guard<std::mutex> lock{_mutex};
    if (_pCtx) { llama_free(_pCtx); _pCtx = nullptr; }
    if (_pModel) { llama_model_free(_pModel); _pModel = nullptr; }
    _pVocab = nullptr;
}

std::string CtAiProviderLlama::_apply_chat_template(const std::vector<CtAiMessage>& messages) const
{
    std::vector<llama_chat_message> chat;
    chat.reserve(messages.size());
    for (const CtAiMessage& msg : messages) {
        chat.push_back(llama_chat_message{msg.role.c_str(), msg.content.c_str()});
    }
    const char* tmpl = llama_model_chat_template(_pModel, nullptr);
    std::vector<char> buf(4096);
    int32_t n = llama_chat_apply_template(tmpl, chat.data(), chat.size(), true/*add_ass*/, buf.data(), static_cast<int32_t>(buf.size()));
    if (n > static_cast<int32_t>(buf.size())) {
        buf.resize(n + 1);
        n = llama_chat_apply_template(tmpl, chat.data(), chat.size(), true/*add_ass*/, buf.data(), static_cast<int32_t>(buf.size()));
    }
    if (n < 0) {
        // the model has no (supported) template: a plain transcript
        std::string prompt;
        for (const CtAiMessage& msg : messages) {
            prompt += msg.role + ": " + msg.content + "\n";
        }
        prompt += "assistant: ";
        return prompt;
    }
    return std::string{buf.data(), static_cast<size_t>(n)};
}

bool CtAiProviderLlama::generate(const CtAiRequest& request,
                                 const CtAiTokenCallback& on_piece,
                                 const CtAiCancelToken& cancel,
                                 std::string& error)
{
    std::lock_guard<std::mutex> lock{_mutex};
    if (not _pCtx) {
        error = "Model not loaded";
        return false;
    }
    const std::string prompt = _apply_chat_template(request.messages);

    // tokenize
    const bool add_bos = llama_vocab_get_add_bos(_pVocab);
    int32_t n_prompt = -llama_tokenize(_pVocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), nullptr, 0, add_bos, true);
    if (n_prompt <= 0) {
        error = "Failed to tokenize the prompt";
        return false;
    }
    std::vector<llama_token> tokens(n_prompt);
    if (llama_tokenize(_pVocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), tokens.data(), n_prompt, add_bos, true) < 0) {
        error = "Failed to tokenize the prompt";
        return false;
    }
    const int n_ctx = static_cast<int>(llama_n_ctx(_pCtx));
    if (n_prompt >= n_ctx - 16) {
        error = "The text is too long for the model context (" + std::to_string(n_prompt) + " tokens, context " + std::to_string(n_ctx) + ")";
        return false;
    }

    // fresh context for every request
    llama_memory_clear(llama_get_memory(_pCtx), true);

    // sampling chain
    llama_sampler* pSampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(pSampler, llama_sampler_init_top_p(request.top_p, 1));
    llama_sampler_chain_add(pSampler, llama_sampler_init_temp(request.temperature));
    llama_sampler_chain_add(pSampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    bool ok{true};
    // prompt processing in batches
    const int n_batch = static_cast<int>(llama_n_batch(_pCtx));
    for (int i = 0; i < n_prompt and ok; i += n_batch) {
        if (cancel and cancel->load()) break;
        const int n = std::min(n_batch, n_prompt - i);
        llama_batch batch = llama_batch_get_one(tokens.data() + i, n);
        if (llama_decode(_pCtx, batch) != 0) {
            error = "llama_decode failed on the prompt";
            ok = false;
        }
    }
    // generation
    int n_generated{0};
    int n_past = n_prompt;
    while (ok) {
        if (cancel and cancel->load()) break;
        if (n_generated >= request.max_tokens or n_past >= n_ctx - 1) break;
        llama_token tok = llama_sampler_sample(pSampler, _pCtx, -1);
        if (llama_vocab_is_eog(_pVocab, tok)) break;
        char piece[256];
        const int32_t n = llama_token_to_piece(_pVocab, tok, piece, sizeof(piece), 0, true/*special*/);
        if (n > 0 and on_piece) on_piece(std::string{piece, static_cast<size_t>(n)});
        ++n_generated;
        llama_batch batch = llama_batch_get_one(&tok, 1);
        if (llama_decode(_pCtx, batch) != 0) {
            error = "llama_decode failed during generation";
            ok = false;
        }
        ++n_past;
    }
    llama_sampler_free(pSampler);
    return ok;
}
