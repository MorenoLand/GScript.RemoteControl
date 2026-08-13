#include "TGScriptEditor.h"
#include "TEditorFormat.h"
#include "TGS2Diagnostics.h"
#include "TTheme.h"

#include <grclib.h>
#include <gtksourceview/gtksource.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

void ensureGScriptEditorMinimap(GtkWidget* editor);

namespace {

    int tabWidth = 2;
    int scriptFontSize = 10;
    std::string scriptFontFamily = "Monospace";
    constexpr gint minimapFontSize = 3;
    bool useTabs = false;
    bool showLineNumbers = true;
    bool showMinimap = false;
    bool syntaxHighlighting = true;
    bool autoIndenting = true;
    bool smartHomeEnd = true;
    bool showBrackets = true;
    bool lspEnabled = true;
    bool scriptDiagnosticsEnabled = true;
    std::string completionSource = "https://api.gscript.dev/";
    std::filesystem::path completionCacheFile;
    unsigned int completionRequest = 0;
    bool apiDefinitionsLoading = false;
    bool apiDefinitionsLoaded = false;
    std::vector<GtkWidget*> completionEditors;
    struct ApiDefinition { std::string name; std::string type; std::vector<std::string> params; std::string returns; std::string scope; std::string description; std::string example; };
    std::vector<ApiDefinition> apiDefinitions;
    std::string lowerText(std::string value);
    std::vector<ApiDefinition> referenceApiDefinitions() {
        const std::vector<std::pair<std::string, std::vector<std::string>>> constructors = {
            {"GuiControl", {"name"}}, {"GuiMLTextCtrl", {"name"}}, {"GuiScrollCtrl", {"name"}}, {"GuiTextCtrl", {"name"}},
            {"GuiTextEditCtrl", {"name"}}, {"GuiTextListCtrl", {"name"}}, {"GuiArrayCtrl", {"name"}}, {"GuiButtonCtrl", {"name"}},
            {"GuiBitmapCtrl", {"name"}}, {"GuiBitmapButtonCtrl", {"name"}}, {"GuiCheckBoxCtrl", {"name"}}, {"GuiContainer", {"name"}},
            {"GuiWindowCtrl", {"name"}}, {"GuiCanvas", {"name"}}, {"GuiPopUpMenuCtrl", {"name"}}, {"GuiShowImgCtrl", {"name"}},
            {"GuiStretchCtrl", {"name"}}, {"GuiProgressCtrl", {"name"}}, {"GuiRadioCtrl", {"name"}}
        };
        std::vector<ApiDefinition> definitions;
        for (const auto& constructor : constructors) definitions.push_back({constructor.first, "class", constructor.second, {}, {}, {}, {}});
        definitions.push_back({"visible", "property", {}, {}, {}, {}, {}});
        return definitions;
    }
    typedef struct _RemoteCompletionProvider { GObject parent; GtkWidget* editor; } RemoteCompletionProvider;
    typedef struct _RemoteCompletionProviderClass { GObjectClass parentClass; } RemoteCompletionProviderClass;
    struct EditorCompletionState { GtkWidget* editor; RemoteCompletionProvider* provider; GtkWidget* signaturePopover; GtkWidget* signatureLabel; void* connection; bool completionArmed = false; bool restoreSignature = false; guint restoreTimer = 0; guint signatureTimer = 0; };
    std::vector<EditorCompletionState> editorCompletionStates;
    std::unordered_map<void*, std::unordered_map<int, std::string>> playerCommunityNames;
    struct EditorSelection { GtkTextMark* anchor; GtkTextMark* caret; };
    struct EditorMultiSelectionState { GtkWidget* editor; GtkTextTag* tag; std::vector<EditorSelection> selections; bool applying; };
    std::vector<std::unique_ptr<EditorMultiSelectionState>> multiSelectionStates;
    struct EditorDiagnosticsState { GtkWidget* editor; GtkTextTag* errorTag; GtkTextTag* warningTag; GtkTextTag* infoTag; guint timeout; std::string source; std::vector<GS2Diagnostic> diagnostics; };
    std::vector<std::unique_ptr<EditorDiagnosticsState>> diagnosticsStates;
    struct EditorBulkInsertState { GtkWidget* editor; GtkSourceBuffer* buffer; guint resumeTimer; bool active; std::size_t pendingBytes; std::size_t pendingLines; };
    std::vector<std::unique_ptr<EditorBulkInsertState>> bulkInsertStates;
    constexpr std::size_t bulkInsertBytes = 8192;
    constexpr std::size_t bulkInsertLines = 128;
    constexpr gint completionPopupMinWidth = 260;
    constexpr gint completionPopupMaxWidth = 320;
    struct CompletionPayload { unsigned int request; std::vector<ApiDefinition> definitions; };
    static gboolean refreshEditorScrollbars(gpointer data);

    GType remoteCompletionProvider_get_type();
    #define REMOTE_TYPE_COMPLETION_PROVIDER (remoteCompletionProvider_get_type())
    #define REMOTE_COMPLETION_PROVIDER(value) (G_TYPE_CHECK_INSTANCE_CAST((value), REMOTE_TYPE_COMPLETION_PROVIDER, RemoteCompletionProvider))

    void setEditorFontSize(GtkWidget* editor, int size) {
        PangoFontDescription* font = pango_font_description_from_string((scriptFontFamily + " " + std::to_string(size)).c_str());
        gtk_widget_override_font(editor, font);
        pango_font_description_free(font);
        g_object_set_data(G_OBJECT(editor), "script-font-size", GINT_TO_POINTER(size));
    }

    EditorDiagnosticsState* diagnosticsState(GtkWidget* editor) {
        const auto state = std::find_if(diagnosticsStates.begin(), diagnosticsStates.end(), [editor](const auto& value) { return value->editor == editor; });
        return state == diagnosticsStates.end() ? nullptr : state->get();
    }

    EditorBulkInsertState* bulkInsertState(GtkTextBuffer* buffer) {
        const auto state = std::find_if(bulkInsertStates.begin(), bulkInsertStates.end(), [buffer](const auto& value) { return GTK_TEXT_BUFFER(value->buffer) == buffer; });
        return state == bulkInsertStates.end() ? nullptr : state->get();
    }

    void clearEditorDiagnostics(EditorDiagnosticsState* state) {
        if (state->timeout != 0) { g_source_remove(state->timeout); state->timeout = 0; }
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor));
        GtkTextIter start;
        GtkTextIter end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gtk_text_buffer_remove_tag(buffer, state->errorTag, &start, &end);
        gtk_text_buffer_remove_tag(buffer, state->warningTag, &start, &end);
        gtk_text_buffer_remove_tag(buffer, state->infoTag, &start, &end);
        state->source.clear();
        state->diagnostics.clear();
    }

    void runEditorDiagnostics(EditorDiagnosticsState* state) {
        EditorBulkInsertState* bulk = bulkInsertState(gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor)));
        if (bulk != nullptr && bulk->active) return;
        clearEditorDiagnostics(state);
        if (!scriptDiagnosticsEnabled) return;
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor));
        GtkTextIter bufferStart;
        GtkTextIter bufferEnd;
        gtk_text_buffer_get_bounds(buffer, &bufferStart, &bufferEnd);
        gchar* text = gtk_text_buffer_get_text(buffer, &bufferStart, &bufferEnd, false);
        state->source = text;
        g_free(text);
        std::vector<GS2ApiFunction> enabledApiFunctions;
        if (lspEnabled) for (const ApiDefinition& definition : apiDefinitions) enabledApiFunctions.push_back(gs2ApiFunction(definition.name, definition.params));
        state->diagnostics = analyzeGS2(state->source, enabledApiFunctions, !lspEnabled || (!apiDefinitionsLoading && apiDefinitionsLoaded && !apiDefinitions.empty()));
        for (const GS2Diagnostic& diagnostic : state->diagnostics) {
            const std::size_t startByte = std::min(diagnostic.start, state->source.size());
            const std::size_t endByte = std::min(diagnostic.end, state->source.size());
            const gint startOffset = static_cast<gint>(g_utf8_pointer_to_offset(state->source.c_str(), state->source.c_str() + startByte));
            const gint endOffset = static_cast<gint>(g_utf8_pointer_to_offset(state->source.c_str(), state->source.c_str() + endByte));
            GtkTextIter start;
            GtkTextIter end;
            gtk_text_buffer_get_iter_at_offset(buffer, &start, startOffset);
            gtk_text_buffer_get_iter_at_offset(buffer, &end, std::max(startOffset + 1, endOffset));
            GtkTextTag* tag = diagnostic.severity == GS2DiagnosticSeverity::Error ? state->errorTag : diagnostic.severity == GS2DiagnosticSeverity::Warning ? state->warningTag : state->infoTag;
            gtk_text_buffer_apply_tag(buffer, tag, &start, &end);
        }
    }

    gboolean runScheduledEditorDiagnostics(gpointer data) {
        EditorDiagnosticsState* state = static_cast<EditorDiagnosticsState*>(data);
        state->timeout = 0;
        runEditorDiagnostics(state);
        return G_SOURCE_REMOVE;
    }

    void scheduleEditorDiagnostics(EditorDiagnosticsState* state) {
        if (state->timeout != 0) g_source_remove(state->timeout);
        state->timeout = g_timeout_add(180, runScheduledEditorDiagnostics, state);
    }

    gboolean resumeEditorAfterBulkInsert(gpointer data) {
        EditorBulkInsertState* state = static_cast<EditorBulkInsertState*>(data);
        state->resumeTimer = 0;
        state->active = false;
        state->pendingBytes = 0;
        state->pendingLines = 0;
        gtk_source_buffer_set_highlight_syntax(state->buffer, syntaxHighlighting);
        gtk_source_buffer_set_highlight_matching_brackets(state->buffer, showBrackets);
        gtk_widget_queue_draw(state->editor);
        EditorDiagnosticsState* diagnostics = diagnosticsState(state->editor);
        if (diagnostics != nullptr && scriptDiagnosticsEnabled) scheduleEditorDiagnostics(diagnostics);
        return G_SOURCE_REMOVE;
    }

    void scheduleBulkInsertResume(EditorBulkInsertState* state) {
        if (state->resumeTimer != 0) g_source_remove(state->resumeTimer);
        state->resumeTimer = g_timeout_add(220, resumeEditorAfterBulkInsert, state);
    }

    void onEditorInsertText(GtkTextBuffer*, GtkTextIter*, gchar* text, gint length, gpointer data) {
        EditorBulkInsertState* state = static_cast<EditorBulkInsertState*>(data);
        if (length <= 0) return;
        state->pendingBytes += static_cast<std::size_t>(length);
        state->pendingLines += static_cast<std::size_t>(std::count(text, text + length, '\n'));
        if (!state->active && state->pendingBytes < bulkInsertBytes && state->pendingLines < bulkInsertLines) return;
        if (!state->active) {
            state->active = true;
            gtk_source_buffer_set_highlight_syntax(state->buffer, false);
            gtk_source_buffer_set_highlight_matching_brackets(state->buffer, false);
            EditorDiagnosticsState* diagnostics = diagnosticsState(state->editor);
            if (diagnostics != nullptr && diagnostics->timeout != 0) { g_source_remove(diagnostics->timeout); diagnostics->timeout = 0; }
        }
        scheduleBulkInsertResume(state);
    }

    bool showDiagnosticTooltip(GtkWidget* editor, const GtkTextIter& iter, GtkTooltip* tooltip) {
        EditorDiagnosticsState* state = diagnosticsState(editor);
        if (state == nullptr || state->diagnostics.empty()) return false;
        const gint offset = gtk_text_iter_get_offset(&iter);
        std::string messages;
        for (const GS2Diagnostic& diagnostic : state->diagnostics) {
            const gint start = static_cast<gint>(g_utf8_pointer_to_offset(state->source.c_str(), state->source.c_str() + std::min(diagnostic.start, state->source.size())));
            const gint end = static_cast<gint>(g_utf8_pointer_to_offset(state->source.c_str(), state->source.c_str() + std::min(diagnostic.end, state->source.size())));
            if (offset < start || offset > std::max(start, end)) continue;
            if (!messages.empty()) messages += '\n';
            messages += diagnostic.severity == GS2DiagnosticSeverity::Error ? "Script analysis — Error: " : diagnostic.severity == GS2DiagnosticSeverity::Warning ? "Script analysis — Warning: " : "Script analysis — Info: ";
            messages += diagnostic.message;
        }
        if (messages.empty()) return false;
        gtk_tooltip_set_text(tooltip, messages.c_str());
        return true;
    }

    gboolean requeryEditorTooltip(gpointer data) {
        GtkWidget* editor = GTK_WIDGET(data);
        gtk_widget_trigger_tooltip_query(editor);
        g_object_unref(editor);
        return G_SOURCE_REMOVE;
    }

    void preserveEditorTooltip(GtkWidget* editor) { g_timeout_add(75, requeryEditorTooltip, g_object_ref(editor)); }

    EditorMultiSelectionState* multiSelectionState(GtkWidget* editor) {
        const auto state = std::find_if(multiSelectionStates.begin(), multiSelectionStates.end(), [editor](const auto& value) { return value->editor == editor; });
        return state == multiSelectionStates.end() ? nullptr : state->get();
    }

    void refreshMultiSelections(EditorMultiSelectionState* state) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor));
        GtkTextIter begin;
        GtkTextIter end;
        gtk_text_buffer_get_bounds(buffer, &begin, &end);
        gtk_text_buffer_remove_tag(buffer, state->tag, &begin, &end);
        for (const EditorSelection& selection : state->selections) {
            GtkTextIter anchor;
            GtkTextIter caret;
            gtk_text_buffer_get_iter_at_mark(buffer, &anchor, selection.anchor);
            gtk_text_buffer_get_iter_at_mark(buffer, &caret, selection.caret);
            if (gtk_text_iter_compare(&anchor, &caret) > 0) std::swap(anchor, caret);
            if (!gtk_text_iter_equal(&anchor, &caret)) gtk_text_buffer_apply_tag(buffer, state->tag, &anchor, &caret);
        }
        gtk_widget_queue_draw(state->editor);
    }

    void clearMultiSelections(EditorMultiSelectionState* state) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor));
        for (const EditorSelection& selection : state->selections) {
            gtk_text_buffer_delete_mark(buffer, selection.anchor);
            gtk_text_buffer_delete_mark(buffer, selection.caret);
        }
        state->selections.clear();
        refreshMultiSelections(state);
    }

    void addMultiSelection(EditorMultiSelectionState* state, const GtkTextIter& anchor, const GtkTextIter& caret) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor));
        GtkTextIter primaryAnchor;
        GtkTextIter primaryCaret;
        gtk_text_buffer_get_iter_at_mark(buffer, &primaryAnchor, gtk_text_buffer_get_selection_bound(buffer));
        gtk_text_buffer_get_iter_at_mark(buffer, &primaryCaret, gtk_text_buffer_get_insert(buffer));
        if (gtk_text_iter_equal(&primaryAnchor, &anchor) && gtk_text_iter_equal(&primaryCaret, &caret)) return;
        for (const EditorSelection& selection : state->selections) {
            GtkTextIter existingAnchor;
            GtkTextIter existingCaret;
            gtk_text_buffer_get_iter_at_mark(buffer, &existingAnchor, selection.anchor);
            gtk_text_buffer_get_iter_at_mark(buffer, &existingCaret, selection.caret);
            if (gtk_text_iter_equal(&existingAnchor, &anchor) && gtk_text_iter_equal(&existingCaret, &caret)) return;
        }
        state->selections.push_back({gtk_text_buffer_create_mark(buffer, nullptr, &anchor, true), gtk_text_buffer_create_mark(buffer, nullptr, &caret, false)});
        refreshMultiSelections(state);
    }

    gboolean drawMultiSelections(GtkWidget* editor, cairo_t* cairo, gpointer data) {
        EditorMultiSelectionState* state = static_cast<EditorMultiSelectionState*>(data);
        if (state->selections.empty()) return FALSE;
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
        GdkRGBA color{};
        gtk_style_context_get_color(gtk_widget_get_style_context(editor), GTK_STATE_FLAG_SELECTED, &color);
        gdk_cairo_set_source_rgba(cairo, &color);
        cairo_set_line_width(cairo, 1.5);
        for (const EditorSelection& selection : state->selections) {
            GtkTextIter anchor;
            GtkTextIter caret;
            gtk_text_buffer_get_iter_at_mark(buffer, &anchor, selection.anchor);
            gtk_text_buffer_get_iter_at_mark(buffer, &caret, selection.caret);
            if (!gtk_text_iter_equal(&anchor, &caret)) continue;
            GdkRectangle location{};
            gtk_text_view_get_iter_location(GTK_TEXT_VIEW(editor), &caret, &location);
            if (location.height <= 0) continue;
            gint x = 0;
            gint y = 0;
            gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(editor), GTK_TEXT_WINDOW_WIDGET, location.x, location.y, &x, &y);
            cairo_move_to(cairo, x + 0.5, y);
            cairo_line_to(cairo, x + 0.5, y + location.height);
        }
        cairo_stroke(cairo);
        return FALSE;
    }

    bool replaceMultiSelections(EditorMultiSelectionState* state, const std::string& text, int eraseDirection) {
        if (state->selections.empty()) return false;
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor));
        struct Operation { GtkTextMark* anchor; GtkTextMark* caret; gint offset; };
        std::vector<Operation> operations;
        GtkTextIter primaryAnchor;
        GtkTextIter primaryCaret;
        gtk_text_buffer_get_iter_at_mark(buffer, &primaryAnchor, gtk_text_buffer_get_selection_bound(buffer));
        gtk_text_buffer_get_iter_at_mark(buffer, &primaryCaret, gtk_text_buffer_get_insert(buffer));
        GtkTextMark* primaryAnchorMark = gtk_text_buffer_create_mark(buffer, nullptr, &primaryAnchor, true);
        GtkTextMark* primaryCaretMark = gtk_text_buffer_create_mark(buffer, nullptr, &primaryCaret, false);
        operations.push_back({primaryAnchorMark, primaryCaretMark, std::max(gtk_text_iter_get_offset(&primaryAnchor), gtk_text_iter_get_offset(&primaryCaret))});
        for (const EditorSelection& selection : state->selections) {
            GtkTextIter anchor;
            GtkTextIter caret;
            gtk_text_buffer_get_iter_at_mark(buffer, &anchor, selection.anchor);
            gtk_text_buffer_get_iter_at_mark(buffer, &caret, selection.caret);
            operations.push_back({selection.anchor, selection.caret, std::max(gtk_text_iter_get_offset(&anchor), gtk_text_iter_get_offset(&caret))});
        }
        std::sort(operations.begin(), operations.end(), [](const Operation& left, const Operation& right) { return left.offset > right.offset; });
        state->applying = true;
        gtk_text_buffer_begin_user_action(buffer);
        for (const Operation& operation : operations) {
            GtkTextIter anchor;
            GtkTextIter caret;
            gtk_text_buffer_get_iter_at_mark(buffer, &anchor, operation.anchor);
            gtk_text_buffer_get_iter_at_mark(buffer, &caret, operation.caret);
            GtkTextIter start = anchor;
            GtkTextIter end = caret;
            if (gtk_text_iter_compare(&start, &end) > 0) std::swap(start, end);
            if (gtk_text_iter_equal(&start, &end)) {
                if (eraseDirection < 0) gtk_text_iter_backward_cursor_position(&start);
                else if (eraseDirection > 0) gtk_text_iter_forward_cursor_position(&end);
            }
            if (!gtk_text_iter_equal(&start, &end)) gtk_text_buffer_delete(buffer, &start, &end);
            if (!text.empty()) gtk_text_buffer_insert(buffer, &start, text.c_str(), static_cast<gint>(text.size()));
            gtk_text_buffer_move_mark(buffer, operation.anchor, &start);
            gtk_text_buffer_move_mark(buffer, operation.caret, &start);
        }
        gtk_text_buffer_end_user_action(buffer);
        gtk_text_buffer_get_iter_at_mark(buffer, &primaryCaret, primaryCaretMark);
        gtk_text_buffer_select_range(buffer, &primaryCaret, &primaryCaret);
        gtk_text_buffer_delete_mark(buffer, primaryAnchorMark);
        gtk_text_buffer_delete_mark(buffer, primaryCaretMark);
        state->applying = false;
        refreshMultiSelections(state);
        return true;
    }

    bool addAdjacentCaret(EditorMultiSelectionState* state, int direction, bool extend) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor));
        GtkTextIter source;
        gtk_text_buffer_get_iter_at_mark(buffer, &source, gtk_text_buffer_get_insert(buffer));
        for (const EditorSelection& selection : state->selections) {
            GtkTextIter candidate;
            gtk_text_buffer_get_iter_at_mark(buffer, &candidate, selection.caret);
            if ((direction > 0 && gtk_text_iter_get_line(&candidate) > gtk_text_iter_get_line(&source)) || (direction < 0 && gtk_text_iter_get_line(&candidate) < gtk_text_iter_get_line(&source))) source = candidate;
        }
        const gint column = gtk_text_iter_get_line_offset(&source);
        if (direction > 0 ? !gtk_text_iter_forward_line(&source) : !gtk_text_iter_backward_line(&source)) return true;
        gtk_text_iter_set_line_offset(&source, std::min(column, gtk_text_iter_get_chars_in_line(&source)));
        GtkTextIter anchor = source;
        if (extend) {
            gtk_text_buffer_get_iter_at_mark(buffer, &anchor, gtk_text_buffer_get_selection_bound(buffer));
            if (!state->selections.empty()) gtk_text_buffer_get_iter_at_mark(buffer, &anchor, state->selections.back().anchor);
        }
        addMultiSelection(state, anchor, source);
        return true;
    }

    bool selectNextOccurrence(EditorMultiSelectionState* state) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor));
        GtkTextIter start;
        GtkTextIter end;
        if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
            gtk_text_buffer_get_iter_at_mark(buffer, &start, gtk_text_buffer_get_insert(buffer));
            end = start;
            if (!gtk_text_iter_starts_word(&start)) gtk_text_iter_backward_word_start(&start);
            if (!gtk_text_iter_ends_word(&end)) gtk_text_iter_forward_word_end(&end);
            if (gtk_text_iter_equal(&start, &end)) return true;
            gtk_text_buffer_select_range(buffer, &start, &end);
        }
        gchar* selected = gtk_text_buffer_get_text(buffer, &start, &end, false);
        GtkTextIter search = end;
        for (const EditorSelection& selection : state->selections) {
            GtkTextIter caret;
            gtk_text_buffer_get_iter_at_mark(buffer, &caret, selection.caret);
            if (gtk_text_iter_compare(&caret, &search) > 0) search = caret;
        }
        GtkTextIter matchStart;
        GtkTextIter matchEnd;
        gboolean found = gtk_text_iter_forward_search(&search, selected, GTK_TEXT_SEARCH_TEXT_ONLY, &matchStart, &matchEnd, nullptr);
        if (!found) {
            gtk_text_buffer_get_start_iter(buffer, &search);
            found = gtk_text_iter_forward_search(&search, selected, GTK_TEXT_SEARCH_TEXT_ONLY, &matchStart, &matchEnd, &start);
        }
        if (found) addMultiSelection(state, matchStart, matchEnd);
        g_free(selected);
        return true;
    }

    gboolean multiSelectionButtonPress(GtkWidget* editor, GdkEventButton* event, gpointer data) {
        EditorMultiSelectionState* state = static_cast<EditorMultiSelectionState*>(data);
        if (event->button != 1) return FALSE;
        if ((event->state & GDK_MOD1_MASK) == 0) { clearMultiSelections(state); return FALSE; }
        gint bufferX = 0;
        gint bufferY = 0;
        gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(editor), GTK_TEXT_WINDOW_WIDGET, static_cast<gint>(event->x), static_cast<gint>(event->y), &bufferX, &bufferY);
        GtkTextIter caret;
        gint trailing = 0;
        gtk_text_view_get_iter_at_position(GTK_TEXT_VIEW(editor), &caret, &trailing, bufferX, bufferY);
        if (trailing > 0) gtk_text_iter_forward_chars(&caret, trailing);
        GtkTextIter anchor = caret;
        if ((event->state & GDK_SHIFT_MASK) != 0 && !state->selections.empty()) {
            GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
            gtk_text_buffer_move_mark(buffer, state->selections.back().caret, &caret);
            refreshMultiSelections(state);
            return TRUE;
        }
        addMultiSelection(state, anchor, caret);
        return TRUE;
    }

    gboolean multiSelectionKeyPress(GtkWidget* editor, GdkEventKey* event, gpointer data) {
        EditorMultiSelectionState* state = static_cast<EditorMultiSelectionState*>(data);
        preserveEditorTooltip(editor);
        const bool control = (event->state & GDK_CONTROL_MASK) != 0;
        const bool alt = (event->state & GDK_MOD1_MASK) != 0;
        if (event->keyval == GDK_KEY_Escape && !state->selections.empty()) { clearMultiSelections(state); return TRUE; }
        if (control && alt && (event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_Down)) return addAdjacentCaret(state, event->keyval == GDK_KEY_Down ? 1 : -1, (event->state & GDK_SHIFT_MASK) != 0);
        if (control && !alt && (event->keyval == GDK_KEY_d || event->keyval == GDK_KEY_D)) return selectNextOccurrence(state);
        if (state->selections.empty() || control || alt) return FALSE;
        if (event->keyval == GDK_KEY_BackSpace) return replaceMultiSelections(state, "", -1);
        if (event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_KP_Delete) return replaceMultiSelections(state, "", 1);
        if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) return replaceMultiSelections(state, "\n", 0);
        if (event->keyval == GDK_KEY_Tab || event->keyval == GDK_KEY_ISO_Left_Tab) return replaceMultiSelections(state, useTabs ? "\t" : std::string(tabWidth, ' '), 0);
        const gunichar character = gdk_keyval_to_unicode(event->keyval);
        if (character == 0 || g_unichar_iscntrl(character)) return FALSE;
        gchar encoded[7]{};
        const gint length = g_unichar_to_utf8(character, encoded);
        return replaceMultiSelections(state, std::string(encoded, length), 0);
    }

    void skipWhitespace(const std::string& text, std::size_t& position) {
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0) ++position;
    }

    bool readUnicodeEscape(const std::string& text, std::size_t& position, std::string& value) {
        if (position + 4 > text.size()) return false;
        std::uint32_t codepoint = 0;
        for (int index = 0; index < 4; ++index) {
            const char character = text[position++];
            codepoint <<= 4;
            if (character >= '0' && character <= '9') codepoint += static_cast<std::uint32_t>(character - '0');
            else if (character >= 'a' && character <= 'f') codepoint += static_cast<std::uint32_t>(character - 'a' + 10);
            else if (character >= 'A' && character <= 'F') codepoint += static_cast<std::uint32_t>(character - 'A' + 10);
            else return false;
        }
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && position + 6 <= text.size() && text[position] == '\\' && text[position + 1] == 'u') {
            std::uint32_t low = 0;
            bool validLow = true;
            for (int index = 0; index < 4; ++index) {
                const char character = text[position + 2 + index];
                low <<= 4;
                if (character >= '0' && character <= '9') low += static_cast<std::uint32_t>(character - '0');
                else if (character >= 'a' && character <= 'f') low += static_cast<std::uint32_t>(character - 'a' + 10);
                else if (character >= 'A' && character <= 'F') low += static_cast<std::uint32_t>(character - 'A' + 10);
                else { validLow = false; break; }
            }
            if (validLow && low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                position += 6;
            }
        }
        if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) codepoint = 0xFFFD;
        gchar encoded[7]{};
        const gint length = g_unichar_to_utf8(static_cast<gunichar>(codepoint), encoded);
        value.append(encoded, static_cast<std::size_t>(length));
        return true;
    }

    bool readString(const std::string& text, std::size_t& position, std::string& value) {
        if (position >= text.size() || text[position++] != '"') return false;
        value.clear();
        while (position < text.size()) {
            const char character = text[position++];
            if (character == '"') return true;
            if (character != '\\' || position >= text.size()) { value += character; continue; }
            const char escaped = text[position++];
            if (escaped == 'n') value += '\n';
            else if (escaped == 'r') value += '\r';
            else if (escaped == 't') value += '\t';
            else if (escaped == 'b') value += '\b';
            else if (escaped == 'f') value += '\f';
            else if (escaped == 'u') { if (!readUnicodeEscape(text, position, value)) return false; }
            else value += escaped;
        }
        return false;
    }

    bool skipValue(const std::string& text, std::size_t& position) {
        skipWhitespace(text, position);
        if (position >= text.size()) return false;
        if (text[position] == '"') { std::string ignored; return readString(text, position, ignored); }
        if (text[position] != '{' && text[position] != '[') {
            while (position < text.size() && text[position] != ',' && text[position] != '}' && text[position] != ']') ++position;
            return true;
        }
        const char open = text[position++];
        const char close = open == '{' ? '}' : ']';
        while (position < text.size()) {
            skipWhitespace(text, position);
            if (position < text.size() && text[position] == close) { ++position; return true; }
            if (!skipValue(text, position)) return false;
            skipWhitespace(text, position);
            if (position < text.size() && text[position] == ',') ++position;
        }
        return false;
    }

    bool readStringArray(const std::string& text, std::size_t& position, std::vector<std::string>& values) {
        skipWhitespace(text, position);
        if (position >= text.size() || text[position++] != '[') return false;
        values.clear();
        while (position < text.size()) {
            skipWhitespace(text, position);
            if (position < text.size() && text[position] == ']') { ++position; return true; }
            std::string value;
            if (!readString(text, position, value)) return false;
            values.push_back(std::move(value));
            skipWhitespace(text, position);
            if (position < text.size() && text[position] == ',') ++position;
        }
        return false;
    }

    void splitParameterSignature(const std::string& signature, std::vector<std::string>& values) {
        std::size_t start = 0;
        int nested = 0;
        char quote = 0;
        bool escaped = false;
        for (std::size_t index = 0; index <= signature.size(); ++index) {
            const char character = index < signature.size() ? signature[index] : ',';
            if (quote != 0) {
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == quote) quote = 0;
                continue;
            }
            if (character == '"' || character == '\'') { quote = character; continue; }
            if (character == '(' || character == '[' || character == '{') ++nested;
            else if (character == ')' || character == ']' || character == '}') nested = std::max(0, nested - 1);
            else if (character == ',' && nested == 0) {
                const std::size_t first = signature.find_first_not_of(" \t\r\n", start);
                const std::size_t last = signature.find_last_not_of(" \t\r\n", index == 0 ? 0 : index - 1);
                if (first != std::string::npos && first < index && last >= first) values.push_back(signature.substr(first, last - first + 1));
                start = index + 1;
            }
        }
    }

    bool readParameterArray(const std::string& text, std::size_t& position, std::vector<std::string>& values) {
        skipWhitespace(text, position);
        if (position >= text.size() || text[position++] != '[') return false;
        values.clear();
        while (position < text.size()) {
            skipWhitespace(text, position);
            if (position < text.size() && text[position] == ']') { ++position; return true; }
            if (position < text.size() && text[position] == '"') {
                std::string value;
                if (!readString(text, position, value)) return false;
                if (!value.empty()) values.push_back(std::move(value));
            } else if (position < text.size() && text[position] == '{') {
                ++position;
                std::string name;
                std::string label;
                std::string type;
                while (position < text.size()) {
                    skipWhitespace(text, position);
                    if (position < text.size() && text[position] == '}') { ++position; break; }
                    std::string key;
                    if (!readString(text, position, key)) return false;
                    skipWhitespace(text, position);
                    if (position >= text.size() || text[position++] != ':') return false;
                    skipWhitespace(text, position);
                    if (key == "name" || key == "label" || key == "type") {
                        std::string value;
                        if (!readString(text, position, value)) return false;
                        if (key == "name") name = std::move(value);
                        else if (key == "label") label = std::move(value);
                        else type = std::move(value);
                    } else if (!skipValue(text, position)) return false;
                    skipWhitespace(text, position);
                    if (position < text.size() && text[position] == ',') ++position;
                }
                if (!name.empty()) values.push_back(std::move(name));
                else if (!label.empty()) values.push_back(std::move(label));
                else if (!type.empty()) values.push_back(std::move(type));
            } else if (!skipValue(text, position)) return false;
            skipWhitespace(text, position);
            if (position < text.size() && text[position] == ',') ++position;
        }
        return false;
    }

    bool readParameterValue(const std::string& text, std::size_t& position, std::vector<std::string>& values) {
        skipWhitespace(text, position);
        if (position >= text.size()) return false;
        if (text[position] == '[') return readParameterArray(text, position, values);
        if (text[position] == '"') {
            std::string signature;
            if (!readString(text, position, signature)) return false;
            values.clear();
            splitParameterSignature(signature, values);
            return true;
        }
        return skipValue(text, position);
    }

    bool readDefinition(const std::string& text, std::size_t& position, ApiDefinition& definition) {
        skipWhitespace(text, position);
        if (position >= text.size() || text[position++] != '{') return false;
        while (position < text.size()) {
            skipWhitespace(text, position);
            if (position < text.size() && text[position] == '}') { ++position; return true; }
            std::string key;
            if (!readString(text, position, key)) return false;
            skipWhitespace(text, position);
            if (position >= text.size() || text[position++] != ':') return false;
            skipWhitespace(text, position);
            if (key == "params" || key == "parameters" || key == "args" || key == "arguments") { if (!readParameterValue(text, position, definition.params)) return false; }
            else if (key == "name" || key == "type" || key == "returns" || key == "scope" || key == "description" || key == "example") {
                std::string value;
                if (!readString(text, position, value)) return false;
                if (key == "name") definition.name = std::move(value);
                else if (key == "type") definition.type = std::move(value);
                else if (key == "returns") definition.returns = std::move(value);
                else if (key == "scope") definition.scope = std::move(value);
                else if (key == "description") definition.description = std::move(value);
                else definition.example = std::move(value);
            } else if (!skipValue(text, position)) return false;
            skipWhitespace(text, position);
            if (position < text.size() && text[position] == ',') ++position;
        }
        return false;
    }

    std::vector<ApiDefinition> completionDefinitions(const std::string& json) {
        std::vector<ApiDefinition> definitions;
        std::size_t position = 0;
        skipWhitespace(json, position);
        if (position >= json.size()) return definitions;
        if (json[position] == '[') {
            ++position;
            while (position < json.size()) {
                skipWhitespace(json, position);
                if (position < json.size() && json[position] == ']') { ++position; break; }
                ApiDefinition definition;
                if (!readDefinition(json, position, definition)) return {};
                if (!definition.name.empty()) definitions.push_back(std::move(definition));
                skipWhitespace(json, position);
                if (position < json.size() && json[position] == ',') ++position;
            }
        } else {
            if (json[position++] != '{') return definitions;
        while (position < json.size()) {
            skipWhitespace(json, position);
            if (position < json.size() && json[position] == '}') break;
            std::string name;
            if (!readString(json, position, name)) return {};
            skipWhitespace(json, position);
            if (position >= json.size() || json[position++] != ':') return {};
            ApiDefinition definition;
            if (!readDefinition(json, position, definition)) return {};
            if (definition.name.empty()) definition.name = std::move(name);
            if (!definition.name.empty()) definitions.push_back(std::move(definition));
            skipWhitespace(json, position);
            if (position < json.size() && json[position] == ',') ++position;
        }
        }
        std::sort(definitions.begin(), definitions.end(), [](const ApiDefinition& left, const ApiDefinition& right) { return left.name < right.name; });
        return definitions;
    }

    void cacheCompletionDefinitions(const std::string& json) {
        if (completionCacheFile.empty()) return;
        std::error_code error;
        std::filesystem::create_directories(completionCacheFile.parent_path(), error);
        std::ofstream stream(completionCacheFile, std::ios::binary | std::ios::trunc);
        if (stream) stream.write(json.data(), static_cast<std::streamsize>(json.size()));
    }

    std::vector<ApiDefinition> cachedCompletionDefinitions() {
        if (completionCacheFile.empty()) return {};
        std::ifstream stream(completionCacheFile, std::ios::binary);
        if (!stream) return {};
        const std::string json((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        return completionDefinitions(json);
    }

    std::string decodeChunkedBody(const std::string& body) {
        std::string result;
        std::size_t position = 0;
        while (position < body.size()) {
            const std::size_t lineEnd = body.find("\r\n", position);
            if (lineEnd == std::string::npos) return "";
            std::size_t length = 0;
            for (std::size_t index = position; index < lineEnd && body[index] != ';'; ++index) {
                const char character = body[index];
                if (character >= '0' && character <= '9') length = length * 16 + static_cast<std::size_t>(character - '0');
                else if (character >= 'a' && character <= 'f') length = length * 16 + static_cast<std::size_t>(character - 'a' + 10);
                else if (character >= 'A' && character <= 'F') length = length * 16 + static_cast<std::size_t>(character - 'A' + 10);
                else return "";
            }
            position = lineEnd + 2;
            if (length == 0) return result;
            if (position + length + 2 > body.size()) return "";
            result.append(body, position, length);
            position += length + 2;
        }
        return "";
    }

    std::vector<ApiDefinition> fetchCompletionDefinitionsFromSource(const std::string& source) {
        if (source.rfind("http://", 0) != 0 && source.rfind("https://", 0) != 0) {
            gchar* contents = nullptr;
            gsize length = 0;
            GError* error = nullptr;
            std::string path = source;
            if (source.rfind("file://", 0) == 0) {
                gchar* localPath = g_filename_from_uri(source.c_str(), nullptr, &error);
                if (localPath == nullptr) { if (error != nullptr) g_error_free(error); return {}; }
                path = localPath;
                g_free(localPath);
            }
            if (!g_file_get_contents(path.c_str(), &contents, &length, &error)) { if (error != nullptr) g_error_free(error); return {}; }
            const std::string json(contents, length);
            const auto result = completionDefinitions(json);
            g_free(contents);
            if (!result.empty()) cacheCompletionDefinitions(json);
            return result;
        }
        GError* error = nullptr;
        GUri* uri = g_uri_parse(source.c_str(), G_URI_FLAGS_NONE, &error);
        if (uri == nullptr) { if (error != nullptr) g_error_free(error); return {}; }
        std::unique_ptr<GUri, decltype(&g_uri_unref)> uriGuard(uri, g_uri_unref);
        const char* scheme = g_uri_get_scheme(uri);
        const char* host = g_uri_get_host(uri);
        if (scheme == nullptr || host == nullptr || (std::string(scheme) != "http" && std::string(scheme) != "https")) return {};
        const bool secure = std::string(scheme) == "https";
        const int port = g_uri_get_port(uri) < 0 ? (secure ? 443 : 80) : g_uri_get_port(uri);
        const std::string endpoint = std::string(host) + ':' + std::to_string(port);
        const char* path = g_uri_get_path(uri);
        const char* query = g_uri_get_query(uri);
        std::string requestPath = path == nullptr || *path == '\0' ? "/" : path;
        if (query != nullptr && *query != '\0') requestPath += '?' + std::string(query);
        SSL_CTX* context = secure ? SSL_CTX_new(TLS_client_method()) : nullptr;
        if (secure && context == nullptr) return {};
        std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> contextGuard(context, SSL_CTX_free);
        if (secure && (SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr), SSL_CTX_set_default_verify_paths(context) != 1)) return {};
        BIO* connection = secure ? BIO_new_ssl_connect(context) : BIO_new_connect(endpoint.c_str());
        if (connection == nullptr) return {};
        std::unique_ptr<BIO, decltype(&BIO_free_all)> connectionGuard(connection, BIO_free_all);
        BIO_set_conn_hostname(connection, endpoint.c_str());
        SSL* ssl = nullptr;
        if (secure) {
            BIO_get_ssl(connection, &ssl);
            if (ssl == nullptr || SSL_set_tlsext_host_name(ssl, host) != 1) return {};
        }
        if (BIO_do_connect(connection) != 1 || (secure && SSL_get_verify_result(ssl) != X509_V_OK)) return {};
        const std::string request = "GET " + requestPath + " HTTP/1.1\r\nHost: " + host + "\r\nUser-Agent: RemoteControl/1.0\r\nAccept: application/json\r\nConnection: close\r\n\r\n";
        if (BIO_write(connection, request.data(), static_cast<int>(request.size())) != static_cast<int>(request.size())) return {};
        std::string response;
        char buffer[8192];
        for (int count; (count = BIO_read(connection, buffer, sizeof(buffer))) > 0;) response.append(buffer, count);
        const std::size_t body = response.find("\r\n\r\n");
        if (body == std::string::npos) return {};
        const std::string payload = response.substr(body + 4);
        std::string headers = response.substr(0, body);
        std::transform(headers.begin(), headers.end(), headers.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        const std::string json = headers.find("transfer-encoding: chunked") == std::string::npos ? payload : decodeChunkedBody(payload);
        const auto result = completionDefinitions(json);
        if (!result.empty()) cacheCompletionDefinitions(json);
        return result;
    }

    std::vector<ApiDefinition> fetchCompletionDefinitions(const std::string& source) {
        const auto definitions = fetchCompletionDefinitionsFromSource(source);
        return definitions.empty() ? cachedCompletionDefinitions() : definitions;
    }

    gboolean applyCompletionText(gpointer data) {
        const auto* payload = static_cast<CompletionPayload*>(data);
        if (payload->request == completionRequest) {
            apiDefinitions = payload->definitions;
            for (const ApiDefinition& reference : referenceApiDefinitions()) {
                const auto existing = std::find_if(apiDefinitions.begin(), apiDefinitions.end(), [&reference](const ApiDefinition& definition) { return lowerText(definition.name) == lowerText(reference.name); });
                if (existing == apiDefinitions.end()) apiDefinitions.push_back(reference);
            }
            apiDefinitionsLoaded = !payload->definitions.empty();
            apiDefinitionsLoading = false;
            for (const auto& state : diagnosticsStates) if (scriptDiagnosticsEnabled) runEditorDiagnostics(state.get());
        }
        return G_SOURCE_REMOVE;
    }

    void startCompletionLoad() {
        const unsigned int request = ++completionRequest;
        apiDefinitionsLoading = true;
        apiDefinitionsLoaded = false;
        const std::string source = completionSource;
        std::thread([request, source] {
            auto* payload = new CompletionPayload{request, fetchCompletionDefinitions(source)};
            g_idle_add_full(G_PRIORITY_DEFAULT, applyCompletionText, payload, +[](gpointer data) { delete static_cast<CompletionPayload*>(data); });
        }).detach();
    }

    std::string lowerText(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    std::string upperCase(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
        return value;
    }

    bool identifierCharacter(gunichar character) { return g_unichar_isalnum(character) || character == '_' || character == '$'; }

    std::string completionPrefix(GtkTextIter iter) {
        GtkTextIter start = iter;
        while (!gtk_text_iter_starts_line(&start)) {
            GtkTextIter previous = start;
            if (!gtk_text_iter_backward_char(&previous) || !identifierCharacter(gtk_text_iter_get_char(&previous))) break;
            start = previous;
        }
        gchar* value = gtk_text_iter_get_text(&start, &iter);
        std::string result = value == nullptr ? "" : value;
        g_free(value);
        return result;
    }

    bool completionInsideString(GtkTextIter iter) {
        GtkTextIter start = iter;
        gtk_text_iter_set_line_offset(&start, 0);
        gchar* value = gtk_text_iter_get_text(&start, &iter);
        gunichar quote = 0;
        bool escaped = false;
        for (const char* cursor = value; cursor != nullptr && *cursor != '\0'; cursor = g_utf8_next_char(cursor)) {
            const gunichar character = g_utf8_get_char(cursor);
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (quote == 0 && (character == '"' || character == '\'')) quote = character;
            else if (character == quote) quote = 0;
        }
        g_free(value);
        return quote != 0;
    }

    std::vector<std::string> localIdentifiers(GtkWidget* editor) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
        GtkTextIter start;
        GtkTextIter end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, false);
        std::set<std::string> seen;
        std::vector<std::string> names;
        for (const char* cursor = text; cursor != nullptr && *cursor != '\0'; ) {
            const gunichar character = g_utf8_get_char(cursor);
            if (!(g_unichar_isalpha(character) || character == '_' || character == '$')) { cursor = g_utf8_next_char(cursor); continue; }
            const char* wordStart = cursor;
            cursor = g_utf8_next_char(cursor);
            while (*cursor != '\0') {
                while (*cursor != '\0' && identifierCharacter(g_utf8_get_char(cursor))) cursor = g_utf8_next_char(cursor);
                if (*cursor != '.' || !identifierCharacter(g_utf8_get_char(g_utf8_next_char(cursor)))) break;
                cursor = g_utf8_next_char(cursor);
            }
            const std::string name(wordStart, cursor - wordStart);
            if (seen.insert(lowerText(name)).second) names.push_back(name);
        }
        g_free(text);
        return names;
    }

    std::vector<ApiDefinition> localFunctionDefinitions(GtkWidget* editor) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
        GtkTextIter start;
        GtkTextIter end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, false);
        std::string source = text == nullptr ? "" : text;
        g_free(text);
        std::vector<ApiDefinition> definitions;
        std::set<std::string> seen;
        std::size_t lineStart = 0;
        while (lineStart <= source.size()) {
            const std::size_t lineEnd = source.find('\n', lineStart);
            const std::string line = source.substr(lineStart, lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart);
            const std::size_t marker = line.find("function ");
            if (marker != std::string::npos && (marker == 0 || line[marker - 1] != '/')) {
                std::size_t nameStart = marker + 9;
                while (nameStart < line.size() && std::isspace(static_cast<unsigned char>(line[nameStart]))) ++nameStart;
                std::size_t nameEnd = nameStart;
                while (nameEnd < line.size() && (std::isalnum(static_cast<unsigned char>(line[nameEnd])) || line[nameEnd] == '_' || line[nameEnd] == '$')) ++nameEnd;
                const std::size_t open = line.find('(', nameEnd);
                const std::size_t close = open == std::string::npos ? std::string::npos : line.find(')', open + 1);
                if (nameEnd > nameStart && open != std::string::npos && close != std::string::npos) {
                    ApiDefinition definition;
                    definition.name = line.substr(nameStart, nameEnd - nameStart);
                    if (seen.insert(lowerText(definition.name)).second) {
                        std::size_t parameterStart = open + 1;
                        while (parameterStart < close) {
                            const std::size_t comma = line.find(',', parameterStart);
                            const std::size_t parameterEnd = comma == std::string::npos || comma > close ? close : comma;
                            std::string parameter = line.substr(parameterStart, parameterEnd - parameterStart);
                            const std::size_t first = parameter.find_first_not_of(" \t");
                            const std::size_t last = parameter.find_last_not_of(" \t");
                            if (first != std::string::npos) definition.params.push_back(parameter.substr(first, last - first + 1));
                            parameterStart = parameterEnd + 1;
                        }
                        definition.type = "Function";
                        definition.scope = source.find("//#CLIENTSIDE") != std::string::npos && source.find("//#SERVERSIDE") == std::string::npos ? "clientside" : "script";
                        definition.description = "Function defined in the current script.";
                        definitions.push_back(std::move(definition));
                    }
                }
            }
            if (lineEnd == std::string::npos) break;
            lineStart = lineEnd + 1;
        }
        return definitions;
    }

    const ApiDefinition* findDefinition(const std::string& name) {
        const std::string lowerName = lowerText(name);
        const auto found = std::find_if(apiDefinitions.begin(), apiDefinitions.end(), [&lowerName](const ApiDefinition& definition) { return lowerText(definition.name) == lowerName; });
        return found == apiDefinitions.end() ? nullptr : &*found;
    }

    int completionMatchScore(const std::string& candidate, const std::string& needle) {
        if (needle.empty()) return 0;
        const std::string lowerCandidate = lowerText(candidate);
        if (lowerCandidate == needle) return 0;
        if (lowerCandidate.rfind(needle, 0) == 0) return 10;
        const std::size_t position = lowerCandidate.find(needle);
        if (position != std::string::npos) return 20 + static_cast<int>(position);
        std::size_t candidatePosition = 0;
        int gaps = 0;
        for (char character : needle) {
            const std::size_t found = lowerCandidate.find(character, candidatePosition);
            if (found == std::string::npos) return -1;
            gaps += static_cast<int>(found - candidatePosition);
            candidatePosition = found + 1;
        }
        return 100 + gaps;
    }

    GdkPixbuf* completionIcon(const std::string& type) {
        const std::string lowerType = lowerText(type);
        const char* names[] = {
            lowerType == "class" ? "applications-development-symbolic" : lowerType == "property" ? "emblem-system-symbolic" : "system-run-symbolic",
            lowerType == "class" ? "applications-development" : lowerType == "property" ? "emblem-system" : "system-run",
            "text-x-generic-symbolic", "text-x-generic"
        };
        GtkIconTheme* theme = gtk_icon_theme_get_default();
        for (const char* name : names) {
            if (g_str_has_suffix(name, "-symbolic")) {
                GtkIconInfo* iconInfo = gtk_icon_theme_lookup_icon(theme, name, 16, static_cast<GtkIconLookupFlags>(GTK_ICON_LOOKUP_USE_BUILTIN | GTK_ICON_LOOKUP_FORCE_SYMBOLIC));
                if (iconInfo != nullptr) {
                    const GdkRGBA foreground = {0.95, 0.97, 1.0, 1.0};
                    gboolean wasSymbolic = FALSE;
                    GError* symbolicError = nullptr;
                    GdkPixbuf* pixbuf = gtk_icon_info_load_symbolic(iconInfo, &foreground, nullptr, nullptr, nullptr, &wasSymbolic, &symbolicError);
                    gtk_icon_info_free(iconInfo);
                    if (symbolicError != nullptr) g_error_free(symbolicError);
                    if (pixbuf != nullptr && wasSymbolic) return pixbuf;
                    if (pixbuf != nullptr) g_object_unref(pixbuf);
                }
            }
            GError* error = nullptr;
            GdkPixbuf* pixbuf = gtk_icon_theme_load_icon(theme, name, 16, GTK_ICON_LOOKUP_USE_BUILTIN, &error);
            if (error != nullptr) g_error_free(error);
            if (pixbuf != nullptr) {
                if (gdk_pixbuf_get_has_alpha(pixbuf)) {
                    GdkPixbuf* contrast = gdk_pixbuf_copy(pixbuf);
                    if (contrast != nullptr) {
                        const int width = gdk_pixbuf_get_width(contrast);
                        const int height = gdk_pixbuf_get_height(contrast);
                        const int rowstride = gdk_pixbuf_get_rowstride(contrast);
                        const int channels = gdk_pixbuf_get_n_channels(contrast);
                        guchar* pixels = gdk_pixbuf_get_pixels(contrast);
                        for (int y = 0; y < height; ++y) {
                            guchar* row = pixels + y * rowstride;
                            for (int x = 0; x < width; ++x) {
                                guchar* pixel = row + x * channels;
                                if (pixel[3] == 0) continue;
                                pixel[0] = 242;
                                pixel[1] = 246;
                                pixel[2] = 252;
                            }
                        }
                        g_object_unref(pixbuf);
                        return contrast;
                    }
                }
                return pixbuf;
            }
        }
        return nullptr;
    }

    struct CompletionPopupClampRequest { GtkSourceCompletion* completion; unsigned int attempts; };

    GtkWidget* completionPopupTreeView(GtkWidget* widget) {
        if (widget == nullptr) return nullptr;
        if (GTK_IS_TREE_VIEW(widget)) return widget;
        if (!GTK_IS_CONTAINER(widget)) return nullptr;
        GtkWidget* found = nullptr;
        GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList* iterator = children; iterator != nullptr && found == nullptr; iterator = iterator->next) found = completionPopupTreeView(GTK_WIDGET(iterator->data));
        g_list_free(children);
        return found;
    }

    GtkSourceCompletionProposal* completionProposalAt(GtkTreeModel* model, GtkTreeIter* iter) {
        if (model == nullptr || iter == nullptr) return nullptr;
        for (gint column = 0; column < gtk_tree_model_get_n_columns(model); ++column) {
            GValue value = G_VALUE_INIT;
            gtk_tree_model_get_value(model, iter, column, &value);
            GObject* object = G_VALUE_HOLDS_OBJECT(&value) ? static_cast<GObject*>(g_value_get_object(&value)) : nullptr;
            GtkSourceCompletionProposal* proposal = object != nullptr && GTK_SOURCE_IS_COMPLETION_PROPOSAL(object) ? GTK_SOURCE_COMPLETION_PROPOSAL(object) : nullptr;
            g_value_unset(&value);
            if (proposal != nullptr) return proposal;
        }
        return nullptr;
    }

    GtkWidget* completionDetailsButton(GtkWidget* widget) {
        if (widget == nullptr) return nullptr;
        if (GTK_IS_BUTTON(widget)) {
            const gchar* label = gtk_button_get_label(GTK_BUTTON(widget));
            if ((label == nullptr || *label == '\0') && GTK_IS_BIN(widget)) {
                GtkWidget* child = gtk_bin_get_child(GTK_BIN(widget));
                if (GTK_IS_LABEL(child)) label = gtk_label_get_text(GTK_LABEL(child));
            }
            if (label != nullptr) {
                std::string normalized;
                for (const char* character = label; *character != '\0'; ++character) if (*character != '_') normalized.push_back(static_cast<char>(g_ascii_tolower(*character)));
                if (normalized.find("details") != std::string::npos) return widget;
            }
        }
        if (!GTK_IS_CONTAINER(widget)) return nullptr;
        GtkWidget* found = nullptr;
        GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList* iterator = children; iterator != nullptr && found == nullptr; iterator = iterator->next) found = completionDetailsButton(GTK_WIDGET(iterator->data));
        g_list_free(children);
        return found;
    }

    GtkWidget* completionFirstButton(GtkWidget* widget) {
        if (widget == nullptr) return nullptr;
        if (GTK_IS_BUTTON(widget)) return widget;
        if (!GTK_IS_CONTAINER(widget)) return nullptr;
        GtkWidget* found = nullptr;
        GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList* iterator = children; iterator != nullptr && found == nullptr; iterator = iterator->next) found = completionFirstButton(GTK_WIDGET(iterator->data));
        g_list_free(children);
        return found;
    }

    GtkWidget* completionPopupContentBox(GtkWidget* popup, GtkWidget* tree) {
        GtkWidget* widget = gtk_widget_get_parent(tree);
        while (widget != nullptr && widget != popup) {
            if (GTK_IS_BOX(widget)) return widget;
            widget = gtk_widget_get_parent(widget);
        }
        return GTK_IS_BOX(popup) ? popup : nullptr;
    }

    void updateCompletionPreview(GtkTreeSelection* selection, gpointer data) {
        GtkWidget* preview = GTK_WIDGET(data);
        GtkWidget* kindLabel = GTK_WIDGET(g_object_get_data(G_OBJECT(preview), "remote-completion-preview-kind"));
        GtkWidget* textLabel = GTK_WIDGET(g_object_get_data(G_OBJECT(preview), "remote-completion-preview-text"));
        GtkTreeModel* model = nullptr;
        GtkTreeIter iter;
        if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
            GtkTreeView* tree = gtk_tree_selection_get_tree_view(selection);
            GtkTreePath* path = nullptr;
            gtk_tree_view_get_cursor(tree, &path, nullptr);
            model = gtk_tree_view_get_model(tree);
            const bool hasCursor = path != nullptr && model != nullptr && gtk_tree_model_get_iter(model, &iter, path);
            if (path != nullptr) gtk_tree_path_free(path);
            if (!hasCursor) { gtk_widget_hide(preview); return; }
        }
        GtkSourceCompletionProposal* proposal = completionProposalAt(model, &iter);
        const gchar* kind = proposal == nullptr ? nullptr : static_cast<const gchar*>(g_object_get_data(G_OBJECT(proposal), "remote-completion-kind"));
        const gchar* text = proposal == nullptr ? nullptr : static_cast<const gchar*>(g_object_get_data(G_OBJECT(proposal), "remote-completion-description"));
        gtk_label_set_text(GTK_LABEL(kindLabel), kind == nullptr || *kind == '\0' ? "Info" : kind);
        gtk_label_set_text(GTK_LABEL(textLabel), text == nullptr ? "" : text);
        gtk_widget_show_all(preview);
    }

    void updateCompletionPreviewCursor(GtkTreeView* tree, gpointer data) { updateCompletionPreview(gtk_tree_view_get_selection(tree), data); }

    bool attachCompletionPreview(GtkWidget* parent, GtkWidget* details, GtkWidget* preview) {
        if (GTK_IS_BOX(parent)) {
            gint position = -1;
            GList* children = gtk_container_get_children(GTK_CONTAINER(parent));
            gint index = 0;
            for (GList* iterator = children; iterator != nullptr; iterator = iterator->next, ++index) if (iterator->data == details) { position = index; break; }
            g_list_free(children);
            gtk_box_pack_start(GTK_BOX(parent), preview, true, true, 0);
            if (position >= 0) gtk_box_reorder_child(GTK_BOX(parent), preview, position);
            return true;
        }
        if (GTK_IS_GRID(parent) && details != nullptr) {
            gint left = 0;
            gint top = 0;
            gint width = 1;
            gint height = 1;
            gtk_container_child_get(GTK_CONTAINER(parent), details, "left-attach", &left, "top-attach", &top, "width", &width, "height", &height, nullptr);
            gtk_grid_attach(GTK_GRID(parent), preview, left, top, width, height);
            return true;
        }
        return false;
    }

    bool installCompletionPreview(GtkWidget* popup) {
        if (popup == nullptr) return false;
        GtkWidget* tree = completionPopupTreeView(popup);
        GtkWidget* details = completionDetailsButton(popup);
        if (details == nullptr) details = completionFirstButton(popup);
        GtkWidget* existing = GTK_WIDGET(g_object_get_data(G_OBJECT(popup), "remote-completion-preview"));
        if (tree == nullptr) return false;
        if (existing != nullptr) {
            if (details != nullptr) { gtk_widget_set_no_show_all(details, true); gtk_widget_hide(details); }
            updateCompletionPreview(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), existing);
            return true;
        }
        GtkWidget* parent = details == nullptr ? completionPopupContentBox(popup, tree) : gtk_widget_get_parent(details);
        if (parent == nullptr) return false;
        GtkWidget* preview = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
        GtkWidget* kind = gtk_label_new(nullptr);
        GtkWidget* text = gtk_label_new(nullptr);
        gtk_style_context_add_class(gtk_widget_get_style_context(preview), "remote-completion-preview");
        gtk_style_context_add_class(gtk_widget_get_style_context(kind), "remote-completion-preview-kind");
        gtk_style_context_add_class(gtk_widget_get_style_context(text), "remote-completion-preview-text");
        gtk_label_set_xalign(GTK_LABEL(kind), 0.0F);
        gtk_label_set_xalign(GTK_LABEL(text), 0.0F);
        gtk_label_set_ellipsize(GTK_LABEL(text), PANGO_ELLIPSIZE_END);
        gtk_label_set_single_line_mode(GTK_LABEL(text), true);
        gtk_widget_set_hexpand(preview, true);
        gtk_widget_set_hexpand(text, true);
        gtk_box_pack_start(GTK_BOX(preview), kind, false, false, 0);
        gtk_box_pack_start(GTK_BOX(preview), text, true, true, 0);
        if (!attachCompletionPreview(parent, details, preview)) {
            GtkWidget* fallback = completionPopupContentBox(popup, tree);
            if (!GTK_IS_BOX(fallback)) { gtk_widget_destroy(preview); return false; }
            gtk_box_pack_end(GTK_BOX(fallback), preview, false, false, 0);
        }
        if (details != nullptr) { gtk_widget_set_no_show_all(details, true); gtk_widget_hide(details); }
        g_object_set_data(G_OBJECT(preview), "remote-completion-preview-kind", kind);
        g_object_set_data(G_OBJECT(preview), "remote-completion-preview-text", text);
        g_object_set_data(G_OBJECT(popup), "remote-completion-preview", preview);
        GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
        gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
        g_signal_connect(selection, "changed", G_CALLBACK(updateCompletionPreview), preview);
        g_signal_connect(tree, "cursor-changed", G_CALLBACK(updateCompletionPreviewCursor), preview);
        updateCompletionPreview(selection, preview);
        return true;
    }

    void renderCompletionScope(GtkTreeViewColumn*, GtkCellRenderer* renderer, GtkTreeModel* model, GtkTreeIter* iter, gpointer) {
        GtkSourceCompletionProposal* proposal = completionProposalAt(model, iter);
        const gchar* scope = proposal == nullptr ? nullptr : static_cast<const gchar*>(g_object_get_data(G_OBJECT(proposal), "remote-completion-scope"));
        g_object_set(renderer, "text", scope == nullptr ? "" : scope, "xalign", 1.0F, "visible", scope != nullptr && *scope != '\0', nullptr);
    }

    void constrainCompletionPopupContents(GtkWidget* widget) {
        if (widget == nullptr) return;
        if (GTK_IS_SCROLLED_WINDOW(widget)) {
            gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(widget), completionPopupMaxWidth);
            gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(widget), false);
        }
        if (GTK_IS_TREE_VIEW(widget)) {
            GtkTreeView* tree = GTK_TREE_VIEW(widget);
            GList* columns = gtk_tree_view_get_columns(tree);
            struct TextRenderer { GtkTreeViewColumn* column; GtkCellRenderer* renderer; };
            std::vector<TextRenderer> textRenderers;
            for (GList* iterator = columns; iterator != nullptr; iterator = iterator->next) {
                GtkTreeViewColumn* column = GTK_TREE_VIEW_COLUMN(iterator->data);
                GList* cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
                for (GList* cell = cells; cell != nullptr; cell = cell->next) {
                    if (!GTK_IS_CELL_RENDERER_TEXT(cell->data)) continue;
                    textRenderers.push_back({column, GTK_CELL_RENDERER(cell->data)});
                    g_object_set(cell->data, "ellipsize", PANGO_ELLIPSIZE_END, "ellipsize-set", TRUE, "max-width-chars", 46, nullptr);
                }
                g_list_free(cells);
            }
            GtkTreeViewColumn* scopeColumn = nullptr;
            GtkCellRenderer* scopeRenderer = nullptr;
            if (textRenderers.size() > 1) {
                scopeColumn = textRenderers.back().column;
                scopeRenderer = textRenderers.back().renderer;
                if (g_object_get_data(G_OBJECT(scopeRenderer), "remote-completion-scope-renderer") == nullptr) {
                    gtk_tree_view_column_set_cell_data_func(scopeColumn, scopeRenderer, renderCompletionScope, nullptr, nullptr);
                    gtk_cell_renderer_set_fixed_size(scopeRenderer, 96, -1);
                    g_object_set(scopeRenderer, "xalign", 1.0F, "ellipsize", PANGO_ELLIPSIZE_END, "ellipsize-set", TRUE, "max-width-chars", 14, nullptr);
                    g_object_set_data(G_OBJECT(scopeRenderer), "remote-completion-scope-renderer", GINT_TO_POINTER(1));
                }
            }
            for (GList* iterator = columns; iterator != nullptr; iterator = iterator->next) {
                GtkTreeViewColumn* column = GTK_TREE_VIEW_COLUMN(iterator->data);
                std::size_t textCount = 0;
                for (const TextRenderer& entry : textRenderers) if (entry.column == column) ++textCount;
                const bool scopeOnly = scopeColumn == column && textCount == 1;
                gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
                gtk_tree_view_column_set_fixed_width(column, textCount == 0 ? 24 : (scopeOnly ? 78 : (scopeColumn == column ? 78 : 218)));
                gtk_tree_view_column_set_expand(column, false);
            }
            g_list_free(columns);
            gtk_widget_set_hexpand(widget, false);
        }
        if (!GTK_IS_CONTAINER(widget)) return;
        GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList* iterator = children; iterator != nullptr; iterator = iterator->next) constrainCompletionPopupContents(GTK_WIDGET(iterator->data));
        g_list_free(children);
    }

    GtkWindow* completionPopupWindow(GtkSourceCompletion* completion) {
        GtkWidget* view = GTK_WIDGET(gtk_source_completion_get_view(completion));
        GtkWidget* viewToplevel = view == nullptr ? nullptr : gtk_widget_get_toplevel(view);
        GtkWindow* owner = viewToplevel != nullptr && GTK_IS_WINDOW(viewToplevel) ? GTK_WINDOW(viewToplevel) : nullptr;
        GtkWindow* fallback = nullptr;
        GList* windows = gtk_window_list_toplevels();
        for (GList* iterator = windows; iterator != nullptr; iterator = iterator->next) {
            GtkWindow* window = GTK_WINDOW(iterator->data);
            if (!GTK_IS_WINDOW(window)) continue;
            const gchar* typeName = G_OBJECT_TYPE_NAME(window);
            GtkWindow* transient = gtk_window_get_transient_for(window);
            const bool namedCompletion = typeName != nullptr && (g_str_has_prefix(typeName, "GtkSourceCompletionWindow") || g_strrstr(typeName, "Completion") != nullptr);
            const bool attachedCompletion = owner != nullptr && window != owner && transient == owner && completionPopupTreeView(GTK_WIDGET(window)) != nullptr;
            if (!namedCompletion && !attachedCompletion) continue;
            if (attachedCompletion) { fallback = window; break; }
            if (gtk_widget_get_visible(GTK_WIDGET(window))) fallback = fallback == nullptr ? window : fallback;
        }
        g_list_free(windows);
        return fallback;
    }

    void enforceCompletionPopupGeometry(GtkWindow* popup) {
        if (popup == nullptr || g_object_get_data(G_OBJECT(popup), "remote-completion-resizing") != nullptr) return;
        g_object_set_data(G_OBJECT(popup), "remote-completion-resizing", GINT_TO_POINTER(1));
        gint width = 0;
        gint height = 0;
        gtk_window_get_size(popup, &width, &height);
        width = std::clamp(width, completionPopupMinWidth, completionPopupMaxWidth);
        height = std::max(height, 1);
        GdkWindow* native = gtk_widget_get_window(GTK_WIDGET(popup));
        if (native != nullptr) {
            GdkDisplay* display = gdk_window_get_display(native);
            GdkMonitor* monitor = gdk_display_get_monitor_at_window(display, native);
            if (monitor != nullptr) {
                GdkRectangle workarea;
                gdk_monitor_get_workarea(monitor, &workarea);
                gint x = 0;
                gint y = 0;
                gtk_window_get_position(popup, &x, &y);
                x = std::clamp(x, workarea.x, std::max(workarea.x, workarea.x + workarea.width - width));
                y = std::clamp(y, workarea.y, std::max(workarea.y, workarea.y + workarea.height - height));
                gtk_window_move(popup, x, y);
            }
        }
        gtk_window_resize(popup, width, height);
        g_object_set_data(G_OBJECT(popup), "remote-completion-resizing", nullptr);
    }

    void installCompletionPopupGeometry(GtkWindow* popup) {
        if (popup == nullptr || g_object_get_data(G_OBJECT(popup), "remote-completion-geometry") != nullptr) return;
        g_object_set_data(G_OBJECT(popup), "remote-completion-geometry", GINT_TO_POINTER(1));
        GdkGeometry geometry{};
        geometry.min_width = completionPopupMinWidth;
        geometry.max_width = completionPopupMaxWidth;
        gtk_window_set_geometry_hints(popup, nullptr, &geometry, static_cast<GdkWindowHints>(GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE));
        gtk_window_set_resizable(popup, false);
        gtk_widget_set_size_request(GTK_WIDGET(popup), completionPopupMinWidth, -1);
        g_signal_connect(popup, "size-allocate", G_CALLBACK(+[](GtkWidget* widget, GtkAllocation*, gpointer) {
            enforceCompletionPopupGeometry(GTK_WINDOW(widget));
        }), nullptr);
        g_signal_connect(popup, "configure-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventConfigure*, gpointer) -> gboolean {
            enforceCompletionPopupGeometry(GTK_WINDOW(widget));
            return false;
        }), nullptr);
        enforceCompletionPopupGeometry(popup);
    }

    void installCompletionToplevelGeometry(GtkWidget* widget) {
        if (widget == nullptr) return;
        if (!GTK_IS_WINDOW(widget)) return;
        GtkWidget* toplevel = gtk_widget_get_toplevel(widget);
        if (toplevel == widget) installCompletionPopupGeometry(GTK_WINDOW(widget));
    }

    gboolean clampCompletionPopup(gpointer data) {
        CompletionPopupClampRequest* request = static_cast<CompletionPopupClampRequest*>(data);
        GtkWindow* popup = completionPopupWindow(request->completion);
        if (popup == nullptr && request->attempts++ < 240) return G_SOURCE_CONTINUE;
        if (popup != nullptr) {
            gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(popup)), "remote-completion-popup");
            constrainCompletionPopupContents(GTK_WIDGET(popup));
            const bool previewInstalled = installCompletionPreview(GTK_WIDGET(popup));
            installCompletionPopupGeometry(popup);
            if (!previewInstalled && request->attempts++ < 240) return G_SOURCE_CONTINUE;
        }
        GtkWidget* view = GTK_WIDGET(gtk_source_completion_get_view(request->completion));
        GtkWindow* owner = view != nullptr && GTK_IS_WINDOW(gtk_widget_get_toplevel(view)) ? GTK_WINDOW(gtk_widget_get_toplevel(view)) : nullptr;
        GList* windows = gtk_window_list_toplevels();
        for (GList* iterator = windows; iterator != nullptr; iterator = iterator->next) {
            GtkWindow* window = GTK_WINDOW(iterator->data);
            const gchar* typeName = G_OBJECT_TYPE_NAME(window);
            GtkWindow* transient = gtk_window_get_transient_for(window);
            const bool namedCompletion = typeName != nullptr && g_strrstr(typeName, "Completion") != nullptr;
            const bool attachedCompletion = owner != nullptr && window != owner && transient == owner && completionPopupTreeView(GTK_WIDGET(window)) != nullptr;
            if ((namedCompletion || attachedCompletion) && window != owner) installCompletionPopupGeometry(window);
        }
        g_list_free(windows);
        return ++request->attempts < 240 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
    }

    void scheduleCompletionPopupClamp(GtkSourceCompletion* completion) {
        CompletionPopupClampRequest* request = new CompletionPopupClampRequest{GTK_SOURCE_COMPLETION(g_object_ref(completion)), 0};
        g_timeout_add_full(G_PRIORITY_DEFAULT, 16, clampCompletionPopup, request, [](gpointer data) {
            CompletionPopupClampRequest* request = static_cast<CompletionPopupClampRequest*>(data);
            g_object_unref(request->completion);
            delete request;
        });
    }

    void setCompletionPreview(GtkSourceCompletionItem* item, const char* kind, const std::string& description, const std::string& scope = {}) {
        g_object_set_data_full(G_OBJECT(item), "remote-completion-kind", g_strdup(kind), g_free);
        g_object_set_data_full(G_OBJECT(item), "remote-completion-description", g_strdup(description.c_str()), g_free);
        g_object_set_data_full(G_OBJECT(item), "remote-completion-scope", g_strdup(scope.c_str()), g_free);
    }

    bool findEditorDefinition(GtkWidget* editor, const std::string& name, ApiDefinition& result) {
        const std::string lowerName = lowerText(name);
        if (const ApiDefinition* definition = findDefinition(name)) { result = *definition; return true; }
        const std::size_t separator = name.rfind('.');
        if (separator != std::string::npos) {
            if (const ApiDefinition* definition = findDefinition(name.substr(separator + 1))) { result = *definition; return true; }
        }
        for (const ApiDefinition& definition : localFunctionDefinitions(editor)) if (lowerText(definition.name) == lowerName) { result = definition; return true; }
        return false;
    }

    std::string wordAtIter(GtkTextIter iter) {
        GtkTextIter start = iter;
        GtkTextIter end = iter;
        while (!gtk_text_iter_starts_line(&start)) {
            GtkTextIter previous = start;
            if (!gtk_text_iter_backward_char(&previous) || !identifierCharacter(gtk_text_iter_get_char(&previous))) break;
            start = previous;
        }
        while (!gtk_text_iter_ends_line(&end) && identifierCharacter(gtk_text_iter_get_char(&end))) gtk_text_iter_forward_char(&end);
        gchar* value = gtk_text_iter_get_text(&start, &end);
        std::string result = value == nullptr ? "" : value;
        g_free(value);
        return result;
    }

    std::string definitionInfo(const ApiDefinition& definition) {
        std::string result;
        if (!definition.type.empty()) result += definition.type;
        if (!definition.scope.empty()) result += (result.empty() ? "" : "  ") + upperCase(definition.scope);
        if (!definition.params.empty()) {
            result += (result.empty() ? "" : "\n") + std::string("Parameters: ");
            for (std::size_t index = 0; index < definition.params.size(); ++index) { if (index != 0) result += ", "; result += definition.params[index]; }
        }
        if (!definition.returns.empty()) result += (result.empty() ? "" : "\n") + std::string("Returns: ") + definition.returns;
        if (!definition.description.empty()) result += (result.empty() ? "" : "\n") + definition.description;
        if (!definition.example.empty()) result += "\n\nExample:\n" + definition.example;
        return result;
    }

    std::string definitionSignature(const ApiDefinition& definition) {
        std::string result = definition.name + "(";
        for (std::size_t index = 0; index < definition.params.size(); ++index) { if (index != 0) result += ", "; result += definition.params[index]; }
        return result + ')';
    }

    std::string completionScopeLabel(const ApiDefinition& definition) {
        const std::string scope = lowerText(definition.scope);
        if (scope == "script") return "GLOBAL";
        if (scope == "client") return "CLIENTSIDE";
        if (scope == "server") return "SERVERSIDE";
        return upperCase(definition.scope);
    }

    std::string completionSummary(const ApiDefinition& definition) {
        std::string result = definitionSignature(definition);
        if (!definition.returns.empty()) result += " - returns " + definition.returns;
        if (!definition.description.empty()) result += " - " + definition.description;
        return result;
    }

    std::vector<std::string> formatScriptHelp(const std::vector<ApiDefinition>& definitions, const std::string& query) {
        const std::string needle = lowerText(query);
        std::vector<const ApiDefinition*> matches;
        for (const ApiDefinition& definition : definitions) if (needle.empty() || lowerText(definition.name).find(needle) != std::string::npos) matches.push_back(&definition);
        if (matches.empty()) return {"[Script Help] No API entries match '" + query + "'."};
        const auto exact = std::find_if(matches.begin(), matches.end(), [&needle](const ApiDefinition* definition) { return lowerText(definition->name) == needle; });
        if (exact != matches.end()) {
            const ApiDefinition& definition = **exact;
            std::vector<std::string> result = {"[Script Help] " + definitionSignature(definition) + (definition.scope.empty() ? "" : " [" + upperCase(definition.scope) + "]")};
            if (!definition.returns.empty()) result.push_back("Returns: " + definition.returns);
            if (!definition.description.empty()) result.push_back(definition.description);
            if (!definition.example.empty()) result.push_back("Example: " + definition.example);
            return result;
        }
        std::vector<std::string> result = {"[Script Help] " + std::to_string(matches.size()) + " entries matching '" + query + "':"};
        const std::size_t count = std::min<std::size_t>(matches.size(), 30);
        for (std::size_t index = 0; index < count; ++index) {
            const ApiDefinition& definition = *matches[index];
            result.push_back(definitionSignature(definition) + (definition.scope.empty() ? "" : " [" + upperCase(definition.scope) + "]") + (definition.description.empty() ? "" : " - " + definition.description));
        }
        if (matches.size() > count) result.push_back("Use a longer /scripthelp2 query to narrow the result.");
        return result;
    }

    gboolean editorQueryTooltip(GtkWidget* editor, gint x, gint y, gboolean, GtkTooltip* tooltip, gpointer) {
        gint bufferX = 0;
        gint bufferY = 0;
        gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(editor), GTK_TEXT_WINDOW_WIDGET, x, y, &bufferX, &bufferY);
        GtkTextIter iter;
        gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(editor), &iter, bufferX, bufferY);
        if (showDiagnosticTooltip(editor, iter, tooltip)) return true;
        ApiDefinition definition;
        if (!findEditorDefinition(editor, wordAtIter(iter), definition)) return false;
        const std::string signature = definitionSignature(definition);
        const std::string info = definitionInfo(definition);
        gchar* escapedSignature = g_markup_escape_text(signature.c_str(), -1);
        gchar* escapedInfo = g_markup_escape_text(info.c_str(), -1);
        gchar* markup = g_strdup_printf("<b>%s</b>\n%s", escapedSignature, escapedInfo == nullptr ? "" : escapedInfo);
        gtk_tooltip_set_markup(tooltip, markup);
        g_free(markup);
        g_free(escapedSignature);
        g_free(escapedInfo);
        return true;
    }

    gchar* remoteCompletionProviderGetName(GtkSourceCompletionProvider*) { return g_strdup(""); }

    GtkSourceCompletionActivation remoteCompletionProviderGetActivation(GtkSourceCompletionProvider*) { return static_cast<GtkSourceCompletionActivation>(GTK_SOURCE_COMPLETION_ACTIVATION_INTERACTIVE | GTK_SOURCE_COMPLETION_ACTIVATION_USER_REQUESTED); }

    bool hasActiveSignatureContext(GtkWidget* editor);

    gboolean remoteCompletionProviderGetStartIter(GtkSourceCompletionProvider*, GtkSourceCompletionContext* context, GtkSourceCompletionProposal*, GtkTextIter* iter) {
        if (!gtk_source_completion_context_get_iter(context, iter)) return false;
        while (!gtk_text_iter_starts_line(iter)) {
            GtkTextIter previous = *iter;
            if (!gtk_text_iter_backward_char(&previous) || !identifierCharacter(gtk_text_iter_get_char(&previous))) break;
            *iter = previous;
        }
        return true;
    }

    void remoteCompletionProviderPopulate(GtkSourceCompletionProvider* provider, GtkSourceCompletionContext* context) {
        auto* remote = REMOTE_COMPLETION_PROVIDER(provider);
        if (remote->editor == nullptr || !lspEnabled) { gtk_source_completion_context_add_proposals(context, provider, nullptr, true); return; }
        if (hasActiveSignatureContext(remote->editor)) { gtk_source_completion_context_add_proposals(context, provider, nullptr, true); return; }
        GtkTextIter iter;
        if (!gtk_source_completion_context_get_iter(context, &iter)) { gtk_source_completion_context_add_proposals(context, provider, nullptr, true); return; }
        const std::string prefix = lowerText(completionPrefix(iter));
        GtkTextIter contextStart = iter;
        while (!gtk_text_iter_starts_line(&contextStart)) {
            GtkTextIter previous = contextStart;
            if (!gtk_text_iter_backward_char(&previous)) break;
            const gunichar character = gtk_text_iter_get_char(&previous);
            if (!identifierCharacter(character) && character != '.') break;
            contextStart = previous;
        }
        gchar* contextText = gtk_text_iter_get_text(&contextStart, &iter);
        const std::string completionContext = contextText == nullptr ? "" : contextText;
        g_free(contextText);
        const auto state = std::find_if(editorCompletionStates.begin(), editorCompletionStates.end(), [remote](const EditorCompletionState& value) { return value.editor == remote->editor; });
        if (state != editorCompletionStates.end() && !state->completionArmed && gtk_source_completion_context_get_activation(context) == GTK_SOURCE_COMPLETION_ACTIVATION_INTERACTIVE) { gtk_source_completion_context_add_proposals(context, provider, nullptr, true); return; }
        const std::size_t dot = completionContext.rfind('.');
        const std::string objectPrefix = dot == std::string::npos ? "" : completionContext.substr(0, dot + 1);
        if (prefix.empty() && gtk_source_completion_context_get_activation(context) == GTK_SOURCE_COMPLETION_ACTIVATION_INTERACTIVE) { gtk_source_completion_context_add_proposals(context, provider, nullptr, true); return; }
        std::set<std::string> seen;
        GList* proposals = nullptr;
        std::vector<ApiDefinition> definitions = apiDefinitions;
        const std::vector<ApiDefinition> localDefinitions = localFunctionDefinitions(remote->editor);
        definitions.insert(definitions.end(), localDefinitions.begin(), localDefinitions.end());
        struct RankedDefinition { int score; ApiDefinition definition; };
        std::vector<RankedDefinition> rankedDefinitions;
        for (const ApiDefinition& definition : definitions) {
            if (!seen.insert(lowerText(definition.name)).second) continue;
            const int score = completionMatchScore(definition.name, prefix);
            if (score < 0) continue;
            rankedDefinitions.push_back({score, definition});
        }
        std::sort(rankedDefinitions.begin(), rankedDefinitions.end(), [](const RankedDefinition& left, const RankedDefinition& right) {
            if (left.score != right.score) return left.score < right.score;
            return lowerText(left.definition.name) < lowerText(right.definition.name);
        });
        for (const RankedDefinition& ranked : rankedDefinitions) {
            const ApiDefinition& definition = ranked.definition;
            std::string label = definition.name;
            if (!definition.params.empty()) {
                label += '(';
                for (std::size_t index = 0; index < definition.params.size(); ++index) { if (index != 0) label += ", "; label += definition.params[index]; }
                label += ')';
            }
            GdkPixbuf* icon = completionIcon(definition.type);
            GtkSourceCompletionItem* item = gtk_source_completion_item_new(label.c_str(), definition.name.c_str(), icon, definitionInfo(definition).c_str());
            const std::string kind = definition.type.empty() ? "Function" : definition.type;
            setCompletionPreview(item, kind.c_str(), completionSummary(definition), completionScopeLabel(definition));
            if (icon != nullptr) g_object_unref(icon);
            proposals = g_list_prepend(proposals, item);
        }
        if (state != editorCompletionStates.end() && state->connection != nullptr && completionInsideString(iter)) {
            RCPlayer* players = nullptr;
            const int count = rc_get_players(state->connection, &players);
            const auto communities = playerCommunityNames.find(state->connection);
            for (int index = 0; index < count; ++index) {
                const std::string account = players[index].account == nullptr ? "" : players[index].account;
                const std::string nick = players[index].nick == nullptr ? "" : players[index].nick;
                std::string communityName;
                if (communities != playerCommunityNames.end()) {
                    const auto community = communities->second.find(players[index].id);
                    if (community != communities->second.end()) communityName = community->second;
                }
                const std::pair<std::string, const char*> aliases[] = {{account, "ACCOUNT"}, {nick, "NICK"}, {communityName, "COMMUNITY"}};
                std::set<std::string> aliasesSeen;
                for (const auto& [alias, kind] : aliases) {
                    const std::string lowerAlias = lowerText(alias);
                    if (alias.empty() || prefix.empty() || !aliasesSeen.insert(lowerAlias).second || completionMatchScore(alias, prefix) < 0) continue;
                    if (!seen.insert("player:" + lowerAlias).second) continue;
                    const std::string label = alias + "  [" + kind + "]";
                    const std::string detail = "Online player" + (account.empty() ? std::string() : " — " + account);
                    GdkPixbuf* icon = completionIcon("player");
                    GtkSourceCompletionItem* item = gtk_source_completion_item_new(label.c_str(), account.empty() ? alias.c_str() : account.c_str(), icon, detail.c_str());
                    setCompletionPreview(item, "Player", detail);
                    if (icon != nullptr) g_object_unref(icon);
                    proposals = g_list_prepend(proposals, item);
                }
            }
        }
        for (const std::string& name : localIdentifiers(remote->editor)) {
            const std::string lowerName = lowerText(name);
            std::string insertText = name;
            if (!objectPrefix.empty()) {
                if (lowerName.rfind(lowerText(objectPrefix), 0) != 0) continue;
                insertText = name.substr(objectPrefix.size());
            }
            if (completionMatchScore(insertText, prefix) < 0 || !seen.insert(lowerText(insertText)).second) continue;
            GdkPixbuf* icon = completionIcon("identifier");
            GtkSourceCompletionItem* item = gtk_source_completion_item_new(insertText.c_str(), insertText.c_str(), icon, "Current script identifier");
            setCompletionPreview(item, "Identifier", "Current script identifier");
            if (icon != nullptr) g_object_unref(icon);
            proposals = g_list_prepend(proposals, item);
        }
        gtk_source_completion_context_add_proposals(context, provider, g_list_reverse(proposals), true);
        g_list_free_full(proposals, g_object_unref);
    }

    GtkWidget* remoteCompletionProviderGetInfoWidget(GtkSourceCompletionProvider*, GtkSourceCompletionProposal*) {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        gtk_style_context_add_class(gtk_widget_get_style_context(box), "remote-completion-info");
        gtk_widget_set_size_request(box, completionPopupMaxWidth - 16, -1);
        GtkWidget* signature = gtk_label_new(nullptr);
        gtk_style_context_add_class(gtk_widget_get_style_context(signature), "remote-completion-signature");
        gtk_label_set_xalign(GTK_LABEL(signature), 0.0F);
        gtk_label_set_line_wrap(GTK_LABEL(signature), true);
        gtk_label_set_max_width_chars(GTK_LABEL(signature), 38);
        GtkWidget* details = gtk_label_new(nullptr);
        gtk_style_context_add_class(gtk_widget_get_style_context(details), "remote-completion-details");
        gtk_label_set_xalign(GTK_LABEL(details), 0.0F);
        gtk_label_set_line_wrap(GTK_LABEL(details), true);
        gtk_label_set_max_width_chars(GTK_LABEL(details), 38);
        gtk_box_pack_start(GTK_BOX(box), signature, false, false, 0);
        gtk_box_pack_start(GTK_BOX(box), details, false, false, 0);
        g_object_set_data(G_OBJECT(box), "remote-completion-signature", signature);
        g_object_set_data(G_OBJECT(box), "remote-completion-details", details);
        return box;
    }

    void remoteCompletionProviderUpdateInfo(GtkSourceCompletionProvider*, GtkSourceCompletionProposal* proposal, GtkSourceCompletionInfo* info) {
        GtkWidget* box = gtk_bin_get_child(GTK_BIN(info));
        if (box == nullptr) {
            box = remoteCompletionProviderGetInfoWidget(nullptr, proposal);
            gtk_container_add(GTK_CONTAINER(info), box);
        }
        GtkWidget* signature = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(box), "remote-completion-signature"));
        GtkWidget* details = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(box), "remote-completion-details"));
        gchar* labelText = gtk_source_completion_proposal_get_label(proposal);
        gchar* escapedLabel = g_markup_escape_text(labelText == nullptr ? "" : labelText, -1);
        gtk_label_set_markup(GTK_LABEL(signature), (std::string("<b>") + (escapedLabel == nullptr ? "" : escapedLabel) + "</b>").c_str());
        g_free(escapedLabel);
        g_free(labelText);
        gchar* infoText = gtk_source_completion_proposal_get_info(proposal);
        gtk_label_set_text(GTK_LABEL(details), infoText == nullptr ? "" : infoText);
        g_free(infoText);
        gtk_widget_set_margin_start(box, 8);
        gtk_widget_set_margin_end(box, 8);
        gtk_widget_set_margin_top(box, 6);
        gtk_widget_set_margin_bottom(box, 6);
        gtk_widget_show_all(box);
    }

    void remoteCompletionProviderInterfaceInit(GtkSourceCompletionProviderIface* iface) {
        iface->get_name = remoteCompletionProviderGetName;
        iface->populate = remoteCompletionProviderPopulate;
        iface->get_activation = remoteCompletionProviderGetActivation;
        iface->get_info_widget = remoteCompletionProviderGetInfoWidget;
        iface->update_info = remoteCompletionProviderUpdateInfo;
        iface->get_start_iter = remoteCompletionProviderGetStartIter;
    }

    gboolean restoreEditorPopups(gpointer data);

    struct ActiveCallContext { std::size_t open = std::string::npos; std::string name; int argument = 0; };

    bool findActiveCallContext(const std::string& line, ActiveCallContext& result) {
        std::vector<std::pair<char, std::size_t>> delimiters;
        char quote = 0;
        bool escaped = false;
        bool lineComment = false;
        bool blockComment = false;
        for (std::size_t index = 0; index < line.size(); ++index) {
            const char character = line[index];
            const char next = index + 1 < line.size() ? line[index + 1] : '\0';
            if (blockComment) { if (character == '*' && next == '/') { blockComment = false; ++index; } continue; }
            if (lineComment) break;
            if (quote != 0) {
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == quote) quote = 0;
                continue;
            }
            if (character == '/' && next == '/') { lineComment = true; break; }
            if (character == '/' && next == '*') { blockComment = true; ++index; continue; }
            if (character == '"' || character == '\'') { quote = character; continue; }
            if (character == '(' || character == '[' || character == '{') delimiters.emplace_back(character, index);
            else if (character == ')' || character == ']' || character == '}') {
                const char opening = character == ')' ? '(' : character == ']' ? '[' : '{';
                for (auto iterator = delimiters.rbegin(); iterator != delimiters.rend(); ++iterator) {
                    if (iterator->first == opening) { delimiters.erase(std::next(iterator).base()); break; }
                }
            }
        }
        auto openIterator = std::find_if(delimiters.rbegin(), delimiters.rend(), [](const auto& delimiter) { return delimiter.first == '('; });
        if (openIterator == delimiters.rend()) return false;
        result.open = openIterator->second;
        std::size_t nameEnd = result.open;
        while (nameEnd > 0 && g_ascii_isspace(line[nameEnd - 1])) --nameEnd;
        std::size_t nameStart = nameEnd;
        while (nameStart > 0 && (g_ascii_isalnum(line[nameStart - 1]) || line[nameStart - 1] == '_' || line[nameStart - 1] == '$' || line[nameStart - 1] == '.')) --nameStart;
        result.name = line.substr(nameStart, nameEnd - nameStart);
        int nested = 0;
        quote = 0;
        escaped = false;
        for (std::size_t index = result.open + 1; index < line.size(); ++index) {
            const char character = line[index];
            if (quote != 0) {
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == quote) quote = 0;
                continue;
            }
            if (character == '"' || character == '\'') { quote = character; continue; }
            if (character == '(' || character == '[' || character == '{') ++nested;
            else if (character == ')' || character == ']' || character == '}') { if (nested > 0) --nested; }
            else if (character == ',' && nested == 0) ++result.argument;
        }
        return !result.name.empty();
    }

    bool hasActiveSignatureContext(GtkWidget* editor) {
        if (editor == nullptr || !GTK_IS_TEXT_VIEW(editor)) return false;
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
        GtkTextIter lineStart = iter;
        gtk_text_iter_set_line_offset(&lineStart, 0);
        gchar* lineValue = gtk_text_iter_get_text(&lineStart, &iter);
        const std::string line = lineValue == nullptr ? "" : lineValue;
        g_free(lineValue);
        ActiveCallContext call;
        if (!findActiveCallContext(line, call)) return false;
        ApiDefinition definition;
        return findEditorDefinition(editor, call.name, definition) && !(definition.params.empty() && definition.returns.empty() && definition.description.empty() && definition.example.empty());
    }

    void updateSignatureHint(GtkWidget* editor) {
        const auto state = std::find_if(editorCompletionStates.begin(), editorCompletionStates.end(), [editor](const EditorCompletionState& value) { return value.editor == editor; });
        if (state == editorCompletionStates.end() || state->signaturePopover == nullptr || state->signatureLabel == nullptr || !lspEnabled || !state->completionArmed) return;
        GtkSourceCompletion* completion = GTK_SOURCE_IS_VIEW(editor) ? gtk_source_view_get_completion(GTK_SOURCE_VIEW(editor)) : nullptr;
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
        GtkTextIter lineStart = iter;
        gtk_text_iter_set_line_offset(&lineStart, 0);
        gchar* lineValue = gtk_text_iter_get_text(&lineStart, &iter);
        const std::string line = lineValue == nullptr ? "" : lineValue;
        g_free(lineValue);
        ActiveCallContext call;
        if (!findActiveCallContext(line, call)) { gtk_widget_hide(state->signaturePopover); return; }
        ApiDefinition definition;
        if (!findEditorDefinition(editor, call.name, definition) || (definition.params.empty() && definition.returns.empty() && definition.description.empty() && definition.example.empty())) { gtk_widget_hide(state->signaturePopover); return; }
        if (completion != nullptr) gtk_source_completion_hide(completion);
        const auto escaped = [](const std::string& value) {
            gchar* text = g_markup_escape_text(value.c_str(), -1);
            std::string result = text == nullptr ? "" : text;
            g_free(text);
            return result;
        };
        std::string markup = "<b>" + escaped(definitionSignature(definition)) + "</b>";
        const std::string kind = definition.type.empty() && definition.scope.empty() ? "" : definition.type + (definition.scope.empty() ? "" : "  " + upperCase(definition.scope));
        if (!kind.empty()) markup += "  <i>" + escaped(kind) + "</i>";
        if (!definition.params.empty()) {
            const int argument = std::min(call.argument, static_cast<int>(definition.params.size()) - 1);
            markup += "\n";
            for (std::size_t index = 0; index < definition.params.size(); ++index) {
                if (index != 0) markup += ", ";
                const std::string parameter = escaped(definition.params[index]);
                markup += index == static_cast<std::size_t>(argument) ? "<b><u>" + parameter + "</u></b>" : parameter;
            }
        }
        if (!definition.returns.empty()) markup += "\n<b>Returns:</b> " + escaped(definition.returns);
        if (!definition.description.empty()) markup += "\n" + escaped(definition.description);
        if (!definition.example.empty()) markup += "\n\n<b>Example:</b>\n" + escaped(definition.example);
        gtk_label_set_markup(GTK_LABEL(state->signatureLabel), markup.c_str());
        GdkRectangle rect;
        gtk_text_view_get_iter_location(GTK_TEXT_VIEW(editor), &iter, &rect);
        gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(editor), GTK_TEXT_WINDOW_WIDGET, rect.x, rect.y, &rect.x, &rect.y);
        rect.width = std::max(1, rect.width);
        rect.height = std::max(1, rect.height);
        gtk_popover_set_position(GTK_POPOVER(state->signaturePopover), GTK_POS_TOP);
        gtk_popover_set_pointing_to(GTK_POPOVER(state->signaturePopover), &rect);
        gtk_widget_show_all(state->signaturePopover);
    }

    gboolean updateSignatureHintLater(gpointer data) {
        GtkWidget* editor = GTK_WIDGET(data);
        const auto state = std::find_if(editorCompletionStates.begin(), editorCompletionStates.end(), [editor](const EditorCompletionState& value) { return value.editor == editor; });
        if (state == editorCompletionStates.end()) return G_SOURCE_REMOVE;
        state->signatureTimer = 0;
        updateSignatureHint(editor);
        return G_SOURCE_REMOVE;
    }

    void scheduleSignatureHint(GtkWidget* editor) {
        const auto state = std::find_if(editorCompletionStates.begin(), editorCompletionStates.end(), [editor](const EditorCompletionState& value) { return value.editor == editor; });
        if (state == editorCompletionStates.end() || !state->completionArmed || state->signatureTimer != 0) return;
        state->signatureTimer = g_idle_add(updateSignatureHintLater, editor);
    }

    gboolean onEditorWindowConfigure(GtkWidget* window, GdkEventConfigure*, gpointer) {
        for (auto& state : editorCompletionStates) {
            if (state.editor == nullptr || gtk_widget_get_toplevel(state.editor) != window) continue;
            state.restoreSignature = state.signaturePopover != nullptr && gtk_widget_get_visible(state.signaturePopover);
            if (state.signaturePopover != nullptr) gtk_widget_hide(state.signaturePopover);
            if (GTK_SOURCE_IS_VIEW(state.editor)) gtk_source_completion_hide(gtk_source_view_get_completion(GTK_SOURCE_VIEW(state.editor)));
            if (state.restoreTimer != 0) g_source_remove(state.restoreTimer);
            state.restoreTimer = g_timeout_add(180, restoreEditorPopups, state.editor);
        }
        return FALSE;
    }

    gboolean restoreEditorPopups(gpointer data) {
        GtkWidget* editor = GTK_WIDGET(data);
        const auto state = std::find_if(editorCompletionStates.begin(), editorCompletionStates.end(), [editor](const EditorCompletionState& value) { return value.editor == editor; });
        if (state == editorCompletionStates.end()) return G_SOURCE_REMOVE;
        state->restoreTimer = 0;
        const bool restore = state->restoreSignature;
        state->restoreSignature = false;
        if (restore && gtk_widget_get_visible(editor)) updateSignatureHint(editor);
        if (gtk_widget_get_visible(editor)) preserveEditorTooltip(editor);
        return G_SOURCE_REMOVE;
    }

    void watchEditorWindow(GtkWidget* editor) {
        GtkWidget* top = gtk_widget_get_toplevel(editor);
        if (!GTK_IS_WINDOW(top) || g_object_get_data(G_OBJECT(top), "remote-lsp-configure-hook") != nullptr) return;
        g_object_set_data(G_OBJECT(top), "remote-lsp-configure-hook", GINT_TO_POINTER(1));
        g_signal_connect(top, "configure-event", G_CALLBACK(onEditorWindowConfigure), nullptr);
    }

    gboolean watchEditorWindowLater(gpointer data) {
        GtkWidget* editor = GTK_WIDGET(data);
        watchEditorWindow(editor);
        g_object_unref(editor);
        return G_SOURCE_REMOVE;
    }

    G_DEFINE_TYPE_WITH_CODE(RemoteCompletionProvider, remoteCompletionProvider, G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE(GTK_SOURCE_TYPE_COMPLETION_PROVIDER, remoteCompletionProviderInterfaceInit))

    static void remoteCompletionProvider_init(RemoteCompletionProvider* provider) { provider->editor = nullptr; }
    static void remoteCompletionProvider_class_init(RemoteCompletionProviderClass*) {}

    void setCompletionProvider(GtkWidget* editor, bool enabled) {
        if (!GTK_SOURCE_IS_VIEW(editor)) return;
        const auto state = std::find_if(editorCompletionStates.begin(), editorCompletionStates.end(), [editor](const EditorCompletionState& value) { return value.editor == editor; });
        if (state == editorCompletionStates.end()) return;
        GtkSourceCompletion* completion = gtk_source_view_get_completion(GTK_SOURCE_VIEW(editor));
        if (enabled) gtk_source_completion_add_provider(completion, GTK_SOURCE_COMPLETION_PROVIDER(state->provider), nullptr);
        else gtk_source_completion_remove_provider(completion, GTK_SOURCE_COMPLETION_PROVIDER(state->provider), nullptr);
    }

    void applyEditorOptions(GtkWidget* editor) {
        if (!GTK_SOURCE_IS_VIEW(editor)) return;
        GtkSourceView* view = GTK_SOURCE_VIEW(editor);
        GtkSourceBuffer* buffer = GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor)));
        EditorBulkInsertState* bulk = bulkInsertState(GTK_TEXT_BUFFER(buffer));
        gtk_source_view_set_tab_width(view, tabWidth);
        gtk_source_view_set_insert_spaces_instead_of_tabs(view, !useTabs);
        gtk_source_view_set_show_line_numbers(view, showLineNumbers);
        gtk_source_view_set_auto_indent(view, autoIndenting);
        gtk_source_view_set_smart_home_end(view, smartHomeEnd ? GTK_SOURCE_SMART_HOME_END_BEFORE : GTK_SOURCE_SMART_HOME_END_DISABLED);
        gtk_source_buffer_set_highlight_syntax(buffer, syntaxHighlighting && (bulk == nullptr || !bulk->active));
        gtk_source_buffer_set_highlight_matching_brackets(buffer, showBrackets && (bulk == nullptr || !bulk->active));
        gtk_source_buffer_set_highlight_matching_brackets(buffer, showBrackets);
        setEditorFontSize(editor, scriptFontSize);
        GtkWidget* map = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(editor), "remote-control-minimap"));
        GtkWidget* strip = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(editor), "remote-control-minimap-strip"));
        if (showMinimap && map == nullptr) {
            ensureGScriptEditorMinimap(editor);
            map = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(editor), "remote-control-minimap"));
            strip = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(editor), "remote-control-minimap-strip"));
        }
        if (map) {
            gtk_source_map_set_view(GTK_SOURCE_MAP(map), showMinimap ? GTK_SOURCE_VIEW(editor) : nullptr);
            setEditorFontSize(map, minimapFontSize);
            if (showMinimap) {
                gtk_widget_show(map);
                if (strip) gtk_widget_show(strip);
            } else {
                gtk_widget_hide(map);
                if (strip) gtk_widget_hide(strip);
            }
        }
        GtkWidget* scrolled = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(editor), "remote-control-editor-scrolled"));
        if (scrolled != nullptr) g_idle_add(refreshEditorScrollbars, scrolled);
    }

}

void setGScriptEditorCacheDirectory(const std::filesystem::path& directory) { completionCacheFile = directory / "scriptapi.json"; }
void setGScriptEditorConnection(GtkWidget* editor, void* connection) {
    const auto state = std::find_if(editorCompletionStates.begin(), editorCompletionStates.end(), [editor](const EditorCompletionState& value) { return value.editor == editor; });
    if (state != editorCompletionStates.end()) state->connection = connection;
}
void detachGScriptEditorConnections(void* connection) { if (connection == nullptr) return; for (auto& state : editorCompletionStates) if (state.connection == connection) state.connection = nullptr; }
void rebindGScriptEditorConnections(void* disconnectedConnection, void* connection) { for (auto& state : editorCompletionStates) if (state.connection == disconnectedConnection) state.connection = connection; }
void updateGScriptEditorPlayerProperty(void* connection, int playerId, const char* property, const char* value) {
    if (connection == nullptr || property == nullptr) return;
    if (g_ascii_strcasecmp(property, "account") == 0) playerCommunityNames[connection].erase(playerId);
    else if (g_ascii_strcasecmp(property, "community") == 0) playerCommunityNames[connection][playerId] = value == nullptr ? "" : value;
}

void requestGScriptHelp(const std::string& query, std::function<void(std::vector<std::string>)> callback) {
    if (!apiDefinitions.empty()) { callback(formatScriptHelp(apiDefinitions, query)); return; }
    const std::string source = completionSource;
    std::thread([query, source, callback = std::move(callback)] {
        auto* payload = new std::pair<std::function<void(std::vector<std::string>)>, std::vector<std::string>>{std::move(callback), formatScriptHelp(fetchCompletionDefinitions(source), query)};
        g_idle_add_full(G_PRIORITY_DEFAULT, +[](gpointer data) {
            auto* payload = static_cast<std::pair<std::function<void(std::vector<std::string>)>, std::vector<std::string>>*>(data);
            payload->first(std::move(payload->second));
            return G_SOURCE_REMOVE;
        }, payload, +[](gpointer data) { delete static_cast<std::pair<std::function<void(std::vector<std::string>)>, std::vector<std::string>>*>(data); });
    }).detach();
}

void setGScriptEditorOptions(const RC::RCOptions& options) {
    setEditorFormatOptions(options);
    const bool wasLspEnabled = lspEnabled;
    const bool sourceChanged = completionSource != options.autocompletesource;
    tabWidth = std::max(1, options.scripttabwidth);
    scriptFontSize = std::max(6, options.scriptfontsize);
    scriptFontFamily = options.scriptfontfamily.empty() ? "Monospace" : options.scriptfontfamily;
    useTabs = options.scriptusetabs;
    showLineNumbers = options.showlinenumbers;
    showMinimap = options.minimap;
    syntaxHighlighting = options.syntaxhighlighting;
    autoIndenting = options.autoindenting;
    smartHomeEnd = options.smarthomeend;
    showBrackets = options.showbrackets;
    lspEnabled = options.lsp;
    scriptDiagnosticsEnabled = options.scriptdiagnostics;
    for (GtkWidget* editor : completionEditors) applyEditorOptions(editor);
    if (wasLspEnabled != lspEnabled) for (GtkWidget* editor : completionEditors) setCompletionProvider(editor, lspEnabled);
    completionSource = options.autocompletesource.empty() ? "https://api.gscript.dev/" : options.autocompletesource;
    if (sourceChanged || (!wasLspEnabled && lspEnabled)) { apiDefinitions.clear(); apiDefinitionsLoaded = false; apiDefinitionsLoading = false; if (lspEnabled) startCompletionLoad(); }
    else if (lspEnabled && apiDefinitions.empty()) startCompletionLoad();
    for (const auto& state : diagnosticsStates) {
        if (scriptDiagnosticsEnabled) runEditorDiagnostics(state.get());
        else clearEditorDiagnostics(state.get());
    }
}

struct MinimapStrip { GtkBin parent; };
struct MinimapStripClass { GtkBinClass parentClass; };
G_DEFINE_TYPE(MinimapStrip, minimap_strip, GTK_TYPE_BIN)

constexpr gint minimapWidth = 120;

static void minimap_strip_get_preferred_width(GtkWidget*, gint* minimum, gint* natural) {
    if (minimum) *minimum = minimapWidth;
    if (natural) *natural = minimapWidth;
}

static void minimap_strip_get_preferred_width_for_height(GtkWidget*, gint, gint* minimum, gint* natural) {
    if (minimum) *minimum = minimapWidth;
    if (natural) *natural = minimapWidth;
}

static void minimap_strip_class_init(MinimapStripClass* klass) {
    GTK_WIDGET_CLASS(klass)->get_preferred_width = minimap_strip_get_preferred_width;
    GTK_WIDGET_CLASS(klass)->get_preferred_width_for_height = minimap_strip_get_preferred_width_for_height;
}

static void minimap_strip_init(MinimapStrip*) {}

static void updateMinimapViewportIndicator(GtkWidget* scrolled) {
    GtkWidget* map = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(scrolled), "remote-control-minimap"));
    GtkWidget* marker = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(scrolled), "remote-control-minimap-marker"));
    if (!map || !marker) return;
    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
    const bool documentFits = gtk_adjustment_get_upper(adjustment) <= gtk_adjustment_get_page_size(adjustment) + 0.5;
    GtkStyleContext* context = gtk_widget_get_style_context(map);
    if (documentFits) gtk_style_context_add_class(context, "remote-control-minimap-no-scrubber");
    else gtk_style_context_remove_class(context, "remote-control-minimap-no-scrubber");
    if (documentFits) { gtk_widget_hide(marker); return; }
    GtkAllocation allocation;
    gtk_widget_get_allocation(map, &allocation);
    const gdouble pageSize = gtk_adjustment_get_page_size(adjustment);
    const gint markerHeight = std::clamp(static_cast<gint>(std::round(allocation.height * pageSize / gtk_adjustment_get_upper(adjustment))), 12, 48);
    gtk_widget_set_size_request(marker, -1, markerHeight);
    const gdouble range = std::max(0.0, gtk_adjustment_get_upper(adjustment) - pageSize);
    const gdouble progress = range == 0.0 ? 0.0 : gtk_adjustment_get_value(adjustment) / range;
    gtk_widget_set_margin_top(marker, static_cast<gint>(std::round(progress * std::max(0, allocation.height - markerHeight))));
    gtk_widget_show(marker);
}

static void onMinimapAdjustmentChanged(GtkAdjustment*, gpointer userData) { updateMinimapViewportIndicator(GTK_WIDGET(userData)); }
static void onMinimapSizeAllocated(GtkWidget* widget, GtkAllocation*, gpointer) { updateMinimapViewportIndicator(widget); }
namespace { gboolean refreshEditorScrollbars(gpointer data) { GtkWidget* scrolled = GTK_WIDGET(data); gtk_widget_queue_resize(scrolled); gtk_widget_queue_allocate(scrolled); gtk_widget_queue_draw(scrolled); gtk_adjustment_changed(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled))); return G_SOURCE_REMOVE; } }

void ensureGScriptEditorMinimap(GtkWidget* editor) {
    if (g_object_get_data(G_OBJECT(editor), "remote-control-minimap") != nullptr) return;
    GtkWidget* row = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(editor), "remote-control-editor-row"));
    GtkWidget* scrolled = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(editor), "remote-control-editor-scrolled"));
    if (row == nullptr || scrolled == nullptr) return;
    GtkWidget* map = gtk_source_map_new();
    GtkWidget* strip = gtk_overlay_new();
    gtk_widget_set_name(map, "remote-control-minimap");
    gtk_widget_set_name(strip, "remote-control-minimap-strip");
    gtk_widget_set_size_request(map, minimapWidth, -1);
    gtk_widget_set_size_request(strip, minimapWidth, -1);
    if (showMinimap) gtk_source_map_set_view(GTK_SOURCE_MAP(map), GTK_SOURCE_VIEW(editor));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(map), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(map), false);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(map), true);
    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(map), false);
    gtk_source_view_set_show_line_marks(GTK_SOURCE_VIEW(map), false);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(map), GTK_WRAP_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(map), 0);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(map), 0);
    gtk_widget_set_hexpand(strip, false);
    gtk_widget_set_halign(strip, GTK_ALIGN_START);
    gtk_widget_set_hexpand(map, false);
    gtk_widget_set_halign(map, GTK_ALIGN_START);
    setEditorFontSize(map, minimapFontSize);
    gtk_widget_set_no_show_all(map, true);
    gtk_widget_set_no_show_all(strip, true);
    g_object_set_data(G_OBJECT(editor), "remote-control-minimap", map);
    g_object_set_data(G_OBJECT(editor), "remote-control-minimap-strip", strip);
    g_object_set_data(G_OBJECT(scrolled), "remote-control-minimap", map);
    GtkWidget* marker = gtk_event_box_new();
    gtk_widget_set_name(marker, "remote-control-minimap-marker");
    gtk_widget_set_size_request(marker, -1, 12);
    gtk_widget_set_valign(marker, GTK_ALIGN_START);
    gtk_widget_set_halign(marker, GTK_ALIGN_FILL);
    g_object_set_data(G_OBJECT(scrolled), "remote-control-minimap-marker", marker);
    GtkCssProvider* minimapStyle = gtk_css_provider_new();
    gtk_css_provider_load_from_data(minimapStyle, "#remote-control-minimap .scrubber { background-image: none; background-color: transparent; border-color: transparent; box-shadow: none; opacity: 0; }", -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(map), GTK_STYLE_PROVIDER(minimapStyle), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(minimapStyle);
    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
    g_signal_connect(adjustment, "changed", G_CALLBACK(onMinimapAdjustmentChanged), scrolled);
    g_signal_connect(adjustment, "value-changed", G_CALLBACK(onMinimapAdjustmentChanged), scrolled);
    g_signal_connect(scrolled, "size-allocate", G_CALLBACK(onMinimapSizeAllocated), nullptr);
    gtk_container_add(GTK_CONTAINER(strip), map);
    gtk_overlay_add_overlay(GTK_OVERLAY(strip), marker);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(strip), marker, true);
    gtk_box_pack_end(GTK_BOX(row), strip, false, false, 0);
    if (showMinimap) {
        gtk_widget_show(map);
        gtk_widget_show(strip);
    } else gtk_widget_hide(strip);
    g_idle_add(refreshEditorScrollbars, scrolled);
}

GtkWidget* wrapGScriptEditor(GtkWidget* editor, GtkWidget* scrolled) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(row), scrolled, true, true, 0);
    g_object_set_data(G_OBJECT(editor), "remote-control-editor-row", row);
    g_object_set_data(G_OBJECT(editor), "remote-control-editor-scrolled", scrolled);
    if (showMinimap) ensureGScriptEditorMinimap(editor);
    return row;
}

void refreshGScriptEditorTheme() {
    for (GtkWidget* editor : completionEditors) {
        if (!GTK_SOURCE_IS_VIEW(editor)) continue;
        GtkSourceBuffer* buffer = GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor)));
        applyRemoteControlSourceStyle(buffer);
    }
}

void configureGScriptEditor(GtkWidget* editor, bool script) {
    if (!GTK_SOURCE_IS_VIEW(editor)) return;
    applyEditorOptions(editor);
    if (completionEditors.empty() && lspEnabled) startCompletionLoad();
    if (std::find(completionEditors.begin(), completionEditors.end(), editor) == completionEditors.end()) {
        completionEditors.push_back(editor);
        GtkTextBuffer* editorBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
        GdkRGBA selectionColor{};
        gtk_style_context_get_background_color(gtk_widget_get_style_context(editor), GTK_STATE_FLAG_SELECTED, &selectionColor);
        selectionColor.alpha = 0.45;
        GtkTextTag* selectionTag = gtk_text_buffer_create_tag(editorBuffer, nullptr, "background-rgba", &selectionColor, nullptr);
        auto multiSelection = std::make_unique<EditorMultiSelectionState>();
        multiSelection->editor = editor;
        multiSelection->tag = selectionTag;
        multiSelection->applying = false;
        EditorMultiSelectionState* multiSelectionPointer = multiSelection.get();
        multiSelectionStates.push_back(std::move(multiSelection));
        auto bulk = std::make_unique<EditorBulkInsertState>();
        bulk->editor = editor;
        bulk->buffer = GTK_SOURCE_BUFFER(editorBuffer);
        bulk->resumeTimer = 0;
        bulk->active = false;
        bulk->pendingBytes = 0;
        bulk->pendingLines = 0;
        EditorBulkInsertState* bulkPointer = bulk.get();
        bulkInsertStates.push_back(std::move(bulk));
        GdkRGBA errorColor{};
        GdkRGBA warningColor{};
        GdkRGBA infoColor{};
        gdk_rgba_parse(&errorColor, "#f44747");
        gdk_rgba_parse(&warningColor, "#e5a50a");
        gdk_rgba_parse(&infoColor, "#4aa3df");
        EditorDiagnosticsState* diagnosticsPointer = nullptr;
        if (script) {
            auto diagnostics = std::make_unique<EditorDiagnosticsState>();
            diagnostics->editor = editor;
            diagnostics->errorTag = gtk_text_buffer_create_tag(editorBuffer, nullptr, "underline", PANGO_UNDERLINE_ERROR, "underline-rgba", &errorColor, nullptr);
            diagnostics->warningTag = gtk_text_buffer_create_tag(editorBuffer, nullptr, "underline", PANGO_UNDERLINE_ERROR, "underline-rgba", &warningColor, nullptr);
            diagnostics->infoTag = gtk_text_buffer_create_tag(editorBuffer, nullptr, "underline", PANGO_UNDERLINE_SINGLE, "underline-rgba", &infoColor, nullptr);
            diagnostics->timeout = 0;
            diagnosticsPointer = diagnostics.get();
            diagnosticsStates.push_back(std::move(diagnostics));
        }
        gtk_widget_add_events(editor, GDK_BUTTON_PRESS_MASK);
        g_signal_connect(editor, "button-press-event", G_CALLBACK(multiSelectionButtonPress), multiSelectionPointer);
        g_signal_connect(editor, "key-press-event", G_CALLBACK(multiSelectionKeyPress), multiSelectionPointer);
        g_signal_connect_after(editor, "draw", G_CALLBACK(drawMultiSelections), multiSelectionPointer);
        g_signal_connect(editorBuffer, "changed", G_CALLBACK(+[](GtkTextBuffer*, gpointer data) {
            EditorMultiSelectionState* state = static_cast<EditorMultiSelectionState*>(data);
            if (!state->applying && !state->selections.empty()) refreshMultiSelections(state);
        }), multiSelectionPointer);
        g_signal_connect(editorBuffer, "insert-text", G_CALLBACK(onEditorInsertText), bulkPointer);
        if (diagnosticsPointer != nullptr) {
            g_signal_connect(editorBuffer, "changed", G_CALLBACK(+[](GtkTextBuffer*, gpointer data) {
                EditorDiagnosticsState* state = static_cast<EditorDiagnosticsState*>(data);
                if (scriptDiagnosticsEnabled) scheduleEditorDiagnostics(state);
            }), diagnosticsPointer);
            if (scriptDiagnosticsEnabled) scheduleEditorDiagnostics(diagnosticsPointer);
        }
        auto* provider = REMOTE_COMPLETION_PROVIDER(g_object_new(REMOTE_TYPE_COMPLETION_PROVIDER, nullptr));
        provider->editor = editor;
        GtkWidget* signaturePopover = gtk_popover_new(editor);
        gtk_popover_set_position(GTK_POPOVER(signaturePopover), GTK_POS_TOP);
        gtk_popover_set_modal(GTK_POPOVER(signaturePopover), false);
        GtkWidget* signatureLabel = gtk_label_new(nullptr);
        gtk_label_set_line_wrap(GTK_LABEL(signatureLabel), true);
        gtk_label_set_line_wrap_mode(GTK_LABEL(signatureLabel), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_max_width_chars(GTK_LABEL(signatureLabel), 38);
        gtk_widget_set_size_request(signatureLabel, completionPopupMaxWidth - 16, -1);
        gtk_widget_set_hexpand(signatureLabel, FALSE);
        gtk_label_set_xalign(GTK_LABEL(signatureLabel), 0.0F);
        gtk_widget_set_margin_start(signatureLabel, 8);
        gtk_widget_set_margin_end(signatureLabel, 8);
        gtk_widget_set_margin_top(signatureLabel, 6);
        gtk_widget_set_margin_bottom(signatureLabel, 6);
        gtk_container_add(GTK_CONTAINER(signaturePopover), signatureLabel);
        gtk_widget_set_size_request(signaturePopover, completionPopupMaxWidth - 16, -1);
        gtk_widget_set_halign(signaturePopover, GTK_ALIGN_START);
        gtk_popover_set_constrain_to(GTK_POPOVER(signaturePopover), GTK_POPOVER_CONSTRAINT_WINDOW);
        editorCompletionStates.push_back({editor, provider, signaturePopover, signatureLabel, nullptr});
        g_signal_connect(editor, "destroy", G_CALLBACK(+[](GtkWidget* widget, gpointer) {
            const auto state = std::find_if(editorCompletionStates.begin(), editorCompletionStates.end(), [widget](const EditorCompletionState& value) { return value.editor == widget; });
            if (state != editorCompletionStates.end()) {
                if (state->restoreTimer != 0) g_source_remove(state->restoreTimer);
                if (state->signatureTimer != 0) g_source_remove(state->signatureTimer);
                GtkSourceCompletion* completion = gtk_source_view_get_completion(GTK_SOURCE_VIEW(widget));
                gtk_source_completion_remove_provider(completion, GTK_SOURCE_COMPLETION_PROVIDER(state->provider), nullptr);
                gtk_widget_destroy(state->signaturePopover);
                g_object_unref(state->provider);
                editorCompletionStates.erase(state);
            }
            completionEditors.erase(std::remove(completionEditors.begin(), completionEditors.end(), widget), completionEditors.end());
            multiSelectionStates.erase(std::remove_if(multiSelectionStates.begin(), multiSelectionStates.end(), [widget](const auto& value) { return value->editor == widget; }), multiSelectionStates.end());
            const auto bulk = std::find_if(bulkInsertStates.begin(), bulkInsertStates.end(), [widget](const auto& value) { return value->editor == widget; });
            if (bulk != bulkInsertStates.end()) {
                if ((*bulk)->resumeTimer != 0) g_source_remove((*bulk)->resumeTimer);
                bulkInsertStates.erase(bulk);
            }
            const auto diagnostics = std::find_if(diagnosticsStates.begin(), diagnosticsStates.end(), [widget](const auto& value) { return value->editor == widget; });
            if (diagnostics != diagnosticsStates.end()) {
                if ((*diagnostics)->timeout != 0) g_source_remove((*diagnostics)->timeout);
                diagnosticsStates.erase(diagnostics);
            }
        }), nullptr);
    }
    GtkSourceCompletion* completion = gtk_source_view_get_completion(GTK_SOURCE_VIEW(editor));
    GtkSourceCompletionInfo* infoWindow = gtk_source_completion_get_info_window(completion);
    if (infoWindow != nullptr) {
        gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(infoWindow)), "remote-completion-info-window");
        installCompletionToplevelGeometry(GTK_WIDGET(infoWindow));
        if (GTK_IS_WINDOW(infoWindow)) installCompletionPopupGeometry(GTK_WINDOW(infoWindow));
        else gtk_widget_set_size_request(GTK_WIDGET(infoWindow), completionPopupMaxWidth, -1);
    }
    g_signal_connect(completion, "show", G_CALLBACK(+[](GtkSourceCompletion* completion, gpointer data) {
        if (hasActiveSignatureContext(GTK_WIDGET(data))) {
            gtk_source_completion_hide(completion);
            scheduleSignatureHint(GTK_WIDGET(data));
            return;
        }
        scheduleCompletionPopupClamp(completion);
    }), editor);
    g_signal_connect(completion, "hide", G_CALLBACK(+[](GtkSourceCompletion*, gpointer data) { scheduleSignatureHint(GTK_WIDGET(data)); }), editor);
    g_object_set(completion, "auto-complete-delay", 120, "show-headers", FALSE, nullptr);
    if (lspEnabled) setCompletionProvider(editor, true);
    gtk_widget_set_has_tooltip(editor, true);
    g_signal_connect(editor, "query-tooltip", G_CALLBACK(editorQueryTooltip), nullptr);
    GtkTextBuffer* completionBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
    g_signal_connect(completionBuffer, "changed", G_CALLBACK(+[](GtkTextBuffer*, gpointer data) {
        scheduleSignatureHint(GTK_WIDGET(data));
    }), editor);
    g_signal_connect(completionBuffer, "mark-set", G_CALLBACK(+[](GtkTextBuffer* buffer, GtkTextIter*, GtkTextMark* mark, gpointer data) {
        if (mark != gtk_text_buffer_get_insert(buffer)) return;
        GtkWidget* editor = GTK_WIDGET(data);
        const auto state = std::find_if(editorCompletionStates.begin(), editorCompletionStates.end(), [editor](const EditorCompletionState& value) { return value.editor == editor; });
        if (state != editorCompletionStates.end() && state->completionArmed) scheduleSignatureHint(editor);
    }), editor);
    g_signal_connect(editor, "key-press-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventKey* event, gpointer) {
        const auto state = std::find_if(editorCompletionStates.begin(), editorCompletionStates.end(), [widget](const EditorCompletionState& value) { return value.editor == widget; });
        const bool modified = (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_META_MASK | GDK_SUPER_MASK)) != 0;
        if (state != editorCompletionStates.end() && ((!modified && g_unichar_isprint(gdk_keyval_to_unicode(event->keyval))) || ((event->state & GDK_CONTROL_MASK) != 0 && event->keyval == GDK_KEY_space))) state->completionArmed = true;
        preserveEditorTooltip(widget);
        if ((event->state & GDK_CONTROL_MASK) == 0 || event->keyval != GDK_KEY_l) return static_cast<gboolean>(FALSE);
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
        GtkTextIter start;
        gtk_text_buffer_get_iter_at_mark(buffer, &start, gtk_text_buffer_get_insert(buffer));
        gtk_text_iter_set_line_offset(&start, 0);
        GtkTextIter end = start;
        gtk_text_iter_forward_to_line_end(&end);
        gtk_text_buffer_select_range(buffer, &start, &end);
        return static_cast<gboolean>(TRUE);
    }), nullptr);
    g_idle_add(watchEditorWindowLater, g_object_ref(editor));
    g_signal_connect(editor, "scroll-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventScroll* event, gpointer) {
        if ((event->state & GDK_CONTROL_MASK) == 0) return static_cast<gboolean>(FALSE);
        int change = 0;
        if (event->direction == GDK_SCROLL_UP) change = 1;
        else if (event->direction == GDK_SCROLL_DOWN) change = -1;
        else {
            gdouble deltaX = 0.0;
            gdouble deltaY = 0.0;
            if (gdk_event_get_scroll_deltas(reinterpret_cast<GdkEvent*>(event), &deltaX, &deltaY)) change = deltaY < 0.0 ? 1 : deltaY > 0.0 ? -1 : 0;
        }
        if (change == 0) return static_cast<gboolean>(TRUE);
        const int size = std::clamp(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "script-font-size")) + change, 6, 48);
        setEditorFontSize(widget, size);
        return static_cast<gboolean>(TRUE);
    }), nullptr);
}

void setGScriptEditorContent(GtkTextBuffer* buffer, const char* content, gint length) {
    if (buffer == nullptr) return;
    const char* source = content == nullptr ? "" : content;
    gint sourceLength = content == nullptr ? 0 : length;
    gchar* validSource = nullptr;
    if (!g_utf8_validate(source, sourceLength, nullptr)) {
        validSource = g_utf8_make_valid(source, sourceLength);
        source = validSource == nullptr ? "" : validSource;
        sourceLength = -1;
    }
    GtkSourceUndoManager* undoManager = GTK_SOURCE_IS_BUFFER(buffer) ? gtk_source_buffer_get_undo_manager(GTK_SOURCE_BUFFER(buffer)) : nullptr;
    if (undoManager != nullptr) gtk_source_undo_manager_begin_not_undoable_action(undoManager);
    gtk_text_buffer_set_text(buffer, source, sourceLength);
    gtk_text_buffer_set_modified(buffer, FALSE);
    if (undoManager != nullptr) gtk_source_undo_manager_end_not_undoable_action(undoManager);
    g_free(validSource);
}

bool consumeEditorCtrlS(GtkWidget* editor, GdkEventKey* event) {
    if ((event->state & GDK_CONTROL_MASK) == 0 || (event->keyval != GDK_KEY_s && event->keyval != GDK_KEY_S)) return false;
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(editor), "remote-control-ctrl-s-down")) != 0) return false;
    g_object_set_data(G_OBJECT(editor), "remote-control-ctrl-s-down", GINT_TO_POINTER(1));
    return true;
}

gboolean releaseEditorCtrlS(GtkWidget* editor, GdkEventKey* event) {
    if (event->keyval == GDK_KEY_s || event->keyval == GDK_KEY_S) g_object_set_data(G_OBJECT(editor), "remote-control-ctrl-s-down", GINT_TO_POINTER(0));
    return FALSE;
}

GtkWidget* createGScriptEditorLineStatus(GtkWidget* editor) {
    GtkWidget* status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget* label = gtk_label_new("Line: 1");
    gtk_widget_set_margin_start(label, 8);
    gtk_widget_set_margin_end(label, 8);
    gtk_widget_set_margin_top(label, 5);
    gtk_widget_set_margin_bottom(label, 5);
    gtk_box_pack_start(GTK_BOX(status), label, false, false, 0);
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
    g_signal_connect(buffer, "mark-set", G_CALLBACK(+[](GtkTextBuffer* textBuffer, GtkTextIter* location, GtkTextMark* mark, gpointer data) {
        if (mark != gtk_text_buffer_get_insert(textBuffer)) return;
        gchar* value = g_strdup_printf("Line: %d", gtk_text_iter_get_line(location) + 1);
        gtk_label_set_text(GTK_LABEL(data), value);
        g_free(value);
    }), label);
    return status;
}

void addGScriptEditorLineStatus(GtkDialog* dialog, GtkWidget* editor) {
    GtkWidget* actionArea = gtk_dialog_get_action_area(dialog);
    GtkWidget* contentArea = gtk_dialog_get_content_area(dialog);
    GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* status = createGScriptEditorLineStatus(editor);
    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* spacer = gtk_label_new(nullptr);
    GtkWidget* goToLine = nullptr;
    GtkWidget* format = nullptr;
    GtkWidget* find = nullptr;
    GtkWidget* apply = nullptr;
    GtkWidget* close = nullptr;
    GList* remaining = nullptr;
    GList* children = gtk_container_get_children(GTK_CONTAINER(actionArea));
    for (GList* child = children; child != nullptr; child = child->next) {
        GtkWidget* button = GTK_WIDGET(child->data);
        g_object_ref(button);
        gtk_container_remove(GTK_CONTAINER(actionArea), button);
        const char* label = GTK_IS_BUTTON(button) ? gtk_button_get_label(GTK_BUTTON(button)) : nullptr;
        if (g_strcmp0(label, "Go to line") == 0) goToLine = button;
        else if (g_strcmp0(label, "Format") == 0) format = button;
        else if (g_strcmp0(label, "Find") == 0) find = button;
        else if (g_strcmp0(label, "Apply") == 0 || g_strcmp0(label, "Save") == 0) apply = button;
        else if (g_strcmp0(label, "Close") == 0 || g_strcmp0(label, "Cancel") == 0) close = button;
        else remaining = g_list_append(remaining, button);
    }
    g_list_free(children);
    auto addButtonIcon = [](GtkWidget* button, const char* icon) { if (button != nullptr) { gtk_button_set_image(GTK_BUTTON(button), gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON)); gtk_button_set_always_show_image(GTK_BUTTON(button), true); } };
    addButtonIcon(goToLine, "go-jump-symbolic");
    addButtonIcon(format, "format-text-bold-symbolic");
    addButtonIcon(find, "edit-find-symbolic");
    addButtonIcon(apply, "document-save-symbolic");
    addButtonIcon(close, "window-close-symbolic");
    gtk_widget_set_margin_start(status, 5);
    gtk_widget_set_margin_end(status, 5);
    gtk_widget_set_margin_top(actions, 3);
    gtk_widget_set_margin_bottom(actions, 3);
    gtk_widget_set_margin_end(actions, 5);
    gtk_widget_set_hexpand(spacer, true);
    if (goToLine != nullptr) gtk_box_pack_start(GTK_BOX(actions), goToLine, false, false, 0);
    if (format != nullptr) gtk_box_pack_start(GTK_BOX(actions), format, false, false, 0);
    if (find != nullptr) gtk_box_pack_start(GTK_BOX(actions), find, false, false, 0);
    if (apply != nullptr) gtk_box_pack_start(GTK_BOX(actions), apply, false, false, 0);
    if (close != nullptr) gtk_box_pack_start(GTK_BOX(actions), close, false, false, 0);
    for (GList* item = remaining; item != nullptr; item = item->next) gtk_box_pack_start(GTK_BOX(actions), GTK_WIDGET(item->data), false, false, 0);
    for (GList* item = remaining; item != nullptr; item = item->next) g_object_unref(item->data);
    g_list_free(remaining);
    if (goToLine != nullptr) g_object_unref(goToLine);
    if (format != nullptr) g_object_unref(format);
    if (find != nullptr) g_object_unref(find);
    if (apply != nullptr) g_object_unref(apply);
    if (close != nullptr) g_object_unref(close);
    gtk_box_pack_start(GTK_BOX(footer), status, false, false, 0);
    gtk_box_pack_start(GTK_BOX(footer), spacer, true, true, 0);
    gtk_box_pack_end(GTK_BOX(footer), actions, false, false, 0);
    gtk_widget_set_no_show_all(actionArea, true);
    gtk_widget_hide(actionArea);
    gtk_box_pack_end(GTK_BOX(contentArea), footer, false, false, 0);
}
