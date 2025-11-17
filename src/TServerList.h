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
    explicit TServerList(std::function<void()> onClose);
    ~TServerList();

    void open(const std::string& account, const std::string& password);

private:
    struct ServerEntry {
        std::string name;
        std::string language;
        std::string description;
        int players = 0;
    };

    struct LoadResult {
        TServerList* serverList;
        std::vector<ServerEntry> entries;
        std::string error;
    };

    static void onRefresh(GtkButton*, gpointer data);
    static void onConnect(GtkButton*, gpointer data);
    static void onSelectionChanged(GtkTreeSelection*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    static gboolean finishLoad(gpointer data);

    void refresh();
    void connect();
    void showEntry(int index);

    std::function<void()> onCloseCallback;
    GtkWidget* window = nullptr;
    GtkListStore* store = nullptr;
    GtkWidget* tree = nullptr;
    GtkWidget* languageField = nullptr;
    GtkWidget* descriptionField = nullptr;
    GtkWidget* statusField = nullptr;
    std::jthread worker;
    std::mutex connectionMutex;
    void* connection = nullptr;
    std::string account;
    std::string password;
    std::vector<ServerEntry> entries;
};
