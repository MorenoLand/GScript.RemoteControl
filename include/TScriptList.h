#pragma once

#include <gtk/gtk.h>
#include "TRCOptions.h"
#include "TExternalEditor.h"
#include <memory>
#include <string>
#include <chrono>
#include <filesystem>
#include <vector>

class TExtensionsManager;

class TScriptList {
public:
    TScriptList(std::string type, RC::RCOptions* options, TExtensionsManager* extensions, const std::filesystem::path& applicationDirectory);
    ~TScriptList();
    void open(void* connection);
    void setConnection(void* connection);
    void hide();
    void setServerName(const std::string& server);
    void setSession(const std::string& server, const std::string& account);
    void restoreOpenEditors();
    static bool hasSavedEditors(const std::filesystem::path& applicationDirectory, const std::string& server, const std::string& account);
    static void restoreScriptReceiver(void* connection);
    static void registerNPCScriptReceiver(void* connection, void (*callback)(const char*, const char*, int, const char*, void*), void* data);
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
    static gboolean onRestoreTimeout(gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void registerScriptReceiver();
    void unregisterScriptReceiver();
    void requestNextRestoredEditor();
    void cancelRestoredEditors();
    void restoreEditorWindowState(const std::string& name, GtkWidget* dialog) const;
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
    std::filesystem::path scriptWindowStatePath;
    std::string sessionKey;
    std::string pendingScriptName;
    std::chrono::steady_clock::time_point pendingScriptRequestAt{};
    std::vector<std::string> pendingRestoreNames;
    std::size_t pendingRestoreIndex = 0;
    std::string pendingCreateName;
    std::string pendingDeleteName;
    guint pendingCreateTimer = 0;
    guint pendingRestoreTimer = 0;
    int pendingCreateAttempts = 0;
    bool opened = false;
    bool scriptReceiverRegistered = false;
    bool restoringEditors = false;
};
