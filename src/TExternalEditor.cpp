#include "TExternalEditor.h"

#include <gio/gio.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <algorithm>
#include <fstream>

namespace {
std::string safePathPart(std::string value) {
    for (char& character : value) if (character == '<' || character == '>' || character == ':' || character == '"' || character == '/' || character == '\\' || character == '|' || character == '?' || character == '*') character = '_';
    if (value.empty()) return "untitled";
    if (value == ".") return "_";
    if (value == "..") return "__";
    return value;
}
}

struct TExternalEditor::Session {
    std::filesystem::path path;
    std::string content;
    std::function<void(const std::string&)> onSaved;
    GFileMonitor* monitor = nullptr;
    guint debounce = 0;
    ~Session() { if (debounce != 0) g_source_remove(debounce); if (monitor != nullptr) g_object_unref(monitor); }
};

TExternalEditor::TExternalEditor(std::filesystem::path nextWorkspace, std::string nextCommand) : workspace(std::move(nextWorkspace)), command(std::move(nextCommand)) {}
TExternalEditor::~TExternalEditor() = default;

void TExternalEditor::open(const std::string& server, const std::string& category, const std::string& name, const std::string& content, std::function<void(const std::string&)> onSaved) {
    std::filesystem::path path = workspace / safePathPart(server) / safePathPart(category) / safePathPart(name);
    if (path.extension().empty()) path += ".gs2";
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content;
    stream.close();
    sessions.erase(std::remove_if(sessions.begin(), sessions.end(), [&](const auto& existing) { return existing->path == path; }), sessions.end());
    auto session = std::make_unique<Session>();
    session->path = path;
    session->content = content;
    session->onSaved = std::move(onSaved);
    GFile* file = g_file_new_for_path(path.string().c_str());
    session->monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, nullptr, nullptr);
    g_object_unref(file);
    if (session->monitor != nullptr) g_signal_connect(session->monitor, "changed", G_CALLBACK(onChanged), session.get());
    sessions.push_back(std::move(session));
#ifdef _WIN32
    if (command.empty()) ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else {
        std::string invocation = command;
        const std::string quoted = "\"" + path.string() + "\"";
        const std::size_t marker = invocation.find("{file}");
        if (marker == std::string::npos) invocation = std::filesystem::exists(command) ? "\"" + command + "\" " + quoted : invocation + " " + quoted; else invocation.replace(marker, 6, quoted);
        g_spawn_command_line_async(invocation.c_str(), nullptr);
    }
#else
    if (command.empty()) {
        GFile* launchFile = g_file_new_for_path(path.string().c_str());
        char* uri = g_file_get_uri(launchFile);
        g_app_info_launch_default_for_uri(uri, nullptr, nullptr);
        g_free(uri);
        g_object_unref(launchFile);
    } else {
        std::string invocation = command;
        char* quoted = g_shell_quote(path.string().c_str());
        const std::size_t marker = invocation.find("{file}");
        if (marker == std::string::npos) invocation += " " + std::string(quoted); else invocation.replace(marker, 6, quoted);
        g_free(quoted);
        gint argumentCount = 0;
        gchar** arguments = nullptr;
        if (g_shell_parse_argv(invocation.c_str(), &argumentCount, &arguments, nullptr)) {
            g_spawn_async(nullptr, arguments, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr);
            g_strfreev(arguments);
        }
    }
#endif
}

void TExternalEditor::onChanged(_GFileMonitor*, void*, void*, int event, void* data) {
    auto* session = static_cast<Session*>(data);
    if (event != G_FILE_MONITOR_EVENT_CHANGED && event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT && event != G_FILE_MONITOR_EVENT_CREATED && event != G_FILE_MONITOR_EVENT_MOVED_IN) return;
    if (session->debounce != 0) g_source_remove(session->debounce);
    session->debounce = g_timeout_add(250, G_SOURCE_FUNC(onDebounced), session);
}

int TExternalEditor::onDebounced(void* data) {
    auto* session = static_cast<Session*>(data);
    session->debounce = 0;
    std::ifstream stream(session->path, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (!stream || content == session->content) return G_SOURCE_REMOVE;
    session->content = content;
    session->onSaved(content);
    return G_SOURCE_REMOVE;
}
