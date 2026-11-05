#pragma once

#include <gtk/gtk.h>
#if __has_include(<gtksourceview/gtksource.h>)
#include <gtksourceview/gtksource.h>
#define RC_HAVE_GTKSOURCEVIEW 1
#endif
#include <string>

void applyRemoteControlTheme(const std::string& theme, bool darkMode, bool roundedCorners);

inline bool remoteControlRoundedCorners() {
    GtkSettings* settings = gtk_settings_get_default();
    if (settings == nullptr) return true;
    gpointer data = g_object_get_data(G_OBJECT(settings), "remote-control-rounded-corners");
    if (data == nullptr) return true;
    return GPOINTER_TO_INT(data) != 0;
}

inline void rcActivateEntriesByDefault(GtkWidget* widget) {
    if (widget == nullptr) return;
    if (GTK_IS_ENTRY(widget)) gtk_entry_set_activates_default(GTK_ENTRY(widget), true);
    if (GTK_IS_CONTAINER(widget)) gtk_container_forall(GTK_CONTAINER(widget), +[](GtkWidget* child, gpointer) { rcActivateEntriesByDefault(child); }, nullptr);
}

inline void rcPolishDialogOnMap(GtkWidget* mapped, gpointer) {
    GtkDialog* dialog = GTK_DIALOG(mapped);
    struct ResponseIcon { int response; const char* icon; bool affirmative; };
    const ResponseIcon responses[] = {
        {GTK_RESPONSE_CANCEL, "window-close-symbolic", false}, {GTK_RESPONSE_CLOSE, "window-close-symbolic", false},
        {GTK_RESPONSE_ACCEPT, "emblem-ok-symbolic", true}, {GTK_RESPONSE_OK, "emblem-ok-symbolic", true}, {GTK_RESPONSE_APPLY, "emblem-ok-symbolic", true}};
    int defaultResponse = 0;
    for (const ResponseIcon& entry : responses) {
        GtkWidget* button = gtk_dialog_get_widget_for_response(dialog, entry.response);
        if (button == nullptr || !GTK_IS_BUTTON(button)) continue;
        if (gtk_button_get_image(GTK_BUTTON(button)) == nullptr) {
            gtk_button_set_image(GTK_BUTTON(button), gtk_image_new_from_icon_name(entry.icon, GTK_ICON_SIZE_BUTTON));
            gtk_button_set_always_show_image(GTK_BUTTON(button), true);
        }
        if (entry.affirmative && defaultResponse == 0) defaultResponse = entry.response;
    }
    if (defaultResponse != 0) {
        if (gtk_window_get_default_widget(GTK_WINDOW(dialog)) == nullptr) gtk_dialog_set_default_response(dialog, defaultResponse);
        rcActivateEntriesByDefault(gtk_dialog_get_content_area(dialog));
    }
}

inline void rcPolishDialog(GtkWidget* dialogWidget) {
    if (dialogWidget == nullptr || !GTK_IS_DIALOG(dialogWidget) || g_object_get_data(G_OBJECT(dialogWidget), "rc-dialog-polished") != nullptr) return;
    g_object_set_data(G_OBJECT(dialogWidget), "rc-dialog-polished", GINT_TO_POINTER(1));
    g_signal_connect(dialogWidget, "map", G_CALLBACK(rcPolishDialogOnMap), nullptr);
}

inline void applyRemoteControlWindowChrome(GtkWidget* widget) {
    if (widget == nullptr || !GTK_IS_WINDOW(widget)) return;
    GtkWindow* window = GTK_WINDOW(widget);
    rcPolishDialog(widget);
    GtkSettings* settings = gtk_settings_get_default();
    GdkPixbuf* icon = settings == nullptr ? nullptr : static_cast<GdkPixbuf*>(g_object_get_data(G_OBJECT(settings), "remote-control-icon-pixbuf"));
    if (icon != nullptr) gtk_window_set_icon(window, icon);
    if (gtk_window_get_type_hint(window) == GDK_WINDOW_TYPE_HINT_DROPDOWN_MENU || gtk_window_get_type_hint(window) == GDK_WINDOW_TYPE_HINT_POPUP_MENU) return;
    if (gtk_window_get_window_type(window) == GTK_WINDOW_POPUP) return;

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
