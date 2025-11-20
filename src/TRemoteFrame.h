#pragma once

#include <gtk/gtk.h>
#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

namespace RC3 { struct RCOptions; }

class TPlayerList;
class TFileBrowserTree;
class TScriptList;
class TServerTextEditor;
class TToallsWindow;
class TAccountsWindow;
class TOptionsWindow;
class TNPCList;

class TRemoteFrame {
public:
    TRemoteFrame(const RC3::RCOptions& options, const std::filesystem::path& applicationDirectory, std::function<void()> onClose);
    ~TRemoteFrame();

    void open(void* connection, const std::string& serverName);
    void show();
    bool openLatestPrivateMessage();

private:
    static void onSend(GtkButton*, gpointer data);
    static void onPlayerList(GtkMenuItem*, gpointer data);
    static void onToalls(GtkMenuItem*, gpointer data);
    static void onAccounts(GtkMenuItem*, gpointer data);
    static void onRCOptions(GtkMenuItem*, gpointer data);
    static void onAccountList(const char* accounts, void* data);
    static void onPlayerText(const char* type, const char* account, const char* content, void* data);
    static void onPlayerRights(const char* account, int rights, const char* ipRange, const char* folderAccess, void* data);
    static void onPlayerAttributes(const char* account, const char* properties, const char* editorText, void* data);
    static void onBanData(const char* account, const char* computerId, const char* details, void* data);
    static void onBanListData(const char* type, const char* account, const char* content, void* data);
    static void onFileBrowser(GtkMenuItem*, gpointer data);
    static void onClasses(GtkMenuItem*, gpointer data);
    static void onWeapons(GtkMenuItem*, gpointer data);
    static void onNPCs(GtkMenuItem*, gpointer data);
    static void onLocalNPCDump(GtkMenuItem*, gpointer data);
    static void onServerOptions(GtkMenuItem*, gpointer data);
    static void onServerFlags(GtkMenuItem*, gpointer data);
    static void onFolderConfig(GtkMenuItem*, gpointer data);
    static gboolean onGraphicalButton(GtkWidget*, GdkEventButton*, gpointer data);
    static void onLocalNPCSubmit(GtkDialog*, gint response, gpointer data);
    static void onLocalNPCData(const char* level, const char* content, void* data);
    static gboolean onEditKey(GtkWidget*, GdkEventKey*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    static void onGraphicalAllocate(GtkWidget*, GdkRectangle*, gpointer data);
    static gboolean processEvents(gpointer data);
    static void onConnected(void* data);
    static void onDisconnected(const char* reason, void* data);
    static void onMessage(const char* message, void* data);
    static void onIrcMessage(const char* channel, const char* line, void* data);
    static void onPrivateMessage(int playerId, const char* account, const char* nick, const char* message, const char* type, void* data);
    static void onServerData(const char* type, const char* content, void* data);
    static gboolean scrollChatToBottom(gpointer data);

    void appendChat(const std::string& message);
    void appendChannelMessage(const std::string& channel, const std::string& message);
    void configureChatField(GtkWidget* field);
    void applyEmotes(GtkTextBuffer* buffer, gint startOffset, const std::string& message);
    bool applyAlertTag(std::string& message);
    void send();
    void addMenuItem(GtkWidget* menu, const char* label, GCallback callback = nullptr);
    void graphicalAction(int index);

    std::function<void()> onCloseCallback;
    GtkWidget* window = nullptr;
    GtkWidget* chatField = nullptr;
    GtkWidget* chatScrolled = nullptr;
    GtkWidget* notebook = nullptr;
    GtkWidget* graphicalFixed = nullptr;
    GtkWidget* backgroundImage = nullptr;
    GtkWidget* editField = nullptr;
    GtkWidget* serverLabel = nullptr;
    GtkWidget* playersLabel = nullptr;
    std::array<GtkWidget*, 12> graphicalButtons{};
    GdkPixbuf* kappaEmote = nullptr;
    GdkPixbuf* pmNormalEmote = nullptr;
    GdkPixbuf* pacmanEmote = nullptr;
    GdkPixbuf* backgroundPixbuf = nullptr;
    void* connection = nullptr;
    guint eventSource = 0;
    gint64 nextNcConnectAttempt = 0;
    bool ncConnectionAttempted = false;
    TPlayerList* playerList = nullptr;
    TFileBrowserTree* fileBrowser = nullptr;
    TScriptList* classList = nullptr;
    TScriptList* weaponList = nullptr;
    TServerTextEditor* serverOptionsEditor = nullptr;
    TServerTextEditor* serverFlagsEditor = nullptr;
    TServerTextEditor* folderConfigEditor = nullptr;
    TToallsWindow* toallsWindow = nullptr;
    TAccountsWindow* accountsWindow = nullptr;
    TOptionsWindow* optionsWindow = nullptr;
    TNPCList* npcList = nullptr;
    const RC3::RCOptions& options;
    std::filesystem::path applicationDirectory;
    std::unordered_map<std::string, GtkWidget*> channelFields;
};
