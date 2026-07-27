#pragma once

#include <gtk/gtk.h>
#include <string>

class TScriptList {
public:
    explicit TScriptList(std::string type);
    ~TScriptList();
    void open(void* connection);
    void hide();
    void setServerName(const std::string& server);
    static void restoreScriptReceiver(void* connection);
private:
    static void onEdit(GtkButton*, gpointer data);
    static void onAdd(GtkButton*, gpointer data);
    static void onDeleteScript(GtkButton*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static void onTreeActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data);
    static void onScript(const char* type, const char* name, int id, const char* script, void* data);
    static void onWeaponListReceived(int count, void* data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void refresh();
    void edit();
    void deleteSelected();
    void showEditor(const char* name, const char* script);
    std::string type;
    GtkWidget* window = nullptr;
    GtkListStore* store = nullptr;
    GtkWidget* tree = nullptr;
    void* connection = nullptr;
    std::string serverName;
};
