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
    static void onBannedChanged(GtkToggleButton*, gpointer data);
    static void onBanTypeChanged(GtkComboBox*, gpointer data);
    static void onBanHistory(GtkButton*, gpointer data);
    static void onStaffActivity(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    GtkWidget* window = nullptr;
    struct Scope { GtkWidget* page = nullptr; GtkWidget* tab = nullptr; GtkWidget* tabIcon = nullptr; GtkWidget* banned = nullptr; GtkWidget* reset = nullptr; GtkWidget* type = nullptr; GtkWidget* timeLeft = nullptr; GtkWidget* reason = nullptr; std::string target; std::string releaseTime; };
    Scope scopes[4];
    void* connection = nullptr;
    std::string account;
    std::string computerId;
    std::vector<std::string> banTypes;
    std::vector<int> banDurations;
    void updateTimeLeft(int scope);
    void updateTabIcon(int scope);
};
