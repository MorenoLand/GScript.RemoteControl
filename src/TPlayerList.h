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
    void notePrivateMessage(int playerId, const char* type);
private:
    static void onRefresh(GtkButton*, gpointer data);
    static void onMassPM(GtkButton*, gpointer data);
    static void onAdminMessage(GtkButton*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static gboolean onButtonPress(GtkWidget*, GdkEventButton*, gpointer data);
    static void onEditAccess(GtkMenuItem*, gpointer data);
    static void onBanData(const char* account, const char* computerId, const char* details, void* data);
    static gboolean onPMBlink(gpointer data);
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
    void editAccess();
    void updatePMIcons();
    GdkPixbuf* pmIconFor(const std::string& type) const;
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
    GdkPixbuf* pmNormalIcon = nullptr;
    GdkPixbuf* pmGuildIcon = nullptr;
    GdkPixbuf* pmAdminIcon = nullptr;
    GdkPixbuf* pmMassIcon = nullptr;
    class TLocalBanWindow* localBanWindow = nullptr;
    std::map<std::string, std::vector<std::string>> serverPlayers;
    std::map<int, std::string> pmTypes;
    guint pmBlinkSource = 0;
    bool pmIconsVisible = true;
    void* connection = nullptr;
};
