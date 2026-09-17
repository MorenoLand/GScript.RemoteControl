#pragma once

#include <filesystem>
#include <functional>
#include <gtk/gtk.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "TRCAccounts.h"

struct RCConnection;

struct SavedListServer {
    std::string name;
    std::string host;
    int port = 14922;
};

namespace RC {
std::vector<SavedListServer> loadListServerProfiles(const std::filesystem::path& path, const std::string& defaultHost, int defaultPort);
bool saveListServerProfiles(const std::filesystem::path& path, const std::vector<SavedListServer>& endpoints);
}

class TServerList {
public:
    TServerList(std::function<void()> onClose, std::function<void(TServerList*, void*, int, const std::string&, const std::string&, const std::string&, bool)> onConnected, std::function<void()> onServerSelected, bool darkMode, const std::string& theme, std::function<void(bool, const std::string&)> onThemeChanged, std::function<std::vector<RC::RCAccount>()> accountChoices = {}, std::function<std::vector<RC::RCAccount>(const std::string&)> accountChoicesForListServer = {}, std::function<void()> onOpenAnother = {});
    TServerList(const std::filesystem::path& applicationDirectory, std::function<void()> onClose, std::function<void(TServerList*, void*, int, const std::string&, const std::string&, const std::string&, bool)> onConnected, std::function<void()> onServerSelected, bool darkMode, const std::string& theme, std::function<void(bool, const std::string&)> onThemeChanged, std::function<std::vector<RC::RCAccount>()> accountChoices = {}, std::function<std::vector<RC::RCAccount>(const std::string&)> accountChoicesForListServer = {}, std::function<void()> onOpenAnother = {});
    ~TServerList();

    void open(std::uint64_t accountId, const std::string& account, const std::string& password, const std::string& nickname, const std::string& listServer);
    void openDirect(std::uint64_t accountId, const std::string& account, const std::string& password, const std::string& nickname, const std::string& host, int port);
    void setLoginParent(GtkWindow* parent) { loginParent = parent; }
    void reopen();
    void show();
    void openListServerSettings();
    void openAnotherListServer(const RC::RCAccount* selectedAccount = nullptr, const std::string& selectedListServer = {});
    void setListServer(const std::string& name, const std::string& host, int port);
    std::string currentListServer() const;
    std::string currentListServerName() const { return listserverName; }
    std::vector<std::string> mcpServerNames() const;
    bool mcpConnect(const std::string& name, std::string& error);

private:
    struct ServerEntry {
        std::string name;
        std::string language;
        std::string description;
        std::string version;
        std::string homepage;
        int players = 0;
        int icon = -1;
    };

    struct LoadResult {
        std::shared_ptr<std::atomic<bool>> alive;
        TServerList* serverList;
        std::vector<ServerEntry> entries;
        std::string error;
    };

    struct ConnectResult {
        std::shared_ptr<std::atomic<bool>> alive;
        TServerList* serverList;
        void* connection;
        int serverIndex;
        bool additional;
        std::string serverName;
        std::string nickname;
        std::string account;
        std::string error;
    };

    static void onRefresh(GtkButton*, gpointer data);
    static void onConnect(GtkButton*, gpointer data);
    static void onSelectionChanged(GtkTreeSelection*, gpointer data);
    static void onRowActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data);
    static gboolean onTreeButtonPress(GtkWidget*, GdkEventButton*, gpointer data);
    static void onHomepage(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    static gboolean finishLoad(gpointer data);
    static gboolean finishConnect(gpointer data);

    void refresh();
    void connect(bool additional = false);
    void connectWithAccount(const RC::RCAccount& account);
    void disconnectCurrentConnection();
    void showEntry(int index);
    std::function<void()> onCloseCallback;
    std::function<void(TServerList*, void*, int, const std::string&, const std::string&, const std::string&, bool)> onConnectedCallback;
    std::function<void()> onServerSelectedCallback;
    std::function<void(bool, const std::string&)> onThemeChangedCallback;
    std::function<std::vector<RC::RCAccount>()> accountChoicesCallback;
    std::function<std::vector<RC::RCAccount>(const std::string&)> accountChoicesForListServerCallback;
    std::function<void()> onOpenAnotherCallback;
    std::vector<std::unique_ptr<TServerList>> additionalLists;
    GtkWidget* window = nullptr;
    std::filesystem::path applicationDirectory;
    GtkWindow* loginParent = nullptr;
    GtkListStore* store = nullptr;
    GtkWidget* tree = nullptr;
    GtkWidget* languageField = nullptr;
    GtkWidget* versionField = nullptr;
    GtkWidget* homepageField = nullptr;
    GtkWidget* descriptionField = nullptr;
    GtkWidget* statusField = nullptr;
    GtkWidget* refreshButton = nullptr;
    GtkWidget* connectButton = nullptr;
    GdkPixbuf* serverIcons[4] = {nullptr, nullptr, nullptr, nullptr};
    std::jthread worker;
    std::jthread connectWorker;
    std::mutex connectionMutex;
    void* connection = nullptr;
    std::shared_ptr<std::atomic<bool>> callbackAlive = std::make_shared<std::atomic<bool>>(true);
    bool connecting = false;
    std::uint64_t accountId = 0;
    std::string account;
    std::string password;
    std::string nickname;
    std::string listserverHost;
    int listserverPort = 14922;
    std::string listserverName;
    std::vector<SavedListServer> listserverEndpoints;
    bool darkMode = true;
    std::string theme = "dark";
    std::vector<ServerEntry> entries;
    bool additionalConnect = false;
    bool defaultAdditionalConnection = false;
};
