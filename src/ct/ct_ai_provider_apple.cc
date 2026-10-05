/*
 * ct_ai_provider_apple.cc
 *
 * CtAiProvider on Apple Foundation Models (Apple Intelligence, macOS 26+)
 * through the Swift bridge libct_applefm.dylib loaded at runtime.
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

#include "ct_ai_provider_apple.h"
#include "ct_filesystem.h"
#include "ct_logging.h"
#include "../apple/ct_applefm.h"

#include <dlfcn.h>
#include <glib.h>
#include <glibmm/miscutils.h>

struct CtAiProviderApple::Bridge
{
    void* handle{nullptr};
    int (*version)(){nullptr};
    int (*availability)(char*, size_t){nullptr};
    int (*context_size)(){nullptr};
    int (*generate)(const char*, const char*, int, double, ct_applefm_piece_cb, ct_applefm_cancel_cb, void*, char*, size_t){nullptr};
};

/*static*/ std::vector<std::string> CtAiProviderApple::library_candidates()
{
    std::vector<std::string> out;
    const char* pEnv = g_getenv("CT_APPLEFM_LIB");
    if (pEnv and *pEnv) out.push_back(pEnv);
    const fs::path exe = fs::get_executable_path();
    if (not exe.empty()) {
        const fs::path dir = exe.parent_path();
        out.push_back((dir / ".." / "Frameworks" / "libct_applefm.dylib").string()); // app bundle
        out.push_back((dir / "libct_applefm.dylib").string());                        // build directory
    }
    return out;
}

/*static*/ CtAiProviderApple::Bridge* CtAiProviderApple::_bridge(std::string& error)
{
    static Bridge bridge;
    static bool tried{false};
    static std::string last_error;
    if (bridge.handle) return &bridge;
    if (tried) { error = last_error; return nullptr; }
    tried = true;
    for (const std::string& candidate : library_candidates()) {
        if (not fs::is_regular_file(candidate)) continue;
        void* handle = dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (not handle) {
            last_error = std::string{"cannot load "} + candidate + ": " + (dlerror() ? dlerror() : "");
            spdlog::warn("apple fm: {}", last_error);
            continue;
        }
        bridge.version = reinterpret_cast<int(*)()>(dlsym(handle, "ct_applefm_version"));
        bridge.availability = reinterpret_cast<int(*)(char*, size_t)>(dlsym(handle, "ct_applefm_availability"));
        bridge.context_size = reinterpret_cast<int(*)()>(dlsym(handle, "ct_applefm_context_size"));
        bridge.generate = reinterpret_cast<int(*)(const char*, const char*, int, double, ct_applefm_piece_cb, ct_applefm_cancel_cb, void*, char*, size_t)>(dlsym(handle, "ct_applefm_generate"));
        if (not bridge.version or not bridge.availability or not bridge.context_size or not bridge.generate) {
            last_error = candidate + " is not a valid Apple Foundation Models bridge";
            dlclose(handle);
            bridge = Bridge{};
            continue;
        }
        bridge.handle = handle;
        spdlog::info("apple fm: bridge {} (version {})", candidate, bridge.version());
        return &bridge;
    }
    if (last_error.empty()) last_error = "The Apple Foundation Models bridge (libct_applefm.dylib) is not part of this build";
    error = last_error;
    return nullptr;
}

/*static*/ int CtAiProviderApple::availability(std::string& reason)
{
    std::string error;
    Bridge* pBridge = _bridge(error);
    if (not pBridge) { reason = error; return CT_APPLEFM_UNSUPPORTED; }
    char buffer[512] = {0};
    const int code = pBridge->availability(buffer, sizeof(buffer));
    reason = buffer;
    return code;
}

CtAiProviderApple::CtAiProviderApple() = default;

CtAiProviderApple::~CtAiProviderApple() = default;

CtAiCapabilities CtAiProviderApple::capabilities() const
{
    CtAiCapabilities caps;
    caps.text = true;
    caps.embedding = false;
    caps.context_size = _contextSize;
    return caps;
}

CtAiModelInfo CtAiProviderApple::model_info() const
{
    CtAiModelInfo info;
    info.name = "Apple Intelligence";
    info.description = "Apple Foundation Models on device language model (macOS 26+)";
    info.path = "system";
    info.context_train = _contextSize;
    return info;
}

bool CtAiProviderApple::load(std::string& error)
{
    std::lock_guard<std::mutex> lock{_mutex};
    if (_loaded) return true;
    Bridge* pBridge = _bridge(error);
    if (not pBridge) return false;
    char reason[512] = {0};
    const int code = pBridge->availability(reason, sizeof(reason));
    if (code != CT_APPLEFM_AVAILABLE) {
        error = reason[0] ? reason : "Apple Intelligence is not available";
        return false;
    }
    _contextSize = pBridge->context_size();
    if (_contextSize <= 0) _contextSize = 4096;
    _loaded = true;
    return true;
}

void CtAiProviderApple::unload()
{
    std::lock_guard<std::mutex> lock{_mutex};
    _loaded = false; // nothing to free: the model belongs to the system
}

namespace {
struct GenerateState {
    const CtAiTokenCallback* on_piece;
    const CtAiCancelToken* cancel;
};
void piece_trampoline(const char* piece, size_t len, void* user)
{
    auto* pState = static_cast<GenerateState*>(user);
    if (pState->on_piece and *pState->on_piece and piece and len > 0) (*pState->on_piece)(std::string{piece, len});
}
int cancel_trampoline(void* user)
{
    auto* pState = static_cast<GenerateState*>(user);
    return (pState->cancel and *pState->cancel and (*pState->cancel)->load()) ? 1 : 0;
}
}

bool CtAiProviderApple::generate(const CtAiRequest& request,
                                 const CtAiTokenCallback& on_piece,
                                 const CtAiCancelToken& cancel,
                                 std::string& error)
{
    std::lock_guard<std::mutex> lock{_mutex};
    Bridge* pBridge = _bridge(error);
    if (not pBridge) return false;
    // the system message becomes the session instructions, the rest is the prompt
    std::string instructions;
    std::string prompt;
    for (const CtAiMessage& m : request.messages) {
        if (m.role == "system") { if (not instructions.empty()) instructions += "\n\n"; instructions += m.content; }
        else {
            if (not prompt.empty()) prompt += "\n\n";
            if (m.role == "assistant") prompt += "Previous answer:\n";
            prompt += m.content;
        }
    }
    GenerateState state{&on_piece, &cancel};
    char err[1024] = {0};
    const int code = pBridge->generate(instructions.c_str(), prompt.c_str(), request.max_tokens, request.temperature,
                                       piece_trampoline, cancel_trampoline, &state, err, sizeof(err));
    if (code == CT_APPLEFM_OK) return true;
    if (code == CT_APPLEFM_CANCELLED) { error.clear(); return false; } // cancellation is not an error (empty message)
    error = err[0] ? err : "Apple Intelligence generation failed";
    return false;
}
