/*
 * ct_ai_panel.h
 *
 * Side panel showing the streamed output of the local AI and taking
 * questions about the current node.
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

class CtMainWin;

class CtAiPanel : public Gtk::Box
{
public:
    explicit CtAiPanel(CtMainWin* pCtMainWin);

    /// start showing a new answer (clears the output)
    void begin(const Glib::ustring& title);
    void append(const Glib::ustring& text);
    void finish(const bool ok, const Glib::ustring& error);
    void set_status(const Glib::ustring& text);
    void focus_question();
    /// 0 = this node, 1 = whole notebook
    void set_ask_scope(const int scope);
    int get_ask_scope() const;
    struct Source { int number{0}; gint64 node_id{0}; Glib::ustring label; Glib::ustring excerpt; };
    /// the excerpts the answer may cite, shown as clickable sources (cleared by begin())
    void set_sources(const std::vector<Source>& sources);
    Glib::ustring get_output_text() const;

private:
    void _on_ask();
    void _on_stop();
    void _on_copy();
    void _on_insert();
    void _on_new_subnode();
    void _update_buttons();

    CtMainWin* const      _pCtMainWin;
    Gtk::Box              _topBox{Gtk::ORIENTATION_HORIZONTAL, 4};
    Gtk::Label            _titleLabel;
    Gtk::Button           _stopButton;
    Gtk::Button           _closeButton;
    Gtk::Box              _askBox{Gtk::ORIENTATION_HORIZONTAL, 4};
    Gtk::ComboBoxText     _scopeCombo;
    Gtk::Entry            _questionEntry;
    Gtk::Button           _askButton;
    Gtk::Box              _sourcesBox{Gtk::ORIENTATION_VERTICAL, 2};
    std::vector<Source>   _sources;
    Gtk::ScrolledWindow   _scrolled;
    Gtk::TextView         _output;
    Gtk::Box              _actionBox{Gtk::ORIENTATION_HORIZONTAL, 4};
    Gtk::Button           _copyButton;
    Gtk::Button           _insertButton;
    Gtk::Button           _subnodeButton;
    Gtk::Label            _statusLabel;
    bool                  _running{false};
};
