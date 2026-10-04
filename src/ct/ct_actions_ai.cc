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
        CtDialogs::info_dialog(_("No AI model is configured yet.\n\nChoose a GGUF model file in Preferences → AI (Local Model)."), *_pCtMainWin);
        return false;
    }
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
    const bool started = pService->run(request,
        [pPanel](const std::string& piece){ pPanel->append(piece); },
        [pPanel](const bool ok, const std::string& error){ pPanel->finish(ok, error); });
    if (not started) {
        pPanel->finish(false, _("The AI is busy."));
    }
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
