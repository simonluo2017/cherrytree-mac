/*
 * ct_main_win_search.cc
 *
 * CtMainWin: document search index (FTS5) upkeep and the search panel.
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

#include "ct_main_win.h"
#include "ct_search_index.h"
#include "ct_search_panel.h"
#include "ct_ai_panel.h"
#include "ct_export2txt.h"
#include "ct_storage_control.h"
#include "ct_misc_utils.h"
#include "ct_logging.h"

namespace {
const int INDEX_NODES_PER_IDLE_TICK{4};
const int INDEX_EDIT_DEBOUNCE_MS{2000};
const char* INDEX_PATH_SEPARATOR{" / "};
}

const CtSearchIndex* CtMainWin::search_index() const
{
    return _uSearchIndex.get();
}

void CtMainWin::search_index_open_for_document()
{
    search_index_close();
    if (not _pCtConfig->searchIndexEnabled) return;
    const fs::path docPath = _uCtStorage ? _uCtStorage->get_file_path() : fs::path{};
    if (docPath.empty()) return; // untitled document: nothing to index yet
    auto pIndex = std::make_unique<CtSearchIndex>();
    std::string error;
    fs::path indexPath = CtSearchIndex::index_path_for_document(docPath);
    if (not pIndex->open(indexPath, &error)) {
        // read only location: keep the index in the user config directory instead
        const fs::path fallbackDir = fs::get_cherrytree_configdir() / "search-index";
        g_mkdir_with_parents(fallbackDir.c_str(), 0755);
        indexPath = fallbackDir / (std::to_string(std::hash<std::string>{}(docPath.string())) + CtSearchIndex::INDEX_FILE_SUFFIX);
        if (not pIndex->open(indexPath, &error)) {
            spdlog::warn("search index: cannot open {}: {}", indexPath.string(), error);
            return;
        }
    }
    spdlog::debug("search index: {}", indexPath.string());
    _uSearchIndex = std::move(pIndex);
    search_index_enqueue_all(false/*force*/);
}

void CtMainWin::search_index_close()
{
    _searchIndexDebounce.disconnect();
    _searchIndexIdle.disconnect();
    _searchIndexQueue.clear();
    _searchIndexQueueTotal = 0;
    _uSearchIndex.reset();
    if (_pSearchPanel) _pSearchPanel->refresh();
}

void CtMainWin::search_index_enqueue_all(const bool force)
{
    if (not _uSearchIndex) return;
    const std::map<gint64, gint64> indexed = _uSearchIndex->indexed_nodes();
    std::set<gint64> present;
    _uCtTreestore->get_store()->foreach_iter([&](const Gtk::TreeModel::iterator& iter)->bool{
        CtTreeIter treeIter = _uCtTreestore->to_ct_tree_iter(iter);
        const gint64 node_id = treeIter.get_node_id();
        present.insert(node_id);
        const auto found = indexed.find(node_id);
        if (force or found == indexed.end() or found->second != treeIter.get_node_modification_time()) {
            _searchIndexQueue.insert(node_id);
        }
        return false; /* continue */
    });
    // nodes that disappeared from the document
    for (const auto& [node_id, mtime] : indexed) {
        if (0 == present.count(node_id)) _uSearchIndex->remove_node(node_id);
    }
    _searchIndexQueueTotal = _searchIndexQueue.size();
    _search_index_start_idle();
}

void CtMainWin::search_index_rebuild()
{
    if (not _uSearchIndex) {
        search_index_open_for_document();
        if (not _uSearchIndex) return;
    }
    _searchIndexQueue.clear();
    _uSearchIndex->clear();
    search_index_enqueue_all(true/*force*/);
}

void CtMainWin::search_index_enqueue_node(const gint64 node_id)
{
    if (not _uSearchIndex) return;
    _searchIndexQueue.insert(node_id);
    _searchIndexQueueTotal = std::max(_searchIndexQueueTotal, _searchIndexQueue.size());
    // edits come in bursts: wait for a pause in the typing
    _searchIndexDebounce.disconnect();
    _searchIndexDebounce = Glib::signal_timeout().connect([this](){
        _search_index_start_idle();
        return false;
    }, INDEX_EDIT_DEBOUNCE_MS);
}

void CtMainWin::search_index_remove_nodes(const std::vector<gint64>& node_ids)
{
    if (not _uSearchIndex) return;
    for (const gint64 node_id : node_ids) {
        _searchIndexQueue.erase(node_id);
        _uSearchIndex->remove_node(node_id);
    }
    if (_pSearchPanel) _pSearchPanel->refresh();
}

void CtMainWin::_search_index_start_idle()
{
    if (_searchIndexIdle.connected() or _searchIndexQueue.empty()) return;
    _searchIndexIdle = Glib::signal_idle().connect(sigc::mem_fun(*this, &CtMainWin::_search_index_idle_tick), Glib::PRIORITY_LOW);
}

bool CtMainWin::_search_index_idle_tick()
{
    if (not _uSearchIndex) {
        _searchIndexQueue.clear();
        return false;
    }
    int processed{0};
    while (not _searchIndexQueue.empty() and processed < INDEX_NODES_PER_IDLE_TICK) {
        const gint64 node_id = *_searchIndexQueue.begin();
        _searchIndexQueue.erase(_searchIndexQueue.begin());
        ++processed;
        CtTreeIter treeIter = _uCtTreestore->get_node_from_node_id(node_id);
        if (not treeIter) {
            _uSearchIndex->remove_node(node_id);
            continue;
        }
        if (treeIter.get_node_is_excluded_from_search()) {
            _uSearchIndex->remove_node(node_id);
            continue;
        }
        Glib::ustring body;
        try {
            CtExportOptions options;
            options.include_node_name = false;
            body = CtExport2Txt{this}.node_export_to_txt(treeIter, fs::path{}, options, -1, -1);
        }
        catch (std::exception& e) {
            spdlog::warn("search index: node {}: {}", node_id, e.what());
            continue;
        }
        const Glib::ustring path = CtMiscUtil::get_node_hierarchical_name(treeIter, INDEX_PATH_SEPARATOR, false/*for_filename*/);
        _uSearchIndex->index_node(node_id,
                                  treeIter.get_node_name(),
                                  path,
                                  treeIter.get_node_tags(),
                                  body,
                                  treeIter.get_node_modification_time());
    }
    const size_t remaining = _searchIndexQueue.size();
    if (remaining > 0 and _searchIndexQueueTotal > 20) {
        _ctStatusBar.update_status(str::format(_("Indexing for search: %s of %s nodes"),
                                               std::to_string(_searchIndexQueueTotal - remaining),
                                               std::to_string(_searchIndexQueueTotal)));
    }
    if (remaining == 0) {
        if (_searchIndexQueueTotal > 20) update_selected_node_statusbar_info();
        _searchIndexQueueTotal = 0;
        if (_pSearchPanel) _pSearchPanel->refresh();
        return false; // done
    }
    return true;
}

void CtMainWin::search_panel_show(const bool show)
{
#if GTKMM_MAJOR_VERSION < 4
    if (not _pSearchPanel) {
        _pSearchPanel = Gtk::manage(new CtSearchPanel{this});
        _hBoxTextSearch.pack_start(*_pSearchPanel, false, false);
    }
    _pSearchPanel->set_visible(show);
    if (show) {
        if (_pAiPanel) _pAiPanel->set_visible(false); // the two panels share the side area
        _pSearchPanel->refresh();
        _pSearchPanel->focus_entry();
    }
    else {
        _ctTextview.mm().grab_focus();
    }
#else
    (void)show;
#endif
}

bool CtMainWin::search_panel_visible() const
{
    return _pSearchPanel and _pSearchPanel->get_visible();
}

void CtMainWin::ai_panel_show(const bool show)
{
#if GTKMM_MAJOR_VERSION < 4
    if (not _uAiService) {
        _uAiService = std::make_unique<CtAiService>(_pCtConfig);
    }
    if (not _pAiPanel) {
        _pAiPanel = Gtk::manage(new CtAiPanel{this});
        _hBoxTextSearch.pack_start(*_pAiPanel, false, false);
    }
    _pAiPanel->set_visible(show);
    if (show) {
        if (_pSearchPanel) _pSearchPanel->set_visible(false);
    }
    else {
        _ctTextview.mm().grab_focus();
    }
#else
    (void)show;
#endif
}

bool CtMainWin::ai_panel_visible() const
{
    return _pAiPanel and _pAiPanel->get_visible();
}
