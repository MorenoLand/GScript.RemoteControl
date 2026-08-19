#pragma once

#include <gtk/gtk.h>
#include <string>

class TAccountsWindow {
public:
    TAccountsWindow();
    ~TAccountsWindow();
    void open(void* connection);
    void setAccounts(const char* accounts);
    void showEditor(void* connection, const std::string& account, const char* content);
    void setServerName(const std::string& server);
    void setUseNewBanType(bool enabled);
private:
    static void onGetList(GtkButton*, gpointer data);
    static void onGetAccounts(GtkButton*, gpointer data);
    static void onAdd(GtkButton*, gpointer data);
    static void onAccountActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data);
    static gboolean onAccountContext(GtkWidget*, GdkEventButton*, gpointer data);
    static void onEditAttributes(GtkMenuItem*, gpointer data);
    static void onEditAccount(GtkMenuItem*, gpointer data);
    static void onEditRights(GtkMenuItem*, gpointer data);
    static void onEditComments(GtkMenuItem*, gpointer data);
    static void onEditAccess(GtkMenuItem*, gpointer data);
    static void onBanHistory(GtkMenuItem*, gpointer data);
    static void onStaffActivity(GtkMenuItem*, gpointer data);
    static void onReset(GtkMenuItem*, gpointer data);
    static void onDeleteAccount(GtkMenuItem*, gpointer data);
    static void onApply(GtkButton*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void openQuery();
    void openEditor(const std::string& account, const char* content);
    void requestSelectedAccount();
    std::string selectedAccount() const;
    std::string accountText() const;
    GtkWidget* queryWindow = nullptr;
    GtkWidget* listWindow = nullptr;
    GtkWidget* editorWindow = nullptr;
    GtkWidget* accountField = nullptr;
    GtkWidget* conditionsField = nullptr;
    GtkWidget* nameField = nullptr;
    GtkWidget* passwordField = nullptr;
    GtkWidget* emailField = nullptr;
    GtkWidget* levelField = nullptr;
    GtkWidget* worldsField = nullptr;
    GtkWidget* bannedCheck = nullptr;
    GtkWidget* guestCheck = nullptr;
    GtkWidget* banTimeField = nullptr;
    GtkWidget* reasonField = nullptr;
    GtkWidget* accountTree = nullptr;
    GtkListStore* store = nullptr;
    void* connection = nullptr;
    std::string editingAccount;
    std::string serverName;
    bool useNewBanType = true;
};
