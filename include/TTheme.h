#pragma once

#include <gtk/gtk.h>
#if __has_include(<gtksourceview/gtksource.h>)
#include <gtksourceview/gtksource.h>
#define RC_HAVE_GTKSOURCEVIEW 1
#endif
#include <string>

void applyRemoteControlTheme(const std::string& theme, bool darkMode, bool roundedCorners);

#ifdef _WIN32
#include <windows.h>
#include <gdk/gdkwin32.h>

struct DwmMargins {
    int cxLeftWidth;
    int cxRightWidth;
    int cyTopHeight;
    int cyBottomHeight;
};

inline bool remoteControlRoundedCorners() {
    GtkSettings* settings = gtk_settings_get_default();
    if (settings == nullptr) return true;
    gpointer data = g_object_get_data(G_OBJECT(settings), "remote-control-rounded-corners");
    if (data == nullptr) return true;
    return GPOINTER_TO_INT(data) != 0;
}

inline void applyNativeWindowChromeStyling(HWND hwnd, bool roundedCorners) {
    if (hwnd == nullptr) return;
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    using DwmEnableBlurBehindWindowFn = HRESULT(WINAPI*)(HWND, const void*);
    static HMODULE dwmModule = LoadLibraryW(L"dwmapi.dll");
    static auto setAttribute = dwmModule == nullptr ? nullptr : reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwmModule, "DwmSetWindowAttribute"));
    static auto enableBlurBehind = dwmModule == nullptr ? nullptr : reinterpret_cast<DwmEnableBlurBehindWindowFn>(GetProcAddress(dwmModule, "DwmEnableBlurBehindWindow"));

    // Disable DWM blur-behind to completely eliminate the frosted Aero Glass halo
    if (enableBlurBehind != nullptr) {
        struct {
            DWORD dwFlags;
            BOOL fEnable;
            HRGN hRgnBlur;
            BOOL fTransitionOnMaximized;
        } bb = { 1 /* DWM_BB_ENABLE */, FALSE, nullptr, FALSE };
        enableBlurBehind(hwnd, &bb);
    }

    // Enable native drop shadow on window class
    SetClassLongPtrW(hwnd, GCL_STYLE, GetClassLongPtrW(hwnd, GCL_STYLE) | CS_DROPSHADOW);

    if (setAttribute != nullptr) {
        // Windows 11 corner preference: 2 = DWMWCP_ROUND, 1 = DWMWCP_DONOTROUND
        const int preference = roundedCorners ? 2 : 1;
        setAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &preference, sizeof(preference));
    }
}

inline void onNativeWindowChromeRealizeOrMap(GtkWidget* w, gpointer) {
    if (w == nullptr) return;
    GdkWindow* gdkWindow = gtk_widget_get_window(w);
    if (gdkWindow == nullptr) return;
    HWND hwnd = reinterpret_cast<HWND>(gdk_win32_window_get_handle(gdkWindow));
    if (hwnd == nullptr) return;
    applyNativeWindowChromeStyling(hwnd, remoteControlRoundedCorners());
}
#else
inline bool remoteControlRoundedCorners() {
    return true;
}
#endif

inline void applyRemoteControlWindowChrome(GtkWidget* widget) {
    if (widget == nullptr || !GTK_IS_WINDOW(widget)) return;
    GtkWindow* window = GTK_WINDOW(widget);
    GtkSettings* settings = gtk_settings_get_default();
    GdkPixbuf* icon = settings == nullptr ? nullptr : static_cast<GdkPixbuf*>(g_object_get_data(G_OBJECT(settings), "remote-control-icon-pixbuf"));
    if (icon != nullptr) gtk_window_set_icon(window, icon);
    if (gtk_window_get_type_hint(window) == GDK_WINDOW_TYPE_HINT_DROPDOWN_MENU || gtk_window_get_type_hint(window) == GDK_WINDOW_TYPE_HINT_POPUP_MENU) return;
    if (gtk_window_get_window_type(window) == GTK_WINDOW_POPUP) return;

#ifdef _WIN32
    if (gtk_widget_get_realized(widget)) {
        onNativeWindowChromeRealizeOrMap(widget, nullptr);
    } else {
        g_signal_connect(widget, "realize", G_CALLBACK(onNativeWindowChromeRealizeOrMap), nullptr);
    }
    g_signal_connect(widget, "map", G_CALLBACK(onNativeWindowChromeRealizeOrMap), nullptr);
#endif

    GtkWidget* titlebar = gtk_window_get_titlebar(window);
    if (titlebar != nullptr && GTK_IS_HEADER_BAR(titlebar)) {
        if (icon != nullptr && g_object_get_data(G_OBJECT(titlebar), "remote-control-title-icon") == nullptr) {
            GdkPixbuf* scaledIcon = gdk_pixbuf_scale_simple(icon, 16, 16, GDK_INTERP_BILINEAR);
            GtkWidget* titleIcon = gtk_image_new_from_pixbuf(scaledIcon);
            g_object_unref(scaledIcon);
            gtk_image_set_pixel_size(GTK_IMAGE(titleIcon), 16);
            gtk_widget_set_size_request(titleIcon, 16, 16);
            gtk_widget_set_margin_start(titleIcon, 7);
            gtk_header_bar_pack_start(GTK_HEADER_BAR(titlebar), titleIcon);
            g_object_set_data(G_OBJECT(titlebar), "remote-control-title-icon", titleIcon);
        }
        return;
    }
    GtkWidget* header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), true);
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(header), ":minimize,maximize,close");
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), gtk_window_get_title(window));
    if (icon != nullptr) {
        GdkPixbuf* scaledIcon = gdk_pixbuf_scale_simple(icon, 16, 16, GDK_INTERP_BILINEAR);
        GtkWidget* titleIcon = gtk_image_new_from_pixbuf(scaledIcon);
        g_object_unref(scaledIcon);
        gtk_image_set_pixel_size(GTK_IMAGE(titleIcon), 16);
        gtk_widget_set_size_request(titleIcon, 16, 16);
        gtk_widget_set_margin_start(titleIcon, 7);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), titleIcon);
        g_object_set_data(G_OBJECT(header), "remote-control-title-icon", titleIcon);
    }
    gtk_window_set_titlebar(window, header);
    g_signal_connect(window, "notify::title", G_CALLBACK(+[](GtkWindow* changedWindow, GParamSpec*, gpointer data) {
        GtkWidget* changedTitlebar = GTK_WIDGET(data);
        if (GTK_IS_HEADER_BAR(changedTitlebar)) gtk_header_bar_set_title(GTK_HEADER_BAR(changedTitlebar), gtk_window_get_title(changedWindow));
    }), header);
}

inline void setRemoteControlSyntaxTheme(const std::string& theme) {
    GtkSettings* settings = gtk_settings_get_default();
    if (settings != nullptr) g_object_set_data_full(G_OBJECT(settings), "remote-control-syntax-theme", g_strdup(theme.c_str()), g_free);
}

inline bool remoteControlDarkMode() {
    GtkSettings* settings = gtk_settings_get_default();
    return settings != nullptr && GPOINTER_TO_INT(g_object_get_data(G_OBJECT(settings), "remote-control-dark-mode")) != 0;
}

inline std::string remoteControlTheme() {
    GtkSettings* settings = gtk_settings_get_default();
    if (settings == nullptr) return "dark";
    const char* theme = static_cast<const char*>(g_object_get_data(G_OBJECT(settings), "remote-control-theme"));
    return theme == nullptr ? "dark" : theme;
}

inline std::string remoteControlSyntaxTheme() {
    GtkSettings* settings = gtk_settings_get_default();
    if (settings == nullptr) return "language-spec";
    const char* theme = static_cast<const char*>(g_object_get_data(G_OBJECT(settings), "remote-control-syntax-theme"));
    return theme == nullptr ? "language-spec" : theme;
}

#ifdef RC_HAVE_GTKSOURCEVIEW
inline void applyRemoteControlSourceStyle(GtkSourceBuffer* buffer) {
    if (buffer == nullptr) return;
    GtkSourceStyleSchemeManager* manager = gtk_source_style_scheme_manager_get_default();
    const std::string theme = remoteControlSyntaxTheme();
    const char* schemeId = theme == "dracula" ? "dracula" : theme == "material" ? "material" : theme == "ayu-mirage" ? "ayu-mirage" : theme == "nord" ? "nord" : theme == "monokai" ? "monokai" : theme == "one-dark" ? "one-dark" : theme == "tokyo-night" ? "tokyo-night" : theme == "gruvbox" ? "gruvbox" : theme == "solarized" ? "solarized" : theme == "catppuccin" ? "catppuccin" : theme == "light" ? "classic" : "graalcolors";
    GtkSourceStyleScheme* scheme = gtk_source_style_scheme_manager_get_scheme(manager, schemeId);
    if (scheme != nullptr) gtk_source_buffer_set_style_scheme(buffer, scheme);
}
#endif
