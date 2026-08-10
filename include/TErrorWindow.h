#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <gtk/gtk.h>

struct ErrorWindowState { std::function<void()> onClosed; bool handled = false; };

inline GtkWidget* createErrorWindow(const char* title, const char* message, GtkWindow* parent = nullptr, std::function<void()> onClosed = {}) {
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "ErrorWindow");
    gtk_container_set_border_width(GTK_CONTAINER(window), 5);
    gtk_window_set_title(GTK_WINDOW(window), title == nullptr ? "Question" : title);
    if (parent != nullptr) { gtk_window_set_transient_for(GTK_WINDOW(window), parent); gtk_window_set_modal(GTK_WINDOW(window), true); gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ON_PARENT); }
    else gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 120);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), box);
    const std::string text = message == nullptr || *message == '\0' ? "Question?" : message;
    GtkWidget* label = gtk_label_new(text.c_str());
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
    auto* state = new ErrorWindowState{std::move(onClosed), false};
    g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* state = static_cast<ErrorWindowState*>(data);
        if (!state->handled) { state->handled = true; auto callback = std::move(state->onClosed); delete state; if (callback) callback(); }
        else delete state;
    }), state);
    g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { gtk_widget_destroy(GTK_WIDGET(data)); }), window);
    gtk_widget_show_all(window);
    if (parent != nullptr) {
        struct Placement { GtkWidget* dialog; GtkWindow* parent; };
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, +[](gpointer data) -> gboolean {
            auto* placement = static_cast<Placement*>(data);
            gint parentX = 0, parentY = 0, parentWidth = 0, parentHeight = 0, dialogWidth = 0, dialogHeight = 0;
            gtk_window_get_position(placement->parent, &parentX, &parentY);
            gtk_window_get_size(placement->parent, &parentWidth, &parentHeight);
            gtk_window_get_size(GTK_WINDOW(placement->dialog), &dialogWidth, &dialogHeight);
            gtk_window_move(GTK_WINDOW(placement->dialog), parentX + std::max(0, (parentWidth - dialogWidth) / 2), parentY + std::max(0, (parentHeight - dialogHeight) / 2));
            return G_SOURCE_REMOVE;
        }, new Placement{window, parent}, +[](gpointer data) { delete static_cast<Placement*>(data); });
    }
    gtk_widget_grab_focus(button);
    return window;
}
