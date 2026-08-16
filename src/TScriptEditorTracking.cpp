#include "TScriptEditorTracking.h"
#include "TMcpProtocol.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace {
    struct EditorState { GtkWidget* dialog; GtkTextBuffer* buffer; std::string name; std::string original; GtkTextTag* changedLines; void* connection; bool connectionDetached; };
    std::unordered_map<GtkTextBuffer*, EditorState> editors;

    std::vector<std::string> lines(const std::string& text) {
        std::vector<std::string> result;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) result.push_back(line);
        if (text.empty() || text.back() == '\n') result.emplace_back();
        return result;
    }

    std::string bufferText(GtkTextBuffer* buffer) {
        GtkTextIter start;
        GtkTextIter end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, false);
        std::string result = text == nullptr ? "" : text;
        g_free(text);
        return result;
    }

    void updateChangedLines(GtkTextBuffer* buffer) {
        const auto found = editors.find(buffer);
        if (found == editors.end()) return;
        EditorState& state = found->second;
        GtkTextIter start;
        GtkTextIter end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gtk_text_buffer_remove_tag(buffer, state.changedLines, &start, &end);
        const std::vector<std::string> original = lines(state.original);
        const std::vector<std::string> current = lines(bufferText(buffer));
        std::size_t first = 0;
        while (first < original.size() && first < current.size() && original[first] == current[first]) ++first;
        std::size_t originalLast = original.size();
        std::size_t currentLast = current.size();
        while (originalLast > first && currentLast > first && original[originalLast - 1] == current[currentLast - 1]) { --originalLast; --currentLast; }
        if (first == originalLast && first == currentLast) return;
        if (current.empty()) return;
        const std::size_t highlightedStart = std::min(first, current.size() - 1);
        const std::size_t highlightedEnd = std::max(highlightedStart + 1, currentLast);
        gtk_text_buffer_get_iter_at_line(buffer, &start, static_cast<gint>(highlightedStart));
        if (highlightedEnd >= current.size()) gtk_text_buffer_get_end_iter(buffer, &end);
        else gtk_text_buffer_get_iter_at_line(buffer, &end, static_cast<gint>(highlightedEnd));
        gtk_text_buffer_apply_tag(buffer, state.changedLines, &start, &end);
    }

    GtkWidget* findSaveButton(GtkWidget* widget) {
        if (GTK_IS_BUTTON(widget)) { const char* label = gtk_button_get_label(GTK_BUTTON(widget)); if (g_strcmp0(label, "Apply") == 0 || g_strcmp0(label, "Save") == 0) return widget; }
        if (!GTK_IS_CONTAINER(widget)) return nullptr;
        GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
        GtkWidget* result = nullptr;
        for (GList* child = children; child != nullptr && result == nullptr; child = child->next) result = findSaveButton(GTK_WIDGET(child->data));
        g_list_free(children);
        return result;
    }
}

void trackScriptEditor(GtkWidget* dialog, GtkTextBuffer* buffer, const std::string& name, const std::string& original, void* connection) {
    GdkRGBA changedLineTint{1.0, 0.78, 0.18, 0.14};
    GtkTextTag* changedLines = gtk_text_buffer_create_tag(buffer, nullptr, "background-rgba", &changedLineTint, nullptr);
    editors.insert_or_assign(buffer, EditorState{dialog, buffer, name, original, changedLines, connection, false});
    gtk_text_buffer_set_modified(buffer, false);
    g_signal_connect(buffer, "changed", G_CALLBACK(+[](GtkTextBuffer* changedBuffer, gpointer) { updateChangedLines(changedBuffer); }), nullptr);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) { editors.erase(static_cast<GtkTextBuffer*>(data)); }), buffer);
}

void detachScriptEditorConnection(void* connection) { if (connection == nullptr) return; for (auto& entry : editors) if (entry.second.connection == connection) entry.second.connectionDetached = true; }
void rebindScriptEditorConnection(void* disconnectedConnection, void* connection) { for (auto& entry : editors) if (entry.second.connection == disconnectedConnection) { entry.second.connection = connection; entry.second.connectionDetached = false; } }
void* scriptEditorConnection(GtkTextBuffer* buffer) { const auto found = editors.find(buffer); return found == editors.end() || found->second.connectionDetached ? nullptr : found->second.connection; }

void markScriptEditorSaved(GtkTextBuffer* buffer) {
    const auto found = editors.find(buffer);
    if (found == editors.end()) return;
    found->second.original = bufferText(buffer);
    gtk_text_buffer_set_modified(buffer, false);
    updateChangedLines(buffer);
}

std::vector<std::string> unsavedScriptEditors() {
    std::vector<std::string> result;
    for (const auto& entry : editors) if (gtk_text_buffer_get_modified(entry.first)) result.push_back(entry.second.name);
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<ScriptEditorSnapshot> scriptEditorSnapshots() {
    std::vector<ScriptEditorSnapshot> result;
    for (const auto& [buffer, state] : editors) {
        GtkTextIter start; GtkTextIter end;
        std::string selection;
        if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) { gchar* value = gtk_text_buffer_get_text(buffer, &start, &end, false); selection = value == nullptr ? "" : value; g_free(value); }
        result.push_back({state.name, bufferText(buffer), selection, GTK_IS_WINDOW(state.dialog) && gtk_window_is_active(GTK_WINDOW(state.dialog)), gtk_text_buffer_get_modified(buffer) != 0});
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.name < right.name; });
    return result;
}

bool writeScriptEditor(const std::string& name, const std::string& text, std::string& error) {
    for (const auto& [buffer, state] : editors) if (state.name == name) { gtk_text_buffer_set_text(buffer, text.c_str(), -1); gtk_text_buffer_set_modified(buffer, true); return true; }
    error = "Script editor is not open";
    return false;
}

bool replaceScriptEditorText(const std::string& name, const std::string& expected, const std::string& replacement, std::string& error) {
    for (const auto& [buffer, state] : editors) if (state.name == name) {
        std::string result;
        if (!mcpGuardedReplace(bufferText(buffer), expected, replacement, result, error)) return false;
        gtk_text_buffer_begin_user_action(buffer);
        gtk_text_buffer_set_text(buffer, result.c_str(), static_cast<gint>(result.size()));
        gtk_text_buffer_end_user_action(buffer);
        gtk_text_buffer_set_modified(buffer, true);
        return true;
    }
    error = "Script editor is not open";
    return false;
}

bool saveScriptEditor(const std::string& name, std::string& error) {
    for (const auto& [buffer, state] : editors) if (state.name == name) { GtkWidget* button = findSaveButton(state.dialog); if (button == nullptr) { error = "Open editor has no save action"; return false; } gtk_button_clicked(GTK_BUTTON(button)); return true; }
    error = "Script editor is not open";
    return false;
}
