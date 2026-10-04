/*
 * ct_ai_service.h
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

#pragma once

#include "ct_ai_provider.h"
#include "ct_filesystem.h"

#include <glibmm.h>
#include <map>
#include <mutex>
#include <string>
#include <thread>

class CtConfig;

struct CtAiPrompt
{
    std::string id;
    int         version{1};
    std::string title;
    std::string system;
    std::string user;   // template with {text} and {question}
};

class CtAiService
{
public:
    using PieceCallback = std::function<void(const std::string& piece)>;
    using DoneCallback  = std::function<void(const bool ok, const std::string& error)>;

    explicit CtAiService(CtConfig* pCtConfig);
    ~CtAiService();
    CtAiService(const CtAiService&) = delete;
    CtAiService& operator=(const CtAiService&) = delete;

    /// prompt templates: data/prompts/*.prompt, overridable in <config>/prompts/
    const std::map<std::string, CtAiPrompt>& prompts() const { return _prompts; }
    const CtAiPrompt* prompt(const std::string& id) const;
    void reload_prompts();
    /// fill the template variables
    static std::string render(const std::string& tmpl, const std::map<std::string, std::string>& vars);
    CtAiRequest build_request(const CtAiPrompt& prompt, const std::map<std::string, std::string>& vars) const;

    bool is_configured() const;
    bool is_busy() const { return _busy; }
    bool is_model_loaded() const;
    std::string model_name() const;
    const CtAiProvider* provider() const { return _uProvider.get(); }

    /// (re)create the provider from the current configuration; drops a loaded model
    void apply_settings();
    void unload_model();

    /// start a generation; callbacks run on the main thread; returns false if busy or not configured
    bool run(const CtAiRequest& request, PieceCallback on_piece, DoneCallback on_done);
    void cancel();

private:
    void _on_dispatch();
    void _load_prompts_from_dir(const fs::path& dir);
    void _restart_unload_timer();

    CtConfig* const _pCtConfig;
    std::unique_ptr<CtAiProvider> _uProvider;
    std::map<std::string, CtAiPrompt> _prompts;

    std::thread _worker;
    std::atomic<bool> _busy{false};
    CtAiCancelToken _cancel;
    Glib::Dispatcher _dispatcher;
    std::mutex _queueMutex;
    std::string _pendingText;
    bool _pendingDone{false};
    bool _pendingOk{true};
    std::string _pendingError;
    PieceCallback _onPiece;
    DoneCallback _onDone;
    sigc::connection _unloadTimer;
};
