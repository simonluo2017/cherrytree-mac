/*
 * ct_search_panel.h
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

#pragma once

#include <gtkmm.h>
#include <set>
#include <vector>

class CtMainWin;
struct CtSearchResult;

/**
 * @brief Panel with a search entry, a scope chooser and the ranked results of
 * the document search index. Activating a result jumps to the node and selects
 * the first occurrence of the query.
 */
class CtSearchPanel : public Gtk::Box
{
public:
    enum class Scope { WholeDocument = 0, CurrentBranch = 1, CurrentNode = 2 };

    explicit CtSearchPanel(CtMainWin* pCtMainWin);

    void focus_entry();
    /// re-run the current query (e.g. after the index changed)
    void refresh();
    void set_status(const Glib::ustring& text);

private:
    struct ResultColumns : public Gtk::TreeModelColumnRecord {
        Gtk::TreeModelColumn<Glib::ustring> markup;
        Gtk::TreeModelColumn<gint64>        node_id;
        ResultColumns() { add(markup); add(node_id); }
    };

    void _run_search();
    void _on_result_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn*);
    void _jump_to(const gint64 node_id);
    std::set<gint64> _scope_node_ids() const;
    Scope _scope() const;

    CtMainWin* const      _pCtMainWin;
    Gtk::Box              _topBox{Gtk::ORIENTATION_HORIZONTAL, 4};
    Gtk::SearchEntry      _entry;
    Gtk::Button           _closeButton;
    Gtk::ComboBoxText     _scopeCombo;
    Gtk::ScrolledWindow   _scrolled;
    ResultColumns         _columns;
    Glib::RefPtr<Gtk::ListStore> _rStore;
    Gtk::TreeView         _treeView;
    Gtk::Label            _statusLabel;
    sigc::connection      _debounce;
    Glib::ustring         _lastQuery;
};
