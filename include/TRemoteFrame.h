#pragma once

#include <gtk/gtk.h>
#include "TRCOptions.h"
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class TPlayerList;
class TFileBrowserTree;
class TScriptList;
class TServerTextEditor;
class TToallsWindow;
class TAccountsWindow;
class TOptionsWindow;
class TNPCList;
class TLevelList;
class TSyncManager;
class TExtensionsManager;
struct WebPAnimation;

class TRemoteFrame {
public:
    TRemoteFrame(const RC::RCOptions& options, const std::filesystem::path& applicationDirectory, std::function<void()> onClose, std::function<void()> onListServer, std::function<void()> onListServerSettings);
    ~TRemoteFrame();

    void open(void* connection, int serverIndex, const std::string& serverName, const std::string& nickname, const std::string& accountName);
    void disconnect();
    void signOut();
    void show();
    void showServerList() { if (onListServerCallback) onListServerCallback(); }
    GdkWindow* nativeWindow() const { return window == nullptr ? nullptr : gtk_widget_get_window(window); }
    void toggleVisibility();
    bool isVisible() const;
    void hideFromTray();
    void showFromTray();
    bool openLatestPrivateMessage();
    bool isNCAuthenticated() const;
    const std::string& currentServerName() const;
    bool isConnected() const;
    void refreshTheme();
    void reloadBackground();
    void updateThemeOptions(const RC::RCOptions& nextOptions);
    void setDownloadServer(const std::string& server);
    bool mcpOpenView(const std::string& view, std::string& error);
    bool mcpSendChat(const std::string& text, std::string& error);

private:
    static void onSend(GtkButton*, gpointer data);
    static void onPlayerList(GtkMenuItem*, gpointer data);
    static void onToalls(GtkMenuItem*, gpointer data);
    static void onAccounts(GtkMenuItem*, gpointer data);
    static void onRCOptions(GtkMenuItem*, gpointer data);
    static void onListServerSettings(GtkButton*, gpointer data);
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
    static void onLevels(GtkMenuItem*, gpointer data);
    static void onLocalNPCDump(GtkMenuItem*, gpointer data);
    static void onServerOptions(GtkMenuItem*, gpointer data);
    static void onServerFlags(GtkMenuItem*, gpointer data);
    static void onFolderConfig(GtkMenuItem*, gpointer data);
    static void onExtensions(GtkMenuItem*, gpointer data);
    static gboolean onGraphicalButton(GtkWidget*, GdkEventButton*, gpointer data);
    static void onLocalNPCSubmit(GtkDialog*, gint response, gpointer data);
    static void onLocalNPCData(const char* level, const char* content, void* data);
    static gboolean onEditKey(GtkWidget*, GdkEventKey*, gpointer data);
    static void onEditPopup(GtkEntry*, GtkMenu*, gpointer data);
    static void onEmojiMenuActivate(GtkMenuItem*, gpointer data);
    static gboolean adjustEmojiPopoverLater(gpointer data);
    static void onMentionChanged(GtkEditable*, gpointer data);
    static gboolean constrainMentionPopup(gpointer data);
    static gboolean onMentionMatch(GtkEntryCompletion*, const gchar*, GtkTreeIter*, gpointer data);
    static gboolean onMentionSelected(GtkEntryCompletion*, GtkTreeModel*, GtkTreeIter*, gpointer data);
    static gboolean onActivityEvent(GtkWidget*, GdkEvent*, gpointer data);
    static gboolean onWindowKey(GtkWidget*, GdkEventKey*, gpointer data);
    static gboolean onFindResultClick(GtkWidget*, GdkEventButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    static gboolean onConfigure(GtkWidget*, GdkEventConfigure*, gpointer data);
    static gboolean onWindowState(GtkWidget*, GdkEventWindowState*, gpointer data);
      static gboolean onGraphicalDraw(GtkWidget*, cairo_t*, gpointer data);
      static void onGraphicalAllocate(GtkWidget*, GdkRectangle*, gpointer data);
      void repositionGraphicalButtons(int requestedWidth = 0);
    static gboolean processEvents(gpointer data);
    static void onConnected(void* data);
    static void onDisconnected(const char* reason, void* data);
    static void onDisconnectedEx(void* handle, const char* reason, void* data);
    static void onMessage(const char* message, void* data);
    static void onIrcMessage(const char* channel, const char* line, void* data);
    static void onPrivateMessage(int playerId, const char* account, const char* nick, const char* message, const char* type, void* data);
    static void onPlayerPropChanged(int playerId, const char* property, const char* value, void* data);
    static void onPlayerPropertiesChanged(int playerId, const char* properties, void* data);
    static void onRawPacket(int packetId, const char* data, int length, void* dataPtr);
    static void onServerData(const char* type, const char* content, void* data);
    static gboolean scrollChatToBottom(gpointer data);
    static gboolean scrollChannelToBottom(gpointer data);

    void appendChat(const std::string& message, bool suppressUrgency = false, bool suppressEmotes = false);
    void appendChatLog(const std::string& message) const;
    void applyOptions(const RC::RCOptions& previous);
    void applyOptionalTools();
    void createGraphicalButton(int index);
    void setOptionalButton(int index, bool enabled);
    void refreshNotebookTheme();
    void sendServerListOptions();
    void updateMassPMAcceptance();
    void updateNCUi(bool connected);
    void setNCChannelVisible(bool visible);
    void trackChannelField(const std::string& channel, GtkWidget* field);
    void appendChannelMessage(const std::string& channel, const std::string& message);
    void beginFindResults(const std::string& base);
    bool appendFindResult(const std::string& message);
    void removeChannel(const std::string& channel);
    void configureChatField(GtkWidget* field);
    void adjustEmojiPopover();
    void refreshMentionCompletion();
    struct ChatTags { GtkTextTag* alert = nullptr; GtkTextTag* bold = nullptr; GtkTextTag* timestamp = nullptr; GtkTextTag* invisible = nullptr; };
    ChatTags& chatTagsFor(GtkTextBuffer* buffer);
    void applyChatUrls(GtkTextBuffer* buffer, gint startOffset, gint endOffset);
    static gboolean onChatLinkClick(GtkWidget* widget, GdkEventButton* event, gpointer data);
    static gboolean onChatMotion(GtkWidget* widget, GdkEventMotion* event, gpointer data);
    void applyEmotes(GtkTextBuffer* buffer, gint startOffset, const std::string& message);
    bool applyAlertTag(std::string& message, bool allowUrgency);
    void send();
    void reconnectNPCServer();
    void disconnectNPCServer();
    void reconnectServer();
    void handleDisconnected(void* disconnectedConnection, std::uint64_t generation, const char* reason);
    void addMenuItem(GtkWidget* menu, const char* label, GCallback callback = nullptr);
    void graphicalAction(int index);

    std::function<void()> onCloseCallback;
    std::function<void()> onListServerCallback;
    std::function<void()> onListServerSettingsCallback;
    std::shared_ptr<bool> callbackAlive = std::make_shared<bool>(true);
    GtkWidget* window = nullptr;
    GtkWidget* chatField = nullptr;
    GtkWidget* chatScrolled = nullptr;
    GtkWidget* notebook = nullptr;
    GtkCssProvider* notebookTabProvider = nullptr;
    GtkWidget* graphicalFixed = nullptr;
    GtkWidget* graphicalContainer = nullptr;
    GtkWidget* backgroundImage = nullptr;
    GtkWidget* editField = nullptr;
    GtkListStore* mentionStore = nullptr;
    GtkEntryCompletion* mentionCompletion = nullptr;
    GtkWidget* serverLabel = nullptr;
    GtkWidget* playersLabel = nullptr;
    GtkWidget* npcServerLabel = nullptr;
    std::array<GtkWidget*, 8> serverLabelShadows{};
    std::array<GtkWidget*, 8> playersLabelShadows{};
    std::array<GtkWidget*, 8> npcServerLabelShadows{};
    std::array<GtkWidget*, 15> graphicalButtons{};
    GdkPixbuf* kappaEmote = nullptr;
    GdkPixbuf* pmNormalEmote = nullptr;
    GdkPixbuf* pacmanEmote = nullptr;
    GdkPixbuf* backgroundPixbuf = nullptr;
    GdkPixbufAnimation* backgroundAnimation = nullptr;
    GdkPixbufAnimationIter* backgroundAnimationIter = nullptr;
    std::unique_ptr<WebPAnimation> backgroundWebPAnimation;
    unsigned int backgroundAnimationSource = 0;
    int graphicalBackgroundWidth = 500;
    void* connection = nullptr;
    void* detachedConnection = nullptr;
    std::uint64_t connectionGeneration = 0;
    int currentServerIndex = -1;
    std::string serverName;
    std::string nickname;
    std::string baseNickname;
    std::string accountName;
    int trayPlayerCount = -1;
    int normalWindowWidth = 500;
    int normalWindowHeight = 350;
    int normalWindowX = 0;
    int normalWindowY = 0;
    bool hasNativeNormalWindowGeometry = false;
    bool windowMaximized = false;
    int trayWindowX = 0;
    int trayWindowY = 0;
    bool trayWindowPositionValid = false;
    bool trayWindowMaximized = false;
    std::vector<std::string> chatHistory;
    int chatHistoryIndex = -1;
    guint eventSource = 0;
    guint emojiPopoverResizeSource = 0;
    gint64 nextNcConnectAttempt = 0;
    gint64 nextNcKeepalive = 0;
    bool ncConnectionAttempted = false;
    bool ncManuallyDisconnected = false;
    bool ncWasAuthenticated = false;
    bool ncReconnectScheduledAutomatically = false;
    bool disconnectHandled = false;
    gint64 lastActivity = 0;
    bool awayNicknameApplied = false;
    bool awayStatusApplied = false;
    bool suppressReconnectDisconnect = false;
    TPlayerList* playerList = nullptr;
    TFileBrowserTree* fileBrowser = nullptr;
    TScriptList* classList = nullptr;
    TScriptList* weaponList = nullptr;
    std::string scriptEditorsRestoredSession;
    bool scriptEditorsRestorePending = false;
    TServerTextEditor* serverOptionsEditor = nullptr;
    TServerTextEditor* serverFlagsEditor = nullptr;
    TServerTextEditor* folderConfigEditor = nullptr;
    TToallsWindow* toallsWindow = nullptr;
    TAccountsWindow* accountsWindow = nullptr;
    TOptionsWindow* optionsWindow = nullptr;
    TNPCList* npcList = nullptr;
    TLevelList* levelList = nullptr;
    std::unique_ptr<TSyncManager> syncManager;
    std::unique_ptr<TExtensionsManager> extensionsManager;
    int syncProgress = 0;
    bool syncInProgress = false;
    RC::RCOptions options;
    std::filesystem::path applicationDirectory;
    std::unordered_map<std::string, GtkWidget*> channelFields;
    std::unordered_set<std::string> ircChannels;
    std::unordered_set<std::string> joinedIrcChannels;
    std::unordered_map<int, std::string> playerCommunityNames;
    std::string findResultBase;
    GtkWidget* findResultsField = nullptr;
    std::unordered_map<GtkTextBuffer*, ChatTags> chatTags;
};
