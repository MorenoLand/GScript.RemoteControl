#include "TRCOptions.h"
#include "TBackup.h"
#include "TDebug.h"
#include "TGScriptEditor.h"
#include "TRemoteFrame.h"
#include "TTheme.h"
#include "TServerList.h"
#include "TStartFrame.h"
#include "TMcpServer.h"
#include "TCrashLogger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <map>
#include <set>
#include <vector>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <fontconfig/fontconfig.h>

#ifdef REMOTE_CONTROL_STATIC_WINDOWS
extern "C" GResource* gtksourceview_get_resource(void);
#endif

#ifdef _WIN32
#include <windows.h>
#include <gdk/gdkwin32.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#ifdef RC_HAVE_X11
#include <gdk/gdkx.h>
#include <X11/keysym.h>
#endif
#endif

void registerBundledFonts(const std::filesystem::path& applicationDirectory) {
    const auto font = applicationDirectory / "fonts" / "tempus-sans-itc.ttf";
    if (!std::filesystem::exists(font)) return;
#ifdef _WIN32
    AddFontResourceExW(font.c_str(), FR_PRIVATE, nullptr);
    SendMessageW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
    const char* userCache = g_get_user_cache_dir();
    const std::filesystem::path fontconfigRoot = userCache != nullptr && *userCache != '\0' ? std::filesystem::path(userCache) / "GScriptRC" : applicationDirectory / "cache";
    const std::filesystem::path fontconfigCache = fontconfigRoot / "fontconfig";
    std::error_code cacheError;
    std::filesystem::create_directories(fontconfigCache, cacheError);
    if (!cacheError) {
        const auto fontconfigRootString = fontconfigRoot.u8string();
        g_setenv("XDG_CACHE_HOME", reinterpret_cast<const char*>(fontconfigRootString.c_str()), TRUE);
    }
    FcConfig* config = FcConfigCreate();
    wchar_t windowsPath[MAX_PATH] = {};
    if (config != nullptr && GetWindowsDirectoryW(windowsPath, MAX_PATH) != 0) {
        const auto systemFonts = (std::filesystem::path(windowsPath) / "Fonts").u8string();
        FcConfigAppFontAddDir(config, reinterpret_cast<const FcChar8*>(systemFonts.c_str()));
    }
    const auto fontPath = font.u8string();
    if (config != nullptr && FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8*>(fontPath.c_str()))) {
        FcConfigBuildFonts(config);
        FcConfigSetCurrent(config);
    } else if (config != nullptr) FcConfigDestroy(config);
#else
    const auto fontPath = font.u8string();
    FcConfig* config = FcConfigGetCurrent();
    if (config != nullptr && FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8*>(fontPath.c_str()))) FcConfigBuildFonts(config);
#endif
}

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
std::vector<TRemoteFrame*> trayRemoteFrames;
std::set<TRemoteFrame*> trayPrimaryFrames;
std::function<void(TRemoteFrame*)> trayFrameSignOut;
std::map<unsigned int, std::wstring> trayMenuDynamicText;
std::map<unsigned int, TRemoteFrame*> trayMenuDynamicTargets;
bool remoteControlDebug = false;
bool remoteControlPacketLog = false;

namespace {

    TStartFrame* trayStartFrame = nullptr;
    TRemoteFrame* trayRemoteFrame = nullptr;
    void toggleTrayApplication();

#ifdef _WIN32
    constexpr int VisibilityHotkeyId = 0x5244;
    constexpr UINT TrayMenuOpenId = 1;
    constexpr UINT TrayMenuServerListId = 2;
    constexpr UINT TrayMenuQuitId = 3;
    constexpr UINT TrayMenuSignOutId = 4;
    HWND serverListHotkeyWindow = nullptr;
    std::string visibilityHotkey;
    UINT trayMenuWidth = 132;
    HMENU trayActiveMenu = nullptr;
    void onTrayOpen(GtkMenuItem*, gpointer);
    const wchar_t* trayMenuText(UINT itemId) {
        const auto dynamic = trayMenuDynamicText.find(itemId);
        if (dynamic != trayMenuDynamicText.end()) return dynamic->second.c_str();
        if (itemId == TrayMenuOpenId) return L"Open";
        if (itemId == TrayMenuServerListId) return L"Server List";
        if (itemId == TrayMenuSignOutId) return L"Sign out";
        if (itemId == TrayMenuQuitId) return L"Quit";
        return L"";
    }
    LRESULT CALLBACK trayMenuWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_MENURBUTTONUP && trayActiveMenu != nullptr && reinterpret_cast<HMENU>(lParam) == trayActiveMenu) {
            const UINT itemId = GetMenuItemID(trayActiveMenu, static_cast<int>(wParam));
            const auto target = trayMenuDynamicTargets.find(itemId);
            if (target != trayMenuDynamicTargets.end() && itemId >= 100 && (itemId - 100) % 2 == 0) {
                TRemoteFrame* frame = target->second;
                if (trayPrimaryFrames.contains(frame)) trayPrimaryFrames.erase(frame); else trayPrimaryFrames.insert(frame);
                const std::wstring marker = trayPrimaryFrames.contains(frame) ? L"[*] " : L"";
                const std::wstring wideServerName(frame->currentServerName().begin(), frame->currentServerName().end());
                for (const auto& item : trayMenuDynamicTargets) if (item.second == frame) {
                    const bool signOut = (item.first - 100) % 2 != 0;
                    const std::wstring text = signOut ? std::wstring(L"Sign out ") + marker + wideServerName : marker + wideServerName;
                    trayMenuDynamicText[item.first] = text;
                    ModifyMenuW(trayActiveMenu, item.first, MF_BYCOMMAND | MF_OWNERDRAW, item.first, text.c_str());
                }
                InvalidateRect(window, nullptr, TRUE);
            }
            return 0;
        }
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
            measure->itemWidth = trayMenuWidth;
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
        if (message->message == WM_HOTKEY && message->wParam == VisibilityHotkeyId) {
            toggleTrayApplication();
            return GDK_FILTER_REMOVE;
        }
        return GDK_FILTER_CONTINUE;
    }
#elif defined(RC_HAVE_X11)
    int visibilityHotkeyKeycode = 0;
    unsigned int visibilityHotkeyModifiers = 0;
    bool visibilityHotkeyFilterInstalled = false;
    GdkFilterReturn onX11Message(GdkXEvent* nativeEvent, GdkEvent*, gpointer) {
        XEvent* event = static_cast<XEvent*>(nativeEvent);
        if (event->type == KeyPress && event->xkey.keycode == visibilityHotkeyKeycode && (event->xkey.state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask)) == visibilityHotkeyModifiers) {
            toggleTrayApplication();
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
        bool openedPrimary = false;
        for (TRemoteFrame* frame : trayRemoteFrames) if (frame != nullptr && frame->isConnected() && trayPrimaryFrames.contains(frame)) {
            frame->showFromTray();
            trayRemoteFrame = frame;
            openedPrimary = true;
        }
        if (openedPrimary) return;
        if (trayRemoteFrame != nullptr) trayRemoteFrame->showFromTray();
        else if (trayStartFrame != nullptr) trayStartFrame->show();
    }

    void onTrayServerList(GtkMenuItem*, gpointer) {
        if (trayServerListOpen) trayServerListOpen();
    }

    void onTraySignOut(GtkMenuItem*, gpointer) {
        clearTrayPMAlert();
        if (traySignOut) traySignOut();
    }

    void onTrayFrameShow(GtkMenuItem*, gpointer data) { trayRemoteFrame = static_cast<TRemoteFrame*>(data); trayRemoteFrame->showFromTray(); clearTrayPMAlert(); }
    void onTrayFrameSignOut(GtkMenuItem*, gpointer data) { if (trayFrameSignOut) trayFrameSignOut(static_cast<TRemoteFrame*>(data)); }

    void onTrayQuit(GtkMenuItem*, gpointer) { gtk_main_quit(); }

    void toggleTrayApplication() {
        std::vector<TRemoteFrame*> primaryFrames;
        for (TRemoteFrame* frame : trayRemoteFrames) if (frame != nullptr && frame->isConnected() && trayPrimaryFrames.contains(frame)) primaryFrames.push_back(frame);
        if (!primaryFrames.empty()) {
            const bool hide = std::any_of(primaryFrames.begin(), primaryFrames.end(), [](TRemoteFrame* frame) { return frame->isVisible(); });
            for (TRemoteFrame* frame : primaryFrames) {
                if (hide) frame->hideFromTray();
                else frame->showFromTray();
            }
        } else if (trayRemoteFrame != nullptr) trayRemoteFrame->toggleVisibility();
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
        trayMenuWidth = 132;
        UINT dynamicId = 100;
        bool firstConnection = true;
        const bool hasConnection = std::any_of(trayRemoteFrames.begin(), trayRemoteFrames.end(), [](TRemoteFrame* frame) { return frame != nullptr && frame->isConnected(); });
        if (hasConnection) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        for (TRemoteFrame* frame : trayRemoteFrames) if (frame != nullptr && frame->isConnected()) {
            const std::string serverName = frame->currentServerName();
            const std::wstring wideServerName(serverName.begin(), serverName.end());
            const std::wstring marker = trayPrimaryFrames.contains(frame) ? L"[*] " : L"";
            const std::wstring label = marker + wideServerName;
            if (!firstConnection) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            firstConnection = false;
            trayMenuWidth = std::max(trayMenuWidth, static_cast<UINT>(std::min<std::size_t>(420, label.size() * 8 + 30)));
            trayMenuDynamicText[dynamicId] = label;
            trayMenuDynamicTargets[dynamicId] = frame;
            AppendMenuW(menu, MF_OWNERDRAW, dynamicId++, label.c_str());
            const std::wstring signOutLabel = std::wstring(L"Sign out ") + marker + wideServerName;
            trayMenuWidth = std::max(trayMenuWidth, static_cast<UINT>(std::min<std::size_t>(420, signOutLabel.size() * 8 + 30)));
            trayMenuDynamicText[dynamicId] = signOutLabel;
            trayMenuDynamicTargets[dynamicId] = frame;
            AppendMenuW(menu, MF_OWNERDRAW, dynamicId++, signOutLabel.c_str());
        }
        AppendMenuW(menu, MF_OWNERDRAW, TrayMenuQuitId, L"Quit");
        POINT cursor;
        GetCursorPos(&cursor);
        HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        SetWindowLongPtrW(owner, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(trayMenuWindowProcedure));
        trayActiveMenu = menu;
        SetForegroundWindow(owner);
        const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_LEFTALIGN | TPM_BOTTOMALIGN, cursor.x, cursor.y, 0, owner, nullptr);
        DestroyMenu(menu);
        trayActiveMenu = nullptr;
        DeleteObject(menuBackground);
        DestroyWindow(owner);
        if (command == TrayMenuOpenId) onTrayOpen(nullptr, nullptr);
        else if (command == TrayMenuServerListId) onTrayServerList(nullptr, nullptr);
        else if (command == TrayMenuSignOutId) onTraySignOut(nullptr, nullptr);
        else if (command == TrayMenuQuitId) onTrayQuit(nullptr, nullptr);
        else if (command >= 100) { const auto target = trayMenuDynamicTargets.find(command); if (target != trayMenuDynamicTargets.end()) { trayRemoteFrame = target->second; if ((command - 100) % 2 == 0) onTrayOpen(nullptr, nullptr); else onTrayFrameSignOut(nullptr, trayRemoteFrame); } }
        trayMenuDynamicText.clear();
        trayMenuDynamicTargets.clear();
#else
        GtkWidget* connections = gtk_menu_item_new_with_label("Connections");
        GtkWidget* connectionMenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(connections), connectionMenu);
        gtk_menu_shell_append(GTK_MENU_SHELL(trayMenu), connections);
        for (TRemoteFrame* frame : trayRemoteFrames) if (frame != nullptr && frame->isConnected()) {
            GtkWidget* show = gtk_menu_item_new_with_label(frame->currentServerName().c_str());
            GtkWidget* signOut = gtk_menu_item_new_with_label((std::string("Sign out ") + frame->currentServerName()).c_str());
            gtk_menu_shell_append(GTK_MENU_SHELL(connectionMenu), show);
            gtk_menu_shell_append(GTK_MENU_SHELL(connectionMenu), signOut);
            g_signal_connect(show, "activate", G_CALLBACK(onTrayFrameShow), frame);
            g_signal_connect(signOut, "activate", G_CALLBACK(onTrayFrameSignOut), frame);
        }
        gtk_widget_show_all(connections);
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
#ifdef REMOTE_CONTROL_STATIC_WINDOWS
        const std::string sharedData = (applicationDirectory / "share").string();
        g_setenv("XDG_DATA_DIRS", sharedData.c_str(), true);
        const auto staticLoaderCache = applicationDirectory / "cache" / "gdk-pixbuf-static.cache";
        std::filesystem::create_directories(staticLoaderCache.parent_path());
        std::ofstream(staticLoaderCache, std::ios::binary | std::ios::trunc);
        g_setenv("GDK_PIXBUF_MODULEDIR", (applicationDirectory / "lib" / "gdk-pixbuf-2.0" / "2.10.0" / "loaders").string().c_str(), true);
        g_setenv("GDK_PIXBUF_MODULE_FILE", staticLoaderCache.string().c_str(), true);
        g_setenv("GSETTINGS_SCHEMA_DIR", (applicationDirectory / "share" / "glib-2.0" / "schemas").string().c_str(), true);
#else
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
#elif defined(__APPLE__)
        const std::string sharedData = (applicationDirectory / "share").string();
        const std::string loaders = (applicationDirectory / "lib" / "gdk-pixbuf-2.0" / "2.10.0" / "loaders").string();
        g_setenv("XDG_DATA_DIRS", sharedData.c_str(), true);
        g_setenv("GDK_PIXBUF_MODULEDIR", loaders.c_str(), true);
        g_setenv("GDK_PIXBUF_MODULE_FILE", (applicationDirectory / "lib" / "gdk-pixbuf-2.0" / "2.10.0" / "loaders.cache").string().c_str(), true);
        g_setenv("GSETTINGS_SCHEMA_DIR", (applicationDirectory / "share" / "glib-2.0" / "schemas").string().c_str(), true);
#endif
    }

    GtkCssProvider* darkThemeProvider = nullptr;

#ifdef _WIN32
    void applyWindowCornerPreference(bool roundedCorners) {
        using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        static HMODULE library = LoadLibraryW(L"dwmapi.dll");
        static auto setAttribute = library == nullptr ? nullptr : reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(library, "DwmSetWindowAttribute"));
        if (setAttribute == nullptr) return;
        const int preference = roundedCorners ? 2 : 1;
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item != nullptr; item = item->next) {
            GtkWidget* window = GTK_WIDGET(item->data);
            GdkWindow* gdkWindow = gtk_widget_get_window(window);
            if (gdkWindow != nullptr) setAttribute(reinterpret_cast<HWND>(gdk_win32_window_get_handle(gdkWindow)), 33, &preference, sizeof(preference));
        }
        g_list_free(windows);
    }
#endif

    void applyTheme(const std::string& theme, bool enabled, bool roundedCorners) {
        if (darkThemeProvider != nullptr) {
            gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(darkThemeProvider));
            g_object_unref(darkThemeProvider);
            darkThemeProvider = nullptr;
        }
        g_object_set_data(G_OBJECT(gtk_settings_get_default()), "remote-control-dark-mode", GINT_TO_POINTER(enabled));
        g_object_set_data_full(G_OBJECT(gtk_settings_get_default()), "remote-control-theme", g_strdup(theme.c_str()), g_free);
#ifdef _WIN32
        applyWindowCornerPreference(roundedCorners);
#endif
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
        std::string css = std::string("window, dialog, .background { background-color: ") + background + "; color: " + text + "; } #GraphicalContainer, #GraphicalBase, #GraphicalHeader, #GraphicalFixed { background-color: transparent; background-image: none; } label, checkbutton label, button label { color: " + std::string(text) + "; } entry { background-image: none; background-color: " + editor + "; color: " + text + "; caret-color: " + accent + "; border: 1px solid " + border + "; } entry:disabled { background-color: " + surface + "; color: #c1c1c1; } textview, textview text { background-color: " + editor + "; color: " + text + "; caret-color: " + accent + "; } combobox button, button { background-image: none; background-color: " + surface + "; color: " + text + "; border: 1px solid " + border + "; } button:hover, combobox button:hover { background-image: none; background-color: " + surface + "; } button:active, combobox button:active { background-image: none; background-color: " + editor + "; } button:disabled { background-image: none; background-color: " + surface + "; color: #828282; } checkbutton { color: " + text + "; } frame, expander { background-color: transparent; } separator, paned separator { background-color: " + border + "; min-height: 1px; min-width: 1px; } treeview.view, treeview.view header button, iconview.view, #remote-file-icon-view, list, list row { background-color: " + editor + "; color: " + text + "; border-color: " + border + "; } treeview.view:selected, iconview.view:selected, #remote-file-icon-view:selected, list row:selected { background-color: " + surface + "; color: #ffffff; } #remote-control-minimap, #remote-control-minimap.view { min-width: 120px; background-color: " + editor + "; color: " + text + "; border-left: 1px solid " + border + "; } #remote-control-minimap-marker { background-color: alpha(" + accent + ", 0.24); border: 1px solid alpha(" + accent + ", 0.72); } #remote-control-minimap .scrubber, #remote-control-minimap.scrubber { background-color: alpha(" + accent + ", 0.24); border: 1px solid alpha(" + accent + ", 0.72); } filechooser box, filechooser .path-bar, filechooser .path-bar button, filechooser .pathbar, filechooser .pathbar button { background-image: none; background-color: " + background + "; color: " + text + "; } filechooser placessidebar, filechooser placessidebar viewport, filechooser placessidebar list, filechooser placessidebar row, filechooser .sidebar, filechooser .sidebar viewport, filechooser .sidebar list, filechooser .sidebar row { background-color: " + editor + "; color: " + text + "; } filechooser placessidebar row:selected, filechooser .sidebar row:selected { background-color: " + surface + "; color: #ffffff; } menubar, menu { background-color: " + surface + "; color: " + text + "; } menuitem { color: " + text + "; } notebook, notebook > header, notebook > header > tabs, notebook > stack { background-color: transparent; border: none; box-shadow: none; outline: none; padding: 0; } scrolledwindow, viewport { background-color: transparent; border: none; box-shadow: none; outline: none; padding: 0; } notebook > header, notebook > header > tabs { min-height: 0; } notebook > header > tabs > tab { background-image: none; background-color: " + surface + "; border: 1px solid " + border + "; border-bottom: none; border-radius: 4px 4px 0 0; margin-right: 2px; padding: 2px 5px; } notebook > header > tabs > tab:checked { background-color: " + background + "; border-color: " + accent + "; } .gtk-source-completion, .gtk-source-completion-content, .gtk-source-completion-list { background-color: " + editor + "; color: " + text + "; border: 1px solid " + border + "; border-radius: 6px; } .gtk-source-completion-list { padding: 3px; } .gtk-source-completion-row { color: " + text + "; border-radius: 4px; padding: 4px 8px; } .gtk-source-completion-row:hover { background-color: " + surface + "; } .gtk-source-completion-row:selected { background-color: " + accent + "; color: " + editor + "; } .gtk-source-completion-info, .remote-completion-info { background-color: " + surface + "; color: " + text + "; border: 1px solid " + border + "; border-radius: 7px; padding: 8px 10px; } .remote-completion-signature { color: " + accent + "; font-weight: bold; } .remote-completion-details { color: " + text + "; }";
        css += std::string(" decoration { background-color: transparent; border: none; border-radius: 8px; box-shadow: none; } headerbar, .titlebar { min-height: 30px; padding: 0 4px; background-image: none; background-color: ") + surface + "; color: " + text + "; border: none; box-shadow: none; } headerbar button, .titlebar button { min-width: 24px; min-height: 24px; padding: 0 4px; background-image: none; background-color: " + surface + "; color: " + text + "; border: 1px solid " + border + "; box-shadow: none; } headerbar button:hover, .titlebar button:hover { background-color: " + border + "; } headerbar .title, .titlebar .title { color: " + text + "; }";
        css += std::string(" notebook > header > tabs > tab:checked, notebook > header > tabs > tab:focus { border-color: ") + border + "; border-bottom-color: transparent; box-shadow: none; outline: none; }";
        css += " notebook > header > tabs > tab:not(:checked) { margin-top: 1px; } notebook > header > tabs > tab:checked { margin-top: 0; margin-bottom: -1px; }";
        gtk_css_provider_load_from_data(provider, css.c_str(), -1, nullptr);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        GtkCssProvider* completionProvider = gtk_css_provider_new();
        const std::string completionCss = std::string("GtkSourceCompletionWindow, GtkSourceCompletionInfo, .remote-completion-info-window, .remote-completion-popup { background-color: ") + editor + "; color: " + text + "; border: 1px solid " + border + "; border-radius: 8px; padding: 4px; } GtkSourceCompletionWindow treeview, GtkSourceCompletionWindow treeview.view, .remote-completion-popup treeview, .remote-completion-popup treeview.view { background-color: " + editor + "; color: " + text + "; border: none; } GtkSourceCompletionWindow treeview.view row, .remote-completion-popup treeview.view row { color: " + text + "; border: none; border-radius: 5px; padding: 4px 7px; } GtkSourceCompletionWindow treeview.view row:hover, .remote-completion-popup treeview.view row:hover { background-color: " + surface + "; } GtkSourceCompletionWindow treeview.view row:selected, .remote-completion-popup treeview.view row:selected { background-color: " + accent + "; color: " + editor + "; } GtkSourceCompletionWindow treeview.view cell, .remote-completion-popup treeview.view cell { color: " + text + "; } GtkSourceCompletionInfo, .remote-completion-info { background-color: " + surface + "; color: " + text + "; border: 1px solid " + border + "; border-radius: 8px; padding: 6px 8px; } .remote-completion-signature { color: " + accent + "; font-weight: bold; } .remote-completion-details { color: " + text + "; }";
        const std::string completionPreviewCss = std::string(" .remote-completion-preview { background-color: ") + surface + "; border-top: 1px solid " + border + "; padding: 4px 6px; } .remote-completion-preview-kind { background-color: " + accent + "; color: " + editor + "; border-radius: 4px; padding: 1px 5px; font-weight: bold; } .remote-completion-preview-text { color: " + text + "; }";
        const std::string combinedCompletionCss = completionCss + completionPreviewCss;
        gtk_css_provider_load_from_data(completionProvider, combinedCompletionCss.c_str(), -1, nullptr);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(completionProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
        g_object_unref(completionProvider);
        darkThemeProvider = provider;
    }

}

void refreshGlobalVisibilityHotkey(const std::string& hotkey) {
#ifdef _WIN32
    visibilityHotkey = hotkey;
    if (serverListHotkeyWindow == nullptr) return;
    UnregisterHotKey(serverListHotkeyWindow, VisibilityHotkeyId);
    if (hotkey.empty()) return;
    UINT modifiers = MOD_NOREPEAT;
    UINT key = 0;
    std::size_t start = 0;
    while (start <= hotkey.size()) {
        const std::size_t separator = hotkey.find('+', start);
        std::string token = hotkey.substr(start, separator == std::string::npos ? std::string::npos : separator - start);
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (token == "ctrl" || token == "control") modifiers |= MOD_CONTROL;
        else if (token == "alt") modifiers |= MOD_ALT;
        else if (token == "shift") modifiers |= MOD_SHIFT;
        else if (token == "super" || token == "meta" || token == "win") modifiers |= MOD_WIN;
        else if (token.size() == 1) key = static_cast<UINT>(VkKeyScanA(static_cast<CHAR>(std::toupper(static_cast<unsigned char>(token[0])))) & 0xff);
        else if (token.size() > 1 && token[0] == 'f') {
            const int number = std::atoi(token.c_str() + 1);
            if (number >= 1 && number <= 24) key = VK_F1 + number - 1;
        } else if (token == "space") key = VK_SPACE;
        else if (token == "tab") key = VK_TAB;
        else if (token == "escape" || token == "esc") key = VK_ESCAPE;
        else if (token == "return" || token == "enter") key = VK_RETURN;
        else if (token == "home") key = VK_HOME;
        else if (token == "end") key = VK_END;
        else if (token == "page_up") key = VK_PRIOR;
        else if (token == "page_down") key = VK_NEXT;
        else if (token == "insert") key = VK_INSERT;
        else if (token == "delete") key = VK_DELETE;
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    if (key != 0) RegisterHotKey(serverListHotkeyWindow, VisibilityHotkeyId, modifiers, key);
#else
#ifdef RC_HAVE_X11
    GdkDisplay* display = gdk_display_get_default();
    if (display == nullptr || !GDK_IS_X11_DISPLAY(display)) return;
    Display* xDisplay = gdk_x11_display_get_xdisplay(display);
    const Window root = DefaultRootWindow(xDisplay);
    const std::array<unsigned int, 4> ignoredModifiers = {0U, static_cast<unsigned int>(LockMask), static_cast<unsigned int>(Mod2Mask), static_cast<unsigned int>(LockMask | Mod2Mask)};
    if (visibilityHotkeyKeycode != 0) for (unsigned int ignored : ignoredModifiers) XUngrabKey(xDisplay, visibilityHotkeyKeycode, visibilityHotkeyModifiers | ignored, root);
    visibilityHotkeyKeycode = 0;
    visibilityHotkeyModifiers = 0;
    if (!visibilityHotkeyFilterInstalled) { gdk_window_add_filter(nullptr, onX11Message, nullptr); visibilityHotkeyFilterInstalled = true; }
    if (hotkey.empty()) { XSync(xDisplay, False); return; }
    std::string keyName;
    std::size_t start = 0;
    while (start <= hotkey.size()) {
        const std::size_t separator = hotkey.find('+', start);
        std::string token = hotkey.substr(start, separator == std::string::npos ? std::string::npos : separator - start);
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (token == "ctrl" || token == "control") visibilityHotkeyModifiers |= ControlMask;
        else if (token == "alt") visibilityHotkeyModifiers |= Mod1Mask;
        else if (token == "shift") visibilityHotkeyModifiers |= ShiftMask;
        else if (token == "super" || token == "meta" || token == "win") visibilityHotkeyModifiers |= Mod4Mask;
        else keyName = token == "escape" ? "Escape" : token == "enter" ? "Return" : token == "page_up" ? "Page_Up" : token == "page_down" ? "Page_Down" : token;
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    if (keyName.size() > 1 && keyName[0] == 'f' && std::all_of(keyName.begin() + 1, keyName.end(), [](unsigned char value) { return std::isdigit(value) != 0; })) keyName[0] = 'F';
    const guint keyValue = keyName.size() == 1 ? gdk_unicode_to_keyval(static_cast<unsigned char>(keyName[0])) : gdk_keyval_from_name(keyName.c_str());
    visibilityHotkeyKeycode = keyValue == GDK_KEY_VoidSymbol ? 0 : XKeysymToKeycode(xDisplay, keyValue);
    if (visibilityHotkeyKeycode != 0) for (unsigned int ignored : ignoredModifiers) XGrabKey(xDisplay, visibilityHotkeyKeycode, visibilityHotkeyModifiers | ignored, root, True, GrabModeAsync, GrabModeAsync);
    XSync(xDisplay, False);
#else
    (void)hotkey;
#endif
#endif
}

void applyRemoteControlTheme(const std::string& theme, bool darkMode, bool roundedCorners) {
    applyTheme(theme, darkMode, roundedCorners);
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
    bool debugMode = false;
    bool packetLog = false;
    int argumentCount = 1;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument.rfind("--mcp-instance=", 0) == 0) mcpInstance = std::strtoul(argument.c_str() + 15, nullptr, 10);
        else if (argument == "--mcp-instance" && index + 1 < argc) mcpInstance = std::strtoul(argv[++index], nullptr, 10);
        else if (argument == "--debug") debugMode = true;
        else if (argument == "--packetlog") { packetLog = true; debugMode = true; }
        else argv[argumentCount++] = argv[index];
    }
    argc = argumentCount;
#ifdef _WIN32
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
#endif
    remoteControlDebug = debugMode;
    remoteControlPacketLog = packetLog;
    remoteControlDebugLog("debug logging enabled");
    const std::filesystem::path applicationDirectory = getApplicationDirectory();
    installRemoteControlCrashLogger(applicationDirectory);
#ifdef _WIN32
    const std::filesystem::path configDirectory = applicationDirectory;
    const std::filesystem::path dataDirectory = applicationDirectory;
    const std::filesystem::path cacheDirectory = applicationDirectory / "cache";
#else
    const std::filesystem::path configDirectory = std::filesystem::path(g_get_user_config_dir()) / "GScriptRC";
    const std::filesystem::path dataDirectory = std::filesystem::path(g_get_user_data_dir()) / "GScriptRC";
    const std::filesystem::path cacheDirectory = std::filesystem::path(g_get_user_cache_dir()) / "GScriptRC";
#endif
    std::filesystem::create_directories(configDirectory);
    std::filesystem::create_directories(dataDirectory);
    std::filesystem::create_directories(cacheDirectory);
    RC::setRCOptionsDirectory(configDirectory);
    std::filesystem::current_path(dataDirectory);
    setBackupDataDirectory(dataDirectory);
    const std::filesystem::path certificateBundle = applicationDirectory / "certs" / "ca-bundle.crt";
    if (std::filesystem::is_regular_file(certificateBundle)) g_setenv("SSL_CERT_FILE", certificateBundle.string().c_str(), true);
    configureGtkRuntime(applicationDirectory);
    registerBundledFonts(applicationDirectory);
    gtk_init(&argc, &argv);
#ifdef REMOTE_CONTROL_STATIC_WINDOWS
    (void)gtksourceview_get_resource();
#endif
    gtk_icon_theme_append_search_path(gtk_icon_theme_get_default(), (applicationDirectory / "share" / "icons").string().c_str());
    GError* iconError = nullptr;
    GdkPixbuf* applicationIcon = gdk_pixbuf_new_from_file((applicationDirectory / "images" / "rcicon.png").string().c_str(), &iconError);
    if (applicationIcon != nullptr) {
        gtk_window_set_default_icon(applicationIcon);
        g_object_set_data_full(G_OBJECT(gtk_settings_get_default()), "remote-control-icon-pixbuf", applicationIcon, g_object_unref);
    } else if (iconError != nullptr) g_error_free(iconError);
    RC::RCOptions options;
    copySyntaxFiles(applicationDirectory);
    RC::loadRCOptions(options, applicationDirectory);
    if (mcpMode) return runMcpServer(options, applicationDirectory, mcpInstance);
    setGScriptEditorCacheDirectory(cacheDirectory / "editors");
    setGScriptEditorOptions(options);
    applyRemoteControlTheme(options.theme, options.darkmode, options.roundedcorners);
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
    std::vector<std::unique_ptr<TRemoteFrame>> remoteFrames;
    std::function<void()> switchServer;
    std::function<void()> openAnotherServerList;
    TRemoteFrame* pendingServerSwitch = nullptr;
    TServerList* pendingServerSwitchSource = nullptr;
    TServerList serverList(applicationDirectory, [&] { if (remoteFrames.empty()) startFrame->show(); }, [&](TServerList* sourceList, void* connection, int serverIndex, const std::string& serverName, const std::string& nickname, const std::string& accountName, bool additional) {
        RC::loadRCOptions(options, applicationDirectory);
        applyRemoteControlTheme(options.theme, options.darkmode, options.roundedcorners);
        if (pendingServerSwitch != nullptr && pendingServerSwitchSource == sourceList && !additional) {
            TRemoteFrame* frame = pendingServerSwitch;
            pendingServerSwitch = nullptr;
            pendingServerSwitchSource = nullptr;
            frame->disconnect();
            frame->open(connection, serverIndex, serverName, nickname, accountName);
            trayRemoteFrame = frame;
            return;
        }
        pendingServerSwitch = nullptr;
        pendingServerSwitchSource = nullptr;
        auto frameSlot = std::make_shared<TRemoteFrame*>(nullptr);
        remoteFrames.push_back(std::make_unique<TRemoteFrame>(options, applicationDirectory, [sourceList, frameSlot, &pendingServerSwitch, &pendingServerSwitchSource] { pendingServerSwitch = *frameSlot; pendingServerSwitchSource = sourceList; sourceList->reopen(); }, [sourceList, frameSlot, &pendingServerSwitch, &pendingServerSwitchSource] { pendingServerSwitch = *frameSlot; pendingServerSwitchSource = sourceList; sourceList->reopen(); }, [sourceList] { sourceList->openListServerSettings(); }));
        TRemoteFrame* frame = remoteFrames.back().get();
        *frameSlot = frame;
        trayRemoteFrames.push_back(frame);
        trayRemoteFrame = frame;
        frame->open(connection, serverIndex, serverName, nickname, accountName);
    }, [] {}, options.darkmode, options.theme, [&](bool darkMode, const std::string& theme) {
        options.darkmode = darkMode;
        options.theme = theme;
        RC::saveRCOptions(options, applicationDirectory);
        applyRemoteControlTheme(theme, darkMode, options.roundedcorners);
        if (options.syncsyntaxtheme) options.syntaxtheme = theme == "dark" ? "language-spec" : theme;
        setRemoteControlSyntaxTheme(options.syntaxtheme);
        setGScriptEditorOptions(options);
        refreshGScriptEditorTheme();
        for (auto& frame : remoteFrames) frame->updateThemeOptions(options);
    }, [&] { return startFrame == nullptr ? std::vector<RC::RCAccount>() : startFrame->allAccounts(); }, [&](const std::string& name) { return startFrame == nullptr ? std::vector<RC::RCAccount>() : startFrame->accountsForListServer(name); }, [&] { openAnotherServerList(); });
    switchServer = [&] {
        serverList.reopen();
    };
    TStartFrame frame(options, applicationDirectory, [&](std::uint64_t accountId, const std::string& account, const std::string& password, const std::string& nickname, const std::string& listServer) { serverList.open(accountId, account, password, nickname, listServer); }, [&] { serverList.openListServerSettings(); }, [&] { return serverList.currentListServer(); });
    serverList.setLoginParent(frame.windowHandle());
    startFrame = &frame;
    openAnotherServerList = [&] { serverList.openAnotherListServer(); };
    trayStartFrame = startFrame;
    trayServerListOpen = [&] {
#ifdef _WIN32
        const HWND foreground = GetForegroundWindow();
        if (frame.nativeWindow() != nullptr && foreground == reinterpret_cast<HWND>(GDK_WINDOW_HWND(frame.nativeWindow()))) { switchServer(); return; }
        for (TRemoteFrame* remote : trayRemoteFrames) {
            if (remote == nullptr || remote->nativeWindow() == nullptr) continue;
            if (foreground == reinterpret_cast<HWND>(GDK_WINDOW_HWND(remote->nativeWindow()))) { remote->showServerList(); return; }
        }
#endif
        if (trayRemoteFrame != nullptr) trayRemoteFrame->showServerList(); else switchServer();
    };
    trayFrameSignOut = [&](TRemoteFrame* closing) {
        if (closing == nullptr) return;
        if (pendingServerSwitch == closing) { pendingServerSwitch = nullptr; pendingServerSwitchSource = nullptr; }
        trayPrimaryFrames.erase(closing);
        closing->signOut();
        trayRemoteFrames.erase(std::remove(trayRemoteFrames.begin(), trayRemoteFrames.end(), closing), trayRemoteFrames.end());
        remoteFrames.erase(std::remove_if(remoteFrames.begin(), remoteFrames.end(), [&](const auto& frame) { return frame.get() == closing; }), remoteFrames.end());
        trayRemoteFrame = trayRemoteFrames.empty() ? nullptr : trayRemoteFrames.back();
        if (trayRemoteFrame == nullptr) startFrame->show();
    };
    traySignOut = [&] { if (trayFrameSignOut) trayFrameSignOut(trayRemoteFrame); };
    auto activeRemoteFrame = [&]() -> TRemoteFrame* { return trayRemoteFrame; };
    startMcpGuiBridge(options, applicationDirectory, {
        [&activeRemoteFrame] { TRemoteFrame* frame = activeRemoteFrame(); return frame == nullptr ? std::string() : frame->currentServerName(); },
        [&activeRemoteFrame] { TRemoteFrame* frame = activeRemoteFrame(); return frame != nullptr && frame->isConnected(); },
        [&] { std::vector<std::string> names; for (TRemoteFrame* frame : trayRemoteFrames) if (frame != nullptr && frame->isConnected()) names.push_back(frame->currentServerName()); return names; },
        [&](const std::string& name, std::string& error) { for (TRemoteFrame* frame : trayRemoteFrames) if (frame != nullptr && frame->isConnected() && g_ascii_strcasecmp(frame->currentServerName().c_str(), name.c_str()) == 0) { trayRemoteFrame = frame; return true; } error = "Connected RC server session not found"; return false; },
        [&frame] { return frame.mcpVisible(); },
        [&frame] { return frame.mcpAccount(); },
        [&frame] { return frame.mcpNickname(); },
        [&frame] { return frame.mcpHasPassword(); },
        [&frame](const std::string& account, const std::string& nickname, std::string& error) { return frame.mcpSubmit(account, nickname, error); },
        [&serverList] { return serverList.mcpServerNames(); },
        [&serverList](const std::string& name, std::string& error) { return serverList.mcpConnect(name, error); },
        [&activeRemoteFrame](const std::string& view, std::string& error) { TRemoteFrame* frame = activeRemoteFrame(); if (frame == nullptr) { error = "No connected RC server window"; return false; } return frame->mcpOpenView(view, error); },
        [&activeRemoteFrame](const std::string& text, std::string& error) { TRemoteFrame* frame = activeRemoteFrame(); if (frame == nullptr) { error = "No connected RC server window"; return false; } return frame->mcpSendChat(text, error); }
    });
#ifdef _WIN32
    frame.show();
    if (GdkWindow* startWindow = frame.nativeWindow()) {
        gdk_window_add_filter(startWindow, onWindowsMessage, nullptr);
        serverListHotkeyWindow = reinterpret_cast<HWND>(GDK_WINDOW_HWND(startWindow));
        refreshGlobalVisibilityHotkey(options.globalhotkey);
    }
#else
    frame.show();
    refreshGlobalVisibilityHotkey(options.globalhotkey);
#endif
    gtk_main();
    stopMcpGuiBridge();
#ifdef _WIN32
    if (serverListHotkeyWindow != nullptr) {
        UnregisterHotKey(serverListHotkeyWindow, VisibilityHotkeyId);
        if (GdkWindow* startWindow = frame.nativeWindow()) gdk_window_remove_filter(startWindow, onWindowsMessage, nullptr);
    }
#endif
#if !defined(_WIN32) && defined(RC_HAVE_X11)
    refreshGlobalVisibilityHotkey("");
    if (visibilityHotkeyFilterInstalled) { gdk_window_remove_filter(nullptr, onX11Message, nullptr); visibilityHotkeyFilterInstalled = false; }
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
