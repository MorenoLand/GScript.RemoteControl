#pragma once

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

inline bool remoteControlDarkMode() {
    GtkSettings* settings = gtk_settings_get_default();
    return settings != nullptr && GPOINTER_TO_INT(g_object_get_data(G_OBJECT(settings), "remote-control-dark-mode")) != 0;
}

inline void applyRemoteControlSourceStyle(GtkSourceBuffer* buffer) {
    if (buffer == nullptr) return;
    GtkSourceStyleSchemeManager* manager = gtk_source_style_scheme_manager_get_default();
    GtkSourceStyleScheme* scheme = gtk_source_style_scheme_manager_get_scheme(manager, remoteControlDarkMode() ? "graalcolors" : "classic");
    if (scheme != nullptr) gtk_source_buffer_set_style_scheme(buffer, scheme);
}
