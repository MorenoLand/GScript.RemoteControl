#pragma once

#include <filesystem>
#include <string>

void setBackupDataDirectory(const std::filesystem::path& directory);
void setBackupServerName(const std::string& serverName);
void backupEditorText(const std::string& type, const std::string& target, const std::string& content, bool modified);
