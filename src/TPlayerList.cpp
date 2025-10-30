#include "TPlayerList.h"

#include <grclib.h>

TPlayerList::TPlayerList() {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "PlayerList");
    gtk_window_set_title(GTK_WINDOW(window), "Players");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 350);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* notebook = gtk_notebook_new();
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    GtkCellRenderer* accountRenderer = gtk_cell_renderer_text_new();
    GtkCellRenderer* levelRenderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* accountColumn = gtk_tree_view_column_new_with_attributes("Account", accountRenderer, "text", 0, nullptr);
    GtkTreeViewColumn* levelColumn = gtk_tree_view_column_new_with_attributes("Level", levelRenderer, "text", 1, nullptr);
    gtk_tree_view_column_set_sort_column_id(accountColumn, 0);
    gtk_tree_view_column_set_sort_column_id(levelColumn, 1);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), accountColumn);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), levelColumn);
    gtk_container_add(GTK_CONTAINER(scrolled), tree);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new("This server"));
    gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* refreshButton = gtk_button_new_with_label("Refresh");
    GtkWidget* closeButton = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), refreshButton);
    gtk_container_add(GTK_CONTAINER(buttons), closeButton);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 5);
    g_signal_connect(refreshButton, "clicked", G_CALLBACK(onRefresh), this);
    g_signal_connect(closeButton, "clicked", G_CALLBACK(gtk_widget_hide), window);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TPlayerList::~TPlayerList() { if (window != nullptr) gtk_widget_destroy(window); }
void TPlayerList::open(void* nextConnection) { connection = nextConnection; refresh(); gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); }
void TPlayerList::onRefresh(GtkButton*, gpointer data) { static_cast<TPlayerList*>(data)->refresh(); }
gboolean TPlayerList::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TPlayerList*>(data)->window); return true; }
void TPlayerList::refresh() {
    gtk_list_store_clear(store);
    RCPlayer* players = nullptr;
    const int count = rc_get_players(connection, &players);
    for (int index = 0; index < count; ++index) {
        GtkTreeIter row;
        gtk_list_store_append(store, &row);
        gtk_list_store_set(store, &row, 0, players[index].account == nullptr ? "" : players[index].account, 1, players[index].level == nullptr ? "" : players[index].level, -1);
    }
}
