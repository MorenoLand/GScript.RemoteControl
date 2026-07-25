#include "GScriptEditor.h"
#include "Theme.h"

#include <gtksourceview/gtksource.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

    int tabWidth = 2;
    int scriptFontSize = 10;
    constexpr gint minimapFontSize = 3;
    bool useTabs = false;
    bool showLineNumbers = true;
    bool showMinimap = false;
    bool syntaxHighlighting = true;
    bool autoIndenting = true;
    bool smartHomeEnd = true;
    bool showBrackets = true;
    bool lspEnabled = true;
    std::string completionSource = "https://api.gscript.dev/";
    std::filesystem::path completionCacheFile;
    unsigned int completionRequest = 0;
    std::vector<GtkWidget*> completionEditors;
    struct ApiDefinition { std::string name; std::string type; std::vector<std::string> params; std::string returns; std::string scope; std::string description; std::string example; };
    std::vector<ApiDefinition> apiDefinitions;
    typedef struct _RemoteCompletionProvider { GObject parent; GtkWidget* editor; } RemoteCompletionProvider;
    typedef struct _RemoteCompletionProviderClass { GObjectClass parentClass; } RemoteCompletionProviderClass;
    struct EditorCompletionState { GtkWidget* editor; RemoteCompletionProvider* provider; GtkWidget* signaturePopover; GtkWidget* signatureLabel; };
    std::vector<EditorCompletionState> editorCompletionStates;
    struct CompletionPayload { unsigned int request; std::vector<ApiDefinition> definitions; };

    GType remoteCompletionProvider_get_type();
    #define REMOTE_TYPE_COMPLETION_PROVIDER (remoteCompletionProvider_get_type())
    #define REMOTE_COMPLETION_PROVIDER(value) (G_TYPE_CHECK_INSTANCE_CAST((value), REMOTE_TYPE_COMPLETION_PROVIDER, RemoteCompletionProvider))

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
            if (key == "params") { if (!readStringArray(text, position, definition.params)) return false; }
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
        if (position >= json.size() || json[position++] != '{') return definitions;
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
        if (payload->request == completionRequest && !payload->definitions.empty()) apiDefinitions = payload->definitions;
        return G_SOURCE_REMOVE;
    }

    void startCompletionLoad() {
        const unsigned int request = ++completionRequest;
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

    bool findEditorDefinition(GtkWidget* editor, const std::string& name, ApiDefinition& result) {
        const std::string lowerName = lowerText(name);
        for (const ApiDefinition& definition : localFunctionDefinitions(editor)) if (lowerText(definition.name) == lowerName) { result = definition; return true; }
        if (const ApiDefinition* definition = findDefinition(name)) { result = *definition; return true; }
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
        ApiDefinition definition;
        if (!findEditorDefinition(editor, wordAtIter(iter), definition)) return false;
        std::string signature = definition.name + "(";
        for (std::size_t index = 0; index < definition.params.size(); ++index) { if (index != 0) signature += ", "; signature += definition.params[index]; }
        signature += ')';
        gchar* escapedSignature = g_markup_escape_text(signature.c_str(), -1);
        gchar* escapedDescription = g_markup_escape_text(definition.description.c_str(), -1);
        gchar* escapedExample = g_markup_escape_text(definition.example.c_str(), -1);
        const std::string scope = upperCase(definition.scope);
        const std::string returns = definition.returns.empty() ? "" : "Returns: " + definition.returns;
        gchar* markup = g_strdup_printf("<b>%s</b>  <i>%s</i>\n%s%s%s%s%s", escapedSignature, scope.c_str(), returns.c_str(), definition.description.empty() ? "" : "\n", escapedDescription, definition.example.empty() ? "" : "\n\nExample:\n", escapedExample);
        gtk_tooltip_set_markup(tooltip, markup);
        g_free(markup);
        g_free(escapedSignature);
        g_free(escapedDescription);
        g_free(escapedExample);
        return true;
    }

    gchar* remoteCompletionProviderGetName(GtkSourceCompletionProvider*) { return g_strdup(""); }

    GtkSourceCompletionActivation remoteCompletionProviderGetActivation(GtkSourceCompletionProvider*) { return static_cast<GtkSourceCompletionActivation>(GTK_SOURCE_COMPLETION_ACTIVATION_INTERACTIVE | GTK_SOURCE_COMPLETION_ACTIVATION_USER_REQUESTED); }

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
        const std::size_t dot = completionContext.rfind('.');
        const std::string objectPrefix = dot == std::string::npos ? "" : completionContext.substr(0, dot + 1);
        if (prefix.size() < 2 && gtk_source_completion_context_get_activation(context) == GTK_SOURCE_COMPLETION_ACTIVATION_INTERACTIVE) { gtk_source_completion_context_add_proposals(context, provider, nullptr, true); return; }
        std::set<std::string> seen;
        GList* proposals = nullptr;
        std::vector<ApiDefinition> definitions = localFunctionDefinitions(remote->editor);
        definitions.insert(definitions.end(), apiDefinitions.begin(), apiDefinitions.end());
        for (const ApiDefinition& definition : definitions) {
            if (!seen.insert(lowerText(definition.name)).second) continue;
            if (!prefix.empty() && lowerText(definition.name).rfind(prefix, 0) != 0) continue;
            std::string label = definition.name;
            if (!definition.params.empty()) {
                label += '(';
                for (std::size_t index = 0; index < definition.params.size(); ++index) { if (index != 0) label += ", "; label += definition.params[index]; }
                label += ')';
            }
            GtkSourceCompletionItem* item = gtk_source_completion_item_new(label.c_str(), definition.name.c_str(), nullptr, definitionInfo(definition).c_str());
            proposals = g_list_prepend(proposals, item);
        }
        for (const std::string& name : localIdentifiers(remote->editor)) {
            const std::string lowerName = lowerText(name);
            std::string insertText = name;
            if (!objectPrefix.empty()) {
                if (lowerName.rfind(lowerText(objectPrefix), 0) != 0) continue;
                insertText = name.substr(objectPrefix.size());
            }
            if ((!prefix.empty() && lowerText(insertText).rfind(prefix, 0) != 0) || !seen.insert(lowerText(insertText)).second) continue;
            GtkSourceCompletionItem* item = gtk_source_completion_item_new(insertText.c_str(), insertText.c_str(), nullptr, "Current script identifier");
            proposals = g_list_prepend(proposals, item);
        }
        gtk_source_completion_context_add_proposals(context, provider, g_list_reverse(proposals), true);
        g_list_free_full(proposals, g_object_unref);
    }

    GtkWidget* remoteCompletionProviderGetInfoWidget(GtkSourceCompletionProvider*, GtkSourceCompletionProposal*) {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_style_context_add_class(gtk_widget_get_style_context(box), "remote-completion-info");
        gtk_widget_set_size_request(box, 360, -1);
        GtkWidget* signature = gtk_label_new(nullptr);
        gtk_style_context_add_class(gtk_widget_get_style_context(signature), "remote-completion-signature");
        gtk_label_set_xalign(GTK_LABEL(signature), 0.0F);
        gtk_label_set_line_wrap(GTK_LABEL(signature), true);
        gtk_label_set_selectable(GTK_LABEL(signature), true);
        GtkWidget* details = gtk_label_new(nullptr);
        gtk_style_context_add_class(gtk_widget_get_style_context(details), "remote-completion-details");
        gtk_label_set_xalign(GTK_LABEL(details), 0.0F);
        gtk_label_set_line_wrap(GTK_LABEL(details), true);
        gtk_label_set_max_width_chars(GTK_LABEL(details), 64);
        gtk_label_set_selectable(GTK_LABEL(details), true);
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
        gtk_widget_set_margin_start(box, 10);
        gtk_widget_set_margin_end(box, 10);
        gtk_widget_set_margin_top(box, 7);
        gtk_widget_set_margin_bottom(box, 7);
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

    void updateSignatureHint(GtkWidget* editor) {
        const auto state = std::find_if(editorCompletionStates.begin(), editorCompletionStates.end(), [editor](const EditorCompletionState& value) { return value.editor == editor; });
        if (state == editorCompletionStates.end() || state->signaturePopover == nullptr || state->signatureLabel == nullptr || !lspEnabled) return;
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
        GtkTextIter lineStart = iter;
        gtk_text_iter_set_line_offset(&lineStart, 0);
        gchar* lineValue = gtk_text_iter_get_text(&lineStart, &iter);
        const std::string line = lineValue == nullptr ? "" : lineValue;
        g_free(lineValue);
        int depth = 0;
        std::size_t open = std::string::npos;
        for (std::size_t index = line.size(); index-- > 0;) {
            if (line[index] == ')') ++depth;
            else if (line[index] == '(') { if (depth == 0) { open = index; break; } --depth; }
        }
        if (open == std::string::npos) { gtk_widget_hide(state->signaturePopover); return; }
        std::size_t nameEnd = open;
        while (nameEnd > 0 && g_ascii_isspace(line[nameEnd - 1])) --nameEnd;
        std::size_t nameStart = nameEnd;
        while (nameStart > 0 && (g_ascii_isalnum(line[nameStart - 1]) || line[nameStart - 1] == '_' || line[nameStart - 1] == '$')) --nameStart;
        ApiDefinition definition;
        if (!findEditorDefinition(editor, line.substr(nameStart, nameEnd - nameStart), definition) || definition.params.empty()) { gtk_widget_hide(state->signaturePopover); return; }
        int argument = 0;
        depth = 0;
        for (std::size_t index = open + 1; index < line.size(); ++index) { if (line[index] == '(') ++depth; else if (line[index] == ')' && depth > 0) --depth; else if (line[index] == ',' && depth == 0) ++argument; }
        argument = std::min(argument, static_cast<int>(definition.params.size()) - 1);
        std::string markup = "<b>" + definitionSignature(definition) + "</b>\n";
        for (std::size_t index = 0; index < definition.params.size(); ++index) { if (index != 0) markup += ", "; const gchar* escaped = g_markup_escape_text(definition.params[index].c_str(), -1); markup += index == static_cast<std::size_t>(argument) ? "<b><u>" + std::string(escaped) + "</u></b>" : escaped; g_free(const_cast<gchar*>(escaped)); }
        if (!definition.description.empty()) { const gchar* escaped = g_markup_escape_text(definition.description.c_str(), -1); markup += "\n" + std::string(escaped); g_free(const_cast<gchar*>(escaped)); }
        gtk_label_set_markup(GTK_LABEL(state->signatureLabel), markup.c_str());
        GdkRectangle rect;
        gtk_text_view_get_iter_location(GTK_TEXT_VIEW(editor), &iter, &rect);
        gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(editor), GTK_TEXT_WINDOW_WIDGET, rect.x, rect.y + rect.height, &rect.x, &rect.y);
        rect.width = 1;
        rect.height = 1;
        gtk_popover_set_pointing_to(GTK_POPOVER(state->signaturePopover), &rect);
        gtk_widget_show_all(state->signaturePopover);
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
        gtk_source_view_set_tab_width(view, tabWidth);
        gtk_source_view_set_insert_spaces_instead_of_tabs(view, !useTabs);
        gtk_source_view_set_show_line_numbers(view, showLineNumbers);
        gtk_source_view_set_auto_indent(view, autoIndenting);
        gtk_source_view_set_smart_home_end(view, smartHomeEnd ? GTK_SOURCE_SMART_HOME_END_BEFORE : GTK_SOURCE_SMART_HOME_END_DISABLED);
        gtk_source_buffer_set_highlight_syntax(buffer, syntaxHighlighting);
        gtk_source_buffer_set_highlight_matching_brackets(buffer, showBrackets);
        gtk_source_buffer_set_highlight_matching_brackets(buffer, showBrackets);
        setEditorFontSize(editor, scriptFontSize);
        GtkWidget* map = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(editor), "remote-control-minimap"));
        GtkWidget* strip = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(editor), "remote-control-minimap-strip"));
        if (map) {
            gtk_source_map_set_view(GTK_SOURCE_MAP(map), showMinimap ? GTK_SOURCE_VIEW(editor) : nullptr);
            setEditorFontSize(map, minimapFontSize);
            if (showMinimap) {
                gtk_widget_show(map);
                if (strip) gtk_widget_show(strip);
            } else if (strip) gtk_widget_hide(strip);
        }
    }

}

void setGScriptEditorCacheDirectory(const std::filesystem::path& directory) { completionCacheFile = directory / "scriptapi.json"; }

void requestGScriptHelp(const std::string& query, std::function<void(std::vector<std::string>)> callback) {
    if (!apiDefinitions.empty()) { callback(formatScriptHelp(apiDefinitions, query)); return; }
    const std::string source = completionSource;
    std::thread([query, source, callback = std::move(callback)] {
        auto* payload = new std::pair<std::function<void(std::vector<std::string>)>, std::vector<std::string>>{std::move(callback), formatScriptHelp(fetchCompletionDefinitions(source), query)};
        g_idle_add(+[](gpointer data) {
            auto* payload = static_cast<std::pair<std::function<void(std::vector<std::string>)>, std::vector<std::string>>*>(data);
            payload->first(std::move(payload->second));
            delete payload;
            return G_SOURCE_REMOVE;
        }, payload);
    }).detach();
}

void setGScriptEditorOptions(const RC::RCOptions& options) {
    const bool wasLspEnabled = lspEnabled;
    const bool sourceChanged = completionSource != options.autocompletesource;
    tabWidth = std::max(1, options.scripttabwidth);
    scriptFontSize = std::max(6, options.scriptfontsize);
    useTabs = options.scriptusetabs;
    showLineNumbers = options.showlinenumbers;
    showMinimap = options.minimap;
    syntaxHighlighting = options.syntaxhighlighting;
    autoIndenting = options.autoindenting;
    smartHomeEnd = options.smarthomeend;
    showBrackets = options.showbrackets;
    lspEnabled = options.lsp;
    for (GtkWidget* editor : completionEditors) applyEditorOptions(editor);
    if (wasLspEnabled != lspEnabled) for (GtkWidget* editor : completionEditors) setCompletionProvider(editor, lspEnabled);
    completionSource = options.autocompletesource.empty() ? "https://api.gscript.dev/" : options.autocompletesource;
    if (sourceChanged || (!wasLspEnabled && lspEnabled)) { apiDefinitions.clear(); if (lspEnabled) startCompletionLoad(); }
    else if (lspEnabled && apiDefinitions.empty()) startCompletionLoad();
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
    if (!map) return;
    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
    const bool documentFits = gtk_adjustment_get_upper(adjustment) <= gtk_adjustment_get_page_size(adjustment) + 0.5;
    GtkStyleContext* context = gtk_widget_get_style_context(map);
    if (documentFits) gtk_style_context_add_class(context, "remote-control-minimap-no-scrubber");
    else gtk_style_context_remove_class(context, "remote-control-minimap-no-scrubber");
}

static void onMinimapAdjustmentChanged(GtkAdjustment*, gpointer userData) { updateMinimapViewportIndicator(GTK_WIDGET(userData)); }
static void onMinimapSizeAllocated(GtkWidget* widget, GtkAllocation*, gpointer) { updateMinimapViewportIndicator(widget); }

GtkWidget* wrapGScriptEditor(GtkWidget* editor, GtkWidget* scrolled) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(row), scrolled, true, true, 0);
    GtkWidget* map = gtk_source_map_new();
    GtkWidget* strip = GTK_WIDGET(g_object_new(minimap_strip_get_type(), nullptr));
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
    g_object_set_data(G_OBJECT(editor), "remote-control-minimap", map);
    g_object_set_data(G_OBJECT(editor), "remote-control-minimap-strip", strip);
    g_object_set_data(G_OBJECT(scrolled), "remote-control-minimap", map);
    GtkCssProvider* minimapStyle = gtk_css_provider_new();
    gtk_css_provider_load_from_data(minimapStyle, "#remote-control-minimap .scrubber { min-height: 6px; max-height: 24px; } #remote-control-minimap.remote-control-minimap-no-scrubber .scrubber { background-image: none; background-color: transparent; border-color: transparent; box-shadow: none; opacity: 0; }", -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(map), GTK_STYLE_PROVIDER(minimapStyle), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(minimapStyle);
    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
    g_signal_connect(adjustment, "changed", G_CALLBACK(onMinimapAdjustmentChanged), scrolled);
    g_signal_connect(scrolled, "size-allocate", G_CALLBACK(onMinimapSizeAllocated), nullptr);
    gtk_container_add(GTK_CONTAINER(strip), map);
    gtk_box_pack_end(GTK_BOX(row), strip, false, false, 0);
    if (showMinimap) {
        gtk_widget_show(map);
        gtk_widget_show(strip);
    } else gtk_widget_hide(strip);
    return row;
}

void refreshGScriptEditorTheme() {
    for (GtkWidget* editor : completionEditors) {
        if (!GTK_SOURCE_IS_VIEW(editor)) continue;
        GtkSourceBuffer* buffer = GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor)));
        applyRemoteControlSourceStyle(buffer);
    }
}

void configureGScriptEditor(GtkWidget* editor) {
    if (!GTK_SOURCE_IS_VIEW(editor)) return;
    applyEditorOptions(editor);
    if (completionEditors.empty() && lspEnabled) startCompletionLoad();
    if (std::find(completionEditors.begin(), completionEditors.end(), editor) == completionEditors.end()) {
        completionEditors.push_back(editor);
        auto* provider = REMOTE_COMPLETION_PROVIDER(g_object_new(REMOTE_TYPE_COMPLETION_PROVIDER, nullptr));
        provider->editor = editor;
        GtkWidget* signaturePopover = gtk_popover_new(editor);
        gtk_popover_set_position(GTK_POPOVER(signaturePopover), GTK_POS_BOTTOM);
        gtk_popover_set_modal(GTK_POPOVER(signaturePopover), false);
        GtkWidget* signatureLabel = gtk_label_new(nullptr);
        gtk_label_set_line_wrap(GTK_LABEL(signatureLabel), true);
        gtk_label_set_xalign(GTK_LABEL(signatureLabel), 0.0F);
        gtk_widget_set_margin_start(signatureLabel, 8);
        gtk_widget_set_margin_end(signatureLabel, 8);
        gtk_widget_set_margin_top(signatureLabel, 6);
        gtk_widget_set_margin_bottom(signatureLabel, 6);
        gtk_container_add(GTK_CONTAINER(signaturePopover), signatureLabel);
        editorCompletionStates.push_back({editor, provider, signaturePopover, signatureLabel});
        g_signal_connect(editor, "destroy", G_CALLBACK(+[](GtkWidget* widget, gpointer) {
            const auto state = std::find_if(editorCompletionStates.begin(), editorCompletionStates.end(), [widget](const EditorCompletionState& value) { return value.editor == widget; });
            if (state != editorCompletionStates.end()) {
                GtkSourceCompletion* completion = gtk_source_view_get_completion(GTK_SOURCE_VIEW(widget));
                gtk_source_completion_remove_provider(completion, GTK_SOURCE_COMPLETION_PROVIDER(state->provider), nullptr);
                gtk_widget_destroy(state->signaturePopover);
                g_object_unref(state->provider);
                editorCompletionStates.erase(state);
            }
            completionEditors.erase(std::remove(completionEditors.begin(), completionEditors.end(), widget), completionEditors.end());
        }), nullptr);
    }
    GtkSourceCompletion* completion = gtk_source_view_get_completion(GTK_SOURCE_VIEW(editor));
    g_object_set(completion, "auto-complete-delay", 120, "show-headers", FALSE, nullptr);
    if (lspEnabled) setCompletionProvider(editor, true);
    gtk_widget_set_has_tooltip(editor, true);
    g_signal_connect(editor, "query-tooltip", G_CALLBACK(editorQueryTooltip), nullptr);
    GtkTextBuffer* completionBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
    g_signal_connect(completionBuffer, "mark-set", G_CALLBACK(+[](GtkTextBuffer* buffer, GtkTextIter*, GtkTextMark* mark, gpointer data) {
        if (mark == gtk_text_buffer_get_insert(buffer)) updateSignatureHint(GTK_WIDGET(data));
    }), editor);
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
        else if (g_strcmp0(label, "Find") == 0) find = button;
        else if (g_strcmp0(label, "Apply") == 0 || g_strcmp0(label, "Save") == 0) apply = button;
        else if (g_strcmp0(label, "Close") == 0 || g_strcmp0(label, "Cancel") == 0) close = button;
        else remaining = g_list_append(remaining, button);
    }
    g_list_free(children);
    gtk_widget_set_margin_start(status, 5);
    gtk_widget_set_margin_end(status, 5);
    gtk_widget_set_margin_top(actions, 3);
    gtk_widget_set_margin_bottom(actions, 3);
    gtk_widget_set_margin_end(actions, 5);
    gtk_widget_set_hexpand(spacer, true);
    if (goToLine != nullptr) gtk_box_pack_start(GTK_BOX(actions), goToLine, false, false, 0);
    if (find != nullptr) gtk_box_pack_start(GTK_BOX(actions), find, false, false, 0);
    if (apply != nullptr) gtk_box_pack_start(GTK_BOX(actions), apply, false, false, 0);
    if (close != nullptr) gtk_box_pack_start(GTK_BOX(actions), close, false, false, 0);
    for (GList* item = remaining; item != nullptr; item = item->next) gtk_box_pack_start(GTK_BOX(actions), GTK_WIDGET(item->data), false, false, 0);
    for (GList* item = remaining; item != nullptr; item = item->next) g_object_unref(item->data);
    g_list_free(remaining);
    if (goToLine != nullptr) g_object_unref(goToLine);
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
