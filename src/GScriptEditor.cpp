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
    bool useTabs = false;
    GtkSourceCompletionWords* completionWords = nullptr;
    GtkSourceBuffer* completionBuffer = nullptr;
    bool completionLoadStarted = false;

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
        while (position < json.size()) {
            skipWhitespace(json, position);
            if (position < json.size() && json[position] == '}') break;
            std::string name;
            if (!readString(json, position, name)) return "";
            skipWhitespace(json, position);
            if (position >= json.size() || json[position++] != ':') return "";
            if (!skipValue(json, position)) return "";
            if (!name.empty()) names.push_back(std::move(name));
            skipWhitespace(json, position);
            if (position < json.size() && json[position] == ',') ++position;
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
        static constexpr const char request[] = "GET / HTTP/1.1\r\nHost: api.gscript.dev\r\nAccept: application/json\r\nConnection: close\r\n\r\n";
        if (BIO_write(connection, request, sizeof(request) - 1) != sizeof(request) - 1) return "";
        std::string response;
        char buffer[8192];
        for (int count; (count = BIO_read(connection, buffer, sizeof(buffer))) > 0;) response.append(buffer, count);
        const std::size_t body = response.find("\r\n\r\n");
        if (body == std::string::npos) return "";
        const std::string payload = response.substr(body + 4);
        return completionText(response.find("Transfer-Encoding: chunked") == std::string::npos ? payload : decodeChunkedBody(payload));
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

}

void setGScriptEditorOptions(const RC::RCOptions& options) {
    tabWidth = std::max(1, options.scripttabwidth);
    useTabs = options.scriptusetabs;
}

void configureGScriptEditor(GtkWidget* editor) {
    if (!GTK_SOURCE_IS_VIEW(editor)) return;
    gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(editor), tabWidth);
    gtk_source_view_set_insert_spaces_instead_of_tabs(GTK_SOURCE_VIEW(editor), !useTabs);
    if (completionWords == nullptr) {
        completionBuffer = gtk_source_buffer_new(nullptr);
        completionWords = gtk_source_completion_words_new("GScript API", nullptr);
        gtk_source_completion_words_register(completionWords, GTK_TEXT_BUFFER(completionBuffer));
        startCompletionLoad();
    }
    gtk_source_completion_add_provider(gtk_source_view_get_completion(GTK_SOURCE_VIEW(editor)), GTK_SOURCE_COMPLETION_PROVIDER(completionWords), nullptr);
}
