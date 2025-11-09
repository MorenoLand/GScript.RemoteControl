#pragma once

#include <gtk/gtk.h>

class TFileBrowserTree {
public:
    TFileBrowserTree();
    ~TFileBrowserTree();
    void open(void* connection);
private:
    static void onRefresh(GtkButton*, gpointer data);
    static void onFolderSelected(GtkTreeSelection*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    static void onFolders(int count, void* data);
    static void onFiles(const char* folder, int count, void* data);
    static void onMessage(const char* message, void* data);
    void refresh();
    void refreshFolders();
    void refreshFiles(const char* folder);
    void addFolder(const char* pattern, const char* rights);
    void appendLog(const char* message);
    GtkWidget* window = nullptr;
    GtkWidget* folderPath = nullptr;
    GtkTreeStore* folders = nullptr;
    GtkListStore* files = nullptr;
    GtkWidget* log = nullptr;
    GdkPixbuf* closedFolderIcon = nullptr;
    GdkPixbuf* openFolderIcon = nullptr;
    GdkPixbuf* textFileIcon = nullptr;
    GdkPixbuf* nwFileIcon = nullptr;
    GdkPixbuf* graalFileIcon = nullptr;
    GdkPixbuf* gmapFileIcon = nullptr;
    void* connection = nullptr;
};
