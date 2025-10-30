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
private:
    static void onGetList(GtkButton*, gpointer data);
    static void onGetAccounts(GtkButton*, gpointer data);
    static void onAdd(GtkButton*, gpointer data);
    static void onAccountActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data);
    static void onApply(GtkButton*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    static void onPlayerTextData(const char* type, const char* account, const char* content, void* data);
    void openQuery();
    void openEditor(const std::string& account, const char* content);
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
    GtkListStore* store = nullptr;
    void* connection = nullptr;
    std::string editingAccount;
};
