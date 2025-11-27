#include "TNPCList.h"

#include <grclib.h>

#include <set>
#include <string>

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
    tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
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
    g_signal_connect(tree, "button-press-event", G_CALLBACK(onTreeButton), this);
    gtk_container_add(GTK_CONTAINER(frame), scrolled);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    gtk_container_set_border_width(GTK_CONTAINER(buttons), 5);
    GtkWidget* refresh = gtk_button_new_with_label("Refresh");
    GtkWidget* add = gtk_button_new_with_label("Add");
    GtkWidget* close = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), refresh);
    gtk_container_add(GTK_CONTAINER(buttons), add);
    gtk_container_add(GTK_CONTAINER(buttons), close);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 0);
    g_signal_connect(refresh, "clicked", G_CALLBACK(onRefresh), this);
    g_signal_connect(add, "clicked", G_CALLBACK(onAdd), this);
    g_signal_connect(close, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}
TNPCList::~TNPCList() { if (window != nullptr) gtk_widget_destroy(window); }
void TNPCList::open(void* nextConnection) { connection = nextConnection; rc_on_npc_added(connection, onNPCChanged, this); rc_on_npc_deleted(connection, [](int, void* data) { static_cast<TNPCList*>(data)->refresh(); }, this); refresh(); gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); }
void TNPCList::onRefresh(GtkButton*, gpointer data) { static_cast<TNPCList*>(data)->refresh(); }
void TNPCList::onAdd(GtkButton*, gpointer data) {
    TNPCList* list = static_cast<TNPCList*>(data);
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Add NPC", GTK_WINDOW(list->window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Send", GTK_RESPONSE_OK, nullptr);
    GtkWidget* grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    const std::string nextId = std::to_string(list->firstFreeNPCId());
    const struct { const char* label; const char* key; const char* value; } fields[] = {{"Name:", "name", ""}, {"ID:", "id", nextId.c_str()}, {"Type:", "type", ""}, {"Scripter:", "scripter", ""}, {"Level:", "level", ""}, {"X:", "x", "0"}, {"Y:", "y", "0"}};
    for (int index = 0; index < static_cast<int>(G_N_ELEMENTS(fields)); ++index) {
        GtkWidget* label = gtk_label_new(fields[index].label);
        GtkWidget* entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), fields[index].value);
        gtk_widget_set_hexpand(entry, true);
        gtk_grid_attach(GTK_GRID(grid), label, 0, index, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), entry, 1, index, 1, 1);
        g_object_set_data(G_OBJECT(dialog), fields[index].key, entry);
    }
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    g_signal_connect(dialog, "response", G_CALLBACK(onAddResponse), list);
    gtk_widget_show_all(dialog);
}
void TNPCList::onAddResponse(GtkDialog* dialog, gint response, gpointer data) {
    TNPCList* list = static_cast<TNPCList*>(data);
    if (response == GTK_RESPONSE_OK) {
        const auto value = [dialog](const char* key) { return gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), key))); };
        rc_create_npc_on_server(list->connection, value("name"), std::atoi(value("id")), value("type"), value("scripter"), value("level"), value("x"), value("y"));
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}
gboolean TNPCList::onTreeButton(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    if (event->type != GDK_BUTTON_PRESS || event->button != 3) return false;
    TNPCList* list = static_cast<TNPCList*>(data);
    GtkTreePath* path = nullptr;
    if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), static_cast<gint>(event->x), static_cast<gint>(event->y), &path, nullptr, nullptr, nullptr)) return false;
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
    gtk_tree_selection_select_path(selection, path);
    GtkTreeIter row;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(list->store), &row, path)) gtk_tree_model_get(GTK_TREE_MODEL(list->store), &row, 0, &list->selectedNPCId, -1);
    gtk_tree_path_free(path);
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* reset = gtk_menu_item_new_with_label("Reset");
    GtkWidget* remove = gtk_menu_item_new_with_label("Delete");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), reset);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), remove);
    g_signal_connect(reset, "activate", G_CALLBACK(onReset), list);
    g_signal_connect(remove, "activate", G_CALLBACK(onDeleteNPC), list);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
    return true;
}
void TNPCList::onReset(GtkMenuItem*, gpointer data) {
    TNPCList* list = static_cast<TNPCList*>(data);
    if (list->selectedNPCId >= 0) rc_reset_npc(list->connection, list->selectedNPCId);
}
void TNPCList::onDeleteNPC(GtkMenuItem*, gpointer data) {
    TNPCList* list = static_cast<TNPCList*>(data);
    if (list->selectedNPCId < 0) return;
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(list->window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL, "Delete NPC %d?", list->selectedNPCId);
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response == GTK_RESPONSE_OK) rc_delete_npc(list->connection, list->selectedNPCId);
}
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
int TNPCList::firstFreeNPCId() const {
    RCNPC* npcs = nullptr;
    const int count = rc_get_npcs(connection, &npcs);
    std::set<int> ids;
    for (int index = 0; index < count; ++index) ids.insert(npcs[index].id);
    int id = 1000;
    while (ids.find(id) != ids.end()) ++id;
    return id;
}
