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

class TRemoteFrame {
public:
    TRemoteFrame(const RC3::RCOptions& options, const std::filesystem::path& applicationDirectory, std::function<void()> onClose);
    ~TRemoteFrame();

    void open(void* connection, const std::string& serverName);

private:
    static void onSend(GtkButton*, gpointer data);
    static void onPlayerList(GtkMenuItem*, gpointer data);
    static void onFileBrowser(GtkMenuItem*, gpointer data);
    static gboolean onGraphicalButton(GtkWidget*, GdkEventButton*, gpointer data);
    static gboolean onEditKey(GtkWidget*, GdkEventKey*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    static gboolean processEvents(gpointer data);
    static void onConnected(void* data);
    static void onDisconnected(const char* reason, void* data);
    static void onMessage(const char* message, void* data);
    static void onIrcMessage(const char* channel, const char* line, void* data);

    void appendChat(const std::string& message);
    void appendChannelMessage(const std::string& channel, const std::string& message);
    void configureChatField(GtkWidget* field);
    bool applyAlertTag(std::string& message);
    void send();
    void addMenuItem(GtkWidget* menu, const char* label, GCallback callback = nullptr);
    void graphicalAction(int index);

    std::function<void()> onCloseCallback;
    GtkWidget* window = nullptr;
    GtkWidget* chatField = nullptr;
    GtkWidget* notebook = nullptr;
    GtkWidget* graphicalFixed = nullptr;
    GtkWidget* editField = nullptr;
    GtkWidget* serverLabel = nullptr;
    GtkWidget* playersLabel = nullptr;
    std::array<GtkWidget*, 12> graphicalButtons{};
    void* connection = nullptr;
    guint eventSource = 0;
    TPlayerList* playerList = nullptr;
    TFileBrowserTree* fileBrowser = nullptr;
    const RC3::RCOptions& options;
    std::filesystem::path applicationDirectory;
    std::unordered_map<std::string, GtkWidget*> channelFields;
};
