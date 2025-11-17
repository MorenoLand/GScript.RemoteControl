#pragma once

#include "RCOptions.h"

#include <filesystem>
#include <functional>
#include <gtk/gtk.h>
#include <string>

class TStartFrame {
public:
    using ConnectCallback = std::function<void(const std::string&, const std::string&)>;

    TStartFrame(RC3::RCOptions& options, const std::filesystem::path& applicationDirectory, ConnectCallback onConnect);
    ~TStartFrame();

    void show();

private:
    static void onConnect(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);

    void connect();
    std::string getText(GtkWidget* widget) const;

    RC3::RCOptions& options;
    std::filesystem::path applicationDirectory;
    ConnectCallback onConnectCallback;
    GtkWidget* window = nullptr;
    GtkWidget* nicknameField = nullptr;
    GtkWidget* accountField = nullptr;
    GtkWidget* passwordField = nullptr;
    GtkWidget* passwordCheck = nullptr;
    GtkWidget* graphicsCheck = nullptr;
};
