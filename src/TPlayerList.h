#pragma once

#include <gtk/gtk.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

class TPlayerList {
public:
    explicit TPlayerList(const std::filesystem::path& applicationDirectory);
    ~TPlayerList();
    void open(void* connection);
    void notePrivateMessage(int playerId, const char* account, const char* nick, const char* message, const char* type);
    bool openLatestPrivateMessage();
private:
    static void onRefresh(GtkButton*, gpointer data);
    static void onMassPM(GtkButton*, gpointer data);
    static void onAdminMessage(GtkButton*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static gboolean onButtonPress(GtkWidget*, GdkEventButton*, gpointer data);
    static void onEditAccess(GtkMenuItem*, gpointer data);
    static void onPrivateMessageMenu(GtkMenuItem*, gpointer data);
    static void onHistoryMenu(GtkMenuItem*, gpointer data);
    static void onDisconnectPlayer(GtkMenuItem*, gpointer data);
    static void onBanData(const char* account, const char* computerId, const char* details, void* data);
    static void onBanListData(const char* type, const char* account, const char* content, void* data);
    static void onPlayerAttributes(const char* account, const char* properties, const char* editorText, void* data);
    static gboolean onPMBlink(gpointer data);
    static void onPMServers(int count, void* data);
    static void onPMGuilds(int count, void* data);
    static void onPMServerPlayers(const char* serverName, const char* playerData, void* data);
    static gboolean onServerButtonPress(GtkWidget*, GdkEventButton*, gpointer data);
    static void onServerActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data);
    static void onGroupExpanded(GtkTreeView*, GtkTreeIter*, GtkTreePath*, gpointer data);
    static void onGroupCollapsed(GtkTreeView*, GtkTreeIter*, GtkTreePath*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void refresh();
    void refreshRemoteLists();
    void sendMassPM();
    void sendAdminMessage();
    void editAccess();
    void editAttributes();
    void openSelectedPrivateMessage();
    void openSelectedHistory();
    void disconnectSelectedPlayer();
    void updatePMIcons();
    void openPrivateMessage(int playerId, const char* account, const char* nick);
    void appendHistory(const char* account, const char* direction, const char* message) const;
    GdkPixbuf* pmIconFor(const std::string& type) const;
    std::vector<int> playerIds() const;
    GtkWidget* window = nullptr;
    GtkWidget* tree = nullptr;
    GtkTreeStore* store = nullptr;
    GtkListStore* guildStore = nullptr;
    GtkTreeStore* serverStore = nullptr;
    GtkWidget* serverTree = nullptr;
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
    std::map<int, std::pair<std::string, std::string>> pmPlayers;
    int latestPMPlayerId = 0;
    guint pmBlinkSource = 0;
    bool pmIconsVisible = true;
    void* connection = nullptr;
    std::filesystem::path applicationDirectory;
};
