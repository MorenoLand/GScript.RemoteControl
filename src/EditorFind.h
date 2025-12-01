#pragma once

#include <gtk/gtk.h>

inline void editorFind(GtkButton*, gpointer data) {
    GtkWidget* editor = GTK_WIDGET(data);
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Find", GTK_WINDOW(gtk_widget_get_toplevel(editor)), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Find", GTK_RESPONSE_ACCEPT, nullptr);
    GtkWidget* entry = gtk_entry_new();
    gtk_widget_set_size_request(entry, 260, -1);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, false, false, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char* query = gtk_entry_get_text(GTK_ENTRY(entry));
        if (query != nullptr && *query != '\0') {
            GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
            GtkTextIter start;
            GtkTextIter matchStart;
            GtkTextIter matchEnd;
            gtk_text_buffer_get_iter_at_mark(buffer, &start, gtk_text_buffer_get_insert(buffer));
            gboolean found = gtk_text_iter_forward_search(&start, query, GTK_TEXT_SEARCH_CASE_INSENSITIVE, &matchStart, &matchEnd, nullptr);
            if (!found) {
                gtk_text_buffer_get_start_iter(buffer, &start);
                found = gtk_text_iter_forward_search(&start, query, GTK_TEXT_SEARCH_CASE_INSENSITIVE, &matchStart, &matchEnd, nullptr);
            }
            if (found) {
                gtk_text_buffer_select_range(buffer, &matchStart, &matchEnd);
                gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(editor), &matchStart, 0.2, false, 0.0, 0.0);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

inline void addEditorFindButton(GtkWidget* dialog, GtkWidget* editor) {
    GtkWidget* button = gtk_button_new_with_label("Find");
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_action_area(GTK_DIALOG(dialog))), button);
    g_signal_connect(button, "clicked", G_CALLBACK(editorFind), editor);
    gtk_widget_show(button);
}
