#pragma once

#include <gtk/gtk.h>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "TServerPlayer.h"

class TPlayerList {
public:
    TPlayerList(const std::filesystem::path& applicationDirectory, std::string accountName);
    ~TPlayerList();
    void open(void* connection);
    void setConnection(void* connection);
    void rebindConnection(void* connection);
    void setServerName(const std::string& server);
    void setStatusList(const char* statuses);
    void setAttachAway(bool enabled);
    void setAwayStatus(bool away);
    void handleBanData(const char* account, const char* computerId, const char* details);
    void handleBanListData(const char* type, const char* account, const char* content);
    void handlePlayerRights(const char* account, int rights, const char* ipRange, const char* folderAccess);
    void handlePlayerAttributes(const char* account, const char* properties, const char* editorText);
    void handlePlayerText(const char* type, const char* account, const char* content);
    void setPlayerProperties(int playerId, const char* properties);
    std::optional<bool> localAccountConnected() const;
    std::string notePrivateMessage(int playerId, const char* account, const char* nick, const char* message, const char* type);
    bool openLatestPrivateMessage();
    bool hasOpenPrivateMessage(int playerId) const;
    void clearPrivateMessageAlert();
    void pmWindowClosed(int playerId, void* data);
private:
    static void onRefresh(GtkButton*, gpointer data);
    static void onMassPM(GtkButton*, gpointer data);
    static void onAdminMessage(GtkButton*, gpointer data);
    static void onStatusChanged(GtkComboBox*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static gboolean onButtonPress(GtkWidget*, GdkEventButton*, gpointer data);
    static void onEditAccess(GtkMenuItem*, gpointer data);
    static void onPrivateMessageMenu(GtkMenuItem*, gpointer data);
    static void onHistoryMenu(GtkMenuItem*, gpointer data);
    static void onBanHistoryMenu(GtkMenuItem*, gpointer data);
    static void onStaffActivityMenu(GtkMenuItem*, gpointer data);
    static void onDisconnectPlayer(GtkMenuItem*, gpointer data);
    static void onResetPlayer(GtkMenuItem*, gpointer data);
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
    void sendAttachAway();
    void refreshRemoteLists();
    void sendMassPM();
    void sendAdminMessage();
    void editAccess();
    void editRights();
    void editAttributes();
    void editComments();
    void editProfile();
    void editAccount();
    void openSelectedPrivateMessage();
    void openSelectedHistory();
    void requestSelectedBanHistory();
    void requestSelectedStaffActivity();
    void disconnectSelectedPlayer();
    void resetSelectedPlayer();
    void updateSelectedPlayerLevel();
    void warpSelectedPlayer();
    void adminMessageSelectedPlayer();
    void updatePMIcons();
    void openPrivateMessage(int playerId, const char* account, const char* nick);
    void openPrivateMessageHistory(const char* account, const char* nick);
    void markPrivateMessageRead(int playerId);
    void appendHistory(const char* account, const char* sender, const char* message) const;
    GdkPixbuf* pmIconFor(const std::string& type) const;
    GdkPixbuf* statusIconFor(const TServerPlayer& player) const;
    void loadStatusIcons();
    std::vector<int> playerIds() const;
    GtkWidget* window = nullptr;
    std::string serverName;
    GtkWidget* tree = nullptr;
    GtkWidget* statusCombo = nullptr;
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
    std::vector<std::string> statusNames;
    std::vector<GdkPixbuf*> statusIcons;
    class TLocalBanWindow* localBanWindow = nullptr;
    std::map<std::string, std::vector<std::string>> serverPlayers;
    std::map<int, std::string> pmTypes;
    std::map<int, std::pair<std::string, std::string>> pmPlayers;
    std::map<int, std::vector<std::string>> pmMessages;
    std::map<int, void*> pmWindows;
    std::map<int, TServerPlayer> serverPlayersById;
    int latestPMPlayerId = 0;
    guint pmBlinkSource = 0;
    bool pmIconsVisible = true;
    void* connection = nullptr;
    std::filesystem::path applicationDirectory;
    std::string accountName;
};
