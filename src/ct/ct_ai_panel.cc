/*
 * ct_ai_panel.cc
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

#include "ct_ai_panel.h"
#include "ct_ai_service.h"
#include "ct_main_win.h"
#include "ct_actions.h"
#include "ct_misc_utils.h"
#include "ct_treestore.h"
#include <algorithm>

#if GTKMM_MAJOR_VERSION < 4

CtAiPanel::CtAiPanel(CtMainWin* pCtMainWin)
 : Gtk::Box{Gtk::ORIENTATION_VERTICAL, 4}
 , _pCtMainWin{pCtMainWin}
{
    get_style_context()->add_class("ct-ai-panel");
    set_size_request(320, -1);
    set_margin_start(4);
    set_margin_end(4);
    set_margin_top(4);

    _titleLabel.set_xalign(0.0);
    _titleLabel.set_hexpand(true);
    _titleLabel.set_ellipsize(Pango::ELLIPSIZE_END);
    _titleLabel.set_markup("<b>" + Glib::ustring{_("AI")} + "</b>");
    _stopButton.set_image_from_icon_name("process-stop-symbolic", Gtk::ICON_SIZE_MENU);
    _stopButton.set_relief(Gtk::RELIEF_NONE);
    _stopButton.set_tooltip_text(_("Stop the generation"));
    _closeButton.set_image_from_icon_name("window-close-symbolic", Gtk::ICON_SIZE_MENU);
    _closeButton.set_relief(Gtk::RELIEF_NONE);
    _closeButton.set_tooltip_text(_("Close the AI panel"));
    _topBox.pack_start(_titleLabel, true, true);
    _topBox.pack_start(_stopButton, false, false);
    _topBox.pack_start(_closeButton, false, false);

    _scopeCombo.append(_("This Node"));
    _scopeCombo.append(_("Notebook"));
    _scopeCombo.set_active(0);
    _scopeCombo.set_tooltip_text(_("This Node: the model only sees the current node. Notebook: the most relevant excerpts of the whole document are retrieved first (search index), the answer cites them."));
    _questionEntry.set_placeholder_text(_("Ask a question…"));
    _questionEntry.set_hexpand(true);
    _askButton.set_label(_("Ask"));
    _askBox.pack_start(_scopeCombo, false, false);
    _askBox.pack_start(_questionEntry, true, true);
    _askBox.pack_start(_askButton, false, false);
    _sourcesBox.set_no_show_all(true);

    _output.set_editable(false);
    _output.set_cursor_visible(false);
    _output.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
    _output.set_left_margin(6);
    _output.set_right_margin(6);
    _output.set_top_margin(6);
    _output.set_bottom_margin(6);
    _output.get_style_context()->add_class("ct-ai-output");
    _scrolled.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    _scrolled.add(_output);

    _copyButton.set_label(_("Copy"));
    _insertButton.set_label(_("Insert into Node"));
    _subnodeButton.set_label(_("New Subnode"));
    _actionBox.pack_start(_copyButton, true, true);
    _actionBox.pack_start(_insertButton, true, true);
    _actionBox.pack_start(_subnodeButton, true, true);

    _statusLabel.set_xalign(0.0);
    _statusLabel.set_ellipsize(Pango::ELLIPSIZE_END);
    _statusLabel.get_style_context()->add_class("dim-label");

    pack_start(_topBox, false, false);
    pack_start(_askBox, false, false);
    pack_start(_sourcesBox, false, false);
    pack_start(_scrolled, true, true);
    pack_start(_actionBox, false, false);
    pack_start(_statusLabel, false, false);

    _askButton.signal_clicked().connect(sigc::mem_fun(*this, &CtAiPanel::_on_ask));
    _questionEntry.signal_activate().connect(sigc::mem_fun(*this, &CtAiPanel::_on_ask));
    _questionEntry.signal_key_press_event().connect([this](GdkEventKey* pEvent){
        if (GDK_KEY_Escape == pEvent->keyval) { _pCtMainWin->ai_panel_show(false); return true; }
        return false;
    }, false);
    _stopButton.signal_clicked().connect(sigc::mem_fun(*this, &CtAiPanel::_on_stop));
    _closeButton.signal_clicked().connect([this](){ _pCtMainWin->ai_panel_show(false); });
    _copyButton.signal_clicked().connect(sigc::mem_fun(*this, &CtAiPanel::_on_copy));
    _insertButton.signal_clicked().connect(sigc::mem_fun(*this, &CtAiPanel::_on_insert));
    _subnodeButton.signal_clicked().connect(sigc::mem_fun(*this, &CtAiPanel::_on_new_subnode));

    show_all_children();
    _update_buttons();
}

void CtAiPanel::_update_buttons()
{
    const bool has_text = _output.get_buffer()->size() > 0;
    _stopButton.set_sensitive(_running);
    _askButton.set_sensitive(not _running);
    _copyButton.set_sensitive(has_text and not _running);
    _insertButton.set_sensitive(has_text and not _running);
    _subnodeButton.set_sensitive(has_text and not _running);
}

void CtAiPanel::begin(const Glib::ustring& title)
{
    _titleLabel.set_markup("<b>" + str::xml_escape(title) + "</b>");
    _output.get_buffer()->set_text("");
    set_sources({});
    _running = true;
    set_status(_("Thinking…"));
    _update_buttons();
}

void CtAiPanel::append(const Glib::ustring& text)
{
    auto rBuffer = _output.get_buffer();
    rBuffer->insert(rBuffer->end(), text);
    // keep the end visible while streaming
    Gtk::TextIter end = rBuffer->end();
    _output.scroll_to(end);
    _update_buttons();
}

void CtAiPanel::finish(const bool ok, const Glib::ustring& error)
{
    _running = false;
    if (ok) {
        const CtAiService* pService = _pCtMainWin->ai_service();
        if (pService) set_status(str::format(_("Done · %s"), pService->model_name()));
        else set_status(_("Done"));
    }
    else {
        set_status(error.empty() ? Glib::ustring{_("Stopped")} : error);
    }
    _update_buttons();
}

void CtAiPanel::set_status(const Glib::ustring& text)
{
    _statusLabel.set_text(text);
}

void CtAiPanel::focus_question()
{
    _questionEntry.grab_focus();
}

Glib::ustring CtAiPanel::get_output_text() const
{
    return _output.get_buffer()->get_text();
}

void CtAiPanel::_on_ask()
{
    const Glib::ustring question = str::trim(_questionEntry.get_text());
    if (question.empty()) return;
    if (1 == get_ask_scope()) _pCtMainWin->get_ct_actions()->ai_ask_notebook_question(question);
    else _pCtMainWin->get_ct_actions()->ai_ask_node_question(question);
}

void CtAiPanel::set_ask_scope(const int scope)
{
    _scopeCombo.set_active(scope);
}

int CtAiPanel::get_ask_scope() const
{
    return std::max(0, _scopeCombo.get_active_row_number());
}

void CtAiPanel::set_sources(const std::vector<Source>& sources)
{
    _sources = sources;
    for (Gtk::Widget* pChild : _sourcesBox.get_children()) _sourcesBox.remove(*pChild);
    if (_sources.empty()) {
        _sourcesBox.hide();
        return;
    }
    auto pTitle = Gtk::manage(new Gtk::Label{});
    pTitle->set_markup("<small><b>" + Glib::ustring{_("Sources")} + "</b></small>");
    pTitle->set_xalign(0.0);
    _sourcesBox.pack_start(*pTitle, false, false);
    for (const Source& source : _sources) {
        auto pButton = Gtk::manage(new Gtk::Button{});
        auto pLabel = Gtk::manage(new Gtk::Label{});
        pLabel->set_markup("<small>[" + std::to_string(source.number) + "] " + str::xml_escape(source.label) + "</small>");
        pLabel->set_xalign(0.0);
        pLabel->set_ellipsize(Pango::ELLIPSIZE_END);
        pButton->add(*pLabel);
        pButton->set_relief(Gtk::RELIEF_NONE);
        pButton->set_halign(Gtk::ALIGN_FILL);
        pButton->set_tooltip_text(source.excerpt);
        const gint64 node_id = source.node_id;
        const Glib::ustring excerpt = source.excerpt;
        pButton->signal_clicked().connect([this, node_id, excerpt](){
            CtTreeIter treeIter = _pCtMainWin->get_tree_store().get_node_from_node_id(node_id);
            if (not treeIter) { set_status(_("The source node does not exist anymore.")); return; }
            _pCtMainWin->get_tree_view().set_cursor_safe(treeIter);
            // select the first line of the excerpt in the node text
            auto rBuffer = _pCtMainWin->get_text_view().get_buffer();
            const auto nl = excerpt.find('\n');
            Glib::ustring needle = str::trim(nl == Glib::ustring::npos ? excerpt : excerpt.substr(0, nl));
            if (needle.size() > 60) needle = needle.substr(0, 60);
            Gtk::TextIter match_start, match_end;
            if (rBuffer and not needle.empty() and rBuffer->begin().forward_search(needle, Gtk::TEXT_SEARCH_CASE_INSENSITIVE, match_start, match_end)) {
                rBuffer->select_range(match_start, match_end);
                _pCtMainWin->get_text_view().mm().scroll_to(match_start, CtTextView::TEXT_SCROLL_MARGIN);
            }
        });
        _sourcesBox.pack_start(*pButton, false, false);
        pButton->show_all();
    }
    pTitle->show();
    _sourcesBox.show();
}

void CtAiPanel::_on_stop()
{
    if (CtAiService* pService = _pCtMainWin->ai_service()) pService->cancel();
}

void CtAiPanel::_on_copy()
{
    Gtk::Clipboard::get()->set_text(get_output_text());
    set_status(_("Copied to the clipboard"));
}

void CtAiPanel::_on_insert()
{
    _pCtMainWin->get_ct_actions()->ai_insert_text_in_node(get_output_text());
}

void CtAiPanel::_on_new_subnode()
{
    _pCtMainWin->get_ct_actions()->ai_new_subnode_with_text(_titleLabel.get_text(), get_output_text());
}

#endif // GTKMM_MAJOR_VERSION < 4
