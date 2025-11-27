#pragma once

#include <gtk/gtk.h>

class TNPCList {
public:
    TNPCList();
    ~TNPCList();
    void open(void* connection);
private:
    static void onRefresh(GtkButton*, gpointer data);
    static void onAdd(GtkButton*, gpointer data);
    static void onAddResponse(GtkDialog*, gint response, gpointer data);
    static gboolean onTreeButton(GtkWidget*, GdkEventButton*, gpointer data);
    static void onReset(GtkMenuItem*, gpointer data);
    static void onDeleteNPC(GtkMenuItem*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static void onNPCChanged(int id, const char* name, void* data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void refresh();
    int firstFreeNPCId() const;
    GtkWidget* window = nullptr;
    GtkWidget* tree = nullptr;
    GtkListStore* store = nullptr;
    void* connection = nullptr;
    int selectedNPCId = -1;
};
