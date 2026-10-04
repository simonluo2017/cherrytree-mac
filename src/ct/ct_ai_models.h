/*
 * ct_ai_models.h
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

#pragma once

#include "ct_filesystem.h"

#include <glibmm.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class CtConfig;

struct CtModelEntry
{
    std::string id;
    std::string name;
    std::string tier;        // small | medium | large
    std::string publisher;
    std::string repo;        // Hugging Face repository "Org/Name"
    std::string file;        // file name inside the repository
    std::string license;
    std::string license_url;
    std::string description;
    int         min_memory_gb{0};
    int         context_train{0};
    uint64_t    size_bytes{0}; // 0 = not pinned
    std::string sha256;        // empty = not pinned
    std::string url;           // optional explicit download URL (mirror); default is the Hugging Face resolve URL

    std::string download_url() const { return url.empty() ? "https://huggingface.co/" + repo + "/resolve/main/" + file : url; }
    std::string repo_url() const { return "https://huggingface.co/" + repo; }
    bool hash_pinned() const { return not sha256.empty(); }
};

enum class CtModelState { NotInstalled, Partial, Downloading, Installed };

struct CtModelProgress
{
    std::string phase;      // "downloading", "verifying"
    uint64_t    done{0};
    uint64_t    total{0};
    double      bytes_per_sec{0.0};
};

class CtModelManager
{
public:
    using ProgressCallback = std::function<void(const CtModelProgress&)>;
    using DoneCallback     = std::function<void(const bool ok, const std::string& message)>;

    explicit CtModelManager(CtConfig* pCtConfig);
    ~CtModelManager();
    CtModelManager(const CtModelManager&) = delete;
    CtModelManager& operator=(const CtModelManager&) = delete;

    /// ~/Library/Application Support/cherrytree-mac/models on macOS, XDG data dir elsewhere
    static fs::path models_dir();
    static uint64_t system_memory_bytes();
    /// "small" / "medium" / "large" for this machine's memory
    static std::string recommended_tier();

    void reload_catalog();
    /// load a specific manifest file (the default is data/models.manifest of the installation)
    bool load_manifest(const fs::path& manifest);
    const std::vector<CtModelEntry>& catalog() const { return _catalog; }
    const CtModelEntry* entry(const std::string& id) const;

    fs::path model_path(const CtModelEntry& entry) const;
    fs::path partial_path(const CtModelEntry& entry) const;
    CtModelState state(const CtModelEntry& entry) const;
    uint64_t partial_bytes(const CtModelEntry& entry) const;

    bool is_downloading() const { return _downloading; }
    const std::string& downloading_id() const { return _downloadingId; }

    /// progress/done are delivered on the main thread to whoever is listening; the
    /// Preferences dialog attaches on open and detaches on close, the download goes on
    void set_callbacks(ProgressCallback on_progress, DoneCallback on_done);
    const CtModelProgress& last_progress() const { return _lastProgress; }
    const std::string& last_message() const { return _lastMessage; }
    /// download (resuming a partial file) and verify
    bool start_download(const CtModelEntry& entry, std::string& error);
    void cancel_download();
    bool delete_model(const CtModelEntry& entry, std::string& error);

    /// the publisher's LFS sha256 of the file from the Hugging Face API (blocking network call)
    std::string fetch_publisher_sha256(const CtModelEntry& entry, std::string& error) const;

private:
    void _on_dispatch();
    bool _download_file(const std::string& url, const fs::path& dest, const uint64_t expected_size, std::string& error);
    std::string _sha256_of_file(const fs::path& path, const std::string& phase, std::string& error);
    void _apply_proxy(void* pCurl) const;

    CtConfig* const _pCtConfig;
    std::vector<CtModelEntry> _catalog;

    std::thread _worker;
    std::atomic<bool> _downloading{false};
    std::atomic<bool> _cancel{false};
    std::string _downloadingId;
    Glib::Dispatcher _dispatcher;
    std::mutex _mutex;
    CtModelProgress _pendingProgress;
    bool _pendingDone{false};
    bool _pendingOk{false};
    std::string _pendingMessage;
    ProgressCallback _onProgress;
    DoneCallback _onDone;
    CtModelProgress _lastProgress;
    std::string _lastMessage;
    gint64 _lastProgressEmit{0};
};
