/*
 * ct_ai_provider.h
 *
 * AI provider abstraction: the application only talks to CtAiProvider, the
 * concrete backend (llama.cpp today, Apple Foundation Models / MLX / cloud
 * later) is interchangeable.
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

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct CtAiMessage
{
    std::string role;    // "system", "user", "assistant"
    std::string content;
};

struct CtAiRequest
{
    std::vector<CtAiMessage> messages;
    int   max_tokens{1024};
    float temperature{0.4f};
    float top_p{0.9f};
};

struct CtAiCapabilities
{
    bool text{false};
    bool embedding{false};
    int  context_size{0};
};

struct CtAiModelInfo
{
    std::string name;        // "Qwen3 4B Instruct"
    std::string description; // architecture, quantisation
    std::string path;        // file path or identifier
    uint64_t    size_bytes{0};
    uint64_t    n_params{0};
    int         context_train{0};
};

/// Cancellation flag shared between the UI and the generation thread
using CtAiCancelToken = std::shared_ptr<std::atomic<bool>>;

/// Called from the generation thread for every piece of text produced
using CtAiTokenCallback = std::function<void(const std::string& piece)>;

/**
 * @brief Interface of an AI backend.
 *
 * generate() is synchronous and blocking: the caller runs it on a worker
 * thread (see CtAiService) and marshals the callbacks back to the UI thread.
 * Implementations must stop promptly when the cancel token is set.
 */
class CtAiProvider
{
public:
    virtual ~CtAiProvider() = default;

    virtual std::string name() const = 0;
    virtual CtAiCapabilities capabilities() const = 0;
    virtual bool is_loaded() const = 0;
    virtual CtAiModelInfo model_info() const = 0;

    /// load the model/backend; returns false and fills error on failure
    virtual bool load(std::string& error) = 0;
    virtual void unload() = 0;

    /// run a generation; returns false and fills error on failure (cancellation is not an error)
    virtual bool generate(const CtAiRequest& request,
                          const CtAiTokenCallback& on_piece,
                          const CtAiCancelToken& cancel,
                          std::string& error) = 0;

    /// embed texts (L2 normalised vectors); only when capabilities().embedding
    virtual bool embed(const std::vector<std::string>& texts,
                       std::vector<std::vector<float>>& out,
                       std::string& error) { (void)texts; (void)out; error = "Embeddings are not supported by this provider"; return false; }
    /// dimension of the embeddings, 0 when unknown / not loaded
    virtual int embedding_dim() const { return 0; }
};
