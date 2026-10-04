/*
 * ct_search_panel.cc
 *
 * Side panel: search the full text index of the open document.
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

#include "ct_search_panel.h"
#include "ct_search_index.h"
#include "ct_ai_service.h"
#include "ct_config.h"
#include "ct_main_win.h"
#include "ct_treestore.h"
#include "ct_misc_utils.h"
#include <cmath>
#include <algorithm>

#if GTKMM_MAJOR_VERSION < 4

namespace {
const int SEARCH_RESULTS_LIMIT{200};
const int SEARCH_DEBOUNCE_MS{150};
}

CtSearchPanel::CtSearchPanel(CtMainWin* pCtMainWin)
 : Gtk::Box{Gtk::ORIENTATION_VERTICAL, 4}
 , _pCtMainWin{pCtMainWin}
{
    get_style_context()->add_class("ct-search-panel");
    set_size_request(300, -1);
    set_margin_start(4);
    set_margin_end(4);
    set_margin_top(4);

    _entry.set_placeholder_text(_("Search notebook…"));
    _entry.set_hexpand(true);
    _closeButton.set_image_from_icon_name("window-close-symbolic", Gtk::ICON_SIZE_MENU);
    _closeButton.set_relief(Gtk::RELIEF_NONE);
    _closeButton.set_tooltip_text(_("Close the search panel"));
    _topBox.pack_start(_entry, true, true);
    _topBox.pack_start(_closeButton, false, false);

    _scopeCombo.append(_("Whole Document"));
    _scopeCombo.append(_("Current Node and Subnodes"));
    _scopeCombo.append(_("Current Node"));
    _scopeCombo.set_active(0);
    _modeCombo.append(_("Keyword"));
    _modeCombo.append(_("Semantic"));
    _modeCombo.append(_("Hybrid"));
    _modeCombo.set_tooltip_text(_("Keyword: full text index. Semantic: meaning, needs an embedding model (Preferences → AI). Hybrid: both, merged."));
    _modeCombo.set_hexpand(true);
    _relatedButton.set_label(_("Related"));
    _relatedButton.set_tooltip_text(_("Notes semantically related to the current node"));
    _modeBox.pack_start(_modeCombo, true, true);
    _modeBox.pack_start(_relatedButton, false, false);

    _rStore = Gtk::ListStore::create(_columns);
    _treeView.set_model(_rStore);
    _treeView.set_headers_visible(false);
    _treeView.set_activate_on_single_click(true);
    auto pRenderer = Gtk::manage(new Gtk::CellRendererText{});
    pRenderer->property_wrap_mode() = Pango::WRAP_WORD_CHAR;
    pRenderer->property_wrap_width() = 280;
    pRenderer->property_ypad() = 4;
    auto pColumn = Gtk::manage(new Gtk::TreeViewColumn{});
    pColumn->pack_start(*pRenderer, true);
    pColumn->add_attribute(pRenderer->property_markup(), _columns.markup);
    _treeView.append_column(*pColumn);
    _treeView.get_style_context()->add_class("ct-search-results");
    _scrolled.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    _scrolled.add(_treeView);

    _statusLabel.set_xalign(0.0);
    _statusLabel.get_style_context()->add_class("dim-label");
    _statusLabel.set_ellipsize(Pango::ELLIPSIZE_END);

    pack_start(_topBox, false, false);
    pack_start(_scopeCombo, false, false);
    pack_start(_modeBox, false, false);
    pack_start(_scrolled, true, true);
    pack_start(_statusLabel, false, false);

    _entry.signal_search_changed().connect([this](){
        _debounce.disconnect();
        _debounce = Glib::signal_timeout().connect([this](){ _run_search(); return false; }, SEARCH_DEBOUNCE_MS);
    });
    _entry.signal_activate().connect([this](){
        // Enter: open the first result
        if (auto iter = _rStore->children().begin()) {
            _treeView.set_cursor(_rStore->get_path(iter));
            _jump_to(iter->get_value(_columns.node_id));
        }
    });
    _entry.signal_key_press_event().connect([this](GdkEventKey* pEvent){
        if (GDK_KEY_Escape == pEvent->keyval) {
            _pCtMainWin->search_panel_show(false);
            return true;
        }
        if (GDK_KEY_Down == pEvent->keyval) {
            _treeView.grab_focus();
            if (auto iter = _rStore->children().begin()) _treeView.set_cursor(_rStore->get_path(iter));
            return true;
        }
        return false;
    }, false);
    _closeButton.signal_clicked().connect([this](){ _pCtMainWin->search_panel_show(false); });
    _scopeCombo.signal_changed().connect([this](){ _run_search(); });
    _modeCombo.signal_changed().connect([this](){ _run_search(); });
    _relatedButton.signal_clicked().connect([this](){ show_related_to_current_node(); });
    _update_mode_availability();
    _treeView.signal_row_activated().connect(sigc::mem_fun(*this, &CtSearchPanel::_on_result_activated));
    _treeView.signal_key_press_event().connect([this](GdkEventKey* pEvent){
        if (GDK_KEY_Escape == pEvent->keyval) {
            _pCtMainWin->search_panel_show(false);
            return true;
        }
        return false;
    }, false);

    show_all_children();
}

void CtSearchPanel::focus_entry()
{
    _entry.grab_focus();
}

void CtSearchPanel::set_status(const Glib::ustring& text)
{
    _statusLabel.set_text(text);
}

CtSearchPanel::Scope CtSearchPanel::_scope() const
{
    return static_cast<Scope>(std::max(0, _scopeCombo.get_active_row_number()));
}

std::set<gint64> CtSearchPanel::_scope_node_ids() const
{
    std::set<gint64> ids;
    const Scope scope = _scope();
    if (Scope::WholeDocument == scope) return ids;
    CtTreeIter currIter = _pCtMainWin->curr_tree_iter();
    if (not currIter) return ids;
    ids.insert(currIter.get_node_id());
    if (Scope::CurrentBranch == scope) {
        for (const gint64 id : currIter.get_children_node_ids()) ids.insert(id);
    }
    return ids;
}

void CtSearchPanel::refresh()
{
    _update_mode_availability();
    _run_search();
}

CtSearchPanel::Mode CtSearchPanel::_mode() const
{
    return static_cast<Mode>(std::max(0, _modeCombo.get_active_row_number()));
}

void CtSearchPanel::_update_mode_availability()
{
    const CtAiService* pService = _pCtMainWin->ai_service();
    const bool semantic = pService and pService->is_embedding_configured() and _pCtMainWin->get_ct_config()->semanticIndexEnabled;
    if (_modeCombo.get_active_row_number() < 0) _modeCombo.set_active(semantic ? 2 : 0);
    if (not semantic and _modeCombo.get_active_row_number() != 0) _modeCombo.set_active(0);
    _modeCombo.set_sensitive(semantic);
    _relatedButton.set_sensitive(semantic);
}

/*static*/ Glib::ustring CtSearchPanel::_chunk_excerpt(const Glib::ustring& chunk_text)
{
    // the first line of a chunk is the node name prefix
    const auto nl = chunk_text.find('\n');
    Glib::ustring body = nl == Glib::ustring::npos ? chunk_text : chunk_text.substr(nl + 1);
    Glib::ustring clean;
    bool prev_space{false};
    for (const gunichar ch : body) {
        const bool space = ch == ' ' or ch == '\t' or ch == '\n' or ch == '\r';
        if (space and prev_space) continue;
        clean += space ? ' ' : ch;
        prev_space = space;
    }
    if (clean.size() > 160) clean = clean.substr(0, 160) + "…";
    return clean;
}

void CtSearchPanel::_show_hits(const std::vector<Hit>& hits, const Glib::ustring& query, const std::set<gint64>& scope_ids)
{
    _rStore->clear();
    const bool scoped = not scope_ids.empty();
    std::vector<Glib::ustring> terms = CtSearchIndex::query_terms(query);
    if (terms.size() > 1) terms.insert(terms.begin(), str::join(terms, " "));
    int shown{0};
    for (const Hit& hit : hits) {
        if (scoped and 0 == scope_ids.count(hit.node_id)) continue;
        if (shown >= SEARCH_RESULTS_LIMIT) break;
        Gtk::TreeModel::Row row = *_rStore->append();
        Glib::ustring markup = "<b>" + str::xml_escape(hit.name) + "</b>";
        Glib::ustring parent_path = hit.path;
        const auto sep = parent_path.rfind(" / ");
        parent_path = sep == Glib::ustring::npos ? Glib::ustring{} : parent_path.substr(0, sep);
        if (not parent_path.empty()) {
            markup += "  <span size='small' alpha='60%'>" + str::xml_escape(parent_path) + "</span>";
        }
        if (not hit.snippet.empty()) {
            const Glib::ustring snippet_lower = hit.snippet.lowercase();
            Glib::ustring highlighted;
            Glib::ustring::size_type pos{0};
            while (pos < hit.snippet.size()) {
                Glib::ustring::size_type best = Glib::ustring::npos;
                Glib::ustring::size_type best_len{0};
                for (const Glib::ustring& term : terms) {
                    if (term.empty()) continue;
                    const auto found = snippet_lower.find(term.lowercase(), pos);
                    if (found != Glib::ustring::npos and (best == Glib::ustring::npos or found < best)) {
                        best = found;
                        best_len = term.size();
                    }
                }
                if (best == Glib::ustring::npos) {
                    highlighted += str::xml_escape(hit.snippet.substr(pos));
                    break;
                }
                highlighted += str::xml_escape(hit.snippet.substr(pos, best - pos));
                highlighted += "<span background='#fff3a0' foreground='#000000'>" + str::xml_escape(hit.snippet.substr(best, best_len)) + "</span>";
                pos = best + best_len;
            }
            markup += "\n<span size='small'>" + highlighted + "</span>";
        }
        row[_columns.markup] = markup;
        row[_columns.node_id] = hit.node_id;
        ++shown;
    }
    if (0 == shown) set_status(_("No matches"));
    else set_status(str::format(_("%s matching nodes"), std::to_string(shown)));
}

void CtSearchPanel::show_related_to_current_node()
{
    _lastQuery.clear();
    const CtSearchIndex* pIndex = _pCtMainWin->search_index();
    CtTreeIter currIter = _pCtMainWin->curr_tree_iter();
    if (not pIndex or not pIndex->is_open() or not currIter) return;
    const gint64 curr_id = currIter.get_node_id();
    const std::vector<float> vec = pIndex->node_vector(curr_id);
    if (vec.empty()) {
        _rStore->clear();
        set_status(_("This node is not in the semantic index yet."));
        return;
    }
    std::map<gint64, double> best;
    for (const CtSemanticResult& r : pIndex->semantic_search(vec, 60)) {
        if (r.node_id == curr_id) continue;
        const auto it = best.find(r.node_id);
        if (it == best.end() or r.distance < it->second) best[r.node_id] = r.distance;
    }
    std::vector<std::pair<double, gint64>> ordered;
    for (const auto& [id, dist] : best) ordered.emplace_back(dist, id);
    std::sort(ordered.begin(), ordered.end());
    std::vector<Hit> hits;
    for (const auto& [dist, id] : ordered) {
        CtTreeIter iter = _pCtMainWin->get_tree_store().get_node_from_node_id(id);
        if (not iter) continue;
        Hit hit;
        hit.node_id = id;
        hit.name = iter.get_node_name();
        hit.path = CtMiscUtil::get_node_hierarchical_name(iter, " / ", false/*for_filename*/);
        hit.snippet = str::format(_("similarity %s%"), std::to_string(static_cast<int>(std::round((1.0 - dist) * 100.0))));
        hits.push_back(std::move(hit));
        if (hits.size() >= 12) break;
    }
    _show_hits(hits, "", {});
    if (not hits.empty()) set_status(str::format(_("Notes related to '%s'"), currIter.get_node_name().raw()));
}

void CtSearchPanel::_run_search()
{
    const Glib::ustring query = str::trim(_entry.get_text());
    _lastQuery = query;
    _rStore->clear();
    const CtSearchIndex* pIndex = _pCtMainWin->search_index();
    if (not pIndex or not pIndex->is_open()) {
        set_status(_("No search index for this document (save the document first)."));
        return;
    }
    if (query.empty()) {
        set_status(str::format(_("%s nodes indexed"), std::to_string(pIndex->count_nodes())));
        return;
    }
    const std::set<gint64> scope_ids = _scope_node_ids();
    const bool scoped = not scope_ids.empty();
    const int limit = scoped ? SEARCH_RESULTS_LIMIT * 5 : SEARCH_RESULTS_LIMIT;
    const Mode mode = _mode();

    std::vector<Hit> hits;
    // keyword hits (also the base of hybrid)
    std::vector<CtSearchResult> fts;
    if (mode != Mode::Semantic) fts = pIndex->search(query, limit);
    // semantic hits aggregated per node by the closest chunk
    std::vector<std::pair<gint64, Glib::ustring>> sem_nodes; // ranked node id, best chunk excerpt
    if (mode != Mode::Keyword) {
        CtAiService* pService = _pCtMainWin->ai_service();
        std::vector<float> qvec;
        std::string error;
        if (pService and pService->embed_query(query.raw(), qvec, error)) {
            std::map<gint64, std::pair<double, Glib::ustring>> best;
            for (const CtSemanticResult& r : pIndex->semantic_search(qvec, std::max(60, limit))) {
                const auto it = best.find(r.node_id);
                if (it == best.end() or r.distance < it->second.first) best[r.node_id] = {r.distance, r.text};
            }
            std::vector<std::pair<double, gint64>> ordered;
            for (const auto& [id, pair] : best) ordered.emplace_back(pair.first, id);
            std::sort(ordered.begin(), ordered.end());
            for (const auto& [dist, id] : ordered) sem_nodes.emplace_back(id, best[id].second);
        }
        else if (not error.empty()) {
            set_status(str::format(_("Semantic search unavailable: %s"), error));
            if (mode == Mode::Semantic) return;
        }
    }
    if (mode == Mode::Keyword or (mode == Mode::Hybrid and sem_nodes.empty())) {
        for (const CtSearchResult& res : fts) {
            hits.push_back(Hit{res.node_id, res.node_name, res.node_path, res.snippet, -res.rank});
        }
    }
    else if (mode == Mode::Semantic) {
        for (const auto& [id, chunk] : sem_nodes) {
            CtTreeIter iter = _pCtMainWin->get_tree_store().get_node_from_node_id(id);
            if (not iter) continue;
            hits.push_back(Hit{id, iter.get_node_name(), CtMiscUtil::get_node_hierarchical_name(iter, " / ", false), _chunk_excerpt(chunk), 0.0});
        }
    }
    else {
        // hybrid: reciprocal rank fusion of the two rankings
        std::map<gint64, Hit> merged;
        const double k = 60.0;
        for (size_t i = 0; i < fts.size(); ++i) {
            Hit& h = merged[fts[i].node_id];
            h.node_id = fts[i].node_id; h.name = fts[i].node_name; h.path = fts[i].node_path; h.snippet = fts[i].snippet;
            h.score += 1.0 / (k + static_cast<double>(i + 1));
        }
        for (size_t i = 0; i < sem_nodes.size(); ++i) {
            const gint64 id = sem_nodes[i].first;
            Hit& h = merged[id];
            if (h.node_id == 0) {
                CtTreeIter iter = _pCtMainWin->get_tree_store().get_node_from_node_id(id);
                if (not iter) { merged.erase(id); continue; }
                h.node_id = id; h.name = iter.get_node_name(); h.path = CtMiscUtil::get_node_hierarchical_name(iter, " / ", false);
                h.snippet = _chunk_excerpt(sem_nodes[i].second);
            }
            h.score += 1.0 / (k + static_cast<double>(i + 1));
        }
        for (auto& [id, h] : merged) hits.push_back(h);
        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b){ return a.score > b.score; });
    }
    _show_hits(hits, query, scope_ids);
}

void CtSearchPanel::_on_result_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn*)
{
    if (auto iter = _rStore->get_iter(path)) {
        _jump_to(iter->get_value(_columns.node_id));
    }
}

void CtSearchPanel::_jump_to(const gint64 node_id)
{
    CtTreeIter treeIter = _pCtMainWin->get_tree_store().get_node_from_node_id(node_id);
    if (not treeIter) {
        set_status(_("The node does not exist anymore, rebuild the search index."));
        return;
    }
    CtTextView& textView = _pCtMainWin->get_text_view();
    if (auto rBuffer = textView.get_buffer()) rBuffer->place_cursor(rBuffer->begin());
    _pCtMainWin->get_tree_view().set_cursor_safe(treeIter);
    // select the first occurrence of the first query term in the node text
    auto rBuffer = textView.get_buffer();
    if (rBuffer) {
        std::vector<Glib::ustring> needles = CtSearchIndex::query_terms(_lastQuery);
        if (needles.size() > 1) needles.insert(needles.begin(), str::join(needles, " ")); // whole phrase first
        for (const Glib::ustring& term : needles) {
            Gtk::TextIter match_start, match_end;
            if (rBuffer->begin().forward_search(term, Gtk::TEXT_SEARCH_CASE_INSENSITIVE, match_start, match_end)) {
                rBuffer->select_range(match_start, match_end);
                textView.mm().scroll_to(match_start, CtTextView::TEXT_SCROLL_MARGIN);
                break;
            }
        }
    }
}

#endif // GTKMM_MAJOR_VERSION < 4
