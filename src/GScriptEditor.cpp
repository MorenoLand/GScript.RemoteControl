#include "GScriptEditor.h"

#include <gtksourceview/gtksource.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

    int tabWidth = 2;
    int scriptFontSize = 10;
    bool useTabs = false;
    bool showLineNumbers = true;
    bool lspEnabled = true;
    GtkSourceCompletionWords* completionWords = nullptr;
    GtkSourceBuffer* completionBuffer = nullptr;
    bool completionLoadStarted = false;
    std::vector<GtkWidget*> completionEditors;

    void setEditorFontSize(GtkWidget* editor, int size) {
        PangoFontDescription* font = pango_font_description_from_string(("Monospace " + std::to_string(size)).c_str());
        gtk_widget_override_font(editor, font);
        pango_font_description_free(font);
        g_object_set_data(G_OBJECT(editor), "script-font-size", GINT_TO_POINTER(size));
    }

    void skipWhitespace(const std::string& text, std::size_t& position) {
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0) ++position;
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

    std::string completionText(const std::string& json) {
        std::size_t position = 0;
        skipWhitespace(json, position);
        if (position >= json.size() || json[position++] != '{') return "";
        std::vector<std::string> names;
        int depth = 1;
        while (position < json.size()) {
            if (json[position] == '"') {
                std::string name;
                if (!readString(json, position, name)) return "";
                std::size_t value = position;
                skipWhitespace(json, value);
                if (depth == 1 && value < json.size() && json[value++] == ':') {
                    skipWhitespace(json, value);
                    if (value < json.size() && json[value] == '{' && !name.empty()) names.push_back(std::move(name));
                }
                continue;
            }
            if (json[position] == '{' || json[position] == '[') ++depth;
            else if (json[position] == '}' || json[position] == ']') --depth;
            ++position;
        }
        std::sort(names.begin(), names.end());
        std::string result;
        for (const std::string& name : names) result += name + '\n';
        return result;
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

    std::string fetchCompletionText() {
        SSL_CTX* context = SSL_CTX_new(TLS_client_method());
        if (context == nullptr) return "";
        std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> contextGuard(context, SSL_CTX_free);
        SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
        if (SSL_CTX_set_default_verify_paths(context) != 1) return "";
        BIO* connection = BIO_new_ssl_connect(context);
        if (connection == nullptr) return "";
        std::unique_ptr<BIO, decltype(&BIO_free_all)> connectionGuard(connection, BIO_free_all);
        BIO_set_conn_hostname(connection, "api.gscript.dev:443");
        SSL* ssl = nullptr;
        BIO_get_ssl(connection, &ssl);
        if (ssl == nullptr || SSL_set_tlsext_host_name(ssl, "api.gscript.dev") != 1 || BIO_do_connect(connection) != 1 || SSL_get_verify_result(ssl) != X509_V_OK) return "";
        static constexpr const char request[] = "GET / HTTP/1.1\r\nHost: api.gscript.dev\r\nUser-Agent: RemoteControl/1.0\r\nAccept: application/json\r\nConnection: close\r\n\r\n";
        if (BIO_write(connection, request, sizeof(request) - 1) != sizeof(request) - 1) return "";
        std::string response;
        char buffer[8192];
        for (int count; (count = BIO_read(connection, buffer, sizeof(buffer))) > 0;) response.append(buffer, count);
        const std::size_t body = response.find("\r\n\r\n");
        if (body == std::string::npos) return "";
        const std::string payload = response.substr(body + 4);
        std::string headers = response.substr(0, body);
        std::transform(headers.begin(), headers.end(), headers.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        return completionText(headers.find("transfer-encoding: chunked") == std::string::npos ? payload : decodeChunkedBody(payload));
    }

    gboolean applyCompletionText(gpointer data) {
        std::unique_ptr<std::string> text(static_cast<std::string*>(data));
        if (completionBuffer != nullptr && !text->empty()) gtk_text_buffer_set_text(GTK_TEXT_BUFFER(completionBuffer), text->c_str(), -1);
        return G_SOURCE_REMOVE;
    }

    void startCompletionLoad() {
        if (completionLoadStarted) return;
        completionLoadStarted = true;
        std::thread([] {
            auto* text = new std::string(fetchCompletionText());
            g_idle_add(applyCompletionText, text);
        }).detach();
    }

    void setCompletionProvider(GtkWidget* editor, bool enabled) {
        if (!GTK_SOURCE_IS_VIEW(editor) || completionWords == nullptr) return;
        GtkSourceCompletion* completion = gtk_source_view_get_completion(GTK_SOURCE_VIEW(editor));
        if (enabled) gtk_source_completion_add_provider(completion, GTK_SOURCE_COMPLETION_PROVIDER(completionWords), nullptr);
        else gtk_source_completion_remove_provider(completion, GTK_SOURCE_COMPLETION_PROVIDER(completionWords), nullptr);
    }

}

void setGScriptEditorOptions(const RC::RCOptions& options) {
    const bool wasLspEnabled = lspEnabled;
    tabWidth = std::max(1, options.scripttabwidth);
    scriptFontSize = std::max(6, options.scriptfontsize);
    useTabs = options.scriptusetabs;
    showLineNumbers = options.showlinenumbers;
    lspEnabled = options.lsp;
    if (completionWords != nullptr && wasLspEnabled != lspEnabled) for (GtkWidget* editor : completionEditors) setCompletionProvider(editor, lspEnabled);
}

void configureGScriptEditor(GtkWidget* editor) {
    if (!GTK_SOURCE_IS_VIEW(editor)) return;
    gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(editor), tabWidth);
    gtk_source_view_set_insert_spaces_instead_of_tabs(GTK_SOURCE_VIEW(editor), !useTabs);
    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(editor), showLineNumbers);
    setEditorFontSize(editor, scriptFontSize);
    if (completionWords == nullptr) {
        completionBuffer = gtk_source_buffer_new(nullptr);
        completionWords = gtk_source_completion_words_new("GScript API", nullptr);
        g_object_set(completionWords, "minimum-word-size", 1, nullptr);
        gtk_source_completion_words_register(completionWords, GTK_TEXT_BUFFER(completionBuffer));
        startCompletionLoad();
    }
    if (std::find(completionEditors.begin(), completionEditors.end(), editor) == completionEditors.end()) {
        completionEditors.push_back(editor);
        GtkTextBuffer* scriptBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
        gtk_source_completion_words_register(completionWords, scriptBuffer);
        g_signal_connect(editor, "destroy", G_CALLBACK(+[](GtkWidget* widget, gpointer) {
            if (completionWords != nullptr) gtk_source_completion_words_unregister(completionWords, gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget)));
            completionEditors.erase(std::remove(completionEditors.begin(), completionEditors.end(), widget), completionEditors.end());
        }), nullptr);
    }
    GtkSourceCompletion* completion = gtk_source_view_get_completion(GTK_SOURCE_VIEW(editor));
    g_object_set(completion, "auto-complete-delay", 120, nullptr);
    if (lspEnabled) gtk_source_completion_add_provider(completion, GTK_SOURCE_COMPLETION_PROVIDER(completionWords), nullptr);
    g_signal_connect(editor, "key-press-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventKey* event, gpointer) {
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
