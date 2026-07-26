#pragma once

#include "TRCOptions.h"
#include "TRCAccounts.h"

#include <filesystem>
#include <functional>
#include <gtk/gtk.h>
#include <string>

class TStartFrame {
public:
    using ConnectCallback = std::function<void(const std::string&, const std::string&, const std::string&)>;
    using ListServerSettingsCallback = std::function<void()>;

    TStartFrame(RC::RCOptions& options, const std::filesystem::path& applicationDirectory, ConnectCallback onConnect, ListServerSettingsCallback onListServerSettings);
    ~TStartFrame();

    void show();
    void toggleVisibility();
    GdkWindow* nativeWindow() const { return window != nullptr ? gtk_widget_get_window(window) : nullptr; }

private:
    static void onConnect(GtkButton*, gpointer data);
    static void onListServerSettings(GtkButton*, gpointer data);
    static void onAccountChanged(GtkComboBox*, gpointer data);
    static void onCancel(GtkButton*, gpointer data);
    static void onDestroy(GtkWidget*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);

    void connect();
    std::string getText(GtkWidget* widget) const;

    RC::RCOptions& options;
    RC::RCAccounts accounts;
    std::filesystem::path applicationDirectory;
    ConnectCallback onConnectCallback;
    ListServerSettingsCallback onListServerSettingsCallback;
    GtkWidget* window = nullptr;
    GtkWidget* nicknameField = nullptr;
    GtkWidget* accountField = nullptr;
    GtkWidget* accountCombo = nullptr;
    GtkWidget* passwordField = nullptr;
    GtkWidget* passwordCheck = nullptr;
    GtkWidget* graphicsCheck = nullptr;
};
