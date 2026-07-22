#include "TServerTextEditor.h"
#include "Theme.h"
#include "Backup.h"
#include "EditorFind.h"
#include "GScriptEditor.h"

#include <grclib.h>
#include <gtksourceview/gtksource.h>

TServerTextEditor::TServerTextEditor(Kind nextKind, const char* title) : kind(nextKind) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 460);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkSourceLanguage* language = gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(), "ini");
    GtkSourceBuffer* sourceBuffer = language != nullptr ? gtk_source_buffer_new_with_language(language) : gtk_source_buffer_new(nullptr);
    applyRemoteControlSourceStyle(sourceBuffer);
    GtkWidget* text = gtk_source_view_new_with_buffer(sourceBuffer);
    configureGScriptEditor(text);
    g_object_unref(sourceBuffer);
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text), true);
    addEditorFindShortcut(text);
    addEditorGoToLineShortcut(text);
    g_signal_connect(text, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer data) {
        if ((event->state & GDK_CONTROL_MASK) == 0 || (event->keyval != GDK_KEY_s && event->keyval != GDK_KEY_S)) return static_cast<gboolean>(FALSE);
        static_cast<TServerTextEditor*>(data)->save();
        return static_cast<gboolean>(TRUE);
    }), this);
    gtk_container_add(GTK_CONTAINER(scrolled), text);
    gtk_box_pack_start(GTK_BOX(root), scrolled, true, true, 0);
    GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* lineStatus = createGScriptEditorLineStatus(text);
    gtk_box_pack_start(GTK_BOX(bottom), lineStatus, false, false, 0);
    GtkWidget* spacer = gtk_label_new(nullptr);
    gtk_widget_set_hexpand(spacer, true);
    gtk_box_pack_start(GTK_BOX(bottom), spacer, true, true, 0);
    GtkWidget* saveButton = gtk_button_new_with_label("Apply");
    GtkWidget* goToLineButton = gtk_button_new_with_label("Go to line");
    GtkWidget* findButton = gtk_button_new_with_label("Find");
    gtk_widget_set_tooltip_text(goToLineButton, "Go to line (Ctrl+G)");
    gtk_widget_set_tooltip_text(findButton, "Find (Ctrl+F)");
    GtkWidget* closeButton = gtk_button_new_with_label("Close");
    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(actions), goToLineButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(actions), findButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(actions), saveButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(actions), closeButton, false, false, 0);
    gtk_widget_set_margin_top(actions, 3);
    gtk_widget_set_margin_bottom(actions, 3);
    gtk_widget_set_margin_end(actions, 5);
    gtk_box_pack_end(GTK_BOX(bottom), actions, false, false, 0);
    gtk_box_pack_start(GTK_BOX(root), bottom, false, false, 0);
    g_signal_connect(goToLineButton, "clicked", G_CALLBACK(editorGoToLine), text);
    g_signal_connect(findButton, "clicked", G_CALLBACK(editorFind), text);
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

void TServerTextEditor::setContent(const char* content) {
    const std::string text = content == nullptr ? "" : content;
    const std::string type = kind == Kind::ServerOptions ? "serveroptions" : kind == Kind::ServerFlags ? "serverflags" : "folderconfig";
    backupEditorText(type, "", text, false);
    gtk_text_buffer_set_text(buffer, text.c_str(), -1);
}
void TServerTextEditor::onSave(GtkButton*, gpointer data) { static_cast<TServerTextEditor*>(data)->save(); }
void TServerTextEditor::onClose(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TServerTextEditor*>(data)->window); }
gboolean TServerTextEditor::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TServerTextEditor*>(data)->window); return true; }

void TServerTextEditor::save() {
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar* content = gtk_text_buffer_get_text(buffer, &start, &end, false);
    const std::string type = kind == Kind::ServerOptions ? "serveroptions" : kind == Kind::ServerFlags ? "serverflags" : "folderconfig";
    backupEditorText(type, "", content == nullptr ? "" : content, true);
    if (kind == Kind::ServerOptions) rc_upload_server_options(connection, content);
    else if (kind == Kind::ServerFlags) rc_upload_server_flags(connection, content);
    else rc_upload_folder_config(connection, content);
    g_free(content);
}
