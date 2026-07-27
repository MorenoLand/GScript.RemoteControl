#include "TMcpServer.h"
#include "TMcpProtocol.h"
#include "TScriptEditorTracking.h"
#include <gtk/gtk.h>
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace {
    constexpr const char* PipePrefix = R"(\\.\pipe\GraalRCMcp-)";
    RC::RCOptions bridgeOptions;
    bool bridgeOptionsReady = false;
    std::filesystem::path bridgeDirectory;
    std::function<std::string()> bridgeServerName;
    std::function<bool()> bridgeConnected;
    McpGuiActions bridgeActions;
    std::atomic<bool> bridgeStopping = false;
    std::thread bridgeThread;
#ifdef _WIN32
    std::mutex pipeMutex;
    std::vector<HANDLE> activePipes;
    std::vector<std::thread> pipeClients;
#endif

    std::string jsonEscape(const std::string& value) { std::string result; for (unsigned char c : value) { if (c == '"') result += "\\\""; else if (c == '\\') result += "\\\\"; else if (c == '\n') result += "\\n"; else if (c == '\r') result += "\\r"; else if (c == '\t') result += "\\t"; else if (c < 0x20) { char text[7]; std::snprintf(text, sizeof(text), "\\u%04x", c); result += text; } else result += static_cast<char>(c); } return result; }
    std::string jsonString(const std::string& value) { return "\"" + jsonEscape(value) + "\""; }
    std::string stringField(const std::string& json, const std::string& key) { std::string result; mcpJsonStringField(json, key, result); return result; }
    std::string idField(const std::string& json) { const std::size_t key = json.find("\"id\""); if (key == std::string::npos) return ""; const std::size_t colon = json.find(':', key + 4); if (colon == std::string::npos) return "null"; std::size_t start = json.find_first_not_of(" \t", colon + 1); if (start == std::string::npos) return "null"; if (json[start] == '"') { std::size_t end = start + 1; while (end < json.size() && (json[end] != '"' || json[end - 1] == '\\')) ++end; return json.substr(start, end - start + 1); } const std::size_t end = json.find_first_of(",}", start); return json.substr(start, end - start); }
    std::string response(const std::string& id, const std::string& result) { return "{\"jsonrpc\":\"2.0\",\"id\":" + (id.empty() ? "null" : id) + ",\"result\":" + result + "}"; }
    std::string errorResponse(const std::string& id, int code, const std::string& message) { return "{\"jsonrpc\":\"2.0\",\"id\":" + (id.empty() ? "null" : id) + ",\"error\":{\"code\":" + std::to_string(code) + ",\"message\":" + jsonString(message) + "}}"; }
    std::string toolResult(const std::string& text, bool error = false) { return "{\"content\":[{\"type\":\"text\",\"text\":" + jsonString(text) + "}],\"isError\":" + (error ? "true" : "false") + "}"; }
    void audit(const std::string& method, const std::string& tool, const std::string& outcome) { if (!bridgeOptionsReady || !bridgeOptions.mcpaudit) return; std::ofstream stream(bridgeDirectory / "mcp-audit.log", std::ios::app); stream << "{\"time\":" << std::time(nullptr) << ",\"method\":" << jsonString(method) << ",\"tool\":" << jsonString(tool) << ",\"outcome\":" << jsonString(outcome) << "}\n"; }
    bool serverAllowed(std::string& reason) { const std::string server = bridgeServerName ? bridgeServerName() : ""; if (!bridgeOptions.mcpserverscope.empty() && g_ascii_strcasecmp(server.c_str(), bridgeOptions.mcpserverscope.c_str()) != 0) { reason = "Current server is outside configured MCP server scope"; return false; } return true; }
    bool approve(const std::string& action) { if (!bridgeOptions.mcpapprove) return true; GtkWidget* dialog = gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Allow this MCP action?\n\n%s", action.c_str()); gtk_window_set_title(GTK_WINDOW(dialog), "RC MCP Approval"); gtk_dialog_add_button(GTK_DIALOG(dialog), "Deny", GTK_RESPONSE_CANCEL); gtk_dialog_add_button(GTK_DIALOG(dialog), "Allow once", GTK_RESPONSE_ACCEPT); const bool allowed = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT; gtk_widget_destroy(dialog); return allowed; }
    std::string scriptType(const std::string& name) { if (name.rfind("Weapon/GUI Script:", 0) == 0) return "Weapon"; if (name.rfind("Class:", 0) == 0) return "Class"; if (name.rfind("NPC:", 0) == 0) return "NPC"; return ""; }
    bool scriptTypeApproved(const std::string& type) { if (type == "Weapon") return bridgeOptions.mcpapproveweapon; if (type == "Class") return bridgeOptions.mcpapproveclass; if (type == "NPC") return bridgeOptions.mcpapprovenpc; return false; }
    void saveScriptTypeApproval(const std::string& type) { if (type == "Weapon") bridgeOptions.mcpapproveweapon = true; else if (type == "Class") bridgeOptions.mcpapproveclass = true; else if (type == "NPC") bridgeOptions.mcpapprovenpc = true; RC::saveRCOptions(bridgeOptions, bridgeDirectory); }
    bool approveScript(const std::string& action, const std::string& name) {
        if (!bridgeOptions.mcpapprove) return true;
        const std::string type = scriptType(name);
        if (!type.empty() && scriptTypeApproved(type)) return true;
        const std::string message = action == "Edit" ? "Edit the open script buffer without saving or applying it?" : "Save/Apply the current open script buffer to the server?";
        GtkWidget* dialog = gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s\n\n%s", message.c_str(), name.c_str());
        gtk_window_set_title(GTK_WINDOW(dialog), "RC MCP Script Approval");
        gtk_dialog_add_button(GTK_DIALOG(dialog), "Deny", GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button(GTK_DIALOG(dialog), "Allow once", GTK_RESPONSE_ACCEPT);
        constexpr int AlwaysAllow = 1001;
        if (!type.empty()) gtk_dialog_add_button(GTK_DIALOG(dialog), ("Always allow " + type + " scripts").c_str(), AlwaysAllow);
        const int response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        if (response == AlwaysAllow) { saveScriptTypeApproval(type); return true; }
        return response == GTK_RESPONSE_ACCEPT;
    }
    void collectWindows(GtkWidget* widget, std::vector<std::string>& tabs) { if (!GTK_IS_CONTAINER(widget)) return; if (GTK_IS_NOTEBOOK(widget)) { const int count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(widget)); for (int i = 0; i < count; ++i) { GtkWidget* page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(widget), i); GtkWidget* label = gtk_notebook_get_tab_label(GTK_NOTEBOOK(widget), page); if (GTK_IS_LABEL(label)) tabs.push_back(gtk_label_get_text(GTK_LABEL(label))); } } GList* children = gtk_container_get_children(GTK_CONTAINER(widget)); for (GList* child = children; child != nullptr; child = child->next) collectWindows(GTK_WIDGET(child->data), tabs); g_list_free(children); }
    std::string windowsText(const std::string& requested = "") { std::ostringstream out; out << "["; bool first = true; GList* windows = gtk_window_list_toplevels(); for (GList* item = windows; item != nullptr; item = item->next) { GtkWidget* window = GTK_WIDGET(item->data); if (!gtk_widget_get_visible(window)) continue; const std::string title = gtk_window_get_title(GTK_WINDOW(window)) == nullptr ? "" : gtk_window_get_title(GTK_WINDOW(window)); if (!requested.empty() && g_ascii_strcasecmp(title.c_str(), requested.c_str()) != 0) continue; if (!first) out << ','; first = false; std::vector<std::string> tabs; collectWindows(window, tabs); out << "{\"title\":" << jsonString(title) << ",\"active\":" << (gtk_window_is_active(GTK_WINDOW(window)) ? "true" : "false") << ",\"tabs\":["; for (std::size_t i = 0; i < tabs.size(); ++i) { if (i) out << ','; out << jsonString(tabs[i]); } out << "]}"; } g_list_free(windows); out << "]"; return out.str(); }
    std::string editorsText(const std::string& requested = "") { std::ostringstream out; out << "["; bool first = true; for (const auto& editor : scriptEditorSnapshots()) { if (!requested.empty() && editor.name != requested) continue; if (!first) out << ','; first = false; out << "{\"name\":" << jsonString(editor.name) << ",\"active\":" << (editor.active ? "true" : "false") << ",\"modified\":" << (editor.modified ? "true" : "false") << ",\"text\":" << jsonString(editor.text) << ",\"selection\":" << jsonString(editor.selection) << "}"; } out << "]"; return out.str(); }
    bool editorText(const std::string& requested, std::string& text) { for (const auto& editor : scriptEditorSnapshots()) if (editor.name == requested) { text = editor.text; return true; } return false; }
    std::string loginText() { return "{\"visible\":" + std::string(bridgeActions.loginVisible && bridgeActions.loginVisible() ? "true" : "false") + ",\"account\":" + jsonString(bridgeActions.loginAccount ? bridgeActions.loginAccount() : "") + ",\"nickname\":" + jsonString(bridgeActions.loginNickname ? bridgeActions.loginNickname() : "") + ",\"hasPassword\":" + (bridgeActions.loginHasPassword && bridgeActions.loginHasPassword() ? "true" : "false") + "}"; }
    std::string serversText() { std::string result = "["; bool comma = false; if (bridgeActions.serverNames) for (const std::string& name : bridgeActions.serverNames()) { if (comma) result += ','; comma = true; result += jsonString(name); } return result + "]"; }
    std::string connectionsText() { std::string result = "["; bool comma = false; if (bridgeActions.connectionNames) for (const std::string& name : bridgeActions.connectionNames()) { if (comma) result += ','; comma = true; result += jsonString(name); } return result + "]"; }
    std::string instanceText() {
        const auto editors = scriptEditorSnapshots();
        bool active = false;
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item != nullptr && !active; item = item->next) active = gtk_window_is_active(GTK_WINDOW(item->data));
        g_list_free(windows);
#ifdef _WIN32
        const unsigned long processId = GetCurrentProcessId();
#else
        const unsigned long processId = 0;
#endif
        return "{\"processId\":" + std::to_string(processId) + ",\"active\":" + (active ? "true" : "false") + ",\"serverConnected\":" + ((bridgeConnected && bridgeConnected()) ? "true" : "false") + ",\"editorCount\":" + std::to_string(editors.size()) + ",\"server\":" + jsonString(bridgeServerName ? bridgeServerName() : "") + "}";
    }
    std::string processRequest(const std::string& request) {
        const std::string id = idField(request), method = stringField(request, "method"), tool = method == "tools/call" ? stringField(request, "name") : "";
        const bool full = bridgeOptions.mcpfullcontrol;
        const bool canRead = full || bridgeOptions.mcpread;
        const bool canWrite = full || bridgeOptions.mcpwrite;
        const bool canServer = full || bridgeOptions.mcpserver;
        const bool canLogin = full || bridgeOptions.mcplogin;
        const bool canWindows = full || bridgeOptions.mcpwindows;
        const bool canAdmin = full || bridgeOptions.mcpadmin;
        if (!bridgeOptionsReady) return errorResponse(id, -32002, "RC GUI bridge is not configured");
        if (!bridgeOptions.mcpenabled) { audit(method, tool, "disabled"); return errorResponse(id, -32001, "MCP integration is disabled in RC Options"); }
        if (method == "rc/instance") return response(id, instanceText());
        if (method == "notifications/initialized") { audit(method, tool, "ok"); return ""; }
        if (method == "initialize") { audit(method, tool, "ok"); return response(id, "{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{\"tools\":{\"listChanged\":false}},\"serverInfo\":{\"name\":\"rc\",\"version\":\"3\"}}"); }
        if (method == "tools/list") {
            std::string tools = "["; bool comma = false; auto add = [&](const std::string& value) { if (comma) tools += ','; comma = true; tools += value; };
            if (canRead) {
                add("{\"name\":\"rc_status\",\"description\":\"Current selected RC GUI process and server context.\",\"inputSchema\":{\"type\":\"object\"}}");
                add("{\"name\":\"rc_list_connections\",\"description\":\"List all connected RC server GUI sessions available to MCP.\",\"inputSchema\":{\"type\":\"object\"}}");
                add("{\"name\":\"rc_login_status\",\"description\":\"Login form state without exposing its password.\",\"inputSchema\":{\"type\":\"object\"}}");
                add("{\"name\":\"rc_list_servers\",\"description\":\"List server names loaded by the native RC server selector.\",\"inputSchema\":{\"type\":\"object\"}}");
                add("{\"name\":\"rc_list_windows\",\"description\":\"List visible managed RC windows and tabs.\",\"inputSchema\":{\"type\":\"object\"}}");
                add("{\"name\":\"rc_read_window\",\"description\":\"Read structured state for a visible managed RC window by title.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"title\":{\"type\":\"string\"}},\"required\":[\"title\"]}}");
                add("{\"name\":\"rc_list_script_editors\",\"description\":\"List open tracked script editors, including dirty state.\",\"inputSchema\":{\"type\":\"object\"}}");
                add("{\"name\":\"rc_read_script_editor\",\"description\":\"Read the complete current open editor buffer and selection, including unsaved text.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"editor\":{\"type\":\"string\"}},\"required\":[\"editor\"]}}");
                add("{\"name\":\"rc_read_script_lines\",\"description\":\"Read a bounded line-numbered range from the current open editor buffer, including unsaved text.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"editor\":{\"type\":\"string\"},\"start\":{\"type\":\"integer\",\"minimum\":1},\"end\":{\"type\":\"integer\",\"minimum\":1}},\"required\":[\"editor\",\"start\",\"end\"]}}");
                add("{\"name\":\"rc_search_script_editor\",\"description\":\"Search the current open editor buffer and return bounded line-numbered matches with optional context.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"editor\":{\"type\":\"string\"},\"query\":{\"type\":\"string\"},\"caseSensitive\":{\"type\":\"boolean\"},\"contextLines\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":20}},\"required\":[\"editor\",\"query\"]}}");
            }
            if (canWrite) {
                add("{\"name\":\"rc_replace_script_text\",\"description\":\"Guarded partial edit: replace one unique expected text region in the open buffer and leave it unsaved; never Save/Apply.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"editor\":{\"type\":\"string\"},\"expected\":{\"type\":\"string\",\"maxLength\":65536},\"replacement\":{\"type\":\"string\",\"maxLength\":65536}},\"required\":[\"editor\",\"expected\",\"replacement\"]}}");
                add("{\"name\":\"rc_write_script_editor\",\"description\":\"Edit only the open script buffer and leave it unsaved; does not Save/Apply to the server.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"editor\":{\"type\":\"string\"},\"text\":{\"type\":\"string\"}},\"required\":[\"editor\",\"text\"]}}");
                add("{\"name\":\"rc_save_script_editor\",\"description\":\"Explicitly Save/Apply the current open script buffer to the server.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"editor\":{\"type\":\"string\"}},\"required\":[\"editor\"]}}");
            }
            if (canLogin) {
                add("{\"name\":\"rc_submit_login\",\"description\":\"Submit the native login form using its existing password and optional non-secret account/nickname overrides; never returns credentials.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"account\":{\"type\":\"string\"},\"nickname\":{\"type\":\"string\"}}}}");
                add("{\"name\":\"rc_connect_server\",\"description\":\"Select and connect a named server through the native RC server list workflow.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"server\":{\"type\":\"string\"}},\"required\":[\"server\"]}}");
            }
            if (canServer) add("{\"name\":\"rc_send_chat\",\"description\":\"Send RC chat text through the native connected RC controller.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},\"required\":[\"text\"]}}");
            if (canWindows) add("{\"name\":\"rc_open_window\",\"description\":\"Create/show a supported managed RC view through its native controller.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"view\":{\"type\":\"string\",\"enum\":[\"player_list\",\"file_browser\",\"classes\",\"weapons\",\"npcs\",\"server_options\",\"server_flags\",\"folder_config\",\"toalls\",\"accounts\",\"options\"]}},\"required\":[\"view\"]}}");
            if (canWindows) add("{\"name\":\"rc_select_connection\",\"description\":\"Select which connected RC server session subsequent MCP GUI actions target.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"server\":{\"type\":\"string\"}},\"required\":[\"server\"]}}");
            if (canAdmin) add("{\"name\":\"rc_focused_window_action\",\"description\":\"Present or close the focused RC window.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"present\",\"close\"]}},\"required\":[\"action\"]}}");
            tools += "]"; audit(method, tool, "ok"); return response(id, "{\"tools\":" + tools + "}");
        }
        if (method != "tools/call") { audit(method, tool, "method-not-found"); return errorResponse(id, -32601, "Method not found"); }
        const bool connectionTool = tool == "rc_status" || tool == "rc_list_connections" || tool == "rc_select_connection" || tool == "rc_login_status" || tool == "rc_submit_login" || tool == "rc_list_servers" || tool == "rc_connect_server" || tool == "rc_list_windows" || tool == "rc_read_window";
        std::string reason; if (!connectionTool && !serverAllowed(reason)) { audit(method, tool, "server-scope-denied"); return errorResponse(id, -32003, reason); }
        if (tool == "rc_status" && canRead) { const std::string server = bridgeServerName ? bridgeServerName() : ""; const std::string text = std::string("{\"bridge\":\"connected\",\"processId\":") + std::to_string(
#ifdef _WIN32
            GetCurrentProcessId()
#else
            0
#endif
        ) + ",\"serverConnected\":" + ((bridgeConnected && bridgeConnected()) ? "true" : "false") + ",\"server\":" + jsonString(server) + ",\"serverScope\":" + jsonString(bridgeOptions.mcpserverscope) + "}"; audit(method, tool, "ok"); return response(id, toolResult(text)); }
        if (tool == "rc_list_connections" && canRead) { audit(method, tool, "ok"); return response(id, toolResult(connectionsText())); }
        if (tool == "rc_login_status" && canRead) { audit(method, tool, "ok"); return response(id, toolResult(loginText())); }
        if (tool == "rc_list_servers" && canRead) { audit(method, tool, "ok"); return response(id, toolResult(serversText())); }
        if (tool == "rc_list_windows" && canRead) { audit(method, tool, "ok"); return response(id, toolResult(windowsText())); }
        if (tool == "rc_read_window" && canRead) { audit(method, tool, "ok"); return response(id, toolResult(windowsText(stringField(request, "title")))); }
        if (tool == "rc_list_script_editors" && canRead) { audit(method, tool, "ok"); return response(id, toolResult(editorsText())); }
        if (tool == "rc_read_script_editor" && canRead) { const std::string name = stringField(request, "editor"); audit(method, tool, "ok"); return response(id, toolResult(editorsText(name))); }
        if (tool == "rc_read_script_lines" && canRead) { const std::string name = stringField(request, "editor"); std::string text; const bool ok = editorText(name, text); audit(method, tool, ok ? "ok" : "failed"); return response(id, toolResult(ok ? mcpEditorLineRange(text, mcpJsonIntField(request, "start", 1), mcpJsonIntField(request, "end", 200)) : "Script editor is not open", !ok)); }
        if (tool == "rc_search_script_editor" && canRead) { const std::string name = stringField(request, "editor"), query = stringField(request, "query"); std::string text; const bool ok = editorText(name, text); audit(method, tool, ok ? "ok" : "failed"); return response(id, toolResult(ok ? mcpEditorSearch(text, query, mcpJsonBoolField(request, "caseSensitive", false), mcpJsonIntField(request, "contextLines", 0)) : "Script editor is not open", !ok)); }
        if (tool == "rc_replace_script_text" && canWrite) { std::string name, expected, replacement; if (!mcpJsonStringField(request, "editor", name) || !mcpJsonStringField(request, "expected", expected) || !mcpJsonStringField(request, "replacement", replacement)) { audit(method, tool, "invalid-json-string"); return errorResponse(id, -32602, "Partial edit requires valid JSON editor, expected, and replacement strings"); } if (!approveScript("Edit", name)) { audit(method, tool, "approval-denied"); return errorResponse(id, -32002, "Mutation denied by user"); } std::string error; const bool ok = replaceScriptEditorText(name, expected, replacement, error); audit(method, tool, ok ? "ok" : "failed"); return response(id, toolResult(ok ? "Open script buffer partially edited; changes remain unsaved" : error, !ok)); }
        if (tool == "rc_write_script_editor" && canWrite) { std::string name, text; if (!mcpJsonStringField(request, "editor", name) || !mcpJsonStringField(request, "text", text)) { audit(method, tool, "invalid-json-string"); return errorResponse(id, -32602, "Edit requires valid JSON editor and text strings"); } if (!approveScript("Edit", name)) { audit(method, tool, "approval-denied"); return errorResponse(id, -32002, "Mutation denied by user"); } std::string error; const bool ok = writeScriptEditor(name, text, error); audit(method, tool, ok ? "ok" : "failed"); return response(id, toolResult(ok ? "Open script buffer edited; changes remain unsaved" : error, !ok)); }
        if (tool == "rc_save_script_editor" && canWrite) { const std::string name = stringField(request, "editor"); if (!approveScript("Save/Apply", name)) { audit(method, tool, "approval-denied"); return errorResponse(id, -32002, "Mutation denied by user"); } std::string error; const bool ok = saveScriptEditor(name, error); audit(method, tool, ok ? "ok" : "failed"); return response(id, toolResult(ok ? "Script Save/Apply action invoked" : error, !ok)); }
        if (tool == "rc_submit_login" && canLogin) { if (!approve("Submit the native RC login form")) return errorResponse(id, -32002, "Mutation denied by user"); std::string error; const bool ok = bridgeActions.submitLogin && bridgeActions.submitLogin(stringField(request, "account"), stringField(request, "nickname"), error); audit(method, tool, ok ? "ok" : "failed"); return response(id, toolResult(ok ? "Native login form submitted" : error, !ok)); }
        if (tool == "rc_connect_server" && canLogin) { const std::string server = stringField(request, "server"); if (!approve("Connect to RC server " + server)) return errorResponse(id, -32002, "Mutation denied by user"); std::string error; const bool ok = bridgeActions.connectServer && bridgeActions.connectServer(server, error); audit(method, tool, ok ? "ok" : "failed"); return response(id, toolResult(ok ? "Native server connection invoked" : error, !ok)); }
        if (tool == "rc_select_connection" && canWindows) { const std::string server = stringField(request, "server"); if (!approve("Select RC server session " + server)) return errorResponse(id, -32002, "Mutation denied by user"); std::string error; const bool ok = bridgeActions.selectConnection && bridgeActions.selectConnection(server, error); audit(method, tool, ok ? "ok" : "failed"); return response(id, toolResult(ok ? "RC server session selected" : error, !ok)); }
        if (tool == "rc_open_window" && canWindows) { const std::string view = stringField(request, "view"); if (!approve("Open managed RC view " + view)) return errorResponse(id, -32002, "Mutation denied by user"); std::string error; const bool ok = bridgeActions.openView && bridgeActions.openView(view, error); audit(method, tool, ok ? "ok" : "failed"); return response(id, toolResult(ok ? "Managed RC view opened" : error, !ok)); }
        if (tool == "rc_send_chat" && canServer) { const std::string text = stringField(request, "text"); if (!approve("Send RC chat text")) return errorResponse(id, -32002, "Mutation denied by user"); std::string error; const bool ok = bridgeActions.sendChat && bridgeActions.sendChat(text, error); audit(method, tool, ok ? "ok" : "failed"); return response(id, toolResult(ok ? "RC chat text sent" : error, !ok)); }
        if (tool == "rc_focused_window_action" && canAdmin) { const std::string action = stringField(request, "action"); if (!approve("Focused window action: " + action)) { audit(method, tool, "approval-denied"); return errorResponse(id, -32002, "Mutation denied by user"); } GList* windows = gtk_window_list_toplevels(); GtkWindow* active = nullptr; for (GList* item = windows; item != nullptr && active == nullptr; item = item->next) if (gtk_window_is_active(GTK_WINDOW(item->data))) active = GTK_WINDOW(item->data); if (active != nullptr && action == "present") gtk_window_present(active); else if (active != nullptr && action == "close") gtk_window_close(active); g_list_free(windows); const bool ok = active != nullptr && (action == "present" || action == "close"); audit(method, tool, ok ? "ok" : "failed"); return response(id, toolResult(ok ? "Focused window action invoked" : "No focused window or invalid action", !ok)); }
        audit(method, tool, "scope-disabled"); return errorResponse(id, -32602, "Tool unavailable or disabled by MCP scope");
    }
    struct Pending { std::string request; std::string response; std::mutex mutex; std::condition_variable condition; bool done = false; };
    gboolean dispatch(gpointer data) { auto* pending = static_cast<Pending*>(data); const std::string value = processRequest(pending->request); { std::lock_guard<std::mutex> lock(pending->mutex); pending->response = value; pending->done = true; } pending->condition.notify_one(); return G_SOURCE_REMOVE; }
#ifdef _WIN32
    std::string pipeName(unsigned long processId) { return PipePrefix + std::to_string(processId); }
    std::string dispatchToGui(const std::string& request) { Pending pending; pending.request = request; g_idle_add(dispatch, &pending); std::unique_lock<std::mutex> lock(pending.mutex); pending.condition.wait(lock, [&] { return pending.done; }); return pending.response; }
    HANDLE connectBridgePipe(unsigned long processId) {
        const std::string name = pipeName(processId);
        HANDLE pipe = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY && WaitNamedPipeA(name.c_str(), 500)) pipe = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) { DWORD mode = PIPE_READMODE_MESSAGE; SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr); }
        return pipe;
    }
    std::vector<unsigned long> bridgeProcessIds(unsigned long requestedInstance) {
        if (requestedInstance != 0) return {requestedInstance};
        std::vector<unsigned long> ids;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return ids;
        PROCESSENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (Process32First(snapshot, &entry)) do {
            if (entry.th32ProcessID != GetCurrentProcessId() && g_ascii_strcasecmp(entry.szExeFile, "RemoteControl.exe") == 0) ids.push_back(entry.th32ProcessID);
        } while (Process32Next(snapshot, &entry));
        CloseHandle(snapshot);
        return ids;
    }
    bool pipeExchange(HANDLE pipe, const std::string& request, std::string& value) {
        const std::string line = request + "\n";
        DWORD written = 0;
        if (!WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &written, nullptr)) return false;
        std::vector<char> buffer(1024 * 1024);
        DWORD read = 0;
        if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || read == 0) return false;
        value.assign(buffer.data(), read);
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
        return true;
    }
    HANDLE selectBridgePipe(unsigned long requestedInstance) {
        HANDLE best = INVALID_HANDLE_VALUE;
        int bestScore = -1;
        for (unsigned long candidateId : bridgeProcessIds(requestedInstance)) {
            HANDLE pipe = connectBridgePipe(candidateId);
            if (pipe == INVALID_HANDLE_VALUE) continue;
            std::string probe;
            if (!pipeExchange(pipe, "{\"jsonrpc\":\"2.0\",\"id\":-9001,\"method\":\"rc/instance\",\"params\":{}}", probe)) { CloseHandle(pipe); continue; }
            unsigned long processId = static_cast<unsigned long>(mcpJsonIntField(probe, "processId", 0));
            int editorCount = mcpJsonIntField(probe, "editorCount", -1);
            bool active = mcpJsonBoolField(probe, "active", false);
            bool connected = mcpJsonBoolField(probe, "serverConnected", false);
            if (editorCount < 0) {
                std::string status;
                std::string editors;
                pipeExchange(pipe, "{\"jsonrpc\":\"2.0\",\"id\":-9002,\"method\":\"tools/call\",\"params\":{\"name\":\"rc_status\",\"arguments\":{}}}", status);
                pipeExchange(pipe, "{\"jsonrpc\":\"2.0\",\"id\":-9003,\"method\":\"tools/call\",\"params\":{\"name\":\"rc_list_script_editors\",\"arguments\":{}}}", editors);
                connected = status.find("serverConnected\\\\\\\":true") != std::string::npos;
                editorCount = editors.find("\"text\":\"[]\"") == std::string::npos ? 1 : 0;
            }
            if (processId != candidateId || (requestedInstance != 0 && processId != requestedInstance)) { CloseHandle(pipe); continue; }
            const int score = mcpInstanceScore(active, connected, editorCount);
            if (score > bestScore) { if (best != INVALID_HANDLE_VALUE) CloseHandle(best); best = pipe; bestScore = score; }
            else CloseHandle(pipe);
            if (requestedInstance != 0 && processId == requestedInstance) break;
        }
        return best;
    }
    void servePipe(HANDLE pipe) {
        {
            std::lock_guard<std::mutex> lock(pipeMutex);
            activePipes.push_back(pipe);
        }
        std::vector<char> buffer(1024 * 1024);
        DWORD read = 0;
        while (!bridgeStopping && ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read > 0) {
            std::string request(buffer.data(), read);
            while (!request.empty() && (request.back() == '\n' || request.back() == '\r')) request.pop_back();
            const std::string value = dispatchToGui(request);
            if (!value.empty()) {
                const std::string line = value + "\n";
                DWORD written = 0;
                if (!WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &written, nullptr)) break;
            }
        }
        {
            std::lock_guard<std::mutex> lock(pipeMutex);
            activePipes.erase(std::remove(activePipes.begin(), activePipes.end(), pipe), activePipes.end());
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    void pipeLoop() {
        const std::string name = pipeName(GetCurrentProcessId());
        while (!bridgeStopping) {
            HANDLE pipe = CreateNamedPipeA(name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, 1024 * 1024, 1024 * 1024, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) return;
            const bool connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
            if (!connected || bridgeStopping) { CloseHandle(pipe); continue; }
            std::lock_guard<std::mutex> lock(pipeMutex);
            pipeClients.emplace_back(servePipe, pipe);
        }
    }
#endif
}

int runMcpServer(const RC::RCOptions& options, const std::filesystem::path&, unsigned long requestedInstance) {
#ifdef _WIN32
    if (!options.mcpenabled) { std::cerr << "RC MCP integration is disabled in Options.\n"; return 2; }
    HANDLE pipe = selectBridgePipe(requestedInstance);
    if (pipe == INVALID_HANDLE_VALUE) { std::cerr << "RC MCP bridge unavailable: no running enabled RC GUI client.\n"; return 3; }
    std::string request; std::vector<char> buffer(1024 * 1024);
    while (std::getline(std::cin, request)) { const std::string line = request + "\n"; DWORD written = 0; if (!WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &written, nullptr)) break; if (idField(request).empty()) continue; DWORD read = 0; if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || read == 0) break; std::cout.write(buffer.data(), read); std::cout.flush(); }
    CloseHandle(pipe); return 0;
#else
    std::cerr << "RC MCP GUI bridge is currently available on Windows only.\n"; return 3;
#endif
}

void startMcpGuiBridge(RC::RCOptions& options, const std::filesystem::path& applicationDirectory, McpGuiActions actions) {
    bridgeOptions = options; bridgeOptionsReady = true; bridgeDirectory = applicationDirectory; bridgeActions = std::move(actions); bridgeServerName = bridgeActions.serverName; bridgeConnected = bridgeActions.connected; bridgeStopping = false;
#ifdef _WIN32
    bridgeThread = std::thread(pipeLoop);
#endif
}

void updateMcpGuiBridgeOptions(const RC::RCOptions& options) { bridgeOptions = options; bridgeOptionsReady = true; }

void stopMcpGuiBridge() {
    bridgeStopping = true;
#ifdef _WIN32
    const std::string name = pipeName(GetCurrentProcessId());
    HANDLE wake = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr); if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
    if (bridgeThread.joinable()) bridgeThread.join();
    {
        std::lock_guard<std::mutex> lock(pipeMutex);
        for (HANDLE pipe : activePipes) { CancelIoEx(pipe, nullptr); DisconnectNamedPipe(pipe); }
    }
    for (std::thread& client : pipeClients) if (client.joinable()) client.join();
    pipeClients.clear();
#endif
    bridgeOptionsReady = false;
}
