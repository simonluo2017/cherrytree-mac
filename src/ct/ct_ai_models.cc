/*
 * ct_ai_models.cc
 *
 * Model Manager: catalog of local models (data/models.manifest), download
 * with resume and SHA256 verification, install / delete.
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

#include "ct_ai_models.h"
#include "ct_config.h"
#include "ct_const.h"
#include "ct_logging.h"

#include <curl/curl.h>
#include <unistd.h>
#include <cstdio>
#include <fstream>

namespace {

const char* const USER_AGENT = "cherrytree-mac model manager (libcurl)";

struct DownloadSink {
    FILE* pFile{nullptr};
    GChecksum* pChecksum{nullptr};
    uint64_t written{0};
};

size_t write_to_file(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* pSink = static_cast<DownloadSink*>(userdata);
    const size_t n = size * nmemb;
    if (fwrite(ptr, 1, n, pSink->pFile) != n) return 0;
    g_checksum_update(pSink->pChecksum, reinterpret_cast<const guchar*>(ptr), n);
    pSink->written += n;
    return n;
}

size_t write_to_string(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* pStr = static_cast<std::string*>(userdata);
    pStr->append(ptr, size * nmemb);
    return size * nmemb;
}

struct ProgressCtx {
    CtModelManager* pManager;
    std::function<int(curl_off_t dlnow, curl_off_t dltotal)> fn;
};

int xferinfo(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t)
{
    auto* pCtx = static_cast<ProgressCtx*>(clientp);
    return pCtx->fn(dlnow, dltotal);
}

} // namespace

CtModelManager::CtModelManager(CtConfig* pCtConfig)
 : _pCtConfig{pCtConfig}
{
    _dispatcher.connect(sigc::mem_fun(*this, &CtModelManager::_on_dispatch));
    reload_catalog();
}

CtModelManager::~CtModelManager()
{
    cancel_download();
    if (_worker.joinable()) _worker.join();
}

/*static*/ fs::path CtModelManager::models_dir()
{
#ifdef __APPLE__
    return fs::path{Glib::get_home_dir()} / "Library" / "Application Support" / "cherrytree-mac" / "models";
#else
    return fs::path{Glib::get_user_data_dir()} / CtConst::APP_NAME / "models";
#endif
}

/*static*/ uint64_t CtModelManager::system_memory_bytes()
{
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 or page_size <= 0) return 0;
    return static_cast<uint64_t>(pages) * static_cast<uint64_t>(page_size);
}

/*static*/ std::string CtModelManager::recommended_tier()
{
    const double gb = static_cast<double>(system_memory_bytes()) / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 23.0) return "large";
    if (gb >= 15.0) return "medium";
    return "small";
}

void CtModelManager::reload_catalog()
{
    load_manifest(fs::get_cherrytree_datadir() / "data" / "models.manifest");
}

bool CtModelManager::load_manifest(const fs::path& manifest)
{
    _catalog.clear();
    if (not fs::is_regular_file(manifest)) {
        spdlog::warn("models: manifest not found at {}", manifest.string());
        return false;
    }
    try {
        Glib::KeyFile keyFile;
        keyFile.load_from_file(manifest.string());
        for (const Glib::ustring& group : keyFile.get_groups()) {
            CtModelEntry e;
            e.id = group;
            auto get = [&](const char* key)->std::string{ return keyFile.has_key(group, key) ? keyFile.get_string(group, key).raw() : std::string{}; };
            e.name = get("name");
            e.tier = get("tier");
            e.publisher = get("publisher");
            e.repo = get("repo");
            e.file = get("file");
            e.license = get("license");
            e.license_url = get("license_url");
            e.description = get("description");
            if (keyFile.has_key(group, "min_memory_gb")) e.min_memory_gb = keyFile.get_integer(group, "min_memory_gb");
            if (keyFile.has_key(group, "context_train")) e.context_train = keyFile.get_integer(group, "context_train");
            if (keyFile.has_key(group, "size_bytes")) e.size_bytes = static_cast<uint64_t>(keyFile.get_uint64(group, "size_bytes"));
            e.sha256 = get("sha256");
            e.url = get("url");
            e.kind = get("kind");
            e.pooling = get("pooling");
            e.query_prefix = get("query_prefix");
            if (keyFile.has_key(group, "embedding_dim")) e.embedding_dim = keyFile.get_integer(group, "embedding_dim");
            // split models: files_extra / sha256_extra / size_bytes_extra are ';' separated lists
            auto split_list = [](const std::string& s){
                std::vector<std::string> v;
                std::string cur;
                for (const char ch : s) { if (ch == ';') { v.push_back(cur); cur.clear(); } else cur += ch; }
                if (not cur.empty() or not v.empty()) v.push_back(cur);
                for (std::string& item : v) { while (not item.empty() and item.front() == ' ') item.erase(0, 1); while (not item.empty() and item.back() == ' ') item.pop_back(); }
                while (not v.empty() and v.back().empty()) v.pop_back();
                return v;
            };
            e.extra_files = split_list(get("files_extra"));
            e.extra_sha256 = split_list(get("sha256_extra"));
            for (const std::string& sz : split_list(get("size_bytes_extra"))) e.extra_sizes.push_back(static_cast<uint64_t>(std::strtoull(sz.c_str(), nullptr, 10)));
            e.extra_sha256.resize(e.extra_files.size());
            e.extra_sizes.resize(e.extra_files.size(), 0);
            bool bad_extra{false};
            for (const std::string& f : e.extra_files) if (f.find('/') != std::string::npos or f.find("..") != std::string::npos) bad_extra = true;
            if (bad_extra) { spdlog::warn("models: skipping entry {} with a malformed extra file", e.id); continue; }
            // only files named like a model from a two level HF repo are accepted
            if (e.repo.empty() or e.file.empty() or e.repo.find('/') == std::string::npos or
                e.file.find('/') != std::string::npos or e.file.find("..") != std::string::npos) {
                spdlog::warn("models: skipping malformed entry {}", e.id);
                continue;
            }
            _catalog.push_back(std::move(e));
        }
    }
    catch (Glib::Error& e) {
        spdlog::error("models: manifest: {}", e.what().raw());
        return false;
    }
    return true;
}

const CtModelEntry* CtModelManager::entry(const std::string& id) const
{
    for (const CtModelEntry& e : _catalog) if (e.id == id) return &e;
    return nullptr;
}

fs::path CtModelManager::model_path(const CtModelEntry& entry) const
{
    return models_dir() / entry.file;
}

fs::path CtModelManager::partial_path(const CtModelEntry& entry) const
{
    return models_dir() / (entry.file + ".part");
}

CtModelState CtModelManager::state(const CtModelEntry& entry) const
{
    if (_downloading and _downloadingId == entry.id) return CtModelState::Downloading;
    bool all_present{true};
    bool any_present{false};
    for (const std::string& f : entry.all_files()) {
        const bool present = fs::is_regular_file(models_dir() / f);
        all_present = all_present and present;
        any_present = any_present or present or fs::is_regular_file(models_dir() / (f + ".part"));
    }
    if (all_present) return CtModelState::Installed;
    if (any_present) return CtModelState::Partial;
    return CtModelState::NotInstalled;
}

uint64_t CtModelManager::partial_bytes(const CtModelEntry& entry) const
{
    uint64_t total{0};
    for (const std::string& f : entry.all_files()) {
        for (const fs::path& p : {models_dir() / f, models_dir() / (f + ".part")}) {
            if (fs::is_regular_file(p)) total += static_cast<uint64_t>(fs::file_size(p));
        }
    }
    return total;
}

void CtModelManager::_apply_proxy(void* pCurl) const
{
    CURL* pHandle = static_cast<CURL*>(pCurl);
    if (not _pCtConfig->proxyUrlColonPort.empty()) {
        curl_easy_setopt(pHandle, CURLOPT_PROXY, _pCtConfig->proxyUrlColonPort.c_str());
        if (not _pCtConfig->proxyUsername.empty()) {
            curl_easy_setopt(pHandle, CURLOPT_PROXYUSERNAME, _pCtConfig->proxyUsername.c_str());
            if (not _pCtConfig->proxyPassword.empty()) {
                curl_easy_setopt(pHandle, CURLOPT_PROXYPASSWORD, _pCtConfig->proxyPassword.c_str());
            }
        }
    }
}

std::string CtModelManager::fetch_publisher_sha256(const CtModelEntry& entry, const std::string& file, std::string& error) const
{
    const std::string url = "https://huggingface.co/api/models/" + entry.repo + "/tree/main";
    std::string body;
    CURL* pCurl = curl_easy_init();
    if (not pCurl) { error = "curl_easy_init failed"; return {}; }
    curl_easy_setopt(pCurl, CURLOPT_URL, url.c_str());
    _apply_proxy(pCurl);
    curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(pCurl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(pCurl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(pCurl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(pCurl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(pCurl, CURLOPT_FAILONERROR, 1L);
    const CURLcode res = curl_easy_perform(pCurl);
    long http_code{0};
    curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(pCurl);
    if (res != CURLE_OK) {
        error = std::string{"Hugging Face API: "} + curl_easy_strerror(res) + " (HTTP " + std::to_string(http_code) + ")";
        return {};
    }
    // minimal JSON scan: the object with "path":"<file>" carries "lfs":{"oid":"<sha256>",...}
    const std::string needle = "\"path\":\"" + file + "\"";
    const auto pos = body.find(needle);
    if (pos == std::string::npos) { error = "The file is not listed in the repository"; return {}; }
    const auto lfs = body.find("\"lfs\"", pos);
    const auto oid = body.find("\"oid\":\"", lfs);
    if (lfs == std::string::npos or oid == std::string::npos) { error = "No LFS metadata for the file"; return {}; }
    const auto start = oid + 7;
    const auto end = body.find('"', start);
    if (end == std::string::npos or end - start != 64) { error = "Unexpected LFS metadata"; return {}; }
    return body.substr(start, 64);
}

std::string CtModelManager::_sha256_of_file(const fs::path& path, const std::string& phase, std::string& error)
{
    std::ifstream in{path.string(), std::ios::binary};
    if (not in) { error = "Cannot read " + path.string(); return {}; }
    GChecksum* pChecksum = g_checksum_new(G_CHECKSUM_SHA256);
    std::vector<char> buf(1 << 20);
    const uint64_t total = fs::file_size(path);
    uint64_t done{0};
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = in.gcount();
        if (n <= 0) break;
        g_checksum_update(pChecksum, reinterpret_cast<const guchar*>(buf.data()), static_cast<gsize>(n));
        done += static_cast<uint64_t>(n);
        if (_cancel) { g_checksum_free(pChecksum); error = "cancelled"; return {}; }
        {
            std::lock_guard<std::mutex> lock{_mutex};
            _pendingProgress = CtModelProgress{phase, done, total, 0.0};
        }
        _dispatcher.emit();
    }
    const std::string hex = g_checksum_get_string(pChecksum);
    g_checksum_free(pChecksum);
    return hex;
}

bool CtModelManager::_download_file(const std::string& url, const fs::path& dest, const uint64_t expected_size, std::string& error)
{
    uint64_t resume_from = fs::is_regular_file(dest) ? static_cast<uint64_t>(fs::file_size(dest)) : 0;
    if (expected_size > 0 and resume_from > expected_size) {
        // a stale partial file bigger than the model: start over
        fs::remove(dest);
        resume_from = 0;
    }
    DownloadSink sink;
    sink.pFile = fopen(dest.c_str(), resume_from > 0 ? "ab" : "wb");
    if (not sink.pFile) { error = "Cannot write " + dest.string(); return false; }
    sink.pChecksum = g_checksum_new(G_CHECKSUM_SHA256);
    sink.written = resume_from;

    CURL* pCurl = curl_easy_init();
    if (not pCurl) { fclose(sink.pFile); g_checksum_free(sink.pChecksum); error = "curl_easy_init failed"; return false; }
    curl_easy_setopt(pCurl, CURLOPT_URL, url.c_str());
    _apply_proxy(pCurl);
    curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, write_to_file);
    curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(pCurl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(pCurl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(pCurl, CURLOPT_LOW_SPEED_TIME, 120L);
    curl_easy_setopt(pCurl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(pCurl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(pCurl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(pCurl, CURLOPT_FAILONERROR, 1L);
    if (resume_from > 0) curl_easy_setopt(pCurl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(resume_from));
    gint64 t0 = g_get_monotonic_time();
    const uint64_t base = resume_from;
    ProgressCtx ctx{this, [&](curl_off_t dlnow, curl_off_t dltotal)->int{
        if (_cancel) return 1;
        const gint64 now = g_get_monotonic_time();
        if (now - _lastProgressEmit < 250000) return 0; // 4 updates per second
        _lastProgressEmit = now;
        const double secs = static_cast<double>(now - t0) / 1e6;
        CtModelProgress p;
        p.phase = "downloading";
        p.done = base + static_cast<uint64_t>(dlnow);
        p.total = expected_size > 0 ? expected_size : (dltotal > 0 ? base + static_cast<uint64_t>(dltotal) : 0);
        p.bytes_per_sec = secs > 0.5 ? static_cast<double>(dlnow) / secs : 0.0;
        { std::lock_guard<std::mutex> lock{_mutex}; _pendingProgress = p; }
        _dispatcher.emit();
        return 0;
    }};
    curl_easy_setopt(pCurl, CURLOPT_XFERINFOFUNCTION, xferinfo);
    curl_easy_setopt(pCurl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(pCurl, CURLOPT_NOPROGRESS, 0L);
    const CURLcode res = curl_easy_perform(pCurl);
    long http_code{0};
    curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(pCurl);
    fclose(sink.pFile);
    g_checksum_free(sink.pChecksum);
    if (res == CURLE_ABORTED_BY_CALLBACK) { error = "cancelled"; return false; }
    if (res == CURLE_RANGE_ERROR and resume_from > 0) {
        // the server ignores Range requests: start over from the beginning
        spdlog::warn("models: resume not supported by the server, restarting the download");
        fs::remove(dest);
        return _download_file(url, dest, expected_size, error);
    }
    if (res != CURLE_OK) {
        if (http_code == 416 and expected_size > 0 and sink.written >= expected_size) return true; // already complete
        error = std::string{"Download failed: "} + curl_easy_strerror(res) + " (HTTP " + std::to_string(http_code) + ")";
        return false;
    }
    return true;
}

void CtModelManager::set_callbacks(ProgressCallback on_progress, DoneCallback on_done)
{
    _onProgress = std::move(on_progress);
    _onDone = std::move(on_done);
}

bool CtModelManager::start_download(const CtModelEntry& entry, std::string& error)
{
    if (_downloading) { error = "A download is already running"; return false; }
    if (_worker.joinable()) _worker.join();
    const fs::path dir = models_dir();
    if (g_mkdir_with_parents(dir.c_str(), 0755) != 0) { error = "Cannot create " + dir.string(); return false; }
    _downloading = true;
    _cancel = false;
    _downloadingId = entry.id;
    _lastMessage.clear();
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _pendingDone = false;
        _pendingProgress = CtModelProgress{"starting", 0, entry.total_size(), 0.0};
    }
    const CtModelEntry e = entry;
    _worker = std::thread([this, e](){
        std::string err;
        bool ok{true};
        std::string message;
        const std::vector<std::string> files = e.all_files();
        for (size_t i = 0; i < files.size() and ok; ++i) {
            const std::string& file = files[i];
            const std::string pinned = i == 0 ? e.sha256 : e.extra_sha256[i - 1];
            const uint64_t size = i == 0 ? e.size_bytes : e.extra_sizes[i - 1];
            const fs::path part = models_dir() / (file + ".part");
            const fs::path final_path = models_dir() / file;
            if (fs::is_regular_file(final_path)) continue; // this part is already installed and verified
            // the expected hash: pinned in the manifest, or the publisher's LFS hash
            std::string expected = pinned;
            std::string hash_source = "pinned in the catalog";
            if (expected.empty()) {
                expected = fetch_publisher_sha256(e, file, err);
                hash_source = "Hugging Face LFS metadata";
                if (expected.empty()) { ok = false; message = "Cannot obtain the hash of " + file + " to verify the download: " + err; break; }
            }
            if (_cancel) { ok = false; message = "cancelled"; break; }
            if (not _download_file(e.download_url_of(file), part, size, err)) { ok = false; message = err; break; }
            if (_cancel) { ok = false; message = "cancelled"; break; }
            const std::string actual = _sha256_of_file(part, "verifying", err);
            if (actual.empty()) { ok = false; message = err; break; }
            if (actual != expected) {
                fs::remove(part);
                ok = false;
                message = "SHA256 mismatch for " + file + " (" + hash_source + "): the download was discarded. Expected " + expected + ", got " + actual;
                break;
            }
            std::string moveErr;
            if (not fs::move_file(part, final_path, &moveErr)) { ok = false; message = "Cannot move the model into place: " + moveErr; break; }
            // keep the hash next to the file for later audits
            std::ofstream side{(final_path.string() + ".sha256")};
            side << actual << "  " << file << "\n" << "source: " << e.download_url_of(file) << "\n" << "hash: " << hash_source << "\n";
            message = "Verified SHA256 " + actual.substr(0, 12) + "… (" + hash_source + ")";
        }
        if (ok and message.empty()) message = "Already installed";
        {
            std::lock_guard<std::mutex> lock{_mutex};
            _pendingDone = true;
            _pendingOk = ok;
            _pendingMessage = message;
        }
        _dispatcher.emit();
    });
    return true;
}

void CtModelManager::cancel_download()
{
    _cancel = true;
}

bool CtModelManager::delete_model(const CtModelEntry& entry, std::string& error)
{
    if (_downloading and _downloadingId == entry.id) { error = "The model is being downloaded"; return false; }
    for (const std::string& f : entry.all_files()) {
        for (const fs::path& p : {models_dir() / f, models_dir() / (f + ".part"), models_dir() / (f + ".sha256")}) {
            if (fs::is_regular_file(p)) fs::remove(p);
        }
    }
    return true;
}

void CtModelManager::_on_dispatch()
{
    CtModelProgress progress;
    bool done{false};
    bool ok{false};
    std::string message;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        progress = _pendingProgress;
        done = _pendingDone;
        ok = _pendingOk;
        message = _pendingMessage;
        if (done) _pendingDone = false;
    }
    if (not done) {
        _lastProgress = progress;
        if (_onProgress) _onProgress(progress);
    }
    else {
        _downloading = false;
        _downloadingId.clear();
        _lastMessage = message;
        _lastProgress = CtModelProgress{};
        if (_worker.joinable()) _worker.join();
        if (_onDone) _onDone(ok, message);
    }
}
