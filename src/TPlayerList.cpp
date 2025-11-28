#include "TPlayerList.h"

#include <grclib.h>

#include <string>
#include <vector>

namespace {
    constexpr int PlayerIconColumn = 0;
    constexpr int PlayerNickColumn = 1;
    constexpr int PlayerAccountColumn = 2;
    constexpr int PlayerLevelColumn = 3;
    constexpr int PlayerIdColumn = 4;

    bool getMessage(GtkWindow* parent, const char* title, const char* label, std::string& message) {
        GtkWidget* dialog = gtk_dialog_new_with_buttons(title, parent, GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Send", GTK_RESPONSE_ACCEPT, nullptr);
        GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget* text = gtk_text_view_new();
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD_CHAR);
        gtk_widget_set_size_request(text, 360, 120);
        gtk_box_pack_start(GTK_BOX(content), gtk_label_new(label), false, false, 5);
        gtk_box_pack_start(GTK_BOX(content), text, true, true, 5);
        gtk_widget_show_all(content);
        const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response == GTK_RESPONSE_ACCEPT) {
            GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
            GtkTextIter start;
            GtkTextIter end;
            gtk_text_buffer_get_bounds(buffer, &start, &end);
            gchar* value = gtk_text_buffer_get_text(buffer, &start, &end, false);
            message = value == nullptr ? "" : value;
            g_free(value);
        }
        gtk_widget_destroy(dialog);
        return response == GTK_RESPONSE_ACCEPT && !message.empty();
    }
}

TPlayerList::TPlayerList() {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "PlayerList");
    gtk_window_set_title(GTK_WINDOW(window), "Players");
    gtk_window_set_default_size(GTK_WINDOW(window), 580, 420);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* notebook = gtk_notebook_new();
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    store = gtk_list_store_new(5, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), GTK_SELECTION_MULTIPLE);
    onlineIcon = gdk_pixbuf_new_from_file("images/plisticononline.png", nullptr);
    GtkCellRenderer* imageRenderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column_new_with_attributes("", imageRenderer, "pixbuf", PlayerIconColumn, nullptr));
    const struct { const char* title; int column; } columns[] = {{"Nick", PlayerNickColumn}, {"Account", PlayerAccountColumn}, {"Level", PlayerLevelColumn}, {"ID", PlayerIdColumn}};
    for (const auto& column : columns) {
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* viewColumn = gtk_tree_view_column_new_with_attributes(column.title, renderer, "text", column.column, nullptr);
        gtk_tree_view_column_set_sort_column_id(viewColumn, column.column);
        gtk_tree_view_column_set_resizable(viewColumn, true);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), viewColumn);
    }
    gtk_container_add(GTK_CONTAINER(scrolled), tree);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new("This server"));
    for (const char* title : {"Guilds", "Servers", "Channels"}) {
        GtkWidget* page = gtk_scrolled_window_new(nullptr, nullptr);
        if (std::string(title) == "Servers") {
            serverStore = gtk_list_store_new(1, G_TYPE_STRING);
            GtkWidget* serverTree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(serverStore));
            GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
            gtk_tree_view_append_column(GTK_TREE_VIEW(serverTree), gtk_tree_view_column_new_with_attributes("Server", renderer, "text", 0, nullptr));
            gtk_container_add(GTK_CONTAINER(page), serverTree);
        }
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, gtk_label_new(title));
    }
    gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);
    GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(bottom), gtk_label_new("Status:"), false, false, 5);
    GtkWidget* status = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(status), "Online");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(status), "Away");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(status), "Offline");
    gtk_combo_box_set_active(GTK_COMBO_BOX(status), 0);
    gtk_box_pack_start(GTK_BOX(bottom), status, false, false, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* refreshButton = gtk_button_new_with_label("Refresh");
    GtkWidget* massPMButton = gtk_button_new_with_label("Mass PM");
    GtkWidget* adminMessageButton = gtk_button_new_with_label("Admin Message");
    GtkWidget* closeButton = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), refreshButton);
    gtk_container_add(GTK_CONTAINER(buttons), massPMButton);
    gtk_container_add(GTK_CONTAINER(buttons), adminMessageButton);
    gtk_container_add(GTK_CONTAINER(buttons), closeButton);
    gtk_box_pack_end(GTK_BOX(bottom), buttons, true, true, 5);
    gtk_box_pack_start(GTK_BOX(root), bottom, false, false, 0);
    g_signal_connect(refreshButton, "clicked", G_CALLBACK(onRefresh), this);
    g_signal_connect(massPMButton, "clicked", G_CALLBACK(onMassPM), this);
    g_signal_connect(adminMessageButton, "clicked", G_CALLBACK(onAdminMessage), this);
    g_signal_connect(closeButton, "clicked", G_CALLBACK(gtk_widget_hide), window);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TPlayerList::~TPlayerList() { if (onlineIcon != nullptr) g_object_unref(onlineIcon); if (window != nullptr) gtk_widget_destroy(window); }
void TPlayerList::open(void* nextConnection) { connection = nextConnection; rc_on_pm_servers_updated(connection, onPMServers, this); refresh(); gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); }
void TPlayerList::onRefresh(GtkButton*, gpointer data) { static_cast<TPlayerList*>(data)->refresh(); }
void TPlayerList::onMassPM(GtkButton*, gpointer data) { static_cast<TPlayerList*>(data)->sendMassPM(); }
void TPlayerList::onAdminMessage(GtkButton*, gpointer data) { static_cast<TPlayerList*>(data)->sendAdminMessage(); }
void TPlayerList::onPMServers(int, void* data) { static_cast<TPlayerList*>(data)->refresh(); }
gboolean TPlayerList::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TPlayerList*>(data)->window); return true; }
void TPlayerList::refresh() {
    gtk_list_store_clear(store);
    RCPlayer* players = nullptr;
    const int count = rc_get_players(connection, &players);
    for (int index = 0; index < count; ++index) {
        GtkTreeIter row;
        gtk_list_store_append(store, &row);
        gtk_list_store_set(store, &row, PlayerIconColumn, onlineIcon, PlayerNickColumn, players[index].nick == nullptr ? "" : players[index].nick, PlayerAccountColumn, players[index].account == nullptr ? "" : players[index].account, PlayerLevelColumn, players[index].level == nullptr ? "" : players[index].level, PlayerIdColumn, players[index].id, -1);
    }
    rc_request_pm_server_list(connection);
    if (serverStore != nullptr) {
        gtk_list_store_clear(serverStore);
        const char** servers = nullptr;
        const int count = rc_get_pm_servers(connection, &servers);
        for (int index = 0; index < count; ++index) { GtkTreeIter row; gtk_list_store_append(serverStore, &row); gtk_list_store_set(serverStore, &row, 0, servers[index], -1); }
    }
}

std::vector<int> TPlayerList::playerIds() const {
    std::vector<int> ids;
    GtkTreeIter row;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &row);
    while (valid) {
        int id = 0;
        gtk_tree_model_get(GTK_TREE_MODEL(store), &row, PlayerIdColumn, &id, -1);
        ids.push_back(id);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &row);
    }
    return ids;
}

void TPlayerList::sendMassPM() {
    const std::vector<int> ids = playerIds();
    if (ids.empty()) return;
    std::string message;
    if (!getMessage(GTK_WINDOW(window), "Mass PM to all", "Message:", message)) return;
    rc_send_mass_pm(connection, ids.data(), static_cast<int>(ids.size()), message.c_str());
}

void TPlayerList::sendAdminMessage() {
    std::string message;
    if (!getMessage(GTK_WINDOW(window), "Admin Message to all", "Message:", message)) return;
    rc_send_admin_message_all(connection, message.c_str());
}
