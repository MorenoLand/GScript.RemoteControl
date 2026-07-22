#include "RCOptions.h"
#include "Backup.h"
#include "Debug.h"
#include "GScriptEditor.h"
#include "TRemoteFrame.h"
#include "TServerList.h"
#include "TStartFrame.h"

#include <algorithm>
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
GtkWidget* trayServerListItem = nullptr;
std::string pmTrayNormalIcon;
std::string pmTrayAlertIcon;
guint pmTrayBlinkSource = 0;
bool pmTrayAlertVisible = false;
std::function<void()> trayServerListOpen;
bool remoteControlDebug = false;

namespace {

    TStartFrame* trayStartFrame = nullptr;
    TRemoteFrame* trayRemoteFrame = nullptr;

    gboolean onTopLevelSizeAllocate(GSignalInvocationHint*, guint, const GValue* values, gpointer) {
        GtkWidget* widget = GTK_WIDGET(g_value_get_object(&values[0]));
        if (!GTK_IS_WINDOW(widget)) return TRUE;
        gtk_widget_queue_draw(widget);
        if (GdkWindow* surface = gtk_widget_get_window(widget)) gdk_window_process_updates(surface, true);
        return TRUE;
    }

    void enableLiveResizePainting() {
        const guint signal = g_signal_lookup("size-allocate", GTK_TYPE_WIDGET);
        if (signal != 0) g_signal_add_emission_hook(signal, 0, onTopLevelSizeAllocate, nullptr, nullptr);
    }

#ifdef _WIN32
    constexpr int ServerListHotkeyId = 0x5243;
    constexpr UINT TrayMenuOpenId = 1;
    constexpr UINT TrayMenuServerListId = 2;
    constexpr UINT TrayMenuQuitId = 3;
    HWND serverListHotkeyWindow = nullptr;
    const wchar_t* trayMenuText(UINT itemId) {
        if (itemId == TrayMenuOpenId) return L"Open";
        if (itemId == TrayMenuServerListId) return L"Server List";
        if (itemId == TrayMenuQuitId) return L"Quit";
        return L"";
    }
    LRESULT CALLBACK trayMenuWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_MEASUREITEM) {
            auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (measure->CtlType != ODT_MENU) return FALSE;
            HDC dc = GetDC(window);
            HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HGDIOBJ oldFont = SelectObject(dc, font);
            SIZE size = {};
            GetTextExtentPoint32W(dc, trayMenuText(measure->itemID), -1, &size);
            SelectObject(dc, oldFont);
            ReleaseDC(window, dc);
            measure->itemWidth = 132;
            measure->itemHeight = static_cast<UINT>(std::max(24L, size.cy + 10));
            return TRUE;
        }
        if (message == WM_DRAWITEM) {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (draw->CtlType != ODT_MENU) return FALSE;
            const bool selected = (draw->itemState & ODS_SELECTED) != 0;
            HBRUSH background = CreateSolidBrush(selected ? RGB(53, 115, 220) : RGB(59, 59, 59));
            FillRect(draw->hDC, &draw->rcItem, background);
            DeleteObject(background);
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, RGB(255, 255, 255));
            RECT textBounds = draw->rcItem;
            textBounds.left += 18;
            DrawTextW(draw->hDC, trayMenuText(draw->itemID), -1, &textBounds, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            return TRUE;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
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
#ifdef _WIN32
        HMENU menu = CreatePopupMenu();
        MENUINFO menuInfo = {};
        menuInfo.cbSize = sizeof(menuInfo);
        menuInfo.fMask = MIM_BACKGROUND;
        HBRUSH menuBackground = CreateSolidBrush(RGB(59, 59, 59));
        menuInfo.hbrBack = menuBackground;
        SetMenuInfo(menu, &menuInfo);
        AppendMenuW(menu, MF_OWNERDRAW, TrayMenuOpenId, L"Open");
        const bool canOpenServerList = trayRemoteFrame != nullptr && trayRemoteFrame->isNCAuthenticated();
        if (canOpenServerList) AppendMenuW(menu, MF_OWNERDRAW, TrayMenuServerListId, L"Server List");
        AppendMenuW(menu, MF_OWNERDRAW, TrayMenuQuitId, L"Quit");
        POINT cursor;
        GetCursorPos(&cursor);
        HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        SetWindowLongPtrW(owner, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(trayMenuWindowProcedure));
        SetForegroundWindow(owner);
        const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_BOTTOMALIGN, cursor.x, cursor.y, 0, owner, nullptr);
        DestroyMenu(menu);
        DeleteObject(menuBackground);
        DestroyWindow(owner);
        if (command == TrayMenuOpenId) onTrayOpen(nullptr, nullptr);
        else if (command == TrayMenuServerListId) onTrayServerList(nullptr, nullptr);
        else if (command == TrayMenuQuitId) onTrayQuit(nullptr, nullptr);
#else
        gtk_widget_set_visible(trayServerListItem, trayRemoteFrame != nullptr && trayRemoteFrame->isNCAuthenticated());
        gtk_menu_popup(GTK_MENU(trayMenu), nullptr, nullptr, gtk_status_icon_position_menu, icon, button, activateTime);
#endif
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

    GtkCssProvider* darkThemeProvider = nullptr;

    void applyDarkTheme(bool enabled) {
        if (darkThemeProvider != nullptr) {
            gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(darkThemeProvider));
            g_object_unref(darkThemeProvider);
            darkThemeProvider = nullptr;
        }
        if (!enabled) return;
        GtkCssProvider* provider = gtk_css_provider_new();
        constexpr const char* css = "window, dialog, .background { background-color: #454545; color: #dddddd; } label, checkbutton label, button label { color: #dddddd; } entry { background-color: #1e1e1e; color: #dddddd; caret-color: #00ff00; border-color: #555555; } entry:disabled { background-color: #383838; color: #c1c1c1; } textview, textview text { background-color: #1e1e1e; color: #dddddd; } combobox button, button { background-image: none; background-color: #383838; color: #cbcbcb; border-color: #555555; } button:hover, combobox button:hover { background-image: none; background-color: #3b3b3b; } button:active, combobox button:active { background-image: none; background-color: #303030; } button:disabled { background-image: none; background-color: #383838; color: #828282; } checkbutton { color: #dddddd; } treeview.view { background-color: #272822; color: #dddddd; } filechooser box, filechooser .path-bar, filechooser .path-bar button, filechooser .pathbar, filechooser .pathbar button { background-image: none; background-color: #454545; color: #dddddd; } filechooser placessidebar, filechooser placessidebar viewport, filechooser placessidebar list, filechooser placessidebar row, filechooser .sidebar, filechooser .sidebar viewport, filechooser .sidebar list, filechooser .sidebar row { background-color: #272822; color: #dddddd; } filechooser placessidebar row:selected, filechooser .sidebar row:selected { background-color: #555555; color: #ffffff; } menubar, menu { background-color: #484848; color: #cbcbcb; } menuitem { color: #cbcbcb; } notebook, notebook > header, notebook > stack, scrolledwindow, viewport { background-color: transparent; border: none; box-shadow: none; padding: 0; } notebook > header, notebook > header > tabs { min-height: 0; } notebook > header > tabs > tab { background-image: none; background-color: #3d3d3d; border: 1px solid #707070; border-bottom: none; border-radius: 4px 4px 0 0; margin-right: 2px; padding: 2px 5px; } notebook > header > tabs > tab:checked { background-color: #454545; border-color: #909090; } treeview.view:selected { background-color: #555555; color: #ffffff; }";
        gtk_css_provider_load_from_data(provider, css, -1, nullptr);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        darkThemeProvider = provider;
    }

}

void remote_control_begin_pm_tray_alert() {
    if (pmTrayIcon == nullptr || pmTrayBlinkSource != 0) return;
    pmTrayAlertVisible = true;
    gtk_status_icon_set_from_file(pmTrayIcon, pmTrayAlertIcon.c_str());
    pmTrayBlinkSource = g_timeout_add(500, onTrayPMBlink, nullptr);
}

void remote_control_clear_pm_tray_alert() { clearTrayPMAlert(); }

int main(int argc, char** argv) {
#ifdef _WIN32
    bool debugMode = false;
    int argumentCount = 1;
    for (int index = 1; index < argc; ++index) {
        if (std::string(argv[index]) == "--debug") debugMode = true;
        else argv[argumentCount++] = argv[index];
    }
    argc = argumentCount;
    if (debugMode && AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
    remoteControlDebug = debugMode;
    remoteControlDebugLog("debug logging enabled");
#endif
    const std::filesystem::path applicationDirectory = getApplicationDirectory();
    std::filesystem::current_path(applicationDirectory);
    setBackupDataDirectory(applicationDirectory);
    const std::filesystem::path certificateBundle = applicationDirectory / "certs" / "ca-bundle.crt";
    if (std::filesystem::is_regular_file(certificateBundle)) g_setenv("SSL_CERT_FILE", certificateBundle.string().c_str(), true);
    configureGtkRuntime(applicationDirectory);
    gtk_init(&argc, &argv);
    enableLiveResizePainting();
    gtk_icon_theme_append_search_path(gtk_icon_theme_get_default(), (applicationDirectory / "share" / "icons").string().c_str());
    RC::RCOptions options;
    copySyntaxFiles(applicationDirectory);
    RC::loadRCOptions(options, applicationDirectory);
    setGScriptEditorOptions(options);
    applyDarkTheme(options.darkmode);
    GtkStatusIcon* trayIcon = gtk_status_icon_new_from_file((applicationDirectory / "images" / "rcicon.png").string().c_str());
    pmTrayIcon = trayIcon;
    pmTrayNormalIcon = (applicationDirectory / "images" / "rcicon.png").string();
    pmTrayAlertIcon = (applicationDirectory / "images" / "pmicon_tray.png").string();
    gtk_status_icon_set_tooltip_text(trayIcon, "Graal RemoteControl");
    gtk_status_icon_set_visible(trayIcon, true);
    trayMenu = gtk_menu_new();
    GtkWidget* openTrayItem = gtk_menu_item_new_with_label("Open");
    trayServerListItem = gtk_menu_item_new_with_label("Server List");
    GtkWidget* quitTrayItem = gtk_menu_item_new_with_label("Quit");
    gtk_menu_shell_append(GTK_MENU_SHELL(trayMenu), openTrayItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(trayMenu), trayServerListItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(trayMenu), quitTrayItem);
    g_signal_connect(openTrayItem, "activate", G_CALLBACK(onTrayOpen), nullptr);
    g_signal_connect(trayServerListItem, "activate", G_CALLBACK(onTrayServerList), nullptr);
    g_signal_connect(quitTrayItem, "activate", G_CALLBACK(onTrayQuit), nullptr);
    gtk_widget_show_all(trayMenu);
    g_signal_connect(trayIcon, "activate", G_CALLBACK(onTrayActivate), nullptr);
    g_signal_connect(trayIcon, "popup-menu", G_CALLBACK(onTrayPopup), nullptr);
    TStartFrame* startFrame = nullptr;
    std::unique_ptr<TRemoteFrame> remoteFrame;
    std::function<void()> switchServer;
    TServerList serverList([&] { if (remoteFrame == nullptr) startFrame->show(); }, [&](void* connection, const std::string& serverName, const std::string& nickname, const std::string& accountName) {
        remoteFrame = std::make_unique<TRemoteFrame>(options, applicationDirectory, [&] { serverList.reopen(); }, [&] { switchServer(); }, [&] { serverList.openListServerSettings(); });
        trayRemoteFrame = remoteFrame.get();
        remoteFrame->open(connection, serverName, nickname, accountName);
    }, [&] {
        if (remoteFrame == nullptr) return;
        trayRemoteFrame = nullptr;
        remoteFrame->disconnect();
        remoteFrame.reset();
    }, options.darkmode, [&](bool darkMode) {
        options.darkmode = darkMode;
        RC::saveRCOptions(options, applicationDirectory);
        applyDarkTheme(darkMode);
    });
    switchServer = [&] {
        serverList.reopen();
    };
    TStartFrame frame(options, applicationDirectory, [&](const std::string& account, const std::string& password, const std::string& nickname) { serverList.open(account, password, nickname); }, [&] { serverList.openListServerSettings(); });
    startFrame = &frame;
    trayStartFrame = startFrame;
    trayServerListOpen = switchServer;
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
    clearTrayPMAlert();
    if (trayMenu != nullptr) {
        gtk_widget_destroy(trayMenu);
        trayMenu = nullptr;
    }
    if (trayIcon != nullptr) {
        gtk_status_icon_set_visible(trayIcon, false);
        g_object_unref(trayIcon);
        pmTrayIcon = nullptr;
    }
    return 0;
}
