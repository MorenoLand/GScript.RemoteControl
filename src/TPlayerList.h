#pragma once

#include <gtk/gtk.h>

class TPlayerList {
public:
    TPlayerList();
    ~TPlayerList();
    void open(void* connection);
private:
    static void onRefresh(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void refresh();
    GtkWidget* window = nullptr;
    GtkListStore* store = nullptr;
    void* connection = nullptr;
};
