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
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void refresh();
    void sendMassPM();
    void sendAdminMessage();
    std::vector<int> playerIds() const;
    GtkWidget* window = nullptr;
    GtkWidget* tree = nullptr;
    GtkListStore* store = nullptr;
    GtkListStore* serverStore = nullptr;
    GdkPixbuf* onlineIcon = nullptr;
    void* connection = nullptr;
};
