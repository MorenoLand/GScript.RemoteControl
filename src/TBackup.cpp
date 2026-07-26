#include "TBackup.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

    std::filesystem::path dataDirectory;
    std::string currentServer;

    bool shouldBackup(const std::string& type) {
        return type != "file" && type.find("dump") == std::string::npos && type != "banhistory" && type != "staffactivity" && type != "localbans";
    }

    std::string escapedFilename(const std::string& value) {
        std::ostringstream result;
        for (unsigned char character : value) {
            if (std::isalnum(character) != 0) result << static_cast<char>(character);
            else result << '%' << std::setw(3) << std::setfill('0') << static_cast<unsigned int>(character);
        }
        return result.str();
    }

    std::filesystem::path backupRoot() { return dataDirectory / "cache"; }

}

void setBackupDataDirectory(const std::filesystem::path& directory) { dataDirectory = directory; }
void setBackupServerName(const std::string& serverName) { currentServer = serverName; }

void backupEditorText(const std::string& type, const std::string& target, const std::string& content, bool modified) {
    if (currentServer.empty() || !shouldBackup(type)) return;
    const std::filesystem::path path = backupRoot() / "backups" / currentServer / (modified ? "modified" : "original") / type / ((target.empty() ? type : escapedFilename(target)) + ".txt");
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (output) output.write(content.data(), static_cast<std::streamsize>(content.size()));
}
