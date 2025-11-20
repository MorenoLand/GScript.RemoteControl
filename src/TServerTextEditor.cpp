#include "TServerTextEditor.h"

#include <grclib.h>

TServerTextEditor::TServerTextEditor(Kind nextKind, const char* title) : kind(nextKind) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 460);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkWidget* text = gtk_text_view_new();
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text), true);
    gtk_container_add(GTK_CONTAINER(scrolled), text);
    gtk_box_pack_start(GTK_BOX(root), scrolled, true, true, 5);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* saveButton = gtk_button_new_with_label("Save");
    GtkWidget* closeButton = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), saveButton);
    gtk_container_add(GTK_CONTAINER(buttons), closeButton);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 5);
    g_signal_connect(saveButton, "clicked", G_CALLBACK(onSave), this);
    g_signal_connect(closeButton, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TServerTextEditor::~TServerTextEditor() { if (window != nullptr) gtk_widget_destroy(window); }

void TServerTextEditor::open(void* nextConnection) {
    connection = nextConnection;
    if (kind == Kind::ServerOptions) rc_request_server_options(connection);
    else if (kind == Kind::ServerFlags) rc_request_server_flags(connection);
    else rc_request_folder_config(connection);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

void TServerTextEditor::setContent(const char* content) { gtk_text_buffer_set_text(buffer, content == nullptr ? "" : content, -1); }
void TServerTextEditor::onSave(GtkButton*, gpointer data) { static_cast<TServerTextEditor*>(data)->save(); }
void TServerTextEditor::onClose(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TServerTextEditor*>(data)->window); }
gboolean TServerTextEditor::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TServerTextEditor*>(data)->window); return true; }

void TServerTextEditor::save() {
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar* content = gtk_text_buffer_get_text(buffer, &start, &end, false);
    if (kind == Kind::ServerOptions) rc_upload_server_options(connection, content);
    else if (kind == Kind::ServerFlags) rc_upload_server_flags(connection, content);
    else rc_upload_folder_config(connection, content);
    g_free(content);
}
