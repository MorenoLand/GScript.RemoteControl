#include "TRCOptions.h"
#include "TBackup.h"
#include "TDebug.h"
#include "TGScriptEditor.h"
#include "TRemoteFrame.h"
#include "TTheme.h"
#include "TServerList.h"
#include "TStartFrame.h"
#include "TMcpServer.h"

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
GtkWidget* traySignOutItem = nullptr;
std::string pmTrayNormalIcon;
std::string pmTrayAlertIcon;
guint pmTrayBlinkSource = 0;
guint traySingleClickSource = 0;
gint64 trayDoubleClickUntil = 0;
bool pmTrayAlertVisible = false;
std::function<void()> trayServerListOpen;
std::function<void()> traySignOut;
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
    constexpr UINT TrayMenuSignOutId = 4;
    HWND serverListHotkeyWindow = nullptr;
    const wchar_t* trayMenuText(UINT itemId) {
        if (itemId == TrayMenuOpenId) return L"Open";
        if (itemId == TrayMenuServerListId) return L"Server List";
        if (itemId == TrayMenuSignOutId) return L"Sign out";
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

    void setTrayTooltip(const char* serverName, int playerCount) {
        if (pmTrayIcon == nullptr) return;
        std::string tooltip = "Remote Control";
        if (serverName != nullptr && *serverName != '\0') tooltip = std::string(serverName) + ": " + std::to_string(playerCount) + (playerCount == 1 ? " player" : " players");
        if (tooltip.size() > 64) tooltip.resize(64);
        gtk_status_icon_set_tooltip_text(pmTrayIcon, tooltip.c_str());
    }

    void onTrayOpen(GtkMenuItem*, gpointer) {
        clearTrayPMAlert();
        if (trayRemoteFrame != nullptr) trayRemoteFrame->show();
        else if (trayStartFrame != nullptr) trayStartFrame->show();
    }

    void onTrayServerList(GtkMenuItem*, gpointer) {
        if (trayServerListOpen) trayServerListOpen();
    }

    void onTraySignOut(GtkMenuItem*, gpointer) {
        clearTrayPMAlert();
        if (traySignOut) traySignOut();
    }

    void onTrayQuit(GtkMenuItem*, gpointer) { gtk_main_quit(); }

    void toggleTrayApplication() {
        if (trayRemoteFrame != nullptr) trayRemoteFrame->toggleVisibility();
        else if (trayStartFrame != nullptr) trayStartFrame->toggleVisibility();
    }

    gboolean onTraySingleClick(gpointer) {
        traySingleClickSource = 0;
        toggleTrayApplication();
        return G_SOURCE_REMOVE;
    }

    gboolean onTrayButtonPress(GtkStatusIcon*, GdkEventButton* event, gpointer) {
        if (event->button != 1) return false;
        if (event->type == GDK_2BUTTON_PRESS) {
            if (traySingleClickSource != 0) g_source_remove(traySingleClickSource);
            traySingleClickSource = 0;
            trayDoubleClickUntil = g_get_monotonic_time() + 300000;
            if (trayRemoteFrame != nullptr && trayRemoteFrame->openLatestPrivateMessage()) clearTrayPMAlert();
            else onTrayOpen(nullptr, nullptr);
            return true;
        }
        if (event->type == GDK_BUTTON_PRESS && traySingleClickSource == 0) traySingleClickSource = g_timeout_add(220, onTraySingleClick, nullptr);
        return true;
    }

    void onTrayActivate(GtkStatusIcon*, gpointer) {
        if (g_get_monotonic_time() < trayDoubleClickUntil) return;
        if (traySingleClickSource == 0) traySingleClickSource = g_timeout_add(220, onTraySingleClick, nullptr);
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
        if (trayRemoteFrame != nullptr) AppendMenuW(menu, MF_OWNERDRAW, TrayMenuSignOutId, L"Sign out");
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
        else if (command == TrayMenuSignOutId) onTraySignOut(nullptr, nullptr);
        else if (command == TrayMenuQuitId) onTrayQuit(nullptr, nullptr);
#else
        gtk_widget_set_visible(trayServerListItem, trayRemoteFrame != nullptr && trayRemoteFrame->isNCAuthenticated());
        gtk_widget_set_visible(traySignOutItem, trayRemoteFrame != nullptr);
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

    void applyTheme(const std::string& theme, bool enabled) {
        if (darkThemeProvider != nullptr) {
            gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(darkThemeProvider));
            g_object_unref(darkThemeProvider);
            darkThemeProvider = nullptr;
        }
        g_object_set_data(G_OBJECT(gtk_settings_get_default()), "remote-control-dark-mode", GINT_TO_POINTER(enabled));
        g_object_set_data_full(G_OBJECT(gtk_settings_get_default()), "remote-control-theme", g_strdup(theme.c_str()), g_free);
        if (theme == "system") {
            gboolean preferDark = false;
            g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", &preferDark, nullptr);
            g_object_set_data(G_OBJECT(gtk_settings_get_default()), "remote-control-dark-mode", GINT_TO_POINTER(preferDark != FALSE));
            return;
        }
        if (!enabled) return;
        GtkCssProvider* provider = gtk_css_provider_new();
        const bool dracula = theme == "dracula";
        const bool material = theme == "material";
        const bool ayuMirage = theme == "ayu-mirage";
        const bool nord = theme == "nord";
        const bool monokai = theme == "monokai";
        const bool oneDark = theme == "one-dark";
        const bool tokyoNight = theme == "tokyo-night";
        const bool gruvbox = theme == "gruvbox";
        const bool solarized = theme == "solarized";
        const bool catppuccin = theme == "catppuccin";
        const char* background = dracula ? "#282a36" : material ? "#263238" : ayuMirage ? "#1f2430" : nord ? "#2e3440" : monokai ? "#272822" : oneDark ? "#282c34" : tokyoNight ? "#1a1b26" : gruvbox ? "#282828" : solarized ? "#002b36" : catppuccin ? "#1e1e2e" : "#454545";
        const char* surface = dracula ? "#44475a" : material ? "#37474f" : ayuMirage ? "#242936" : nord ? "#3b4252" : monokai ? "#30312b" : oneDark ? "#21252b" : tokyoNight ? "#24283b" : gruvbox ? "#3c3836" : solarized ? "#073642" : catppuccin ? "#313244" : "#383838";
        const char* editor = dracula ? "#282a36" : material ? "#263238" : ayuMirage ? "#242936" : nord ? "#2e3440" : monokai ? "#1e1f1c" : oneDark ? "#282c34" : tokyoNight ? "#1a1b26" : gruvbox ? "#282828" : solarized ? "#002b36" : catppuccin ? "#1e1e2e" : "#1e1e1e";
        const char* text = dracula ? "#f8f8f2" : material ? "#eeffff" : ayuMirage ? "#cccac2" : nord ? "#eceff4" : monokai ? "#f8f8f2" : oneDark ? "#abb2bf" : tokyoNight ? "#c0caf5" : gruvbox ? "#ebdbb2" : solarized ? "#839496" : catppuccin ? "#cdd6f4" : "#dddddd";
        const char* accent = dracula ? "#bd93f9" : material ? "#80cbc4" : ayuMirage ? "#ffcc66" : nord ? "#88c0d0" : monokai ? "#a6e22e" : oneDark ? "#61afef" : tokyoNight ? "#7aa2f7" : gruvbox ? "#fabd2f" : solarized ? "#b58900" : catppuccin ? "#cba6f7" : "#00ff00";
        const char* border = dracula ? "#6272a4" : material ? "#546e7a" : ayuMirage ? "#4b5263" : nord ? "#4c566a" : monokai ? "#75715e" : oneDark ? "#3e4451" : tokyoNight ? "#3b4261" : gruvbox ? "#665c54" : solarized ? "#586e75" : catppuccin ? "#585b70" : "#555555";
        const std::string css = std::string("window, dialog, .background { background-color: ") + background + "; color: " + text + "; } label, checkbutton label, button label { color: " + std::string(text) + "; } entry { background-image: none; background-color: " + editor + "; color: " + text + "; caret-color: " + accent + "; border: 1px solid " + border + "; } entry:disabled { background-color: " + surface + "; color: #c1c1c1; } textview, textview text { background-color: " + editor + "; color: " + text + "; caret-color: " + accent + "; } combobox button, button { background-image: none; background-color: " + surface + "; color: " + text + "; border: 1px solid " + border + "; } button:hover, combobox button:hover { background-image: none; background-color: " + surface + "; } button:active, combobox button:active { background-image: none; background-color: " + editor + "; } button:disabled { background-image: none; background-color: " + surface + "; color: #828282; } checkbutton { color: " + text + "; } frame, expander { background-color: transparent; } separator, paned separator { background-color: " + border + "; min-height: 1px; min-width: 1px; } treeview.view, treeview.view header button, list, list row { background-color: " + editor + "; color: " + text + "; border-color: " + border + "; } treeview.view:selected, list row:selected { background-color: " + surface + "; color: #ffffff; } #remote-control-minimap, #remote-control-minimap.view { min-width: 120px; max-width: 120px; background-color: " + editor + "; color: " + text + "; border-left: 1px solid " + border + "; } #remote-control-minimap-marker { background-color: alpha(" + accent + ", 0.24); border: 1px solid alpha(" + accent + ", 0.72); } #remote-control-minimap .scrubber, #remote-control-minimap.scrubber { background-color: alpha(" + accent + ", 0.24); border: 1px solid alpha(" + accent + ", 0.72); } filechooser box, filechooser .path-bar, filechooser .path-bar button, filechooser .pathbar, filechooser .pathbar button { background-image: none; background-color: " + background + "; color: " + text + "; } filechooser placessidebar, filechooser placessidebar viewport, filechooser placessidebar list, filechooser placessidebar row, filechooser .sidebar, filechooser .sidebar viewport, filechooser .sidebar list, filechooser .sidebar row { background-color: " + editor + "; color: " + text + "; } filechooser placessidebar row:selected, filechooser .sidebar row:selected { background-color: " + surface + "; color: #ffffff; } menubar, menu { background-color: " + surface + "; color: " + text + "; } menuitem { color: " + text + "; } notebook, notebook > header, notebook > header > tabs, notebook > stack { background-color: transparent; border: none; box-shadow: none; outline: none; padding: 0; } scrolledwindow, viewport { background-color: transparent; border: none; box-shadow: none; outline: none; padding: 0; } notebook > header, notebook > header > tabs { min-height: 0; } notebook > header > tabs > tab { background-image: none; background-color: " + surface + "; border: 1px solid " + border + "; border-bottom: none; border-radius: 4px 4px 0 0; margin-right: 2px; padding: 2px 5px; } notebook > header > tabs > tab:checked { background-color: " + background + "; border-color: " + accent + "; } .gtk-source-completion, .gtk-source-completion-content, .gtk-source-completion-list { background-color: " + editor + "; color: " + text + "; border: 1px solid " + border + "; border-radius: 6px; } .gtk-source-completion-list { padding: 3px; } .gtk-source-completion-row { color: " + text + "; border-radius: 4px; padding: 4px 8px; } .gtk-source-completion-row:hover { background-color: " + surface + "; } .gtk-source-completion-row:selected { background-color: " + accent + "; color: " + editor + "; } .gtk-source-completion-info, .remote-completion-info { background-color: " + surface + "; color: " + text + "; border: 1px solid " + border + "; border-radius: 7px; padding: 8px 10px; } .remote-completion-signature { color: " + accent + "; font-weight: bold; } .remote-completion-details { color: " + text + "; margin-top: 4px; }";
        gtk_css_provider_load_from_data(provider, css.c_str(), -1, nullptr);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        darkThemeProvider = provider;
    }

}

void applyRemoteControlTheme(const std::string& theme, bool darkMode) {
    applyTheme(theme, darkMode);
}

void remote_control_begin_pm_tray_alert() {
    if (pmTrayIcon == nullptr || pmTrayBlinkSource != 0) return;
    pmTrayAlertVisible = true;
    gtk_status_icon_set_from_file(pmTrayIcon, pmTrayAlertIcon.c_str());
    pmTrayBlinkSource = g_timeout_add(500, onTrayPMBlink, nullptr);
}

void remote_control_clear_pm_tray_alert() { clearTrayPMAlert(); }
void remote_control_set_tray_label(const char* serverName, int playerCount) { setTrayTooltip(serverName, playerCount); }

int main(int argc, char** argv) {
    const bool mcpMode = std::find_if(argv + 1, argv + argc, [](const char* value) { return std::string(value) == "--mcp"; }) != argv + argc;
    unsigned long mcpInstance = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument.rfind("--mcp-instance=", 0) == 0) mcpInstance = std::strtoul(argument.c_str() + 15, nullptr, 10);
        else if (argument == "--mcp-instance" && index + 1 < argc) mcpInstance = std::strtoul(argv[++index], nullptr, 10);
    }
#ifdef _WIN32
    bool debugMode = false;
    int argumentCount = 1;
    for (int index = 1; index < argc; ++index) {
        if (std::string(argv[index]) == "--debug") debugMode = true;
        else argv[argumentCount++] = argv[index];
    }
    argc = argumentCount;
    if (debugMode && AttachConsole(ATTACH_PARENT_PROCESS)) {
#ifdef _MSC_VER
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
#else
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
#endif
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
    if (mcpMode) return runMcpServer(options, applicationDirectory, mcpInstance);
    setGScriptEditorCacheDirectory(applicationDirectory / "cache");
    setGScriptEditorOptions(options);
    applyRemoteControlTheme(options.theme, options.darkmode);
    setRemoteControlSyntaxTheme(options.syntaxtheme);
    GtkStatusIcon* trayIcon = gtk_status_icon_new_from_file((applicationDirectory / "images" / "rcicon.png").string().c_str());
    pmTrayIcon = trayIcon;
    pmTrayNormalIcon = (applicationDirectory / "images" / "rcicon.png").string();
    pmTrayAlertIcon = (applicationDirectory / "images" / "pmicon_tray.png").string();
    gtk_status_icon_set_tooltip_text(trayIcon, "Remote Control");
    gtk_status_icon_set_visible(trayIcon, true);
    trayMenu = gtk_menu_new();
    GtkWidget* openTrayItem = gtk_menu_item_new_with_label("Open");
    trayServerListItem = gtk_menu_item_new_with_label("Server List");
    traySignOutItem = gtk_menu_item_new_with_label("Sign out");
    GtkWidget* quitTrayItem = gtk_menu_item_new_with_label("Quit");
    gtk_menu_shell_append(GTK_MENU_SHELL(trayMenu), openTrayItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(trayMenu), trayServerListItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(trayMenu), traySignOutItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(trayMenu), quitTrayItem);
    g_signal_connect(openTrayItem, "activate", G_CALLBACK(onTrayOpen), nullptr);
    g_signal_connect(trayServerListItem, "activate", G_CALLBACK(onTrayServerList), nullptr);
    g_signal_connect(traySignOutItem, "activate", G_CALLBACK(onTraySignOut), nullptr);
    g_signal_connect(quitTrayItem, "activate", G_CALLBACK(onTrayQuit), nullptr);
    gtk_widget_show_all(trayMenu);
    g_signal_connect(trayIcon, "button-press-event", G_CALLBACK(onTrayButtonPress), nullptr);
    g_signal_connect(trayIcon, "activate", G_CALLBACK(onTrayActivate), nullptr);
    g_signal_connect(trayIcon, "popup-menu", G_CALLBACK(onTrayPopup), nullptr);
    TStartFrame* startFrame = nullptr;
    std::unique_ptr<TRemoteFrame> remoteFrame;
    std::function<void()> switchServer;
    TServerList serverList([&] { if (remoteFrame == nullptr) startFrame->show(); }, [&](void* connection, int serverIndex, const std::string& serverName, const std::string& nickname, const std::string& accountName) {
        remoteFrame = std::make_unique<TRemoteFrame>(options, applicationDirectory, [&] { serverList.reopen(); }, [&] { switchServer(); }, [&] { serverList.openListServerSettings(); });
        trayRemoteFrame = remoteFrame.get();
        remoteFrame->open(connection, serverIndex, serverName, nickname, accountName);
    }, [&] {
        if (remoteFrame == nullptr) return;
        trayRemoteFrame = nullptr;
        remoteFrame->disconnect();
        remoteFrame.reset();
    }, options.darkmode, options.theme, [&](bool darkMode, const std::string& theme) {
        options.darkmode = darkMode;
        options.theme = theme;
        RC::saveRCOptions(options, applicationDirectory);
        applyRemoteControlTheme(theme, darkMode);
        if (options.syncsyntaxtheme) options.syntaxtheme = theme == "dark" ? "language-spec" : theme;
        setRemoteControlSyntaxTheme(options.syntaxtheme);
        setGScriptEditorOptions(options);
        refreshGScriptEditorTheme();
        if (remoteFrame != nullptr) remoteFrame->updateThemeOptions(options);
    });
    switchServer = [&] {
        serverList.reopen();
    };
    TStartFrame frame(options, applicationDirectory, [&](std::uint64_t accountId, const std::string& account, const std::string& password, const std::string& nickname, const std::string& listServer) { serverList.open(accountId, account, password, nickname, listServer); }, [&] { serverList.openListServerSettings(); }, [&] { return serverList.currentListServer(); });
    serverList.setLoginParent(frame.windowHandle());
    startFrame = &frame;
    trayStartFrame = startFrame;
    trayServerListOpen = switchServer;
    traySignOut = [&] {
        if (remoteFrame == nullptr) return;
        trayRemoteFrame = nullptr;
        remoteFrame->signOut();
        remoteFrame.reset();
        startFrame->show();
    };
    startMcpGuiBridge(options, applicationDirectory, {
        [&remoteFrame] { return remoteFrame ? remoteFrame->currentServerName() : std::string(); },
        [&remoteFrame] { return remoteFrame != nullptr && remoteFrame->isConnected(); },
        [&frame] { return frame.mcpVisible(); },
        [&frame] { return frame.mcpAccount(); },
        [&frame] { return frame.mcpNickname(); },
        [&frame] { return frame.mcpHasPassword(); },
        [&frame](const std::string& account, const std::string& nickname, std::string& error) { return frame.mcpSubmit(account, nickname, error); },
        [&serverList] { return serverList.mcpServerNames(); },
        [&serverList](const std::string& name, std::string& error) { return serverList.mcpConnect(name, error); },
        [&remoteFrame](const std::string& view, std::string& error) { if (!remoteFrame) { error = "No connected RC server window"; return false; } return remoteFrame->mcpOpenView(view, error); },
        [&remoteFrame](const std::string& text, std::string& error) { if (!remoteFrame) { error = "No connected RC server window"; return false; } return remoteFrame->mcpSendChat(text, error); }
    });
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
    stopMcpGuiBridge();
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
