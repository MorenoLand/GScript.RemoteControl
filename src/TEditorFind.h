#pragma once

#include <algorithm>
#include <cstdlib>
#include <gtk/gtk.h>
#include <string>

#include "TEditorFormat.h"

inline std::string editorLastFindText;

struct EditorFindState {
    GtkWidget* editor = nullptr;
    GtkWidget* dialog = nullptr;
    GtkWidget* findEntry = nullptr;
    GtkWidget* replaceLabel = nullptr;
    GtkWidget* replaceEntry = nullptr;
    GtkWidget* replaceNext = nullptr;
    GtkWidget* replaceAll = nullptr;
    GtkWidget* caseSensitive = nullptr;
    GtkWidget* fullWords = nullptr;
};

inline GtkWidget* editorIconButton(const char* label, const char* icon) { GtkWidget* button = gtk_button_new_with_label(label); GtkWidget* image = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON); gtk_button_set_image(GTK_BUTTON(button), image); gtk_button_set_always_show_image(GTK_BUTTON(button), true); return button; }
inline void editorFindButtonIcon(GtkWidget* button, const char* icon) { gtk_button_set_image(GTK_BUTTON(button), gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON)); gtk_button_set_always_show_image(GTK_BUTTON(button), true); gtk_button_box_set_child_non_homogeneous(GTK_BUTTON_BOX(gtk_widget_get_parent(button)), button, true); }

inline bool editorFindWordBoundary(const GtkTextIter& start, const GtkTextIter& end, const std::string& query) {
    if (query.empty()) return true;
    const gunichar first = g_utf8_get_char(query.c_str());
    const char* lastStart = g_utf8_find_prev_char(query.c_str(), query.c_str() + query.size());
    const gunichar last = lastStart == nullptr ? first : g_utf8_get_char(lastStart);
    GtkTextIter before = start;
    GtkTextIter after = end;
    const gunichar beforeChar = gtk_text_iter_starts_line(&before) ? 0 : gtk_text_iter_get_char(gtk_text_iter_backward_char(&before) ? &before : &before);
    const gunichar afterChar = gtk_text_iter_ends_line(&after) ? 0 : gtk_text_iter_get_char(&after);
    const bool wordFirst = g_unichar_isalnum(first) || first == '_';
    const bool wordLast = g_unichar_isalnum(last) || last == '_';
    const bool beforeWord = g_unichar_isalnum(beforeChar) || beforeChar == '_';
    const bool afterWord = g_unichar_isalnum(afterChar) || afterChar == '_';
    return (!wordFirst || !beforeWord) && (!wordLast || !afterWord);
}

inline bool editorFindSearch(EditorFindState* state, GtkTextIter start, bool previous, GtkTextIter* matchStart, GtkTextIter* matchEnd) {
    const std::string query = gtk_entry_get_text(GTK_ENTRY(state->findEntry));
    const GtkTextSearchFlags flags = static_cast<GtkTextSearchFlags>(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->caseSensitive)) ? GTK_TEXT_SEARCH_TEXT_ONLY : (GTK_TEXT_SEARCH_TEXT_ONLY | GTK_TEXT_SEARCH_CASE_INSENSITIVE));
    while (previous ? gtk_text_iter_backward_search(&start, query.c_str(), flags, matchStart, matchEnd, nullptr) : gtk_text_iter_forward_search(&start, query.c_str(), flags, matchStart, matchEnd, nullptr)) {
        if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->fullWords)) || editorFindWordBoundary(*matchStart, *matchEnd, query)) return true;
        start = previous ? *matchStart : *matchEnd;
    }
    return false;
}

inline void rememberEditorFind(EditorFindState* state) {
    const char* query = gtk_entry_get_text(GTK_ENTRY(state->findEntry));
    if (query != nullptr) editorLastFindText = query;
}

inline void selectEditorFindMatch(EditorFindState* state, bool previous) {
    rememberEditorFind(state);
    if (editorLastFindText.empty()) return;
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor));
    GtkTextIter start;
    GtkTextIter matchStart;
    GtkTextIter matchEnd;
    GtkTextIter selectionStart;
    GtkTextIter selectionEnd;
    const bool selected = gtk_text_buffer_get_selection_bounds(buffer, &selectionStart, &selectionEnd);
    if (selected) start = previous ? selectionStart : selectionEnd;
    else gtk_text_buffer_get_iter_at_mark(buffer, &start, gtk_text_buffer_get_insert(buffer));
    gboolean found = editorFindSearch(state, start, previous, &matchStart, &matchEnd);
    if (!found) {
        if (previous) gtk_text_buffer_get_end_iter(buffer, &start);
        else gtk_text_buffer_get_start_iter(buffer, &start);
        found = editorFindSearch(state, start, previous, &matchStart, &matchEnd);
    }
    if (!found) return;
    gtk_text_buffer_select_range(buffer, &matchStart, &matchEnd);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(state->editor), &matchStart, 0.2, false, 0.0, 0.0);
}

inline bool editorFindSelectionMatches(EditorFindState* state) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor));
    GtkTextIter start;
    GtkTextIter end;
    if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) return false;
    gchar* selected = gtk_text_buffer_get_text(buffer, &start, &end, false);
    const char* query = gtk_entry_get_text(GTK_ENTRY(state->findEntry));
    const bool caseSensitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->caseSensitive));
    gchar* selectedFold = caseSensitive ? g_strdup(selected == nullptr ? "" : selected) : g_utf8_casefold(selected == nullptr ? "" : selected, -1);
    gchar* queryFold = caseSensitive ? g_strdup(query == nullptr ? "" : query) : g_utf8_casefold(query == nullptr ? "" : query, -1);
    const bool matches = g_strcmp0(selectedFold, queryFold) == 0;
    g_free(selected); g_free(selectedFold); g_free(queryFold);
    return matches;
}

inline void replaceEditorFindNext(EditorFindState* state) {
    rememberEditorFind(state);
    if (editorLastFindText.empty()) return;
    if (!editorFindSelectionMatches(state)) selectEditorFindMatch(state, false);
    if (!editorFindSelectionMatches(state)) return;
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor));
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_selection_bounds(buffer, &start, &end);
    const char* replacement = gtk_entry_get_text(GTK_ENTRY(state->replaceEntry));
    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_delete(buffer, &start, &end);
    gtk_text_buffer_insert(buffer, &start, replacement == nullptr ? "" : replacement, -1);
    gtk_text_buffer_end_user_action(buffer);
    selectEditorFindMatch(state, false);
}

inline void replaceEditorFindAll(EditorFindState* state) {
    rememberEditorFind(state);
    if (editorLastFindText.empty()) return;
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor));
    GtkTextIter search;
    GtkTextIter matchStart;
    GtkTextIter matchEnd;
    gtk_text_buffer_get_start_iter(buffer, &search);
    const char* replacement = gtk_entry_get_text(GTK_ENTRY(state->replaceEntry));
    int replaced = 0;
    gtk_text_buffer_begin_user_action(buffer);
    while (editorFindSearch(state, search, false, &matchStart, &matchEnd)) {
        gtk_text_buffer_delete(buffer, &matchStart, &matchEnd);
        gtk_text_buffer_insert(buffer, &matchStart, replacement == nullptr ? "" : replacement, -1);
        search = matchStart;
        ++replaced;
    }
    gtk_text_buffer_end_user_action(buffer);
    if (replaced > 0) {
        gtk_text_buffer_place_cursor(buffer, &search);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(state->editor), &search, 0.2, false, 0.0, 0.0);
    }
}

inline EditorFindState* editorFindState(GtkWidget* editor) {
    EditorFindState* state = static_cast<EditorFindState*>(g_object_get_data(G_OBJECT(editor), "editor-find-state"));
    if (state != nullptr) return state;
    state = new EditorFindState();
    state->editor = editor;
    state->dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(state->dialog), "Find and Replace");
    gtk_window_set_transient_for(GTK_WINDOW(state->dialog), GTK_WINDOW(gtk_widget_get_toplevel(editor)));
    gtk_window_set_position(GTK_WINDOW(state->dialog), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_default_size(GTK_WINDOW(state->dialog), 360, -1);
    gtk_window_set_resizable(GTK_WINDOW(state->dialog), false);
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    GtkWidget* findLabel = gtk_label_new("Find:");
    state->findEntry = gtk_entry_new();
    state->replaceLabel = gtk_label_new("Replace:");
    state->replaceEntry = gtk_entry_new();
    state->fullWords = gtk_check_button_new_with_label("Full words only");
    state->caseSensitive = gtk_check_button_new_with_label("Case sensitive");
    GtkWidget* searchOptions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_label_set_max_width_chars(GTK_LABEL(gtk_bin_get_child(GTK_BIN(state->fullWords))), 14);
    gtk_label_set_max_width_chars(GTK_LABEL(gtk_bin_get_child(GTK_BIN(state->caseSensitive))), 12);
    gtk_widget_set_hexpand(state->findEntry, true);
    gtk_widget_set_hexpand(state->replaceEntry, true);
    gtk_box_pack_start(GTK_BOX(searchOptions), state->fullWords, true, false, 0);
    gtk_box_pack_end(GTK_BOX(searchOptions), state->caseSensitive, true, false, 0);
    gtk_grid_attach(GTK_GRID(grid), findLabel, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), state->findEntry, 1, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), state->replaceLabel, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), state->replaceEntry, 1, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), searchOptions, 1, 2, 2, 1);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(state->dialog))), grid, false, false, 0);
    GtkWidget* close = gtk_dialog_add_button(GTK_DIALOG(state->dialog), "Close", GTK_RESPONSE_CLOSE);
    GtkWidget* previous = gtk_dialog_add_button(GTK_DIALOG(state->dialog), "Prev", 1);
    GtkWidget* next = gtk_dialog_add_button(GTK_DIALOG(state->dialog), "Next", 2);
    state->replaceNext = gtk_dialog_add_button(GTK_DIALOG(state->dialog), "Replace", 3);
    state->replaceAll = gtk_dialog_add_button(GTK_DIALOG(state->dialog), "All", 4);
    GtkWidget* actions = gtk_dialog_get_action_area(GTK_DIALOG(state->dialog));
    gtk_button_box_set_layout(GTK_BUTTON_BOX(actions), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(actions), 3);
    editorFindButtonIcon(close, "window-close-symbolic");
    editorFindButtonIcon(previous, "go-previous-symbolic");
    editorFindButtonIcon(next, "go-next-symbolic");
    editorFindButtonIcon(state->replaceNext, "edit-find-replace-symbolic");
    editorFindButtonIcon(state->replaceAll, "edit-select-all-symbolic");
    g_signal_connect(state->dialog, "response", G_CALLBACK(+[](GtkDialog* dialog, gint response, gpointer data) {
        EditorFindState* find = static_cast<EditorFindState*>(data);
        if (response == GTK_RESPONSE_CLOSE || response == GTK_RESPONSE_DELETE_EVENT) gtk_widget_hide(GTK_WIDGET(dialog));
        else if (response == 1) selectEditorFindMatch(find, true);
        else if (response == 2) selectEditorFindMatch(find, false);
        else if (response == 3) replaceEditorFindNext(find);
        else if (response == 4) replaceEditorFindAll(find);
    }), state);
    g_signal_connect(state->dialog, "delete-event", G_CALLBACK(+[](GtkWidget* dialog, GdkEvent*, gpointer) { gtk_widget_hide(dialog); return static_cast<gboolean>(TRUE); }), nullptr);
    g_signal_connect(state->findEntry, "activate", G_CALLBACK(+[](GtkEntry*, gpointer data) { selectEditorFindMatch(static_cast<EditorFindState*>(data), false); }), state);
    g_signal_connect(state->replaceEntry, "activate", G_CALLBACK(+[](GtkEntry*, gpointer data) { replaceEditorFindNext(static_cast<EditorFindState*>(data)); }), state);
    g_object_set_data_full(G_OBJECT(editor), "editor-find-state", state, +[](gpointer data) { EditorFindState* find = static_cast<EditorFindState*>(data); if (find->dialog != nullptr) gtk_widget_destroy(find->dialog); delete find; });
    return state;
}

inline void openEditorFind(GtkWidget* editor, bool replace = false) {
    EditorFindState* state = editorFindState(editor);
    if (gtk_entry_get_text_length(GTK_ENTRY(state->findEntry)) == 0 && !editorLastFindText.empty()) gtk_entry_set_text(GTK_ENTRY(state->findEntry), editorLastFindText.c_str());
    gtk_widget_show_all(state->dialog);
    gtk_widget_set_visible(state->replaceLabel, replace);
    gtk_widget_set_visible(state->replaceEntry, replace);
    gtk_widget_set_visible(state->replaceNext, replace);
    gtk_widget_set_visible(state->replaceAll, replace);
    gtk_window_resize(GTK_WINDOW(state->dialog), 360, replace ? 170 : 125);
    gtk_window_present(GTK_WINDOW(state->dialog));
    gtk_widget_grab_focus(state->findEntry);
}

inline void editorFind(GtkButton*, gpointer data) { openEditorFind(GTK_WIDGET(data)); }

inline gboolean editorFindButtonPress(GtkWidget*, GdkEventButton* event, gpointer data) {
    if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY) return false;
    openEditorFind(GTK_WIDGET(data), true);
    return true;
}

inline void openEditorGoToLine(GtkWidget* editor) {
    GtkTextIter current;
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
    gtk_text_buffer_get_iter_at_mark(buffer, &current, gtk_text_buffer_get_insert(buffer));
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Go to line", GTK_WINDOW(gtk_widget_get_toplevel(editor)), static_cast<GtkDialogFlags>(0), "Close", GTK_RESPONSE_CANCEL, "Go", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_text(GTK_ENTRY(entry), std::to_string(gtk_text_iter_get_line(&current) + 1).c_str());
    gtk_widget_set_size_request(entry, 120, -1);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, false, false, 8);
    g_object_set_data(G_OBJECT(dialog), "line-entry", entry);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* responseDialog, gint response, gpointer userData) {
        if (response == GTK_RESPONSE_ACCEPT) {
            GtkWidget* lineEntry = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(responseDialog), "line-entry"));
            GtkTextBuffer* textBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(userData));
            const long requested = std::strtol(gtk_entry_get_text(GTK_ENTRY(lineEntry)), nullptr, 10);
            const int lineCount = gtk_text_buffer_get_line_count(textBuffer);
            const int line = std::clamp(static_cast<int>(requested) - 1, 0, std::max(0, lineCount - 1));
            GtkTextIter target;
            gtk_text_buffer_get_iter_at_line(textBuffer, &target, line);
            gtk_text_buffer_place_cursor(textBuffer, &target);
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(userData), &target, 0.2, false, 0.0, 0.0);
        }
        gtk_widget_destroy(GTK_WIDGET(responseDialog));
    }), editor);
    gtk_widget_show_all(dialog);
}

inline void editorGoToLine(GtkButton*, gpointer data) { openEditorGoToLine(GTK_WIDGET(data)); }

inline gboolean editorFindKey(GtkWidget*, GdkEventKey* event, gpointer data) {
    if ((event->state & GDK_CONTROL_MASK) == 0) return FALSE;
    if (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F) openEditorFind(GTK_WIDGET(data), false);
    else if (event->keyval == GDK_KEY_h || event->keyval == GDK_KEY_H) openEditorFind(GTK_WIDGET(data), true);
    else return FALSE;
    return TRUE;
}

inline gboolean editorGoToLineKey(GtkWidget*, GdkEventKey* event, gpointer data) {
    if ((event->state & GDK_CONTROL_MASK) == 0 || (event->keyval != GDK_KEY_g && event->keyval != GDK_KEY_G)) return FALSE;
    openEditorGoToLine(GTK_WIDGET(data));
    return TRUE;
}

inline void addEditorFindShortcut(GtkWidget* editor) { g_signal_connect(editor, "key-press-event", G_CALLBACK(editorFindKey), editor); }
inline void addEditorGoToLineShortcut(GtkWidget* editor) { g_signal_connect(editor, "key-press-event", G_CALLBACK(editorGoToLineKey), editor); }

inline void addEditorFindButton(GtkWidget* dialog, GtkWidget* editor) {
    GtkWidget* goToLineButton = editorIconButton("Go to line", "go-jump-symbolic");
    GtkWidget* formatButton = createEditorFormatButton(editor);
    GtkWidget* button = editorIconButton("Find", "edit-find-symbolic");
    gtk_widget_set_tooltip_text(goToLineButton, "Go to line (Ctrl+G)");
    gtk_widget_set_tooltip_text(button, "Find (Ctrl+F) / right-click Replace (Ctrl+H)");
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_action_area(GTK_DIALOG(dialog))), goToLineButton);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_action_area(GTK_DIALOG(dialog))), formatButton);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_action_area(GTK_DIALOG(dialog))), button);
    g_signal_connect(goToLineButton, "clicked", G_CALLBACK(editorGoToLine), editor);
    g_signal_connect(button, "clicked", G_CALLBACK(editorFind), editor);
    gtk_widget_add_events(button, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(button, "button-press-event", G_CALLBACK(editorFindButtonPress), editor);
    addEditorFindShortcut(editor);
    addEditorGoToLineShortcut(editor);
    gtk_widget_show(goToLineButton);
    gtk_widget_show(formatButton);
    gtk_widget_show(button);
}
