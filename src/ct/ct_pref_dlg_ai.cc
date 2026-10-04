/*
 * ct_pref_dlg_ai.cc
 *
 * Preferences tab: local AI model and generation settings.
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

#include "ct_pref_dlg.h"
#include "ct_main_win.h"
#include "ct_ai_service.h"
#include "ct_misc_utils.h"
#include "ct_dialogs.h"
#include "ct_filesystem.h"

Gtk::Widget* CtPrefDlg::build_tab_ai()
{
    auto vbox_model = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_VERTICAL, 4/*spacing*/});
    auto label_intro = Gtk::manage(new Gtk::Label{});
    label_intro->set_markup(_("The AI runs <b>on this Mac only</b> (llama.cpp, Metal). Nothing is sent over the network.\nChoose a model in GGUF format; the selection or the current node is the only text the model sees."));
    label_intro->set_xalign(0.0);
    label_intro->set_line_wrap(true);

    auto hbox_model = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 4/*spacing*/});
    auto label_model = Gtk::manage(new Gtk::Label{_("Model File (GGUF)")});
    auto entry_model = Gtk::manage(new Gtk::Entry{});
    entry_model->set_text(_pConfig->aiModelPath);
    entry_model->set_hexpand(true);
    entry_model->set_placeholder_text(_("/path/to/model.gguf"));
    auto button_browse = Gtk::manage(new Gtk::Button{_("Browse…")});
    auto button_unload = Gtk::manage(new Gtk::Button{_("Unload Model Now")});
    auto label_model_info = Gtk::manage(new Gtk::Label{});
    label_model_info->set_xalign(0.0);
    label_model_info->get_style_context()->add_class("dim-label");
#if GTKMM_MAJOR_VERSION < 4
    hbox_model->pack_start(*label_model, false, false);
    hbox_model->pack_start(*entry_model, true, true);
    hbox_model->pack_start(*button_browse, false, false);
    vbox_model->pack_start(*label_intro, false, false);
    vbox_model->pack_start(*hbox_model, false, false);
    vbox_model->pack_start(*label_model_info, false, false);
    vbox_model->pack_start(*button_unload, false, false);
#else
    hbox_model->append(*label_model);
    hbox_model->append(*entry_model);
    hbox_model->append(*button_browse);
    vbox_model->append(*label_intro);
    vbox_model->append(*hbox_model);
    vbox_model->append(*label_model_info);
    vbox_model->append(*button_unload);
#endif
    Gtk::Frame* frame_model = new_managed_frame_with_align(_("Local Model"), vbox_model);

    auto grid_gen = Gtk::manage(new Gtk::Grid{});
    grid_gen->set_row_spacing(4);
    grid_gen->set_column_spacing(8);
    auto label_ctx = Gtk::manage(new Gtk::Label{_("Context Size (tokens)")});
    auto spin_ctx = Gtk::manage(new Gtk::SpinButton{Gtk::Adjustment::create(_pConfig->aiContextSize, 512, 131072, 512)});
    auto label_tokens = Gtk::manage(new Gtk::Label{_("Max Answer Length (tokens)")});
    auto spin_tokens = Gtk::manage(new Gtk::SpinButton{Gtk::Adjustment::create(_pConfig->aiMaxTokens, 64, 8192, 64)});
    auto label_temp = Gtk::manage(new Gtk::Label{_("Temperature")});
    auto spin_temp = Gtk::manage(new Gtk::SpinButton{Gtk::Adjustment::create(_pConfig->aiTemperature, 0.0, 2.0, 0.1), 0.1, 1});
    auto label_threads = Gtk::manage(new Gtk::Label{_("CPU Threads (0 = automatic)")});
    auto spin_threads = Gtk::manage(new Gtk::SpinButton{Gtk::Adjustment::create(_pConfig->aiThreads, 0, 128, 1)});
    auto label_unload = Gtk::manage(new Gtk::Label{_("Unload the Model After Idle Minutes (0 = never)")});
    auto spin_unload = Gtk::manage(new Gtk::SpinButton{Gtk::Adjustment::create(_pConfig->aiUnloadMinutes, 0, 1440, 1)});
    for (Gtk::Label* pLabel : {label_ctx, label_tokens, label_temp, label_threads, label_unload}) pLabel->set_xalign(0.0);
    grid_gen->attach(*label_ctx,     0, 0, 1, 1); grid_gen->attach(*spin_ctx,     1, 0, 1, 1);
    grid_gen->attach(*label_tokens,  0, 1, 1, 1); grid_gen->attach(*spin_tokens,  1, 1, 1, 1);
    grid_gen->attach(*label_temp,    0, 2, 1, 1); grid_gen->attach(*spin_temp,    1, 2, 1, 1);
    grid_gen->attach(*label_threads, 0, 3, 1, 1); grid_gen->attach(*spin_threads, 1, 3, 1, 1);
    grid_gen->attach(*label_unload,  0, 4, 1, 1); grid_gen->attach(*spin_unload,  1, 4, 1, 1);
    Gtk::Frame* frame_gen = new_managed_frame_with_align(_("Generation"), grid_gen);

    auto label_prompts = Gtk::manage(new Gtk::Label{});
    label_prompts->set_markup(str::format(_("Prompt templates are read from <tt>data/prompts/*.prompt</tt>; copies in <tt>%s</tt> override them."),
                                          str::xml_escape((fs::get_cherrytree_configdir() / "prompts").string())));
    label_prompts->set_xalign(0.0);
    label_prompts->set_line_wrap(true);
    Gtk::Frame* frame_prompts = new_managed_frame_with_align(_("Prompts"), label_prompts);

    auto pMainBox = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_VERTICAL, 3/*spacing*/});
    pMainBox->set_margin_start(6);
    pMainBox->set_margin_top(6);
#if GTKMM_MAJOR_VERSION < 4
    pMainBox->pack_start(*frame_model, false, false);
    pMainBox->pack_start(*frame_gen, false, false);
    pMainBox->pack_start(*frame_prompts, false, false);
#else
    pMainBox->append(*frame_model);
    pMainBox->append(*frame_gen);
    pMainBox->append(*frame_prompts);
#endif

    auto f_update_model_info = [this, label_model_info](){
        const std::string& path = _pConfig->aiModelPath;
        if (path.empty()) {
            label_model_info->set_text(_("No model configured."));
        }
        else if (not fs::is_regular_file(path)) {
            label_model_info->set_text(_("The model file does not exist."));
        }
        else {
            const double gb = static_cast<double>(fs::file_size(path)) / (1024.0 * 1024.0 * 1024.0);
            const bool loaded = _pCtMainWin->ai_service() and _pCtMainWin->ai_service()->is_model_loaded();
            label_model_info->set_text(str::format("%s GB · %s", str::format("%.2f", gb), loaded ? _("loaded") : _("not loaded")));
        }
    };
    f_update_model_info();

    auto f_apply = [this, f_update_model_info](){
        apply_for_each_window([](CtMainWin* win) { if (win->ai_service()) win->ai_service()->apply_settings(); });
        f_update_model_info();
    };
    entry_model->signal_changed().connect([this, entry_model, f_apply](){
        _pConfig->aiModelPath = str::trim(entry_model->get_text()).raw();
        f_apply();
    });
    button_browse->signal_clicked().connect([this, entry_model](){
        CtDialogs::CtFileSelectArgs args{};
        args.curr_folder = fs::path{_pConfig->aiModelPath}.parent_path().string();
        args.filter_name = _("GGUF model");
        args.filter_pattern = {"*.gguf"};
        const std::string filepath = CtDialogs::file_select_dialog(_pCtMainWin, args);
        if (not filepath.empty()) entry_model->set_text(filepath);
    });
    button_unload->signal_clicked().connect([this, f_update_model_info](){
        apply_for_each_window([](CtMainWin* win) { if (win->ai_service()) win->ai_service()->unload_model(); });
        f_update_model_info();
    });
    spin_ctx->signal_value_changed().connect([this, spin_ctx, f_apply](){ _pConfig->aiContextSize = spin_ctx->get_value_as_int(); f_apply(); });
    spin_tokens->signal_value_changed().connect([this, spin_tokens](){ _pConfig->aiMaxTokens = spin_tokens->get_value_as_int(); });
    spin_temp->signal_value_changed().connect([this, spin_temp](){ _pConfig->aiTemperature = spin_temp->get_value(); });
    spin_threads->signal_value_changed().connect([this, spin_threads, f_apply](){ _pConfig->aiThreads = spin_threads->get_value_as_int(); f_apply(); });
    spin_unload->signal_value_changed().connect([this, spin_unload](){ _pConfig->aiUnloadMinutes = spin_unload->get_value_as_int(); });

    return pMainBox;
}
