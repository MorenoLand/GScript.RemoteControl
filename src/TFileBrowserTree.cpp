#include "TFileBrowserTree.h"
#include "EditorFind.h"
#include "GScriptEditor.h"

#include <grclib.h>
#include <gtksourceview/gtksource.h>

#include <string>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {
    constexpr int FolderIconColumn = 0;
    constexpr int FolderPathColumn = 1;
    constexpr int FolderRightsColumn = 2;
    constexpr int FolderDisplayColumn = 3;
    constexpr int FileIconColumn = 0;
    constexpr int FilePathColumn = 1;
    constexpr int FileRightsColumn = 2;
    constexpr int FileSizeColumn = 3;
    constexpr int FileModifiedColumn = 4;
    constexpr int FileSizeSortColumn = 5;
    constexpr int FileModifiedSortColumn = 6;
    struct FileMenuItem { TFileBrowserTree* browser; std::string path; };
    void destroyFileMenuItem(gpointer data, GClosure*) { delete static_cast<FileMenuItem*>(data); }

    std::string formatModified(int timestamp) {
        if (timestamp <= 0) return "";
        const std::time_t value = timestamp;
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &value);
#else
        localtime_r(&value, &local);
#endif
        std::ostringstream stream;
        stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
        return stream.str();
    }

    GdkPixbuf* loadImage(const char* name) { return gdk_pixbuf_new_from_file((std::string("images/") + name).c_str(), nullptr); }

    GdkPixbuf* fileIcon(const RCFileBrowserEntry& entry, GdkPixbuf* text, GdkPixbuf* nw, GdkPixbuf* graal, GdkPixbuf* gmap) {
        const std::string path = entry.path == nullptr ? "" : entry.path;
        if (path.ends_with(".nw")) return nw;
        if (path.ends_with(".gmap")) return gmap;
        if (path.ends_with(".graal")) return graal;
        return text;
    }
}

TFileBrowserTree::TFileBrowserTree() {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "File Browser");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(window, "FileBrowser");
    gtk_container_set_border_width(GTK_CONTAINER(root), 5);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* frame = gtk_frame_new(" Files ");
    gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
    gtk_box_pack_start(GTK_BOX(root), frame, true, true, 0);
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(content), 5);
    gtk_container_add(GTK_CONTAINER(frame), content);
    folderPath = gtk_label_new("Current Folder:");
    gtk_misc_set_alignment(GTK_MISC(folderPath), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(content), folderPath, false, false, 5);
    GtkWidget* panes = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_position(GTK_PANED(panes), 366);
    gtk_box_pack_start(GTK_BOX(content), panes, true, true, 0);
    GtkWidget* filePanes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(filePanes), 200);
    gtk_paned_pack1(GTK_PANED(panes), filePanes, false, true);
    GtkWidget* folderScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkWidget* fileScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(folderScrolled), GTK_SHADOW_IN);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(fileScrolled), GTK_SHADOW_IN);
    folders = gtk_tree_store_new(4, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(folders), FolderDisplayColumn, GTK_SORT_ASCENDING);
    files = gtk_list_store_new(7, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
    folderView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(folders));
    fileView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(files));
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(fileView)), GTK_SELECTION_MULTIPLE);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(folderView), false);
    GtkTreeViewColumn* folderColumn = gtk_tree_view_column_new();
    GtkCellRenderer* image = gtk_cell_renderer_pixbuf_new();
    GtkCellRenderer* text = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(folderColumn, image, false);
    gtk_tree_view_column_add_attribute(folderColumn, image, "pixbuf", FolderIconColumn);
    gtk_tree_view_column_pack_start(folderColumn, text, true);
    gtk_tree_view_column_add_attribute(folderColumn, text, "text", FolderDisplayColumn);
    gtk_tree_view_append_column(GTK_TREE_VIEW(folderView), folderColumn);
    image = gtk_cell_renderer_pixbuf_new();
    text = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* nameColumn = gtk_tree_view_column_new_with_attributes("Name", image, "pixbuf", FileIconColumn, nullptr);
    gtk_tree_view_column_pack_start(nameColumn, text, true);
    gtk_tree_view_column_add_attribute(nameColumn, text, "text", FilePathColumn);
    gtk_tree_view_column_set_resizable(nameColumn, true);
    gtk_tree_view_column_set_fixed_width(nameColumn, 290);
    gtk_tree_view_column_set_sort_column_id(nameColumn, FilePathColumn);
    gtk_tree_view_append_column(GTK_TREE_VIEW(fileView), nameColumn);
    const struct { const char* name; int column; int sortColumn; int width; } columns[] = {{"Rights", FileRightsColumn, FileRightsColumn, 60}, {"Size", FileSizeColumn, FileSizeSortColumn, 60}, {"Modified", FileModifiedColumn, FileModifiedSortColumn, 145}};
    for (const auto& column : columns) {
        text = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* viewColumn = gtk_tree_view_column_new_with_attributes(column.name, text, "text", column.column, nullptr);
        gtk_tree_view_column_set_resizable(viewColumn, true);
        gtk_tree_view_column_set_fixed_width(viewColumn, column.width);
        gtk_tree_view_column_set_sort_column_id(viewColumn, column.sortColumn);
        gtk_tree_view_append_column(GTK_TREE_VIEW(fileView), viewColumn);
    }
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(files), FilePathColumn, GTK_SORT_ASCENDING);
    gtk_container_add(GTK_CONTAINER(folderScrolled), folderView);
    gtk_container_add(GTK_CONTAINER(fileScrolled), fileView);
    gtk_paned_pack1(GTK_PANED(filePanes), folderScrolled, false, true);
    gtk_paned_pack2(GTK_PANED(filePanes), fileScrolled, true, true);
    GtkWidget* logScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(logScrolled), GTK_SHADOW_IN);
    log = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log), false);
    GtkCssProvider* logProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(logProvider, "textview, textview text { background-color: #1e1e1e; color: #d4d4d4; }", -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(log), GTK_STYLE_PROVIDER(logProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(logProvider);
    gtk_container_add(GTK_CONTAINER(logScrolled), log);
    gtk_paned_pack2(GTK_PANED(panes), logScrolled, true, true);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    gtk_container_set_border_width(GTK_CONTAINER(buttons), 5);
    GtkWidget* closeButton = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), closeButton);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, true, 0);
    closedFolderIcon = loadImage("rcfiles_folderclosed.png");
    openFolderIcon = loadImage("rcfiles_folderopen.png");
    textFileIcon = loadImage("rcfiles_text.png");
    nwFileIcon = loadImage("rcfiles_nw.png");
    graalFileIcon = loadImage("rcfiles_graal.png");
    gmapFileIcon = loadImage("rcfiles_gmap.png");
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(folderView)), "changed", G_CALLBACK(onFolderSelected), this);
    g_signal_connect(folderView, "button-press-event", G_CALLBACK(onFolderButtonPress), this);
    g_signal_connect(fileView, "button-press-event", G_CALLBACK(onFileButtonPress), this);
    GtkTargetEntry fileTarget[] = {{const_cast<gchar*>("application/x-remote-control-file-path"), GTK_TARGET_SAME_APP, 1}};
    GtkTargetEntry dropTargets[] = {{const_cast<gchar*>("application/x-remote-control-file-path"), GTK_TARGET_SAME_APP, 1}, {const_cast<gchar*>("text/uri-list"), 0, 2}};
    gtk_drag_source_set(fileView, GDK_BUTTON1_MASK, fileTarget, G_N_ELEMENTS(fileTarget), GDK_ACTION_MOVE);
    gtk_drag_dest_set(folderView, GTK_DEST_DEFAULT_ALL, dropTargets, G_N_ELEMENTS(dropTargets), static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    gtk_drag_dest_set(fileView, GTK_DEST_DEFAULT_ALL, dropTargets, G_N_ELEMENTS(dropTargets), static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    g_signal_connect(fileView, "drag-data-get", G_CALLBACK(onFileDragDataGet), this);
    g_signal_connect(folderView, "drag-data-received", G_CALLBACK(onDropDataReceived), this);
    g_signal_connect(fileView, "drag-data-received", G_CALLBACK(onDropDataReceived), this);
    g_signal_connect(closeButton, "clicked", G_CALLBACK(onRefresh), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TFileBrowserTree::~TFileBrowserTree() {
    if (closedFolderIcon != nullptr) g_object_unref(closedFolderIcon);
    if (openFolderIcon != nullptr) g_object_unref(openFolderIcon);
    if (textFileIcon != nullptr) g_object_unref(textFileIcon);
    if (nwFileIcon != nullptr) g_object_unref(nwFileIcon);
    if (graalFileIcon != nullptr) g_object_unref(graalFileIcon);
    if (gmapFileIcon != nullptr) g_object_unref(gmapFileIcon);
    if (window != nullptr) gtk_widget_destroy(window);
}

void TFileBrowserTree::open(void* nextConnection) {
    connection = nextConnection;
    rc_on_filebrowser_folders(connection, onFolders, this);
    rc_on_filebrowser_files(connection, onFiles, this);
    rc_on_filebrowser_message(connection, onMessage, this);
    rc_on_file_received(connection, onFileReceived, this);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    refresh();
}

void TFileBrowserTree::onRefresh(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TFileBrowserTree*>(data)->window); }
void TFileBrowserTree::onFolders(int, void* data) { static_cast<TFileBrowserTree*>(data)->refreshFolders(); }
void TFileBrowserTree::onFiles(const char* folder, int count, void* data) { static_cast<TFileBrowserTree*>(data)->refreshFiles(folder, count); }
void TFileBrowserTree::onMessage(const char* message, void* data) { static_cast<TFileBrowserTree*>(data)->appendLog(message == nullptr ? "" : message); }
gboolean TFileBrowserTree::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TFileBrowserTree*>(data)->window); return true; }

void TFileBrowserTree::onFolderSelected(GtkTreeSelection* selection, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(selection, nullptr, &row)) return;
    gchar* folder = nullptr;
    gtk_tree_model_get(GTK_TREE_MODEL(browser->folders), &row, FolderPathColumn, &folder, -1);
    if (folder != nullptr) {
        std::string folderPath(folder);
        if (!folderPath.empty() && folderPath.back() != '/') folderPath += '/';
        if (!rc_filebrowser_cd(browser->connection, folderPath.c_str())) browser->appendLog(rc_last_error(browser->connection));
        g_free(folder);
    }
}

gboolean TFileBrowserTree::onFolderButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY) return false;
    static_cast<TFileBrowserTree*>(data)->showItemMenu(widget, event, true);
    return true;
}

gboolean TFileBrowserTree::onFileButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    if (event->type == GDK_2BUTTON_PRESS && event->button == GDK_BUTTON_PRIMARY) {
        GtkTreePath* path = nullptr;
        if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), static_cast<gint>(event->x), static_cast<gint>(event->y), &path, nullptr, nullptr, nullptr)) return false;
        GtkTreeIter row;
        GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
        gchar* itemPath = nullptr;
        if (gtk_tree_model_get_iter(model, &row, path)) gtk_tree_model_get(model, &row, FilePathColumn, &itemPath, -1);
        gtk_tree_path_free(path);
        if (itemPath != nullptr && *itemPath != '\0') {
            browser->pendingEditPath = itemPath;
            if (!rc_filebrowser_download(browser->connection, itemPath)) browser->appendLog(rc_last_error(browser->connection));
        }
        g_free(itemPath);
        return true;
    }
    if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY) return false;
    browser->showItemMenu(widget, event, false);
    return true;
}

void TFileBrowserTree::onFileDragDataGet(GtkWidget* widget, GdkDragContext*, GtkSelectionData* selection, guint, guint, gpointer) {
    GtkTreeSelection* selected = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
    GtkTreeModel* model = nullptr;
    GList* rows = gtk_tree_selection_get_selected_rows(selected, &model);
    std::string paths;
    for (GList* node = rows; node != nullptr; node = node->next) {
        GtkTreeIter row;
        gchar* path = nullptr;
        if (gtk_tree_model_get_iter(model, &row, static_cast<GtkTreePath*>(node->data))) gtk_tree_model_get(model, &row, FilePathColumn, &path, -1);
        if (path != nullptr && *path != '\0') { if (!paths.empty()) paths += '\n'; paths += path; }
        g_free(path);
        gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
    }
    g_list_free(rows);
    if (!paths.empty()) gtk_selection_data_set(selection, gtk_selection_data_get_target(selection), 8, reinterpret_cast<const guchar*>(paths.data()), static_cast<gint>(paths.size()));
}

void TFileBrowserTree::onDropDataReceived(GtkWidget* widget, GdkDragContext* context, gint x, gint y, GtkSelectionData* selection, guint info, guint time, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    std::string destination = browser->currentFolder;
    if (widget == browser->folderView) {
        GtkTreePath* rowPath = nullptr;
        if (gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(widget), x, y, &rowPath, nullptr)) {
            GtkTreeIter row;
            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->folders), &row, rowPath)) {
                gchar* value = nullptr;
                gtk_tree_model_get(GTK_TREE_MODEL(browser->folders), &row, FolderPathColumn, &value, -1);
                if (value != nullptr) destination = value;
                g_free(value);
            }
            gtk_tree_path_free(rowPath);
        }
    }
    bool success = false;
    if (info == 1) {
        const guchar* source = gtk_selection_data_get_data(selection);
        const gint length = gtk_selection_data_get_length(selection);
        if (source != nullptr && length > 0) {
            success = true;
            std::istringstream files(std::string(reinterpret_cast<const char*>(source), length));
            for (std::string filePath; std::getline(files, filePath);) if (!filePath.empty() && !rc_filebrowser_move(browser->connection, destination.c_str(), filePath.c_str())) { browser->appendLog(rc_last_error(browser->connection)); success = false; }
        }
    } else if (info == 2) {
        gchar** uris = g_uri_list_extract_uris(reinterpret_cast<const gchar*>(gtk_selection_data_get_data(selection)));
        if (uris != nullptr) {
            success = true;
            for (int index = 0; uris[index] != nullptr; ++index) {
                GError* error = nullptr;
                gchar* filename = g_filename_from_uri(uris[index], nullptr, &error);
                gchar* contents = nullptr;
                gsize length = 0;
                if (filename == nullptr || !g_file_get_contents(filename, &contents, &length, &error)) {
                    success = false;
                    if (error != nullptr) { browser->appendLog(error->message); g_error_free(error); }
                } else {
                    gchar* basename = g_path_get_basename(filename);
                    std::string remotePath = destination;
                    if (!remotePath.empty() && remotePath.back() != '/') remotePath += '/';
                    remotePath += basename;
                    if (!rc_upload_file(browser->connection, remotePath.c_str(), contents, static_cast<int>(length))) { browser->appendLog(rc_last_error(browser->connection)); success = false; }
                    g_free(basename);
                    g_free(contents);
                }
                g_free(filename);
            }
            g_strfreev(uris);
        }
    }
    if (!success) browser->appendLog(rc_last_error(browser->connection));
    gtk_drag_finish(context, success, false, time);
}

void TFileBrowserTree::showItemMenu(GtkWidget* view, GdkEventButton* event, bool folder) {
    GtkTreePath* path = nullptr;
    if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view), static_cast<gint>(event->x), static_cast<gint>(event->y), &path, nullptr, nullptr, nullptr)) return;
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_unselect_all(selection);
    gtk_tree_selection_select_path(selection, path);
    GtkTreeIter row;
    GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
    gtk_tree_model_get_iter(model, &row, path);
    gchar* itemPath = nullptr;
    gtk_tree_model_get(model, &row, folder ? FolderPathColumn : FilePathColumn, &itemPath, -1);
    gtk_tree_path_free(path);
    if (itemPath == nullptr || *itemPath == '\0') { g_free(itemPath); return; }
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* upload = gtk_menu_item_new_with_label("Upload file(s)");
    g_signal_connect(upload, "activate", G_CALLBACK(onUpload), this);
    if (!folder) {
        GtkWidget* download = gtk_menu_item_new_with_label("Download");
        GtkWidget* editAsText = gtk_menu_item_new_with_label("Edit");
        GtkWidget* move = gtk_menu_item_new_with_label("Move");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), download);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), editAsText);
        FileMenuItem* item = new FileMenuItem{this, itemPath};
        FileMenuItem* editItem = new FileMenuItem{this, itemPath};
        FileMenuItem* moveItem = new FileMenuItem{this, itemPath};
        g_signal_connect_data(download, "activate", G_CALLBACK(onDownload), item, destroyFileMenuItem, G_CONNECT_AFTER);
        g_signal_connect_data(editAsText, "activate", G_CALLBACK(onEditAsText), editItem, destroyFileMenuItem, G_CONNECT_AFTER);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), upload);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), move);
        g_signal_connect_data(move, "activate", G_CALLBACK(onMove), moveItem, destroyFileMenuItem, G_CONNECT_AFTER);
    } else {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), upload);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    }
    GtkWidget* rename = gtk_menu_item_new_with_label("Rename");
    GtkWidget* remove = gtk_menu_item_new_with_label("Delete");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), rename);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), remove);
    FileMenuItem* renameItem = new FileMenuItem{this, itemPath};
    FileMenuItem* deleteItem = new FileMenuItem{this, itemPath};
    g_signal_connect_data(rename, "activate", G_CALLBACK(onRename), renameItem, destroyFileMenuItem, G_CONNECT_AFTER);
    g_signal_connect_data(remove, "activate", G_CALLBACK(onDeleteItem), deleteItem, destroyFileMenuItem, G_CONNECT_AFTER);
    g_free(itemPath);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
}

void TFileBrowserTree::onDownload(GtkMenuItem*, gpointer data) {
    FileMenuItem* item = static_cast<FileMenuItem*>(data);
    if (!rc_filebrowser_download(item->browser->connection, item->path.c_str())) item->browser->appendLog(rc_last_error(item->browser->connection));
}

void TFileBrowserTree::onEditAsText(GtkMenuItem*, gpointer data) {
    FileMenuItem* item = static_cast<FileMenuItem*>(data);
    item->browser->pendingEditPath = item->path;
    if (!rc_filebrowser_download(item->browser->connection, item->path.c_str())) item->browser->appendLog(rc_last_error(item->browser->connection));
}

void TFileBrowserTree::onDeleteItem(GtkMenuItem*, gpointer data) {
    FileMenuItem* item = static_cast<FileMenuItem*>(data);
    if (!rc_filebrowser_delete(item->browser->connection, item->path.c_str())) item->browser->appendLog(rc_last_error(item->browser->connection));
    else if (!item->browser->currentFolder.empty() && !rc_filebrowser_cd(item->browser->connection, item->browser->currentFolder.c_str())) item->browser->appendLog(rc_last_error(item->browser->connection));
}

void TFileBrowserTree::onRename(GtkMenuItem*, gpointer data) {
    FileMenuItem* item = static_cast<FileMenuItem*>(data);
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Rename", GTK_WINDOW(item->browser->window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Rename", GTK_RESPONSE_ACCEPT, nullptr);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), item->path.c_str());
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, false, false, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        if (!rc_filebrowser_rename(item->browser->connection, item->path.c_str(), gtk_entry_get_text(GTK_ENTRY(entry)))) item->browser->appendLog(rc_last_error(item->browser->connection));
        else if (!item->browser->currentFolder.empty() && !rc_filebrowser_cd(item->browser->connection, item->browser->currentFolder.c_str())) item->browser->appendLog(rc_last_error(item->browser->connection));
    }
    gtk_widget_destroy(dialog);
}

void TFileBrowserTree::onMove(GtkMenuItem*, gpointer data) {
    FileMenuItem* item = static_cast<FileMenuItem*>(data);
    GtkWidget* dialog = gtk_dialog_new_with_buttons(("Move " + item->path).c_str(), GTK_WINDOW(item->browser->window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "OK", GTK_RESPONSE_ACCEPT, nullptr);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), item->browser->currentFolder.c_str());
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, false, false, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT && !rc_filebrowser_move(item->browser->connection, gtk_entry_get_text(GTK_ENTRY(entry)), item->path.c_str())) item->browser->appendLog(rc_last_error(item->browser->connection));
    gtk_widget_destroy(dialog);
}

void TFileBrowserTree::onUpload(GtkMenuItem*, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Upload file(s)", GTK_WINDOW(browser->window), GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Upload", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), true);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GSList* filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
        for (GSList* node = filenames; node != nullptr; node = node->next) {
            gchar* contents = nullptr;
            gsize length = 0;
            GError* error = nullptr;
            const gchar* filename = static_cast<const gchar*>(node->data);
            if (!g_file_get_contents(filename, &contents, &length, &error)) {
                browser->appendLog(error == nullptr ? "Unable to read selected file" : error->message);
                if (error != nullptr) g_error_free(error);
            } else {
                gchar* basename = g_path_get_basename(filename);
                std::string remotePath = browser->currentFolder;
                if (!remotePath.empty() && remotePath.back() != '/') remotePath += '/';
                remotePath += basename;
                if (!rc_upload_file(browser->connection, remotePath.c_str(), contents, static_cast<int>(length))) browser->appendLog(rc_last_error(browser->connection));
                g_free(basename);
                g_free(contents);
            }
            g_free(node->data);
        }
        g_slist_free(filenames);
    }
    gtk_widget_destroy(dialog);
}

void TFileBrowserTree::onFileReceived(const char* path, const void* content, int length, void* data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    if (path == nullptr || browser->pendingEditPath != path) return;
    browser->pendingEditPath.clear();
    browser->showTextEditor(path, content, length);
}

void TFileBrowserTree::showTextEditor(const char* path, const void* content, int length) {
    struct EditorState { TFileBrowserTree* browser; std::string path; GtkWidget* editor; };
    GtkWidget* dialog = gtk_dialog_new_with_buttons(path, GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 520);
    GtkSourceLanguage* language = gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(), "graal");
    GtkSourceBuffer* sourceBuffer = language != nullptr ? gtk_source_buffer_new_with_language(language) : gtk_source_buffer_new(nullptr);
    GtkSourceStyleScheme* scheme = gtk_source_style_scheme_manager_get_scheme(gtk_source_style_scheme_manager_get_default(), "graalcolors");
    if (scheme != nullptr) gtk_source_buffer_set_style_scheme(sourceBuffer, scheme);
    GtkWidget* editor = gtk_source_view_new_with_buffer(sourceBuffer);
    configureGScriptEditor(editor);
    addEditorFindButton(dialog, editor);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(editor), true);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(sourceBuffer), static_cast<const char*>(content), length);
    g_object_unref(sourceBuffer);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scrolled), editor);
    GtkWidget* contentArea = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(contentArea), 0);
    gtk_box_set_spacing(GTK_BOX(contentArea), 0);
    gtk_widget_set_margin_top(scrolled, 0);
    gtk_box_pack_start(GTK_BOX(contentArea), scrolled, true, true, 0);
    auto* state = new EditorState{this, path, editor};
    g_signal_connect(editor, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer responseDialog) {
        if ((event->state & GDK_CONTROL_MASK) == 0 || (event->keyval != GDK_KEY_s && event->keyval != GDK_KEY_S)) return static_cast<gboolean>(FALSE);
        gtk_dialog_response(GTK_DIALOG(responseDialog), GTK_RESPONSE_ACCEPT);
        return static_cast<gboolean>(TRUE);
    }), dialog);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* responseDialog, gint response, gpointer userData) {
        auto* editorState = static_cast<EditorState*>(userData);
        if (response != GTK_RESPONSE_ACCEPT) { gtk_widget_destroy(GTK_WIDGET(responseDialog)); return; }
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editorState->editor));
        GtkTextIter start;
        GtkTextIter end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar* value = gtk_text_buffer_get_text(buffer, &start, &end, false);
        if (!rc_upload_file(editorState->browser->connection, editorState->path.c_str(), value, static_cast<int>(strlen(value)))) editorState->browser->appendLog(rc_last_error(editorState->browser->connection));
        g_free(value);
    }), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer userData) { delete static_cast<EditorState*>(userData); }), state);
    gtk_widget_show_all(dialog);
}

void TFileBrowserTree::refresh() {
    if (connection != nullptr && !rc_filebrowser_start(connection)) appendLog(rc_last_error(connection));
}

void TFileBrowserTree::refreshFolders() {
    RCFileBrowserFolder* entries = nullptr;
    const int count = rc_copy_filebrowser_folders(connection, &entries);
    gtk_tree_store_clear(folders);
    for (int index = 0; index < count; ++index) addFolder(entries[index].pattern == nullptr ? "" : entries[index].pattern, entries[index].rights == nullptr ? "" : entries[index].rights);
    rc_free_filebrowser_folders(entries, count);
}

void TFileBrowserTree::refreshFiles(const char* folder, int count) {
    RCFileBrowserEntry* entries = nullptr;
    const int entryCount = count > 0 ? rc_copy_filebrowser_files(connection, &entries) : 0;
    gtk_list_store_clear(files);
    currentFolder = folder == nullptr ? "" : folder;
    gtk_label_set_text(GTK_LABEL(folderPath), (std::string("Current Folder: ") + (folder == nullptr ? "" : folder)).c_str());
    for (int index = 0; index < entryCount; ++index) {
        GtkTreeIter row;
        gtk_list_store_append(files, &row);
        const std::string modified = formatModified(entries[index].modified);
        const std::string size = entries[index].size == 0 ? "" : std::to_string(entries[index].size);
        gtk_list_store_set(files, &row, FileIconColumn, fileIcon(entries[index], textFileIcon, nwFileIcon, graalFileIcon, gmapFileIcon), FilePathColumn, entries[index].path == nullptr ? "" : entries[index].path, FileRightsColumn, entries[index].rights == nullptr ? "" : entries[index].rights, FileSizeColumn, size.c_str(), FileModifiedColumn, modified.c_str(), FileSizeSortColumn, entries[index].size, FileModifiedSortColumn, entries[index].modified, -1);
    }
    rc_free_filebrowser_files(entries, entryCount);
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
            gtk_tree_store_set(folders, &row, FolderIconColumn, index + 1 == parts.size() ? closedFolderIcon : openFolderIcon, FolderPathColumn, fullPath.c_str(), FolderRightsColumn, index + 1 == parts.size() && rights != nullptr ? rights : "", FolderDisplayColumn, label.c_str(), -1);
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
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(log), &end, 0.0, false, 0.0, 1.0);
}
