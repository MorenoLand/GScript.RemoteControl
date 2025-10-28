#include "TFileBrowserTree.h"

#include <grclib.h>

#include <string>
#include <vector>

namespace {
    constexpr int FolderPathColumn = 0;
    constexpr int FolderRightsColumn = 1;
    constexpr int FolderDisplayColumn = 2;
    constexpr int FilePathColumn = 0;
    constexpr int FileRightsColumn = 1;
    constexpr int FileModifiedColumn = 2;
}

TFileBrowserTree::TFileBrowserTree() {
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
    folders = gtk_tree_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    files = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* folderView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(folders));
    GtkWidget* fileView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(files));
    GtkCellRenderer* text = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(folderView), gtk_tree_view_column_new_with_attributes("Folders", text, "text", FolderDisplayColumn, nullptr));
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

TFileBrowserTree::~TFileBrowserTree() { if (window != nullptr) gtk_widget_destroy(window); }

void TFileBrowserTree::open(void* nextConnection) {
    connection = nextConnection;
    rc_on_filebrowser_folders(connection, onFolders, this);
    rc_on_filebrowser_files(connection, onFiles, this);
    rc_on_filebrowser_message(connection, onMessage, this);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    refresh();
}

void TFileBrowserTree::onRefresh(GtkButton*, gpointer data) { static_cast<TFileBrowserTree*>(data)->refresh(); }
void TFileBrowserTree::onFolders(int, void* data) { static_cast<TFileBrowserTree*>(data)->refreshFolders(); }
void TFileBrowserTree::onFiles(const char* folder, int, void* data) { static_cast<TFileBrowserTree*>(data)->refreshFiles(folder); }
void TFileBrowserTree::onMessage(const char* message, void* data) { static_cast<TFileBrowserTree*>(data)->appendLog(message == nullptr ? "" : message); }
gboolean TFileBrowserTree::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TFileBrowserTree*>(data)->window); return true; }

void TFileBrowserTree::onFolderActivated(GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn*, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    GtkTreeIter row;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->folders), &row, path)) return;
    gchar* folder = nullptr;
    gtk_tree_model_get(GTK_TREE_MODEL(browser->folders), &row, FolderPathColumn, &folder, -1);
    if (folder != nullptr) {
        if (!rc_filebrowser_cd(browser->connection, folder)) browser->appendLog(rc_last_error(browser->connection));
        g_free(folder);
    }
}

void TFileBrowserTree::refresh() {
    if (connection != nullptr && !rc_filebrowser_start(connection)) appendLog(rc_last_error(connection));
}

void TFileBrowserTree::refreshFolders() {
    RCFileBrowserFolder* entries = nullptr;
    const int count = rc_copy_filebrowser_folders(connection, &entries);
    gtk_tree_store_clear(folders);
    for (int index = 0; index < count; ++index) addFolder(entries[index].pattern, entries[index].rights);
    rc_free_filebrowser_folders(entries, count);
}

void TFileBrowserTree::refreshFiles(const char* folder) {
    RCFileBrowserEntry* entries = nullptr;
    const int count = rc_copy_filebrowser_files(connection, &entries);
    gtk_list_store_clear(files);
    gtk_entry_set_text(GTK_ENTRY(folderPath), folder == nullptr ? "" : folder);
    for (int index = 0; index < count; ++index) {
        GtkTreeIter row;
        gtk_list_store_append(files, &row);
        const std::string modified = entries[index].modified == 0 ? "" : std::to_string(entries[index].modified);
        gtk_list_store_set(files, &row, FilePathColumn, entries[index].path == nullptr ? "" : entries[index].path, FileRightsColumn, entries[index].rights == nullptr ? "" : entries[index].rights, FileModifiedColumn, modified.c_str(), -1);
    }
    rc_free_filebrowser_files(entries, count);
}

void TFileBrowserTree::addFolder(const char* pattern, const char* rights) {
    if (pattern == nullptr || *pattern == '\0') return;
    std::string folderPath(pattern);
    const std::size_t wildcard = folderPath.find('*');
    if (wildcard != std::string::npos) folderPath.resize(wildcard);
    while (!folderPath.empty() && folderPath.back() == '/') folderPath.pop_back();
    if (folderPath.empty()) return;
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < folderPath.size()) {
        const std::size_t end = folderPath.find('/', start);
        const std::string part = folderPath.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) parts.push_back(part);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    GtkTreeIter parent;
    GtkTreeIter* parentPointer = nullptr;
    std::string fullPath;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        fullPath += parts[index];
        GtkTreeIter row;
        gboolean found = gtk_tree_model_iter_children(GTK_TREE_MODEL(folders), &row, parentPointer);
        while (found) {
            gchar* existing = nullptr;
            gtk_tree_model_get(GTK_TREE_MODEL(folders), &row, FolderPathColumn, &existing, -1);
            const bool matches = existing != nullptr && fullPath == existing;
            g_free(existing);
            if (matches) break;
            found = gtk_tree_model_iter_next(GTK_TREE_MODEL(folders), &row);
        }
        if (!found) {
            gtk_tree_store_append(folders, &row, parentPointer);
            const std::string label = parts[index] + "/";
            gtk_tree_store_set(folders, &row, FolderPathColumn, fullPath.c_str(), FolderRightsColumn, index + 1 == parts.size() && rights != nullptr ? rights : "", FolderDisplayColumn, label.c_str(), -1);
        }
        parent = row;
        parentPointer = &parent;
        fullPath += "/";
    }
}

void TFileBrowserTree::appendLog(const char* message) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, (std::string(message) + "\n").c_str(), -1);
}
