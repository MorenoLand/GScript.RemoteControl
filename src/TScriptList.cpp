#include "TScriptList.h"

#include <grclib.h>
#include <gtksourceview/gtksource.h>

TScriptList::TScriptList(std::string nextType) : type(std::move(nextType)) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), (type == "classes" ? "Classes" : "Weapons"));
    gtk_window_set_default_size(GTK_WINDOW(window), 540, 460);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* frame = gtk_frame_new(" Scripts ");
    gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
    gtk_box_pack_start(GTK_BOX(root), frame, true, true, 0);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_set_border_width(GTK_CONTAINER(scrolled), 5);
    gtk_container_add(GTK_CONTAINER(frame), scrolled);
    store = gtk_list_store_new(1, G_TYPE_STRING);
    tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), false);
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column_new_with_attributes("Name", renderer, "text", 0, nullptr));
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), 0, GTK_SORT_ASCENDING);
    gtk_container_add(GTK_CONTAINER(scrolled), tree);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* editButton = gtk_button_new_with_label("Edit");
    GtkWidget* closeButton = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), editButton);
    gtk_container_add(GTK_CONTAINER(buttons), closeButton);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 5);
    g_signal_connect(editButton, "clicked", G_CALLBACK(onEdit), this);
    g_signal_connect(closeButton, "clicked", G_CALLBACK(gtk_widget_hide), window);
    g_signal_connect(tree, "row-activated", G_CALLBACK(onTreeActivated), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TScriptList::~TScriptList() { if (window != nullptr) gtk_widget_destroy(window); }

void TScriptList::open(void* nextConnection) {
    connection = nextConnection;
    rc_on_script_received(connection, onScript, this);
    refresh();
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

void TScriptList::onEdit(GtkButton*, gpointer data) { static_cast<TScriptList*>(data)->edit(); }
void TScriptList::onTreeActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data) { static_cast<TScriptList*>(data)->edit(); }
gboolean TScriptList::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TScriptList*>(data)->window); return true; }

void TScriptList::refresh() {
    gtk_list_store_clear(store);
    if (type == "weapons") {
        RCWeapon* entries = nullptr;
        const int count = rc_get_weapons(connection, &entries);
        for (int index = 0; index < count; ++index) { GtkTreeIter row; gtk_list_store_append(store, &row); gtk_list_store_set(store, &row, 0, entries[index].name == nullptr ? "" : entries[index].name, -1); }
    } else {
        RCClass* entries = nullptr;
        const int count = rc_get_classes(connection, &entries);
        for (int index = 0; index < count; ++index) { GtkTreeIter row; gtk_list_store_append(store, &row); gtk_list_store_set(store, &row, 0, entries[index].name == nullptr ? "" : entries[index].name, -1); }
    }
}

void TScriptList::edit() {
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) return;
    gchar* name = nullptr;
    gtk_tree_model_get(model, &row, 0, &name, -1);
    if (name == nullptr) return;
    if (type == "weapons") rc_request_weapon_script(connection, name);
    else rc_request_class_script(connection, name);
    g_free(name);
}

void TScriptList::onScript(const char* scriptType, const char* name, int, const char* script, void* data) {
    TScriptList* list = static_cast<TScriptList*>(data);
    if ((list->type == "weapons" && std::string(scriptType) != "weapon") || (list->type == "classes" && std::string(scriptType) != "class")) return;
    list->showEditor(name == nullptr ? "" : name, script == nullptr ? "" : script);
}

void TScriptList::showEditor(const char* name, const char* script) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(name, GTK_WINDOW(window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 520);
    GtkSourceLanguage* language = gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(), "graal");
    GtkSourceBuffer* sourceBuffer = gtk_source_buffer_new_with_language(language);
    GtkSourceStyleScheme* scheme = gtk_source_style_scheme_manager_get_scheme(gtk_source_style_scheme_manager_get_default(), "graalcolors");
    gtk_source_buffer_set_style_scheme(sourceBuffer, scheme);
    GtkWidget* editor = gtk_source_view_new_with_buffer(sourceBuffer);
    g_object_unref(sourceBuffer);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(editor), true);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(sourceBuffer), script, -1);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scrolled), editor);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scrolled, true, true, 5);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkTextIter start, end;
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar* updated = gtk_text_buffer_get_text(buffer, &start, &end, false);
        if (type == "weapons") rc_update_weapon(connection, name, "", updated);
        else rc_update_class(connection, name, updated);
        g_free(updated);
    }
    gtk_widget_destroy(dialog);
}
