/*
 * ct_ai_provider_openai.h
 *
 * CtAiProvider on an OpenAI compatible HTTP API (LiteLLM proxy, vLLM,
 * Ollama, llama-server, OpenAI...): chat completions with streaming and
 * embeddings. The text leaves the machine: this backend is off by default
 * and only used when the user configures a server.
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

#include <mutex>

class CtAiProviderOpenAI : public CtAiProvider
{
public:
    struct Settings {
        std::string base_url;        // "http://localhost:4000" (with or without /v1)
        std::string api_key;         // Bearer token, may be empty
        std::string model;           // chat model id
        std::string embedding_model; // embedding model id, empty = no embeddings
        int         context_size{4096};
        std::string proxy, proxy_user, proxy_password;
    };

    explicit CtAiProviderOpenAI(const Settings& settings);
    ~CtAiProviderOpenAI() override = default;

    std::string name() const override { return "OpenAI compatible API"; }
    CtAiCapabilities capabilities() const override;
    bool is_loaded() const override { return _loaded; }
    CtAiModelInfo model_info() const override;
    bool load(std::string& error) override;
    void unload() override { _loaded = false; }
    bool generate(const CtAiRequest& request,
                  const CtAiTokenCallback& on_piece,
                  const CtAiCancelToken& cancel,
                  std::string& error) override;
    bool embed(const std::vector<std::string>& texts,
               std::vector<std::vector<float>>& out,
               std::string& error) override;
    int embedding_dim() const override { return _embeddingDim; }

    const Settings& settings() const { return _settings; }
    /// GET /models: the model ids the server offers (blocking, short timeout)
    static std::vector<std::string> list_models(const Settings& settings, std::string& error);
    /// "http://host:4000" or "http://host:4000/v1" -> "http://host:4000/v1"
    static std::string api_root(const std::string& base_url);

private:
    Settings _settings;
    bool _loaded{false};
    int _embeddingDim{0};
    mutable std::mutex _mutex;
};
