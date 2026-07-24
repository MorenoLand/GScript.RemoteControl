#pragma once

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <string>

void applyRemoteControlTheme(const std::string& theme, bool darkMode);

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

inline void applyRemoteControlSourceStyle(GtkSourceBuffer* buffer) {
    if (buffer == nullptr) return;
    GtkSourceStyleSchemeManager* manager = gtk_source_style_scheme_manager_get_default();
    const std::string theme = remoteControlTheme();
    const char* schemeId = theme == "dracula" ? "dracula" : theme == "material" ? "material" : theme == "ayu-mirage" ? "ayu-mirage" : theme == "nord" ? "nord" : theme == "monokai" ? "monokai" : theme == "one-dark" ? "one-dark" : theme == "light" ? "classic" : "graalcolors";
    GtkSourceStyleScheme* scheme = gtk_source_style_scheme_manager_get_scheme(manager, schemeId);
    if (scheme != nullptr) gtk_source_buffer_set_style_scheme(buffer, scheme);
}
