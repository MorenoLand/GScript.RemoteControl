#include "RCOptions.h"
#include "Backup.h"
#include "GScriptEditor.h"
#include "TRemoteFrame.h"
#include "TServerList.h"
#include "TStartFrame.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <vector>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

#ifdef _WIN32
#include <windows.h>
#include <gdk/gdkwin32.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

GtkStatusIcon* pmTrayIcon = nullptr;
GtkWidget* trayMenu = nullptr;
std::string pmTrayNormalIcon;
std::string pmTrayAlertIcon;
guint pmTrayBlinkSource = 0;
bool pmTrayAlertVisible = false;
std::function<void()> trayServerListOpen;

namespace {

    TStartFrame* trayStartFrame = nullptr;
    TRemoteFrame* trayRemoteFrame = nullptr;

#ifdef _WIN32
    constexpr int ServerListHotkeyId = 0x5243;
    HWND serverListHotkeyWindow = nullptr;
    GdkFilterReturn onWindowsMessage(GdkXEvent* event, GdkEvent*, gpointer) {
        MSG* message = static_cast<MSG*>(event);
        if (message->message == WM_HOTKEY && message->wParam == ServerListHotkeyId) {
            if (trayServerListOpen) trayServerListOpen();
            return GDK_FILTER_REMOVE;
        }
        return GDK_FILTER_CONTINUE;
    }
#endif

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

    void onTrayServerList(GtkMenuItem*, gpointer) {
        if (trayServerListOpen) trayServerListOpen();
    }

    void onTrayQuit(GtkMenuItem*, gpointer) { gtk_main_quit(); }

    void onTrayActivate(GtkStatusIcon*, gpointer) {
        if (trayRemoteFrame != nullptr && trayRemoteFrame->openLatestPrivateMessage()) {
            clearTrayPMAlert();
            return;
        }
        onTrayOpen(nullptr, nullptr);
    }

    void onTrayPopup(GtkStatusIcon* icon, guint button, guint32 activateTime, gpointer) {
        gtk_menu_popup(GTK_MENU(trayMenu), nullptr, nullptr, gtk_status_icon_position_menu, icon, button, activateTime);
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
#elif defined(__APPLE__)
        uint32_t length = 0;
        if (_NSGetExecutablePath(nullptr, &length) == -1 && length != 0) {
            std::vector<char> executablePath(length);
            if (_NSGetExecutablePath(executablePath.data(), &length) == 0) return std::filesystem::weakly_canonical(std::filesystem::path(executablePath.data())).parent_path();
        }
#else
        std::array<char, 4096> executablePath = {};
        const ssize_t length = readlink("/proc/self/exe", executablePath.data(), executablePath.size() - 1);
        if (length > 0 && static_cast<std::size_t>(length) < executablePath.size()) return std::filesystem::path(executablePath.data()).parent_path();
#endif
        return std::filesystem::current_path();
    }

    void configureGtkRuntime(const std::filesystem::path& applicationDirectory) {
#ifdef _WIN32
        SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
        AddDllDirectory(applicationDirectory.c_str());
        const std::string loaders = (applicationDirectory / "lib" / "gdk-pixbuf-2.0" / "2.10.0" / "loaders").string();
        const auto svgLoader = applicationDirectory / "lib" / "gdk-pixbuf-2.0" / "2.10.0" / "loaders" / "pixbufloader_svg.dll";
        LoadLibraryExW(svgLoader.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
        const std::string sharedData = (applicationDirectory / "share").string();
        g_setenv("XDG_DATA_DIRS", sharedData.c_str(), true);
        g_setenv("GDK_PIXBUF_MODULEDIR", loaders.c_str(), true);
        const auto loaderCacheTemplate = applicationDirectory / "lib" / "gdk-pixbuf-2.0" / "2.10.0" / "loaders.cache";
        std::ifstream loaderCacheInput(loaderCacheTemplate, std::ios::binary);
        std::string loaderCacheContent((std::istreambuf_iterator<char>(loaderCacheInput)), std::istreambuf_iterator<char>());
        const std::string absolutePrefix = "\"" + applicationDirectory.generic_string() + "/";
        for (std::size_t offset = 0; (offset = loaderCacheContent.find("\"./", offset)) != std::string::npos; offset += absolutePrefix.size()) loaderCacheContent.replace(offset, 3, absolutePrefix);
        const auto runtimeCache = applicationDirectory / "cache" / "gdk-pixbuf-loaders.cache";
        std::filesystem::create_directories(runtimeCache.parent_path());
        std::ofstream(runtimeCache, std::ios::binary | std::ios::trunc) << loaderCacheContent;
        g_setenv("GDK_PIXBUF_MODULE_FILE", runtimeCache.string().c_str(), true);
        g_setenv("GSETTINGS_SCHEMA_DIR", (applicationDirectory / "share" / "glib-2.0" / "schemas").string().c_str(), true);
#endif
    }

    void applyDarkTheme() {
        GtkCssProvider* provider = gtk_css_provider_new();
        constexpr const char* css = "window, dialog, .background { background-color: #454545; color: #dddddd; } label, checkbutton label, button label { color: #dddddd; } entry { background-color: #1e1e1e; color: #dddddd; caret-color: #00ff00; border-color: #555555; } entry:disabled { background-color: #383838; color: #c1c1c1; } textview, textview text { background-color: #1e1e1e; color: #dddddd; } combobox button, button { background-image: none; background-color: #383838; color: #cbcbcb; border-color: #555555; } button:hover, combobox button:hover { background-image: none; background-color: #3b3b3b; } button:active, combobox button:active { background-image: none; background-color: #303030; } button:disabled { background-image: none; background-color: #383838; color: #828282; } checkbutton { color: #dddddd; } treeview.view { background-color: #272822; color: #dddddd; } filechooser box, filechooser .path-bar, filechooser .path-bar button, filechooser .pathbar, filechooser .pathbar button { background-image: none; background-color: #454545; color: #dddddd; } filechooser placessidebar, filechooser placessidebar viewport, filechooser placessidebar list, filechooser placessidebar row, filechooser .sidebar, filechooser .sidebar viewport, filechooser .sidebar list, filechooser .sidebar row { background-color: #272822; color: #dddddd; } filechooser placessidebar row:selected, filechooser .sidebar row:selected { background-color: #555555; color: #ffffff; } menubar, menu { background-color: #484848; color: #cbcbcb; } menuitem { color: #cbcbcb; } notebook, notebook > header, notebook > stack, scrolledwindow, viewport { background-color: transparent; border: none; box-shadow: none; padding: 0; } notebook > header, notebook > header > tabs { min-height: 0; } notebook > header > tabs > tab { background-image: none; background-color: #3d3d3d; border: 1px solid #707070; border-bottom: none; border-radius: 4px 4px 0 0; margin-right: 2px; padding: 2px 5px; } notebook > header > tabs > tab:checked { background-color: #454545; border-color: #909090; } treeview.view:selected { background-color: #555555; color: #ffffff; }";
        gtk_css_provider_load_from_data(provider, css, -1, nullptr);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
    }

}

void remote_control_begin_pm_tray_alert() {
    if (pmTrayIcon == nullptr || pmTrayBlinkSource != 0) return;
    pmTrayAlertVisible = true;
    gtk_status_icon_set_from_file(pmTrayIcon, pmTrayAlertIcon.c_str());
    pmTrayBlinkSource = g_timeout_add(500, onTrayPMBlink, nullptr);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    bool debugMode = false;
    for (int index = 1; index < argc; ++index) if (std::string(argv[index]) == "--debug") debugMode = true;
    if (debugMode && AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
#endif
    const std::filesystem::path applicationDirectory = getApplicationDirectory();
    std::filesystem::current_path(applicationDirectory);
    setBackupDataDirectory(applicationDirectory);
    const std::filesystem::path certificateBundle = applicationDirectory / "certs" / "ca-bundle.crt";
    if (std::filesystem::is_regular_file(certificateBundle)) g_setenv("SSL_CERT_FILE", certificateBundle.string().c_str(), true);
    configureGtkRuntime(applicationDirectory);
    gtk_init(&argc, &argv);
    gtk_icon_theme_append_search_path(gtk_icon_theme_get_default(), (applicationDirectory / "share" / "icons").string().c_str());
    RC::RCOptions options;
    copySyntaxFiles(applicationDirectory);
    RC::loadRCOptions(options, applicationDirectory);
    setGScriptEditorOptions(options);
    applyDarkTheme();
    GtkStatusIcon* trayIcon = gtk_status_icon_new_from_file((applicationDirectory / "images" / "rcicon.png").string().c_str());
    pmTrayIcon = trayIcon;
    pmTrayNormalIcon = (applicationDirectory / "images" / "rcicon.png").string();
    pmTrayAlertIcon = (applicationDirectory / "images" / "pmicon_tray.png").string();
    gtk_status_icon_set_tooltip_text(trayIcon, "Graal RemoteControl");
    gtk_status_icon_set_visible(trayIcon, true);
    trayMenu = gtk_menu_new();
    GtkWidget* openTrayItem = gtk_menu_item_new_with_label("Open");
    GtkWidget* serverListTrayItem = gtk_menu_item_new_with_label("Server List");
    GtkWidget* quitTrayItem = gtk_menu_item_new_with_label("Quit");
    gtk_menu_shell_append(GTK_MENU_SHELL(trayMenu), openTrayItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(trayMenu), serverListTrayItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(trayMenu), quitTrayItem);
    g_signal_connect(openTrayItem, "activate", G_CALLBACK(onTrayOpen), nullptr);
    g_signal_connect(serverListTrayItem, "activate", G_CALLBACK(onTrayServerList), nullptr);
    g_signal_connect(quitTrayItem, "activate", G_CALLBACK(onTrayQuit), nullptr);
    gtk_widget_show_all(trayMenu);
    g_signal_connect(trayIcon, "activate", G_CALLBACK(onTrayActivate), nullptr);
    g_signal_connect(trayIcon, "popup-menu", G_CALLBACK(onTrayPopup), nullptr);
    TStartFrame* startFrame = nullptr;
    std::unique_ptr<TRemoteFrame> remoteFrame;
    TServerList serverList([&] { startFrame->show(); }, [&](void* connection, const std::string& serverName, const std::string& nickname) {
        remoteFrame = std::make_unique<TRemoteFrame>(options, applicationDirectory, [&] { serverList.reopen(); }, [&] { serverList.openListServerSettings(); });
        trayRemoteFrame = remoteFrame.get();
        remoteFrame->open(connection, serverName, nickname);
    });
    TStartFrame frame(options, applicationDirectory, [&](const std::string& account, const std::string& password, const std::string& nickname) { serverList.open(account, password, nickname); }, [&] { serverList.openListServerSettings(); });
    startFrame = &frame;
    trayStartFrame = startFrame;
    trayServerListOpen = [&] { serverList.show(); };
#ifdef _WIN32
    frame.show();
    if (GdkWindow* startWindow = frame.nativeWindow()) {
        gdk_window_add_filter(startWindow, onWindowsMessage, nullptr);
        serverListHotkeyWindow = reinterpret_cast<HWND>(GDK_WINDOW_HWND(startWindow));
        RegisterHotKey(serverListHotkeyWindow, ServerListHotkeyId, MOD_NOREPEAT, VK_F8);
    }
#else
    frame.show();
#endif
    gtk_main();
#ifdef _WIN32
    if (serverListHotkeyWindow != nullptr) {
        UnregisterHotKey(serverListHotkeyWindow, ServerListHotkeyId);
        if (GdkWindow* startWindow = frame.nativeWindow()) gdk_window_remove_filter(startWindow, onWindowsMessage, nullptr);
    }
#endif
    return 0;
}
