#include "TNPCList.h"
#include "TButtonIcons.h"
#include "TBackup.h"
#include "TEditorFind.h"
#include "TGScriptEditor.h"
#include "TScriptEditorTracking.h"
#include "TScriptList.h"
#include "TTheme.h"
#include "TTreeSearch.h"

#include <grclib.h>
#include <gtksourceview/gtksource.h>

#include <set>
#include <string>

TNPCList::TNPCList(std::string accountName, RC::RCOptions* nextOptions) : accountName(std::move(accountName)), options(nextOptions) {
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
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(tree), true);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(tree), 1);
    gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(tree), treeSearchContains, nullptr, nullptr);
    const struct { const char* title; int column; int width; } columns[] = {{"ID", 0, 60}, {"Name", 1, 180}, {"Type", 2, 120}, {"Level", 3, 120}};
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
    GtkWidget* remove = gtk_button_new_with_label("Delete");
    GtkWidget* close = gtk_button_new_with_label("Close");
    applyGtkButtonIcon(refresh, GTK_STOCK_REFRESH);
    applyGtkButtonIcon(add, GTK_STOCK_ADD);
    applyGtkButtonIcon(remove, GTK_STOCK_DELETE);
    applyGtkButtonIcon(close, GTK_STOCK_CLOSE);
    gtk_container_add(GTK_CONTAINER(buttons), refresh);
    gtk_container_add(GTK_CONTAINER(buttons), add);
    gtk_container_add(GTK_CONTAINER(buttons), remove);
    gtk_container_add(GTK_CONTAINER(buttons), close);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 0);
    g_signal_connect(refresh, "clicked", G_CALLBACK(onRefresh), this);
    g_signal_connect(add, "clicked", G_CALLBACK(onAdd), this);
    g_signal_connect(remove, "clicked", G_CALLBACK(onDeleteNPC), this);
    g_signal_connect(close, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}
TNPCList::~TNPCList() { if (window != nullptr) gtk_widget_destroy(window); if (store != nullptr) g_object_unref(store); }
void TNPCList::setServerName(const std::string& server) { serverName = server; gtk_window_set_title(GTK_WINDOW(window), serverName.empty() ? "NPCs" : ("NPCs - " + serverName).c_str()); }
void TNPCList::hide() { if (window != nullptr) gtk_widget_hide(window); }
void TNPCList::open(void* nextConnection) { connection = nextConnection; rc_on_npc_added(connection, onNPCChanged, this); rc_on_npc_deleted(connection, [](int, void* data) { static_cast<TNPCList*>(data)->refresh(); }, this); refresh(); gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); }
void TNPCList::setConnection(void* nextConnection) { connection = nextConnection; }
void TNPCList::onRefresh(GtkButton*, gpointer data) { static_cast<TNPCList*>(data)->refresh(); }
void TNPCList::onAdd(GtkButton*, gpointer data) {
    TNPCList* list = static_cast<TNPCList*>(data);
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Add NPC", GTK_WINDOW(list->window), GTK_DIALOG_MODAL, "Apply", GTK_RESPONSE_OK, "Cancel", GTK_RESPONSE_CANCEL, nullptr);
    GtkWidget* grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    const std::string nextId = std::to_string(list->firstFreeNPCId());
    const struct { const char* label; const char* key; const char* value; int row; } fields[] = {{"Name:", "name", "", 0}, {"ID:", "id", nextId.c_str(), 1}, {"Scripter:", "scripter", list->accountName.c_str(), 3}, {"Starting level:", "level", list->addNPCLevel.c_str(), 4}};
    for (int index = 0; index < static_cast<int>(G_N_ELEMENTS(fields)); ++index) {
        GtkWidget* label = gtk_label_new(fields[index].label);
        GtkWidget* entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), fields[index].value);
        gtk_widget_set_hexpand(entry, true);
        gtk_widget_set_size_request(entry, 162, -1);
        gtk_grid_attach(GTK_GRID(grid), label, 0, fields[index].row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), entry, 1, fields[index].row, 3, 1);
        g_object_set_data(G_OBJECT(dialog), fields[index].key, entry);
    }
    GtkWidget* typeLabel = gtk_label_new("Type:");
    GtkWidget* typeCombo = gtk_combo_box_text_new_with_entry();
    for (const char* type : {"OBJECT", "ANIMAL", "POLICE", "HORSE", "BOAT", "HOUSEC"}) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(typeCombo), type);
    GtkWidget* typeField = gtk_bin_get_child(GTK_BIN(typeCombo));
    gtk_entry_set_text(GTK_ENTRY(typeField), list->addNPCType.c_str());
    gtk_widget_set_hexpand(typeCombo, true);
    gtk_grid_attach(GTK_GRID(grid), typeLabel, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), typeCombo, 1, 2, 3, 1);
    g_object_set_data(G_OBJECT(dialog), "type", typeField);
    GtkWidget* xLabel = gtk_label_new("X:");
    GtkWidget* xEntry = gtk_entry_new();
    GtkWidget* yLabel = gtk_label_new("Y:");
    GtkWidget* yEntry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(xEntry), list->addNPCX.c_str());
    gtk_entry_set_text(GTK_ENTRY(yEntry), list->addNPCY.c_str());
    gtk_widget_set_size_request(xEntry, 84, -1);
    gtk_widget_set_size_request(yEntry, 84, -1);
    gtk_grid_attach(GTK_GRID(grid), xLabel, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), xEntry, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), yLabel, 2, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), yEntry, 3, 5, 1, 1);
    g_object_set_data(G_OBJECT(dialog), "x", xEntry);
    g_object_set_data(G_OBJECT(dialog), "y", yEntry);
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
        list->addNPCType = value("type");
        list->addNPCLevel = value("level");
        list->addNPCX = value("x");
        list->addNPCY = value("y");
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
    GtkWidget* editScript = gtk_menu_item_new_with_label("Edit Script");
    GtkWidget* editFlags = gtk_menu_item_new_with_label("Edit Flags");
    GtkWidget* viewAttributes = gtk_menu_item_new_with_label("View Attributes");
    GtkWidget* separator = gtk_separator_menu_item_new();
    GtkWidget* warp = gtk_menu_item_new_with_label("Warp");
    GtkWidget* reset = gtk_menu_item_new_with_label("Reset");
    GtkWidget* remove = gtk_menu_item_new_with_label("Delete");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), editScript);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), editFlags);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), viewAttributes);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), reset);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), remove);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), warp);
    g_signal_connect(editScript, "activate", G_CALLBACK(onEditScript), list);
    g_signal_connect(editFlags, "activate", G_CALLBACK(onEditFlags), list);
    g_signal_connect(viewAttributes, "activate", G_CALLBACK(onViewAttributes), list);
    g_signal_connect(warp, "activate", G_CALLBACK(onWarp), list);
    g_signal_connect(reset, "activate", G_CALLBACK(onReset), list);
    g_signal_connect(remove, "activate", G_CALLBACK(onDeleteNPC), list);
    g_signal_connect(menu, "selection-done", G_CALLBACK(+[](GtkMenu* menuWidget, gpointer) { gtk_widget_destroy(GTK_WIDGET(menuWidget)); }), nullptr);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
    return true;
}
void TNPCList::onEditScript(GtkMenuItem*, gpointer data) {
    TNPCList* list = static_cast<TNPCList*>(data);
    if (list->selectedNPCId < 0) return;
    rc_on_script_received(list->connection, onNPCScript, list);
    rc_request_npc_script(list->connection, list->selectedNPCId);
}
void TNPCList::onEditFlags(GtkMenuItem*, gpointer data) {
    TNPCList* list = static_cast<TNPCList*>(data);
    if (list->selectedNPCId < 0) return;
    rc_on_npc_flags(list->connection, onNPCFlags, list);
    rc_get_npc_flags(list->connection, list->selectedNPCId);
}
void TNPCList::onViewAttributes(GtkMenuItem*, gpointer data) {
    TNPCList* list = static_cast<TNPCList*>(data);
    if (list->selectedNPCId < 0) return;
    rc_on_npc_attributes(list->connection, onNPCAttributes, list);
    rc_request_npc_attributes(list->connection, list->selectedNPCId);
}
void TNPCList::onWarp(GtkMenuItem*, gpointer data) {
    TNPCList* list = static_cast<TNPCList*>(data);
    if (list->selectedNPCId < 0) return;
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Warp NPC", GTK_WINDOW(list->window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Warp", GTK_RESPONSE_OK, nullptr);
    GtkWidget* grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    const char* labels[] = {"Level:", "X:", "Y:"};
    const char* keys[] = {"level", "x", "y"};
    for (int index = 0; index < 3; ++index) {
        GtkWidget* entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), index == 0 ? "" : "0");
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(labels[index]), 0, index, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), entry, 1, index, 1, 1);
        g_object_set_data(G_OBJECT(dialog), keys[index], entry);
    }
    g_object_set_data(G_OBJECT(dialog), "npc-list", list);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid);
    g_signal_connect(dialog, "response", G_CALLBACK(onWarpResponse), list);
    gtk_widget_show_all(dialog);
}
void TNPCList::onWarpResponse(GtkDialog* dialog, gint response, gpointer data) {
    if (response == GTK_RESPONSE_OK) {
        TNPCList* list = static_cast<TNPCList*>(data);
        const auto value = [dialog](const char* key) { return gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), key))); };
        rc_warp_npc(list->connection, list->selectedNPCId, std::strtof(value("x"), nullptr), std::strtof(value("y"), nullptr), value("level"));
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}
void TNPCList::onNPCScript(const char* scriptType, const char* name, int id, const char* script, void* data) {
    if (scriptType == nullptr || std::string(scriptType) != "npc") return;
    TNPCList* list = static_cast<TNPCList*>(data);
    list->showScriptEditor(name == nullptr ? "NPC" : name, id, script == nullptr ? "" : script);
    TScriptList::restoreScriptReceiver(list->connection);
}
void TNPCList::onNPCFlags(int id, const char* flags, void* data) { static_cast<TNPCList*>(data)->showFlagsEditor(id, flags == nullptr ? "" : flags); }
void TNPCList::onNPCAttributes(int id, const char* attributes, void* data) { static_cast<TNPCList*>(data)->showAttributes(id, attributes == nullptr ? "" : attributes); }
void TNPCList::onReset(GtkMenuItem*, gpointer data) {
    TNPCList* list = static_cast<TNPCList*>(data);
    if (list->selectedNPCId >= 0) rc_reset_npc(list->connection, list->selectedNPCId);
}
void TNPCList::onDeleteNPC(GtkMenuItem*, gpointer data) {
    TNPCList* list = static_cast<TNPCList*>(data);
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(list->tree)), &model, &row)) gtk_tree_model_get(model, &row, 0, &list->selectedNPCId, -1);
    if (list->selectedNPCId < 0) return;
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(list->window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL, "Delete NPC %d?", list->selectedNPCId);
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response == GTK_RESPONSE_OK) rc_delete_npc(list->connection, list->selectedNPCId);
}
void TNPCList::showScriptEditor(const char* name, int id, const char* script) {
    if (options != nullptr && (options->externaleditorscope == "scripts" || options->externaleditorscope == "text")) {
        if (externalEditor == nullptr || externalWorkspace != options->externaleditorworkspace || externalCommand != options->externaleditorcommand) {
            externalWorkspace = options->externaleditorworkspace;
            externalCommand = options->externaleditorcommand;
            externalEditor = std::make_unique<TExternalEditor>(externalWorkspace, externalCommand);
        }
        const std::string scriptName = name == nullptr || *name == '\0' ? std::to_string(id) : std::string(name);
        const std::string backupName = "npc" + scriptName;
        externalEditor->open(serverName, "npcs", std::to_string(id) + "_" + scriptName, script == nullptr ? "" : script, [this, id, backupName](const std::string& updated) {
            backupEditorText("npcscript", backupName, updated, true);
            if (connection != nullptr) rc_update_npc(connection, id, updated.c_str());
        });
        return;
    }
    struct EditorState { void* connection; int id; std::string backupName; GtkWidget* editor; };
    const std::string scriptName = name == nullptr ? "" : name;
    const std::string editorTitle = "Npcscript: " + scriptName + (serverName.empty() ? "" : " (" + serverName + ")");
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
    const std::string backupName = name != nullptr && *name != '\0' ? "npc" + std::string(name) : std::to_string(id);
    backupEditorText("npcscript", backupName, script, false);
    g_object_unref(sourceBuffer);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scrolled), editor);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 0);
    gtk_box_set_spacing(GTK_BOX(content), 0);
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
    auto* state = new EditorState{connection, id, backupName, editor};
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* responseDialog, gint response, gpointer data) {
        auto* editorState = static_cast<EditorState*>(data);
        if (response == GTK_RESPONSE_ACCEPT) {
            GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editorState->editor));
            GtkTextIter start;
            GtkTextIter end;
            gtk_text_buffer_get_bounds(buffer, &start, &end);
            gchar* updated = gtk_text_buffer_get_text(buffer, &start, &end, false);
            backupEditorText("npcscript", editorState->backupName, updated == nullptr ? "" : updated, true);
            void* currentConnection = scriptEditorConnection(buffer);
            if (currentConnection != nullptr) rc_update_npc(currentConnection, editorState->id, updated);
            g_free(updated);
            markScriptEditorSaved(buffer);
        } else gtk_widget_destroy(GTK_WIDGET(responseDialog));
    }), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) { delete static_cast<EditorState*>(data); }), state);
    gtk_widget_show_all(dialog);
}
void TNPCList::showFlagsEditor(int id, const char* flags) {
    if (options != nullptr && options->externaleditorscope == "text") {
        if (externalEditor == nullptr || externalWorkspace != options->externaleditorworkspace || externalCommand != options->externaleditorcommand) {
            externalWorkspace = options->externaleditorworkspace;
            externalCommand = options->externaleditorcommand;
            externalEditor = std::make_unique<TExternalEditor>(externalWorkspace, externalCommand);
        }
        const std::string fileName = "npc_" + std::to_string(id) + "_flags.txt";
        externalEditor->open(serverName, "npcflags", fileName, flags == nullptr ? "" : flags, [this, id](const std::string& updated) {
            backupEditorText("npcflags", std::to_string(id), updated, true);
            if (connection != nullptr) rc_set_npc_flags(connection, id, updated.c_str());
        });
        return;
    }
    struct FlagState { void* connection; int id; GtkWidget* text; };
    const std::string editorTitle = serverName.empty() ? "Edit Flags" : "Edit Flags - " + serverName;
    GtkWidget* dialog = gtk_dialog_new_with_buttons(editorTitle.c_str(), GTK_WINDOW(window), static_cast<GtkDialogFlags>(0), "Apply", GTK_RESPONSE_ACCEPT, "Close", GTK_RESPONSE_CANCEL, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 360);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkSourceLanguage* language = gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(), "ini");
    GtkSourceBuffer* sourceBuffer = language != nullptr ? gtk_source_buffer_new_with_language(language) : gtk_source_buffer_new(nullptr);
    applyRemoteControlSourceStyle(sourceBuffer);
    GtkWidget* text = gtk_source_view_new_with_buffer(sourceBuffer);
    configureGScriptEditor(text, false);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text), true);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(sourceBuffer), flags, -1);
    backupEditorText("npcflags", std::to_string(id), flags, false);
    g_object_unref(sourceBuffer);
    gtk_container_add(GTK_CONTAINER(scrolled), text);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), wrapGScriptEditor(text, scrolled), true, true, 0);
    addGScriptEditorLineStatus(GTK_DIALOG(dialog), text);
    auto* state = new FlagState{connection, id, text};
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* responseDialog, gint response, gpointer data) {
        auto* state = static_cast<FlagState*>(data);
        if (response == GTK_RESPONSE_ACCEPT) {
            GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->text));
            GtkTextIter start;
            GtkTextIter end;
            gtk_text_buffer_get_bounds(buffer, &start, &end);
            gchar* content = gtk_text_buffer_get_text(buffer, &start, &end, false);
            backupEditorText("npcflags", std::to_string(state->id), content == nullptr ? "" : content, true);
            rc_set_npc_flags(state->connection, state->id, content == nullptr ? "" : content);
            g_free(content);
        } else gtk_widget_destroy(GTK_WIDGET(responseDialog));
    }), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) { delete static_cast<FlagState*>(data); }), state);
    gtk_widget_show_all(dialog);
}
void TNPCList::showAttributes(int id, const char* attributes) {
    const std::string editorTitle = "Attributes of NPC " + std::to_string(id) + (serverName.empty() ? "" : " - " + serverName);
    GtkWidget* dialog = gtk_dialog_new_with_buttons(editorTitle.c_str(), GTK_WINDOW(window), static_cast<GtkDialogFlags>(0), "Close", GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 360);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkSourceLanguage* language = gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(), "ini");
    GtkSourceBuffer* sourceBuffer = language != nullptr ? gtk_source_buffer_new_with_language(language) : gtk_source_buffer_new(nullptr);
    applyRemoteControlSourceStyle(sourceBuffer);
    GtkWidget* text = gtk_source_view_new_with_buffer(sourceBuffer);
    configureGScriptEditor(text, false);
    gtk_widget_set_name(text, "NPCAttributes");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text), false);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text), true);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(sourceBuffer), attributes, -1);
    g_object_unref(sourceBuffer);
    gtk_container_add(GTK_CONTAINER(scrolled), text);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), wrapGScriptEditor(text, scrolled), true, true, 0);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* responseDialog, gint, gpointer) { gtk_widget_destroy(GTK_WIDGET(responseDialog)); }), nullptr);
    gtk_widget_show_all(dialog);
}
void TNPCList::onClose(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TNPCList*>(data)->window); }
void TNPCList::onNPCChanged(int, const char*, void* data) { static_cast<TNPCList*>(data)->refresh(); }
gboolean TNPCList::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TNPCList*>(data)->window); return true; }
void TNPCList::refresh() {
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
    gtk_list_store_clear(store);
    RCNPC* npcs = nullptr;
    const int count = rc_get_npcs(connection, &npcs);
    for (int index = 0; index < count; ++index) {
        GtkTreeIter row;
        gtk_list_store_append(store, &row);
        gtk_list_store_set(store, &row, 0, npcs[index].id, 1, npcs[index].name == nullptr ? "" : npcs[index].name, 2, npcs[index].type == nullptr ? "" : npcs[index].type, 3, npcs[index].level == nullptr ? "" : npcs[index].level, -1);
    }
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), 0, GTK_SORT_ASCENDING);
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
