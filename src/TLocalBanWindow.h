#pragma once

#include <gtk/gtk.h>
#include <string>
#include <vector>

class TLocalBanWindow {
public:
    TLocalBanWindow();
    ~TLocalBanWindow();
    void open(void* connection, const std::string& account, const std::string& computerId, const std::string& details);
    void setBanTypes(const char* types);
private:
    static void onApply(GtkButton*, gpointer data);
    static void onCancel(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    GtkWidget* window = nullptr;
    struct Scope { GtkWidget* banned = nullptr; GtkWidget* reset = nullptr; GtkWidget* type = nullptr; GtkWidget* release = nullptr; GtkWidget* reason = nullptr; GtkWidget* world = nullptr; std::string target; };
    Scope scopes[4];
    void* connection = nullptr;
    std::string account;
    std::string computerId;
    std::vector<std::string> banTypes;
};
