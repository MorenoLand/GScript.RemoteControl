#pragma once

#include <algorithm>
#include <string>
#include <gtk/gtk.h>

inline GtkWidget* createErrorWindow(const char* title, const char* message, GtkWindow* parent = nullptr) {
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "ErrorWindow");
    gtk_container_set_border_width(GTK_CONTAINER(window), 5);
    gtk_window_set_title(GTK_WINDOW(window), title == nullptr ? "Question" : title);
    if (parent != nullptr) { gtk_window_set_transient_for(GTK_WINDOW(window), parent); gtk_window_set_modal(GTK_WINDOW(window), true); gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ON_PARENT); }
    else gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 120);
    gtk_window_set_resizable(GTK_WINDOW(window), false);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), box);
    std::string text = message == nullptr || *message == '\0' ? "Question?" : message;
    for (std::size_t comma = text.find(','); comma != std::string::npos; comma = text.find(',', comma + 2)) {
        std::size_t next = comma + 1;
        while (next < text.size() && text[next] == ' ') ++next;
        text.replace(comma + 1, next - comma - 1, "\n");
    }
    GtkWidget* label = gtk_label_new(text.c_str());
    gtk_widget_set_name(label, "ErrorMsgLabel");
    gtk_label_set_line_wrap(GTK_LABEL(label), true);
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 64);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_margin_start(label, 8);
    gtk_widget_set_margin_end(label, 8);
    gtk_widget_set_margin_top(label, 8);
    gtk_widget_set_margin_bottom(label, 8);
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
    if (parent != nullptr) {
        struct Placement { GtkWidget* dialog; GtkWindow* parent; };
        g_idle_add(+[](gpointer data) -> gboolean {
            auto* placement = static_cast<Placement*>(data);
            gint parentX = 0, parentY = 0, parentWidth = 0, parentHeight = 0, dialogWidth = 0, dialogHeight = 0;
            gtk_window_get_position(placement->parent, &parentX, &parentY);
            gtk_window_get_size(placement->parent, &parentWidth, &parentHeight);
            gtk_window_get_size(GTK_WINDOW(placement->dialog), &dialogWidth, &dialogHeight);
            gtk_window_move(GTK_WINDOW(placement->dialog), parentX + std::max(0, (parentWidth - dialogWidth) / 2), parentY + std::max(0, (parentHeight - dialogHeight) / 2));
            delete placement;
            return G_SOURCE_REMOVE;
        }, new Placement{window, parent});
    }
    gtk_widget_grab_focus(button);
    return window;
}
