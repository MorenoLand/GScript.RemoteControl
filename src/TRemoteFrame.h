#pragma once

#include <gtk/gtk.h>
#include <functional>
#include <string>

class TPlayerList;
class TFileBrowserTree;

class TRemoteFrame {
public:
    explicit TRemoteFrame(std::function<void()> onClose);
    ~TRemoteFrame();

    void open(void* connection);

private:
    static void onSend(GtkButton*, gpointer data);
    static void onPlayerList(GtkMenuItem*, gpointer data);
    static void onFileBrowser(GtkMenuItem*, gpointer data);
    static gboolean onEditKey(GtkWidget*, GdkEventKey*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    static gboolean processEvents(gpointer data);
    static void onConnected(void* data);
    static void onDisconnected(const char* reason, void* data);
    static void onMessage(const char* message, void* data);

    void appendChat(const std::string& message);
    void send();

    std::function<void()> onCloseCallback;
    GtkWidget* window = nullptr;
    GtkWidget* chatField = nullptr;
    GtkWidget* editField = nullptr;
    GtkWidget* serverLabel = nullptr;
    GtkWidget* playersLabel = nullptr;
    void* connection = nullptr;
    guint eventSource = 0;
    TPlayerList* playerList = nullptr;
    TFileBrowserTree* fileBrowser = nullptr;
};
