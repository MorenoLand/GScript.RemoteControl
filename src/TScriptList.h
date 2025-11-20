#pragma once

#include <gtk/gtk.h>
#include <string>

class TScriptList {
public:
    explicit TScriptList(std::string type);
    ~TScriptList();
    void open(void* connection);
private:
    static void onEdit(GtkButton*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static void onTreeActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data);
    static void onScript(const char* type, const char* name, int id, const char* script, void* data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void refresh();
    void edit();
    void showEditor(const char* name, const char* script);
    std::string type;
    GtkWidget* window = nullptr;
    GtkListStore* store = nullptr;
    GtkWidget* tree = nullptr;
    void* connection = nullptr;
};
