#pragma once

#include "RCOptions.h"

#include <gtk/gtk.h>
#include <filesystem>

class TOptionsWindow {
public:
    TOptionsWindow(RC::RCOptions& options, const std::filesystem::path& applicationDirectory);
    ~TOptionsWindow();
    void open();
private:
    static void onClose(GtkButton*, gpointer data);
    static void onBrowseDownload(GtkButton*, gpointer data);
    static void onBrowseLog(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void save();
    GtkWidget* window = nullptr;
    GtkWidget* nickname = nullptr;
    GtkWidget* downloadFolder = nullptr;
    GtkWidget* logFile = nullptr;
    GtkWidget* chatFontSize = nullptr;
    GtkWidget* ignoreMass = nullptr;
    GtkWidget* ignoreMassClient = nullptr;
    GtkWidget* globalPMs = nullptr;
    GtkWidget* attachAway = nullptr;
    GtkWidget* buddies = nullptr;
    GtkWidget* separateNC = nullptr;
    GtkWidget* timestamps = nullptr;
    GtkWidget* pmAlerts = nullptr;
    GtkWidget* logChat = nullptr;
    GtkWidget* syntax = nullptr;
    GtkWidget* autoIndent = nullptr;
    GtkWidget* smartHomeEnd = nullptr;
    GtkWidget* brackets = nullptr;
    GtkWidget* lineNumbers = nullptr;
    GtkWidget* scriptTabWidth = nullptr;
    GtkWidget* scriptUseTabs = nullptr;
    GtkWidget* scriptFontSize = nullptr;
    RC::RCOptions& options;
    std::filesystem::path applicationDirectory;
};
