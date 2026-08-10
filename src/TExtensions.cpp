#include "TExtensions.h"

#include <gtk/gtk.h>
#include <algorithm>
#include <cctype>
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

    std::vector<std::string> splitList(const std::string& value) {
        std::vector<std::string> result;
        std::size_t start = 0;
        while (start <= value.size()) { const std::size_t end = value.find(',', start); const std::string item = trim(value.substr(start, end == std::string::npos ? std::string::npos : end - start)); if (!item.empty()) result.push_back(item); if (end == std::string::npos) break; start = end + 1; }
        return result;
    }

    std::vector<std::string> splitStateList(const std::string& value) {
        std::vector<std::string> result;
        std::string item;
        for (const char character : value) {
            if (character == ',' || std::isspace(static_cast<unsigned char>(character))) { if (!item.empty()) { result.push_back(item); item.clear(); } }
            else item += character;
        }
        if (!item.empty()) result.push_back(item);
        return result;
    }

    std::vector<std::string> jsonStringArray(const std::string& json, const std::string& key) {
        std::vector<std::string> result;
        const std::string field = "\"" + key + "\"";
        std::size_t position = json.find(field);
        if (position == std::string::npos) return result;
        position = json.find('[', position + field.size());
        if (position == std::string::npos) return result;
        const std::size_t end = json.find(']', position + 1);
        if (end == std::string::npos) return result;
        for (++position; position < end;) {
            while (position < end && (json[position] == ' ' || json[position] == '\t' || json[position] == '\r' || json[position] == '\n' || json[position] == ',')) ++position;
            if (position >= end) break;
            if (json[position] != '"') { while (position < end && json[position] != ',') ++position; continue; }
            std::size_t cursor = position;
            std::string value;
            ++cursor;
            for (; cursor < end; ++cursor) { if (json[cursor] == '"') break; if (json[cursor] == '\\' && cursor + 1 < end) { value += json[++cursor]; } else value += json[cursor]; }
            if (!value.empty()) result.push_back(value);
            position = cursor < end ? cursor + 1 : end;
        }
        return result;
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

    std::vector<std::string> jsonObjectArray(const std::string& json, const std::string& key) {
        std::vector<std::string> result;
        const std::string field = "\"" + key + "\"";
        std::size_t position = json.find(field);
        if (position == std::string::npos) return result;
        position = json.find('[', position + field.size());
        if (position == std::string::npos) return result;
        const std::size_t arrayEnd = json.find(']', position + 1);
        if (arrayEnd == std::string::npos) return result;
        ++position;
        while (position < arrayEnd) {
            while (position < arrayEnd && (std::isspace(static_cast<unsigned char>(json[position])) || json[position] == ',')) ++position;
            if (position >= arrayEnd) break;
            if (json[position] != '{') { while (position < arrayEnd && json[position] != ',') ++position; continue; }
            const std::size_t start = position;
            int depth = 0;
            bool quoted = false;
            bool escaped = false;
            for (; position < arrayEnd; ++position) {
                const char character = json[position];
                if (quoted) { if (escaped) escaped = false; else if (character == '\\') escaped = true; else if (character == '"') quoted = false; continue; }
                if (character == '"') quoted = true;
                else if (character == '{') ++depth;
                else if (character == '}' && --depth == 0) { ++position; break; }
            }
            if (depth == 0) result.push_back(json.substr(start, position - start));
        }
        return result;
    }

    std::vector<RC::ExtensionWindowAction> jsonWindowActions(const std::string& json) {
        std::vector<RC::ExtensionWindowAction> result;
        for (const auto& object : jsonObjectArray(json, "windowActions")) {
            RC::ExtensionWindowAction action;
            jsonStringValue(object, "kind", action.kind);
            jsonStringValue(object, "id", action.id);
            jsonStringValue(object, "label", action.label);
            jsonStringValue(object, "placement", action.placement);
            jsonStringValue(object, "icon", action.icon);
            if (action.kind.empty()) action.kind = "script-editor";
            if (action.label.empty()) action.label = action.id;
            if (action.placement != "bottom") action.placement = "top";
            if (!action.id.empty() && !action.label.empty()) result.push_back(std::move(action));
        }
        return result;
    }

    std::vector<RC::ExtensionWindowAction> iniWindowActions(const std::string& value) {
        std::vector<RC::ExtensionWindowAction> result;
        for (const auto& token : splitList(value)) {
            const std::size_t first = token.find(':');
            const std::size_t second = first == std::string::npos ? std::string::npos : token.find(':', first + 1);
            RC::ExtensionWindowAction action;
            action.kind = first == std::string::npos ? "script-editor" : trim(token.substr(0, first));
            action.id = first == std::string::npos ? trim(token) : trim(token.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1));
            action.label = second == std::string::npos ? action.id : trim(token.substr(second + 1));
            if (!action.id.empty()) result.push_back(std::move(action));
        }
        return result;
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
        if (type == "ready" || type == "status" || type == "response") return {};
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
        jsonStringValue(json, "publisher", manifest.publisher);
        jsonStringValue(json, "entry", manifest.entry);
        jsonStringValue(json, "runtime", manifest.runtime);
        manifest.requestedCapabilities = jsonStringArray(json, "capabilities");
        if (manifest.requestedCapabilities.empty()) manifest.requestedCapabilities = jsonStringArray(json, "requestedCapabilities");
        manifest.uiContributions = jsonStringArray(json, "ui");
        if (manifest.uiContributions.empty()) manifest.uiContributions = jsonStringArray(json, "uiContributions");
        manifest.themes = jsonStringArray(json, "themes");
        manifest.commands = jsonStringArray(json, "commands");
        manifest.readOnlyViews = jsonStringArray(json, "readOnlyViews");
        manifest.windowActions = jsonWindowActions(json);
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
    manifest.publisher = getValue(values, "publisher");
    manifest.entry = getValue(values, "entry");
    manifest.runtime = getValue(values, "runtime");
    manifest.requestedCapabilities = splitList(getValue(values, "capabilities"));
    if (manifest.requestedCapabilities.empty()) manifest.requestedCapabilities = splitList(getValue(values, "requestedCapabilities"));
    manifest.uiContributions = splitList(getValue(values, "ui"));
    if (manifest.uiContributions.empty()) manifest.uiContributions = splitList(getValue(values, "uiContributions"));
    manifest.themes = splitList(getValue(values, "themes"));
    manifest.commands = splitList(getValue(values, "commands"));
    manifest.readOnlyViews = splitList(getValue(values, "readOnlyViews"));
    manifest.windowActions = iniWindowActions(getValue(values, "windowActions"));
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
    std::set<std::string> approvedCapabilities;
    std::string runtimeError;
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
    const auto userManifests = RC::scanExtensionManifests(std::filesystem::path(g_get_user_data_dir()) / "GScriptRC" / "extensions");
    for (const auto& manifest : userManifests) {
        const auto existing = std::find_if(manifests.begin(), manifests.end(), [&](const auto& value) { return value.id == manifest.id; });
        if (existing == manifests.end()) manifests.push_back(manifest); else *existing = manifest;
    }
    std::map<std::string, std::pair<bool, bool>> stateValues;
    std::map<std::string, std::string> capabilityValues;
    std::ifstream state(statePath());
    std::string id;
    int value = 0;
    int output = 0;
    while (state >> id >> value) { state >> output; std::string capabilities; std::getline(state, capabilities); stateValues[id] = {value != 0, output != 0}; capabilityValues[id] = trim(capabilities); }
    for (const auto& manifest : manifests) { ExtensionState current; current.manifest = manifest; const auto found = stateValues.find(manifest.id); if (found != stateValues.end()) { current.enabled = found->second.first; current.outputToTab = found->second.second; const auto grants = capabilityValues.find(manifest.id); if (grants != capabilityValues.end()) for (const auto& capability : splitStateList(grants->second)) current.approvedCapabilities.insert(capability); } extensions.push_back(std::move(current)); }
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
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        const std::string status = !extension.manifest.error.empty() || !extension.runtimeError.empty() ? " (failed)" : extension.launching ? " (starting)" : extension.pid != 0 ? " (running)" : extension.enabled ? " (stopped)" : " (disabled)";
        const std::string error = extension.manifest.error.empty() ? extension.runtimeError : extension.manifest.error;
        const std::string labelText = extension.manifest.name + " " + extension.manifest.version + status + (error.empty() ? "" : " - " + error);
        GtkWidget* label = gtk_label_new(labelText.c_str());
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(label), 32);
        gtk_widget_set_hexpand(label, true);
        GtkWidget* enabled = gtk_check_button_new_with_label("Enabled");
        gtk_widget_set_tooltip_text(enabled, "Enable or disable this extension");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled), extension.enabled);
        g_object_set_data(G_OBJECT(enabled), "extension-index", GSIZE_TO_POINTER(index));
        g_signal_connect(enabled, "toggled", G_CALLBACK(onEnable), this);
        auto makeAction = [](const char* icon, const char* tooltip) {
            GtkWidget* button = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_MENU);
            gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
            gtk_widget_set_size_request(button, 30, 28);
            gtk_widget_set_tooltip_text(button, tooltip);
            return button;
        };
        GtkWidget* open = makeAction("document-open-symbolic", "Open extension folder or entry");
        GtkWidget* log = makeAction("view-list-symbolic", extension.log.empty() ? "View extension log" : "View extension log (unread output)");
        GtkWidget* outputTab = makeAction("tab-new-symbolic", extension.outputToTab ? "Disable extension output tab" : "Enable extension output tab");
        GtkWidget* details = makeAction("dialog-information-symbolic", "View extension details");
        GtkWidget* remove = makeAction("user-trash-symbolic", "Remove extension");
        GtkStyleContext* outputContext = gtk_widget_get_style_context(outputTab);
        if (extension.outputToTab) gtk_style_context_add_class(outputContext, "extension-output-active");
        GtkStyleContext* logContext = gtk_widget_get_style_context(log);
        if (!extension.log.empty()) gtk_style_context_add_class(logContext, "suggested-action");
        g_object_set_data(G_OBJECT(open), "extension-index", GSIZE_TO_POINTER(index));
        g_object_set_data(G_OBJECT(log), "extension-index", GSIZE_TO_POINTER(index));
        g_object_set_data(G_OBJECT(outputTab), "extension-index", GSIZE_TO_POINTER(index));
        g_object_set_data(G_OBJECT(details), "extension-index", GSIZE_TO_POINTER(index));
        g_object_set_data(G_OBJECT(remove), "extension-index", GSIZE_TO_POINTER(index));
        g_signal_connect(open, "clicked", G_CALLBACK(onOpen), this);
        g_signal_connect(log, "clicked", G_CALLBACK(onLog), this);
        g_signal_connect(outputTab, "clicked", G_CALLBACK(onOutputTab), this);
        g_signal_connect(details, "clicked", G_CALLBACK(onDetails), this);
        g_signal_connect(remove, "clicked", G_CALLBACK(onRemove), this);
        gtk_box_pack_start(GTK_BOX(row), enabled, false, false, 0);
        gtk_box_pack_start(GTK_BOX(row), label, true, true, 0);
        gtk_box_pack_start(GTK_BOX(row), open, false, false, 0);
        gtk_box_pack_start(GTK_BOX(row), log, false, false, 0);
        gtk_box_pack_start(GTK_BOX(row), outputTab, false, false, 0);
        gtk_box_pack_start(GTK_BOX(row), details, false, false, 0);
        gtk_box_pack_start(GTK_BOX(row), remove, false, false, 0);
        gtk_widget_set_sensitive(open, extension.manifest.error.empty());
        gtk_widget_set_sensitive(enabled, extension.manifest.error.empty());
        gtk_widget_add_events(row, GDK_BUTTON_PRESS_MASK);
        g_object_set_data(G_OBJECT(row), "extension-index", GSIZE_TO_POINTER(index));
        g_signal_connect(row, "button-press-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventButton* event, gpointer data) -> gboolean { if (event->button != 3) return false; auto* manager = static_cast<TExtensionsManager*>(data); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(widget), "extension-index")); GtkWidget* menu = gtk_menu_new(); GtkWidget* toggle = gtk_menu_item_new_with_label(index < manager->extensions.size() && manager->extensions[index].enabled ? "Disable" : "Enable"); g_object_set_data(G_OBJECT(toggle), "extension-index", GSIZE_TO_POINTER(index)); g_signal_connect(toggle, "activate", G_CALLBACK(+[](GtkMenuItem* item, gpointer value) { auto* manager = static_cast<TExtensionsManager*>(value); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(item), "extension-index")); if (index < manager->extensions.size()) manager->setEnabled(index, !manager->extensions[index].enabled); }), manager); gtk_menu_shell_append(GTK_MENU_SHELL(menu), toggle); GtkWidget* send = gtk_menu_item_new_with_label(index < manager->extensions.size() && manager->extensions[index].outputToTab ? "Stop sending output to tab" : "Send output to tab"); g_object_set_data(G_OBJECT(send), "extension-index", GSIZE_TO_POINTER(index)); g_signal_connect(send, "activate", G_CALLBACK(+[](GtkMenuItem* item, gpointer value) { auto* manager = static_cast<TExtensionsManager*>(value); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(item), "extension-index")); if (index < manager->extensions.size()) manager->setOutputTab(index, !manager->extensions[index].outputToTab); }), manager); gtk_menu_shell_append(GTK_MENU_SHELL(menu), send); gtk_widget_show_all(menu); gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event)); return true; }), this);
        gtk_box_pack_start(GTK_BOX(list), row, false, false, 2);
        gtk_widget_show_all(row);
    }
}

void TExtensionsManager::showWindow() {
    if (window == nullptr) {
        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_widget_set_name(window, "ExtensionsWindow");
        gtk_window_set_title(GTK_WINDOW(window), "Extensions");
        gtk_window_set_default_size(GTK_WINDOW(window), 520, 180);
        gtk_window_set_transient_for(GTK_WINDOW(window), parent);
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ON_PARENT);
        g_signal_connect(window, "delete-event", G_CALLBACK(+[](GtkWidget* widget, GdkEvent*, gpointer) -> gboolean { gtk_widget_hide(widget); return true; }), nullptr);
        GtkCssProvider* extensionsCss = gtk_css_provider_new();
        gtk_css_provider_load_from_data(extensionsCss, "#ExtensionsWindow button.extension-output-active, #ExtensionsWindow button.extension-output-active:hover, #ExtensionsWindow button.extension-output-active:active, #ExtensionsWindow button.extension-output-active:focus { background-image: none; background-color: @theme_selected_bg_color; color: @theme_selected_fg_color; border-color: @theme_selected_bg_color; }", -1, nullptr);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(extensionsCss), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
        g_object_unref(extensionsCss);
        GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_container_set_border_width(GTK_CONTAINER(root), 8);
        gtk_container_add(GTK_CONTAINER(window), root);
        GtkWidget* frame = gtk_frame_new("Installed extensions");
        gtk_box_pack_start(GTK_BOX(root), frame, true, true, 0);
        GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_container_set_border_width(GTK_CONTAINER(list), 4);
        gtk_container_add(GTK_CONTAINER(scroll), list);
        gtk_container_add(GTK_CONTAINER(frame), scroll);
        GtkWidget* close = gtk_button_new_with_label("Close");
        gtk_widget_set_halign(close, GTK_ALIGN_END);
        g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_hide), window);
        gtk_box_pack_start(GTK_BOX(root), close, false, false, 0);
    }
    const bool compactInitialSize = gtk_widget_get_visible(window) == false;
    refresh();
    if (compactInitialSize) {
        const int height = std::min(300, std::max(150, 112 + static_cast<int>(extensions.size()) * 36));
        gtk_window_resize(GTK_WINDOW(window), 520, height);
    }
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

std::vector<RC::ExtensionWindowActionBinding> TExtensionsManager::windowActions(const std::string& kind) const {
    std::vector<RC::ExtensionWindowActionBinding> result;
    for (const auto& extension : extensions) {
        if (!extension.enabled || !extension.manifest.error.empty()) continue;
        for (const auto& action : extension.manifest.windowActions) if (action.kind == kind) result.push_back({extension.manifest.id, extension.manifest.name, extension.manifest.directory, action});
    }
    return result;
}

bool TExtensionsManager::invokeWindowAction(const RC::ExtensionWindowActionBinding& binding, const RC::ExtensionWindowContext& context, std::string& error) {
    const auto found = std::find_if(extensions.begin(), extensions.end(), [&](const ExtensionState& extension) { return extension.manifest.id == binding.extensionId; });
    if (found == extensions.end()) { error = "Extension is no longer installed"; return false; }
    if (!found->enabled || !found->manifest.error.empty()) { error = "Extension is disabled or has an invalid manifest"; return false; }
    if (found->pid == 0 || found->input == nullptr) { error = "Extension runtime is not running"; return false; }
    const auto declared = std::find_if(found->manifest.windowActions.begin(), found->manifest.windowActions.end(), [&](const RC::ExtensionWindowAction& action) { return action.kind == binding.action.kind && action.id == binding.action.id; });
    if (declared == found->manifest.windowActions.end()) { error = "Window action is not declared by the extension"; return false; }
    static std::atomic<unsigned long long> requestId = 0;
    const std::string id = "window-action-" + std::to_string(++requestId);
    const std::string request = "{\"id\":\"" + jsonEscape(id) + "\",\"method\":\"window/action\",\"params\":{\"action\":\"" + jsonEscape(binding.action.id) + "\",\"context\":{\"windowId\":\"" + jsonEscape(context.windowId) + "\",\"kind\":\"" + jsonEscape(context.kind) + "\",\"title\":\"" + jsonEscape(context.title) + "\",\"scriptType\":\"" + jsonEscape(context.scriptType) + "\",\"scriptName\":\"" + jsonEscape(context.scriptName) + "\",\"text\":\"" + jsonEscape(context.text) + "\",\"selection\":\"" + jsonEscape(context.selection) + "}}}";
    const std::size_t index = static_cast<std::size_t>(std::distance(extensions.begin(), found));
    sendRequest(index, request);
    return true;
}

void TExtensionsManager::onEnable(GtkToggleButton* button, gpointer data) { auto* manager = static_cast<TExtensionsManager*>(data); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(button), "extension-index")); manager->setEnabled(index, gtk_toggle_button_get_active(button)); }
void TExtensionsManager::onOpen(GtkButton* button, gpointer data) { auto* manager = static_cast<TExtensionsManager*>(data); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(button), "extension-index")); if (index >= manager->extensions.size()) return; const auto& manifest = manager->extensions[index].manifest; const auto path = manifest.autoDiscover || manifest.entry.empty() ? manifest.directory : manifest.directory / manifest.entry; gchar* uri = g_filename_to_uri(path.string().c_str(), nullptr, nullptr); if (uri != nullptr) { gtk_show_uri_on_window(manager->window == nullptr ? nullptr : GTK_WINDOW(manager->window), uri, GDK_CURRENT_TIME, nullptr); g_free(uri); } }
void TExtensionsManager::onLog(GtkButton* button, gpointer data) { auto* manager = static_cast<TExtensionsManager*>(data); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(button), "extension-index")); manager->showLog(index); }
void TExtensionsManager::onOutputTab(GtkButton* button, gpointer data) { auto* manager = static_cast<TExtensionsManager*>(data); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(button), "extension-index")); if (index < manager->extensions.size()) manager->setOutputTab(index, !manager->extensions[index].outputToTab); }
void TExtensionsManager::onDetails(GtkButton* button, gpointer data) { auto* manager = static_cast<TExtensionsManager*>(data); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(button), "extension-index")); manager->showDetails(index); }
void TExtensionsManager::onRemove(GtkButton* button, gpointer data) { auto* manager = static_cast<TExtensionsManager*>(data); const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(button), "extension-index")); manager->removeExtension(index); }

void TExtensionsManager::setEnabled(std::size_t index, bool enabled) {
    if (index >= extensions.size()) return;
    extensions[index].enabled = enabled;
    if (!enabled) extensions[index].runtimeError.clear();
    saveState();
    if (enabled) launch(index); else stop(index);
    refresh();
}

void TExtensionsManager::saveState() {
    std::filesystem::create_directories(statePath().parent_path());
    std::ofstream state(statePath(), std::ios::trunc);
    for (const auto& extension : extensions) { state << extension.manifest.id << ' ' << (extension.enabled ? 1 : 0) << ' ' << (extension.outputToTab ? 1 : 0); for (const auto& capability : extension.approvedCapabilities) state << ' ' << capability; state << '\n'; }
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

void TExtensionsManager::removeExtension(std::size_t index) {
    if (index >= extensions.size()) return;
    auto& extension = extensions[index];
    GtkWidget* dialog = gtk_message_dialog_new(window == nullptr ? parent : GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "Remove extension '%s'?", extension.manifest.name.c_str());
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "This removes the installed package directory and stops its runtime.");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Close", GTK_RESPONSE_CANCEL, "Remove", GTK_RESPONSE_ACCEPT, nullptr);
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response != GTK_RESPONSE_ACCEPT) return;
    if (extension.launching) { RC::appendExtensionLog(extension.log, "Remove deferred while the extension is starting; disable it and try again."); extension.enabled = false; saveState(); refresh(); return; }
    const std::string name = extension.manifest.name;
    const auto directory = extension.manifest.directory;
    stop(index);
    if (closeCallback) closeCallback(name);
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    if (error) { RC::appendExtensionLog(extension.log, "Remove failed: " + error.message()); refresh(); return; }
    extensions.erase(extensions.begin() + static_cast<std::ptrdiff_t>(index));
    saveState();
    refresh();
}

void TExtensionsManager::launch(std::size_t index) {
    if (index >= extensions.size() || extensions[index].pid != 0 || extensions[index].launching) return;
    const auto& manifest = extensions[index].manifest;
    extensions[index].runtimeError.clear();
    if (manifest.runtime == "gs2engine-stdio" && manifest.api != 1) { extensions[index].runtimeError = "Unsupported GS2 extension API: " + std::to_string(manifest.api); RC::appendExtensionLog(extensions[index].log, extensions[index].runtimeError); refresh(); return; }
    const auto entry = manifest.entry.empty() ? std::filesystem::path() : manifest.directory / manifest.entry;
    if (!manifest.autoDiscover && !std::filesystem::exists(entry)) { extensions[index].runtimeError = "Entry not found: " + entry.string(); RC::appendExtensionLog(extensions[index].log, extensions[index].runtimeError); refresh(); return; }
    std::vector<std::string> arguments;
    if (manifest.runtime == "gs2engine-stdio") {
#ifdef _WIN32
        const auto host = manifest.directory / "GS2Engine.exe";
#else
        const auto host = manifest.directory / "GS2Engine";
#endif
        if (!std::filesystem::exists(host)) { extensions[index].runtimeError = "GS2Engine host not found: " + host.string(); RC::appendExtensionLog(extensions[index].log, extensions[index].runtimeError); refresh(); return; }
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
        g_main_context_invoke_full(nullptr, G_PRIORITY_DEFAULT, onLaunchComplete, result, +[](gpointer data) { delete static_cast<LaunchResult*>(data); });
    }).detach();
}

gboolean TExtensionsManager::onLaunchComplete(gpointer data) {
    auto* result = static_cast<LaunchResult*>(data);
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
    if (!error.empty()) { extensions[index].runtimeError = error; RC::appendExtensionLog(extensions[index].log, error); refresh(); return; }
    extensions[index].runtimeError.clear();
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
                    g_main_context_invoke_full(nullptr, G_PRIORITY_DEFAULT, onReaderLine, new ReaderLine{state, index, protocol, pending.substr(0, newline)}, +[](gpointer data) { delete static_cast<ReaderLine*>(data); });
                    pending.erase(0, newline + 1);
                }
            }
            if (!pending.empty() && !reader->stop.load()) g_main_context_invoke_full(nullptr, G_PRIORITY_DEFAULT, onReaderLine, new ReaderLine{state, index, protocol, pending}, +[](gpointer data) { delete static_cast<ReaderLine*>(data); });
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
    auto* result = static_cast<ReaderLine*>(data);
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

void TExtensionsManager::onChildExit(GPid pid, gint, gpointer data) { auto* manager = static_cast<TExtensionsManager*>(data); for (auto& extension : manager->extensions) if (extension.pid == pid) { extension.pid = 0; extension.childWatch = 0; if (extension.enabled && !extension.launching) { extension.runtimeError = "Runtime exited unexpectedly"; RC::appendExtensionLog(extension.log, extension.runtimeError); } if (extension.outputWatch != 0) { g_source_remove(extension.outputWatch); extension.outputWatch = 0; } if (extension.errorWatch != 0) { g_source_remove(extension.errorWatch); extension.errorWatch = 0; } if (extension.input != nullptr) { g_io_channel_unref(extension.input); extension.input = nullptr; } if (extension.output != nullptr) { g_io_channel_unref(extension.output); extension.output = nullptr; } if (extension.errorOutput != nullptr) { g_io_channel_unref(extension.errorOutput); extension.errorOutput = nullptr; }
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

void TExtensionsManager::showDetails(std::size_t index) {
    if (index >= extensions.size()) return;
    auto& extension = extensions[index];
    const auto& manifest = extension.manifest;
    GtkWidget* dialog = gtk_dialog_new_with_buttons(manifest.name.c_str(), window == nullptr ? nullptr : GTK_WINDOW(window), GTK_DIALOG_MODAL, "Close", GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 460, 320);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    const std::string status = !manifest.error.empty() || !extension.runtimeError.empty() ? "failed" : extension.launching ? "starting" : extension.pid != 0 ? "running" : extension.enabled ? "stopped" : "disabled";
    std::string summary = "ID: " + manifest.id + "\nVersion: " + manifest.version + "\nStatus: " + status + "\nRuntime: " + manifest.runtime + "\nEntry: " + (manifest.entry.empty() ? (manifest.autoDiscover ? "automatic discovery" : "none") : manifest.entry);
    if (!manifest.publisher.empty()) summary += "\nPublisher: " + manifest.publisher;
    GtkWidget* label = gtk_label_new(summary.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_pack_start(GTK_BOX(content), label, false, false, 6);
    GtkWidget* capabilitiesFrame = gtk_frame_new("Requested capabilities");
    GtkWidget* capabilities = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_container_set_border_width(GTK_CONTAINER(capabilities), 6);
    gtk_container_add(GTK_CONTAINER(capabilitiesFrame), capabilities);
    std::vector<std::pair<std::string, GtkWidget*>> capabilityButtons;
    for (const auto& capability : manifest.requestedCapabilities) { GtkWidget* button = gtk_check_button_new_with_label(capability.c_str()); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), extension.approvedCapabilities.count(capability) != 0); gtk_box_pack_start(GTK_BOX(capabilities), button, false, false, 0); capabilityButtons.emplace_back(capability, button); }
    if (manifest.requestedCapabilities.empty()) gtk_box_pack_start(GTK_BOX(capabilities), gtk_label_new("None declared"), false, false, 0);
    gtk_box_pack_start(GTK_BOX(content), capabilitiesFrame, false, false, 6);
    std::string slots = "UI slots: ";
    for (const auto& slot : manifest.uiContributions) { if (slots.size() > 10) slots += ", "; slots += slot; }
    if (manifest.uiContributions.empty()) slots += "none";
    GtkWidget* slotsLabel = gtk_label_new(slots.c_str());
    gtk_label_set_xalign(GTK_LABEL(slotsLabel), 0.0f);
    gtk_box_pack_start(GTK_BOX(content), slotsLabel, false, false, 6);
    const auto joinList = [](const std::vector<std::string>& values) { std::string result; for (const auto& value : values) { if (!result.empty()) result += ", "; result += value; } return result.empty() ? std::string("none") : result; };
    GtkWidget* contributions = gtk_label_new(("Themes: " + joinList(manifest.themes) + "\nCommands/shortcuts: " + joinList(manifest.commands) + "\nRead-only views: " + joinList(manifest.readOnlyViews)).c_str());
    gtk_label_set_xalign(GTK_LABEL(contributions), 0.0f);
    gtk_box_pack_start(GTK_BOX(content), contributions, false, false, 6);
    if (!manifest.error.empty() || !extension.runtimeError.empty()) { const std::string errorText = !manifest.error.empty() ? "Manifest error: " + manifest.error : "Runtime error: " + extension.runtimeError; GtkWidget* error = gtk_label_new(errorText.c_str()); gtk_label_set_xalign(GTK_LABEL(error), 0.0f); gtk_box_pack_start(GTK_BOX(content), error, false, false, 6); }
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    extension.approvedCapabilities.clear();
    for (const auto& item : capabilityButtons) if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(item.second))) extension.approvedCapabilities.insert(item.first);
    saveState();
    gtk_widget_destroy(dialog);
}
