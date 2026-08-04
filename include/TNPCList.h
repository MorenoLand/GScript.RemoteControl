#pragma once

#include <gtk/gtk.h>
#include "TRCOptions.h"
#include "TExternalEditor.h"
#include <memory>
#include <string>

class TNPCList {
public:
    explicit TNPCList(std::string accountName, RC::RCOptions* options = nullptr);
    ~TNPCList();
    void open(void* connection);
    void setConnection(void* connection);
    void hide();
    void setServerName(const std::string& server);
private:
    static void onRefresh(GtkButton*, gpointer data);
    static void onAdd(GtkButton*, gpointer data);
    static void onAddResponse(GtkDialog*, gint response, gpointer data);
    static gboolean onTreeButton(GtkWidget*, GdkEventButton*, gpointer data);
    static void onEditScript(GtkMenuItem*, gpointer data);
    static void onEditFlags(GtkMenuItem*, gpointer data);
    static void onViewAttributes(GtkMenuItem*, gpointer data);
    static void onWarp(GtkMenuItem*, gpointer data);
    static void onWarpResponse(GtkDialog*, gint response, gpointer data);
    static void onNPCScript(const char* scriptType, const char* name, int id, const char* script, void* data);
    static void onNPCFlags(int id, const char* flags, void* data);
    static void onNPCAttributes(int id, const char* attributes, void* data);
    static void onReset(GtkMenuItem*, gpointer data);
    static void onDeleteNPC(GtkMenuItem*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static void onNPCChanged(int id, const char* name, void* data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void refresh();
    void showScriptEditor(const char* name, int id, const char* script);
    void showFlagsEditor(int id, const std::string& npcName, const char* flags);
    void showAttributes(int id, const char* attributes);
    std::string npcNameForId(int id) const;
    int firstFreeNPCId() const;
    GtkWidget* window = nullptr;
    GtkWidget* tree = nullptr;
    GtkListStore* store = nullptr;
    void* connection = nullptr;
    std::string accountName;
    std::string serverName;
    RC::RCOptions* options = nullptr;
    std::unique_ptr<TExternalEditor> externalEditor;
    std::string externalWorkspace;
    std::string externalCommand;
    std::string addNPCType = "OBJECT";
    std::string addNPCLevel = "onlinestartlocal.nw";
    std::string addNPCX = "30.5";
    std::string addNPCY = "30";
    int selectedNPCId = -1;
    int pendingCreateId = -1;
    std::string pendingCreateName;
};
