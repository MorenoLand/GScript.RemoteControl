#include "TLevelList.h"

#include <IEnums.h>
#include <grclib.h>
#include "TEditorFind.h"
#include "TGScriptEditor.h"
#include "TButtonIcons.h"
#include "TTheme.h"
#include <gtksourceview/gtksource.h>

#include <string>
#include <cstring>

TLevelList::TLevelList(GtkWindow* parent) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    applyRemoteControlWindowChrome(window);
    gtk_window_set_title(GTK_WINDOW(window), "Levels");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 460);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    GtkSourceBuffer* sourceBuffer = gtk_source_buffer_new(nullptr);
    applyRemoteControlSourceStyle(sourceBuffer);
    GtkWidget* text = gtk_source_view_new_with_buffer(sourceBuffer);
    configureGScriptEditor(text, false);
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text), true);
    addEditorFindShortcut(text);
    addEditorGoToLineShortcut(text);
    g_object_unref(sourceBuffer);
    gtk_container_add(GTK_CONTAINER(scrolled), text);
    gtk_box_pack_start(GTK_BOX(root), wrapGScriptEditor(text, scrolled), true, true, 0);
    GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* lineStatus = createGScriptEditorLineStatus(text);
    gtk_box_pack_start(GTK_BOX(bottom), lineStatus, false, false, 0);
    GtkWidget* spacer = gtk_label_new(nullptr);
    gtk_widget_set_hexpand(spacer, true);
    gtk_box_pack_start(GTK_BOX(bottom), spacer, true, true, 0);
    GtkWidget* findButton = editorIconButton("Find", "edit-find-symbolic");
    GtkWidget* applyButton = editorIconButton("Apply", "emblem-ok-symbolic");
    GtkWidget* closeButton = editorIconButton("Close", "window-close-symbolic");
    gtk_box_pack_end(GTK_BOX(bottom), closeButton, false, false, 0);
    gtk_box_pack_end(GTK_BOX(bottom), applyButton, false, false, 0);
    gtk_box_pack_end(GTK_BOX(bottom), findButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(root), bottom, false, false, 0);
    g_signal_connect(findButton, "clicked", G_CALLBACK(editorFind), text);
    gtk_widget_add_events(findButton, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(findButton, "button-press-event", G_CALLBACK(editorFindButtonPress), text);
    g_signal_connect(applyButton, "clicked", G_CALLBACK(onApply), this);
    g_signal_connect(closeButton, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}
TLevelList::~TLevelList() { if (window != nullptr) gtk_widget_destroy(window); }
void TLevelList::setServerName(const std::string& server) { serverName = server; gtk_window_set_title(GTK_WINDOW(window), serverName.empty() ? "Levels" : ("Levels - " + serverName).c_str()); }
void TLevelList::hide() { if (window != nullptr) gtk_widget_hide(window); }
void TLevelList::open(void* nextConnection) {
    if (nextConnection == nullptr) return;
    connection = nextConnection;
    gtk_text_buffer_set_text(buffer, "Loading levels...", -1);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    const char emptyPayload[] = "";
    if (!rc_send_nc_packet(connection, PLI_NC_LEVELLISTGET, emptyPayload, 0)) setContent("Unable to request the local level list.");
}
void TLevelList::setContent(const char* content) {
    std::string value = content == nullptr ? "" : content;
    for (char& character : value) if (character == '\r') character = '\n';
    while (value.find("\n\n") != std::string::npos) value.replace(value.find("\n\n"), 2, "\n");
    gtk_text_buffer_set_text(buffer, value.c_str(), -1);
}
void TLevelList::onApply(GtkButton*, gpointer data) { static_cast<TLevelList*>(data)->apply(); }
void TLevelList::onClose(GtkButton*, gpointer data) { static_cast<TLevelList*>(data)->hide(); }
gboolean TLevelList::onDelete(GtkWidget*, GdkEvent*, gpointer data) { static_cast<TLevelList*>(data)->hide(); return true; }
void TLevelList::apply() {
    if (connection == nullptr) return;
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar* content = gtk_text_buffer_get_text(buffer, &start, &end, false);
    char* tokenized = rc_gtokenize(content == nullptr ? "" : content);
    rc_send_nc_packet(connection, PLI_NC_LEVELLISTSET, tokenized == nullptr ? "" : tokenized, tokenized == nullptr ? 0 : static_cast<int>(strlen(tokenized)));
    rc_free(tokenized);
    g_free(content);
}
