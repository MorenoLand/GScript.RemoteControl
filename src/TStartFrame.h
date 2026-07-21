#pragma once

#include "RCOptions.h"
#include "RCAccounts.h"

#include <filesystem>
#include <functional>
#include <gtk/gtk.h>
#include <string>

class TStartFrame {
public:
    using ConnectCallback = std::function<void(const std::string&, const std::string&, const std::string&)>;

    TStartFrame(RC::RCOptions& options, const std::filesystem::path& applicationDirectory, ConnectCallback onConnect);
    ~TStartFrame();

    void show();

private:
    static void onConnect(GtkButton*, gpointer data);
    static void onAccountChanged(GtkComboBox*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);

    void connect();
    std::string getText(GtkWidget* widget) const;

    RC::RCOptions& options;
    RC::RCAccounts accounts;
    std::filesystem::path applicationDirectory;
    ConnectCallback onConnectCallback;
    GtkWidget* window = nullptr;
    GtkWidget* nicknameField = nullptr;
    GtkWidget* accountField = nullptr;
    GtkWidget* accountCombo = nullptr;
    GtkWidget* passwordField = nullptr;
    GtkWidget* passwordCheck = nullptr;
    GtkWidget* graphicsCheck = nullptr;
};
