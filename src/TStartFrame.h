#pragma once

#include "TRCAccounts.h"
#include "TRCOptions.h"

#include <filesystem>
#include <functional>
#include <gtk/gtk.h>
#include <string>

class TStartFrame {
public:
    using ConnectCallback = std::function<void(const std::string&, const std::string&, const std::string&)>;
    using ListServerSettingsCallback = std::function<void()>;
    using ListServerEndpointCallback = std::function<std::string()>;

    TStartFrame(RC::RCOptions& options, const std::filesystem::path& applicationDirectory, ConnectCallback onConnect, ListServerSettingsCallback onListServerSettings, ListServerEndpointCallback listServerEndpoint);
    ~TStartFrame();

    void show();
    void toggleVisibility();
    GdkWindow* nativeWindow() const { return window != nullptr ? gtk_widget_get_window(window) : nullptr; }
    bool mcpVisible() const;
    std::string mcpAccount() const;
    std::string mcpNickname() const;
    bool mcpHasPassword() const;
    bool mcpSubmit(const std::string& account, const std::string& nickname, std::string& error);

private:
    static void onConnect(GtkButton*, gpointer data);
    static void onListServerSettings(GtkButton*, gpointer data);
    static void onManageAccounts(GtkButton*, gpointer data);
    static void onAccountChanged(GtkComboBox*, gpointer data);
    static void onCancel(GtkButton*, gpointer data);
    static void onDestroy(GtkWidget*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);

    void connect();
    void selectAccount(const std::string& accountName);
    void refreshAccountMenu();
    void openAccountManager();
    bool editAccount(const std::string& accountName);
    std::string getText(GtkWidget* widget) const;

    RC::RCOptions& options;
    RC::RCAccounts accounts;
    std::filesystem::path applicationDirectory;
    ConnectCallback onConnectCallback;
    ListServerSettingsCallback onListServerSettingsCallback;
    ListServerEndpointCallback listServerEndpointCallback;
    std::string selectedAccount;
    GtkWidget* window = nullptr;
    GtkWidget* nicknameField = nullptr;
    GtkWidget* accountCombo = nullptr;
    GtkWidget* passwordField = nullptr;
    GtkWidget* passwordCheck = nullptr;
    GtkWidget* graphicsCheck = nullptr;
};
