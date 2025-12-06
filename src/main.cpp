#include "RCOptions.h"
#include "TRemoteFrame.h"
#include "TServerList.h"
#include "TStartFrame.h"

#include <array>
#include <filesystem>
#include <memory>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

#ifdef _WIN32
#include <windows.h>
#endif

GtkStatusIcon* pmTrayIcon = nullptr;
std::string pmTrayNormalIcon;
std::string pmTrayAlertIcon;
guint pmTrayBlinkSource = 0;
bool pmTrayAlertVisible = false;

namespace {

    TStartFrame* trayStartFrame = nullptr;
    TRemoteFrame* trayRemoteFrame = nullptr;

    gboolean onTrayPMBlink(gpointer) {
        pmTrayAlertVisible = !pmTrayAlertVisible;
        gtk_status_icon_set_from_file(pmTrayIcon, (pmTrayAlertVisible ? pmTrayAlertIcon : pmTrayNormalIcon).c_str());
        return G_SOURCE_CONTINUE;
    }

    void clearTrayPMAlert() {
        if (pmTrayBlinkSource != 0) g_source_remove(pmTrayBlinkSource);
        pmTrayBlinkSource = 0;
        pmTrayAlertVisible = false;
        gtk_status_icon_set_from_file(pmTrayIcon, pmTrayNormalIcon.c_str());
    }

    void onTrayOpen(GtkMenuItem*, gpointer) {
        clearTrayPMAlert();
        if (trayRemoteFrame != nullptr) trayRemoteFrame->show();
        else if (trayStartFrame != nullptr) trayStartFrame->show();
    }

    void onTrayQuit(GtkMenuItem*, gpointer) { gtk_main_quit(); }

    void onTrayActivate(GtkStatusIcon*, gpointer) {
        if (trayRemoteFrame != nullptr && trayRemoteFrame->openLatestPrivateMessage()) {
            clearTrayPMAlert();
            return;
        }
        onTrayOpen(nullptr, nullptr);
    }

    void onTrayPopup(GtkStatusIcon*, guint, guint32, gpointer) {
        GtkWidget* menu = gtk_menu_new();
        GtkWidget* open = gtk_menu_item_new_with_label("Open");
        GtkWidget* quit = gtk_menu_item_new_with_label("Quit");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), open);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);
        g_signal_connect(open, "activate", G_CALLBACK(onTrayOpen), nullptr);
        g_signal_connect(quit, "activate", G_CALLBACK(onTrayQuit), nullptr);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
    }

    void copySyntaxFiles(const std::filesystem::path& applicationDirectory) {
        const auto languageSpecsDirectory = (applicationDirectory / "language-specs").string();
        gchar* searchPaths[] = {const_cast<gchar*>(languageSpecsDirectory.c_str()), nullptr};
        gtk_source_language_manager_set_search_path(gtk_source_language_manager_get_default(), searchPaths);
        gtk_source_style_scheme_manager_append_search_path(gtk_source_style_scheme_manager_get_default(), languageSpecsDirectory.c_str());
    }

    std::filesystem::path getApplicationDirectory() {
#ifdef _WIN32
        std::array<wchar_t, 32768> executablePath;
        const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        if (length != 0 && length < executablePath.size()) return std::filesystem::path(executablePath.data()).parent_path();
#endif
        return std::filesystem::current_path();
    }

    void configureGtkRuntime(const std::filesystem::path& applicationDirectory) {
#ifdef _WIN32
        const std::string applicationPath = applicationDirectory.string();
        const std::string loaders = (applicationDirectory / "lib" / "gdk-pixbuf-2.0" / "2.10.0" / "loaders").string();
        g_setenv("GTK_DATA_PREFIX", applicationPath.c_str(), true);
        g_setenv("GDK_PIXBUF_MODULEDIR", loaders.c_str(), true);
        g_setenv("GDK_PIXBUF_MODULE_FILE", (applicationDirectory / "lib" / "gdk-pixbuf-2.0" / "2.10.0" / "loaders.cache").string().c_str(), true);
        g_setenv("GSETTINGS_SCHEMA_DIR", (applicationDirectory / "share" / "glib-2.0" / "schemas").string().c_str(), true);
#endif
    }

    void applyDarkTheme() {
        GtkCssProvider* provider = gtk_css_provider_new();
        constexpr const char* css = "window, dialog, .background { background-color: #454545; color: #dddddd; } label, checkbutton label, button label { color: #dddddd; } entry { background-color: #1e1e1e; color: #dddddd; caret-color: #00ff00; border-color: #555555; } entry:disabled { background-color: #383838; color: #c1c1c1; } textview, textview text { background-color: #1e1e1e; color: #dddddd; } combobox button, button { background-image: none; background-color: #383838; color: #cbcbcb; border-color: #555555; } button:hover, combobox button:hover { background-image: none; background-color: #3b3b3b; } button:active, combobox button:active { background-image: none; background-color: #303030; } button:disabled { background-image: none; background-color: #383838; color: #828282; } checkbutton { color: #dddddd; } treeview.view { background-color: #272822; color: #dddddd; } menubar, menu { background-color: #484848; color: #cbcbcb; } menuitem { color: #cbcbcb; } notebook, notebook > header, notebook > stack, scrolledwindow, viewport { background-color: transparent; border: none; box-shadow: none; padding: 0; } notebook > header > tabs > tab { background-image: none; background-color: #3d3d3d; border: 1px solid #707070; border-bottom: none; border-radius: 4px 4px 0 0; margin-right: 4px; padding: 4px 10px; } notebook > header > tabs > tab:checked { background-color: #454545; border-color: #909090; } treeview.view:selected { background-color: #555555; color: #ffffff; }";
        gtk_css_provider_load_from_data(provider, css, -1, nullptr);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
    }

}

void rc3_begin_pm_tray_alert() {
    if (pmTrayIcon == nullptr || pmTrayBlinkSource != 0) return;
    pmTrayAlertVisible = true;
    gtk_status_icon_set_from_file(pmTrayIcon, pmTrayAlertIcon.c_str());
    pmTrayBlinkSource = g_timeout_add(500, onTrayPMBlink, nullptr);
}

int main(int argc, char** argv) {
    const std::filesystem::path applicationDirectory = getApplicationDirectory();
    std::filesystem::current_path(applicationDirectory);
    configureGtkRuntime(applicationDirectory);
    gtk_init(&argc, &argv);
    gtk_icon_theme_append_search_path(gtk_icon_theme_get_default(), (applicationDirectory / "share" / "icons").string().c_str());
    RC3::RCOptions options;
    copySyntaxFiles(applicationDirectory);
    RC3::loadRCOptions(options, applicationDirectory);
    applyDarkTheme();
    GtkStatusIcon* trayIcon = gtk_status_icon_new_from_file((applicationDirectory / "images" / "rcicon.png").string().c_str());
    pmTrayIcon = trayIcon;
    pmTrayNormalIcon = (applicationDirectory / "images" / "rcicon.png").string();
    pmTrayAlertIcon = (applicationDirectory / "images" / "pmicon_tray.png").string();
    gtk_status_icon_set_tooltip_text(trayIcon, "Graal RemoteControl");
    g_signal_connect(trayIcon, "activate", G_CALLBACK(onTrayActivate), nullptr);
    g_signal_connect(trayIcon, "popup-menu", G_CALLBACK(onTrayPopup), nullptr);
    TStartFrame* startFrame = nullptr;
    std::unique_ptr<TRemoteFrame> remoteFrame;
    TServerList serverList([&] { startFrame->show(); }, [&](void* connection, const std::string& serverName) {
        remoteFrame = std::make_unique<TRemoteFrame>(options, applicationDirectory, [&] { serverList.reopen(); });
        trayRemoteFrame = remoteFrame.get();
        remoteFrame->open(connection, serverName);
    });
    TStartFrame frame(options, applicationDirectory, [&](const std::string& account, const std::string& password) { serverList.open(account, password); });
    startFrame = &frame;
    trayStartFrame = startFrame;
    frame.show();
    gtk_main();
    return 0;
}
