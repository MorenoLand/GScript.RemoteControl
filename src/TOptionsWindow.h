#pragma once

#include "RCOptions.h"

#include <gtk/gtk.h>
#include <filesystem>
#include <functional>

class TOptionsWindow {
public:
    TOptionsWindow(RC::RCOptions& options, const std::filesystem::path& applicationDirectory, std::function<void(const RC::RCOptions&)> onSaved);
    ~TOptionsWindow();
    void open();
private:
    static void onClose(GtkButton*, gpointer data);
    static void onThemeChanged(GtkComboBox*, gpointer data);
    static void onSyntaxThemeChanged(GtkComboBox*, gpointer data);
    static void onSyncSyntaxThemeChanged(GtkToggleButton*, gpointer data);
    static void onBrowseDownload(GtkButton*, gpointer data);
    static void onBrowseLog(GtkButton*, gpointer data);
    static void onBrowseAutocompleteSource(GtkButton*, gpointer data);
    static void onBrowseBackground(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void save();
    void applyThemeSelection();
    void applySyntaxThemeSelection();
    void applySyntaxThemeSync();
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
    GtkWidget* minimap = nullptr;
    GtkWidget* lsp = nullptr;
    GtkWidget* autocompleteSource = nullptr;
    GtkWidget* scriptTabWidth = nullptr;
    GtkWidget* scriptUseTabs = nullptr;
    GtkWidget* scriptFontSize = nullptr;
    GtkWidget* theme = nullptr;
    GtkWidget* syntaxTheme = nullptr;
    GtkWidget* syncSyntaxTheme = nullptr;
    GtkWidget* syncColors = nullptr;
    GtkWidget* chatbarTextColor = nullptr;
    GtkWidget* chatbarBackgroundColor = nullptr;
    GtkWidget* chatTextColor = nullptr;
    GtkWidget* chatBoldColor = nullptr;
    GtkWidget* chatBackgroundColor = nullptr;
    GtkWidget* labelColor = nullptr;
    GtkWidget* labelBackgroundColor = nullptr;
    GtkWidget* serverLabel = nullptr;
    GtkWidget* playersLabel = nullptr;
    GtkWidget* npcServerLabel = nullptr;
    GtkWidget* backgroundImage = nullptr;
    RC::RCOptions& options;
    std::filesystem::path applicationDirectory;
    std::function<void(const RC::RCOptions&)> onSaved;
};
