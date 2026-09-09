#include "TOptionsWindow.h"
#include "TGScriptEditor.h"
#include "TTheme.h"

#include <algorithm>

void refreshGlobalVisibilityHotkey(const std::string& hotkey);
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace {
    GtkWidget* addCheck(GtkBox* box, const char* label, bool value) {
        GtkWidget* field = gtk_check_button_new_with_label(label);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(field), value);
        gtk_box_pack_start(box, field, false, false, 0);
        return field;
    }
    GtkWidget* addCheckGrid(GtkGrid* grid, const char* label, bool value, int index, int columns = 3) {
        GtkWidget* field = gtk_check_button_new_with_label(label);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(field), value);
        gtk_widget_set_size_request(field, 150, -1);
        GtkWidget* text = gtk_bin_get_child(GTK_BIN(field));
        gtk_label_set_line_wrap(GTK_LABEL(text), true);
        gtk_label_set_max_width_chars(GTK_LABEL(text), 21);
        gtk_grid_attach(grid, field, index % columns, index / columns, 1, 1);
        return field;
    }
    GtkWidget* addEntry(GtkGrid* grid, const char* label, const std::string& value, int row, int column = 0) {
        GtkWidget* field = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(field), value.c_str());
        gtk_entry_set_width_chars(GTK_ENTRY(field), 16);
        const int base = column * 3;
        gtk_widget_set_hexpand(field, true);
        gtk_grid_attach(grid, gtk_label_new(label), base, row, 1, 1);
        gtk_grid_attach(grid, field, base + 1, row, 1, 1);
        return field;
    }
    GtkWidget* addFontButton(GtkGrid* grid, const char* label, const std::string& value, int size, int row, int column = 0) {
        GtkWidget* field = gtk_font_button_new_with_font((value + " " + std::to_string(size)).c_str());
        gtk_font_button_set_use_font(GTK_FONT_BUTTON(field), true);
        gtk_font_button_set_use_size(GTK_FONT_BUTTON(field), true);
        gtk_font_button_set_show_style(GTK_FONT_BUTTON(field), true);
        gtk_font_button_set_show_size(GTK_FONT_BUTTON(field), true);
        gtk_widget_set_size_request(field, 165, -1);
        const int base = column * 3;
        gtk_widget_set_hexpand(field, true);
        gtk_grid_attach(grid, gtk_label_new(label), base, row, 1, 1);
        gtk_grid_attach(grid, field, base + 1, row, 1, 1);
        return field;
    }
    void readFontButton(GtkWidget* field, std::string& value, int& size) {
        gchar* selected = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(field));
        if (selected == nullptr) return;
        PangoFontDescription* description = pango_font_description_from_string(selected);
        g_free(selected);
        const int selectedSize = pango_font_description_get_size(description);
        if (selectedSize > 0) size = std::clamp(selectedSize / PANGO_SCALE, 1, 1000);
        pango_font_description_unset_fields(description, PANGO_FONT_MASK_SIZE);
        gchar* face = pango_font_description_to_string(description);
        if (face != nullptr && *face != '\0') value = face;
        g_free(face);
        pango_font_description_free(description);
    }
    GtkWidget* addColorEntry(GtkGrid* grid, const char* label, const std::string& value, int row, int column = 0) {
        GtkWidget* field = gtk_color_button_new();
        GdkRGBA color{};
        if (!gdk_rgba_parse(&color, value.c_str())) gdk_rgba_parse(&color, "#000000");
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(field), &color);
        const int base = column * 3;
        gtk_grid_attach(grid, gtk_label_new(label), base, row, 1, 1);
        gtk_grid_attach(grid, field, base + 1, row, 1, 1);
        return field;
    }
    std::string colorValue(GtkWidget* field) {
        GdkRGBA color{};
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(field), &color);
        char value[16];
        std::snprintf(value, sizeof(value), "#%02x%02x%02x", static_cast<unsigned int>(color.red * 255.0 + 0.5), static_cast<unsigned int>(color.green * 255.0 + 0.5), static_cast<unsigned int>(color.blue * 255.0 + 0.5));
        return value;
    }
    std::string colorValueWithAlpha(GtkWidget* field) {
        GdkRGBA color{};
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(field), &color);
        char value[20];
        std::snprintf(value, sizeof(value), "#%02x%02x%02x%02x", static_cast<unsigned int>(color.red * 255.0 + 0.5), static_cast<unsigned int>(color.green * 255.0 + 0.5), static_cast<unsigned int>(color.blue * 255.0 + 0.5), static_cast<unsigned int>(color.alpha * 255.0 + 0.5));
        return value;
    }
    std::string tintForColor(const std::string& value) {
        GdkRGBA color{};
        if (!gdk_rgba_parse(&color, value.c_str())) return "#00000000";
        char result[20];
        std::snprintf(result, sizeof(result), "#%02x%02x%02x%02x", static_cast<unsigned int>(color.red * 255.0 + 0.5), static_cast<unsigned int>(color.green * 255.0 + 0.5), static_cast<unsigned int>(color.blue * 255.0 + 0.5), 96u);
        return result;
    }
    void themeColors(const std::string& theme, RC::RCOptions& options);
    std::string tintForTheme(const std::string& theme, const RC::RCOptions& current) {
        RC::RCOptions themed = current;
        themeColors(theme, themed);
        return tintForColor(themed.colorchatback);
    }
    void themeColors(const std::string& theme, RC::RCOptions& options) {
        if (theme == "dracula") { options.coloredit = "#f8f8f2"; options.coloreditback = "#282a36"; options.colorchat = "#f8f8f2"; options.colorchatbold = "#50fa7b"; options.colorchatback = "#282a36"; options.colorlabel = "#50fa7b"; options.colorlabelback = "#282a36"; }
        else if (theme == "material") { options.coloredit = "#eeffff"; options.coloreditback = "#263238"; options.colorchat = "#eeffff"; options.colorchatbold = "#80cbc4"; options.colorchatback = "#263238"; options.colorlabel = "#80cbc4"; options.colorlabelback = "#263238"; }
        else if (theme == "ayu-mirage") { options.coloredit = "#cccac2"; options.coloreditback = "#242936"; options.colorchat = "#cccac2"; options.colorchatbold = "#ffcc66"; options.colorchatback = "#242936"; options.colorlabel = "#ffcc66"; options.colorlabelback = "#242936"; }
        else if (theme == "nord") { options.coloredit = "#eceff4"; options.coloreditback = "#2e3440"; options.colorchat = "#eceff4"; options.colorchatbold = "#88c0d0"; options.colorchatback = "#2e3440"; options.colorlabel = "#88c0d0"; options.colorlabelback = "#2e3440"; }
        else if (theme == "monokai") { options.coloredit = "#f8f8f2"; options.coloreditback = "#1e1f1c"; options.colorchat = "#f8f8f2"; options.colorchatbold = "#a6e22e"; options.colorchatback = "#1e1f1c"; options.colorlabel = "#a6e22e"; options.colorlabelback = "#1e1f1c"; }
        else if (theme == "one-dark") { options.coloredit = "#abb2bf"; options.coloreditback = "#282c34"; options.colorchat = "#abb2bf"; options.colorchatbold = "#61afef"; options.colorchatback = "#282c34"; options.colorlabel = "#61afef"; options.colorlabelback = "#282c34"; }
        else if (theme == "tokyo-night") { options.coloredit = "#c0caf5"; options.coloreditback = "#1a1b26"; options.colorchat = "#c0caf5"; options.colorchatbold = "#7aa2f7"; options.colorchatback = "#1a1b26"; options.colorlabel = "#7aa2f7"; options.colorlabelback = "#1a1b26"; }
        else if (theme == "gruvbox") { options.coloredit = "#ebdbb2"; options.coloreditback = "#282828"; options.colorchat = "#ebdbb2"; options.colorchatbold = "#fabd2f"; options.colorchatback = "#282828"; options.colorlabel = "#fabd2f"; options.colorlabelback = "#282828"; }
        else if (theme == "solarized") { options.coloredit = "#839496"; options.coloreditback = "#002b36"; options.colorchat = "#839496"; options.colorchatbold = "#b58900"; options.colorchatback = "#002b36"; options.colorlabel = "#b58900"; options.colorlabelback = "#002b36"; }
        else if (theme == "catppuccin") { options.coloredit = "#cdd6f4"; options.coloreditback = "#1e1e2e"; options.colorchat = "#cdd6f4"; options.colorchatbold = "#cba6f7"; options.colorchatback = "#1e1e2e"; options.colorlabel = "#cba6f7"; options.colorlabelback = "#1e1e2e"; }
        else if (theme == "light") { options.coloredit = "#202020"; options.coloreditback = "#ffffff"; options.colorchat = "#202020"; options.colorchatbold = "#008000"; options.colorchatback = "#ffffff"; options.colorlabel = "#008000"; options.colorlabelback = "#ffffff"; }
        else { options.coloredit = "#00ff00"; options.coloreditback = "#1e1e1e"; options.colorchat = "#d4d4d4"; options.colorchatbold = "#00C000"; options.colorchatback = "#1e1e1e"; options.colorlabel = "#00C000"; options.colorlabelback = "#1e1e1e"; }
    }
}

TOptionsWindow::TOptionsWindow(RC::RCOptions& nextOptions, const std::filesystem::path& nextApplicationDirectory, std::function<void(const RC::RCOptions&)> nextOnSaved) : options(nextOptions), applicationDirectory(nextApplicationDirectory), onSaved(std::move(nextOnSaved)) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "OptionsWindow");
    gtk_window_set_title(GTK_WINDOW(window), "Options");
    gtk_window_set_default_size(GTK_WINDOW(window), 550, 420);
    gtk_window_set_resizable(GTK_WINDOW(window), false);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    notebook = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(notebook), 5);
    GtkWidget* general = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(general), 5);
    GtkWidget* generalChecks = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(generalChecks), 2);
    gtk_grid_set_column_spacing(GTK_GRID(generalChecks), 8);
    ignoreMass = addCheckGrid(GTK_GRID(generalChecks), "Ignore Mass PMs", options.nomassmessages, 0, 4);
    ignoreMassClient = addCheckGrid(GTK_GRID(generalChecks), "Ignore Mass PMs if client is on", options.nomassifclienton, 1, 4);
    attachAway = addCheckGrid(GTK_GRID(generalChecks), "Go in away mode when closing playerlist", options.attachaway, 2, 4);
    afkEnabled = addCheckGrid(GTK_GRID(generalChecks), "Set nickname to Away when inactive", options.afkenabled, 10, 4);
    globalPMs = addCheckGrid(GTK_GRID(generalChecks), "Allow Global PMs", options.globalpms, 3, 4);
    buddies = addCheckGrid(GTK_GRID(generalChecks), "Show yourself on other buddy lists", options.buddytracking, 4, 4);
    separateNC = addCheckGrid(GTK_GRID(generalChecks), "Separate NC from RC Chat", options.separatenc, 5, 4);
    timestamps = addCheckGrid(GTK_GRID(generalChecks), "Timestamp RC Messages", options.rctimestamps, 6, 4);
    pmAlerts = addCheckGrid(GTK_GRID(generalChecks), "Show new PM Alerts in RC Chat", options.newpmalerts, 7, 4);
    notificationSounds = addCheckGrid(GTK_GRID(generalChecks), "Play sounds for alerts and PMs", options.notificationsounds, 8, 4);
    separateFindResults = addCheckGrid(GTK_GRID(generalChecks), "Separate find results tabs", options.separatefindresults, 9, 4);
    modernFileBrowser = addCheckGrid(GTK_GRID(generalChecks), "Modern File Browser", options.modernfilebrowser, 11, 4);
    fileBrowserHoverPreview = addCheckGrid(GTK_GRID(generalChecks), "File Browser hover previews", options.filebrowserhoverpreview, 12, 4);
    fileBrowserThumbnails = addCheckGrid(GTK_GRID(generalChecks), "Modern File Browser thumbnails (Experimental)", options.filebrowserthumbnails, 13, 4);
    extensionsEnabled = addCheckGrid(GTK_GRID(generalChecks), "Extensions", options.extensionsenabled, 14, 4);
    syncEnabled = addCheckGrid(GTK_GRID(generalChecks), "Sync & Git Backups", options.syncenabled, 15, 4);
    levelListEnabled = addCheckGrid(GTK_GRID(generalChecks), "Level List (Legacy)", options.levellistenabled, 16, 4);
    useNewBanType = addCheckGrid(GTK_GRID(generalChecks), "Use new ban type", options.usenewbantype, 17, 4);
    autoReconnectNC = addCheckGrid(GTK_GRID(generalChecks), "Auto reconnect NC", options.autoreconnectnc, 18, 4);
    gtk_box_pack_start(GTK_BOX(general), generalChecks, false, false, 0);
    GtkWidget* generalGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(generalGrid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(generalGrid), 12);
    nickname = addEntry(GTK_GRID(generalGrid), "Nickname:", options.nickname, 0, 0);
    downloadFolder = addEntry(GTK_GRID(generalGrid), "Downloadfolder:", options.downloadfolder, 1, 0);
    GtkWidget* downloadBrowse = gtk_button_new_with_label("Browse");
    gtk_grid_attach(GTK_GRID(generalGrid), downloadBrowse, 2, 1, 1, 1);
    logChat = gtk_check_button_new_with_label("Log RC Chat");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(logChat), options.logrcchat);
    gtk_grid_attach(GTK_GRID(generalGrid), logChat, 0, 2, 1, 1);
    logFile = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(logFile), options.chatlogfile.c_str());
    gtk_grid_attach(GTK_GRID(generalGrid), logFile, 1, 2, 1, 1);
    GtkWidget* logBrowse = gtk_button_new_with_label("Browse");
    gtk_grid_attach(GTK_GRID(generalGrid), logBrowse, 2, 2, 1, 1);
    chatFontFamily = addFontButton(GTK_GRID(generalGrid), "Chat font:", options.chatfontfamily, options.chatfontsize, 3);
    afkTimeout = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(afkTimeout), "5", "5 minutes");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(afkTimeout), "15", "15 minutes");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(afkTimeout), "30", "30 minutes");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(afkTimeout), "60", "1 hour");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(afkTimeout), std::to_string(options.afktimeout).c_str());
    gtk_grid_attach(GTK_GRID(generalGrid), gtk_label_new("Away timeout:"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(generalGrid), afkTimeout, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(generalGrid), gtk_label_new("Global show/hide hotkey:"), 3, 0, 1, 1);
    GtkWidget* hotkeyOverlay = gtk_overlay_new();
    globalHotkey = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(globalHotkey), options.globalhotkey.c_str());
    gtk_editable_set_editable(GTK_EDITABLE(globalHotkey), false);
    gtk_entry_set_width_chars(GTK_ENTRY(globalHotkey), 16);
    gtk_widget_set_tooltip_text(globalHotkey, "Focus and press a key combination");
    gtk_container_add(GTK_CONTAINER(hotkeyOverlay), globalHotkey);
    GtkWidget* hotkeyConfirm = gtk_button_new_with_label("OK");
    gtk_button_set_relief(GTK_BUTTON(hotkeyConfirm), GTK_RELIEF_NONE);
    GtkCssProvider* hotkeyCss = gtk_css_provider_new();
    gtk_css_provider_load_from_data(hotkeyCss, "button { background-image: none; background-color: transparent; border: none; box-shadow: none; padding: 2px 7px; }", -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(hotkeyConfirm), GTK_STYLE_PROVIDER(hotkeyCss), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(hotkeyCss);
    gtk_widget_set_halign(hotkeyConfirm, GTK_ALIGN_END);
    gtk_widget_set_valign(hotkeyConfirm, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(hotkeyConfirm, "Save global show/hide hotkey");
    gtk_overlay_add_overlay(GTK_OVERLAY(hotkeyOverlay), hotkeyConfirm);
    gtk_grid_attach(GTK_GRID(generalGrid), hotkeyOverlay, 4, 0, 2, 1);
    externalEditorWorkspace = addEntry(GTK_GRID(generalGrid), "External workspace:", options.externaleditorworkspace, 1, 1);
    externalEditorCommand = addEntry(GTK_GRID(generalGrid), "External editor command:", options.externaleditorcommand, 2, 1);
    GtkWidget* externalWorkspaceBrowse = gtk_button_new_with_label("Browse");
    GtkWidget* externalEditorBrowse = gtk_button_new_with_label("Browse");
    gtk_grid_attach(GTK_GRID(generalGrid), externalWorkspaceBrowse, 5, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(generalGrid), externalEditorBrowse, 5, 2, 1, 1);
    externalEditorScope = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(externalEditorScope), "off", "Disabled");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(externalEditorScope), "scripts", "Scripts only");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(externalEditorScope), "text", "Scripts + RC text editors");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(externalEditorScope), options.externaleditorscope.c_str());
    gtk_grid_attach(GTK_GRID(generalGrid), gtk_label_new("External editor mode:"), 3, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(generalGrid), externalEditorScope, 4, 3, 2, 1);
    g_signal_connect(globalHotkey, "key-press-event", G_CALLBACK(onGlobalHotkeyKeyPress), this);
    g_signal_connect(hotkeyConfirm, "clicked", G_CALLBACK(onGlobalHotkeyConfirm), this);
    gtk_box_pack_start(GTK_BOX(general), generalGrid, false, false, 4);
    GtkWidget* script = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(script), 5);
    GtkWidget* scriptChecks = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(scriptChecks), 2);
    gtk_grid_set_column_spacing(GTK_GRID(scriptChecks), 8);
    syntax = addCheckGrid(GTK_GRID(scriptChecks), "Syntax highlighting", options.syntaxhighlighting, 0);
    autoIndent = addCheckGrid(GTK_GRID(scriptChecks), "Auto indenting", options.autoindenting, 1);
    smartHomeEnd = addCheckGrid(GTK_GRID(scriptChecks), "Smart Home/End", options.smarthomeend, 2);
    brackets = addCheckGrid(GTK_GRID(scriptChecks), "Show brackets", options.showbrackets, 3);
    lineNumbers = addCheckGrid(GTK_GRID(scriptChecks), "Show line numbers", options.showlinenumbers, 4);
    minimap = addCheckGrid(GTK_GRID(scriptChecks), "Show minimap (WIP)", options.minimap, 5);
    lsp = addCheckGrid(GTK_GRID(scriptChecks), "LSP / autocomplete", options.lsp, 6);
    scriptDiagnostics = addCheckGrid(GTK_GRID(scriptChecks), "Script analysis", options.scriptdiagnostics, 7);
    scriptUseTabs = addCheckGrid(GTK_GRID(scriptChecks), "Use real tabs for indentation", options.scriptusetabs, 8);
    gtk_box_pack_start(GTK_BOX(script), scriptChecks, false, false, 0);
    GtkWidget* scriptGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(scriptGrid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(scriptGrid), 12);
    scriptTabWidth = addEntry(GTK_GRID(scriptGrid), "Script tab width:", std::to_string(options.scripttabwidth), 0, 0);
    scriptFontFamily = addFontButton(GTK_GRID(scriptGrid), "Script font:", options.scriptfontfamily, options.scriptfontsize, 1, 0);
    autocompleteSource = addEntry(GTK_GRID(scriptGrid), "Autocomplete source:", options.autocompletesource, 2, 0);
    GtkWidget* autocompleteBrowse = gtk_button_new_with_label("Browse");
    gtk_grid_attach(GTK_GRID(scriptGrid), autocompleteBrowse, 2, 2, 1, 1);
    syntaxTheme = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(syntaxTheme), "language-spec", "Language spec");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(syntaxTheme), "dracula", "Dracula");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(syntaxTheme), "material", "Material");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(syntaxTheme), "ayu-mirage", "Ayu Mirage");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(syntaxTheme), "nord", "Nord");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(syntaxTheme), "monokai", "Monokai");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(syntaxTheme), "one-dark", "One Dark");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(syntaxTheme), "tokyo-night", "Tokyo Night");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(syntaxTheme), "gruvbox", "Gruvbox");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(syntaxTheme), "solarized", "Solarized Dark");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(syntaxTheme), "catppuccin", "Catppuccin Mocha");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(syntaxTheme), "light", "Light");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(syntaxTheme), options.syntaxtheme.c_str());
    gtk_grid_attach(GTK_GRID(scriptGrid), gtk_label_new("Syntax theme:"), 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(scriptGrid), syntaxTheme, 4, 0, 2, 1);
    syncSyntaxTheme = gtk_check_button_new_with_label("Sync syntax theme with UI theme");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(syncSyntaxTheme), options.syncsyntaxtheme);
    gtk_grid_attach(GTK_GRID(scriptGrid), syncSyntaxTheme, 4, 1, 2, 1);
    gtk_widget_set_sensitive(syntaxTheme, !options.syncsyntaxtheme);
    gtk_box_pack_start(GTK_BOX(script), scriptGrid, false, false, 4);
    GtkWidget* formatter = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(formatter), 5);
    GtkWidget* formatterGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(formatterGrid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(formatterGrid), 5);
    formatIndentWidth = addEntry(GTK_GRID(formatterGrid), "Indent width:", std::to_string(options.formatindentwidth), 0);
    gtk_box_pack_start(GTK_BOX(formatter), formatterGrid, false, false, 4);
    GtkWidget* formatterChecks = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(formatterChecks), 2);
    gtk_grid_set_column_spacing(GTK_GRID(formatterChecks), 8);
    formatUseTabs = addCheckGrid(GTK_GRID(formatterChecks), "Use tabs when formatting", options.formatusetabs, 0);
    formatTrimTrailing = addCheckGrid(GTK_GRID(formatterChecks), "Trim trailing whitespace when formatting", options.formattrimtrailing, 1);
    removeLineComments = addCheckGrid(GTK_GRID(formatterChecks), "Remove line comments", options.removelinecomments, 2);
    removeBlockComments = addCheckGrid(GTK_GRID(formatterChecks), "Remove block comments", options.removeblockcomments, 3);
    preserveClientside = addCheckGrid(GTK_GRID(formatterChecks), "Preserve standalone //#CLIENTSIDE", options.preserveclientside, 4);
    gtk_box_pack_start(GTK_BOX(formatter), formatterChecks, false, false, 0);
    GtkWidget* customization = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(customization), 5);
    GtkWidget* customizationGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(customizationGrid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(customizationGrid), 12);
    chatbarTextColor = addColorEntry(GTK_GRID(customizationGrid), "Chatbar text color:", options.coloredit, 0, 0);
    chatbarBackgroundColor = addColorEntry(GTK_GRID(customizationGrid), "Chatbar background color:", options.coloreditback, 1, 0);
    chatTextColor = addColorEntry(GTK_GRID(customizationGrid), "Chat text color:", options.colorchat, 2, 0);
    chatBoldColor = addColorEntry(GTK_GRID(customizationGrid), "Chat bold color:", options.colorchatbold, 3, 0);
    chatBackgroundColor = addColorEntry(GTK_GRID(customizationGrid), "Chat background color:", options.colorchatback, 4, 0);
    labelColor = addColorEntry(GTK_GRID(customizationGrid), "Label color:", options.colorlabel, 5, 0);
    labelBackgroundColor = addColorEntry(GTK_GRID(customizationGrid), "Label background color:", options.colorlabelback, 6, 0);
    serverLabel = addEntry(GTK_GRID(customizationGrid), "Server label:", options.labelservers, 0, 1);
    playersLabel = addEntry(GTK_GRID(customizationGrid), "Players label:", options.labelplayers, 1, 1);
    npcServerLabel = addEntry(GTK_GRID(customizationGrid), "NPC server label:", options.labelnpcserver, 2, 1);
    backgroundImage = addEntry(GTK_GRID(customizationGrid), "Background image:", options.background, 3, 1);
    GtkWidget* backgroundBrowse = gtk_button_new_with_label("Browse");
    gtk_grid_attach(GTK_GRID(customizationGrid), backgroundBrowse, 5, 3, 1, 1);
    backgroundTint = addColorEntry(GTK_GRID(customizationGrid), "Background image tint:", options.backgroundtint, 4, 1);
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(backgroundTint), true);
    backgroundTintSolid = gtk_check_button_new_with_label("Solid fill");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(backgroundTintSolid), options.backgroundtintsolid);
    gtk_grid_attach(GTK_GRID(customizationGrid), backgroundTintSolid, 5, 4, 1, 1);
    syncBackgroundTint = gtk_check_button_new_with_label("Sync background tint with theme");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(syncBackgroundTint), options.syncbackgroundtint);
    gtk_grid_attach(GTK_GRID(customizationGrid), syncBackgroundTint, 4, 6, 2, 1);
    g_signal_connect(backgroundTint, "color-set", G_CALLBACK(+[](GtkColorButton*, gpointer data) { static_cast<TOptionsWindow*>(data)->save(); }), this);
    theme = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "system", "System (OS theme)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "dark", "Dark");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "dracula", "Dracula");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "material", "Material");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "ayu-mirage", "Ayu Mirage");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "nord", "Nord");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "monokai", "Monokai");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "one-dark", "One Dark");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "tokyo-night", "Tokyo Night");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "gruvbox", "Gruvbox");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "solarized", "Solarized Dark");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "catppuccin", "Catppuccin Mocha");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "light", "Light");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(theme), options.theme.c_str());
    gtk_grid_attach(GTK_GRID(customizationGrid), gtk_label_new("Theme:"), 3, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(customizationGrid), theme, 4, 5, 2, 1);
    gtk_box_pack_start(GTK_BOX(customization), customizationGrid, false, false, 4);
    GtkWidget* customizationChecks = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(customizationChecks), 8);
    syncColors = addCheckGrid(GTK_GRID(customizationChecks), "Sync colors with theme", options.synccolors, 0);
    roundedCorners = addCheckGrid(GTK_GRID(customizationChecks), "Rounded app corners", options.roundedcorners, 1);
    gtk_box_pack_start(GTK_BOX(customization), customizationChecks, false, false, 0);
    GtkWidget* mcp = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(mcp), 5);
    GtkWidget* mcpChecks = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(mcpChecks), 2);
    gtk_grid_set_column_spacing(GTK_GRID(mcpChecks), 8);
    mcpEnabled = addCheckGrid(GTK_GRID(mcpChecks), "Enable local MCP integration", options.mcpenabled, 0);
    mcpRead = addCheckGrid(GTK_GRID(mcpChecks), "Allow read-only tools", options.mcpread, 1);
    mcpWrite = addCheckGrid(GTK_GRID(mcpChecks), "Allow file and script writes", options.mcpwrite, 2);
    mcpServer = addCheckGrid(GTK_GRID(mcpChecks), "Allow server and game mutations", options.mcpserver, 3);
    mcpLogin = addCheckGrid(GTK_GRID(mcpChecks), "Allow login and connection control", options.mcplogin, 4);
    mcpWindows = addCheckGrid(GTK_GRID(mcpChecks), "Allow window and UI control", options.mcpwindows, 5);
    mcpAdmin = addCheckGrid(GTK_GRID(mcpChecks), "Allow administrative actions", options.mcpadmin, 6);
    mcpFullControl = addCheckGrid(GTK_GRID(mcpChecks), "Full native RC control", options.mcpfullcontrol, 7);
    mcpApprove = addCheckGrid(GTK_GRID(mcpChecks), "Require approval for mutations", options.mcpapprove, 8);
    mcpAudit = addCheckGrid(GTK_GRID(mcpChecks), "Write MCP audit log", options.mcpaudit, 9);
    gtk_box_pack_start(GTK_BOX(mcp), mcpChecks, false, false, 0);
    GtkWidget* approvalFrame = gtk_frame_new("Saved script approvals");
    GtkWidget* approvalBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_container_set_border_width(GTK_CONTAINER(approvalBox), 5);
    GtkWidget* approvalChecks = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(approvalChecks), 2);
    gtk_grid_set_column_spacing(GTK_GRID(approvalChecks), 8);
    mcpApproveWeapon = addCheckGrid(GTK_GRID(approvalChecks), "Allow Weapon script edits and saves without prompting", options.mcpapproveweapon, 0);
    mcpApproveClass = addCheckGrid(GTK_GRID(approvalChecks), "Allow Class script edits and saves without prompting", options.mcpapproveclass, 1);
    mcpApproveNpc = addCheckGrid(GTK_GRID(approvalChecks), "Allow NPC script edits and saves without prompting", options.mcpapprovenpc, 2);
    gtk_box_pack_start(GTK_BOX(approvalBox), approvalChecks, false, false, 0);
    GtkWidget* resetMcpApprovals = gtk_button_new_with_label("Reset saved script approvals");
    gtk_box_pack_start(GTK_BOX(approvalBox), resetMcpApprovals, false, false, 2);
    gtk_container_add(GTK_CONTAINER(approvalFrame), approvalBox);
    gtk_box_pack_start(GTK_BOX(mcp), approvalFrame, false, false, 4);
    GtkWidget* mcpGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(mcpGrid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(mcpGrid), 12);
    mcpFileRoots = addEntry(GTK_GRID(mcpGrid), "Allowed file roots:", options.mcpfileroots, 0, 0);
    mcpServerScope = addEntry(GTK_GRID(mcpGrid), "Allowed server scope:", options.mcpserverscope, 0, 1);
    gtk_box_pack_start(GTK_BOX(mcp), mcpGrid, false, false, 4);
    GtkWidget* mcpLaunch = gtk_entry_new();
#ifdef _WIN32
    gtk_entry_set_text(GTK_ENTRY(mcpLaunch), "RemoteControl.exe --mcp");
#else
    gtk_entry_set_text(GTK_ENTRY(mcpLaunch), "RemoteControl --mcp");
#endif
    gtk_editable_set_editable(GTK_EDITABLE(mcpLaunch), false);
    gtk_box_pack_start(GTK_BOX(mcp), gtk_label_new("Stdio client launch command:"), false, false, 0);
    gtk_box_pack_start(GTK_BOX(mcp), mcpLaunch, false, false, 0);
    gtk_box_pack_start(GTK_BOX(mcp), gtk_label_new("Stdio is local process-to-process communication; RC does not open a TCP listener."), false, false, 4);
    GtkWidget* mcpAuditButtons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(mcpAuditButtons), GTK_BUTTONBOX_START);
    GtkWidget* viewMcpAudit = gtk_button_new_with_label("View audit log");
    GtkWidget* clearMcpAudit = gtk_button_new_with_label("Clear audit log");
    gtk_container_add(GTK_CONTAINER(mcpAuditButtons), viewMcpAudit);
    gtk_container_add(GTK_CONTAINER(mcpAuditButtons), clearMcpAudit);
    gtk_box_pack_start(GTK_BOX(mcp), mcpAuditButtons, false, false, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), general, gtk_label_new("General Options "));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), script, gtk_label_new("Script Style "));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), formatter, gtk_label_new("Formatter "));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), customization, gtk_label_new("Customization "));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), mcp, gtk_label_new("MCP"));
    g_signal_connect(notebook, "switch-page", G_CALLBACK(onPageChanged), this);
    gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);
    GtkCssProvider* tabs = gtk_css_provider_new();
    gtk_css_provider_load_from_data(tabs, "#OptionsWindow notebook > header { border-bottom: 1px solid #777777; } #OptionsWindow notebook > header > tabs > tab { border: 1px solid #777777; border-bottom: 0; border-radius: 4px 4px 0 0; margin-right: 3px; padding: 4px 8px; } #OptionsWindow notebook > header > tabs > tab:checked { border-color: #aaaaaa; border-bottom-color: transparent; margin-bottom: -1px; } #OptionsWindow notebook > stack { border: 1px solid #777777; border-top: 0; }", -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(tabs), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(tabs);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* close = gtk_button_new_with_label("Close");
    gtk_button_set_image(GTK_BUTTON(close), gtk_image_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(close), true);
    gtk_container_add(GTK_CONTAINER(buttons), close);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 5);
    g_signal_connect(close, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(theme, "changed", G_CALLBACK(onThemeChanged), this);
    g_signal_connect(syncBackgroundTint, "toggled", G_CALLBACK(+[](GtkToggleButton*, gpointer data) { static_cast<TOptionsWindow*>(data)->save(); }), this);
    g_signal_connect(syntaxTheme, "changed", G_CALLBACK(onSyntaxThemeChanged), this);
    g_signal_connect(syncSyntaxTheme, "toggled", G_CALLBACK(onSyncSyntaxThemeChanged), this);
    for (GtkWidget* field : {syntax, autoIndent, smartHomeEnd, brackets, lineNumbers, minimap, lsp, scriptDiagnostics, scriptUseTabs, modernFileBrowser, fileBrowserHoverPreview, fileBrowserThumbnails, extensionsEnabled, syncEnabled, levelListEnabled}) g_signal_connect(field, "toggled", G_CALLBACK(onLiveEditorOptionChanged), this);
    for (GtkWidget* field : {mcpApproveWeapon, mcpApproveClass, mcpApproveNpc}) g_signal_connect(field, "toggled", G_CALLBACK(onLiveEditorOptionChanged), this);
    g_signal_connect(chatFontFamily, "font-set", G_CALLBACK(onLiveEditorOptionChanged), this);
    g_signal_connect(scriptFontFamily, "font-set", G_CALLBACK(onLiveEditorOptionChanged), this);
    g_signal_connect(scriptTabWidth, "changed", G_CALLBACK(onLiveEditorOptionChanged), this);
    g_signal_connect(autocompleteSource, "focus-out-event", G_CALLBACK(onLiveEditorOptionFocusOut), this);
    g_signal_connect(downloadBrowse, "clicked", G_CALLBACK(onBrowseDownload), this);
    g_signal_connect(externalWorkspaceBrowse, "clicked", G_CALLBACK(onBrowseExternalWorkspace), this);
    g_signal_connect(externalEditorBrowse, "clicked", G_CALLBACK(onBrowseExternalEditor), this);
    g_signal_connect(logBrowse, "clicked", G_CALLBACK(onBrowseLog), this);
    g_signal_connect(autocompleteBrowse, "clicked", G_CALLBACK(onBrowseAutocompleteSource), this);
    g_signal_connect(backgroundBrowse, "clicked", G_CALLBACK(onBrowseBackground), this);
    g_signal_connect(viewMcpAudit, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        TOptionsWindow* optionsWindow = static_cast<TOptionsWindow*>(data);
        GtkWidget* dialog = gtk_dialog_new_with_buttons("MCP Audit Log", nullptr, GTK_DIALOG_MODAL, "Close", GTK_RESPONSE_CLOSE, nullptr);
        gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
        gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 420);
        GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
        GtkWidget* view = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(view), false);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), true);
        gtk_container_add(GTK_CONTAINER(scrolled), view);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scrolled, true, true, 5);
        std::ifstream input(RC::rcOptionsDirectory(optionsWindow->applicationDirectory) / "mcp-audit.log", std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (contents.empty()) contents = "No MCP requests have been logged.";
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), contents.c_str(), static_cast<gint>(contents.size()));
        gtk_widget_show_all(dialog);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }), this);
    g_signal_connect(clearMcpAudit, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        TOptionsWindow* optionsWindow = static_cast<TOptionsWindow*>(data);
        std::ofstream(RC::rcOptionsDirectory(optionsWindow->applicationDirectory) / "mcp-audit.log", std::ios::binary | std::ios::trunc);
    }), this);
    g_signal_connect(resetMcpApprovals, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        TOptionsWindow* optionsWindow = static_cast<TOptionsWindow*>(data);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(optionsWindow->mcpApproveWeapon), false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(optionsWindow->mcpApproveClass), false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(optionsWindow->mcpApproveNpc), false);
        optionsWindow->save();
    }), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}
TOptionsWindow::~TOptionsWindow() { if (window != nullptr) gtk_widget_destroy(window); }
void TOptionsWindow::setServerName(const std::string& server) { gtk_window_set_title(GTK_WINDOW(window), server.empty() ? "Options" : ("Options - " + server).c_str()); }
void TOptionsWindow::open() {
    RC::RCOptions persisted = options;
    RC::loadRCOptions(persisted, applicationDirectory);
    options.mcpapproveweapon = persisted.mcpapproveweapon; options.mcpapproveclass = persisted.mcpapproveclass; options.mcpapprovenpc = persisted.mcpapprovenpc;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mcpApproveWeapon), options.mcpapproveweapon);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mcpApproveClass), options.mcpapproveclass);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mcpApproveNpc), options.mcpapprovenpc);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}
void TOptionsWindow::onClose(GtkButton*, gpointer data) { TOptionsWindow* window = static_cast<TOptionsWindow*>(data); window->save(); gtk_widget_hide(window->window); }
void TOptionsWindow::onPageChanged(GtkNotebook*, GtkWidget*, guint, gpointer data) {
    TOptionsWindow* optionsWindow = static_cast<TOptionsWindow*>(data);
}
void TOptionsWindow::onThemeChanged(GtkComboBox*, gpointer data) { static_cast<TOptionsWindow*>(data)->applyThemeSelection(); }
void TOptionsWindow::onSyntaxThemeChanged(GtkComboBox*, gpointer data) { static_cast<TOptionsWindow*>(data)->applySyntaxThemeSelection(); }
void TOptionsWindow::onSyncSyntaxThemeChanged(GtkToggleButton*, gpointer data) { static_cast<TOptionsWindow*>(data)->applySyntaxThemeSync(); }
void TOptionsWindow::onLiveEditorOptionChanged(GtkWidget*, gpointer data) { static_cast<TOptionsWindow*>(data)->save(); }
gboolean TOptionsWindow::onGlobalHotkeyKeyPress(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    if (event->keyval == GDK_KEY_BackSpace || event->keyval == GDK_KEY_Delete) {
        gtk_entry_set_text(GTK_ENTRY(widget), "");
        return true;
    }
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        onGlobalHotkeyConfirm(nullptr, data);
        return true;
    }
    if (event->keyval == GDK_KEY_Control_L || event->keyval == GDK_KEY_Control_R || event->keyval == GDK_KEY_Shift_L || event->keyval == GDK_KEY_Shift_R || event->keyval == GDK_KEY_Alt_L || event->keyval == GDK_KEY_Alt_R || event->keyval == GDK_KEY_Meta_L || event->keyval == GDK_KEY_Meta_R || event->keyval == GDK_KEY_Super_L || event->keyval == GDK_KEY_Super_R) return true;
    std::string text;
    if ((event->state & GDK_CONTROL_MASK) != 0) text += "Ctrl+";
    if ((event->state & GDK_MOD1_MASK) != 0) text += "Alt+";
    if ((event->state & GDK_SHIFT_MASK) != 0) text += "Shift+";
    if ((event->state & GDK_SUPER_MASK) != 0) text += "Super+";
    const gchar* key = gdk_keyval_name(event->keyval);
    if (key != nullptr) text += key;
    gtk_entry_set_text(GTK_ENTRY(widget), text.c_str());
    return true;
}
void TOptionsWindow::onGlobalHotkeyConfirm(GtkButton*, gpointer data) {
    TOptionsWindow* optionsWindow = static_cast<TOptionsWindow*>(data);
    optionsWindow->save();
    gtk_widget_grab_focus(optionsWindow->globalHotkey);
}
gboolean TOptionsWindow::onLiveEditorOptionFocusOut(GtkWidget*, GdkEventFocus*, gpointer data) { static_cast<TOptionsWindow*>(data)->save(); return false; }
void TOptionsWindow::onBrowseDownload(GtkButton*, gpointer data) {
    TOptionsWindow* optionsWindow = static_cast<TOptionsWindow*>(data);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Download folder", nullptr, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Cancel", GTK_RESPONSE_CANCEL, "Select", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), gtk_entry_get_text(GTK_ENTRY(optionsWindow->downloadFolder)));
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(optionsWindow->downloadFolder), path);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
}
void TOptionsWindow::onBrowseExternalWorkspace(GtkButton*, gpointer data) {
    TOptionsWindow* optionsWindow = static_cast<TOptionsWindow*>(data);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("External editor workspace", nullptr, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Cancel", GTK_RESPONSE_CANCEL, "Select", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), gtk_entry_get_text(GTK_ENTRY(optionsWindow->externalEditorWorkspace)));
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) { gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)); gtk_entry_set_text(GTK_ENTRY(optionsWindow->externalEditorWorkspace), path); g_free(path); }
    gtk_widget_destroy(dialog);
}
void TOptionsWindow::onBrowseExternalEditor(GtkButton*, gpointer data) {
    TOptionsWindow* optionsWindow = static_cast<TOptionsWindow*>(data);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("External editor executable", nullptr, GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Select", GTK_RESPONSE_ACCEPT, nullptr);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) { gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)); gtk_entry_set_text(GTK_ENTRY(optionsWindow->externalEditorCommand), path); g_free(path); }
    gtk_widget_destroy(dialog);
}
void TOptionsWindow::onBrowseLog(GtkButton*, gpointer data) {
    TOptionsWindow* optionsWindow = static_cast<TOptionsWindow*>(data);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("RC chat log", nullptr, GTK_FILE_CHOOSER_ACTION_SAVE, "Cancel", GTK_RESPONSE_CANCEL, "Select", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), gtk_entry_get_text(GTK_ENTRY(optionsWindow->logFile)));
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(optionsWindow->logFile), path);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
}
void TOptionsWindow::onBrowseAutocompleteSource(GtkButton*, gpointer data) {
    TOptionsWindow* optionsWindow = static_cast<TOptionsWindow*>(data);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Autocomplete source", nullptr, GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Select", GTK_RESPONSE_ACCEPT, nullptr);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(optionsWindow->autocompleteSource), path);
        g_free(path);
        optionsWindow->save();
    }
    gtk_widget_destroy(dialog);
}
void TOptionsWindow::onBrowseBackground(GtkButton*, gpointer data) {
    TOptionsWindow* optionsWindow = static_cast<TOptionsWindow*>(data);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Background image", nullptr, GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Select", GTK_RESPONSE_ACCEPT, nullptr);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(optionsWindow->backgroundImage), path);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
}
gboolean TOptionsWindow::onDelete(GtkWidget*, GdkEvent*, gpointer data) { TOptionsWindow* window = static_cast<TOptionsWindow*>(data); window->save(); gtk_widget_hide(window->window); return true; }
void TOptionsWindow::save() {
    if (saving) return;
    saving = true;
    const RC::RCOptions previous = options;
    options.nickname = gtk_entry_get_text(GTK_ENTRY(nickname)); options.downloadfolder = gtk_entry_get_text(GTK_ENTRY(downloadFolder)); options.externaleditorworkspace = gtk_entry_get_text(GTK_ENTRY(externalEditorWorkspace)); options.externaleditorcommand = gtk_entry_get_text(GTK_ENTRY(externalEditorCommand)); if (const char* externalScope = gtk_combo_box_get_active_id(GTK_COMBO_BOX(externalEditorScope))) options.externaleditorscope = externalScope; options.chatlogfile = gtk_entry_get_text(GTK_ENTRY(logFile)); readFontButton(chatFontFamily, options.chatfontfamily, options.chatfontsize);
    if (const char* selectedTheme = gtk_combo_box_get_active_id(GTK_COMBO_BOX(theme))) options.theme = selectedTheme;
    if (const char* selectedSyntaxTheme = gtk_combo_box_get_active_id(GTK_COMBO_BOX(syntaxTheme))) options.syntaxtheme = selectedSyntaxTheme;
    options.darkmode = options.theme == "system" ? options.darkmode : options.theme != "light"; options.syncsyntaxtheme = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(syncSyntaxTheme)); options.synccolors = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(syncColors)); options.roundedcorners = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(roundedCorners));
    if (options.syncsyntaxtheme) {
        options.syntaxtheme = options.theme == "dark" || options.theme == "system" ? "language-spec" : options.theme;
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(syntaxTheme), options.syntaxtheme.c_str());
    }
    options.afkenabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(afkEnabled)); const char* afkId = gtk_combo_box_get_active_id(GTK_COMBO_BOX(afkTimeout)); options.afktimeout = afkId == nullptr ? 15 : std::clamp(std::atoi(afkId), 1, 1440);
    options.globalhotkey = gtk_entry_get_text(GTK_ENTRY(globalHotkey));
    options.nomassmessages = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ignoreMass)); options.nomassifclienton = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ignoreMassClient)); options.attachaway = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(attachAway)); options.globalpms = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(globalPMs)); options.buddytracking = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buddies)); options.separatenc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(separateNC)); options.rctimestamps = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(timestamps)); options.newpmalerts = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pmAlerts)); options.notificationsounds = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(notificationSounds)); options.logrcchat = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(logChat)); options.separatefindresults = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(separateFindResults)); options.modernfilebrowser = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(modernFileBrowser)); options.filebrowserhoverpreview = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fileBrowserHoverPreview)); options.filebrowserthumbnails = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fileBrowserThumbnails)); options.extensionsenabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(extensionsEnabled)); options.syncenabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(syncEnabled)); options.levellistenabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(levelListEnabled)); options.usenewbantype = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(useNewBanType)); options.autoreconnectnc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(autoReconnectNC)); options.syntaxhighlighting = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(syntax)); options.autoindenting = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(autoIndent)); options.smarthomeend = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(smartHomeEnd)); options.showbrackets = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(brackets)); options.showlinenumbers = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lineNumbers)); options.minimap = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(minimap)); options.lsp = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lsp)); options.scriptdiagnostics = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scriptDiagnostics)); options.scripttabwidth = std::clamp(std::atoi(gtk_entry_get_text(GTK_ENTRY(scriptTabWidth))), 1, 1000); options.scriptusetabs = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scriptUseTabs)); readFontButton(scriptFontFamily, options.scriptfontfamily, options.scriptfontsize); options.autocompletesource = gtk_entry_get_text(GTK_ENTRY(autocompleteSource));
    options.formatindentwidth = std::clamp(std::atoi(gtk_entry_get_text(GTK_ENTRY(formatIndentWidth))), 1, 16); options.formatusetabs = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(formatUseTabs)); options.formattrimtrailing = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(formatTrimTrailing)); options.removelinecomments = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(removeLineComments)); options.removeblockcomments = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(removeBlockComments)); options.preserveclientside = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(preserveClientside));
    options.coloredit = colorValue(chatbarTextColor); options.coloreditback = colorValue(chatbarBackgroundColor); options.colorchat = colorValue(chatTextColor); options.colorchatbold = colorValue(chatBoldColor); options.colorchatback = colorValue(chatBackgroundColor); options.colorlabel = colorValue(labelColor); options.colorlabelback = colorValue(labelBackgroundColor); options.labelservers = gtk_entry_get_text(GTK_ENTRY(serverLabel)); options.labelplayers = gtk_entry_get_text(GTK_ENTRY(playersLabel)); options.labelnpcserver = gtk_entry_get_text(GTK_ENTRY(npcServerLabel)); options.background = gtk_entry_get_text(GTK_ENTRY(backgroundImage)); options.syncbackgroundtint = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(syncBackgroundTint)); options.backgroundtint = options.syncbackgroundtint ? tintForTheme(options.theme, options) : colorValueWithAlpha(backgroundTint); options.backgroundtintsolid = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(backgroundTintSolid));
    options.mcpenabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcpEnabled)); options.mcpread = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcpRead)); options.mcpwrite = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcpWrite)); options.mcpserver = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcpServer)); options.mcplogin = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcpLogin)); options.mcpwindows = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcpWindows)); options.mcpadmin = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcpAdmin)); options.mcpfullcontrol = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcpFullControl)); options.mcpapprove = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcpApprove)); options.mcpaudit = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcpAudit)); options.mcpapproveweapon = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcpApproveWeapon)); options.mcpapproveclass = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcpApproveClass)); options.mcpapprovenpc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcpApproveNpc)); options.mcpfileroots = gtk_entry_get_text(GTK_ENTRY(mcpFileRoots)); options.mcpserverscope = gtk_entry_get_text(GTK_ENTRY(mcpServerScope));
    setGScriptEditorOptions(options);
    applyRemoteControlTheme(options.theme, options.darkmode, options.roundedcorners);
    setRemoteControlSyntaxTheme(options.syntaxtheme);
    refreshGScriptEditorTheme();
    RC::saveRCOptions(options, applicationDirectory);
    refreshGlobalVisibilityHotkey(options.globalhotkey);
    onSaved(previous);
    saving = false;
}

void TOptionsWindow::applySyntaxThemeSelection() {
    const char* selected = gtk_combo_box_get_active_id(GTK_COMBO_BOX(syntaxTheme));
    if (selected == nullptr || options.syntaxtheme == selected) return;
    options.syntaxtheme = selected;
    setRemoteControlSyntaxTheme(options.syntaxtheme);
    refreshGScriptEditorTheme();
    RC::saveRCOptions(options, applicationDirectory);
}
void TOptionsWindow::applySyntaxThemeSync() {
    options.syncsyntaxtheme = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(syncSyntaxTheme));
    gtk_widget_set_sensitive(syntaxTheme, !options.syncsyntaxtheme);
    if (options.syncsyntaxtheme) {
        options.syntaxtheme = options.theme == "dark" || options.theme == "system" ? "language-spec" : options.theme;
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(syntaxTheme), options.syntaxtheme.c_str());
        setRemoteControlSyntaxTheme(options.syntaxtheme);
        refreshGScriptEditorTheme();
    }
    RC::saveRCOptions(options, applicationDirectory);
}
void TOptionsWindow::applyThemeSelection() {
    const char* selectedTheme = gtk_combo_box_get_active_id(GTK_COMBO_BOX(theme));
    if (selectedTheme == nullptr || options.theme == selectedTheme) return;
    const RC::RCOptions previous = options;
    options.theme = selectedTheme;
    options.darkmode = options.theme == "system" ? options.darkmode : options.theme != "light";
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(syncSyntaxTheme))) {
        options.syntaxtheme = options.theme == "dark" || options.theme == "system" ? "language-spec" : options.theme;
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(syntaxTheme), options.syntaxtheme.c_str());
        setRemoteControlSyntaxTheme(options.syntaxtheme);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(syncColors))) {
        themeColors(options.theme, options);
        GdkRGBA color{};
        const std::string values[] = {options.coloredit, options.coloreditback, options.colorchat, options.colorchatbold, options.colorchatback, options.colorlabel, options.colorlabelback};
        GtkWidget* fields[] = {chatbarTextColor, chatbarBackgroundColor, chatTextColor, chatBoldColor, chatBackgroundColor, labelColor, labelBackgroundColor};
        for (int index = 0; index < 7; ++index) if (gdk_rgba_parse(&color, values[index].c_str())) gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(fields[index]), &color);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(syncBackgroundTint))) {
        options.backgroundtint = tintForTheme(options.theme, options);
        GdkRGBA tint{};
        if (gdk_rgba_parse(&tint, options.backgroundtint.c_str())) gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(backgroundTint), &tint);
    }
    setGScriptEditorOptions(options);
    applyRemoteControlTheme(options.theme, options.darkmode, options.roundedcorners);
    refreshGScriptEditorTheme();
    RC::saveRCOptions(options, applicationDirectory);
    onSaved(previous);
}
