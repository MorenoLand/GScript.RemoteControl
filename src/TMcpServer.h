#pragma once
#include "TRCOptions.h"
#include <filesystem>
#include <functional>
int runMcpServer(const RC::RCOptions& options, const std::filesystem::path& applicationDirectory);
void startMcpGuiBridge(RC::RCOptions& options, const std::filesystem::path& applicationDirectory, std::function<std::string()> serverName, std::function<bool()> connected);
void updateMcpGuiBridgeOptions(const RC::RCOptions& options);
void stopMcpGuiBridge();
