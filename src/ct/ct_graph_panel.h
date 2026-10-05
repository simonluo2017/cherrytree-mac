/*
 * ct_graph_panel.h
 *
 * Side panel: the knowledge graph extracted from the document (entities,
 * relations, the notes mentioning them) with a small neighbourhood drawing.
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
#include <vector>

class CtMainWin;
struct CtGraphEntity;
struct CtGraphRelation;

#if GTKMM_MAJOR_VERSION < 4

class CtGraphPanel : public Gtk::Box
{
public:
    explicit CtGraphPanel(CtMainWin* pCtMainWin);

    void focus_entry();
    /// reload the entity list and the selected entity (after the graph changed)
    void refresh();
    /// the selected node changed: refresh when the list is limited to the current node
    void on_node_changed();
    void select_entity(const gint64 entity_id);

private:
    struct EntityColumns : public Gtk::TreeModelColumnRecord {
        Gtk::TreeModelColumn<Glib::ustring> markup;
        Gtk::TreeModelColumn<gint64>        entity_id;
        EntityColumns() { add(markup); add(entity_id); }
    };
    struct DetailColumns : public Gtk::TreeModelColumnRecord {
        Gtk::TreeModelColumn<Glib::ustring> markup;
        Gtk::TreeModelColumn<gint64>        node_id;   // 0: not a node row
        Gtk::TreeModelColumn<gint64>        entity_id; // 0: not an entity row
        DetailColumns() { add(markup); add(node_id); add(entity_id); }
    };
    // a neighbour in the drawing
    struct Neighbour { gint64 entity_id{0}; Glib::ustring name, type, relation; bool outgoing{true}; double x{0}, y{0}; };

    void _reload_entities();
    void _show_entity(const gint64 entity_id);
    void _update_status();
    bool _on_draw(const Cairo::RefPtr<Cairo::Context>& cr);
    bool _on_draw_click(GdkEventButton* pEvent);
    void _jump_to_node(const gint64 node_id);
    static Glib::ustring _type_label(const Glib::ustring& type);
    static void _type_color(const Glib::ustring& type, double& r, double& g, double& b);

    CtMainWin* const      _pCtMainWin;
    Gtk::Box              _topBox{Gtk::ORIENTATION_HORIZONTAL, 4};
    Gtk::Label            _titleLabel;
    Gtk::Button           _closeButton;
    Gtk::Box              _filterBox{Gtk::ORIENTATION_HORIZONTAL, 4};
    Gtk::SearchEntry      _entry;
    Gtk::CheckButton      _nodeOnlyCheck;
    Gtk::Paned            _paned{Gtk::ORIENTATION_VERTICAL};
    Gtk::ScrolledWindow   _entitiesScrolled;
    EntityColumns         _entityColumns;
    Glib::RefPtr<Gtk::ListStore> _rEntities;
    Gtk::TreeView         _entitiesView;
    Gtk::Box              _detailBox{Gtk::ORIENTATION_VERTICAL, 4};
    Gtk::DrawingArea      _drawing;
    Gtk::ScrolledWindow   _detailScrolled;
    DetailColumns         _detailColumns;
    Glib::RefPtr<Gtk::ListStore> _rDetail;
    Gtk::TreeView         _detailView;
    Gtk::Box              _buttonBox{Gtk::ORIENTATION_HORIZONTAL, 4};
    Gtk::Button           _buildButton;
    Gtk::Button           _pauseButton;
    Gtk::Button           _rebuildButton;
    Gtk::Label            _statusLabel;
    sigc::connection      _debounce;
    gint64                _selectedEntity{0};
    Glib::ustring         _selectedName, _selectedType;
    std::vector<Neighbour> _neighbours;
    bool                  _reloading{false};
};

#endif // GTKMM_MAJOR_VERSION < 4
