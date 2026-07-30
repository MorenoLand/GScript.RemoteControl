#pragma once
#include "TRCOptions.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
struct McpGuiActions {
    std::function<std::string()> serverName;
    std::function<bool()> connected;
    std::function<std::vector<std::string>()> connectionNames;
    std::function<bool(const std::string&, std::string&)> selectConnection;
    std::function<bool()> loginVisible;
    std::function<std::string()> loginAccount;
    std::function<std::string()> loginNickname;
    std::function<bool()> loginHasPassword;
    std::function<bool(const std::string&, const std::string&, std::string&)> submitLogin;
    std::function<std::vector<std::string>()> serverNames;
    std::function<bool(const std::string&, std::string&)> connectServer;
    std::function<bool(const std::string&, std::string&)> openView;
    std::function<bool(const std::string&, std::string&)> sendChat;
};
int runMcpServer(const RC::RCOptions& options, const std::filesystem::path& applicationDirectory, unsigned long requestedInstance = 0);
void startMcpGuiBridge(RC::RCOptions& options, const std::filesystem::path& applicationDirectory, McpGuiActions actions);
void updateMcpGuiBridgeOptions(const RC::RCOptions& options);
void stopMcpGuiBridge();
