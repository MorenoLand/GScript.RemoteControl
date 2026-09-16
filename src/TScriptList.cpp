#include "TScriptList.h"
#include "TExtensions.h"
#include "TButtonIcons.h"
#include "TBackup.h"
#include "TEditorFind.h"
#include "TGScriptEditor.h"
#include "TScriptEditorTracking.h"
#include "TTheme.h"
#include "TTreeSearch.h"
#include "TDebug.h"

#include <grclib.h>
#include <gtksourceview/gtksource.h>
#include <filesystem>
#include <cstring>
#include <utility>
#include <algorithm>
#include <fstream>
#include <system_error>
#include <unordered_map>

namespace {
    struct ScriptReceiverState { TScriptList* classList = nullptr; TScriptList* weaponList = nullptr; void (*npcCallback)(const char*, const char*, int, const char*, void*) = nullptr; void* npcData = nullptr; };
    std::unordered_map<void*, std::unique_ptr<ScriptReceiverState>> scriptReceivers;

    struct ScriptWindowRecord { std::string session; std::string type; std::string name; int x = 0; int y = 0; int width = 0; int height = 0; };

    std::string scriptSessionKey(const std::string& server, const std::string& account) { return server + "\x1f" + account; }
    std::filesystem::path scriptStatePath(const std::filesystem::path& applicationDirectory) { return RC::rcOptionsDirectory(applicationDirectory) / "script-windows.txt"; }

    std::string encodeScriptStateField(const std::string& value) {
        static constexpr char digits[] = "0123456789ABCDEF";
        std::string result;
        for (const unsigned char character : value) {
            if (character == '%' || character == '\t' || character == '\r' || character == '\n' || character < 0x20) { result += '%'; result += digits[character >> 4]; result += digits[character & 0x0F]; }
            else result += static_cast<char>(character);
        }
        return result;
    }

    int scriptStateHex(char character) { if (character >= '0' && character <= '9') return character - '0'; if (character >= 'A' && character <= 'F') return character - 'A' + 10; if (character >= 'a' && character <= 'f') return character - 'a' + 10; return -1; }
    std::string decodeScriptStateField(const std::string& value) {
        std::string result;
        for (std::size_t index = 0; index < value.size(); ++index) {
            if (value[index] == '%' && index + 2 < value.size()) { const int high = scriptStateHex(value[index + 1]); const int low = scriptStateHex(value[index + 2]); if (high >= 0 && low >= 0) { result += static_cast<char>((high << 4) | low); index += 2; continue; } }
            result += value[index];
        }
        return result;
    }

    std::vector<std::string> splitScriptStateFields(const std::string& line) {
        std::vector<std::string> fields;
        std::size_t start = 0;
        while (start <= line.size()) { const std::size_t end = line.find('\t', start); fields.push_back(decodeScriptStateField(line.substr(start, end == std::string::npos ? std::string::npos : end - start))); if (end == std::string::npos) break; start = end + 1; }
        return fields;
    }

    std::vector<ScriptWindowRecord> loadScriptWindowRecords(const std::filesystem::path& path) {
        std::vector<ScriptWindowRecord> records;
        std::ifstream input(path);
        for (std::string line; std::getline(input, line);) {
            const std::vector<std::string> fields = splitScriptStateFields(line);
            if (fields.size() != 7 || fields[0].empty() || fields[1].empty() || fields[2].empty()) continue;
            try { records.push_back({fields[0], fields[1], fields[2], std::stoi(fields[3]), std::stoi(fields[4]), std::stoi(fields[5]), std::stoi(fields[6])}); } catch (...) { }
        }
        return records;
    }

    void saveScriptWindowRecords(const std::filesystem::path& path, const std::vector<ScriptWindowRecord>& records) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return;
        std::ofstream output(path, std::ios::trunc);
        if (!output) return;
        for (const ScriptWindowRecord& record : records) output << encodeScriptStateField(record.session) << '\t' << encodeScriptStateField(record.type) << '\t' << encodeScriptStateField(record.name) << '\t' << record.x << '\t' << record.y << '\t' << record.width << '\t' << record.height << '\n';
    }

    void updateScriptWindowRecord(const std::filesystem::path& path, const ScriptWindowRecord& record) {
        std::vector<ScriptWindowRecord> records = loadScriptWindowRecords(path);
        const auto existing = std::find_if(records.begin(), records.end(), [&](const ScriptWindowRecord& value) { return value.session == record.session && value.type == record.type && value.name == record.name; });
        if (existing == records.end()) records.push_back(record); else *existing = record;
        saveScriptWindowRecords(path, records);
    }

    void removeScriptWindowRecord(const std::filesystem::path& path, const std::string& session, const std::string& type, const std::string& name) {
        std::vector<ScriptWindowRecord> records = loadScriptWindowRecords(path);
        records.erase(std::remove_if(records.begin(), records.end(), [&](const ScriptWindowRecord& value) { return value.session == session && value.type == type && value.name == name; }), records.end());
        saveScriptWindowRecords(path, records);
    }

    bool findScriptWindowRecord(const std::filesystem::path& path, const std::string& session, const std::string& type, const std::string& name, ScriptWindowRecord& record) {
        const auto records = loadScriptWindowRecords(path);
        const auto found = std::find_if(records.begin(), records.end(), [&](const ScriptWindowRecord& value) { return value.session == session && value.type == type && value.name == name; });
        if (found == records.end()) return false;
        record = *found;
        return true;
    }

    bool hasScriptWindowRecords(const std::filesystem::path& path, const std::string& session) {
        const auto records = loadScriptWindowRecords(path);
        return std::any_of(records.begin(), records.end(), [&](const ScriptWindowRecord& value) { return value.session == session; });
    }

    std::size_t scriptByteCount(const char* script) { return script == nullptr ? 0 : std::strlen(script); }
    std::size_t scriptLineCount(const char* script) {
        if (script == nullptr || *script == '\0') return 0;
        std::size_t lines = 1;
        for (const char* cursor = script; *cursor != '\0'; ++cursor) if (*cursor == '\n') ++lines;
        return lines;
    }

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

    GtkWidget* extensionActionIcon(const RC::ExtensionWindowActionBinding& action) {
        const std::string& specification = action.action.icon;
        if (specification.empty()) return nullptr;
        if (specification.rfind("gtk:", 0) == 0) return gtk_image_new_from_icon_name(specification.substr(4).c_str(), GTK_ICON_SIZE_BUTTON);
        std::string value = specification.rfind("file:", 0) == 0 ? specification.substr(5) : specification;
        std::filesystem::path path(value);
        if (path.is_relative()) {
            const auto direct = action.extensionDirectory / path;
            const auto images = action.extensionDirectory / "images" / path;
            path = std::filesystem::exists(direct) ? direct : images;
        }
        if (!std::filesystem::exists(path)) return gtk_image_new_from_icon_name(specification.c_str(), GTK_ICON_SIZE_BUTTON);
        GError* error = nullptr;
        GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(path.string().c_str(), 18, 18, true, &error);
        if (error != nullptr) g_error_free(error);
        if (pixbuf == nullptr) return nullptr;
        GtkWidget* image = gtk_image_new_from_pixbuf(pixbuf);
        g_object_unref(pixbuf);
        return image;
    }

    GtkWidget* createEditorExtensionActionButton(const RC::ExtensionWindowActionBinding& action, GtkWidget* editor, TExtensionsManager* extensionsManager, const std::string& type, const std::string& scriptName, const std::string& editorTitle) {
        auto* actionState = new EditorExtensionActionState{extensionsManager, action, editor, type + ":" + scriptName, "script-editor", type, scriptName, editorTitle};
        GtkWidget* actionButton = gtk_button_new_with_label(action.action.label.c_str());
        if (GtkWidget* image = extensionActionIcon(action)) { gtk_button_set_image(GTK_BUTTON(actionButton), image); gtk_button_set_always_show_image(GTK_BUTTON(actionButton), true); }
        gtk_widget_set_tooltip_text(actionButton, ("Run " + action.action.label + " from " + action.extensionName).c_str());
        g_signal_connect_data(actionButton, "clicked", G_CALLBACK(onEditorExtensionAction), actionState, [](gpointer data, GClosure*) { delete static_cast<EditorExtensionActionState*>(data); }, static_cast<GConnectFlags>(0));
        return actionButton;
    }

    void addEditorExtensionActionBar(GtkWidget* content, const std::vector<RC::ExtensionWindowActionBinding>& actions, GtkWidget* editor, TExtensionsManager* extensionsManager, const std::string& type, const std::string& scriptName, const std::string& editorTitle) {
        if (actions.empty()) return;
        GtkWidget* actionBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(actionBar), 4);
        for (const auto& action : actions) gtk_box_pack_start(GTK_BOX(actionBar), createEditorExtensionActionButton(action, editor, extensionsManager, type, scriptName, editorTitle), false, false, 0);
        gtk_box_pack_start(GTK_BOX(content), actionBar, false, false, 0);
    }
}

TScriptList::TScriptList(std::string nextType, RC::RCOptions* nextOptions, TExtensionsManager* nextExtensions, const std::filesystem::path& applicationDirectory) : type(std::move(nextType)), options(nextOptions), extensionsManager(nextExtensions), scriptWindowStatePath(scriptStatePath(applicationDirectory)) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    applyRemoteControlWindowChrome(window);
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
    GtkWidget* addButton = (type == "weapons" || type == "classes") ? gtk_button_new_with_label("Add") : nullptr;
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

TScriptList::~TScriptList() { cancelRestoredEditors(); unregisterScriptReceiver(); if (pendingCreateTimer != 0) g_source_remove(pendingCreateTimer); if (window != nullptr) gtk_widget_destroy(window); if (store != nullptr) g_object_unref(store); }
void TScriptList::hide() { if (window != nullptr) gtk_widget_hide(window); }

void TScriptList::open(void* nextConnection) {
    if (nextConnection == nullptr) return;
    setConnection(nextConnection);
    opened = true;
    registerScriptReceiver();
    remoteControlDebugLog("script list: open type=%s connection=%p server=%s", type.c_str(), connection, serverName.c_str());
    if (type == "classes") {
        rc_on_class_added(connection, onClassAdded, this);
        rc_on_class_deleted(connection, onClassDeleted, this);
        refresh();
        gtk_widget_show_all(window);
        gtk_window_present(GTK_WINDOW(window));
        return;
    }
    rc_on_weapon_added(connection, onWeaponAdded, this);
    rc_on_weapon_deleted(connection, onWeaponDeleted, this);
    rc_on_weapon_list_received(connection, onWeaponListReceived, this);
    rc_request_weapon_list(connection);
}
void TScriptList::setConnection(void* nextConnection) {
    if (nextConnection == nullptr) cancelRestoredEditors();
    if (connection == nextConnection) { if (opened && connection != nullptr) registerScriptReceiver(); return; }
    unregisterScriptReceiver();
    connection = nextConnection;
    if (opened && connection != nullptr) registerScriptReceiver();
}
void TScriptList::setSession(const std::string& server, const std::string& account) { sessionKey = scriptSessionKey(server, account); }
bool TScriptList::hasSavedEditors(const std::filesystem::path& applicationDirectory, const std::string& server, const std::string& account) { return hasScriptWindowRecords(scriptStatePath(applicationDirectory), scriptSessionKey(server, account)); }
void TScriptList::restoreScriptReceiver(void* connection) {
    if (connection == nullptr) return;
    const auto found = scriptReceivers.find(connection);
    if (found == scriptReceivers.end()) { rc_on_script_received(connection, onScript, nullptr); return; }
    found->second->npcCallback = nullptr;
    found->second->npcData = nullptr;
    rc_on_script_received(connection, onScript, found->second.get());
}
void TScriptList::registerNPCScriptReceiver(void* connection, void (*callback)(const char*, const char*, int, const char*, void*), void* data) {
    if (connection == nullptr) return;
    auto& state = scriptReceivers[connection];
    if (state == nullptr) state = std::make_unique<ScriptReceiverState>();
    state->npcCallback = callback;
    state->npcData = data;
    rc_on_script_received(connection, onScript, state.get());
}
void TScriptList::registerScriptReceiver() {
    if (connection == nullptr) return;
    auto& state = scriptReceivers[connection];
    if (state == nullptr) state = std::make_unique<ScriptReceiverState>();
    if (type == "classes") state->classList = this; else state->weaponList = this;
    rc_on_script_received(connection, onScript, state.get());
    scriptReceiverRegistered = true;
}
void TScriptList::unregisterScriptReceiver() {
    if (!scriptReceiverRegistered || connection == nullptr) { scriptReceiverRegistered = false; return; }
    const auto found = scriptReceivers.find(connection);
    if (found == scriptReceivers.end()) { scriptReceiverRegistered = false; return; }
    ScriptReceiverState* state = found->second.get();
    if (state->classList == this) state->classList = nullptr;
    if (state->weaponList == this) state->weaponList = nullptr;
    if (state->classList == nullptr && state->weaponList == nullptr && state->npcCallback == nullptr) {
        rc_on_script_received(connection, onScript, nullptr);
        scriptReceivers.erase(found);
    } else rc_on_script_received(connection, onScript, state);
    scriptReceiverRegistered = false;
}

void TScriptList::onWeaponListReceived(int, void* data) {
    TScriptList* list = static_cast<TScriptList*>(data);
    if (list == nullptr || list->connection == nullptr) return;
    remoteControlDebugLog("script list: weapon list received list=%p connection=%p", list, list->connection);
    list->refresh();
    if (!list->pendingCreateName.empty() || !list->pendingDeleteName.empty()) {
        RCWeapon* entries = nullptr;
        const int count = rc_get_weapons(list->connection, &entries);
        bool deleted = !list->pendingDeleteName.empty();
        for (int index = 0; index < count; ++index) if (entries[index].name != nullptr && list->pendingCreateName == entries[index].name) {
            const std::string createdName = list->pendingCreateName;
            list->pendingCreateName.clear();
            list->showEditor(createdName.c_str(), "");
            break;
        }
        if (deleted) for (int index = 0; index < count; ++index) if (entries[index].name != nullptr && list->pendingDeleteName == entries[index].name) { deleted = false; break; }
        if (deleted) { remoteControlDebugLog("script delete: type=weapons name=%s confirmed", list->pendingDeleteName.c_str()); list->pendingDeleteName.clear(); }
        if (list->pendingCreateName.empty() && list->pendingDeleteName.empty() && list->pendingCreateTimer != 0) {
            g_source_remove(list->pendingCreateTimer);
            list->pendingCreateTimer = 0;
        }
    }
    gtk_widget_show_all(list->window);
    gtk_window_present(GTK_WINDOW(list->window));
}

void TScriptList::onWeaponAdded(const char* name, void* data) {
    auto* list = static_cast<TScriptList*>(data);
    if (list == nullptr || list->connection == nullptr || list->type != "weapons") return;
    list->refresh();
    if (name == nullptr || list->pendingCreateName != name) return;
    list->pendingCreateName.clear();
    if (list->pendingCreateTimer != 0) { g_source_remove(list->pendingCreateTimer); list->pendingCreateTimer = 0; }
    list->showEditor(name, "");
}

void TScriptList::onWeaponDeleted(const char* name, void* data) {
    auto* list = static_cast<TScriptList*>(data);
    if (list == nullptr || list->connection == nullptr || list->type != "weapons") return;
    list->refresh();
    if (name != nullptr && list->pendingDeleteName == name) {
        list->pendingDeleteName.clear();
        if (list->pendingCreateName.empty() && list->pendingCreateTimer != 0) { g_source_remove(list->pendingCreateTimer); list->pendingCreateTimer = 0; }
    }
}

void TScriptList::onClassAdded(const char* name, void* data) {
    auto* list = static_cast<TScriptList*>(data);
    if (list == nullptr || list->connection == nullptr || list->type != "classes") return;
    list->refresh();
    if (name == nullptr || list->pendingCreateName != name) return;
    list->pendingCreateName.clear();
    list->showEditor(name, "");
}

void TScriptList::onClassDeleted(const char*, void* data) { auto* list = static_cast<TScriptList*>(data); if (list != nullptr && list->connection != nullptr) list->refresh(); }

gboolean TScriptList::onWeaponMutationPoll(gpointer data) {
    auto* list = static_cast<TScriptList*>(data);
    if (list == nullptr || list->connection == nullptr || (list->pendingCreateName.empty() && list->pendingDeleteName.empty()) || ++list->pendingCreateAttempts > 20) { if (list != nullptr) { list->pendingCreateName.clear(); list->pendingDeleteName.clear(); list->pendingCreateTimer = 0; } return G_SOURCE_REMOVE; }
    rc_request_weapon_list(list->connection);
    return G_SOURCE_CONTINUE;
}

void TScriptList::restoreOpenEditors() {
    opened = true;
    registerScriptReceiver();
    if (connection == nullptr || sessionKey.empty() || restoringEditors) return;
    pendingRestoreNames.clear();
    pendingRestoreIndex = 0;
    const std::string scriptType = type == "classes" ? "class" : "weapon";
    for (const ScriptWindowRecord& record : loadScriptWindowRecords(scriptWindowStatePath)) if (record.session == sessionKey && record.type == scriptType && !record.name.empty()) pendingRestoreNames.push_back(record.name);
    if (pendingRestoreNames.empty()) return;
    restoringEditors = true;
    requestNextRestoredEditor();
}

void TScriptList::requestNextRestoredEditor() {
    if (pendingRestoreTimer != 0) { g_source_remove(pendingRestoreTimer); pendingRestoreTimer = 0; }
    if (!restoringEditors || connection == nullptr) return;
    const std::string scriptType = type == "classes" ? "class" : "weapon";
    while (pendingRestoreIndex < pendingRestoreNames.size()) {
        pendingScriptName = pendingRestoreNames[pendingRestoreIndex];
        pendingScriptRequestAt = std::chrono::steady_clock::now();
        registerScriptReceiver();
        const int result = type == "weapons" ? rc_request_weapon_script(connection, pendingScriptName.c_str()) : rc_request_class_script(connection, pendingScriptName.c_str());
        remoteControlDebugLog("script restore request: type=%s name=%s result=%d connection=%p", scriptType.c_str(), pendingScriptName.c_str(), result, connection);
        if (result > 0) { pendingRestoreTimer = g_timeout_add_seconds(10, onRestoreTimeout, this); return; }
        pendingScriptName.clear();
        ++pendingRestoreIndex;
    }
    restoringEditors = false;
    pendingRestoreNames.clear();
    pendingRestoreIndex = 0;
}

void TScriptList::cancelRestoredEditors() {
    if (pendingRestoreTimer != 0) { g_source_remove(pendingRestoreTimer); pendingRestoreTimer = 0; }
    restoringEditors = false;
    pendingRestoreNames.clear();
    pendingRestoreIndex = 0;
    pendingScriptName.clear();
}

gboolean TScriptList::onRestoreTimeout(gpointer data) {
    auto* list = static_cast<TScriptList*>(data);
    if (list == nullptr || !list->restoringEditors) return G_SOURCE_REMOVE;
    list->pendingRestoreTimer = 0;
    remoteControlDebugLog("script restore timeout: type=%s name=%s connection=%p", list->type.c_str(), list->pendingScriptName.c_str(), list->connection);
    list->pendingScriptName.clear();
    ++list->pendingRestoreIndex;
    list->requestNextRestoredEditor();
    return G_SOURCE_REMOVE;
}

void TScriptList::restoreEditorWindowState(const std::string& name, GtkWidget* dialog) const {
    if (sessionKey.empty() || name.empty() || dialog == nullptr || !GTK_IS_WINDOW(dialog)) return;
    ScriptWindowRecord record;
    if (!findScriptWindowRecord(scriptWindowStatePath, sessionKey, type == "classes" ? "class" : "weapon", name, record) || record.width <= 0 || record.height <= 0) return;
    gtk_window_resize(GTK_WINDOW(dialog), record.width, record.height);
    gtk_window_move(GTK_WINDOW(dialog), record.x, record.y);
}

void TScriptList::onEdit(GtkButton*, gpointer data) { static_cast<TScriptList*>(data)->edit(); }
void TScriptList::onDeleteScript(GtkButton*, gpointer data) { static_cast<TScriptList*>(data)->deleteSelected(); }
void TScriptList::onAdd(GtkButton*, gpointer data) {
    struct AddState { TScriptList* list; GtkWidget* name; GtkWidget* image; };
    TScriptList* list = static_cast<TScriptList*>(data);
    const bool isClass = list->type == "classes";
    GtkWidget* dialog = gtk_dialog_new_with_buttons(isClass ? "Add Class" : "Add Weapon/GUI Script", nullptr, GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Add", GTK_RESPONSE_ACCEPT, nullptr);
    applyRemoteControlWindowChrome(dialog);
    GtkWidget* grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    GtkWidget* name = gtk_entry_new();
    GtkWidget* image = isClass ? nullptr : gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Name:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name, 1, 0, 1, 1);
    if (!isClass) {
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Icon:"), 0, 1, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), image, 1, 1, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid, true, true, 0);
    auto* state = new AddState{list, name, image};
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* addDialog, gint response, gpointer userData) {
        auto* values = static_cast<AddState*>(userData);
        if (response == GTK_RESPONSE_ACCEPT) {
            const char* name = gtk_entry_get_text(GTK_ENTRY(values->name));
            if (values->list->connection != nullptr && name != nullptr && *name != '\0') {
                const int result = values->list->type == "classes" ? rc_add_class(values->list->connection, name, "") : rc_add_weapon(values->list->connection, name, gtk_entry_get_text(GTK_ENTRY(values->image)), "");
                remoteControlDebugLog("script create: type=%s name=%s result=%d", values->list->type.c_str(), name, result);
                if (result > 0) {
                    values->list->pendingCreateName = name;
                    if (values->list->type == "weapons") {
                        values->list->pendingCreateAttempts = 0;
                        if (values->list->pendingCreateTimer != 0) g_source_remove(values->list->pendingCreateTimer);
                        values->list->pendingCreateTimer = g_timeout_add(250, onWeaponMutationPoll, values->list);
                        rc_request_weapon_list(values->list->connection);
                    }
                }
            }
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
    if (connection == nullptr) return;
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
    gtk_list_store_clear(store);
    if (type == "weapons") {
        RCWeapon* entries = nullptr;
        const int count = rc_get_weapons(connection, &entries);
        remoteControlDebugLog("script list: refresh type=weapons count=%d connection=%p", count, connection);
        for (int index = 0; index < count; ++index) if (entries[index].name != nullptr && entries[index].name[0] != '\0') { GtkTreeIter row; gtk_list_store_append(store, &row); gtk_list_store_set(store, &row, 0, entries[index].name, -1); }
    } else {
        RCClass* entries = nullptr;
        const int count = rc_get_classes(connection, &entries);
        remoteControlDebugLog("script list: refresh type=classes count=%d connection=%p", count, connection);
        for (int index = 0; index < count; ++index) if (entries[index].name != nullptr && entries[index].name[0] != '\0') { GtkTreeIter row; gtk_list_store_append(store, &row); gtk_list_store_set(store, &row, 0, entries[index].name, -1); }
    }
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), 0, GTK_SORT_ASCENDING);
}

void TScriptList::edit() {
    if (connection == nullptr) return;
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) {
        remoteControlDebugLog("script request: ignored because no row is selected type=%s connection=%p", type.c_str(), connection);
        return;
    }
    gchar* name = nullptr;
    gtk_tree_model_get(model, &row, 0, &name, -1);
    if (name == nullptr) {
        remoteControlDebugLog("script request: selected row has no script name type=%s connection=%p", type.c_str(), connection);
        return;
    }
    cancelRestoredEditors();
    registerScriptReceiver();
    pendingScriptName = name;
    pendingScriptRequestAt = std::chrono::steady_clock::now();
    remoteControlDebugLog("script request: type=%s name=%s connection=%p", type.c_str(), name, connection);
    const int result = type == "weapons" ? rc_request_weapon_script(connection, name) : rc_request_class_script(connection, name);
    remoteControlDebugLog("script request: type=%s name=%s result=%d", type.c_str(), name, result);
    if (result <= 0) pendingScriptName.clear();
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
    GtkWidget* dialog = gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL, "Delete %s %s?", noun.c_str(), name);
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response == GTK_RESPONSE_OK && connection != nullptr) {
        const int result = type == "weapons" ? rc_delete_weapon(connection, name) : rc_delete_class(connection, name);
        remoteControlDebugLog("script delete: type=%s name=%s result=%d", type.c_str(), name, result);
        if (result > 0 && type == "weapons") {
            pendingDeleteName = name;
            pendingCreateAttempts = 0;
            if (pendingCreateTimer != 0) g_source_remove(pendingCreateTimer);
            pendingCreateTimer = g_timeout_add(250, onWeaponMutationPoll, this);
            rc_request_weapon_list(connection);
        }
    }
    g_free(name);
}

void TScriptList::onScript(const char* scriptType, const char* name, int id, const char* script, void* data) {
    const std::string callbackType = scriptType == nullptr ? "" : scriptType;
    auto* receiver = static_cast<ScriptReceiverState*>(data);
    if (receiver != nullptr && callbackType == "npc" && receiver->npcCallback != nullptr) { receiver->npcCallback(scriptType, name, id, script, receiver->npcData); return; }
    TScriptList* list = receiver == nullptr ? nullptr : (callbackType == "weapon" ? receiver->weaponList : callbackType == "class" ? receiver->classList : nullptr);
    const std::size_t bytes = scriptByteCount(script);
    const std::size_t lines = scriptLineCount(script);
    remoteControlDebugLog("script received: callbackType=%s id=%d name=%s bytes=%zu lines=%zu classList=%p weaponList=%p data=%p", callbackType.c_str(), id, name == nullptr ? "" : name, bytes, lines, receiver == nullptr ? nullptr : receiver->classList, receiver == nullptr ? nullptr : receiver->weaponList, data);
    if (list == nullptr || name == nullptr || list->pendingScriptName != name) {
        remoteControlDebugLog("script received: dropped because no matching request for %s", name == nullptr ? "" : name);
        return;
    }
    long long elapsedMs = -1;
    if (!list->pendingScriptName.empty() && name != nullptr && list->pendingScriptName == name) elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - list->pendingScriptRequestAt).count();
    remoteControlDebugLog("script received: routing list=%p requested=%s elapsed_ms=%lld", list, list->pendingScriptName.c_str(), elapsedMs);
    if (list->pendingRestoreTimer != 0) { g_source_remove(list->pendingRestoreTimer); list->pendingRestoreTimer = 0; }
    const bool restoring = list->restoringEditors;
    list->showEditor(name == nullptr ? "" : name, script == nullptr ? "" : script);
    list->pendingScriptName.clear();
    if (restoring) { ++list->pendingRestoreIndex; list->requestNextRestoredEditor(); }
}

void TScriptList::showEditor(const char* name, const char* script) {
    const std::string scriptName = name == nullptr ? "" : name;
    remoteControlDebugLog("script editor: prepare type=%s name=%s bytes=%zu lines=%zu server=%s external_scope=%s", type.c_str(), scriptName.c_str(), scriptByteCount(script), scriptLineCount(script), serverName.c_str(), options == nullptr ? "" : options->externaleditorscope.c_str());
    std::string weaponIcon;
    if (type == "weapons" && connection != nullptr) {
        RCWeapon* entries = nullptr;
        const int count = rc_get_weapons(connection, &entries);
        for (int index = 0; index < count; ++index) if (entries[index].name != nullptr && scriptName == entries[index].name) { if (entries[index].image != nullptr) weaponIcon = entries[index].image; break; }
    }
    if (options != nullptr && (options->externaleditorscope == "scripts" || options->externaleditorscope == "text")) {
        remoteControlDebugLog("script editor: external open type=%s name=%s workspace=%s command=%s", type.c_str(), scriptName.c_str(), options->externaleditorworkspace.c_str(), options->externaleditorcommand.c_str());
        if (externalEditor == nullptr || externalWorkspace != options->externaleditorworkspace || externalCommand != options->externaleditorcommand) {
            externalWorkspace = options->externaleditorworkspace;
            externalCommand = options->externaleditorcommand;
            externalEditor = std::make_unique<TExternalEditor>(externalWorkspace, externalCommand);
        }
        externalEditor->open(serverName, type, scriptName, script == nullptr ? "" : script, [this, scriptName, weaponIcon](const std::string& updated) {
            backupEditorText(type == "weapons" ? "weapon" : "class", scriptName, updated, true);
            if (connection == nullptr) return;
            if (type == "weapons") rc_update_weapon(connection, scriptName.c_str(), weaponIcon.c_str(), updated.c_str()); else rc_update_class(connection, scriptName.c_str(), updated.c_str());
        });
        remoteControlDebugLog("script editor: external open dispatched type=%s name=%s", type.c_str(), scriptName.c_str());
        return;
    }
    struct EditorState { void* connection; bool weapon; std::string name; GtkWidget* editor; GtkWidget* icon; GtkWidget* dialog; std::filesystem::path statePath; std::string session; std::string type; guint geometryTimer; };
    const std::string editorTitle = (type == "weapons" ? "Weapon: " : "Class: ") + scriptName + (serverName.empty() ? "" : " (" + serverName + ")");
    GtkWidget* dialog = gtk_dialog_new_with_buttons(editorTitle.c_str(), nullptr, static_cast<GtkDialogFlags>(0), "Apply", GTK_RESPONSE_ACCEPT, "Close", GTK_RESPONSE_CANCEL, nullptr);
    applyRemoteControlWindowChrome(dialog);
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
    remoteControlDebugLog("script editor: GTK buffer populated type=%s name=%s chars=%d utf8=%s", type.c_str(), scriptName.c_str(), gtk_text_buffer_get_char_count(GTK_TEXT_BUFFER(sourceBuffer)), g_utf8_validate(script == nullptr ? "" : script, -1, nullptr) ? "valid" : "repaired");
    trackScriptEditor(dialog, GTK_TEXT_BUFFER(sourceBuffer), editorTitle, script, connection);
    backupEditorText(type == "weapons" ? "weapon" : "class", scriptName.c_str(), script, false);
    g_object_unref(sourceBuffer);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scrolled), editor);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 0);
    gtk_box_set_spacing(GTK_BOX(content), 0);
    const auto actions = extensionsManager == nullptr ? std::vector<RC::ExtensionWindowActionBinding>() : extensionsManager->windowActions("script-editor");
    std::vector<RC::ExtensionWindowActionBinding> topActions;
    std::vector<RC::ExtensionWindowActionBinding> bottomActions;
    for (const auto& action : actions) (action.action.placement == "bottom" ? bottomActions : topActions).push_back(action);
    addEditorExtensionActionBar(content, topActions, editor, extensionsManager, type, scriptName, editorTitle);
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
    if (!bottomActions.empty()) {
        GtkWidget* actionBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(actionBar), 4);
        for (const auto& action : bottomActions) gtk_box_pack_start(GTK_BOX(actionBar), createEditorExtensionActionButton(action, editor, extensionsManager, type, scriptName, editorTitle), false, false, 0);
        gtk_box_pack_end(GTK_BOX(content), actionBar, false, false, 0);
    }
    addGScriptEditorLineStatus(GTK_DIALOG(dialog), editor);
    g_signal_connect(editor, "key-press-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventKey* event, gpointer dialog) {
        if (consumeEditorCtrlS(widget, event)) {
            gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
            return static_cast<gboolean>(TRUE);
        }
        return static_cast<gboolean>(FALSE);
    }), dialog);
    g_signal_connect(editor, "key-release-event", G_CALLBACK(releaseEditorCtrlS), nullptr);
    auto* state = new EditorState{connection, type == "weapons", scriptName, editor, icon, dialog, scriptWindowStatePath, sessionKey, type == "weapons" ? "weapon" : "class", 0};
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
        } else { if (!editorState->session.empty()) removeScriptWindowRecord(editorState->statePath, editorState->session, editorState->type, editorState->name); gtk_widget_destroy(GTK_WIDGET(responseDialog)); }
    }), state);
    g_signal_connect(dialog, "configure-event", G_CALLBACK(+[](GtkWidget* configured, GdkEventConfigure*, gpointer data) -> gboolean {
        auto* state = static_cast<EditorState*>(data);
        if (state->geometryTimer == 0) state->geometryTimer = g_timeout_add(250, +[](gpointer timerData) -> gboolean {
            auto* state = static_cast<EditorState*>(timerData);
            state->geometryTimer = 0;
            if (state->dialog != nullptr) { gint x = 0; gint y = 0; gint width = 0; gint height = 0; gtk_window_get_position(GTK_WINDOW(state->dialog), &x, &y); gtk_window_get_size(GTK_WINDOW(state->dialog), &width, &height); if (!state->session.empty() && width > 0 && height > 0) updateScriptWindowRecord(state->statePath, {state->session, state->type, state->name, x, y, width, height}); }
            return G_SOURCE_REMOVE;
        }, state);
        return false;
    }), state);
    g_signal_connect(dialog, "delete-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer data) -> gboolean { auto* state = static_cast<EditorState*>(data); if (!state->session.empty()) removeScriptWindowRecord(state->statePath, state->session, state->type, state->name); return false; }), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) { auto* state = static_cast<EditorState*>(data); if (state->geometryTimer != 0) g_source_remove(state->geometryTimer); delete state; }), state);
    gtk_widget_show_all(dialog);
    restoreEditorWindowState(scriptName, dialog);
    gint x = 0;
    gint y = 0;
    gint width = 0;
    gint height = 0;
    gtk_window_get_position(GTK_WINDOW(dialog), &x, &y);
    gtk_window_get_size(GTK_WINDOW(dialog), &width, &height);
    if (!state->session.empty() && width > 0 && height > 0) updateScriptWindowRecord(state->statePath, {state->session, state->type, state->name, x, y, width, height});
    remoteControlDebugLog("script editor: GTK dialog presented type=%s name=%s dialog=%p", type.c_str(), scriptName.c_str(), dialog);
    if (icon != nullptr) {
        gtk_editable_select_region(GTK_EDITABLE(icon), 0, 0);
        gtk_editable_set_position(GTK_EDITABLE(icon), -1);
    }
    gtk_widget_grab_focus(editor);
}
