#pragma once

#include <gtk/gtk.h>

class TFileBrowser {
public:
    TFileBrowser();
    ~TFileBrowser();

    void open(void* connection);

private:
    static void onRefresh(GtkButton*, gpointer data);
    static void onFolderActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    static void onFolders(int count, void* data);
    static void onFiles(const char* folder, int count, void* data);
    static void onMessage(const char* message, void* data);

    void refresh();
    void refreshFolders();
    void refreshFiles(const char* folder);
    void appendLog(const char* message);

    GtkWidget* window = nullptr;
    GtkWidget* folderPath = nullptr;
    GtkListStore* folders = nullptr;
    GtkListStore* files = nullptr;
    GtkWidget* log = nullptr;
    void* connection = nullptr;
};
