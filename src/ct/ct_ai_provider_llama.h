/*
 * ct_ai_provider_llama.h
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

#pragma once

#include "ct_ai_provider.h"

#include <mutex>

struct llama_model;
struct llama_context;
struct llama_vocab;

class CtAiProviderLlama : public CtAiProvider
{
public:
    struct Settings {
        std::string model_path;
        int n_ctx{4096};
        int n_threads{0};      // 0 = auto
        int n_gpu_layers{-1};  // -1 = all (Metal on macOS)
    };

    explicit CtAiProviderLlama(const Settings& settings);
    ~CtAiProviderLlama() override;

    std::string name() const override { return "llama.cpp"; }
    CtAiCapabilities capabilities() const override;
    bool is_loaded() const override;
    CtAiModelInfo model_info() const override;
    bool load(std::string& error) override;
    void unload() override;
    bool generate(const CtAiRequest& request,
                  const CtAiTokenCallback& on_piece,
                  const CtAiCancelToken& cancel,
                  std::string& error) override;

    const Settings& settings() const { return _settings; }

private:
    std::string _apply_chat_template(const std::vector<CtAiMessage>& messages) const;

    Settings            _settings;
    llama_model*        _pModel{nullptr};
    llama_context*      _pCtx{nullptr};
    const llama_vocab*  _pVocab{nullptr};
    mutable std::mutex  _mutex; // one generation at a time
};
