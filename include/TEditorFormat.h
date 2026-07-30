#pragma once

#include "TRCOptions.h"
#include <algorithm>
#include <gtk/gtk.h>
#include <string>
#include <vector>

inline int editorFormatIndentWidth = 2;
inline bool editorFormatUseTabs = false;
inline bool editorFormatTrimTrailing = true;
inline bool editorRemoveLineComments = true;
inline bool editorRemoveBlockComments = true;
inline bool editorPreserveClientside = true;

inline void setEditorFormatOptions(const RC::RCOptions& options) {
    editorFormatIndentWidth = std::clamp(options.formatindentwidth, 1, 16);
    editorFormatUseTabs = options.formatusetabs;
    editorFormatTrimTrailing = options.formattrimtrailing;
    editorRemoveLineComments = options.removelinecomments;
    editorRemoveBlockComments = options.removeblockcomments;
    editorPreserveClientside = options.preserveclientside;
}

inline std::string editorTrimTrailingWhitespace(const std::string& line) {
    const std::size_t end = line.find_last_not_of(" \t");
    return end == std::string::npos ? "" : line.substr(0, end + 1);
}

inline std::vector<std::string> editorSplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) return lines;
        start = end + 1;
    }
    if (text.empty() || text.back() == '\n') lines.emplace_back();
    return lines;
}

inline std::string editorJoinLines(const std::vector<std::string>& lines) {
    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index != 0) result += '\n';
        result += lines[index];
    }
    return result;
}

inline int editorGraalScriptBraceDelta(const std::string& line, bool& inBlockComment) {
    int delta = 0;
    char quote = '\0';
    bool escaped = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        const char next = index + 1 < line.size() ? line[index + 1] : '\0';
        if (inBlockComment) {
            if (character == '*' && next == '/') { inBlockComment = false; ++index; }
            continue;
        }
        if (quote != '\0') {
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == quote) quote = '\0';
            continue;
        }
        if (character == '/' && next == '*') { inBlockComment = true; ++index; continue; }
        if (character == '/' && next == '/') break;
        if (character == '"' || character == '\'') { quote = character; continue; }
        if (character == '{') ++delta;
        else if (character == '}') --delta;
    }
    return delta;
}

inline int editorGraalScriptLeadingClosures(const std::string& line) {
    std::size_t index = line.find_first_not_of(" \t");
    int count = 0;
    while (index != std::string::npos && index < line.size() && line[index] == '}') { ++count; ++index; }
    return count;
}

inline std::string formatGraalScriptCode(const std::string& text) {
    std::vector<std::string> lines = editorSplitLines(text);
    int depth = 0;
    bool inBlockComment = false;
    for (std::string& line : lines) {
        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        const std::string content = editorFormatTrimTrailing ? editorTrimTrailingWhitespace(line.substr(first)) : line.substr(first);
        const int lineDepth = std::max(0, depth - editorGraalScriptLeadingClosures(content));
        line.assign(static_cast<std::size_t>(editorFormatUseTabs ? lineDepth : lineDepth * editorFormatIndentWidth), editorFormatUseTabs ? '\t' : ' ');
        line += content;
        depth = std::max(0, depth + editorGraalScriptBraceDelta(content, inBlockComment));
    }
    return editorJoinLines(lines);
}

inline std::string removeGraalScriptComments(const std::string& text) {
    std::string result;
    char quote = '\0';
    bool escaped = false;
    bool inBlockComment = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char character = text[index];
        const char next = index + 1 < text.size() ? text[index + 1] : '\0';
        if (inBlockComment) {
            if (!editorRemoveBlockComments) result += character;
            if (character == '*' && next == '/') { if (!editorRemoveBlockComments) result += next; inBlockComment = false; ++index; }
            else if (editorRemoveBlockComments && character == '\n') result += character;
            continue;
        }
        if (quote != '\0') {
            result += character;
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == quote) quote = '\0';
            continue;
        }
        if (character == '"' || character == '\'' || character == '`') { quote = character; result += character; continue; }
        if (character == '/' && next == '*') { if (!editorRemoveBlockComments) result += "/*"; inBlockComment = true; ++index; continue; }
        if (character == '/' && next == '/') {
            const std::size_t lineStart = text.rfind('\n', index);
            const std::size_t contentStart = lineStart == std::string::npos ? 0 : lineStart + 1;
            const std::size_t lineEnd = text.find('\n', index);
            std::string directive = text.substr(index, lineEnd == std::string::npos ? std::string::npos : lineEnd - index);
            const std::size_t directiveEnd = directive.find_last_not_of(" \t\r");
            directive = directiveEnd == std::string::npos ? "" : directive.substr(0, directiveEnd + 1);
            if (!editorRemoveLineComments || (editorPreserveClientside && text.substr(contentStart, index - contentStart).find_first_not_of(" \t\r") == std::string::npos && directive == "//#CLIENTSIDE")) {
                result += text.substr(index, lineEnd == std::string::npos ? std::string::npos : lineEnd - index);
                index = lineEnd == std::string::npos ? text.size() : lineEnd - 1;
            } else index = lineEnd == std::string::npos ? text.size() : lineEnd - 1;
            continue;
        }
        result += character;
    }
    return result;
}

inline void replaceEditorText(GtkWidget* editor, const std::string& text) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
    const int offset = gtk_text_iter_get_offset(&cursor);
    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_set_text(buffer, text.c_str(), static_cast<gint>(text.size()));
    gtk_text_buffer_end_user_action(buffer);
    gtk_text_buffer_get_iter_at_offset(buffer, &cursor, std::min(offset, gtk_text_buffer_get_char_count(buffer)));
    gtk_text_buffer_place_cursor(buffer, &cursor);
}

inline std::string getEditorText(GtkWidget* editor) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, false);
    const std::string result = text == nullptr ? "" : text;
    g_free(text);
    return result;
}

inline std::string formatEditorText(const std::string& text) { return formatGraalScriptCode(removeGraalScriptComments(text)); }
inline void editorFormatCode(GtkButton*, gpointer data) { replaceEditorText(GTK_WIDGET(data), formatEditorText(getEditorText(GTK_WIDGET(data)))); }

inline GtkWidget* createEditorFormatButton(GtkWidget* editor) {
    GtkWidget* button = gtk_button_new_with_label("Format");
    gtk_button_set_image(GTK_BUTTON(button), gtk_image_new_from_icon_name("format-text-bold-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(button), true);
    g_signal_connect(button, "clicked", G_CALLBACK(editorFormatCode), editor);
    gtk_widget_set_tooltip_text(button, "Format using Formatter Options");
    return button;
}
