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
#include "ct_ai_provider_openai.h"
#include "ct_misc_utils.h"
#include "ct_dialogs.h"
#include "ct_filesystem.h"

Gtk::Widget* CtPrefDlg::build_tab_ai()
{
    auto vbox_model = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_VERTICAL, 4/*spacing*/});
    auto label_intro = Gtk::manage(new Gtk::Label{});
    label_intro->set_markup(_("The AI runs <b>on this Mac only</b>: a GGUF model through llama.cpp (Metal), or the system model of Apple Intelligence (macOS 26). Nothing is sent over the network.\nThe selection, the current node or the retrieved excerpts are the only text the model sees."));
    label_intro->set_xalign(0.0);
    label_intro->set_line_wrap(true);

    // backend: local GGUF file or Apple Foundation Models
    auto hbox_backend = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 4/*spacing*/});
    auto label_backend = Gtk::manage(new Gtk::Label{_("Backend")});
    auto combo_backend = Gtk::manage(new Gtk::ComboBoxText{});
    combo_backend->append("llama", _("Local model file (llama.cpp, GGUF)"));
    combo_backend->append("apple", _("Apple Intelligence (Apple Foundation Models, macOS 26+)"));
    combo_backend->append("openai", _("API server (LiteLLM / OpenAI compatible, sends the text to the server)"));
    combo_backend->set_active_id(_pConfig->aiBackend);
    auto label_backend_status = Gtk::manage(new Gtk::Label{});
    label_backend_status->set_xalign(0.0);
    label_backend_status->set_line_wrap(true);
    label_backend_status->get_style_context()->add_class("dim-label");
    auto f_update_backend_status = [this, label_backend_status](){
        if (_pConfig->aiBackend == "apple") {
            std::string reason;
            const bool ok = CtAiService::apple_backend_available(reason);
            label_backend_status->set_text(Glib::ustring{ok ? _("Apple Intelligence: available") : _("Apple Intelligence: not available")} + (ok ? "" : " · " + reason));
        }
        else if (_pConfig->aiBackend == "openai") {
            label_backend_status->set_text(str::format(_("API server: %s · model: %s"),
                                                       CtAiProviderOpenAI::api_root(_pConfig->aiApiBaseUrl),
                                                       _pConfig->aiApiModel.empty() ? std::string{_("none")} : _pConfig->aiApiModel));
        }
        else {
            label_backend_status->set_text(_("llama.cpp: embedding models (semantic search) use llama.cpp too."));
        }
    };
    f_update_backend_status();
    label_backend->set_xalign(0.0);

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
    auto hbox_embed = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 4/*spacing*/});
    auto label_embed = Gtk::manage(new Gtk::Label{_("Embedding Model (GGUF)")});
    auto entry_embed = Gtk::manage(new Gtk::Entry{});
    entry_embed->set_text(_pConfig->aiEmbeddingModelPath);
    entry_embed->set_hexpand(true);
    entry_embed->set_placeholder_text(_("used by the semantic search index; pick one in the catalog and press Use This Model"));
    auto check_semantic = Gtk::manage(new Gtk::CheckButton{_("Keep a Semantic Search Index of the Document (embeds the notes in the background)")});
    check_semantic->set_active(_pConfig->semanticIndexEnabled);
    auto check_bg_pause = Gtk::manage(new Gtk::CheckButton{_("Pause the Semantic Index Embedding (local GPU work, the part that heats the Mac); keyword search and your own requests still run")});
    check_bg_pause->set_active(_pConfig->semanticIndexPaused);
    check_bg_pause->signal_toggled().connect([this, check_bg_pause](){
        apply_for_each_window([check_bg_pause](CtMainWin* win) { win->semantic_index_set_paused(check_bg_pause->get_active()); });
    });
    auto check_graph_pause = Gtk::manage(new Gtk::CheckButton{_("Pause the Knowledge Graph Extraction (generation model: API calls or local GPU)")});
    check_graph_pause->set_active(_pConfig->knowledgeGraphPaused);
    check_graph_pause->signal_toggled().connect([this, check_graph_pause](){
        apply_for_each_window([check_graph_pause](CtMainWin* win) { win->graph_extract_set_paused(check_graph_pause->get_active()); });
    });
    auto hbox_pace = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 4/*spacing*/});
    auto label_pace = Gtk::manage(new Gtk::Label{_("Background AI Pace")});
    auto combo_pace = Gtk::manage(new Gtk::ComboBoxText{});
    combo_pace->append(_("Full speed (fastest indexing, fans may spin)"));
    combo_pace->append(_("Balanced (works half of the time)"));
    combo_pace->append(_("Quiet (works a quarter of the time, lowest power)"));
    combo_pace->set_active(_pConfig->aiBackgroundPace);
    combo_pace->set_hexpand(true);
    combo_pace->signal_changed().connect([this, combo_pace](){ _pConfig->aiBackgroundPace = std::max(0, combo_pace->get_active_row_number()); });
    hbox_pace->pack_start(*label_pace, false, false);
    hbox_pace->pack_start(*combo_pace, true, true);
    auto check_graph = Gtk::manage(new Gtk::CheckButton{_("Build a Knowledge Graph of the Document (the generation model reads the notes in the background and extracts entities and relations; Tools → AI → Knowledge Graph)")});
    check_graph->set_active(_pConfig->knowledgeGraphEnabled);
    for (Gtk::CheckButton* pCheck : {check_semantic, check_graph, check_bg_pause, check_graph_pause}) {
        if (auto pLabel = dynamic_cast<Gtk::Label*>(pCheck->get_child())) { pLabel->set_line_wrap(true); pLabel->set_xalign(0.0); }
    }
    hbox_embed->pack_start(*label_embed, false, false);
    hbox_embed->pack_start(*entry_embed, true, true);
    hbox_model->pack_start(*label_model, false, false);
    hbox_model->pack_start(*entry_model, true, true);
    hbox_model->pack_start(*button_browse, false, false);
    hbox_backend->pack_start(*label_backend, false, false);
    hbox_backend->pack_start(*combo_backend, true, true);
    vbox_model->pack_start(*label_intro, false, false);
    vbox_model->pack_start(*hbox_backend, false, false);
    vbox_model->pack_start(*label_backend_status, false, false);
    vbox_model->pack_start(*hbox_model, false, false);
    vbox_model->pack_start(*label_model_info, false, false);
    vbox_model->pack_start(*hbox_embed, false, false);
    vbox_model->pack_start(*check_semantic, false, false);
    vbox_model->pack_start(*check_graph, false, false);
    vbox_model->pack_start(*check_bg_pause, false, false);
    vbox_model->pack_start(*check_graph_pause, false, false);
    vbox_model->pack_start(*hbox_pace, false, false);
    vbox_model->pack_start(*button_unload, false, false);
    check_graph->signal_toggled().connect([this, check_graph](){
        _pConfig->knowledgeGraphEnabled = check_graph->get_active();
        apply_for_each_window([](CtMainWin* win) {
            if (win->get_ct_config()->knowledgeGraphEnabled) { win->search_index_enqueue_all(true/*force*/); win->graph_extract_resume(); }
            else win->graph_extract_pause();
        });
    });
    entry_embed->signal_changed().connect([this, entry_embed](){
        _pConfig->aiEmbeddingModelPath = str::trim(entry_embed->get_text()).raw();
        apply_for_each_window([](CtMainWin* win) { if (win->ai_service()) { win->ai_service()->apply_settings(); win->semantic_index_kick(); } });
    });
    check_semantic->signal_toggled().connect([this, check_semantic](){
        _pConfig->semanticIndexEnabled = check_semantic->get_active();
        apply_for_each_window([](CtMainWin* win) { win->search_index_enqueue_all(true/*force*/); win->semantic_index_kick(); });
    });
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

    // ---- OpenAI compatible API server (LiteLLM proxy and the like)
    auto grid_api = Gtk::manage(new Gtk::Grid{});
    grid_api->set_row_spacing(4);
    grid_api->set_column_spacing(8);
    auto label_api_warn = Gtk::manage(new Gtk::Label{});
    label_api_warn->set_markup(_("<b>Not local:</b> with this backend the selection, the current node, the retrieved excerpts and (for the knowledge graph) every chunk of the document are sent to the server below. Use a server you trust, e.g. your own LiteLLM proxy. The API key is stored in the CherryTree config file."));
    label_api_warn->set_xalign(0.0);
    label_api_warn->set_line_wrap(true);
    auto label_api_url = Gtk::manage(new Gtk::Label{_("Server URL")});
    auto entry_api_url = Gtk::manage(new Gtk::Entry{});
    entry_api_url->set_text(_pConfig->aiApiBaseUrl);
    entry_api_url->set_placeholder_text("http://localhost:4000");
    entry_api_url->set_hexpand(true);
    auto label_api_key = Gtk::manage(new Gtk::Label{_("API Key")});
    auto entry_api_key = Gtk::manage(new Gtk::Entry{});
    entry_api_key->set_text(_pConfig->aiApiKey);
    entry_api_key->set_visibility(false);
    entry_api_key->set_placeholder_text(_("sk-… (empty if the server needs none)"));
    auto label_api_model = Gtk::manage(new Gtk::Label{_("Chat Model")});
    auto combo_api_model = Gtk::manage(new Gtk::ComboBoxText{true/*has_entry*/});
    combo_api_model->get_entry()->set_text(_pConfig->aiApiModel);
    combo_api_model->get_entry()->set_placeholder_text(_("model id, e.g. qwen-3.5-35b"));
    combo_api_model->set_hexpand(true);
    auto label_api_embed = Gtk::manage(new Gtk::Label{_("Embedding Model (optional)")});
    auto combo_api_embed = Gtk::manage(new Gtk::ComboBoxText{true/*has_entry*/});
    combo_api_embed->get_entry()->set_text(_pConfig->aiApiEmbeddingModel);
    combo_api_embed->get_entry()->set_placeholder_text(_("empty = semantic search keeps the local GGUF embedding model"));
    combo_api_embed->set_hexpand(true);
    auto button_api_refresh = Gtk::manage(new Gtk::Button{_("Fetch Models")});
    button_api_refresh->set_tooltip_text(_("Ask the server for its model list (GET /v1/models); also checks the URL and the key"));
    auto label_api_status = Gtk::manage(new Gtk::Label{});
    label_api_status->set_xalign(0.0);
    label_api_status->set_line_wrap(true);
    label_api_status->get_style_context()->add_class("dim-label");
    for (Gtk::Label* pLabel : {label_api_url, label_api_key, label_api_model, label_api_embed}) pLabel->set_xalign(0.0);
    grid_api->attach(*label_api_warn,     0, 0, 3, 1);
    grid_api->attach(*label_api_url,      0, 1, 1, 1); grid_api->attach(*entry_api_url,    1, 1, 1, 1); grid_api->attach(*button_api_refresh, 2, 1, 1, 1);
    grid_api->attach(*label_api_key,      0, 2, 1, 1); grid_api->attach(*entry_api_key,    1, 2, 2, 1);
    grid_api->attach(*label_api_model,    0, 3, 1, 1); grid_api->attach(*combo_api_model,  1, 3, 2, 1);
    grid_api->attach(*label_api_embed,    0, 4, 1, 1); grid_api->attach(*combo_api_embed,  1, 4, 2, 1);
    grid_api->attach(*label_api_status,   0, 5, 3, 1);
    Gtk::Frame* frame_api = new_managed_frame_with_align(_("API Server (LiteLLM / OpenAI compatible)"), grid_api);
    auto f_api_settings = [this]()->CtAiProviderOpenAI::Settings{
        CtAiProviderOpenAI::Settings s;
        s.base_url = _pConfig->aiApiBaseUrl;
        s.api_key = _pConfig->aiApiKey;
        s.proxy = _pConfig->proxyUrlColonPort;
        s.proxy_user = _pConfig->proxyUsername;
        s.proxy_password = _pConfig->proxyPassword;
        return s;
    };
    button_api_refresh->signal_clicked().connect([this, f_api_settings, combo_api_model, combo_api_embed, label_api_status](){
        label_api_status->set_text(_("Contacting the server…"));
        while (gtk_events_pending()) gtk_main_iteration();
        std::string error;
        const std::vector<std::string> models = CtAiProviderOpenAI::list_models(f_api_settings(), error);
        if (not error.empty()) { label_api_status->set_text(error); return; }
        const Glib::ustring chat = combo_api_model->get_entry()->get_text();
        const Glib::ustring embed = combo_api_embed->get_entry()->get_text();
        combo_api_model->remove_all();
        combo_api_embed->remove_all();
        for (const std::string& id : models) { combo_api_model->append(id); combo_api_embed->append(id); }
        combo_api_model->get_entry()->set_text(chat);
        combo_api_embed->get_entry()->set_text(embed);
        label_api_status->set_text(str::format(_("%s models available: %s"), std::to_string(models.size()), str::join(models, ", ")));
    });

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
            row[pColumns->size] = f_size_str(e.total_size());
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
        Glib::ustring parts_note;
        if (not pEntry->extra_files.empty()) parts_note = str::format(_(" (%s files)"), std::to_string(pEntry->all_files().size()));
        markup += f_size_str(pEntry->total_size()) + parts_note + memory_note + "\n";
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
        question += Glib::ustring{_("Size")} + ": " + f_size_str(pEntry->total_size()) + "\n";
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
    button_use->signal_clicked().connect([this, f_selected_entry, &manager, entry_model, entry_embed](){
        const CtModelEntry* pEntry = f_selected_entry();
        if (not pEntry) return;
        if (pEntry->is_embedding()) {
            _pConfig->aiEmbeddingPooling = pEntry->pooling;
            _pConfig->aiEmbeddingQueryPrefix = pEntry->query_prefix;
            entry_embed->set_text(manager.model_path(*pEntry).string());
        }
        else {
            entry_model->set_text(manager.model_path(*pEntry).string());
        }
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
    pMainBox->pack_start(*frame_api, false, false);
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
        if (_pConfig->aiBackend == "apple") {
            label_model_info->set_text(_("The GGUF file is not used while Apple Intelligence is the backend."));
        }
        else if (_pConfig->aiBackend == "openai") {
            label_model_info->set_text(_("The GGUF file is not used while the API server is the backend."));
        }
        else if (path.empty()) {
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
    auto f_apply_api = [this, f_apply, f_update_backend_status](){
        f_apply();
        f_update_backend_status();
        // the embedding source may have changed: the index is re-embedded when the model id differs
        apply_for_each_window([](CtMainWin* win) { win->semantic_index_kick(); });
    };
    combo_backend->signal_changed().connect([this, combo_backend, f_apply_api](){
        const Glib::ustring id = combo_backend->get_active_id();
        _pConfig->aiBackend = (id == "apple" or id == "openai") ? id.raw() : "llama";
        f_apply_api();
    });
    entry_api_url->signal_changed().connect([this, entry_api_url, f_apply_api](){ _pConfig->aiApiBaseUrl = str::trim(entry_api_url->get_text()).raw(); f_apply_api(); });
    entry_api_key->signal_changed().connect([this, entry_api_key, f_apply_api](){ _pConfig->aiApiKey = str::trim(entry_api_key->get_text()).raw(); f_apply_api(); });
    combo_api_model->get_entry()->signal_changed().connect([this, combo_api_model, f_apply_api](){ _pConfig->aiApiModel = str::trim(combo_api_model->get_entry()->get_text()).raw(); f_apply_api(); });
    combo_api_embed->get_entry()->signal_changed().connect([this, combo_api_embed, f_apply_api](){ _pConfig->aiApiEmbeddingModel = str::trim(combo_api_embed->get_entry()->get_text()).raw(); f_apply_api(); });
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
