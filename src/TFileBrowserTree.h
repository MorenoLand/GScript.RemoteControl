#pragma once

#include <gtk/gtk.h>

#include <string>

class TFileBrowserTree {
public:
    TFileBrowserTree();
    ~TFileBrowserTree();
    void open(void* connection);
    void setDownloadFolder(const std::string& folder);
private:
    static void onRefresh(GtkButton*, gpointer data);
    static void onFolderSelected(GtkTreeSelection*, gpointer data);
    static gboolean onFolderButtonPress(GtkWidget*, GdkEventButton*, gpointer data);
    static gboolean onFileButtonPress(GtkWidget*, GdkEventButton*, gpointer data);
    static void onFileDragDataGet(GtkWidget*, GdkDragContext*, GtkSelectionData*, guint, guint, gpointer data);
    static void onDropDataReceived(GtkWidget*, GdkDragContext*, gint, gint, GtkSelectionData*, guint, guint, gpointer data);
    static void onDownload(GtkMenuItem*, gpointer data);
    static void onEditAsText(GtkMenuItem*, gpointer data);
    static void onDeleteItem(GtkMenuItem*, gpointer data);
    static void onRename(GtkMenuItem*, gpointer data);
    static void onMove(GtkMenuItem*, gpointer data);
    static void onUpload(GtkMenuItem*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    static void onFolders(int count, void* data);
    static void onFiles(const char* folder, int count, void* data);
    static void onMessage(const char* message, void* data);
    static void onFileReceived(const char* path, const void* content, int length, void* data);
    void refresh();
    void refreshFolders();
    void refreshFiles(const char* folder, int count);
    void addFolder(const char* pattern, const char* rights);
    void appendLog(const char* message);
    void showTextEditor(const char* path, const void* content, int length);
    void showItemMenu(GtkWidget* view, GdkEventButton* event, bool folder);
    GtkWidget* window = nullptr;
    GtkWidget* folderPath = nullptr;
    GtkTreeStore* folders = nullptr;
    GtkListStore* files = nullptr;
    GtkWidget* folderView = nullptr;
    GtkWidget* fileView = nullptr;
    GtkWidget* log = nullptr;
    GdkPixbuf* closedFolderIcon = nullptr;
    GdkPixbuf* openFolderIcon = nullptr;
    GdkPixbuf* textFileIcon = nullptr;
    GdkPixbuf* nwFileIcon = nullptr;
    GdkPixbuf* graalFileIcon = nullptr;
    GdkPixbuf* gmapFileIcon = nullptr;
    void* connection = nullptr;
    std::string currentFolder;
    std::string pendingEditPath;
    std::string downloadFolder;
};
