#pragma once

#include <gtk/gtk.h>
#include <map>
#include <string>
#include <vector>

class TPlayerList {
public:
    TPlayerList();
    ~TPlayerList();
    void open(void* connection);
private:
    static void onRefresh(GtkButton*, gpointer data);
    static void onMassPM(GtkButton*, gpointer data);
    static void onAdminMessage(GtkButton*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static void onPMServers(int count, void* data);
    static void onPMGuilds(int count, void* data);
    static void onPMServerPlayers(const char* serverName, const char* playerData, void* data);
    static gboolean onServerExpand(GtkTreeView*, GtkTreeIter*, GtkTreePath*, gpointer data);
    static void onGroupExpanded(GtkTreeView*, GtkTreeIter*, GtkTreePath*, gpointer data);
    static void onGroupCollapsed(GtkTreeView*, GtkTreeIter*, GtkTreePath*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void refresh();
    void refreshRemoteLists();
    void sendMassPM();
    void sendAdminMessage();
    std::vector<int> playerIds() const;
    GtkWidget* window = nullptr;
    GtkWidget* tree = nullptr;
    GtkTreeStore* store = nullptr;
    GtkListStore* guildStore = nullptr;
    GtkTreeStore* serverStore = nullptr;
    GtkListStore* channelStore = nullptr;
    GdkPixbuf* onlineIcon = nullptr;
    GdkPixbuf* channelIcon = nullptr;
    GdkPixbuf* channelClosedIcon = nullptr;
    std::map<std::string, std::vector<std::string>> serverPlayers;
    void* connection = nullptr;
};
