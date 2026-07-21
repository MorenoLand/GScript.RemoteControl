#pragma once

#include <functional>
#include <gtk/gtk.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct RCConnection;

class TServerList {
public:
    TServerList(std::function<void()> onClose, std::function<void(void*, const std::string&, const std::string&)> onConnected);
    ~TServerList();

    void open(const std::string& account, const std::string& password, const std::string& nickname);
    void reopen();
    void show();
    void openListServerSettings();

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
    void showEntry(int index);
    void setListServer(const std::string& host, int port);

    std::function<void()> onCloseCallback;
    std::function<void(void*, const std::string&, const std::string&)> onConnectedCallback;
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
    std::vector<ServerEntry> entries;
};
