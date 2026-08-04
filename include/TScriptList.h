#pragma once

#include <gtk/gtk.h>
#include "TRCOptions.h"
#include "TExternalEditor.h"
#include <memory>
#include <string>
#include <chrono>

class TExtensionsManager;

class TScriptList {
public:
    TScriptList(std::string type, RC::RCOptions* options, TExtensionsManager* extensions = nullptr);
    ~TScriptList();
    void open(void* connection);
    void setConnection(void* connection);
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
    static void onWeaponAdded(const char* name, void* data);
    static void onWeaponDeleted(const char* name, void* data);
    static void onClassAdded(const char* name, void* data);
    static void onClassDeleted(const char* name, void* data);
    static void onWeaponListReceived(int count, void* data);
    static gboolean onWeaponMutationPoll(gpointer data);
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
    RC::RCOptions* options = nullptr;
    TExtensionsManager* extensionsManager = nullptr;
    std::unique_ptr<TExternalEditor> externalEditor;
    std::string externalWorkspace;
    std::string externalCommand;
    std::string pendingScriptName;
    std::chrono::steady_clock::time_point pendingScriptRequestAt{};
    std::string pendingCreateName;
    std::string pendingDeleteName;
    guint pendingCreateTimer = 0;
    int pendingCreateAttempts = 0;
};
