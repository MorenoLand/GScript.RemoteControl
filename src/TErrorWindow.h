#pragma once

#include <gtk/gtk.h>

inline GtkWidget* createErrorWindow(const char* title, const char* message) {
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "ErrorWindow");
    gtk_container_set_border_width(GTK_CONTAINER(window), 5);
    gtk_window_set_title(GTK_WINDOW(window), title == nullptr ? "Question" : title);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 120);
    gtk_window_set_resizable(GTK_WINDOW(window), false);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), box);
    GtkWidget* label = gtk_label_new(message == nullptr || *message == '\0' ? "Question?" : message);
    gtk_widget_set_name(label, "ErrorMsgLabel");
    gtk_label_set_line_wrap(GTK_LABEL(label), true);
    gtk_box_pack_start(GTK_BOX(box), label, true, true, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), buttons, false, true, 0);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(buttons), 5);
    GtkWidget* button = gtk_button_new_from_stock(GTK_STOCK_OK);
    gtk_widget_set_size_request(button, 80, 24);
    gtk_container_add(GTK_CONTAINER(buttons), button);
    g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { gtk_widget_destroy(GTK_WIDGET(data)); }), window);
    gtk_widget_show_all(window);
    gtk_widget_grab_focus(button);
    return window;
}
