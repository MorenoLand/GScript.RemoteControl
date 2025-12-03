#include "TLocalBanWindow.h"

#include <grclib.h>

#include <sstream>

TLocalBanWindow::TLocalBanWindow() {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Edit Access");
    gtk_window_set_default_size(GTK_WINDOW(window), 430, 330);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* notebook = gtk_notebook_new();
    const char* titles[] = {"Account / World", "Account / All", "Computer / World", "Computer / All"};
    for (int index = 0; index < 4; ++index) {
        GtkWidget* page = gtk_grid_new();
        gtk_container_set_border_width(GTK_CONTAINER(page), 8);
        gtk_grid_set_row_spacing(GTK_GRID(page), 6);
        gtk_grid_set_column_spacing(GTK_GRID(page), 8);
        scopes[index].banned = gtk_check_button_new_with_label("Banned");
        scopes[index].type = gtk_combo_box_text_new();
        scopes[index].release = gtk_entry_new();
        scopes[index].reason = gtk_entry_new();
        gtk_grid_attach(GTK_GRID(page), scopes[index].banned, 0, 0, 2, 1);
        scopes[index].reset = gtk_check_button_new_with_label("Reset Ban");
        gtk_grid_attach(GTK_GRID(page), scopes[index].reset, 0, 1, 2, 1);
        gtk_grid_attach(GTK_GRID(page), scopes[index].type, 1, 2, 1, 1);
        gtk_grid_attach(GTK_GRID(page), gtk_label_new("Ban type:"), 0, 2, 1, 1);
        if ((index & 1) == 0) {
            scopes[index].world = gtk_entry_new();
            gtk_grid_attach(GTK_GRID(page), gtk_label_new("World:"), 0, 3, 1, 1);
            gtk_grid_attach(GTK_GRID(page), scopes[index].world, 1, 3, 1, 1);
        } else {
            gtk_grid_attach(GTK_GRID(page), gtk_label_new("World: all"), 0, 3, 2, 1);
        }
        gtk_grid_attach(GTK_GRID(page), gtk_label_new("Release time:"), 0, 4, 1, 1);
        gtk_grid_attach(GTK_GRID(page), scopes[index].release, 1, 4, 1, 1);
        gtk_grid_attach(GTK_GRID(page), gtk_label_new("Reason:"), 0, 5, 1, 1);
        gtk_grid_attach(GTK_GRID(page), scopes[index].reason, 1, 5, 1, 1);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, gtk_label_new(titles[index]));
    }
    gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_set_border_width(GTK_CONTAINER(buttons), 5);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* apply = gtk_button_new_with_label("Apply");
    GtkWidget* cancel = gtk_button_new_with_label("Cancel");
    gtk_container_add(GTK_CONTAINER(buttons), apply);
    gtk_container_add(GTK_CONTAINER(buttons), cancel);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 0);
    g_signal_connect(apply, "clicked", G_CALLBACK(onApply), this);
    g_signal_connect(cancel, "clicked", G_CALLBACK(onCancel), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TLocalBanWindow::~TLocalBanWindow() { if (window != nullptr) gtk_widget_destroy(window); }

void TLocalBanWindow::setBanTypes(const char* types) {
    banTypes.clear();
    std::istringstream input(types == nullptr ? "" : types);
    for (std::string type; std::getline(input, type);) if (!type.empty()) banTypes.push_back(type);
    if (banTypes.empty()) banTypes.push_back("default");
    for (Scope& scope : scopes) {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(scope.type));
        for (const std::string& type : banTypes) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scope.type), type.c_str());
        gtk_combo_box_set_active(GTK_COMBO_BOX(scope.type), 0);
    }
}

void TLocalBanWindow::open(void* nextConnection, const std::string& nextAccount, const std::string& nextComputerId, const std::string&) {
    connection = nextConnection;
    account = nextAccount;
    computerId = nextComputerId;
    if (banTypes.empty()) setBanTypes(nullptr);
    gtk_window_set_title(GTK_WINDOW(window), ("Edit Access of " + account).c_str());
    for (int index = 0; index < 4; ++index) {
        scopes[index].target = index < 2 ? account : (computerId.empty() ? "" : "pc:" + computerId);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scopes[index].banned), false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scopes[index].reset), false);
        gtk_entry_set_text(GTK_ENTRY(scopes[index].release), "");
        gtk_entry_set_text(GTK_ENTRY(scopes[index].reason), "");
        if (scopes[index].world != nullptr) gtk_entry_set_text(GTK_ENTRY(scopes[index].world), "");
    }
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

void TLocalBanWindow::onApply(GtkButton*, gpointer data) {
    TLocalBanWindow* editor = static_cast<TLocalBanWindow*>(data);
    for (int index = 0; index < 4; ++index) {
        Scope& scope = editor->scopes[index];
        if (scope.target.empty()) continue;
        const bool banned = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scope.banned));
        if (!banned && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scope.reset))) continue;
        gchar* type = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(scope.type));
        const char* world = scope.world == nullptr ? "all" : gtk_entry_get_text(GTK_ENTRY(scope.world));
        if (world[0] != '\0') rc_set_ban(editor->connection, scope.target.c_str(), world, banned, type == nullptr ? "" : type, gtk_entry_get_text(GTK_ENTRY(scope.release)), gtk_entry_get_text(GTK_ENTRY(scope.reason)));
        g_free(type);
    }
    gtk_widget_hide(editor->window);
}

void TLocalBanWindow::onCancel(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TLocalBanWindow*>(data)->window); }
gboolean TLocalBanWindow::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TLocalBanWindow*>(data)->window); return true; }
