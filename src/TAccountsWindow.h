#pragma once
#include <gtk/gtk.h>

class TAccountsWindow {
public:
    TAccountsWindow();
    ~TAccountsWindow();
    void open(void* connection);
    void setAccounts(const char* accounts);
private:
    static void onGetList(GtkButton*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    GtkWidget* window = nullptr;
    GtkWidget* accountField = nullptr;
    GtkWidget* conditionsField = nullptr;
    GtkListStore* store = nullptr;
    void* connection = nullptr;
};
