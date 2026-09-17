#pragma once

#include "TRCAccounts.h"
#include "TRCOptions.h"

#include <filesystem>
#include <functional>
#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <cstdint>

class TStartFrame {
public:
    using ConnectCallback = std::function<void(std::uint64_t, const std::string&, const std::string&, const std::string&, const std::string&)>;
    using DirectConnectCallback = std::function<void(std::uint64_t, const std::string&, const std::string&, const std::string&, const std::string&, int)>;
    using ListServerSettingsCallback = std::function<void()>;
    using ListServerEndpointCallback = std::function<std::string()>;

    TStartFrame(RC::RCOptions& options, const std::filesystem::path& applicationDirectory, ConnectCallback onConnect, ListServerSettingsCallback onListServerSettings, ListServerEndpointCallback listServerEndpoint, DirectConnectCallback onDirectConnect = {});
    ~TStartFrame();

    void show();
    void toggleVisibility();
    GdkWindow* nativeWindow() const { return window != nullptr ? gtk_widget_get_window(window) : nullptr; }
    GtkWindow* windowHandle() const { return GTK_WINDOW(window); }
    bool mcpVisible() const;
    std::string mcpAccount() const;
    std::string mcpNickname() const;
    bool mcpHasPassword() const;
    bool mcpSubmit(const std::string& account, const std::string& nickname, std::string& error);
    bool editAccount(const std::string& accountName, GtkWindow* parent = nullptr, int accountIndex = -1);
    std::vector<RC::RCAccount> accountsForListServer(const std::string& listServer) const;
    std::vector<RC::RCAccount> allAccounts() const { return accounts.entries(); }

private:
    static void onConnect(GtkButton*, gpointer data);
    static void onListServerSettings(GtkButton*, gpointer data);
    static void onManageAccounts(GtkButton*, gpointer data);
    static void onAccountChanged(GtkComboBox*, gpointer data);
    static void onAccountEntryChanged(GtkEditable*, gpointer data);
    static gboolean onAccountPointerEnter(GtkWidget*, GdkEventCrossing*, gpointer data);
    static gboolean onAccountPointerLeave(GtkWidget*, GdkEventCrossing*, gpointer data);
    static gboolean onAccountManagePointerEnter(GtkWidget*, GdkEventCrossing*, gpointer data);
    static gboolean onAccountManagePointerLeave(GtkWidget*, GdkEventCrossing*, gpointer data);
    static gboolean onAccountFocusIn(GtkWidget*, GdkEventFocus*, gpointer data);
    static gboolean onAccountFocusOut(GtkWidget*, GdkEventFocus*, gpointer data);
    static gboolean onAccountManageHideLater(gpointer data);
    static void onCancel(GtkButton*, gpointer data);
    static void onDestroy(GtkWidget*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);

    void connect();
    void selectAccount(const std::string& accountName);
    void updateAccountTitle(int accountIndex);
    void refreshAccountMenu();
    void openAccountManager();
    void scheduleAccountManageHide();
    std::string getText(GtkWidget* widget) const;

    RC::RCOptions& options;
    RC::RCAccounts accounts;
    std::filesystem::path applicationDirectory;
    ConnectCallback onConnectCallback;
    DirectConnectCallback onDirectConnectCallback;
    ListServerSettingsCallback onListServerSettingsCallback;
    ListServerEndpointCallback listServerEndpointCallback;
    std::string selectedAccount;
    GtkWidget* window = nullptr;
    GtkWidget* titlebar = nullptr;
    GtkWidget* nicknameField = nullptr;
    GtkWidget* accountCombo = nullptr;
    GtkWidget* accountField = nullptr;
    GtkWidget* accountManageButton = nullptr;
    bool accountHovered = false;
    bool accountManageHovered = false;
    bool accountSelectionInProgress = false;
    std::shared_ptr<bool> callbackAlive = std::make_shared<bool>(true);
    int selectedAccountIndex = -1;
    int requestedAccountIndex = -1;
    GtkWidget* passwordField = nullptr;
    GtkWidget* passwordCheck = nullptr;
    GtkWidget* graphicsCheck = nullptr;
};
