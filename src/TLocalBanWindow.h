#pragma once

#include <gtk/gtk.h>
#include <string>

class TLocalBanWindow {
public:
    TLocalBanWindow();
    ~TLocalBanWindow();
    void open(void* connection, const std::string& account, const std::string& details);
private:
    static void onApply(GtkButton*, gpointer data);
    static void onCancel(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    GtkWidget* window = nullptr;
    GtkWidget* bannedCheck = nullptr;
    GtkWidget* text = nullptr;
    void* connection = nullptr;
    std::string account;
};
