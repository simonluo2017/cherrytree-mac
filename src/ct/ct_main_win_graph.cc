/*
 * ct_main_win_graph.cc
 *
 * CtMainWin: background extraction of the knowledge graph (entities and
 * relations) from the chunks of the search index with the generation model,
 * and the graph panel.
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
#include "ct_graph_panel.h"
#include "ct_logging.h"

namespace {
const int GRAPH_MAX_FAILURES{3};
const int GRAPH_MAX_TOKENS{700};
}

void CtMainWin::graph_extract_kick()
{
    if (not _uSearchIndex or not _pCtConfig->knowledgeGraphEnabled or _pCtConfig->knowledgeGraphPaused) return;
    if (_graphExtracting or _graphPaused) return;
    if (_graphFailures >= GRAPH_MAX_FAILURES) return; // stopped until the next explicit build
    CtAiService* pService = ai_service();
    if (not pService or not pService->is_configured() or pService->is_busy()) return;
    if (_uSearchIndex->graph_count_pending() == 0) { _graph_update_status(); return; }
    _graph_extract_next();
}

void CtMainWin::graph_extract_pause()
{
    _graphRetryTimer.disconnect();
    _graphPaused = true;
    if (not _graphExtracting) return;
    CtAiService* pService = ai_service();
    pService->cancel();
    // the generation stops at the next token: wait for the worker so that the model is free
    const gint64 started = g_get_monotonic_time();
    while (_graphExtracting and g_get_monotonic_time() - started < 5 * G_USEC_PER_SEC) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(5000);
    }
}

void CtMainWin::graph_extract_resume()
{
    _graphPaused = false;
    _graphFailures = 0;
    graph_extract_kick();
}

void CtMainWin::graph_rebuild()
{
    graph_extract_pause();
    _graphPaused = false;
    _graphFailures = 0;
    if (not _uSearchIndex) {
        search_index_open_for_document();
        if (not _uSearchIndex) return;
    }
    _uSearchIndex->graph_clear();
    if (_pGraphPanel) _pGraphPanel->refresh();
    // the chunks only exist when the semantic index or the graph is on: make sure every node has them
    search_index_enqueue_all(true/*force*/);
}

void CtMainWin::_graph_extract_next()
{
    if (not _uSearchIndex or _graphPaused) return;
    CtAiService* pService = ai_service();
    if (pService->is_busy()) return;
    const std::vector<CtChunk> pending = _uSearchIndex->graph_pending_chunks(1);
    if (pending.empty()) {
        _graphExtracting = false;
        _graph_update_status();
        if (_pGraphPanel) _pGraphPanel->refresh();
        return;
    }
    const CtChunk& chunk = pending.front();
    const CtAiPrompt* pPrompt = pService->prompt("extract_graph");
    if (not pPrompt) {
        spdlog::warn("knowledge graph: prompt extract_graph not found");
        _graphFailures = GRAPH_MAX_FAILURES;
        return;
    }
    // the first line of a chunk is the node name
    const auto nl = chunk.text.find('\n');
    const Glib::ustring title = nl == Glib::ustring::npos ? chunk.text : chunk.text.substr(0, nl);
    const Glib::ustring text = nl == Glib::ustring::npos ? Glib::ustring{} : chunk.text.substr(nl + 1);
    if (str::trim(text).size() < 20) {
        // nothing to extract from
        _uSearchIndex->graph_mark_extracted(chunk.chunk_id);
        Glib::signal_idle().connect_once([this](){ _graph_extract_next(); }, Glib::PRIORITY_LOW);
        return;
    }
    CtAiRequest request = pService->build_request(*pPrompt, {{"title", title.raw()}, {"text", text.raw()}});
    request.max_tokens = std::min(request.max_tokens, GRAPH_MAX_TOKENS);
    request.temperature = 0.1f;
    request.background = true;
    _graphChunkId = chunk.chunk_id;
    _graphNodeId = chunk.node_id;
    _graphOutput.clear();
    _graphExtracting = true;
    _graphChunkStart = g_get_monotonic_time();
    ai_status_refresh();
    const bool started = pService->run(request,
        [this](const std::string& piece){ _graphOutput += piece; },
        [this](const bool ok, const std::string& error){ _graph_on_done(ok, error); });
    if (not started) {
        _graphExtracting = false;
        return;
    }
    _graph_update_status();
}

void CtMainWin::_graph_on_done(const bool ok, const std::string& error)
{
    _graphExtracting = false;
    if (not _uSearchIndex) return;
    CtAiService* pService = ai_service();
    if (ok) {
        const CtGraphExtraction extraction = CtSearchIndex::parse_extraction(_graphOutput);
        if (extraction.empty()) _uSearchIndex->graph_mark_extracted(_graphChunkId);
        else _uSearchIndex->graph_store_extraction(_graphChunkId, _graphNodeId, extraction);
        _uSearchIndex->graph_set_model_name(pService->model_name());
        _graphFailures = 0;
        spdlog::debug("knowledge graph: chunk {}: {} entities, {} relations", _graphChunkId, extraction.entities.size(), extraction.relations.size());
    }
    else if (error.empty()) {
        return; // cancelled: the chunk stays pending, resumed by the next kick
    }
    else if (pService->is_model_loaded()) {
        // the chunk itself is the problem (too long for the context, ...): skip it
        spdlog::warn("knowledge graph: chunk {}: {}", _graphChunkId, error);
        _uSearchIndex->graph_mark_extracted(_graphChunkId);
    }
    else {
        ++_graphFailures;
        spdlog::warn("knowledge graph: {}", error);
        _ctStatusBar.update_status(str::format(_("Knowledge graph: %s"), error));
        if (_graphFailures >= GRAPH_MAX_FAILURES) return;
    }
    if (_pGraphPanel and _pGraphPanel->get_visible()) _pGraphPanel->refresh();
    ai_status_refresh();
    if (_graphPaused) return;
    // rest between chunks according to the configured pace (energy / fan noise)
    const int rest_ms = ai_background_rest_ms(g_get_monotonic_time() - _graphChunkStart);
    if (rest_ms > 0) _graphRetryTimer = Glib::signal_timeout().connect([this](){ _graph_extract_next(); return false; }, rest_ms);
    else Glib::signal_idle().connect_once([this](){ _graph_extract_next(); }, Glib::PRIORITY_LOW);
}

void CtMainWin::graph_extract_set_paused(const bool paused)
{
    _pCtConfig->knowledgeGraphPaused = paused;
    if (paused) {
        graph_extract_pause();
        _ctStatusBar.update_status(_("Knowledge graph extraction paused"));
    }
    else {
        _ctStatusBar.update_status(_("Knowledge graph extraction resumed"));
        graph_extract_resume();
    }
    if (_pGraphPanel) _pGraphPanel->refresh();
    ai_status_refresh();
}

void CtMainWin::semantic_index_set_paused(const bool paused)
{
    _pCtConfig->semanticIndexPaused = paused;
    if (paused) {
        // the running batch finishes (a few seconds), no new one is started
        _ctStatusBar.update_status(_("Semantic index (embedding) paused"));
    }
    else {
        _ctStatusBar.update_status(_("Semantic index (embedding) resumed"));
        semantic_index_kick();
    }
    if (_pSearchPanel) _pSearchPanel->refresh();
    ai_status_refresh();
}

void CtMainWin::ai_status_refresh()
{
#if GTKMM_MAJOR_VERSION < 4
    Gtk::Label& label = _ctStatusBar.aiStatus;
    if (not _uSearchIndex) {
        label.hide();
        return;
    }
    std::vector<Glib::ustring> parts;
    std::vector<Glib::ustring> tips;
    // full text index
    if (_searchIndexQueueTotal > 0 and not _searchIndexQueue.empty()) {
        parts.push_back(str::format(_("index %s/%s"), std::to_string(_searchIndexQueueTotal - _searchIndexQueue.size()), std::to_string(_searchIndexQueueTotal)));
        tips.push_back(_("Full text index: nodes indexed so far / nodes to index"));
    }
    else {
        parts.push_back(str::format(_("index %s"), std::to_string(_uSearchIndex->count_nodes())));
        tips.push_back(_("Full text index: number of indexed nodes (up to date)"));
    }
    // semantic index (embedding)
    if (_pCtConfig->semanticIndexEnabled) {
        const gint64 pending = _uSearchIndex->count_pending_chunks();
        const gint64 done = _uSearchIndex->count_embedded_chunks();
        const gint64 failed = _uSearchIndex->count_failed_chunks();
        const gint64 total = pending + done + failed;
        const CtAiService* pService = _uAiService.get();
        Glib::ustring state;
        if (not pService or not pService->is_embedding_configured()) state = _("no model");
        else if (_pCtConfig->semanticIndexPaused and pending > 0) state = _("paused");
        else if (_semanticBusy) state = _("running");
        Glib::ustring part = str::format(_("embed %s/%s"), std::to_string(done), std::to_string(total));
        if (not state.empty()) part += " (" + state + ")";
        parts.push_back(part);
        Glib::ustring tip = _("Semantic index: chunks embedded / chunks of the document");
        if (failed > 0) tip += "\n" + str::format(_("%s chunks could not be embedded and were skipped"), std::to_string(failed));
        if (state == _("no model")) tip += "\n" + Glib::ustring{_("No embedding model: pick one in Preferences → AI (Model Catalog → Qwen3 Embedding → Use This Model)")};
        tips.push_back(tip);
    }
    // knowledge graph
    if (_pCtConfig->knowledgeGraphEnabled) {
        const gint64 pending = _uSearchIndex->graph_count_pending();
        const gint64 done = _uSearchIndex->graph_count_extracted();
        Glib::ustring state;
        if (_pCtConfig->knowledgeGraphPaused and pending > 0) state = _("paused");
        else if (_graphExtracting) state = _("running");
        Glib::ustring part = str::format(_("graph %s/%s"), std::to_string(done), std::to_string(pending + done));
        if (not state.empty()) part += " (" + state + ")";
        parts.push_back(part);
        tips.push_back(str::format(_("Knowledge graph: chunks read by the model / chunks of the document · %s entities, %s relations"),
                                   std::to_string(_uSearchIndex->graph_count_entities()), std::to_string(_uSearchIndex->graph_count_relations())));
    }
    label.set_text("AI: " + str::join(parts, " · "));
    label.set_tooltip_text(str::join(tips, "\n"));
    label.show();
#endif
}

int CtMainWin::ai_background_rest_ms(const gint64 work_us) const
{
    const int factor = _pCtConfig->aiBackgroundPace == 2 ? 3 : (_pCtConfig->aiBackgroundPace == 1 ? 1 : 0);
    if (factor == 0 or work_us <= 0) return 0;
    const gint64 rest_ms = (work_us / 1000) * factor;
    return static_cast<int>(std::min<gint64>(rest_ms, 15000));
}

void CtMainWin::_graph_update_status()
{
    if (not _uSearchIndex) return;
    const gint64 pending = _uSearchIndex->graph_count_pending();
    if (pending > 0) {
        _ctStatusBar.update_status(str::format(_("Knowledge graph: %s chunks to read"), std::to_string(pending)));
    }
    else {
        _ctStatusBar.update_status(str::format(_("Knowledge graph: %s entities, %s relations"),
                                               std::to_string(_uSearchIndex->graph_count_entities()),
                                               std::to_string(_uSearchIndex->graph_count_relations())));
    }
}

void CtMainWin::graph_panel_show(const bool show)
{
#if GTKMM_MAJOR_VERSION < 4
    if (not _pGraphPanel) {
        _pGraphPanel = Gtk::manage(new CtGraphPanel{this});
        _hBoxTextSearch.pack_start(*_pGraphPanel, false, false);
    }
    _pGraphPanel->set_visible(show);
    if (show) {
        if (_pSearchPanel) _pSearchPanel->set_visible(false);
        if (_pAiPanel) _pAiPanel->set_visible(false);
        _pGraphPanel->refresh();
        _pGraphPanel->focus_entry();
    }
    else {
        _ctTextview.mm().grab_focus();
    }
#else
    (void)show;
#endif
}

bool CtMainWin::graph_panel_visible() const
{
    return _pGraphPanel and _pGraphPanel->get_visible();
}
