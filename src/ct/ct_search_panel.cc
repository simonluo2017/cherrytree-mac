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
#include "ct_main_win.h"
#include "ct_treestore.h"
#include "ct_misc_utils.h"

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
    _run_search();
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
    std::vector<CtSearchResult> results = pIndex->search(query, scoped ? SEARCH_RESULTS_LIMIT * 5 : SEARCH_RESULTS_LIMIT);
    int shown{0};
    for (const CtSearchResult& res : results) {
        if (scoped and 0 == scope_ids.count(res.node_id)) continue;
        if (shown >= SEARCH_RESULTS_LIMIT) break;
        Gtk::TreeModel::Row row = *_rStore->append();
        Glib::ustring markup = "<b>" + str::xml_escape(res.node_name) + "</b>";
        // the path without the node itself
        Glib::ustring parent_path = res.node_path;
        const auto sep = parent_path.rfind(" / ");
        parent_path = sep == Glib::ustring::npos ? Glib::ustring{} : parent_path.substr(0, sep);
        if (not parent_path.empty()) {
            markup += "  <span size='small' alpha='60%'>" + str::xml_escape(parent_path) + "</span>";
        }
        if (not res.snippet.empty()) {
            // highlight the matched terms in the excerpt
            Glib::ustring snippet = res.snippet;
            Glib::ustring highlighted;
            const Glib::ustring snippet_lower = snippet.lowercase();
            std::vector<Glib::ustring> terms = CtSearchIndex::query_terms(query);
            if (terms.size() > 1) terms.insert(terms.begin(), str::join(terms, " "));
            Glib::ustring::size_type pos{0};
            while (pos < snippet.size()) {
                Glib::ustring::size_type best = Glib::ustring::npos;
                Glib::ustring::size_type best_len{0};
                for (const Glib::ustring& term : terms) {
                    const auto found = snippet_lower.find(term.lowercase(), pos);
                    if (found != Glib::ustring::npos and (best == Glib::ustring::npos or found < best)) {
                        best = found;
                        best_len = term.size();
                    }
                }
                if (best == Glib::ustring::npos) {
                    highlighted += str::xml_escape(snippet.substr(pos));
                    break;
                }
                highlighted += str::xml_escape(snippet.substr(pos, best - pos));
                highlighted += "<span background='#fff3a0' foreground='#000000'>" + str::xml_escape(snippet.substr(best, best_len)) + "</span>";
                pos = best + best_len;
            }
            markup += "\n<span size='small'>" + highlighted + "</span>";
        }
        row[_columns.markup] = markup;
        row[_columns.node_id] = res.node_id;
        ++shown;
    }
    if (0 == shown) set_status(_("No matches"));
    else set_status(str::format(_("%s matching nodes"), std::to_string(shown)));
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
