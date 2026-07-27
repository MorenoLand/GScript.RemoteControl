#pragma once

#include <gtk/gtk.h>

inline void applyGtkButtonIcon(GtkWidget* button, const char* stockId) { if (button == nullptr) return; gtk_button_set_image(GTK_BUTTON(button), gtk_image_new_from_stock(stockId, GTK_ICON_SIZE_BUTTON)); gtk_button_set_always_show_image(GTK_BUTTON(button), true); }
