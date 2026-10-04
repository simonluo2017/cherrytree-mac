/*
 * ct_ai_service.cc
 *
 * AI service: owns the provider, runs generations on a worker thread and
 * delivers the output on the GTK main thread; loads the prompt templates.
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

#include "ct_ai_service.h"
#include "ct_config.h"
#include "ct_logging.h"
#ifdef HAVE_LLAMA_CPP
#include "ct_ai_provider_llama.h"
#endif

CtAiService::CtAiService(CtConfig* pCtConfig)
 : _pCtConfig{pCtConfig}
{
    _dispatcher.connect(sigc::mem_fun(*this, &CtAiService::_on_dispatch));
    reload_prompts();
    apply_settings();
}

CtAiService::~CtAiService()
{
    cancel();
    if (_worker.joinable()) _worker.join();
    _unloadTimer.disconnect();
}

const CtAiPrompt* CtAiService::prompt(const std::string& id) const
{
    const auto it = _prompts.find(id);
    return it == _prompts.end() ? nullptr : &it->second;
}

void CtAiService::_load_prompts_from_dir(const fs::path& dir)
{
    if (not fs::is_directory(dir)) return;
    for (const fs::path& file : fs::get_dir_entries(dir)) {
        if (file.extension() != ".prompt") continue;
        try {
            Glib::KeyFile keyFile;
            keyFile.load_from_file(file.string());
            CtAiPrompt prompt;
            prompt.id = keyFile.get_string("prompt", "id");
            if (keyFile.has_key("prompt", "version")) prompt.version = keyFile.get_integer("prompt", "version");
            if (keyFile.has_key("prompt", "title")) prompt.title = keyFile.get_string("prompt", "title");
            if (keyFile.has_key("prompt", "system")) prompt.system = keyFile.get_string("prompt", "system");
            prompt.user = keyFile.get_string("prompt", "user");
            if (prompt.id.empty() or prompt.user.empty()) continue;
            _prompts[prompt.id] = prompt; // a later directory (user config) overrides
        }
        catch (Glib::Error& e) {
            spdlog::warn("ai: prompt file {}: {}", file.string(), e.what().raw());
        }
    }
}

void CtAiService::reload_prompts()
{
    _prompts.clear();
    _load_prompts_from_dir(fs::get_cherrytree_datadir() / "data" / "prompts");
    _load_prompts_from_dir(fs::get_cherrytree_configdir() / "prompts");
    spdlog::debug("ai: {} prompts loaded", _prompts.size());
}

/*static*/ std::string CtAiService::render(const std::string& tmpl, const std::map<std::string, std::string>& vars)
{
    std::string out = tmpl;
    for (const auto& [key, value] : vars) {
        const std::string needle = "{" + key + "}";
        std::string::size_type pos{0};
        while ((pos = out.find(needle, pos)) != std::string::npos) {
            out.replace(pos, needle.size(), value);
            pos += value.size();
        }
    }
    return out;
}

CtAiRequest CtAiService::build_request(const CtAiPrompt& prompt, const std::map<std::string, std::string>& vars) const
{
    CtAiRequest request;
    if (not prompt.system.empty()) request.messages.push_back(CtAiMessage{"system", render(prompt.system, vars)});
    request.messages.push_back(CtAiMessage{"user", render(prompt.user, vars)});
    request.max_tokens = _pCtConfig->aiMaxTokens;
    request.temperature = static_cast<float>(_pCtConfig->aiTemperature);
    return request;
}

bool CtAiService::is_configured() const
{
    return _uProvider and not _pCtConfig->aiModelPath.empty();
}

bool CtAiService::is_model_loaded() const
{
    return _uProvider and _uProvider->is_loaded();
}

std::string CtAiService::model_name() const
{
    if (not _uProvider) return {};
    return _uProvider->model_info().name;
}

void CtAiService::apply_settings()
{
    if (_busy) return; // applied at the next run
    _uProvider.reset();
#ifdef HAVE_LLAMA_CPP
    CtAiProviderLlama::Settings settings;
    settings.model_path = _pCtConfig->aiModelPath;
    settings.n_ctx = _pCtConfig->aiContextSize;
    settings.n_threads = _pCtConfig->aiThreads;
    _uProvider = std::make_unique<CtAiProviderLlama>(settings);
#endif
}

void CtAiService::unload_model()
{
    _unloadTimer.disconnect();
    if (_busy or not _uProvider) return;
    if (_uProvider->is_loaded()) {
        spdlog::info("ai: model unloaded");
        _uProvider->unload();
    }
}

void CtAiService::_restart_unload_timer()
{
    _unloadTimer.disconnect();
    const int minutes = _pCtConfig->aiUnloadMinutes;
    if (minutes <= 0) return;
    _unloadTimer = Glib::signal_timeout().connect([this](){
        unload_model();
        return false;
    }, minutes * 60 * 1000);
}

bool CtAiService::run(const CtAiRequest& request, PieceCallback on_piece, DoneCallback on_done)
{
    if (_busy or not _uProvider) return false;
    if (_worker.joinable()) _worker.join();
    _unloadTimer.disconnect();
    _busy = true;
    _cancel = std::make_shared<std::atomic<bool>>(false);
    _onPiece = std::move(on_piece);
    _onDone = std::move(on_done);
    {
        std::lock_guard<std::mutex> lock{_queueMutex};
        _pendingText.clear();
        _pendingDone = false;
        _pendingOk = true;
        _pendingError.clear();
    }
    CtAiProvider* pProvider = _uProvider.get();
    CtAiCancelToken cancel = _cancel;
    _worker = std::thread([this, pProvider, request, cancel](){
        std::string error;
        bool ok = pProvider->is_loaded() or pProvider->load(error);
        if (ok and not (cancel and cancel->load())) {
            ok = pProvider->generate(request, [this](const std::string& piece){
                {
                    std::lock_guard<std::mutex> lock{_queueMutex};
                    _pendingText += piece;
                }
                _dispatcher.emit();
            }, cancel, error);
        }
        {
            std::lock_guard<std::mutex> lock{_queueMutex};
            _pendingDone = true;
            _pendingOk = ok;
            _pendingError = error;
        }
        _dispatcher.emit();
    });
    return true;
}

void CtAiService::cancel()
{
    if (_cancel) _cancel->store(true);
}

void CtAiService::_on_dispatch()
{
    std::string text;
    bool done{false};
    bool ok{true};
    std::string error;
    {
        std::lock_guard<std::mutex> lock{_queueMutex};
        text.swap(_pendingText);
        done = _pendingDone;
        ok = _pendingOk;
        error = _pendingError;
        if (done) _pendingDone = false;
    }
    if (not text.empty() and _onPiece) _onPiece(text);
    if (done) {
        _busy = false;
        if (_worker.joinable()) _worker.join();
        _restart_unload_timer();
        if (_onDone) _onDone(ok, error);
    }
}
