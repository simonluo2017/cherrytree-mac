/*
 * ct_main.cc
 *
 * Copyright 2009-2024
 * Giuseppe Penone <giuspen@gmail.com>
 * Evgenii Gurianov <https://github.com/txe>
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

#include "ct_app.h"
#include "ct_misc_utils.h"
#include "config.h"
#include "ct_logging.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#if defined(_WIN32)
#include <locale>
#include <codecvt>
#endif /* _WIN32 */

void glib_log_handler(const gchar*/*log_domain*/, GLogLevelFlags log_level, const gchar* message, gpointer user_data)
{
    if (not message or not user_data) {
        return;
    }
    auto pGtkLogger = static_cast<spdlog::logger*>(user_data);
    switch (log_level) {
        case G_LOG_LEVEL_ERROR:    pGtkLogger->error(message);    break;
        case G_LOG_LEVEL_CRITICAL: pGtkLogger->critical(message); break;
        case G_LOG_LEVEL_WARNING:  pGtkLogger->warn(message);     break;
        case G_LOG_LEVEL_MESSAGE:  pGtkLogger->info(message);     break;
        case G_LOG_LEVEL_INFO:     pGtkLogger->info(message);     break;
        case G_LOG_LEVEL_DEBUG:
            // disable due to excessive output
            //pGtkLogger->debug(msg);
            break;
        default:                   pGtkLogger->info(message);
    }
}

// On macOS (Homebrew/MacPorts) the compiled GSettings schemas of GTK, needed by the file
// chooser, are often not found: the binary runs from the build directory, the Homebrew prefix
// is not in the XDG data dirs, or the schemas were never compiled. GLib then prints
// "No GSettings schemas are installed on the system" and aborts (trace trap) the first time a
// file dialog is opened. This points GSETTINGS_SCHEMA_DIR to a usable compiled schemas
// directory, compiling the schemas into the user config directory when needed.
// It must run before the first use of GSettings (the default schema source is cached).
[[maybe_unused]] static void ensure_gsettings_schemas_available()
{
    static const char* const requiredSchema{"org.gtk.Settings.FileChooser"};
    if (not Glib::getenv("GSETTINGS_SCHEMA_DIR").empty()) {
        return;
    }
    auto f_dir_provides_schema = [](const std::string& dir)->bool{
        if (not Glib::file_test(Glib::build_filename(dir, "gschemas.compiled"), Glib::FILE_TEST_EXISTS)) {
            return false;
        }
        GError* pError{nullptr};
        GSettingsSchemaSource* pSource = g_settings_schema_source_new_from_directory(dir.c_str(), nullptr, TRUE, &pError);
        if (pError) {
            g_error_free(pError);
            return false;
        }
        if (not pSource) {
            return false;
        }
        GSettingsSchema* pSchema = g_settings_schema_source_lookup(pSource, requiredSchema, FALSE);
        const bool found = nullptr != pSchema;
        if (pSchema) g_settings_schema_unref(pSchema);
        g_settings_schema_source_unref(pSource);
        return found;
    };
    // 1) the directories GLib searches by itself
    for (const std::string& dataDir : Glib::get_system_data_dirs()) {
        if (f_dir_provides_schema(Glib::build_filename(dataDir, "glib-2.0", "schemas"))) {
            return; // nothing to do
        }
    }
    // 2) the usual package manager prefixes on macOS
    const std::vector<std::string> candidateDirs{
        "/opt/homebrew/share/glib-2.0/schemas",
        "/usr/local/share/glib-2.0/schemas",
        "/opt/local/share/glib-2.0/schemas"};
    for (const std::string& dir : candidateDirs) {
        if (f_dir_provides_schema(dir)) {
            Glib::setenv("GSETTINGS_SCHEMA_DIR", dir, true/*overwrite*/);
            g_message("GSETTINGS_SCHEMA_DIR = %s", dir.c_str());
            return;
        }
    }
    // 3) schemas installed but not compiled: compile them into the user config directory
    for (const std::string& dir : candidateDirs) {
        if (not Glib::file_test(Glib::build_filename(dir, std::string{requiredSchema} + ".gschema.xml"), Glib::FILE_TEST_EXISTS)) {
            continue;
        }
        const std::string targetDir = Glib::build_filename(fs::get_cherrytree_configdir().string(), "gsettings-schemas");
        if (g_mkdir_with_parents(targetDir.c_str(), 0755) < 0) {
            continue;
        }
        std::string compiler{"glib-compile-schemas"};
        const std::string compilerInPrefix = Glib::build_filename(dir, "..", "..", "..", "bin", "glib-compile-schemas");
        if (Glib::file_test(compilerInPrefix, Glib::FILE_TEST_IS_EXECUTABLE)) {
            compiler = compilerInPrefix;
        }
        std::string std_out, std_err;
        int exit_status{-1};
        try {
            Glib::spawn_command_line_sync(Glib::shell_quote(compiler) + " --targetdir=" + Glib::shell_quote(targetDir) + " " + Glib::shell_quote(dir),
                                          &std_out, &std_err, &exit_status);
        }
        catch (Glib::Error& error) {
            g_warning("%s: %s", compiler.c_str(), error.what().c_str());
            continue;
        }
        if (0 == exit_status and f_dir_provides_schema(targetDir)) {
            Glib::setenv("GSETTINGS_SCHEMA_DIR", targetDir, true/*overwrite*/);
            g_message("compiled GSettings schemas from %s, GSETTINGS_SCHEMA_DIR = %s", dir.c_str(), targetDir.c_str());
            return;
        }
        g_warning("%s failed (%d): %s", compiler.c_str(), exit_status, std_err.c_str());
    }
    g_warning("GSettings schema %s not found: the file dialogs will abort. "
              "Install the GTK schemas (e.g. 'brew install gtk+3') and run 'glib-compile-schemas <prefix>/share/glib-2.0/schemas', "
              "or export GSETTINGS_SCHEMA_DIR pointing to a directory containing gschemas.compiled", requiredSchema);
}

int main(int argc, char *argv[])
{
#if GTKMM_MAJOR_VERSION >= 4
    // On some X11/EGL stacks (notably when DRI3 is unavailable), opening GTK4 popovers/menus
    // can corrupt EGL dispatch teardown at process exit. If the user did not choose a renderer,
    // force cairo to avoid GPU/EGL code paths.
    if (Glib::getenv("GSK_RENDERER").empty()) {
        (void)Glib::setenv("GSK_RENDERER", "cairo", true/*overwrite*/);
    }
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
    try {
        std::locale::global(std::locale("")); // Set the global C++ locale to the user-specified locale
    }
    catch (std::exception& e) {
        g_warning("%s\n", e.what());
    }
#endif /* !_WIN32 && !__APPLE__ */

    {
        const char* pExePath = argv[0];
#if defined(_WIN32)
        wchar_t path_buff[1024];
        std::wstring wtf = std::wstring(path_buff, GetModuleFileNameW(NULL, path_buff, 1023));
        std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
        std::string converted_str = converter.to_bytes(wtf);
        pExePath = converted_str.c_str();
#endif /* _WIN32 */
        //g_message("exe_path = %s", pExePath);
        fs::register_exe_path_detect_if_portable(pExePath);
    }

#if defined(__APPLE__)
    ensure_gsettings_schemas_available();
#endif /* __APPLE__ */

#ifdef HAVE_NLS
    const std::string ct_lang = CtMiscUtil::get_ct_language();
    if (ct_lang != CtConst::LANG_DEFAULT) {
        const std::string ct_lang_utf8 = ct_lang + ".UTF-8";
        if ( fs::alter_locale_env_var("LANGUAGE", ct_lang + ":en") and
             fs::alter_locale_env_var("LANG", ct_lang_utf8) and
             fs::alter_locale_env_var("LC_ALL", ct_lang_utf8) ) {
            g_message("Language overwrite = %s (localedir = %s)", ct_lang.c_str(), fs::get_cherrytree_localedir().c_str());
        }
        else {
            g_critical("Couldn't set language %s", ct_lang.c_str());
        }
    }
    bindtextdomain(GETTEXT_PACKAGE, fs::get_cherrytree_localedir().c_str());
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
#endif /* HAVE_NLS */

    // output logs into console and a log file
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    std::optional<fs::path> optLogdir = fs::get_cherrytree_logdir();
    if (optLogdir.has_value()) {
        try {
            // Create a file rotating logger with 5mb size max and 3 rotated files
            auto max_size = 1048576 * 5;
            auto max_files = 3;
            fs::path log_path = optLogdir.value() / "cherrytree.log";
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_path.c_str(), max_size, max_files));
        }
        catch (const spdlog::spdlog_ex &ex) {
            spdlog::debug("Log init failed: {}", ex.what());
        }
    }

    spdlog::drop(""); // remove the default logger (if you want, you can use its name)
    // these two loggers are the same, they just add "[che]" and "[gtk]" in their output
    auto cherrytree_logger = std::make_shared<spdlog::logger>("che", begin(sinks), end(sinks));
    auto gtk_logger = std::make_shared<spdlog::logger>("gtk", begin(sinks), end(sinks));

    spdlog::set_default_logger(cherrytree_logger);         // make our logger as a default logger
    spdlog::register_logger(gtk_logger);                   // register it, so we can access it in another place
    spdlog::flush_on(spdlog::level::debug);                // flush when "info" or higher message is logged on all loggers
    spdlog::set_level(spdlog::level::debug);               // Setup spdlog, use debug level by default for now

    g_log_set_default_handler(glib_log_handler, gtk_logger.get()); // Redirect Gtk log messages to spdlog

    bool is_secondary_session{false};
    for (int i = 1; i < argc; ++i) {
        if (0 == strcmp("-S", argv[i]) or
            0 == strcmp("--secondary_session", argv[i]))
        {
            is_secondary_session = true;
            break;
        }
    }

    Glib::RefPtr<CtApp> r_app = CtApp::create(is_secondary_session ? "_2" : "");
    return r_app->run(argc, argv);
}
