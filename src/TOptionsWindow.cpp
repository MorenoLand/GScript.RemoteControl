#include "TOptionsWindow.h"

#include <algorithm>
#include <cstdlib>

namespace {
    GtkWidget* addCheck(GtkBox* box, const char* label, bool value) {
        GtkWidget* field = gtk_check_button_new_with_label(label);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(field), value);
        gtk_box_pack_start(box, field, false, false, 0);
        return field;
    }
    GtkWidget* addEntry(GtkGrid* grid, const char* label, const std::string& value, int row) {
        GtkWidget* field = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(field), value.c_str());
        gtk_grid_attach(grid, gtk_label_new(label), 0, row, 1, 1);
        gtk_grid_attach(grid, field, 1, row, 1, 1);
        return field;
    }
}

TOptionsWindow::TOptionsWindow(RC3::RCOptions& nextOptions, const std::filesystem::path& nextApplicationDirectory) : options(nextOptions), applicationDirectory(nextApplicationDirectory) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Options");
    gtk_window_set_default_size(GTK_WINDOW(window), 370, 400);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* notebook = gtk_notebook_new();
    GtkWidget* general = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(general), 8);
    ignoreMass = addCheck(GTK_BOX(general), "Ignore Mass PMs", options.nomassmessages);
    ignoreMassClient = addCheck(GTK_BOX(general), "Ignore Mass PMs if client is on", options.nomassifclienton);
    globalPMs = addCheck(GTK_BOX(general), "Allow Global PMs", options.globalpms);
    buddies = addCheck(GTK_BOX(general), "Show yourself on other buddy lists", options.showbuddies);
    separateNC = addCheck(GTK_BOX(general), "Separate NC from RC Chat", options.separatenc);
    timestamps = addCheck(GTK_BOX(general), "Timestamp RC Messages", options.rctimestamps);
    pmAlerts = addCheck(GTK_BOX(general), "Show new PM Alerts in RC Chat", options.newpmalerts);
    GtkWidget* generalGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(generalGrid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(generalGrid), 5);
    nickname = addEntry(GTK_GRID(generalGrid), "Nickname:", options.nickname, 0);
    downloadFolder = addEntry(GTK_GRID(generalGrid), "Downloadfolder:", options.downloadfolder, 1);
    logChat = addCheck(GTK_BOX(general), "Log RC Chat", options.logrcchat);
    logFile = addEntry(GTK_GRID(generalGrid), "Log file:", options.chatlogfile, 2);
    chatFontSize = addEntry(GTK_GRID(generalGrid), "Chat font size:", std::to_string(options.chatfontsize), 3);
    gtk_box_pack_start(GTK_BOX(general), generalGrid, false, false, 4);
    GtkWidget* script = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(script), 8);
    syntax = addCheck(GTK_BOX(script), "Syntax highlighting", options.syntaxhighlighting);
    autoIndent = addCheck(GTK_BOX(script), "Auto indenting", options.autoindenting);
    smartHomeEnd = addCheck(GTK_BOX(script), "Smart Home/End", options.smarthomeend);
    brackets = addCheck(GTK_BOX(script), "Show brackets", options.showbrackets);
    lineNumbers = addCheck(GTK_BOX(script), "Show line numbers", options.showlinenumbers);
    GtkWidget* scriptGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(scriptGrid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(scriptGrid), 5);
    scriptTabWidth = addEntry(GTK_GRID(scriptGrid), "Script tab width:", std::to_string(options.scripttabwidth), 0);
    scriptFontSize = addEntry(GTK_GRID(scriptGrid), "Script font size:", std::to_string(options.scriptfontsize), 1);
    gtk_box_pack_start(GTK_BOX(script), scriptGrid, false, false, 4);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), general, gtk_label_new("General Options"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), script, gtk_label_new("Script Style"));
    gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* close = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), close);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 5);
    g_signal_connect(close, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}
TOptionsWindow::~TOptionsWindow() { if (window != nullptr) gtk_widget_destroy(window); }
void TOptionsWindow::open() { gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); }
void TOptionsWindow::onClose(GtkButton*, gpointer data) { TOptionsWindow* window = static_cast<TOptionsWindow*>(data); window->save(); gtk_widget_hide(window->window); }
gboolean TOptionsWindow::onDelete(GtkWidget*, GdkEvent*, gpointer data) { TOptionsWindow* window = static_cast<TOptionsWindow*>(data); window->save(); gtk_widget_hide(window->window); return true; }
void TOptionsWindow::save() {
    options.nickname = gtk_entry_get_text(GTK_ENTRY(nickname)); options.downloadfolder = gtk_entry_get_text(GTK_ENTRY(downloadFolder)); options.chatlogfile = gtk_entry_get_text(GTK_ENTRY(logFile)); options.chatfontsize = std::max(1, std::atoi(gtk_entry_get_text(GTK_ENTRY(chatFontSize))));
    options.nomassmessages = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ignoreMass)); options.nomassifclienton = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ignoreMassClient)); options.globalpms = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(globalPMs)); options.showbuddies = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buddies)); options.separatenc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(separateNC)); options.rctimestamps = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(timestamps)); options.newpmalerts = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pmAlerts)); options.logrcchat = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(logChat)); options.syntaxhighlighting = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(syntax)); options.autoindenting = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(autoIndent)); options.smarthomeend = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(smartHomeEnd)); options.showbrackets = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(brackets)); options.showlinenumbers = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lineNumbers)); options.scripttabwidth = std::max(1, std::atoi(gtk_entry_get_text(GTK_ENTRY(scriptTabWidth)))); options.scriptfontsize = std::max(1, std::atoi(gtk_entry_get_text(GTK_ENTRY(scriptFontSize))));
    RC3::saveRCOptions(options, applicationDirectory);
}
