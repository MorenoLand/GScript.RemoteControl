#include "TOptionsWindow.h"
#include "GScriptEditor.h"

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

TOptionsWindow::TOptionsWindow(RC::RCOptions& nextOptions, const std::filesystem::path& nextApplicationDirectory, std::function<void(const RC::RCOptions&)> nextOnSaved) : options(nextOptions), applicationDirectory(nextApplicationDirectory), onSaved(std::move(nextOnSaved)) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "OptionsWindow");
    gtk_window_set_title(GTK_WINDOW(window), "Options");
    gtk_window_set_default_size(GTK_WINDOW(window), 380, 400);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* notebook = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(notebook), 5);
    GtkWidget* general = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(general), 5);
    ignoreMass = addCheck(GTK_BOX(general), "Ignore Mass PMs", options.nomassmessages);
    ignoreMassClient = addCheck(GTK_BOX(general), "Ignore Mass PMs if client is on", options.nomassifclienton);
    attachAway = addCheck(GTK_BOX(general), "Go in away mode when closing playerlist", options.attachaway);
    globalPMs = addCheck(GTK_BOX(general), "Allow Global PMs", options.globalpms);
    buddies = addCheck(GTK_BOX(general), "Show yourself on other buddy lists", options.buddytracking);
    separateNC = addCheck(GTK_BOX(general), "Separate NC from RC Chat", options.separatenc);
    timestamps = addCheck(GTK_BOX(general), "Timestamp RC Messages", options.rctimestamps);
    pmAlerts = addCheck(GTK_BOX(general), "Show new PM Alerts in RC Chat", options.newpmalerts);
    GtkWidget* generalGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(generalGrid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(generalGrid), 5);
    nickname = addEntry(GTK_GRID(generalGrid), "Nickname:", options.nickname, 0);
    downloadFolder = addEntry(GTK_GRID(generalGrid), "Downloadfolder:", options.downloadfolder, 1);
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
    chatFontSize = addEntry(GTK_GRID(generalGrid), "Chat font size:", std::to_string(options.chatfontsize), 3);
    gtk_box_pack_start(GTK_BOX(general), generalGrid, false, false, 4);
    GtkWidget* script = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(script), 5);
    syntax = addCheck(GTK_BOX(script), "Syntax highlighting", options.syntaxhighlighting);
    autoIndent = addCheck(GTK_BOX(script), "Auto indenting", options.autoindenting);
    smartHomeEnd = addCheck(GTK_BOX(script), "Smart Home/End", options.smarthomeend);
    brackets = addCheck(GTK_BOX(script), "Show brackets", options.showbrackets);
    lineNumbers = addCheck(GTK_BOX(script), "Show line numbers", options.showlinenumbers);
    lsp = addCheck(GTK_BOX(script), "LSP / autocomplete", options.lsp);
    GtkWidget* scriptGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(scriptGrid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(scriptGrid), 5);
    scriptTabWidth = addEntry(GTK_GRID(scriptGrid), "Script tab width:", std::to_string(options.scripttabwidth), 0);
    scriptUseTabs = addCheck(GTK_BOX(script), "Use real tabs for indentation", options.scriptusetabs);
    scriptFontSize = addEntry(GTK_GRID(scriptGrid), "Script font size:", std::to_string(options.scriptfontsize), 1);
    autocompleteSource = addEntry(GTK_GRID(scriptGrid), "Autocomplete source:", options.autocompletesource, 2);
    GtkWidget* autocompleteBrowse = gtk_button_new_with_label("Browse");
    gtk_grid_attach(GTK_GRID(scriptGrid), autocompleteBrowse, 2, 2, 1, 1);
    gtk_box_pack_start(GTK_BOX(script), scriptGrid, false, false, 4);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), general, gtk_label_new("General Options "));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), script, gtk_label_new("Script Style "));
    gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);
    GtkCssProvider* tabs = gtk_css_provider_new();
    gtk_css_provider_load_from_data(tabs, "#OptionsWindow notebook > header { border-bottom: 1px solid #777777; } #OptionsWindow notebook > header > tabs > tab { border: 1px solid #777777; border-bottom: 0; border-radius: 4px 4px 0 0; margin-right: 3px; padding: 4px 8px; } #OptionsWindow notebook > header > tabs > tab:checked { border-color: #aaaaaa; margin-bottom: -1px; } #OptionsWindow notebook > stack { border: 1px solid #777777; border-top: 0; }", -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(tabs), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(tabs);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* close = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), close);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 5);
    g_signal_connect(close, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(downloadBrowse, "clicked", G_CALLBACK(onBrowseDownload), this);
    g_signal_connect(logBrowse, "clicked", G_CALLBACK(onBrowseLog), this);
    g_signal_connect(autocompleteBrowse, "clicked", G_CALLBACK(onBrowseAutocompleteSource), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}
TOptionsWindow::~TOptionsWindow() { if (window != nullptr) gtk_widget_destroy(window); }
void TOptionsWindow::open() { gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); }
void TOptionsWindow::onClose(GtkButton*, gpointer data) { TOptionsWindow* window = static_cast<TOptionsWindow*>(data); window->save(); gtk_widget_hide(window->window); }
void TOptionsWindow::onBrowseDownload(GtkButton*, gpointer data) {
    TOptionsWindow* optionsWindow = static_cast<TOptionsWindow*>(data);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Download folder", GTK_WINDOW(optionsWindow->window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Cancel", GTK_RESPONSE_CANCEL, "Select", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), gtk_entry_get_text(GTK_ENTRY(optionsWindow->downloadFolder)));
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(optionsWindow->downloadFolder), path);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
}
void TOptionsWindow::onBrowseLog(GtkButton*, gpointer data) {
    TOptionsWindow* optionsWindow = static_cast<TOptionsWindow*>(data);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("RC chat log", GTK_WINDOW(optionsWindow->window), GTK_FILE_CHOOSER_ACTION_SAVE, "Cancel", GTK_RESPONSE_CANCEL, "Select", GTK_RESPONSE_ACCEPT, nullptr);
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
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Autocomplete source", GTK_WINDOW(optionsWindow->window), GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Select", GTK_RESPONSE_ACCEPT, nullptr);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(optionsWindow->autocompleteSource), path);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
}
gboolean TOptionsWindow::onDelete(GtkWidget*, GdkEvent*, gpointer data) { TOptionsWindow* window = static_cast<TOptionsWindow*>(data); window->save(); gtk_widget_hide(window->window); return true; }
void TOptionsWindow::save() {
    const RC::RCOptions previous = options;
    options.nickname = gtk_entry_get_text(GTK_ENTRY(nickname)); options.downloadfolder = gtk_entry_get_text(GTK_ENTRY(downloadFolder)); options.chatlogfile = gtk_entry_get_text(GTK_ENTRY(logFile)); options.chatfontsize = std::clamp(std::atoi(gtk_entry_get_text(GTK_ENTRY(chatFontSize))), 1, 1000);
    options.nomassmessages = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ignoreMass)); options.nomassifclienton = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ignoreMassClient)); options.attachaway = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(attachAway)); options.globalpms = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(globalPMs)); options.buddytracking = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buddies)); options.separatenc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(separateNC)); options.rctimestamps = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(timestamps)); options.newpmalerts = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pmAlerts)); options.logrcchat = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(logChat)); options.syntaxhighlighting = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(syntax)); options.autoindenting = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(autoIndent)); options.smarthomeend = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(smartHomeEnd)); options.showbrackets = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(brackets)); options.showlinenumbers = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lineNumbers)); options.lsp = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lsp)); options.scripttabwidth = std::clamp(std::atoi(gtk_entry_get_text(GTK_ENTRY(scriptTabWidth))), 1, 1000); options.scriptusetabs = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scriptUseTabs)); options.scriptfontsize = std::clamp(std::atoi(gtk_entry_get_text(GTK_ENTRY(scriptFontSize))), 1, 1000); options.autocompletesource = gtk_entry_get_text(GTK_ENTRY(autocompleteSource));
    setGScriptEditorOptions(options);
    RC::saveRCOptions(options, applicationDirectory);
    onSaved(previous);
}
