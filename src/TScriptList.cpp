#include "TScriptList.h"
#include "Backup.h"
#include "EditorFind.h"
#include "GScriptEditor.h"

#include <grclib.h>
#include <gtksourceview/gtksource.h>
#include <utility>

namespace {
    TScriptList* classList = nullptr;
    TScriptList* weaponList = nullptr;
}

TScriptList::TScriptList(std::string nextType) : type(std::move(nextType)) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), (type == "classes" ? "Classes" : "Weapon/GUI-Script List"));
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
    GtkWidget* addButton = type == "weapons" ? gtk_button_new_with_label("Add") : nullptr;
    GtkWidget* deleteButton = gtk_button_new_with_label("Delete");
    GtkWidget* closeButton = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), editButton);
    if (addButton != nullptr) gtk_container_add(GTK_CONTAINER(buttons), addButton);
    gtk_container_add(GTK_CONTAINER(buttons), deleteButton);
    gtk_container_add(GTK_CONTAINER(buttons), closeButton);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 5);
    g_signal_connect(editButton, "clicked", G_CALLBACK(onEdit), this);
    if (addButton != nullptr) g_signal_connect(addButton, "clicked", G_CALLBACK(onAdd), this);
    g_signal_connect(deleteButton, "clicked", G_CALLBACK(onDeleteScript), this);
    g_signal_connect(closeButton, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(tree, "row-activated", G_CALLBACK(onTreeActivated), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TScriptList::~TScriptList() { if (classList == this) classList = nullptr; if (weaponList == this) weaponList = nullptr; if (window != nullptr) gtk_widget_destroy(window); if (store != nullptr) g_object_unref(store); }

void TScriptList::open(void* nextConnection) {
    connection = nextConnection;
    if (type == "classes") classList = this;
    else weaponList = this;
    restoreScriptReceiver(connection);
    refresh();
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}
void TScriptList::restoreScriptReceiver(void* connection) { rc_on_script_received(connection, onScript, nullptr); }

void TScriptList::onEdit(GtkButton*, gpointer data) { static_cast<TScriptList*>(data)->edit(); }
void TScriptList::onDeleteScript(GtkButton*, gpointer data) { static_cast<TScriptList*>(data)->deleteSelected(); }
void TScriptList::onAdd(GtkButton*, gpointer data) {
    struct AddState { TScriptList* list; GtkWidget* name; GtkWidget* image; };
    TScriptList* list = static_cast<TScriptList*>(data);
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Add Weapon/GUI Script", GTK_WINDOW(list->window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Add", GTK_RESPONSE_ACCEPT, nullptr);
    GtkWidget* grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    GtkWidget* name = gtk_entry_new();
    GtkWidget* image = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Name:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Image:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), image, 1, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid, true, true, 0);
    auto* state = new AddState{list, name, image};
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* addDialog, gint response, gpointer userData) {
        auto* values = static_cast<AddState*>(userData);
        if (response == GTK_RESPONSE_ACCEPT) {
            const char* name = gtk_entry_get_text(GTK_ENTRY(values->name));
            if (name != nullptr && *name != '\0') rc_add_weapon(values->list->connection, name, gtk_entry_get_text(GTK_ENTRY(values->image)), "");
        }
        gtk_widget_destroy(GTK_WIDGET(addDialog));
    }), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer userData) { delete static_cast<AddState*>(userData); }), state);
    gtk_widget_show_all(dialog);
}
void TScriptList::onClose(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TScriptList*>(data)->window); }
void TScriptList::onTreeActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data) { static_cast<TScriptList*>(data)->edit(); }
gboolean TScriptList::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TScriptList*>(data)->window); return true; }

void TScriptList::refresh() {
    gtk_list_store_clear(store);
    if (type == "weapons") {
        RCWeapon* entries = nullptr;
        const int count = rc_get_weapons(connection, &entries);
        for (int index = 0; index < count; ++index) if (entries[index].name != nullptr && entries[index].name[0] != '\0') { GtkTreeIter row; gtk_list_store_append(store, &row); gtk_list_store_set(store, &row, 0, entries[index].name, -1); }
    } else {
        RCClass* entries = nullptr;
        const int count = rc_get_classes(connection, &entries);
        for (int index = 0; index < count; ++index) if (entries[index].name != nullptr && entries[index].name[0] != '\0') { GtkTreeIter row; gtk_list_store_append(store, &row); gtk_list_store_set(store, &row, 0, entries[index].name, -1); }
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

void TScriptList::deleteSelected() {
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) return;
    gchar* name = nullptr;
    gtk_tree_model_get(model, &row, 0, &name, -1);
    if (name == nullptr || *name == '\0') { g_free(name); return; }
    const std::string noun = type == "weapons" ? "Weapon/GUI Script" : "Class";
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL, "Delete %s %s?", noun.c_str(), name);
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response == GTK_RESPONSE_OK) {
        if (type == "weapons") rc_delete_weapon(connection, name);
        else rc_delete_class(connection, name);
    }
    g_free(name);
}

void TScriptList::onScript(const char* scriptType, const char* name, int, const char* script, void* data) {
    TScriptList* list = scriptType != nullptr && std::string(scriptType) == "weapon" ? weaponList : classList;
    if (list == nullptr) return;
    list->showEditor(name == nullptr ? "" : name, script == nullptr ? "" : script);
}

void TScriptList::showEditor(const char* name, const char* script) {
    struct EditorState { void* connection; bool weapon; std::string name; GtkWidget* editor; };
    GtkWidget* dialog = gtk_dialog_new_with_buttons(name, GTK_WINDOW(window), static_cast<GtkDialogFlags>(0), "Apply", GTK_RESPONSE_ACCEPT, "Close", GTK_RESPONSE_CANCEL, nullptr);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), false);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), nullptr);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_NORMAL);
    gtk_window_set_resizable(GTK_WINDOW(dialog), true);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 520);
    GtkSourceLanguage* language = gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(), "graal");
    GtkSourceBuffer* sourceBuffer = language != nullptr ? gtk_source_buffer_new_with_language(language) : gtk_source_buffer_new(nullptr);
    GtkSourceStyleScheme* scheme = gtk_source_style_scheme_manager_get_scheme(gtk_source_style_scheme_manager_get_default(), "graalcolors");
    if (scheme != nullptr) gtk_source_buffer_set_style_scheme(sourceBuffer, scheme);
    GtkWidget* editor = gtk_source_view_new_with_buffer(sourceBuffer);
    configureGScriptEditor(editor);
    addEditorFindButton(dialog, editor);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(editor), true);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(sourceBuffer), script, -1);
    backupEditorText(type == "weapons" ? "weapon" : "class", name, script, false);
    g_object_unref(sourceBuffer);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scrolled), editor);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 0);
    gtk_box_set_spacing(GTK_BOX(content), 0);
    gtk_widget_set_margin_top(scrolled, 0);
    gtk_box_pack_start(GTK_BOX(content), scrolled, true, true, 0);
    g_signal_connect(editor, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer dialog) {
        if ((event->state & GDK_CONTROL_MASK) != 0 && (event->keyval == GDK_KEY_s || event->keyval == GDK_KEY_S)) {
            gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
            return static_cast<gboolean>(TRUE);
        }
        return static_cast<gboolean>(FALSE);
    }), dialog);
    auto* state = new EditorState{connection, type == "weapons", name, editor};
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* responseDialog, gint response, gpointer data) {
        auto* editorState = static_cast<EditorState*>(data);
        if (response == GTK_RESPONSE_ACCEPT) {
            GtkTextBuffer* editorBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editorState->editor));
            GtkTextIter start;
            GtkTextIter end;
            gtk_text_buffer_get_bounds(editorBuffer, &start, &end);
            gchar* updated = gtk_text_buffer_get_text(editorBuffer, &start, &end, false);
            backupEditorText(editorState->weapon ? "weapon" : "class", editorState->name, updated == nullptr ? "" : updated, true);
            if (editorState->weapon) rc_update_weapon(editorState->connection, editorState->name.c_str(), "", updated);
            else rc_update_class(editorState->connection, editorState->name.c_str(), updated);
            g_free(updated);
        } else gtk_widget_destroy(GTK_WIDGET(responseDialog));
    }), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) { delete static_cast<EditorState*>(data); }), state);
    gtk_widget_show_all(dialog);
}
