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
#include "ct_ai_models.h"
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

    // ---- model catalog: one click download with resume and SHA256 verification
    CtAiService* pService = _pCtMainWin->ai_service();
    CtModelManager& manager = pService->model_manager();
    auto vbox_catalog = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_VERTICAL, 4/*spacing*/});
    auto label_machine = Gtk::manage(new Gtk::Label{});
    {
        const double gb = static_cast<double>(CtModelManager::system_memory_bytes()) / (1024.0 * 1024.0 * 1024.0);
        label_machine->set_markup(str::format(_("This Mac: <b>%s GB</b> unified memory · recommended tier: <b>%s</b> · models are stored in <tt>%s</tt>"),
                                              fmt::format("{:.0f}", gb), CtModelManager::recommended_tier(),
                                              str::xml_escape(CtModelManager::models_dir().string())));
    }
    label_machine->set_xalign(0.0);
    label_machine->set_line_wrap(true);
    struct CatalogColumns : public Gtk::TreeModelColumnRecord {
        Gtk::TreeModelColumn<Glib::ustring> name, tier, size, status, id;
        CatalogColumns() { add(name); add(tier); add(size); add(status); add(id); }
    };
    auto pColumns = std::make_shared<CatalogColumns>();
    auto rStore = Gtk::ListStore::create(*pColumns);
    auto pTreeView = Gtk::manage(new Gtk::TreeView{rStore});
    pTreeView->append_column(_("Model"), pColumns->name);
    pTreeView->append_column(_("Tier"), pColumns->tier);
    pTreeView->append_column(_("Size"), pColumns->size);
    pTreeView->append_column(_("Status"), pColumns->status);
    pTreeView->set_size_request(-1, 110);
    auto pScrolled = Gtk::manage(new Gtk::ScrolledWindow{});
    pScrolled->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    pScrolled->add(*pTreeView);
    auto label_detail = Gtk::manage(new Gtk::Label{});
    label_detail->set_xalign(0.0);
    label_detail->set_line_wrap(true);
    label_detail->get_style_context()->add_class("dim-label");
    auto hbox_catalog_buttons = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 4/*spacing*/});
    auto button_download = Gtk::manage(new Gtk::Button{_("Download")});
    auto button_cancel = Gtk::manage(new Gtk::Button{_("Cancel Download")});
    auto button_use = Gtk::manage(new Gtk::Button{_("Use This Model")});
    auto button_delete = Gtk::manage(new Gtk::Button{_("Delete")});
    auto progress_bar = Gtk::manage(new Gtk::ProgressBar{});
    progress_bar->set_show_text(true);
    auto label_download_status = Gtk::manage(new Gtk::Label{});
    label_download_status->set_xalign(0.0);
    label_download_status->set_line_wrap(true);
    hbox_catalog_buttons->pack_start(*button_download, false, false);
    hbox_catalog_buttons->pack_start(*button_cancel, false, false);
    hbox_catalog_buttons->pack_start(*button_use, false, false);
    hbox_catalog_buttons->pack_start(*button_delete, false, false);
    vbox_catalog->pack_start(*label_machine, false, false);
    vbox_catalog->pack_start(*pScrolled, true, true);
    vbox_catalog->pack_start(*label_detail, false, false);
    vbox_catalog->pack_start(*hbox_catalog_buttons, false, false);
    vbox_catalog->pack_start(*progress_bar, false, false);
    vbox_catalog->pack_start(*label_download_status, false, false);
    Gtk::Frame* frame_catalog = new_managed_frame_with_align(_("Model Catalog (official publisher releases, SHA256 verified)"), vbox_catalog);

    auto f_size_str = [](const uint64_t bytes)->Glib::ustring{
        if (0 == bytes) return _("unknown");
        return str::format("%s GB", fmt::format("{:.2f}", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)));
    };
    auto f_status_str = [&manager, f_size_str](const CtModelEntry& e)->Glib::ustring{
        switch (manager.state(e)) {
            case CtModelState::Installed: return _("installed");
            case CtModelState::Downloading: return _("downloading…");
            case CtModelState::Partial: return str::format(_("partial (%s), resumable"), f_size_str(manager.partial_bytes(e)).raw());
            default: return _("not installed");
        }
    };
    auto f_selected_entry = [pTreeView, pColumns, &manager]()->const CtModelEntry*{
        auto iter = pTreeView->get_selection()->get_selected();
        if (not iter) return nullptr;
        return manager.entry(Glib::ustring{iter->get_value(pColumns->id)}.raw());
    };
    auto f_refresh_list = [&manager, rStore, pColumns, f_size_str, f_status_str, pTreeView](){
        std::string selected_id;
        if (auto iter = pTreeView->get_selection()->get_selected()) selected_id = Glib::ustring{iter->get_value(pColumns->id)}.raw();
        rStore->clear();
        const std::string recommended = CtModelManager::recommended_tier();
        for (const CtModelEntry& e : manager.catalog()) {
            Gtk::TreeModel::Row row = *rStore->append();
            row[pColumns->name] = e.name;
            row[pColumns->tier] = e.tier == recommended ? e.tier + " ★" : e.tier;
            row[pColumns->size] = f_size_str(e.size_bytes);
            row[pColumns->status] = f_status_str(e);
            row[pColumns->id] = e.id;
            if (e.id == selected_id) pTreeView->get_selection()->select(row);
        }
    };
    auto f_update_buttons = [f_selected_entry, &manager, button_download, button_cancel, button_use, button_delete, label_detail, f_size_str](){
        const CtModelEntry* pEntry = f_selected_entry();
        const bool downloading = manager.is_downloading();
        if (not pEntry) {
            button_download->set_sensitive(false);
            button_cancel->set_sensitive(downloading);
            button_use->set_sensitive(false);
            button_delete->set_sensitive(false);
            label_detail->set_text("");
            return;
        }
        const CtModelState st = manager.state(*pEntry);
        button_download->set_sensitive(not downloading and st != CtModelState::Installed);
        button_cancel->set_sensitive(downloading and manager.downloading_id() == pEntry->id);
        button_use->set_sensitive(st == CtModelState::Installed);
        button_delete->set_sensitive(not downloading and st != CtModelState::NotInstalled);
        const double gb = static_cast<double>(CtModelManager::system_memory_bytes()) / (1024.0 * 1024.0 * 1024.0);
        Glib::ustring memory_note;
        if (pEntry->min_memory_gb > 0 and gb + 0.5 < pEntry->min_memory_gb) {
            memory_note = str::format(_(" ⚠ needs about %s GB of memory"), std::to_string(pEntry->min_memory_gb));
        }
        Glib::ustring hash_note{_("not pinned in the catalog: the publisher's Hugging Face LFS hash is used")};
        if (pEntry->hash_pinned()) hash_note = str::xml_escape(pEntry->sha256);
        Glib::ustring markup = str::xml_escape(pEntry->description) + "\n";
        markup += Glib::ustring{_("Source")} + ": <a href='" + str::xml_escape(pEntry->repo_url()) + "'>" + str::xml_escape(pEntry->repo) + "</a> · ";
        markup += Glib::ustring{_("License")} + ": <a href='" + str::xml_escape(pEntry->license_url) + "'>" + str::xml_escape(pEntry->license) + "</a> · ";
        markup += f_size_str(pEntry->size_bytes) + memory_note + "\n";
        markup += Glib::ustring{_("SHA256")} + ": " + hash_note;
        label_detail->set_markup(markup);
    };
    f_refresh_list();
    f_update_buttons();
    pTreeView->get_selection()->signal_changed().connect(f_update_buttons);

    auto f_progress = [progress_bar, label_download_status, f_size_str](const CtModelProgress& p){
        if (p.total > 0) {
            progress_bar->set_fraction(std::min(1.0, static_cast<double>(p.done) / static_cast<double>(p.total)));
            progress_bar->set_text(f_size_str(p.done) + " / " + f_size_str(p.total));
        }
        else {
            progress_bar->pulse();
            progress_bar->set_text(f_size_str(p.done));
        }
        Glib::ustring speed;
        if (p.bytes_per_sec > 0) speed = str::format(" · %s MB/s", fmt::format("{:.1f}", p.bytes_per_sec / (1024.0 * 1024.0)));
        if (p.phase == "verifying") label_download_status->set_text(_("Verifying SHA256…"));
        else label_download_status->set_text(Glib::ustring{_("Downloading…")} + speed);
    };
    auto f_done = [progress_bar, label_download_status, f_refresh_list, f_update_buttons](const bool ok, const std::string& message){
        progress_bar->set_fraction(ok ? 1.0 : 0.0);
        progress_bar->set_text(ok ? _("done") : "");
        label_download_status->set_text(message);
        f_refresh_list();
        f_update_buttons();
    };
    manager.set_callbacks(f_progress, f_done);
    if (manager.is_downloading()) f_progress(manager.last_progress());
    else if (not manager.last_message().empty()) label_download_status->set_text(manager.last_message());
    // the dialog is modal and short lived, the download is not: detach on close
    signal_hide().connect([&manager](){ manager.set_callbacks(nullptr, nullptr); });

    button_download->signal_clicked().connect([this, f_selected_entry, &manager, f_size_str, f_refresh_list, f_update_buttons, label_download_status](){
        const CtModelEntry* pEntry = f_selected_entry();
        if (not pEntry) return;
        Glib::ustring question = "<b>" + str::xml_escape(pEntry->name) + "</b>\n\n";
        question += Glib::ustring{_("Publisher")} + ": " + str::xml_escape(pEntry->publisher) + "\n";
        question += Glib::ustring{_("Download from")} + ": " + str::xml_escape(pEntry->download_url()) + "\n";
        question += Glib::ustring{_("License")} + ": " + str::xml_escape(pEntry->license) + "\n";
        question += Glib::ustring{_("Size")} + ": " + f_size_str(pEntry->size_bytes) + "\n";
        question += Glib::ustring{_("Verification")} + ": " + (pEntry->hash_pinned() ? _("SHA256 pinned in the catalog") : _("SHA256 from the publisher's Hugging Face LFS metadata")) + "\n\n";
        question += _("Download this file now? The model is only used after the SHA256 matches.");
        if (not CtDialogs::question_dialog(question, *this)) return;
        std::string error;
        if (not manager.start_download(*pEntry, error)) {
            CtDialogs::error_dialog(str::xml_escape(error), *this);
            return;
        }
        label_download_status->set_text(_("Starting…"));
        f_refresh_list();
        f_update_buttons();
    });
    button_cancel->signal_clicked().connect([&manager](){ manager.cancel_download(); });
    button_use->signal_clicked().connect([f_selected_entry, &manager, entry_model](){
        const CtModelEntry* pEntry = f_selected_entry();
        if (pEntry) entry_model->set_text(manager.model_path(*pEntry).string());
    });
    button_delete->signal_clicked().connect([this, f_selected_entry, &manager, f_refresh_list, f_update_buttons, entry_model](){
        const CtModelEntry* pEntry = f_selected_entry();
        if (not pEntry) return;
        if (not CtDialogs::question_dialog(str::format(_("Delete the model file %s from this Mac?"), str::xml_escape(pEntry->file)), *this)) return;
        std::string error;
        if (not manager.delete_model(*pEntry, error)) CtDialogs::error_dialog(str::xml_escape(error), *this);
        if (entry_model->get_text() == manager.model_path(*pEntry).string()) entry_model->set_text("");
        f_refresh_list();
        f_update_buttons();
    });

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
    pMainBox->pack_start(*frame_catalog, true, true);
    pMainBox->pack_start(*frame_model, false, false);
    pMainBox->pack_start(*frame_gen, false, false);
    pMainBox->pack_start(*frame_prompts, false, false);
#else
    pMainBox->append(*frame_catalog);
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
            label_model_info->set_text(str::format("%s GB · %s", fmt::format("{:.2f}", gb), loaded ? _("loaded") : _("not loaded")));
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
