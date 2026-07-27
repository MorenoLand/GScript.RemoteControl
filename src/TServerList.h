#pragma once

#include <filesystem>
#include <functional>
#include <gtk/gtk.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
    TServerList(std::function<void()> onClose, std::function<void(void*, int, const std::string&, const std::string&, const std::string&)> onConnected, std::function<void()> onServerSelected, bool darkMode, const std::string& theme, std::function<void(bool, const std::string&)> onThemeChanged);
    ~TServerList();

    void open(const std::string& account, const std::string& password, const std::string& nickname);
    void reopen();
    void show();
    void openListServerSettings();
    void setListServer(const std::string& name, const std::string& host, int port);
    std::string currentListServer() const;
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
        TServerList* serverList;
        std::vector<ServerEntry> entries;
        std::string error;
    };

    static void onRefresh(GtkButton*, gpointer data);
    static void onConnect(GtkButton*, gpointer data);
    static void onSelectionChanged(GtkTreeSelection*, gpointer data);
    static void onRowActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data);
    static void onHomepage(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    static gboolean finishLoad(gpointer data);

    void refresh();
    void connect();
    void disconnectCurrentConnection();
    void showEntry(int index);
    std::function<void()> onCloseCallback;
    std::function<void(void*, int, const std::string&, const std::string&, const std::string&)> onConnectedCallback;
    std::function<void()> onServerSelectedCallback;
    std::function<void(bool, const std::string&)> onThemeChangedCallback;
    GtkWidget* window = nullptr;
    GtkListStore* store = nullptr;
    GtkWidget* tree = nullptr;
    GtkWidget* languageField = nullptr;
    GtkWidget* versionField = nullptr;
    GtkWidget* homepageField = nullptr;
    GtkWidget* descriptionField = nullptr;
    GtkWidget* statusField = nullptr;
    GtkWidget* refreshButton = nullptr;
    GdkPixbuf* serverIcons[2] = {nullptr, nullptr};
    std::jthread worker;
    std::mutex connectionMutex;
    void* connection = nullptr;
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
};
