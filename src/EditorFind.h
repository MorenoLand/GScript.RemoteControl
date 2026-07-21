#pragma once

#include <algorithm>
#include <cstdlib>
#include <gtk/gtk.h>
#include <string>

inline void openEditorFind(GtkWidget* editor) {
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

inline void editorFind(GtkButton*, gpointer data) { openEditorFind(GTK_WIDGET(data)); }

inline void openEditorGoToLine(GtkWidget* editor) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
    GtkTextIter current;
    gtk_text_buffer_get_iter_at_mark(buffer, &current, gtk_text_buffer_get_insert(buffer));
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Go to line", GTK_WINDOW(gtk_widget_get_toplevel(editor)), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Go", GTK_RESPONSE_ACCEPT, nullptr);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_text(GTK_ENTRY(entry), std::to_string(gtk_text_iter_get_line(&current) + 1).c_str());
    gtk_widget_set_size_request(entry, 120, -1);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, false, false, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const long requested = std::strtol(gtk_entry_get_text(GTK_ENTRY(entry)), nullptr, 10);
        const int lineCount = gtk_text_buffer_get_line_count(buffer);
        const int line = std::clamp(static_cast<int>(requested) - 1, 0, std::max(0, lineCount - 1));
        GtkTextIter target;
        gtk_text_buffer_get_iter_at_line(buffer, &target, line);
        gtk_text_buffer_place_cursor(buffer, &target);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(editor), &target, 0.2, false, 0.0, 0.0);
    }
    gtk_widget_destroy(dialog);
}

inline void editorGoToLine(GtkButton*, gpointer data) { openEditorGoToLine(GTK_WIDGET(data)); }

inline gboolean editorFindKey(GtkWidget*, GdkEventKey* event, gpointer data) {
    if ((event->state & GDK_CONTROL_MASK) == 0 || (event->keyval != GDK_KEY_f && event->keyval != GDK_KEY_F)) return FALSE;
    openEditorFind(GTK_WIDGET(data));
    return TRUE;
}

inline gboolean editorGoToLineKey(GtkWidget*, GdkEventKey* event, gpointer data) {
    if ((event->state & GDK_CONTROL_MASK) == 0 || (event->keyval != GDK_KEY_g && event->keyval != GDK_KEY_G)) return FALSE;
    openEditorGoToLine(GTK_WIDGET(data));
    return TRUE;
}

inline void addEditorFindShortcut(GtkWidget* editor) { g_signal_connect(editor, "key-press-event", G_CALLBACK(editorFindKey), editor); }
inline void addEditorGoToLineShortcut(GtkWidget* editor) { g_signal_connect(editor, "key-press-event", G_CALLBACK(editorGoToLineKey), editor); }

inline void addEditorFindButton(GtkWidget* dialog, GtkWidget* editor) {
    GtkWidget* goToLineButton = gtk_button_new_with_label("Go to line");
    GtkWidget* button = gtk_button_new_with_label("Find");
    gtk_widget_set_tooltip_text(goToLineButton, "Go to line (Ctrl+G)");
    gtk_widget_set_tooltip_text(button, "Find (Ctrl+F)");
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_action_area(GTK_DIALOG(dialog))), goToLineButton);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_action_area(GTK_DIALOG(dialog))), button);
    g_signal_connect(goToLineButton, "clicked", G_CALLBACK(editorGoToLine), editor);
    g_signal_connect(button, "clicked", G_CALLBACK(editorFind), editor);
    addEditorFindShortcut(editor);
    addEditorGoToLineShortcut(editor);
    gtk_widget_show(goToLineButton);
    gtk_widget_show(button);
}
