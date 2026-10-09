/*
 * ct_graph_panel.cc
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

#include "ct_graph_panel.h"
#include "ct_main_win.h"
#include "ct_search_index.h"
#include "ct_ai_service.h"
#include "ct_misc_utils.h"
#include "ct_treestore.h"
#include <algorithm>
#include <cmath>

#if GTKMM_MAJOR_VERSION < 4

namespace {
const int ENTITIES_LIMIT{500};
const int FILTER_DEBOUNCE_MS{150};
const size_t NEIGHBOURS_MAX{14};
}

CtGraphPanel::CtGraphPanel(CtMainWin* pCtMainWin)
 : Gtk::Box{Gtk::ORIENTATION_VERTICAL, 4}
 , _pCtMainWin{pCtMainWin}
{
    get_style_context()->add_class("ct-graph-panel");
    set_size_request(320, -1);
    set_margin_start(4);
    set_margin_end(4);
    set_margin_top(4);

    _titleLabel.set_xalign(0.0);
    _titleLabel.set_hexpand(true);
    _titleLabel.set_markup("<b>" + Glib::ustring{_("Knowledge Graph")} + "</b>");
    _closeButton.set_image_from_icon_name("window-close-symbolic", Gtk::ICON_SIZE_MENU);
    _closeButton.set_relief(Gtk::RELIEF_NONE);
    _closeButton.set_tooltip_text(_("Close the knowledge graph panel"));
    _topBox.pack_start(_titleLabel, true, true);
    _topBox.pack_start(_closeButton, false, false);

    _entry.set_placeholder_text(_("Filter entities…"));
    _entry.set_hexpand(true);
    _nodeOnlyCheck.set_label(_("This node"));
    _nodeOnlyCheck.set_tooltip_text(_("Only the entities mentioned in the current node"));
    _filterBox.pack_start(_entry, true, true);
    _filterBox.pack_start(_nodeOnlyCheck, false, false);

    _rEntities = Gtk::ListStore::create(_entityColumns);
    _entitiesView.set_model(_rEntities);
    _entitiesView.set_headers_visible(false);
    {
        auto pRenderer = Gtk::manage(new Gtk::CellRendererText{});
        pRenderer->property_ellipsize() = Pango::ELLIPSIZE_END;
        pRenderer->property_ypad() = 2;
        auto pColumn = Gtk::manage(new Gtk::TreeViewColumn{});
        pColumn->pack_start(*pRenderer, true);
        pColumn->add_attribute(pRenderer->property_markup(), _entityColumns.markup);
        _entitiesView.append_column(*pColumn);
    }
    _entitiesScrolled.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    _entitiesScrolled.add(_entitiesView);
    _entitiesScrolled.set_size_request(-1, 120);

    _drawing.set_size_request(-1, 190);
    _drawing.add_events(Gdk::BUTTON_PRESS_MASK);
    _drawing.get_style_context()->add_class("ct-graph-drawing");
    _rDetail = Gtk::ListStore::create(_detailColumns);
    _detailView.set_model(_rDetail);
    _detailView.set_headers_visible(false);
    _detailView.set_activate_on_single_click(true);
    {
        auto pRenderer = Gtk::manage(new Gtk::CellRendererText{});
        pRenderer->property_wrap_mode() = Pango::WRAP_WORD_CHAR;
        pRenderer->property_wrap_width() = 290;
        pRenderer->property_ypad() = 3;
        auto pColumn = Gtk::manage(new Gtk::TreeViewColumn{});
        pColumn->pack_start(*pRenderer, true);
        pColumn->add_attribute(pRenderer->property_markup(), _detailColumns.markup);
        _detailView.append_column(*pColumn);
    }
    _detailScrolled.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    _detailScrolled.add(_detailView);
    _detailScrolled.set_size_request(-1, 110);
    _detailBox.pack_start(_drawing, false, false);
    _detailBox.pack_start(_detailScrolled, true, true);

    _paned.pack1(_entitiesScrolled, true, false);
    _paned.pack2(_detailBox, true, false);
    _paned.set_position(150);

    _buildButton.set_label(_("Build"));
    _buildButton.set_tooltip_text(_("Read the notes with the local model and extract entities and relations (background)"));
    _pauseButton.set_label(_("Pause"));
    _rebuildButton.set_label(_("Rebuild"));
    _rebuildButton.set_tooltip_text(_("Drop the graph and extract everything again"));
    _buttonBox.pack_start(_buildButton, true, true);
    _buttonBox.pack_start(_pauseButton, true, true);
    _buttonBox.pack_start(_rebuildButton, true, true);
    _statusLabel.set_xalign(0.0);
    _statusLabel.set_line_wrap(true);
    _statusLabel.get_style_context()->add_class("dim-label");

    pack_start(_topBox, false, false);
    pack_start(_filterBox, false, false);
    pack_start(_paned, true, true);
    pack_start(_buttonBox, false, false);
    pack_start(_statusLabel, false, false);

    _closeButton.signal_clicked().connect([this](){ _pCtMainWin->graph_panel_show(false); });
    _entry.signal_search_changed().connect([this](){
        _debounce.disconnect();
        _debounce = Glib::signal_timeout().connect([this](){ _reload_entities(); return false; }, FILTER_DEBOUNCE_MS);
    });
    _entry.signal_key_press_event().connect([this](GdkEventKey* pEvent){
        if (GDK_KEY_Escape == pEvent->keyval) { _pCtMainWin->graph_panel_show(false); return true; }
        if (GDK_KEY_Down == pEvent->keyval) {
            _entitiesView.grab_focus();
            if (auto iter = _rEntities->children().begin()) _entitiesView.set_cursor(_rEntities->get_path(iter));
            return true;
        }
        return false;
    }, false);
    _nodeOnlyCheck.signal_toggled().connect([this](){ _reload_entities(); });
    _entitiesView.get_selection()->signal_changed().connect([this](){
        if (_reloading) return;
        auto iter = _entitiesView.get_selection()->get_selected();
        if (iter) _show_entity(iter->get_value(_entityColumns.entity_id));
    });
    _detailView.signal_row_activated().connect([this](const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn*){
        auto iter = _rDetail->get_iter(path);
        if (not iter) return;
        const gint64 node_id = iter->get_value(_detailColumns.node_id);
        const gint64 entity_id = iter->get_value(_detailColumns.entity_id);
        if (node_id > 0) _jump_to_node(node_id);
        else if (entity_id > 0) select_entity(entity_id);
    });
    _drawing.signal_draw().connect(sigc::mem_fun(*this, &CtGraphPanel::_on_draw));
    _drawing.signal_button_press_event().connect(sigc::mem_fun(*this, &CtGraphPanel::_on_draw_click));
    _buildButton.signal_clicked().connect([this](){
        CtConfig* pConfig = _pCtMainWin->get_ct_config();
        if (not pConfig->knowledgeGraphEnabled) {
            pConfig->knowledgeGraphEnabled = true;
            _pCtMainWin->search_index_enqueue_all(true/*force*/); // creates the chunks when the semantic index is off
        }
        _pCtMainWin->graph_extract_set_paused(false);
        _update_status();
    });
    _pauseButton.signal_clicked().connect([this](){ _pCtMainWin->graph_extract_set_paused(true); _update_status(); });
    _rebuildButton.signal_clicked().connect([this](){
        _pCtMainWin->get_ct_config()->knowledgeGraphEnabled = true;
        _pCtMainWin->graph_rebuild();
        refresh();
    });

    show_all_children();
}

void CtGraphPanel::focus_entry()
{
    _entry.grab_focus();
}

void CtGraphPanel::refresh()
{
    _reload_entities();
    _update_status();
}

void CtGraphPanel::on_node_changed()
{
    if (_nodeOnlyCheck.get_active()) _reload_entities();
}

/*static*/ Glib::ustring CtGraphPanel::_type_label(const Glib::ustring& type)
{
    if (type == "person") return _("person");
    if (type == "organization") return _("organization");
    if (type == "place") return _("place");
    if (type == "project") return _("project");
    if (type == "product") return _("product");
    if (type == "software") return _("software");
    if (type == "system") return _("system");
    if (type == "concept") return _("concept");
    if (type == "event") return _("event");
    if (type == "date") return _("date");
    if (type.empty() or type == "other") return _("other");
    return type;
}

/*static*/ void CtGraphPanel::_type_color(const Glib::ustring& type, double& r, double& g, double& b)
{
    struct C { const char* type; double r, g, b; };
    static const C colors[] = {
        {"person", 0.93, 0.47, 0.33}, {"organization", 0.52, 0.42, 0.80}, {"place", 0.30, 0.68, 0.50},
        {"project", 0.26, 0.58, 0.85}, {"product", 0.84, 0.60, 0.20}, {"software", 0.20, 0.64, 0.72},
        {"system", 0.45, 0.55, 0.64}, {"concept", 0.80, 0.42, 0.62}, {"event", 0.86, 0.36, 0.36}, {"date", 0.60, 0.60, 0.40}};
    for (const C& c : colors) {
        if (type == c.type) { r = c.r; g = c.g; b = c.b; return; }
    }
    r = 0.55; g = 0.55; b = 0.58;
}

void CtGraphPanel::_reload_entities()
{
    _reloading = true;
    _rEntities->clear();
    const CtSearchIndex* pIndex = _pCtMainWin->search_index();
    bool selected_still_listed{false};
    if (pIndex and pIndex->is_open()) {
        std::vector<CtGraphEntity> entities;
        if (_nodeOnlyCheck.get_active()) {
            CtTreeIter currIter = _pCtMainWin->curr_tree_iter();
            if (currIter) entities = pIndex->graph_entities_of_node(currIter.get_node_id());
            const std::string filter = CtSearchIndex::normalize_entity_name(_entry.get_text());
            if (not filter.empty()) {
                entities.erase(std::remove_if(entities.begin(), entities.end(), [&filter](const CtGraphEntity& e){
                    return CtSearchIndex::normalize_entity_name(e.name).find(filter) == std::string::npos;
                }), entities.end());
            }
        }
        else {
            entities = pIndex->graph_entities(_entry.get_text(), ENTITIES_LIMIT);
        }
        for (const CtGraphEntity& e : entities) {
            Gtk::TreeModel::Row row = *_rEntities->append();
            double r, g, b;
            _type_color(e.type, r, g, b);
            char color[16];
            g_snprintf(color, sizeof(color), "#%02x%02x%02x", static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255));
            Glib::ustring markup = "<span foreground='" + Glib::ustring{color} + "'>●</span> " + str::xml_escape(e.name);
            markup += "  <span size='small' alpha='60%'>" + str::xml_escape(_type_label(e.type)) + " · " + std::to_string(e.mentions) + "</span>";
            row[_entityColumns.markup] = markup;
            row[_entityColumns.entity_id] = e.entity_id;
            if (e.entity_id == _selectedEntity) {
                _entitiesView.get_selection()->select(row);
                selected_still_listed = true;
            }
        }
    }
    _reloading = false;
    if (selected_still_listed) _show_entity(_selectedEntity);
    else if (auto iter = _rEntities->children().begin()) {
        _entitiesView.get_selection()->select(iter);
        _show_entity(iter->get_value(_entityColumns.entity_id));
    }
    else _show_entity(0);
}

void CtGraphPanel::select_entity(const gint64 entity_id)
{
    bool listed{false};
    for (auto iter = _rEntities->children().begin(); iter; ++iter) {
        if (iter->get_value(_entityColumns.entity_id) == entity_id) {
            _entitiesView.get_selection()->select(iter);
            _entitiesView.scroll_to_row(_rEntities->get_path(iter));
            listed = true;
            break;
        }
    }
    if (not listed) {
        // filtered out: widen the list
        _selectedEntity = entity_id;
        _nodeOnlyCheck.set_active(false);
        _entry.set_text("");
        _reload_entities();
    }
}

void CtGraphPanel::_show_entity(const gint64 entity_id)
{
    _selectedEntity = entity_id;
    _neighbours.clear();
    _rDetail->clear();
    _selectedName.clear();
    _selectedType.clear();
    const CtSearchIndex* pIndex = _pCtMainWin->search_index();
    CtGraphEntity entity;
    if (entity_id > 0 and pIndex and pIndex->graph_entity(entity_id, entity)) {
        _selectedName = entity.name;
        _selectedType = entity.type;
        if (not entity.description.empty()) {
            Gtk::TreeModel::Row row = *_rDetail->append();
            row[_detailColumns.markup] = "<b>" + str::xml_escape(entity.name) + "</b> <span size='small' alpha='60%'>" + str::xml_escape(_type_label(entity.type)) + "</span>\n" + str::xml_escape(entity.description);
            row[_detailColumns.node_id] = 0;
            row[_detailColumns.entity_id] = 0;
        }
        const std::vector<CtGraphRelation> relations = pIndex->graph_relations_of(entity_id);
        if (not relations.empty()) {
            Gtk::TreeModel::Row header = *_rDetail->append();
            header[_detailColumns.markup] = "<span size='small'><b>" + Glib::ustring{_("Relations")} + "</b></span>";
            header[_detailColumns.node_id] = 0;
            header[_detailColumns.entity_id] = 0;
        }
        std::set<gint64> drawn;
        for (const CtGraphRelation& r : relations) {
            const bool outgoing = r.source_id == entity_id;
            const gint64 other_id = outgoing ? r.target_id : r.source_id;
            const Glib::ustring& other_name = outgoing ? r.target_name : r.source_name;
            Gtk::TreeModel::Row row = *_rDetail->append();
            Glib::ustring markup = outgoing
                ? "→ <i>" + str::xml_escape(r.type) + "</i> → <b>" + str::xml_escape(other_name) + "</b>"
                : "<b>" + str::xml_escape(other_name) + "</b> → <i>" + str::xml_escape(r.type) + "</i> →";
            if (not r.description.empty()) markup += "\n<span size='small' alpha='70%'>" + str::xml_escape(r.description) + "</span>";
            row[_detailColumns.markup] = markup;
            row[_detailColumns.node_id] = 0;
            row[_detailColumns.entity_id] = other_id;
            if (_neighbours.size() < NEIGHBOURS_MAX and drawn.insert(other_id).second) {
                CtGraphEntity other;
                Neighbour n;
                n.entity_id = other_id;
                n.name = other_name;
                n.type = pIndex->graph_entity(other_id, other) ? other.type : Glib::ustring{"other"};
                n.relation = r.type;
                n.outgoing = outgoing;
                _neighbours.push_back(n);
            }
        }
        const std::vector<std::pair<gint64, int>> nodes = pIndex->graph_nodes_of_entity(entity_id);
        if (not nodes.empty()) {
            Gtk::TreeModel::Row header = *_rDetail->append();
            header[_detailColumns.markup] = "<span size='small'><b>" + Glib::ustring{_("Mentioned in")} + "</b></span>";
            header[_detailColumns.node_id] = 0;
            header[_detailColumns.entity_id] = 0;
        }
        for (const auto& [node_id, count] : nodes) {
            CtTreeIter treeIter = _pCtMainWin->get_tree_store().get_node_from_node_id(node_id);
            if (not treeIter) continue;
            Gtk::TreeModel::Row row = *_rDetail->append();
            Glib::ustring markup = "▸ " + str::xml_escape(treeIter.get_node_name());
            const Glib::ustring path = CtMiscUtil::get_node_hierarchical_name(treeIter, " / ", false/*for_filename*/);
            const auto sep = path.rfind(" / ");
            if (sep != Glib::ustring::npos) markup += "  <span size='small' alpha='60%'>" + str::xml_escape(path.substr(0, sep)) + "</span>";
            row[_detailColumns.markup] = markup;
            row[_detailColumns.node_id] = node_id;
            row[_detailColumns.entity_id] = 0;
        }
    }
    _drawing.queue_draw();
}

bool CtGraphPanel::_on_draw(const Cairo::RefPtr<Cairo::Context>& cr)
{
    const Gtk::Allocation alloc = _drawing.get_allocation();
    const double W = alloc.get_width();
    const double H = alloc.get_height();
    auto style = get_style_context();
    const Gdk::RGBA fg = style->get_color(Gtk::STATE_FLAG_NORMAL);
    // halo behind the labels: the theme background is not reliable, derive it from the text colour
    const bool dark_text = (0.299 * fg.get_red() + 0.587 * fg.get_green() + 0.114 * fg.get_blue()) < 0.5;
    const double halo = dark_text ? 1.0 : 0.12;
    cr->set_line_width(1.0);
    auto layout = _drawing.create_pango_layout("");
    auto fit_text = [](const Glib::ustring& text, const size_t max){ return text.size() > max ? text.substr(0, max - 1) + "…" : text; };
    if (_selectedEntity <= 0 or _selectedName.empty()) {
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.5);
        layout->set_text(_("Select an entity to see its neighbourhood"));
        int tw, th;
        layout->get_pixel_size(tw, th);
        cr->move_to((W - tw) / 2, (H - th) / 2);
        layout->show_in_cairo_context(cr);
        return true;
    }
    const double cx = W / 2, cy = H / 2;
    const double radius = std::max(40.0, std::min(W, H) / 2 - 36);
    const size_t n = _neighbours.size();
    for (size_t i = 0; i < n; ++i) {
        const double angle = -M_PI / 2 + 2 * M_PI * static_cast<double>(i) / static_cast<double>(n);
        _neighbours[i].x = cx + radius * std::cos(angle);
        _neighbours[i].y = cy + radius * std::sin(angle);
    }
    // edges with the relation label
    for (const Neighbour& nb : _neighbours) {
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.35);
        cr->move_to(cx, cy);
        cr->line_to(nb.x, nb.y);
        cr->stroke();
        // arrow head at the target end
        const double tx = nb.outgoing ? nb.x : cx, ty = nb.outgoing ? nb.y : cy;
        const double fx = nb.outgoing ? cx : nb.x, fy = nb.outgoing ? cy : nb.y;
        const double ang = std::atan2(ty - fy, tx - fx);
        const double node_r = nb.outgoing ? 9.0 : 14.0;
        const double ax = tx - node_r * std::cos(ang), ay = ty - node_r * std::sin(ang);
        cr->move_to(ax, ay);
        cr->line_to(ax - 7 * std::cos(ang - 0.4), ay - 7 * std::sin(ang - 0.4));
        cr->line_to(ax - 7 * std::cos(ang + 0.4), ay - 7 * std::sin(ang + 0.4));
        cr->close_path();
        cr->fill();
        layout->set_markup("<span size='x-small'>" + str::xml_escape(fit_text(nb.relation, 18)) + "</span>");
        int tw, th;
        layout->get_pixel_size(tw, th);
        const double mx = (cx + nb.x) / 2, my = (cy + nb.y) / 2;
        cr->set_source_rgba(halo, halo, halo, 0.85);
        cr->rectangle(mx - tw / 2.0 - 2, my - th / 2.0, tw + 4, th);
        cr->fill();
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.8);
        cr->move_to(mx - tw / 2.0, my - th / 2.0);
        layout->show_in_cairo_context(cr);
    }
    // neighbour nodes
    for (const Neighbour& nb : _neighbours) {
        double r, g, b;
        _type_color(nb.type, r, g, b);
        cr->set_source_rgb(r, g, b);
        cr->arc(nb.x, nb.y, 8, 0, 2 * M_PI);
        cr->fill();
        layout->set_markup("<span size='small'>" + str::xml_escape(fit_text(nb.name, 22)) + "</span>");
        int tw, th;
        layout->get_pixel_size(tw, th);
        double lx = nb.x - tw / 2.0;
        lx = std::max(2.0, std::min(W - tw - 2.0, lx));
        const double ly = nb.y > cy ? nb.y + 10 : nb.y - 10 - th;
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.95);
        cr->move_to(lx, ly);
        layout->show_in_cairo_context(cr);
    }
    // the selected entity in the middle
    {
        double r, g, b;
        _type_color(_selectedType, r, g, b);
        cr->set_source_rgb(r, g, b);
        cr->arc(cx, cy, 13, 0, 2 * M_PI);
        cr->fill();
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.6);
        cr->arc(cx, cy, 13, 0, 2 * M_PI);
        cr->stroke();
        layout->set_markup("<b>" + str::xml_escape(fit_text(_selectedName, 26)) + "</b>");
        int tw, th;
        layout->get_pixel_size(tw, th);
        cr->set_source_rgba(halo, halo, halo, 0.85);
        cr->rectangle(cx - tw / 2.0 - 3, cy + 15, tw + 6, th);
        cr->fill();
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 1.0);
        cr->move_to(cx - tw / 2.0, cy + 15);
        layout->show_in_cairo_context(cr);
    }
    if (_neighbours.empty()) {
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.5);
        layout->set_markup("<span size='small'>" + Glib::ustring{_("no relations yet")} + "</span>");
        int tw, th;
        layout->get_pixel_size(tw, th);
        cr->move_to((W - tw) / 2, H - th - 4);
        layout->show_in_cairo_context(cr);
    }
    return true;
}

bool CtGraphPanel::_on_draw_click(GdkEventButton* pEvent)
{
    if (pEvent->button != 1) return false;
    for (const Neighbour& nb : _neighbours) {
        const double dx = pEvent->x - nb.x, dy = pEvent->y - nb.y;
        if (dx * dx + dy * dy <= 16 * 16) {
            select_entity(nb.entity_id);
            return true;
        }
    }
    return false;
}

void CtGraphPanel::_jump_to_node(const gint64 node_id)
{
    CtTreeIter treeIter = _pCtMainWin->get_tree_store().get_node_from_node_id(node_id);
    if (not treeIter) { _statusLabel.set_text(_("The node does not exist anymore.")); return; }
    _pCtMainWin->get_tree_view().set_cursor_safe(treeIter);
    // select the first occurrence of the entity name in the node
    auto rBuffer = _pCtMainWin->get_text_view().get_buffer();
    Gtk::TextIter match_start, match_end;
    if (rBuffer and not _selectedName.empty() and rBuffer->begin().forward_search(_selectedName, Gtk::TEXT_SEARCH_CASE_INSENSITIVE, match_start, match_end)) {
        rBuffer->select_range(match_start, match_end);
        _pCtMainWin->get_text_view().mm().scroll_to(match_start, CtTextView::TEXT_SCROLL_MARGIN);
    }
}

void CtGraphPanel::_update_status()
{
    const CtSearchIndex* pIndex = _pCtMainWin->search_index();
    const CtAiService* pService = _pCtMainWin->ai_service();
    const bool enabled = _pCtMainWin->get_ct_config()->knowledgeGraphEnabled;
    const bool running = _pCtMainWin->graph_extract_running();
    _pauseButton.set_sensitive(running);
    _buildButton.set_sensitive(not running);
    if (not pIndex or not pIndex->is_open()) {
        _statusLabel.set_text(_("The knowledge graph needs the search index: save the document first."));
        _buildButton.set_sensitive(false);
        _rebuildButton.set_sensitive(false);
        return;
    }
    _rebuildButton.set_sensitive(not running);
    Glib::ustring text = str::format(_("%s entities · %s relations"),
                                     std::to_string(pIndex->graph_count_entities()), std::to_string(pIndex->graph_count_relations()));
    const gint64 pending = pIndex->graph_count_pending();
    if (not pService or not pService->is_configured()) {
        text += Glib::ustring{" · "} + _("no generation model configured (Preferences → AI)");
    }
    else if (running) {
        text += " · " + str::format(_("reading the notes, %s chunks left"), std::to_string(pending));
    }
    else if (_pCtMainWin->get_ct_config()->knowledgeGraphPaused) {
        text += Glib::ustring{" · "} + _("paused (press Build or Tools → AI → Resume Knowledge Graph Extraction)");
    }
    else if (not enabled) {
        text += Glib::ustring{" · "} + _("press Build to extract the graph with the local model");
    }
    else if (pending > 0) {
        text += " · " + str::format(_("paused, %s chunks left"), std::to_string(pending));
    }
    else {
        text += Glib::ustring{" · "} + _("up to date");
    }
    _statusLabel.set_text(text);
}

#endif // GTKMM_MAJOR_VERSION < 4
