/*
 * ct_ai_provider_openai.cc
 *
 * CtAiProvider on an OpenAI compatible HTTP API (LiteLLM proxy, vLLM,
 * Ollama, llama-server, OpenAI...), see ct_ai_provider_openai.h
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

#include "ct_ai_provider_openai.h"
#include "ct_logging.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;

namespace {

const char* USER_AGENT = "cherrytree-mac";

struct CurlHandle {
    CURL* p{nullptr};
    curl_slist* headers{nullptr};
    CurlHandle() : p{curl_easy_init()} {}
    ~CurlHandle() { if (headers) curl_slist_free_all(headers); if (p) curl_easy_cleanup(p); }
};

void apply_common(CurlHandle& h, const CtAiProviderOpenAI::Settings& s, const std::string& url, const long timeout_s)
{
    curl_easy_setopt(h.p, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h.p, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(h.p, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(h.p, CURLOPT_TIMEOUT, timeout_s);
    curl_easy_setopt(h.p, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h.p, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(h.p, CURLOPT_NOSIGNAL, 1L);
    h.headers = curl_slist_append(h.headers, "Content-Type: application/json");
    h.headers = curl_slist_append(h.headers, "Accept: application/json, text/event-stream");
    if (not s.api_key.empty()) h.headers = curl_slist_append(h.headers, ("Authorization: Bearer " + s.api_key).c_str());
    curl_easy_setopt(h.p, CURLOPT_HTTPHEADER, h.headers);
    if (not s.proxy.empty()) {
        curl_easy_setopt(h.p, CURLOPT_PROXY, s.proxy.c_str());
        if (not s.proxy_user.empty()) {
            curl_easy_setopt(h.p, CURLOPT_PROXYUSERNAME, s.proxy_user.c_str());
            if (not s.proxy_password.empty()) curl_easy_setopt(h.p, CURLOPT_PROXYPASSWORD, s.proxy_password.c_str());
        }
    }
}

size_t write_to_string(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

// error message of an OpenAI style error body, or the raw body
std::string error_from_body(const std::string& body, const long http_code)
{
    try {
        const json j = json::parse(body);
        if (j.contains("error")) {
            if (j["error"].is_object() and j["error"].contains("message")) return j["error"]["message"].get<std::string>();
            if (j["error"].is_string()) return j["error"].get<std::string>();
        }
        if (j.contains("detail") and j["detail"].is_string()) return j["detail"].get<std::string>();
        if (j.contains("message") and j["message"].is_string()) return j["message"].get<std::string>();
    }
    catch (...) {}
    std::string msg = "HTTP " + std::to_string(http_code);
    if (not body.empty()) msg += ": " + body.substr(0, 300);
    return msg;
}

// streaming chat completion: server sent events, one json per "data:" line
struct StreamState {
    const CtAiTokenCallback* on_piece{nullptr};
    const CtAiCancelToken* cancel{nullptr};
    std::string buffer;   // unfinished line
    std::string raw;      // whole body, for a non streamed error
    std::string error;
    bool done{false};
    long http_code{0};
    CURL* curl{nullptr};
};

void handle_sse_line(StreamState& st, const std::string& line)
{
    if (line.compare(0, 5, "data:") != 0) return;
    std::string payload = line.substr(5);
    while (not payload.empty() and payload[0] == ' ') payload.erase(0, 1);
    if (payload == "[DONE]") { st.done = true; return; }
    try {
        const json j = json::parse(payload);
        if (j.contains("error")) { st.error = error_from_body(payload, 200); return; }
        if (not j.contains("choices") or not j["choices"].is_array() or j["choices"].empty()) return;
        const json& choice = j["choices"][0];
        if (choice.contains("delta") and choice["delta"].is_object()) {
            const json& delta = choice["delta"];
            if (delta.contains("content") and delta["content"].is_string()) {
                const std::string piece = delta["content"].get<std::string>();
                if (not piece.empty() and st.on_piece and *st.on_piece) (*st.on_piece)(piece);
            }
        }
        else if (choice.contains("text") and choice["text"].is_string()) {
            const std::string piece = choice["text"].get<std::string>();
            if (not piece.empty() and st.on_piece and *st.on_piece) (*st.on_piece)(piece);
        }
    }
    catch (std::exception& e) {
        spdlog::debug("openai api: unparsable event: {}", e.what());
    }
}

size_t write_stream(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* st = static_cast<StreamState*>(userdata);
    if (st->cancel and *st->cancel and (*st->cancel)->load()) return 0; // aborts the transfer
    const size_t n = size * nmemb;
    if (st->http_code == 0) curl_easy_getinfo(st->curl, CURLINFO_RESPONSE_CODE, &st->http_code);
    st->raw.append(ptr, n);
    if (st->http_code != 200) return n; // an error body: parsed at the end
    st->buffer.append(ptr, n);
    std::string::size_type pos;
    while ((pos = st->buffer.find('\n')) != std::string::npos) {
        std::string line = st->buffer.substr(0, pos);
        st->buffer.erase(0, pos + 1);
        if (not line.empty() and line.back() == '\r') line.pop_back();
        handle_sse_line(*st, line);
    }
    return n;
}

} // namespace

/*static*/ std::string CtAiProviderOpenAI::api_root(const std::string& base_url)
{
    std::string root = base_url;
    while (not root.empty() and (root.back() == '/' or root.back() == ' ')) root.pop_back();
    while (not root.empty() and root.front() == ' ') root.erase(0, 1);
    if (root.size() < 3 or root.compare(root.size() - 3, 3, "/v1") != 0) root += "/v1";
    return root;
}

CtAiProviderOpenAI::CtAiProviderOpenAI(const Settings& settings)
 : _settings{settings}
{
}

CtAiCapabilities CtAiProviderOpenAI::capabilities() const
{
    CtAiCapabilities caps;
    caps.text = not _settings.model.empty();
    caps.embedding = not _settings.embedding_model.empty();
    caps.context_size = _settings.context_size > 0 ? _settings.context_size : 4096;
    return caps;
}

CtAiModelInfo CtAiProviderOpenAI::model_info() const
{
    CtAiModelInfo info;
    info.name = _settings.model.empty() ? _settings.embedding_model : _settings.model;
    info.description = "OpenAI compatible API at " + api_root(_settings.base_url);
    info.path = api_root(_settings.base_url);
    info.context_train = _settings.context_size;
    return info;
}

bool CtAiProviderOpenAI::load(std::string& error)
{
    if (_settings.base_url.empty()) { error = "No API server URL configured"; return false; }
    if (_settings.model.empty() and _settings.embedding_model.empty()) { error = "No model configured for the API server"; return false; }
    _loaded = true; // nothing to load: every request is a network call
    return true;
}

bool CtAiProviderOpenAI::generate(const CtAiRequest& request,
                                  const CtAiTokenCallback& on_piece,
                                  const CtAiCancelToken& cancel,
                                  std::string& error)
{
    std::lock_guard<std::mutex> lock{_mutex};
    if (_settings.model.empty()) { error = "No chat model configured for the API server"; return false; }
    json body;
    body["model"] = _settings.model;
    body["stream"] = true;
    body["max_tokens"] = request.max_tokens;
    body["temperature"] = request.temperature;
    body["top_p"] = request.top_p;
    json messages = json::array();
    for (const CtAiMessage& m : request.messages) messages.push_back({{"role", m.role}, {"content", m.content}});
    body["messages"] = messages;
    const std::string payload = body.dump();

    CurlHandle h;
    if (not h.p) { error = "curl initialisation failed"; return false; }
    apply_common(h, _settings, api_root(_settings.base_url) + "/chat/completions", 600L);
    StreamState st;
    st.on_piece = &on_piece;
    st.cancel = &cancel;
    st.curl = h.p;
    curl_easy_setopt(h.p, CURLOPT_POST, 1L);
    curl_easy_setopt(h.p, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(h.p, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(h.p, CURLOPT_WRITEFUNCTION, write_stream);
    curl_easy_setopt(h.p, CURLOPT_WRITEDATA, &st);
    const CURLcode rc = curl_easy_perform(h.p);
    if (cancel and cancel->load()) { error.clear(); return false; } // cancellation is not an error
    if (not st.buffer.empty()) handle_sse_line(st, st.buffer); // a last line without newline
    long http_code{0};
    curl_easy_getinfo(h.p, CURLINFO_RESPONSE_CODE, &http_code);
    if (rc != CURLE_OK) {
        error = std::string{"Cannot reach the API server: "} + curl_easy_strerror(rc);
        return false;
    }
    if (http_code != 200) {
        error = "API server: " + error_from_body(st.raw, http_code);
        return false;
    }
    if (not st.error.empty()) { error = "API server: " + st.error; return false; }
    if (st.raw.empty()) { error = "The API server returned an empty answer"; return false; }
    // some servers answer a plain (non streamed) completion
    if (st.raw.find("data:") == std::string::npos) {
        try {
            const json j = json::parse(st.raw);
            if (j.contains("choices") and not j["choices"].empty()) {
                const json& choice = j["choices"][0];
                std::string text;
                if (choice.contains("message") and choice["message"].contains("content") and choice["message"]["content"].is_string()) text = choice["message"]["content"];
                else if (choice.contains("text") and choice["text"].is_string()) text = choice["text"];
                if (not text.empty() and on_piece) on_piece(text);
            }
        }
        catch (std::exception&) {
            error = "Unexpected answer from the API server: " + st.raw.substr(0, 200);
            return false;
        }
    }
    return true;
}

bool CtAiProviderOpenAI::embed(const std::vector<std::string>& texts, std::vector<std::vector<float>>& out, std::string& error)
{
    std::lock_guard<std::mutex> lock{_mutex};
    out.clear();
    if (_settings.embedding_model.empty()) { error = "No embedding model configured for the API server"; return false; }
    if (texts.empty()) return true;
    json body;
    body["model"] = _settings.embedding_model;
    body["input"] = texts;
    body["encoding_format"] = "float";
    const std::string payload = body.dump();
    CurlHandle h;
    if (not h.p) { error = "curl initialisation failed"; return false; }
    apply_common(h, _settings, api_root(_settings.base_url) + "/embeddings", 300L);
    std::string response;
    curl_easy_setopt(h.p, CURLOPT_POST, 1L);
    curl_easy_setopt(h.p, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(h.p, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(h.p, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(h.p, CURLOPT_WRITEDATA, &response);
    const CURLcode rc = curl_easy_perform(h.p);
    if (rc != CURLE_OK) { error = std::string{"Cannot reach the API server: "} + curl_easy_strerror(rc); return false; }
    long http_code{0};
    curl_easy_getinfo(h.p, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) { error = "API server: " + error_from_body(response, http_code); return false; }
    try {
        const json j = json::parse(response);
        if (not j.contains("data") or not j["data"].is_array()) { error = "Unexpected embeddings answer"; return false; }
        out.resize(texts.size());
        for (const json& item : j["data"]) {
            const size_t index = item.contains("index") ? item["index"].get<size_t>() : out.size();
            if (index >= out.size() or not item.contains("embedding") or not item["embedding"].is_array()) continue;
            std::vector<float> vec;
            vec.reserve(item["embedding"].size());
            for (const json& v : item["embedding"]) vec.push_back(v.get<float>());
            double norm{0.0};
            for (const float x : vec) norm += static_cast<double>(x) * x;
            norm = std::sqrt(norm);
            if (norm > 0.0) for (float& x : vec) x = static_cast<float>(x / norm);
            if (_embeddingDim == 0) _embeddingDim = static_cast<int>(vec.size());
            out[index] = std::move(vec);
        }
        for (const std::vector<float>& v : out) {
            if (v.empty()) { error = "The API server returned fewer embeddings than texts"; return false; }
        }
    }
    catch (std::exception& e) {
        error = std::string{"Unexpected embeddings answer: "} + e.what();
        return false;
    }
    return true;
}

/*static*/ std::vector<std::string> CtAiProviderOpenAI::list_models(const Settings& settings, std::string& error)
{
    std::vector<std::string> ids;
    CurlHandle h;
    if (not h.p) { error = "curl initialisation failed"; return ids; }
    apply_common(h, settings, api_root(settings.base_url) + "/models", 15L);
    std::string response;
    curl_easy_setopt(h.p, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(h.p, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(h.p, CURLOPT_WRITEDATA, &response);
    const CURLcode rc = curl_easy_perform(h.p);
    if (rc != CURLE_OK) { error = std::string{"Cannot reach the API server: "} + curl_easy_strerror(rc); return ids; }
    long http_code{0};
    curl_easy_getinfo(h.p, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) { error = "API server: " + error_from_body(response, http_code); return ids; }
    try {
        const json j = json::parse(response);
        const json& data = j.contains("data") ? j["data"] : j;
        if (data.is_array()) {
            for (const json& item : data) {
                if (item.is_object() and item.contains("id") and item["id"].is_string()) ids.push_back(item["id"].get<std::string>());
                else if (item.is_string()) ids.push_back(item.get<std::string>());
            }
        }
    }
    catch (std::exception& e) {
        error = std::string{"Unexpected models answer: "} + e.what();
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}
