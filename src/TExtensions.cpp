#include "TExtensions.h"

#include <gtk/gtk.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <atomic>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

namespace {
    std::string trim(std::string value) {
        const std::size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        const std::size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    std::string getValue(const std::map<std::string, std::string>& values, const char* key) {
        const auto found = values.find(key);
        return found == values.end() ? std::string() : found->second;
    }

    bool jsonBooleanValue(const std::string& json, const std::string& key, bool fallback) {
        const std::string field = "\"" + key + "\"";
        std::size_t position = json.find(field);
        if (position == std::string::npos) return fallback;
        position = json.find(':', position + field.size());
        if (position == std::string::npos) return fallback;
        const std::string value = trim(json.substr(position + 1));
        if (value.rfind("true", 0) == 0) return true;
        if (value.rfind("false", 0) == 0) return false;
        return fallback;
    }

    void appendUtf8(std::string& value, unsigned codepoint) {
        if (codepoint <= 0x7f) value += static_cast<char>(codepoint);
        else if (codepoint <= 0x7ff) { value += static_cast<char>(0xc0 | (codepoint >> 6)); value += static_cast<char>(0x80 | (codepoint & 0x3f)); }
        else if (codepoint <= 0xffff) { value += static_cast<char>(0xe0 | (codepoint >> 12)); value += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)); value += static_cast<char>(0x80 | (codepoint & 0x3f)); }
        else { value += static_cast<char>(0xf0 | (codepoint >> 18)); value += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)); value += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)); value += static_cast<char>(0x80 | (codepoint & 0x3f)); }
    }

    unsigned hexDigit(char value) {
        if (value >= '0' && value <= '9') return static_cast<unsigned>(value - '0');
        if (value >= 'a' && value <= 'f') return static_cast<unsigned>(value - 'a' + 10);
        if (value >= 'A' && value <= 'F') return static_cast<unsigned>(value - 'A' + 10);
        return 16;
    }

    std::string jsonEscape(const std::string& value) {
        std::string result;
        for (unsigned char character : value) {
            if (character == '"') result += "\\\"";
            else if (character == '\\') result += "\\\\";
            else if (character == '\n') result += "\\n";
            else if (character == '\r') result += "\\r";
            else if (character == '\t') result += "\\t";
            else result += static_cast<char>(character);
        }
        return result;
    }

    bool jsonStringValue(const std::string& json, const std::string& key, std::string& value) {
        const std::string field = "\"" + key + "\"";
        std::size_t position = json.find(field);
        if (position == std::string::npos) return false;
        position = json.find(':', position + field.size());
        if (position == std::string::npos) return false;
        position = json.find('"', position + 1);
        if (position == std::string::npos) return false;
        value.clear();
        for (++position; position < json.size(); ++position) {
            const char character = json[position];
            if (character == '"') return true;
            if (character != '\\') { value += character; continue; }
            if (++position >= json.size()) return false;
            const char escaped = json[position];
            if (escaped == 'n') value += '\n';
            else if (escaped == 'r') value += '\r';
            else if (escaped == 't') value += '\t';
            else if (escaped == 'u' && position + 4 < json.size()) { unsigned codepoint = 0; bool valid = true; for (std::size_t digit = 1; digit <= 4; ++digit) { const unsigned part = hexDigit(json[position + digit]); if (part > 15) { valid = false; break; } codepoint = (codepoint << 4) | part; } if (valid) { appendUtf8(value, codepoint); position += 4; } else value += escaped; }
            else value += escaped;
        }
        return false;
    }

    std::string extensionDisplayLine(const std::string& json) {
        std::string display;
        if (jsonStringValue(json, "display", display) && !display.empty()) return display;
        if (jsonStringValue(json, "text", display) && !display.empty()) return display;
        std::string message;
        if (jsonStringValue(json, "message", message) && !message.empty()) return message;
        std::string type;
        jsonStringValue(json, "type", type);
        const auto objectStart = json.find('{');
        if (objectStart == std::string::npos) return trim(json);
        if (type == "ready" || type == "status" || type == "response" || type == "error" || type == "log") return {};
        std::function<std::string(std::size_t&, int)> parseValue;
        std::function<std::string(std::size_t&, int)> parseObject;
        auto parseString = [&json](std::size_t& position) {
            std::string value;
            if (position >= json.size() || json[position] != '"') return value;
            for (++position; position < json.size(); ++position) {
                if (json[position] == '"') { ++position; break; }
                if (json[position] == '\\' && position + 1 < json.size()) { const char escaped = json[++position]; if (escaped == 'u' && position + 4 < json.size()) { unsigned codepoint = 0; bool valid = true; for (std::size_t digit = 1; digit <= 4; ++digit) { const unsigned part = hexDigit(json[position + digit]); if (part > 15) { valid = false; break; } codepoint = (codepoint << 4) | part; } if (valid) { appendUtf8(value, codepoint); position += 4; continue; } } value += escaped; continue; }
                value += json[position];
            }
            return value;
        };
        parseObject = [&json, &parseValue, &parseObject, &parseString](std::size_t& position, int depth) {
            std::string result;
            if (position >= json.size() || json[position] != '{') return result;
            ++position;
            while (position < json.size() && json[position] != '}') {
                while (position < json.size() && (json[position] == ' ' || json[position] == '\t' || json[position] == '\r' || json[position] == '\n' || json[position] == ',')) ++position;
                if (position >= json.size() || json[position] == '}') break;
                const std::string key = parseString(position);
                while (position < json.size() && (json[position] == ' ' || json[position] == '\t' || json[position] == ':')) ++position;
                const std::string value = parseValue(position, depth + 1);
                if (!key.empty() && !value.empty()) { if (!result.empty()) result += ", "; result += key + "=" + value; }
            }
            if (position < json.size() && json[position] == '}') ++position;
            return result;
        };
        parseValue = [&json, &parseObject, &parseString](std::size_t& position, int depth) {
            while (position < json.size() && (json[position] == ' ' || json[position] == '\t' || json[position] == '\r' || json[position] == '\n')) ++position;
            if (position >= json.size()) return std::string();
            if (json[position] == '"') return parseString(position);
            if (json[position] == '{' && depth <= 2) return "{" + parseObject(position, depth) + "}";
            const std::size_t start = position;
            int nesting = 0;
            while (position < json.size()) { const char character = json[position]; if (character == '[' || character == '{') ++nesting; else if (character == ']' || character == '}') { if (nesting == 0) break; --nesting; } else if (character == ',' && nesting == 0) break; ++position; }
            return trim(json.substr(start, position - start));
        };
        std::size_t position = objectStart;
        const std::string summary = parseObject(position, 0);
        return summary.empty() ? trim(json) : summary;
    }

    int jsonIntegerValue(const std::string& json, const std::string& key, int fallback) {
        const std::string field = "\"" + key + "\"";
        std::size_t position = json.find(field);
        if (position == std::string::npos) return fallback;
        position = json.find(':', position + field.size());
        if (position == std::string::npos) return fallback;
        std::stringstream stream(json.substr(position + 1));
        int value = fallback;
        stream >> value;
        return value;
    }

#ifdef _WIN32
    std::wstring utf8ToWide(const std::string& value) {
        if (value.empty()) return {};
        const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
        std::wstring result(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length);
        return result;
    }

    std::wstring quoteWindowsArgument(const std::string& value) {
        const std::wstring wide = utf8ToWide(value);
        if (wide.find_first_of(L" \t\"") == std::wstring::npos) return wide;
        std::wstring result = L"\"";
        std::size_t backslashes = 0;
        for (wchar_t character : wide) {
            if (character == L'\\') { ++backslashes; continue; }
            if (character == L'"') { result.append(backslashes * 2 + 1, L'\\'); result += character; backslashes = 0; continue; }
            result.append(backslashes, L'\\');
            backslashes = 0;
            result += character;
        }
        result.append(backslashes * 2, L'\\');
        result += L'"';
        return result;
    }
#endif
}

namespace RC {

bool loadExtensionManifest(const std::filesystem::path& path, ExtensionManifest& manifest) {
    std::ifstream stream(path);
    if (!stream) { manifest.error = "Manifest could not be opened"; return false; }
    std::error_code canonicalError;
    manifest.directory = std::filesystem::weakly_canonical(path.parent_path(), canonicalError);
    if (canonicalError) manifest.directory = std::filesystem::absolute(path.parent_path());
    if (path.extension() == ".json") {
        const std::string json((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        jsonStringValue(json, "id", manifest.id);
        jsonStringValue(json, "name", manifest.name);
        jsonStringValue(json, "version", manifest.version);
        jsonStringValue(json, "entry", manifest.entry);
        jsonStringValue(json, "runtime", manifest.runtime);
        std::string mode;
        jsonStringValue(json, "mode", mode);
        if (mode.empty()) jsonStringValue(json, "entryMode", mode);
        manifest.autoDiscover = jsonBooleanValue(json, "autoDiscover", false) || mode == "auto" || mode == "discover";
        manifest.api = jsonIntegerValue(json, "api", 1);
        if (manifest.id.empty() || manifest.name.empty() || manifest.version.empty() || manifest.runtime.empty() || (!manifest.autoDiscover && manifest.entry.empty())) { manifest.error = manifest.autoDiscover ? "Manifest requires id, name, version, and runtime" : "Manifest requires id, name, version, runtime, and entry"; return false; }
        return true;
    }
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) continue;
        values[trim(line.substr(0, separator))] = trim(line.substr(separator + 1));
    }
    manifest.id = getValue(values, "id");
    manifest.name = getValue(values, "name");
    manifest.version = getValue(values, "version");
    manifest.entry = getValue(values, "entry");
    manifest.runtime = getValue(values, "runtime");
    const std::string mode = getValue(values, "mode").empty() ? getValue(values, "entryMode") : getValue(values, "mode");
    const std::string autoDiscover = getValue(values, "autoDiscover");
    manifest.autoDiscover = mode == "auto" || mode == "discover" || autoDiscover == "1" || autoDiscover == "true";
    if (manifest.id.empty() || manifest.name.empty() || manifest.version.empty() || (!manifest.autoDiscover && manifest.entry.empty())) { manifest.error = manifest.autoDiscover ? "Manifest requires id, name, and version" : "Manifest requires id, name, version, and entry"; return false; }
    return true;
}

std::vector<ExtensionManifest> scanExtensionManifests(const std::filesystem::path& root) {
    std::vector<ExtensionManifest> result;
    if (!std::filesystem::is_directory(root)) return result;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (error || !entry.is_directory()) continue;
        for (const char* name : {"extension.json", "extension.ini", "manifest.ini"}) {
            ExtensionManifest manifest;
            const auto manifestPath = entry.path() / name;
            if (!std::filesystem::exists(manifestPath)) continue;
            if (!loadExtensionManifest(manifestPath, manifest)) { manifest.directory = entry.path(); if (manifest.id.empty()) manifest.id = entry.path().filename().string(); if (manifest.name.empty()) manifest.name = manifest.id; result.push_back(std::move(manifest)); }
            else result.push_back(std::move(manifest));
            break;
        }
    }
    std::sort(result.begin(), result.end(), [](const ExtensionManifest& left, const ExtensionManifest& right) { return left.id < right.id; });
    return result;
}

void appendExtensionLog(std::string& log, const std::string& line, std::size_t limit) {
    if (!line.empty()) { if (!log.empty()) log.push_back('\n'); log += line; }
    if (log.size() > limit) log.erase(0, log.size() - limit);
}

bool spawnExtensionProcess(const std::filesystem::path& directory, const std::vector<std::string>& arguments, ExtensionProcess& process, std::string& error) {
    if (arguments.empty()) { error = "Extension command is empty"; return false; }
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE inputRead = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    HANDLE errorRead = nullptr;
    HANDLE errorWrite = nullptr;
    if (!CreatePipe(&inputRead, &inputWrite, &security, 0) || !CreatePipe(&outputRead, &outputWrite, &security, 0) || !CreatePipe(&errorRead, &errorWrite, &security, 0)) {
        error = "Failed to create extension stdio pipes";
        if (inputRead != nullptr) CloseHandle(inputRead);
        if (inputWrite != nullptr) CloseHandle(inputWrite);
        if (outputRead != nullptr) CloseHandle(outputRead);
        if (outputWrite != nullptr) CloseHandle(outputWrite);
        if (errorRead != nullptr) CloseHandle(errorRead);
        if (errorWrite != nullptr) CloseHandle(errorWrite);
        return false;
    }
    SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errorRead, HANDLE_FLAG_INHERIT, 0);
    std::wstring command;
    for (const auto& argument : arguments) { if (!command.empty()) command += L' '; command += quoteWindowsArgument(argument); }
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = inputRead;
    startup.hStdOutput = outputWrite;
    startup.hStdError = errorWrite;
    PROCESS_INFORMATION processInformation{};
    const std::wstring workingDirectory = directory.wstring();
    const BOOL started = CreateProcessW(nullptr, commandBuffer.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, workingDirectory.c_str(), &startup, &processInformation);
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    CloseHandle(errorWrite);
    if (!started) {
        error = "Failed to launch extension process: " + std::to_string(GetLastError());
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        CloseHandle(errorRead);
        return false;
    }
    CloseHandle(processInformation.hThread);
    process.pid = reinterpret_cast<GPid>(processInformation.hProcess);
    process.input = _open_osfhandle(reinterpret_cast<intptr_t>(inputWrite), _O_WRONLY);
    process.output = _open_osfhandle(reinterpret_cast<intptr_t>(outputRead), _O_RDONLY);
    process.errorOutput = _open_osfhandle(reinterpret_cast<intptr_t>(errorRead), _O_RDONLY);
    if (process.input < 0 || process.output < 0 || process.errorOutput < 0) {
        error = "Failed to attach extension stdio pipes";
        if (process.input >= 0) _close(process.input); else CloseHandle(inputWrite);
        if (process.output >= 0) _close(process.output); else CloseHandle(outputRead);
        if (process.errorOutput >= 0) _close(process.errorOutput); else CloseHandle(errorRead);
        TerminateProcess(processInformation.hProcess, 0);
        CloseHandle(processInformation.hProcess);
        process = {};
        return false;
    }
    return true;
#else
    std::vector<std::string> mutableArguments = arguments;
    std::vector<gchar*> argv;
    for (auto& argument : mutableArguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    GError* spawnError = nullptr;
    const GSpawnFlags flags = static_cast<GSpawnFlags>(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH);
    if (!g_spawn_async_with_pipes(directory.string().c_str(), argv.data(), nullptr, flags, nullptr, nullptr, &process.pid, &process.input, &process.output, &process.errorOutput, &spawnError)) {
        error = spawnError == nullptr ? "Failed to launch extension process" : spawnError->message;
        if (spawnError != nullptr) g_error_free(spawnError);
        return false;
    }
    return true;
#endif
}

}

struct TExtensionsManager::ExtensionState {
    RC::ExtensionManifest manifest;
    bool enabled = false;
    GPid pid = 0;
    GIOChannel* input = nullptr;
    GIOChannel* output = nullptr;
    GIOChannel* errorOutput = nullptr;
#ifdef _WIN32
    struct ReaderState { gint fd = -1; std::atomic<bool> stop = false; std::thread worker; };
    std::shared_ptr<ReaderState> outputReader;
    std::shared_ptr<ReaderState> errorReader;
#endif
    guint outputWatch = 0;
    guint errorWatch = 0;
    guint childWatch = 0;
    bool ready = false;
    bool startSent = false;
    bool launching = false;
    bool outputToTab = false;
    std::string log;
};

struct TExtensionsManager::AsyncState {
    std::mutex mutex;
    TExtensionsManager* owner = nullptr;
};

struct TExtensionsManager::LaunchResult {
    std::shared_ptr<AsyncState> state;
    std::size_t index = 0;
    RC::ExtensionProcess process;
    std::string error;
};

#ifdef _WIN32
struct TExtensionsManager::ReaderLine {
    std::shared_ptr<AsyncState> state;
    std::size_t index = 0;
    bool protocol = false;
    std::string line;
};
#endif


TExtensionsManager::TExtensionsManager(GtkWindow* nextParent, const std::filesystem::path& nextApplicationDirectory, std::function<void(const std::string&, const std::string&)> nextOutputCallback, std::function<void(const std::string&)> nextCloseCallback) : parent(nextParent), applicationDirectory(nextApplicationDirectory), outputCallback(std::move(nextOutputCallback)), closeCallback(std::move(nextCloseCallback)), asyncState(std::make_shared<AsyncState>()) { asyncState->owner = this; scan(); }

TExtensionsManager::~TExtensionsManager() {
    { std::lock_guard<std::mutex> lock(asyncState->mutex); asyncState->owner = nullptr; }
    for (std::size_t index = 0; index < extensions.size(); ++index) stop(index);
    if (window != nullptr) gtk_widget_destroy(window);
}

std::filesystem::path TExtensionsManager::statePath() const { return std::filesystem::path(g_get_user_config_dir()) / "GScriptRC" / "extensions.state"; }

void TExtensionsManager::scan() {
    extensions.clear();
    auto manifests = RC::scanExtensionManifests(applicationDirectory / "extensions");
#ifndef _WIN32
    const auto userManifests = RC::scanExtensionManifests(std::filesystem::path(g_get_user_data_dir()) / "GScriptRC" / "extensions");
    for (const auto& manifest : userManifests) {
        const auto existing = std::find_if(manifests.begin(), manifests.end(), [&](const auto& value) { return value.id == manifest.id; });
        if (existing == manifests.end()) manifests.push_back(manifest); else *existing = manifest;
    }
#endif
    std::map<std::string, std::pair<bool, bool>> stateValues;
    std::ifstream state(statePath());
    std::string id;
    int value = 0;
    int output = 0;
    while (state >> id >> value) { state >> output; stateValues[id] = {value != 0, output != 0}; }
    for (const auto& manifest : manifests) { ExtensionState current; current.manifest = manifest; const auto found = stateValues.find(manifest.id); if (found != stateValues.end()) { current.enabled = found->second.first; current.outputToTab = found->second.second; } extensions.push_back(std::move(current)); }
    for (std::size_t index = 0; index < extensions.size(); ++index) if (extensions[index].enabled && extensions[index].manifest.error.empty()) launch(index);
    for (std::size_t index = 0; index < extensions.size(); ++index) if (extensions[index].outputToTab && extensions[index].manifest.error.empty() && outputCallback) outputCallback(extensions[index].manifest.name, {});
}

void TExtensionsManager::refresh() {
    if (list == nullptr) return;
    GList* children = gtk_container_get_children(GTK_CONTAINER(list));
    for (GList* child = children; child != nullptr; child = child->next) gtk_widget_destroy(GTK_WIDGET(child->data));
    g_list_free(children);
    for (std::size_t index = 0; index < extensions.size(); ++index) {
        auto& extension = extensions[index];
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        const std::string labelText = extension.manifest.name + " " + extension.manifest.version + (extension.pid != 0 ? " (running)" : "") + (extension.manifest.error.empty() ? "" : " — " + extension.manifest.error);
        GtkWidget* label = gtk_label_new(labelText.c_str());
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_widget_set_hexpand(label, true);
        GtkWidget* enabled = gtk_check_button_new_with_label("Enabled");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled), extension.enabled);
        g_object_set_data(G_OBJECT(enabled), "extension-index", GSIZE_TO_POINTER(index));
        g_signal_connect(enabled, "toggled", G_CALLBACK(onEnable), this);
        GtkWidget* open = gtk_button_new_with_label("Open");
        GtkWidget* log = gtk_button_new_with_label(extension.log.empty() ? "View Log" : "View Log •");
        GtkWidget* outputTab = gtk_button_new_with_label("Output Tab");
        GtkStyleContext* outputContext = gtk_widget_get_style_context(outputTab);
        if (extension.outputToTab) gtk_style_context_add_class(outputContext, "suggested-action");
        g_object_set_data(G_OBJECT(open), "extension-index", GSIZE_TO_POINTER(index));
        g_object_set_data(G_OBJECT(log), "extension-index", GSIZE_TO_POINTER(index));
        g_object_set_data(G_OBJECT(outputTab), "extension-index", GSIZE_TO_POINTER(index));
        g_signal_connect(open, "clicked", G_CALLBACK(onOpen), this);
        g_signal_connect(log, "clicked", G_CALLBACK(onLog), this);
        g_signal_connect(outputTab, "clicked", G_CALLBACK(onOutputTab), this);
        gtk_box_pack_start(GTK_BOX(row), enabled, false, false, 0);
        gtk_box_pack_start(GTK_BOX(row), label, true, true, 0);
        gtk_box_pack_start(GTK_BOX(row), open, false, false, 0);
        gtk_box_pack_start(GTK_BOX(row), log, false, false, 0);
        gtk_box_pack_start(GTK_BOX(row), outputTab, false, false, 0);
        gtk_widget_set_sensitive(open, extension.manifest.error.empty());
        gtk_widget_set_sensitive(enabled, extension.manifest.error.empty());
        gtk_widget_add_events(row, GDK_BUTTON_PRESS_MASK);
        g_object_set_data(G_OBJECT(row), "extension-index", GSIZE_TO_POINTER(index));
        g_signal_connect(row, "button-press-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventButton* event, gpointer data) -> gboolean { if (event->button != 3) return false; auto* manager = static_cast<TExtensionsManager*>(data); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(widget), "extension-index")); GtkWidget* menu = gtk_menu_new(); GtkWidget* toggle = gtk_menu_item_new_with_label(index < manager->extensions.size() && manager->extensions[index].enabled ? "Disable" : "Enable"); g_object_set_data(G_OBJECT(toggle), "extension-index", GSIZE_TO_POINTER(index)); g_signal_connect(toggle, "activate", G_CALLBACK(+[](GtkMenuItem* item, gpointer value) { auto* manager = static_cast<TExtensionsManager*>(value); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(item), "extension-index")); if (index < manager->extensions.size()) manager->setEnabled(index, !manager->extensions[index].enabled); }), manager); gtk_menu_shell_append(GTK_MENU_SHELL(menu), toggle); GtkWidget* send = gtk_menu_item_new_with_label(index < manager->extensions.size() && manager->extensions[index].outputToTab ? "Stop sending output to tab" : "Send output to tab"); g_object_set_data(G_OBJECT(send), "extension-index", GSIZE_TO_POINTER(index)); g_signal_connect(send, "activate", G_CALLBACK(+[](GtkMenuItem* item, gpointer value) { auto* manager = static_cast<TExtensionsManager*>(value); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(item), "extension-index")); if (index < manager->extensions.size()) manager->setOutputTab(index, !manager->extensions[index].outputToTab); }), manager); gtk_menu_shell_append(GTK_MENU_SHELL(menu), send); gtk_widget_show_all(menu); gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event)); return true; }), this);
        gtk_box_pack_start(GTK_BOX(list), row, false, false, 4);
        gtk_widget_show_all(row);
    }
}

void TExtensionsManager::showWindow() {
    if (window == nullptr) {
        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window), "Extensions");
        gtk_window_set_default_size(GTK_WINDOW(window), 520, 320);
        gtk_window_set_transient_for(GTK_WINDOW(window), parent);
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ON_PARENT);
        g_signal_connect(window, "delete-event", G_CALLBACK(+[](GtkWidget* widget, GdkEvent*, gpointer) -> gboolean { gtk_widget_hide(widget); return true; }), nullptr);
        GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_container_set_border_width(GTK_CONTAINER(root), 8);
        gtk_container_add(GTK_CONTAINER(window), root);
        GtkWidget* frame = gtk_frame_new("Installed extensions");
        gtk_box_pack_start(GTK_BOX(root), frame, true, true, 0);
        GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(list), 6);
        gtk_container_add(GTK_CONTAINER(scroll), list);
        gtk_container_add(GTK_CONTAINER(frame), scroll);
        GtkWidget* close = gtk_button_new_with_label("Close");
        gtk_widget_set_halign(close, GTK_ALIGN_END);
        g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_hide), window);
        gtk_box_pack_start(GTK_BOX(root), close, false, false, 0);
    }
    refresh();
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

void TExtensionsManager::onEnable(GtkToggleButton* button, gpointer data) { auto* manager = static_cast<TExtensionsManager*>(data); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(button), "extension-index")); manager->setEnabled(index, gtk_toggle_button_get_active(button)); }
void TExtensionsManager::onOpen(GtkButton* button, gpointer data) { auto* manager = static_cast<TExtensionsManager*>(data); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(button), "extension-index")); if (index >= manager->extensions.size()) return; const auto& manifest = manager->extensions[index].manifest; const auto path = manifest.autoDiscover || manifest.entry.empty() ? manifest.directory : manifest.directory / manifest.entry; gchar* uri = g_filename_to_uri(path.string().c_str(), nullptr, nullptr); if (uri != nullptr) { gtk_show_uri_on_window(manager->window == nullptr ? nullptr : GTK_WINDOW(manager->window), uri, GDK_CURRENT_TIME, nullptr); g_free(uri); } }
void TExtensionsManager::onLog(GtkButton* button, gpointer data) { auto* manager = static_cast<TExtensionsManager*>(data); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(button), "extension-index")); manager->showLog(index); }
void TExtensionsManager::onOutputTab(GtkButton* button, gpointer data) { auto* manager = static_cast<TExtensionsManager*>(data); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(button), "extension-index")); if (index < manager->extensions.size()) manager->setOutputTab(index, !manager->extensions[index].outputToTab); }

void TExtensionsManager::setEnabled(std::size_t index, bool enabled) {
    if (index >= extensions.size()) return;
    extensions[index].enabled = enabled;
    saveState();
    if (enabled) launch(index); else stop(index);
    refresh();
}

void TExtensionsManager::saveState() {
    std::filesystem::create_directories(statePath().parent_path());
    std::ofstream state(statePath(), std::ios::trunc);
    for (const auto& extension : extensions) state << extension.manifest.id << ' ' << (extension.enabled ? 1 : 0) << ' ' << (extension.outputToTab ? 1 : 0) << '\n';
}

void TExtensionsManager::routeLog(std::size_t index) {
    if (index >= extensions.size() || !extensions[index].outputToTab || !outputCallback) return;
    const auto& extension = extensions[index];
    if (extension.log.empty()) { outputCallback(extension.manifest.name, {}); return; }
    std::size_t start = 0;
    while (start <= extension.log.size()) { const std::size_t end = extension.log.find('\n', start); outputCallback(extension.manifest.name, extension.log.substr(start, end == std::string::npos ? std::string::npos : end - start)); if (end == std::string::npos) break; start = end + 1; }
}

void TExtensionsManager::setOutputTab(std::size_t index, bool enabled) {
    if (index >= extensions.size()) return;
    extensions[index].outputToTab = enabled;
    saveState();
    if (enabled) routeLog(index); else if (closeCallback) closeCallback(extensions[index].manifest.name);
    refresh();
}

void TExtensionsManager::launch(std::size_t index) {
    if (index >= extensions.size() || extensions[index].pid != 0 || extensions[index].launching) return;
    const auto& manifest = extensions[index].manifest;
    if (manifest.runtime == "gs2engine-stdio" && manifest.api != 1) { RC::appendExtensionLog(extensions[index].log, "Unsupported GS2 extension API: " + std::to_string(manifest.api)); return; }
    const auto entry = manifest.entry.empty() ? std::filesystem::path() : manifest.directory / manifest.entry;
    if (!manifest.autoDiscover && !std::filesystem::exists(entry)) { RC::appendExtensionLog(extensions[index].log, "Entry not found: " + entry.string()); return; }
    std::vector<std::string> arguments;
    if (manifest.runtime == "gs2engine-stdio") {
#ifdef _WIN32
        const auto host = manifest.directory / "GS2Engine.exe";
#else
        const auto host = manifest.directory / "GS2Engine";
#endif
        if (!std::filesystem::exists(host)) { RC::appendExtensionLog(extensions[index].log, "GS2Engine host not found: " + host.string()); return; }
        arguments.push_back(host.string());
        arguments.push_back("--extension-stdio");
    } else {
        arguments.push_back(manifest.runtime.empty() ? entry.string() : manifest.runtime);
        if (!manifest.runtime.empty() && !manifest.autoDiscover) arguments.push_back(entry.string());
    }
    extensions[index].launching = true;
    const auto state = asyncState;
    const auto directory = manifest.directory;
    std::thread([state, index, directory, arguments = std::move(arguments)]() mutable {
        auto* result = new LaunchResult;
        result->state = state;
        result->index = index;
        RC::spawnExtensionProcess(directory, arguments, result->process, result->error);
        g_main_context_invoke(nullptr, onLaunchComplete, result);
    }).detach();
}

gboolean TExtensionsManager::onLaunchComplete(gpointer data) {
    std::unique_ptr<LaunchResult> result(static_cast<LaunchResult*>(data));
    TExtensionsManager* manager = nullptr;
    { std::lock_guard<std::mutex> lock(result->state->mutex); manager = result->state->owner; }
    if (manager != nullptr) manager->finishLaunch(result->index, result->process, result->error);
    else if (result->process.pid != 0) {
#ifdef _WIN32
        TerminateProcess(reinterpret_cast<HANDLE>(result->process.pid), 0);
        CloseHandle(reinterpret_cast<HANDLE>(result->process.pid));
        if (result->process.input >= 0) _close(result->process.input);
        if (result->process.output >= 0) _close(result->process.output);
        if (result->process.errorOutput >= 0) _close(result->process.errorOutput);
#else
        kill(result->process.pid, SIGTERM);
        if (result->process.input >= 0) close(result->process.input);
        if (result->process.output >= 0) close(result->process.output);
        if (result->process.errorOutput >= 0) close(result->process.errorOutput);
        g_spawn_close_pid(result->process.pid);
#endif
    }
    return G_SOURCE_REMOVE;
}

void TExtensionsManager::finishLaunch(std::size_t index, RC::ExtensionProcess process, const std::string& error) {
    if (index >= extensions.size()) return;
    extensions[index].launching = false;
    if (!error.empty()) { RC::appendExtensionLog(extensions[index].log, error); refresh(); return; }
    if (!extensions[index].enabled || extensions[index].pid != 0) {
#ifdef _WIN32
        TerminateProcess(reinterpret_cast<HANDLE>(process.pid), 0);
        CloseHandle(reinterpret_cast<HANDLE>(process.pid));
        if (process.input >= 0) _close(process.input);
        if (process.output >= 0) _close(process.output);
        if (process.errorOutput >= 0) _close(process.errorOutput);
#else
        kill(process.pid, SIGTERM);
        if (process.input >= 0) close(process.input);
        if (process.output >= 0) close(process.output);
        if (process.errorOutput >= 0) close(process.errorOutput);
        g_spawn_close_pid(process.pid);
#endif
        return;
    }
    extensions[index].pid = process.pid;
    extensions[index].ready = false;
    extensions[index].startSent = false;
    extensions[index].input = g_io_channel_unix_new(process.input);
    g_io_channel_set_encoding(extensions[index].input, "UTF-8", nullptr);
    if (extensions[index].manifest.runtime != "gs2engine-stdio") sendRequest(index, "{\"type\":\"start\",\"protocol\":\"json-lines\"}");
#ifdef _WIN32
    const auto startReader = [this, index](gint fd, bool protocol) {
        auto reader = std::make_shared<ExtensionState::ReaderState>();
        reader->fd = fd;
        const auto state = asyncState;
        reader->worker = std::thread([reader, state, index, protocol]() {
            std::string pending;
            char buffer[4096];
            while (!reader->stop.load()) {
                const int count = _read(reader->fd, buffer, sizeof(buffer));
                if (count <= 0) break;
                pending.append(buffer, count);
                std::size_t newline = 0;
                while ((newline = pending.find('\n')) != std::string::npos) {
                    g_main_context_invoke(nullptr, onReaderLine, new ReaderLine{state, index, protocol, pending.substr(0, newline)});
                    pending.erase(0, newline + 1);
                }
            }
            if (!pending.empty() && !reader->stop.load()) g_main_context_invoke(nullptr, onReaderLine, new ReaderLine{state, index, protocol, pending});
        });
        return reader;
    };
    extensions[index].outputReader = startReader(process.output, true);
    extensions[index].errorReader = startReader(process.errorOutput, false);
#else
    extensions[index].output = g_io_channel_unix_new(process.output);
    extensions[index].errorOutput = g_io_channel_unix_new(process.errorOutput);
    g_io_channel_set_encoding(extensions[index].output, "UTF-8", nullptr);
    g_io_channel_set_flags(extensions[index].output, static_cast<GIOFlags>(g_io_channel_get_flags(extensions[index].output) | G_IO_FLAG_NONBLOCK), nullptr);
    extensions[index].outputWatch = g_io_add_watch(extensions[index].output, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), onOutput, this);
    g_io_channel_set_encoding(extensions[index].errorOutput, "UTF-8", nullptr);
    g_io_channel_set_flags(extensions[index].errorOutput, static_cast<GIOFlags>(g_io_channel_get_flags(extensions[index].errorOutput) | G_IO_FLAG_NONBLOCK), nullptr);
    extensions[index].errorWatch = g_io_add_watch(extensions[index].errorOutput, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), onOutput, this);
#endif
    extensions[index].childWatch = g_child_watch_add(process.pid, onChildExit, this);
    refresh();
}

void TExtensionsManager::sendRequest(std::size_t index, const std::string& request) {
    if (index >= extensions.size() || extensions[index].input == nullptr) return;
    const std::string line = request + "\n";
    gsize written = 0;
    g_io_channel_write_chars(extensions[index].input, line.c_str(), static_cast<gssize>(line.size()), &written, nullptr);
    g_io_channel_flush(extensions[index].input, nullptr);
}

void TExtensionsManager::handleProtocolLine(std::size_t index, const std::string& line) {
    if (index >= extensions.size() || extensions[index].manifest.runtime != "gs2engine-stdio") return;
    std::string type;
    if (!jsonStringValue(line, "type", type) || type != "ready" || extensions[index].startSent) return;
    extensions[index].ready = true;
    extensions[index].startSent = true;
    std::string request = "{\"id\":\"1\",\"method\":\"start\",\"params\":{\"extensionId\":\"" + jsonEscape(extensions[index].manifest.id) + "\"";
    if (!extensions[index].manifest.autoDiscover && !extensions[index].manifest.entry.empty()) {
        const auto entry = std::filesystem::absolute(extensions[index].manifest.directory / extensions[index].manifest.entry);
        request += ",\"entry\":\"" + jsonEscape(entry.string()) + "\"";
    } else if (extensions[index].manifest.autoDiscover) {
        const auto packageRoot = std::filesystem::absolute(extensions[index].manifest.directory);
        request += ",\"packageRoot\":\"" + jsonEscape(packageRoot.string()) + "\"";
    }
    request += "}}";
    sendRequest(index, request);
}

void TExtensionsManager::stop(std::size_t index) {
    if (index >= extensions.size()) return;
    auto& extension = extensions[index];
    if (extension.outputWatch != 0) { g_source_remove(extension.outputWatch); extension.outputWatch = 0; }
    if (extension.errorWatch != 0) { g_source_remove(extension.errorWatch); extension.errorWatch = 0; }
    if (extension.childWatch != 0) { g_source_remove(extension.childWatch); extension.childWatch = 0; }
    if (extension.input != nullptr) {
        if (extension.manifest.runtime == "gs2engine-stdio") {
            sendRequest(index, "{\"id\":\"2\",\"method\":\"stop\"}");
            sendRequest(index, "{\"id\":\"3\",\"method\":\"shutdown\"}");
        } else sendRequest(index, "{\"type\":\"stop\"}");
        g_io_channel_shutdown(extension.input, true, nullptr);
        g_io_channel_unref(extension.input);
        extension.input = nullptr;
    }
    if (extension.output != nullptr) { g_io_channel_shutdown(extension.output, true, nullptr); g_io_channel_unref(extension.output); extension.output = nullptr; }
    if (extension.errorOutput != nullptr) { g_io_channel_shutdown(extension.errorOutput, true, nullptr); g_io_channel_unref(extension.errorOutput); extension.errorOutput = nullptr; }
#ifdef _WIN32
    const auto stopReader = [](auto& reader) { if (reader == nullptr) return; reader->stop.store(true); if (reader->fd >= 0) { _close(reader->fd); reader->fd = -1; } if (reader->worker.joinable()) reader->worker.join(); reader.reset(); };
    stopReader(extension.outputReader);
    stopReader(extension.errorReader);
#endif
    if (extension.pid != 0) {
#ifdef _WIN32
        HANDLE process = reinterpret_cast<HANDLE>(extension.pid);
        HANDLE reaper = nullptr;
        if (process != nullptr && DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &reaper, 0, FALSE, DUPLICATE_SAME_ACCESS)) std::thread([reaper]() { if (WaitForSingleObject(reaper, 750) == WAIT_TIMEOUT) TerminateProcess(reaper, 0); CloseHandle(reaper); }).detach();
#else
        kill(extension.pid, SIGTERM);
#endif
        g_spawn_close_pid(extension.pid); extension.pid = 0;
    }
}

#ifdef _WIN32
gboolean TExtensionsManager::onReaderLine(gpointer data) {
    std::unique_ptr<ReaderLine> result(static_cast<ReaderLine*>(data));
    TExtensionsManager* manager = nullptr;
    { std::lock_guard<std::mutex> lock(result->state->mutex); manager = result->state->owner; }
    if (manager == nullptr || result->index >= manager->extensions.size()) return G_SOURCE_REMOVE;
    auto& extension = manager->extensions[result->index];
    const std::string raw = trim(result->line);
    const std::string display = result->protocol ? extensionDisplayLine(raw) : raw;
    if (result->protocol) manager->handleProtocolLine(result->index, raw);
    if (!display.empty()) { RC::appendExtensionLog(extension.log, display); if (extension.outputToTab && manager->outputCallback) manager->outputCallback(extension.manifest.name, display); }
    return G_SOURCE_REMOVE;
}
#endif

gboolean TExtensionsManager::onOutput(GIOChannel* channel, GIOCondition condition, gpointer data) {
    auto* manager = static_cast<TExtensionsManager*>(data);
    for (std::size_t index = 0; index < manager->extensions.size(); ++index) {
        auto& extension = manager->extensions[index];
        if (extension.output != channel && extension.errorOutput != channel) continue;
        gchar* line = nullptr;
        gsize length = 0;
        for (unsigned count = 0; count < 32 && g_io_channel_read_line(channel, &line, &length, nullptr, nullptr) == G_IO_STATUS_NORMAL; ++count) { const std::string raw = trim(line); const bool protocol = extension.output == channel; const std::string outputLine = protocol ? extensionDisplayLine(raw) : raw; if (protocol) manager->handleProtocolLine(index, raw); if (!outputLine.empty()) { RC::appendExtensionLog(extension.log, outputLine); if (extension.outputToTab && manager->outputCallback) manager->outputCallback(extension.manifest.name, outputLine); } g_free(line); line = nullptr; }
        if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) return G_SOURCE_REMOVE;
        return G_SOURCE_CONTINUE;
    }
    return G_SOURCE_REMOVE;
}

void TExtensionsManager::onChildExit(GPid pid, gint, gpointer data) { auto* manager = static_cast<TExtensionsManager*>(data); for (auto& extension : manager->extensions) if (extension.pid == pid) { extension.pid = 0; extension.childWatch = 0; if (extension.outputWatch != 0) { g_source_remove(extension.outputWatch); extension.outputWatch = 0; } if (extension.errorWatch != 0) { g_source_remove(extension.errorWatch); extension.errorWatch = 0; } if (extension.input != nullptr) { g_io_channel_unref(extension.input); extension.input = nullptr; } if (extension.output != nullptr) { g_io_channel_unref(extension.output); extension.output = nullptr; } if (extension.errorOutput != nullptr) { g_io_channel_unref(extension.errorOutput); extension.errorOutput = nullptr; }
#ifdef _WIN32
        const auto stopReader = [](auto& reader) { if (reader == nullptr) return; reader->stop.store(true); if (reader->fd >= 0) { _close(reader->fd); reader->fd = -1; } if (reader->worker.joinable()) reader->worker.join(); reader.reset(); };
        stopReader(extension.outputReader);
        stopReader(extension.errorReader);
#endif
        manager->refresh(); break; } g_spawn_close_pid(pid); }

void TExtensionsManager::showLog(std::size_t index) {
    if (index >= extensions.size()) return;
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Extension Log", window == nullptr ? nullptr : GTK_WINDOW(window), GTK_DIALOG_MODAL, "Close", GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 320);
    GtkWidget* view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), false);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), true);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), view);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), extensions[index].log.c_str(), -1);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}
