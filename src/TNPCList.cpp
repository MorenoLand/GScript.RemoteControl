#include "TNPCList.h"

#include <grclib.h>

TNPCList::TNPCList() {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "NPCs");
    gtk_window_set_default_size(GTK_WINDOW(window), 520, 360);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* frame = gtk_frame_new(" NPCs ");
    gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
    gtk_box_pack_start(GTK_BOX(root), frame, true, true, 0);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
    store = gtk_list_store_new(4, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    const struct { const char* title; int column; int width; } columns[] = {{"ID", 0, 60}, {"Name", 1, 180}, {"Type", 2, 120}, {"Image", 3, 120}};
    for (const auto& column : columns) {
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* viewColumn = gtk_tree_view_column_new_with_attributes(column.title, renderer, "text", column.column, nullptr);
        gtk_tree_view_column_set_sort_column_id(viewColumn, column.column);
        gtk_tree_view_column_set_resizable(viewColumn, true);
        gtk_tree_view_column_set_fixed_width(viewColumn, column.width);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), viewColumn);
    }
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), 0, GTK_SORT_ASCENDING);
    gtk_container_add(GTK_CONTAINER(scrolled), tree);
    gtk_container_add(GTK_CONTAINER(frame), scrolled);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    gtk_container_set_border_width(GTK_CONTAINER(buttons), 5);
    GtkWidget* refresh = gtk_button_new_with_label("Refresh");
    GtkWidget* close = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), refresh);
    gtk_container_add(GTK_CONTAINER(buttons), close);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 0);
    g_signal_connect(refresh, "clicked", G_CALLBACK(onRefresh), this);
    g_signal_connect(close, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}
TNPCList::~TNPCList() { if (window != nullptr) gtk_widget_destroy(window); }
void TNPCList::open(void* nextConnection) { connection = nextConnection; rc_on_npc_added(connection, onNPCChanged, this); rc_on_npc_deleted(connection, [](int, void* data) { static_cast<TNPCList*>(data)->refresh(); }, this); refresh(); gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); }
void TNPCList::onRefresh(GtkButton*, gpointer data) { static_cast<TNPCList*>(data)->refresh(); }
void TNPCList::onClose(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TNPCList*>(data)->window); }
void TNPCList::onNPCChanged(int, const char*, void* data) { static_cast<TNPCList*>(data)->refresh(); }
gboolean TNPCList::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TNPCList*>(data)->window); return true; }
void TNPCList::refresh() {
    gtk_list_store_clear(store);
    RCNPC* npcs = nullptr;
    const int count = rc_get_npcs(connection, &npcs);
    for (int index = 0; index < count; ++index) {
        GtkTreeIter row;
        gtk_list_store_append(store, &row);
        gtk_list_store_set(store, &row, 0, npcs[index].id, 1, npcs[index].name == nullptr ? "" : npcs[index].name, 2, npcs[index].type == nullptr ? "" : npcs[index].type, 3, npcs[index].image == nullptr ? "" : npcs[index].image, -1);
    }
}
