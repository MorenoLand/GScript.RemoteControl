#include "TScriptList.h"
#include "TExtensions.h"
#include "TButtonIcons.h"
#include "TBackup.h"
#include "TEditorFind.h"
#include "TGScriptEditor.h"
#include "TScriptEditorTracking.h"
#include "TTheme.h"
#include "TTreeSearch.h"

#include <grclib.h>
#include <gtksourceview/gtksource.h>
#include <utility>

namespace {
    TScriptList* classList = nullptr;
    TScriptList* weaponList = nullptr;

    struct EditorExtensionActionState {
        TExtensionsManager* manager;
        RC::ExtensionWindowActionBinding action;
        GtkWidget* editor;
        std::string windowId;
        std::string kind;
        std::string scriptType;
        std::string scriptName;
        std::string title;
    };

    void onEditorExtensionAction(GtkButton*, gpointer data) {
        auto* state = static_cast<EditorExtensionActionState*>(data);
        if (state == nullptr || state->manager == nullptr || state->editor == nullptr) return;
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor));
        GtkTextIter start;
        GtkTextIter end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, false);
        GtkTextIter selectionStart;
        GtkTextIter selectionEnd;
        std::string selection;
        if (gtk_text_buffer_get_selection_bounds(buffer, &selectionStart, &selectionEnd)) {
            gchar* selected = gtk_text_buffer_get_text(buffer, &selectionStart, &selectionEnd, false);
            if (selected != nullptr) { selection = selected; g_free(selected); }
        }
        RC::ExtensionWindowContext context;
        context.windowId = state->windowId;
        context.kind = state->kind;
        context.title = state->title;
        context.scriptType = state->scriptType;
        context.scriptName = state->scriptName;
        context.text = text == nullptr ? "" : text;
        context.selection = selection;
        std::string error;
        if (!state->manager->invokeWindowAction(state->action, context, error) && !error.empty()) g_printerr("Extension window action failed: %s\n", error.c_str());
        g_free(text);
    }
}

TScriptList::TScriptList(std::string nextType, RC::RCOptions* nextOptions, TExtensionsManager* nextExtensions) : type(std::move(nextType)), options(nextOptions), extensionsManager(nextExtensions) {
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
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(tree), true);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(tree), 0);
    gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(tree), treeSearchContains, nullptr, nullptr);
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
    applyGtkButtonIcon(editButton, GTK_STOCK_EDIT);
    if (addButton != nullptr) applyGtkButtonIcon(addButton, GTK_STOCK_ADD);
    applyGtkButtonIcon(deleteButton, GTK_STOCK_DELETE);
    applyGtkButtonIcon(closeButton, GTK_STOCK_CLOSE);
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

void TScriptList::setServerName(const std::string& server) { serverName = server; const std::string base = type == "classes" ? "Classes" : "Weapon/GUI-Script List"; gtk_window_set_title(GTK_WINDOW(window), serverName.empty() ? base.c_str() : (base + " - " + serverName).c_str()); }

TScriptList::~TScriptList() { if (classList == this) classList = nullptr; if (weaponList == this) weaponList = nullptr; if (window != nullptr) gtk_widget_destroy(window); if (store != nullptr) g_object_unref(store); }
void TScriptList::hide() { if (window != nullptr) gtk_widget_hide(window); }

void TScriptList::open(void* nextConnection) {
    connection = nextConnection;
    if (type == "classes") {
        classList = this;
        restoreScriptReceiver(connection);
        refresh();
        gtk_widget_show_all(window);
        gtk_window_present(GTK_WINDOW(window));
        return;
    }
    weaponList = this;
    restoreScriptReceiver(connection);
    rc_on_weapon_list_received(connection, onWeaponListReceived, this);
    rc_request_weapon_list(connection);
}
void TScriptList::setConnection(void* nextConnection) { connection = nextConnection; }
void TScriptList::restoreScriptReceiver(void* connection) { rc_on_script_received(connection, onScript, nullptr); }

void TScriptList::onWeaponListReceived(int, void* data) {
    TScriptList* list = static_cast<TScriptList*>(data);
    if (list == nullptr || list->connection == nullptr) return;
    list->refresh();
    gtk_widget_show_all(list->window);
    gtk_window_present(GTK_WINDOW(list->window));
}

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
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Icon:"), 0, 1, 1, 1);
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
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
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
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), 0, GTK_SORT_ASCENDING);
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
    const std::string scriptName = name == nullptr ? "" : name;
    std::string weaponIcon;
    if (type == "weapons" && connection != nullptr) {
        RCWeapon* entries = nullptr;
        const int count = rc_get_weapons(connection, &entries);
        for (int index = 0; index < count; ++index) if (entries[index].name != nullptr && scriptName == entries[index].name) { if (entries[index].image != nullptr) weaponIcon = entries[index].image; break; }
    }
    if (options != nullptr && (options->externaleditorscope == "scripts" || options->externaleditorscope == "text")) {
        if (externalEditor == nullptr || externalWorkspace != options->externaleditorworkspace || externalCommand != options->externaleditorcommand) {
            externalWorkspace = options->externaleditorworkspace;
            externalCommand = options->externaleditorcommand;
            externalEditor = std::make_unique<TExternalEditor>(externalWorkspace, externalCommand);
        }
        externalEditor->open(serverName, type, scriptName, script == nullptr ? "" : script, [this, scriptName, weaponIcon](const std::string& updated) {
            backupEditorText(type == "weapons" ? "weapon" : "class", scriptName, updated, true);
            if (type == "weapons") rc_update_weapon(connection, scriptName.c_str(), weaponIcon.c_str(), updated.c_str()); else rc_update_class(connection, scriptName.c_str(), updated.c_str());
        });
        return;
    }
    struct EditorState { void* connection; bool weapon; std::string name; GtkWidget* editor; GtkWidget* icon; };
    const std::string editorTitle = (type == "weapons" ? "Weapon: " : "Class: ") + scriptName + (serverName.empty() ? "" : " (" + serverName + ")");
    GtkWidget* dialog = gtk_dialog_new_with_buttons(editorTitle.c_str(), GTK_WINDOW(window), static_cast<GtkDialogFlags>(0), "Apply", GTK_RESPONSE_ACCEPT, "Close", GTK_RESPONSE_CANCEL, nullptr);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), false);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), nullptr);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_NORMAL);
    gtk_window_set_resizable(GTK_WINDOW(dialog), true);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 520);
    GtkSourceLanguage* language = gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(), "graal");
    GtkSourceBuffer* sourceBuffer = language != nullptr ? gtk_source_buffer_new_with_language(language) : gtk_source_buffer_new(nullptr);
    applyRemoteControlSourceStyle(sourceBuffer);
    GtkWidget* editor = gtk_source_view_new_with_buffer(sourceBuffer);
    configureGScriptEditor(editor);
    setGScriptEditorConnection(editor, connection);
    addEditorFindButton(dialog, editor);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(editor), true);
    setGScriptEditorContent(GTK_TEXT_BUFFER(sourceBuffer), script);
    trackScriptEditor(dialog, GTK_TEXT_BUFFER(sourceBuffer), editorTitle, script, connection);
    backupEditorText(type == "weapons" ? "weapon" : "class", scriptName.c_str(), script, false);
    g_object_unref(sourceBuffer);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scrolled), editor);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 0);
    gtk_box_set_spacing(GTK_BOX(content), 0);
    const auto actions = extensionsManager == nullptr ? std::vector<RC::ExtensionWindowActionBinding>() : extensionsManager->windowActions("script-editor");
    if (!actions.empty()) {
        GtkWidget* actionBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(actionBar), 4);
        for (const auto& action : actions) {
            auto* actionState = new EditorExtensionActionState{extensionsManager, action, editor, type + ":" + scriptName, "script-editor", type, scriptName, editorTitle};
            GtkWidget* actionButton = gtk_button_new_with_label(action.action.label.c_str());
            gtk_widget_set_tooltip_text(actionButton, ("Run " + action.action.label + " from " + action.extensionName).c_str());
            g_signal_connect_data(actionButton, "clicked", G_CALLBACK(onEditorExtensionAction), actionState, [](gpointer data, GClosure*) { delete static_cast<EditorExtensionActionState*>(data); }, static_cast<GConnectFlags>(0));
            gtk_box_pack_start(GTK_BOX(actionBar), actionButton, false, false, 0);
        }
        gtk_box_pack_start(GTK_BOX(content), actionBar, false, false, 0);
    }
    GtkWidget* icon = nullptr;
    if (type == "weapons") {
        GtkWidget* iconRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(iconRow), 5);
        gtk_box_pack_start(GTK_BOX(iconRow), gtk_label_new("Icon:"), false, false, 0);
        icon = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(icon), weaponIcon.c_str());
        gtk_widget_set_hexpand(icon, true);
        gtk_box_pack_start(GTK_BOX(iconRow), icon, true, true, 0);
        gtk_box_pack_start(GTK_BOX(content), iconRow, false, false, 0);
    }
    gtk_widget_set_margin_top(scrolled, 0);
    gtk_box_pack_start(GTK_BOX(content), wrapGScriptEditor(editor, scrolled), true, true, 0);
    addGScriptEditorLineStatus(GTK_DIALOG(dialog), editor);
    g_signal_connect(editor, "key-press-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventKey* event, gpointer dialog) {
        if (consumeEditorCtrlS(widget, event)) {
            gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
            return static_cast<gboolean>(TRUE);
        }
        return static_cast<gboolean>(FALSE);
    }), dialog);
    g_signal_connect(editor, "key-release-event", G_CALLBACK(releaseEditorCtrlS), nullptr);
    auto* state = new EditorState{connection, type == "weapons", scriptName, editor, icon};
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* responseDialog, gint response, gpointer data) {
        auto* editorState = static_cast<EditorState*>(data);
        if (response == GTK_RESPONSE_ACCEPT) {
            GtkTextBuffer* editorBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editorState->editor));
            GtkTextIter start;
            GtkTextIter end;
            gtk_text_buffer_get_bounds(editorBuffer, &start, &end);
            gchar* updated = gtk_text_buffer_get_text(editorBuffer, &start, &end, false);
            backupEditorText(editorState->weapon ? "weapon" : "class", editorState->name, updated == nullptr ? "" : updated, true);
            void* currentConnection = scriptEditorConnection(editorBuffer);
            if (currentConnection != nullptr) {
                if (editorState->weapon) rc_update_weapon(currentConnection, editorState->name.c_str(), editorState->icon == nullptr ? "" : gtk_entry_get_text(GTK_ENTRY(editorState->icon)), updated);
                else rc_update_class(currentConnection, editorState->name.c_str(), updated);
            }
            g_free(updated);
            markScriptEditorSaved(editorBuffer);
        } else gtk_widget_destroy(GTK_WIDGET(responseDialog));
    }), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) { delete static_cast<EditorState*>(data); }), state);
    gtk_widget_show_all(dialog);
    if (icon != nullptr) {
        gtk_editable_select_region(GTK_EDITABLE(icon), 0, 0);
        gtk_editable_set_position(GTK_EDITABLE(icon), -1);
    }
    gtk_widget_grab_focus(editor);
}
