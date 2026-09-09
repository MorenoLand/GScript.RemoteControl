#include "TFileBrowser.h"

#include <grclib.h>

#include <string>

namespace {
    constexpr int FolderPathColumn = 0;
    constexpr int FolderRightsColumn = 1;
    constexpr int FilePathColumn = 0;
    constexpr int FileRightsColumn = 1;
    constexpr int FileModifiedColumn = 2;
}

TFileBrowser::TFileBrowser() {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "File Browser");
    gtk_window_set_default_size(GTK_WINDOW(window), 700, 480);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(root), 5);
    gtk_container_add(GTK_CONTAINER(window), root);

    GtkWidget* pathRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(pathRow), gtk_label_new("Folder:"), false, false, 0);
    folderPath = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(folderPath), false);
    gtk_box_pack_start(GTK_BOX(pathRow), folderPath, true, true, 0);
    GtkWidget* refreshButton = gtk_button_new_with_label("Refresh");
    gtk_box_pack_start(GTK_BOX(pathRow), refreshButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(root), pathRow, false, false, 0);

    GtkWidget* panes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget* folderScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkWidget* fileScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(folderScrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(fileScrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    folders = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    files = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* folderView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(folders));
    GtkWidget* fileView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(files));
    GtkCellRenderer* text = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(folderView), gtk_tree_view_column_new_with_attributes("Folders", text, "text", FolderPathColumn, nullptr));
    text = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(folderView), gtk_tree_view_column_new_with_attributes("Rights", text, "text", FolderRightsColumn, nullptr));
    text = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(fileView), gtk_tree_view_column_new_with_attributes("Files", text, "text", FilePathColumn, nullptr));
    text = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(fileView), gtk_tree_view_column_new_with_attributes("Rights", text, "text", FileRightsColumn, nullptr));
    text = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(fileView), gtk_tree_view_column_new_with_attributes("Modified", text, "text", FileModifiedColumn, nullptr));
    gtk_container_add(GTK_CONTAINER(folderScrolled), folderView);
    gtk_container_add(GTK_CONTAINER(fileScrolled), fileView);
    gtk_paned_pack1(GTK_PANED(panes), folderScrolled, true, false);
    gtk_paned_pack2(GTK_PANED(panes), fileScrolled, true, false);
    gtk_box_pack_start(GTK_BOX(root), panes, true, true, 0);

    GtkWidget* logScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_size_request(logScrolled, -1, 90);
    log = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log), false);
    gtk_container_add(GTK_CONTAINER(logScrolled), log);
    gtk_box_pack_start(GTK_BOX(root), logScrolled, false, true, 0);

    g_signal_connect(refreshButton, "clicked", G_CALLBACK(onRefresh), this);
    g_signal_connect(folderView, "row-activated", G_CALLBACK(onFolderActivated), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TFileBrowser::~TFileBrowser() {
    if (window != nullptr) gtk_widget_destroy(window);
    if (folders != nullptr) g_object_unref(folders);
    if (files != nullptr) g_object_unref(files);
}

void TFileBrowser::open(void* nextConnection) {
    if (nextConnection == nullptr) return;
    connection = nextConnection;
    rc_on_filebrowser_folders(connection, onFolders, this);
    rc_on_filebrowser_files(connection, onFiles, this);
    rc_on_filebrowser_message(connection, onMessage, this);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    refresh();
}

void TFileBrowser::onRefresh(GtkButton*, gpointer data) { static_cast<TFileBrowser*>(data)->refresh(); }

void TFileBrowser::onFolderActivated(GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn*, gpointer data) {
    TFileBrowser* browser = static_cast<TFileBrowser*>(data);
    if (browser->connection == nullptr) return;
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->folders), &iter, path)) return;
    gchar* folder = nullptr;
    gtk_tree_model_get(GTK_TREE_MODEL(browser->folders), &iter, FolderPathColumn, &folder, -1);
    if (folder != nullptr) {
        if (!rc_filebrowser_cd(browser->connection, folder)) browser->appendLog(rc_last_error(browser->connection));
        g_free(folder);
    }
}

gboolean TFileBrowser::onDelete(GtkWidget*, GdkEvent*, gpointer data) {
    gtk_widget_hide(static_cast<TFileBrowser*>(data)->window);
    return true;
}

void TFileBrowser::onFolders(int, void* data) { static_cast<TFileBrowser*>(data)->refreshFolders(); }
void TFileBrowser::onFiles(const char* folder, int, void* data) { static_cast<TFileBrowser*>(data)->refreshFiles(folder); }
void TFileBrowser::onMessage(const char* message, void* data) { static_cast<TFileBrowser*>(data)->appendLog(message == nullptr ? "" : message); }

void TFileBrowser::refresh() {
    if (connection == nullptr) return;
    if (!rc_filebrowser_start(connection)) appendLog(rc_last_error(connection));
}

void TFileBrowser::refreshFolders() {
    if (connection == nullptr) return;
    RCFileBrowserFolder* entries = nullptr;
    const int count = rc_copy_filebrowser_folders(connection, &entries);
    gtk_list_store_clear(folders);
    for (int index = 0; index < count; ++index) {
        GtkTreeIter iter;
        gtk_list_store_append(folders, &iter);
        gtk_list_store_set(folders, &iter, FolderPathColumn, entries[index].pattern == nullptr ? "" : entries[index].pattern, FolderRightsColumn, entries[index].rights == nullptr ? "" : entries[index].rights, -1);
    }
    rc_free_filebrowser_folders(entries, count);
}

void TFileBrowser::refreshFiles(const char* folder) {
    if (connection == nullptr) return;
    RCFileBrowserEntry* entries = nullptr;
    const int count = rc_copy_filebrowser_files(connection, &entries);
    gtk_list_store_clear(files);
    gtk_entry_set_text(GTK_ENTRY(folderPath), folder == nullptr ? "" : folder);
    for (int index = 0; index < count; ++index) {
        GtkTreeIter iter;
        gtk_list_store_append(files, &iter);
        const std::string modified = entries[index].modified == 0 ? "" : std::to_string(entries[index].modified);
        gtk_list_store_set(files, &iter, FilePathColumn, entries[index].path == nullptr ? "" : entries[index].path, FileRightsColumn, entries[index].rights == nullptr ? "" : entries[index].rights, FileModifiedColumn, modified.c_str(), -1);
    }
    rc_free_filebrowser_files(entries, count);
}

void TFileBrowser::appendLog(const char* message) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, (std::string(message) + "\n").c_str(), -1);
}
