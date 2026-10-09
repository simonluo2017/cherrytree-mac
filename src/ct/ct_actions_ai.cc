/*
 * ct_actions_ai.cc
 *
 * Local AI actions: summarize / explain / extract tasks / generate tags /
 * ask this node, on the selection or the current node.
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

#include "ct_actions.h"
#include "ct_main_win.h"
#include "ct_ai_service.h"
#include "ct_ai_panel.h"
#include "ct_dialogs.h"
#include "ct_export2txt.h"
#include "ct_treestore.h"
#include "ct_misc_utils.h"
#include "ct_logging.h"
#include "ct_search_index.h"
#include <algorithm>

// the selection if any, otherwise the plain text of the whole node
Glib::ustring CtActions::_ai_source_text(bool& is_selection)
{
    is_selection = false;
    CtTreeIter treeIter = _pCtMainWin->curr_tree_iter();
    if (not treeIter) return {};
    Glib::RefPtr<Gtk::TextBuffer> rBuffer = _pCtMainWin->get_text_view().get_buffer();
    if (rBuffer and rBuffer->get_has_selection()) {
        Gtk::TextIter start, end;
        rBuffer->get_selection_bounds(start, end);
        const Glib::ustring selected = rBuffer->get_text(start, end);
        if (not str::trim(selected).empty()) {
            is_selection = true;
            return selected;
        }
    }
    try {
        CtExportOptions options;
        options.include_node_name = false;
        return CtExport2Txt{_pCtMainWin}.node_export_to_txt(treeIter, fs::path{}, options, -1, -1);
    }
    catch (std::exception& e) {
        spdlog::warn("ai: {}", e.what());
        return {};
    }
}

bool CtActions::_ai_ready_or_error()
{
    CtAiService* pService = _pCtMainWin->ai_service();
    if (not pService) {
        CtDialogs::error_dialog(_("This build has no local AI backend."), *_pCtMainWin);
        return false;
    }
    if (not pService->is_configured()) {
        CtDialogs::info_dialog(_("No AI model is configured yet.\n\nChoose a GGUF model file in Preferences → AI (Local Model), or Apple Intelligence on macOS 26."), *_pCtMainWin);
        return false;
    }
    // the background graph extraction yields to the user
    if (pService->is_busy() and _pCtMainWin->graph_extract_running()) _pCtMainWin->graph_extract_pause();
    if (pService->is_busy()) {
        CtDialogs::info_dialog(_("The AI is still answering the previous request. Stop it first or wait for it to finish."), *_pCtMainWin);
        return false;
    }
    if (not _is_there_selected_node_or_error()) return false;
    return true;
}

void CtActions::_ai_run_prompt(const std::string& prompt_id, const std::map<std::string, std::string>& vars, const Glib::ustring& title)
{
    CtAiService* pService = _pCtMainWin->ai_service();
    const CtAiPrompt* pPrompt = pService->prompt(prompt_id);
    if (not pPrompt) {
        CtDialogs::error_dialog(str::format(_("The prompt '%s' was not found in data/prompts."), prompt_id), *_pCtMainWin);
        return;
    }
    _pCtMainWin->ai_panel_show(true);
    CtAiPanel* pPanel = _pCtMainWin->ai_panel();
    pPanel->begin(title);
    if (not pService->is_model_loaded()) pPanel->set_status(_("Loading the model…"));
    const CtAiRequest request = pService->build_request(*pPrompt, vars);
    CtMainWin* pMainWin = _pCtMainWin;
    const bool started = pService->run(request,
        [pPanel](const std::string& piece){ pPanel->append(piece); },
        [pPanel, pMainWin](const bool ok, const std::string& error){
            pPanel->finish(ok, error);
            pMainWin->graph_extract_resume(); // the background extraction continues after the user's request
        });
    if (not started) {
        pPanel->finish(false, _("The AI is busy."));
    }
}

void CtActions::ai_toggle_semantic_index_pause()
{
    const bool paused = not _pCtConfig->semanticIndexPaused;
    _pCtMainWin->semantic_index_set_paused(paused);
    CtDialogs::info_dialog(paused
        ? _("The semantic index (embedding with the local model, the part that heats the Mac) is paused until you resume it from the same menu. Keyword search, Ask Notebook and your own requests still work.")
        : _("The semantic index (embedding) is resumed."), *_pCtMainWin);
}

void CtActions::ai_toggle_graph_pause()
{
    const bool paused = not _pCtConfig->knowledgeGraphPaused;
    _pCtMainWin->graph_extract_set_paused(paused);
    CtDialogs::info_dialog(paused
        ? _("The knowledge graph extraction is paused until you resume it from the same menu or press Build in the graph panel. The graph built so far stays available.")
        : _("The knowledge graph extraction is resumed."), *_pCtMainWin);
}

void CtActions::ai_toggle_graph_panel()
{
    _pCtMainWin->graph_panel_show(not _pCtMainWin->graph_panel_visible());
}

void CtActions::_ai_text_action(const std::string& prompt_id, const Glib::ustring& title)
{
    if (not _ai_ready_or_error()) return;
    bool is_selection{false};
    const Glib::ustring text = _ai_source_text(is_selection);
    if (str::trim(text).empty()) {
        CtDialogs::info_dialog(_("The node is empty."), *_pCtMainWin);
        return;
    }
    const Glib::ustring scope = is_selection ? _("selection") : _pCtMainWin->curr_tree_iter().get_node_name();
    _ai_run_prompt(prompt_id, {{"text", text.raw()}}, title + " · " + scope);
}

void CtActions::ai_summarize()     { _ai_text_action("summarize", _("Summary")); }
void CtActions::ai_explain()       { _ai_text_action("explain", _("Explanation")); }
void CtActions::ai_extract_tasks() { _ai_text_action("extract_tasks", _("Tasks")); }
void CtActions::ai_generate_tags() { _ai_text_action("generate_tags", _("Tags")); }

void CtActions::ai_ask_node()
{
    if (not _ai_ready_or_error()) return;
    _pCtMainWin->ai_panel_show(true);
    _pCtMainWin->ai_panel()->focus_question();
}

void CtActions::ai_ask_node_question(const Glib::ustring& question)
{
    if (not _ai_ready_or_error()) return;
    bool is_selection{false};
    const Glib::ustring text = _ai_source_text(is_selection);
    if (str::trim(text).empty()) {
        CtDialogs::info_dialog(_("The node is empty."), *_pCtMainWin);
        return;
    }
    _ai_run_prompt("qa_node", {{"text", text.raw()}, {"question", question.raw()}}, question);
}

void CtActions::ai_toggle_panel()
{
    _pCtMainWin->ai_panel_show(not _pCtMainWin->ai_panel_visible());
}

void CtActions::ai_insert_text_in_node(const Glib::ustring& text)
{
    if (text.empty()) return;
    if (not _is_there_selected_node_or_error()) return;
    if (not _is_curr_node_not_read_only_or_error()) return;
    Glib::RefPtr<Gtk::TextBuffer> rBuffer = _pCtMainWin->get_text_view().get_buffer();
    if (not rBuffer) return;
    Gtk::TextIter end = rBuffer->end();
    Glib::ustring to_insert = text;
    if (rBuffer->size() > 0) to_insert = "\n\n" + to_insert;
    rBuffer->insert(end, to_insert);
    _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::nbuf, true/*new_machine_state*/);
    _pCtMainWin->get_text_view().mm().scroll_to(rBuffer->get_insert());
}

void CtActions::ai_new_subnode_with_text(const Glib::ustring& name, const Glib::ustring& text)
{
    if (text.empty()) return;
    if (not _is_there_selected_node_or_error()) return;
    CtNodeData nodeData{};
    nodeData.name = name.empty() ? Glib::ustring{_("AI answer")} : name;
    nodeData.syntax = CtConst::RICH_TEXT_ID;
    nodeData.pTextBuffer = _pCtMainWin->get_new_text_buffer(text);
    Gtk::TreeModel::iterator newIter = _node_add_with_data(_pCtMainWin->curr_tree_iter(), nodeData, true/*add_as_child*/, nullptr/*node_state*/);
    if (newIter) {
        _pCtMainWin->get_tree_view().set_cursor_safe(newIter);
    }
}

// ---- Ask Notebook (RAG over the search index)

std::vector<CtActions::AiExcerpt> CtActions::_ai_retrieve_excerpts(const Glib::ustring& question, const int max_excerpts, const size_t max_chars, std::string* pGraphFacts)
{
    std::vector<AiExcerpt> out;
    if (pGraphFacts) pGraphFacts->clear();
    const CtSearchIndex* pIndex = _pCtMainWin->search_index();
    if (not pIndex or not pIndex->is_open()) return out;
    CtAiService* pService = _pCtMainWin->ai_service();
    auto strip_name_line = [](const Glib::ustring& chunk)->Glib::ustring{
        const auto nl = chunk.find('\n');
        return nl == Glib::ustring::npos ? chunk : chunk.substr(nl + 1);
    };
    auto node_path = [this](const gint64 node_id)->Glib::ustring{
        CtTreeIter iter = _pCtMainWin->get_tree_store().get_node_from_node_id(node_id);
        return iter ? Glib::ustring{CtMiscUtil::get_node_hierarchical_name(iter, " / ", false/*for_filename*/)} : Glib::ustring{};
    };
    // keyword side: the chunks of the best FTS nodes that contain the most query terms
    std::vector<Glib::ustring> terms;
    for (const Glib::ustring& t : CtSearchIndex::query_terms(question)) if (t.size() >= 2) terms.push_back(t.lowercase());
    std::vector<std::pair<gint64, CtChunk>> keyword_ranked; // chunk id, chunk
    for (const CtSearchResult& res : pIndex->search(question, 12)) {
        const CtChunk* pBest{nullptr};
        int best_hits{-1};
        std::vector<CtChunk> chunks = pIndex->chunks_of_node(res.node_id);
        for (const CtChunk& c : chunks) {
            const Glib::ustring lower = c.text.lowercase();
            int hits{0};
            for (const Glib::ustring& t : terms) if (lower.find(t) != Glib::ustring::npos) ++hits;
            if (hits > best_hits) { best_hits = hits; pBest = &c; }
        }
        if (pBest) keyword_ranked.emplace_back(pBest->chunk_id, *pBest);
        else if (chunks.empty()) {
            // no chunks (semantic index off): use the beginning of the node text
            CtTreeIter iter = _pCtMainWin->get_tree_store().get_node_from_node_id(res.node_id);
            if (not iter) continue;
            try {
                CtExportOptions options; options.include_node_name = false;
                CtChunk c; c.node_id = res.node_id; c.chunk_id = -res.node_id;
                c.text = iter.get_node_name() + "\n" + CtExport2Txt{_pCtMainWin}.node_export_to_txt(iter, fs::path{}, options, -1, -1).substr(0, 1500);
                keyword_ranked.emplace_back(c.chunk_id, c);
            } catch (std::exception&) {}
        }
    }
    // semantic side
    std::vector<CtSemanticResult> semantic;
    if (pService and pService->is_embedding_configured() and _pCtConfig->semanticIndexEnabled) {
        std::vector<float> qvec;
        std::string error;
        if (pService->embed_query(question.raw(), qvec, error)) semantic = pIndex->semantic_search(qvec, 20);
        else spdlog::warn("ask notebook: {}", error);
    }
    // knowledge graph side: the chunks mentioning the entities named in the question (and their neighbours)
    std::vector<CtChunk> graph_ranked;
    if (pIndex->graph_count_entities() > 0) {
        const std::vector<CtGraphEntity> matched = pIndex->graph_match_entities(question, 8);
        std::map<gint64, double> weights;
        std::string facts;
        int fact_lines{0};
        for (const CtGraphEntity& e : matched) {
            weights[e.entity_id] = 2.0;
            if (not e.description.empty() and fact_lines < 14) {
                facts += "- " + e.name.raw() + " (" + e.type.raw() + "): " + e.description.raw() + "\n";
                ++fact_lines;
            }
        }
        for (const CtGraphEntity& e : matched) {
            for (const CtGraphRelation& r : pIndex->graph_relations_of(e.entity_id)) {
                const gint64 other = r.source_id == e.entity_id ? r.target_id : r.source_id;
                if (0 == weights.count(other)) weights[other] = 1.0; // one hop
                if (fact_lines < 14) {
                    facts += "- " + r.source_name.raw() + " → " + r.type.raw() + " → " + r.target_name.raw();
                    if (not r.description.empty()) facts += " (" + r.description.raw() + ")";
                    facts += "\n";
                    ++fact_lines;
                }
            }
        }
        for (const auto& [chunk_id, score] : pIndex->graph_chunks_of_entities(weights, 20)) {
            CtChunk c;
            if (pIndex->chunk(chunk_id, c)) graph_ranked.push_back(c);
        }
        if (pGraphFacts and not facts.empty()) *pGraphFacts = facts;
    }
    // reciprocal rank fusion at chunk level
    std::map<gint64, AiExcerpt> merged;
    const double k = 60.0;
    for (size_t i = 0; i < keyword_ranked.size(); ++i) {
        AiExcerpt& e = merged[keyword_ranked[i].first];
        e.node_id = keyword_ranked[i].second.node_id; e.chunk_id = keyword_ranked[i].first; e.text = keyword_ranked[i].second.text;
        e.score += 1.0 / (k + static_cast<double>(i + 1));
    }
    for (size_t i = 0; i < semantic.size(); ++i) {
        AiExcerpt& e = merged[semantic[i].chunk_id];
        e.node_id = semantic[i].node_id; e.chunk_id = semantic[i].chunk_id; e.text = semantic[i].text;
        e.score += 1.0 / (k + static_cast<double>(i + 1));
    }
    for (size_t i = 0; i < graph_ranked.size(); ++i) {
        AiExcerpt& e = merged[graph_ranked[i].chunk_id];
        e.node_id = graph_ranked[i].node_id; e.chunk_id = graph_ranked[i].chunk_id; e.text = graph_ranked[i].text;
        e.score += 1.0 / (k + static_cast<double>(i + 1));
    }
    for (auto& [id, e] : merged) out.push_back(e);
    std::sort(out.begin(), out.end(), [](const AiExcerpt& a, const AiExcerpt& b){ return a.score > b.score; });
    // at most two excerpts per node, within the character budget
    std::vector<AiExcerpt> chosen;
    std::map<gint64, int> per_node;
    size_t used{0};
    for (AiExcerpt& e : out) {
        if (static_cast<int>(chosen.size()) >= max_excerpts) break;
        if (per_node[e.node_id] >= 2) continue;
        Glib::ustring body = strip_name_line(e.text);
        const size_t per_excerpt = std::min<size_t>(1800, std::max<size_t>(300, max_chars / 2));
        if (body.size() > per_excerpt) body = body.substr(0, per_excerpt) + "…";
        if (used + body.size() > max_chars and not chosen.empty()) continue;
        e.text = body;
        e.path = node_path(e.node_id);
        if (e.path.empty()) continue;
        used += body.size();
        ++per_node[e.node_id];
        chosen.push_back(e);
    }
    return chosen;
}

void CtActions::ai_ask_notebook()
{
    if (not _ai_ready_or_error()) return;
    _pCtMainWin->ai_panel_show(true);
    _pCtMainWin->ai_panel()->set_ask_scope(1);
    _pCtMainWin->ai_panel()->focus_question();
}

void CtActions::ai_ask_notebook_question(const Glib::ustring& question)
{
    if (not _ai_ready_or_error()) return;
    const CtSearchIndex* pIndex = _pCtMainWin->search_index();
    if (not pIndex or not pIndex->is_open()) {
        CtDialogs::info_dialog(_("Ask Notebook needs the search index: save the document first."), *_pCtMainWin);
        return;
    }
    // the excerpts must fit in the model context together with the answer: ~2.5 chars per token
    int ctx_tokens = _pCtConfig->aiContextSize;
    if (const CtAiProvider* pProvider = _pCtMainWin->ai_service()->provider()) {
        if (pProvider->is_loaded()) ctx_tokens = std::min(ctx_tokens, pProvider->capabilities().context_size);
    }
    const int free_tokens = std::max(200, ctx_tokens - _pCtConfig->aiMaxTokens - 350);
    const size_t budget_chars = static_cast<size_t>(std::min(9000, std::max(600, free_tokens * 2)));
    std::string facts;
    const std::vector<AiExcerpt> excerpts = _ai_retrieve_excerpts(question, 6, budget_chars, &facts);
    if (excerpts.empty()) {
        _pCtMainWin->ai_panel_show(true);
        _pCtMainWin->ai_panel()->begin(question);
        _pCtMainWin->ai_panel()->finish(false, _("Nothing in the notebook matches the question (try other words, or wait for the index to finish)."));
        return;
    }
    std::string context;
    std::vector<CtAiPanel::Source> sources;
    int n{0};
    for (const AiExcerpt& e : excerpts) {
        ++n;
        context += "[" + std::to_string(n) + "] " + e.path.raw() + "\n" + e.text.raw() + "\n\n";
        sources.push_back(CtAiPanel::Source{n, e.node_id, e.path, e.text});
    }
    std::string facts_block;
    if (not facts.empty() and facts.size() < budget_chars / 3) {
        facts_block = std::string{_("Facts from the knowledge graph of the notebook (derived automatically, may be imperfect):")} + "\n" + facts + "\n";
    }
    _ai_run_prompt("qa_notebook", {{"context", context}, {"facts", facts_block}, {"question", question.raw()}}, question);
    _pCtMainWin->ai_panel()->set_sources(sources);
}
