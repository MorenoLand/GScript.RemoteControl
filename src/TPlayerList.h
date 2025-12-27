#pragma once

#include <gtk/gtk.h>
#include <vector>

class TPlayerList {
public:
    TPlayerList();
    ~TPlayerList();
    void open(void* connection);
private:
    static void onRefresh(GtkButton*, gpointer data);
    static void onMassPM(GtkButton*, gpointer data);
    static void onAdminMessage(GtkButton*, gpointer data);
    static void onPMServers(int count, void* data);
    static void onPMGuilds(int count, void* data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void refresh();
    void refreshRemoteLists();
    void sendMassPM();
    void sendAdminMessage();
    std::vector<int> playerIds() const;
    GtkWidget* window = nullptr;
    GtkWidget* tree = nullptr;
    GtkListStore* store = nullptr;
    GtkListStore* guildStore = nullptr;
    GtkListStore* serverStore = nullptr;
    GtkListStore* channelStore = nullptr;
    GdkPixbuf* onlineIcon = nullptr;
    GdkPixbuf* channelIcon = nullptr;
    void* connection = nullptr;
};
