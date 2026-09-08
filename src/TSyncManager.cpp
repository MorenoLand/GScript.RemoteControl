#include "TSyncManager.h"
#include "TEncryption.h"
#include <grclib.h>
#include <cctype>
#include <cstdint>
#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <openssl/crypto.h>
#include <sstream>

namespace {
constexpr gint64 pullInterval = 15LL * 60LL * G_USEC_PER_SEC;
constexpr std::array<unsigned char, 8> credentialMagic = {'G', 'S', 'R', 'C', 'G', 'I', 'T', '1'};
constexpr std::size_t keySize = RC::Encryption::keySize;
enum FileRootColumn { FileRootSelected, FileRootPath, FileRootLabel, FileRootIcon, FileRootLoaded, FileRootPlaceholder, FileRootColumnCount };

std::string safeName(const std::string& value) {
    std::string result;
    for (unsigned char c : value) result += std::isalnum(c) || c == '-' || c == '_' || c == '.' ? static_cast<char>(c) : '_';
    while (!result.empty() && (result.back() == '.' || result.back() == ' ')) result.pop_back();
    return result.empty() ? "script" : result;
}

std::string stableSuffix(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : value) { hash ^= c; hash *= 1099511628211ULL; }
    std::ostringstream text;
    text << std::hex << std::setw(8) << std::setfill('0') << static_cast<std::uint32_t>(hash);
    return text.str();
}

std::string keyFor(TSyncManager::ScriptType type, const std::string& name, int id) {
    if (type == TSyncManager::ScriptType::NPC) return "npc:" + std::to_string(id);
    return (type == TSyncManager::ScriptType::Class ? "class:" : "weapon:") + name;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return stream ? std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()) : std::string();
}
void applyModifiedTime(const std::filesystem::path& path, int timestamp) {
    if (timestamp <= 0) return;
    const auto systemTime = std::chrono::system_clock::from_time_t(timestamp);
    const auto fileTime = std::filesystem::file_time_type::clock::now() + (systemTime - std::chrono::system_clock::now());
    std::error_code error;
    std::filesystem::last_write_time(path, fileTime, error);
}
std::string formatEta(gint64 seconds) {
    if (seconds < 0) return "";
    if (seconds < 60) return std::to_string(seconds) + "s";
    if (seconds < 3600) return std::to_string(seconds / 60) + "m " + std::to_string(seconds % 60) + "s";
    return std::to_string(seconds / 3600) + "h " + std::to_string((seconds % 3600) / 60) + "m";
}

bool writeFile(const std::filesystem::path& path, const std::string& content) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    return stream.good();
}

std::size_t contentHash(const std::string& content) { return std::hash<std::string>{}(content); }

const char* scriptTypeName(TSyncManager::ScriptType type) {
    if (type == TSyncManager::ScriptType::Class) return "class";
    if (type == TSyncManager::ScriptType::NPC) return "npc";
    return "weapon";
}

std::string normalizeRemotePath(std::string path) {
    const std::size_t first = path.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const std::size_t last = path.find_last_not_of(" \t\r\n");
    path = path.substr(first, last - first + 1);
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    while (!path.empty() && path.back() == '/') path.pop_back();
    std::filesystem::path normalized = std::filesystem::path(path).lexically_normal();
    for (const auto& part : normalized) if (part == "..") return "";
    path = normalized.generic_string();
    while (!path.empty() && path.back() == '/') path.pop_back();
    return path == "." ? "" : path;
}

bool isSafeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    const std::filesystem::path normalized = path.lexically_normal();
    if (normalized.empty() || normalized == ".") return false;
    for (const auto& part : normalized) if (part == "..") return false;
    return true;
}

std::vector<std::string> splitFolders(const std::string& value) {
    std::vector<std::string> folders;
    std::string current;
    for (char c : value) {
        if (c != ';' && c != ',' && c != '\n' && c != '\r') { current += c; continue; }
        const std::string folder = normalizeRemotePath(current);
        if (!folder.empty() && std::find(folders.begin(), folders.end(), folder) == folders.end()) folders.push_back(folder);
        current.clear();
    }
    const std::string folder = normalizeRemotePath(current);
    if (!folder.empty() && std::find(folders.begin(), folders.end(), folder) == folders.end()) folders.push_back(folder);
    return folders;
}

std::vector<std::string> splitPatterns(const std::string& value) {
    std::vector<std::string> patterns;
    std::string current;
    auto append = [&]() {
        const std::size_t first = current.find_first_not_of(" \t");
        const std::size_t last = current.find_last_not_of(" \t");
        if (first != std::string::npos) patterns.push_back(current.substr(first, last - first + 1));
        current.clear();
    };
    for (char character : value) {
        if (character == ';' || character == ',' || character == '\n' || character == '\r') append();
        else current += character;
    }
    append();
    return patterns;
}

std::string encodeGraalFilename(const std::string& value) {
    std::ostringstream encoded;
    for (unsigned char character : value) {
        if (std::isalnum(character)) encoded << static_cast<char>(character);
        else encoded << '%' << std::setw(3) << std::setfill('0') << static_cast<unsigned int>(character);
    }
    return encoded.str();
}

std::string decodeGraalFilename(const std::string& value) {
    std::string decoded;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 3 < value.size() && std::isdigit(static_cast<unsigned char>(value[index + 1])) && std::isdigit(static_cast<unsigned char>(value[index + 2])) && std::isdigit(static_cast<unsigned char>(value[index + 3]))) {
            decoded += static_cast<char>((value[index + 1] - '0') * 100 + (value[index + 2] - '0') * 10 + value[index + 3] - '0');
            index += 3;
        } else decoded += value[index];
    }
    return decoded;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool isBrowsableArchive(const RCFileBrowserEntry& entry) {
    if (!entry.is_directory || entry.size <= 0 || entry.path == nullptr) return false;
    return lowerAscii(std::filesystem::path(normalizeRemotePath(entry.path)).extension().string()) == ".zip";
}

std::string extractWrappedScript(TSyncManager::ScriptType type, const std::string& content) {
    if (type == TSyncManager::ScriptType::Class) return content;
    const std::string startMarker = type == TSyncManager::ScriptType::NPC ? "NPCSCRIPT\n" : "SCRIPT\n";
    const std::string endMarker = type == TSyncManager::ScriptType::NPC ? "\nNPCSCRIPTEND" : "\nSCRIPTEND";
    const std::size_t start = content.find(startMarker);
    if (start == std::string::npos) return content;
    const std::size_t body = start + startMarker.size();
    const std::size_t end = content.find(endMarker, body);
    return content.substr(body, end == std::string::npos ? std::string::npos : end - body);
}

bool readBytes(const std::filesystem::path& path, std::vector<unsigned char>& data) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size < 0 || size > 16 * 1024 * 1024) return false;
    stream.seekg(0);
    data.resize(static_cast<std::size_t>(size));
    return data.empty() || static_cast<bool>(stream.read(reinterpret_cast<char*>(data.data()), size));
}

bool writeBytes(const std::filesystem::path& path, const std::vector<unsigned char>& data) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream || (!data.empty() && !stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size())))) return false;
    stream.close();
#ifndef _WIN32
    std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, error);
#endif
    return static_cast<bool>(stream);
}

bool loadStoreKey(std::array<unsigned char, keySize>& key) {
    const std::filesystem::path directory = std::filesystem::path(g_get_user_config_dir()) / "GScriptRC";
    return RC::Encryption::loadOrCreateKey(directory, "accounts.key", key);
}

bool encryptCredential(const std::string& plaintext, std::vector<unsigned char>& output) {
    std::array<unsigned char, keySize> key = {};
    const bool ok = loadStoreKey(key) && RC::Encryption::encrypt(std::vector<unsigned char>(plaintext.begin(), plaintext.end()), key, credentialMagic, output);
    OPENSSL_cleanse(key.data(), key.size());
    return ok;
}

bool decryptCredential(const std::vector<unsigned char>& input, std::string& plaintext) {
    std::array<unsigned char, keySize> key = {};
    if (!loadStoreKey(key)) return false;
    std::vector<unsigned char> decrypted;
    const bool ok = RC::Encryption::decrypt(input, key, credentialMagic, decrypted);
    OPENSSL_cleanse(key.data(), key.size());
    if (!ok) return false;
    plaintext.assign(reinterpret_cast<const char*>(decrypted.data()), decrypted.size());
    if (!decrypted.empty()) OPENSSL_cleanse(decrypted.data(), decrypted.size());
    return true;
}
}

TSyncManager::TSyncManager(GtkWidget* nextParent, std::filesystem::path nextApplicationDirectory, std::function<void(int, bool)> nextOnProgress) : parent(nextParent), applicationDirectory(std::move(nextApplicationDirectory)), onProgress(std::move(nextOnProgress)) {}

TSyncManager::~TSyncManager() {
    if (timer != 0) g_source_remove(timer);
    if (refreshSource != 0) g_source_remove(refreshSource);
    if (manifestDirty) saveManifest();
    if (connection != nullptr) {
        rc_on_sync_server_data(connection, nullptr, nullptr);
        rc_on_sync_filebrowser_folders(connection, nullptr, nullptr);
        rc_on_sync_filebrowser_files(connection, nullptr, nullptr);
        rc_on_sync_file_received(connection, nullptr, nullptr);
    }
    if (fileRootFolderIcon != nullptr) g_object_unref(fileRootFolderIcon);
    if (fileRootOpenFolderIcon != nullptr) g_object_unref(fileRootOpenFolderIcon);
    if (window != nullptr) gtk_widget_destroy(window);
}

void TSyncManager::setConnection(void* nextConnection, const std::string& nextServerName) {
    if (connection != nullptr && connection != nextConnection) {
        rc_on_sync_server_data(connection, nullptr, nullptr);
        rc_on_sync_filebrowser_folders(connection, nullptr, nullptr);
        rc_on_sync_filebrowser_files(connection, nullptr, nullptr);
        rc_on_sync_file_received(connection, nullptr, nullptr);
    }
    if (serverName != nextServerName) {
        if (timer != 0) { g_source_remove(timer); timer = 0; }
        entries.clear();
        connection = nextConnection;
        serverName = nextServerName;
        enabled = autoStart = autoCommit = autoPush = autoPull = false;
        workspace.clear();
        fileEntries.clear();
        configEntries.clear();
        scriptModifiedTimes.clear();
        pendingFileFolders.clear();
        pendingFileDownloads.clear();
        pendingArchiveFolders.clear();
        archiveProbeFolders.clear();
        pendingScriptMetadataFolders.clear();
        activeScriptMetadataFolder.clear();
        activeScriptMetadataStarted = 0;
        activeFileFolder.clear();
        activeFileDownload.clear();
        activeFileDownloadFolder.clear();
        fileFolders.clear();
        fileExcludes.clear();
        syncScripts = true;
        syncFiles = false;
        syncConfig = false;
        syncDirection = SyncDirection::DownloadOnly;
        allowRemoteDeletes = false;
        oneShotActive = false;
        progressUnseen = false;
        discoveringFileRoots = false;
        discoveringScriptMetadata = false;
        loadConfig();
        loadManifest();
        if (connection != nullptr) {
            rc_on_sync_server_data(connection, onServerData, this);
            rc_on_sync_filebrowser_folders(connection, onFileRoots, this);
            rc_on_sync_filebrowser_files(connection, onFileList, this);
            rc_on_sync_file_received(connection, onFileReceived, this);
        }
        if (autoStart && connection != nullptr) start();
        return;
    }
    connection = nextConnection;
    if (connection != nullptr) {
        rc_on_sync_server_data(connection, onServerData, this);
        rc_on_sync_filebrowser_folders(connection, onFileRoots, this);
        rc_on_sync_filebrowser_files(connection, onFileList, this);
        rc_on_sync_file_received(connection, onFileReceived, this);
    }
    if (autoStart && connection != nullptr && timer == 0) start();
}

std::filesystem::path TSyncManager::configPath() const {
    return std::filesystem::path(g_get_user_config_dir()) / "GScriptRC" / "sync" / (safeName(serverName) + "-" + stableSuffix(serverName) + ".conf");
}

std::filesystem::path TSyncManager::manifestPath() const { return workspace / ".graal-sync" / "manifest.conf"; }
std::filesystem::path TSyncManager::absolutePath(const Entry& entry) const { return workspace / entry.relativePath; }

void TSyncManager::loadConfig() {
    if (serverName.empty()) return;
    GKeyFile* file = g_key_file_new();
    GError* error = nullptr;
    if (g_key_file_load_from_file(file, configPath().string().c_str(), G_KEY_FILE_NONE, &error)) {
        gchar* value = g_key_file_get_string(file, "sync", "workspace", nullptr);
        if (value != nullptr) { workspace = value; g_free(value); }
        enabled = false;
        autoStart = g_key_file_has_key(file, "sync", "autoStart", nullptr) && g_key_file_get_boolean(file, "sync", "autoStart", nullptr);
        autoCommit = g_key_file_get_boolean(file, "git", "autoCommit", nullptr);
        autoPush = g_key_file_get_boolean(file, "git", "autoPush", nullptr);
        autoPull = g_key_file_get_boolean(file, "git", "autoPull", nullptr);
        gchar* remote = g_key_file_get_string(file, "git", "remoteUrl", nullptr);
        if (remote != nullptr) { gitRemoteUrl = remote; g_free(remote); }
        syncScripts = !g_key_file_has_key(file, "sync", "scripts", nullptr) || g_key_file_get_boolean(file, "sync", "scripts", nullptr);
        syncFiles = g_key_file_get_boolean(file, "sync", "files", nullptr);
        syncConfig = g_key_file_get_boolean(file, "sync", "serverConfig", nullptr);
        gint direction = g_key_file_has_key(file, "sync", "direction", nullptr) ? g_key_file_get_integer(file, "sync", "direction", nullptr) : 0;
        if (direction < 0 || direction > 2) direction = 0;
        syncDirection = static_cast<SyncDirection>(direction);
        allowRemoteDeletes = uploadsAllowed() && g_key_file_has_key(file, "sync", "allowRemoteDeletes", nullptr) && g_key_file_get_boolean(file, "sync", "allowRemoteDeletes", nullptr);
        gchar* folders = g_key_file_get_string(file, "sync", "fileFolders", nullptr);
        if (folders != nullptr) { fileFolders = folders; g_free(folders); }
        gchar* excludes = g_key_file_get_string(file, "sync", "excludeFiles", nullptr);
        if (excludes != nullptr) { fileExcludes = excludes; g_free(excludes); }
    }
    if (error != nullptr) g_error_free(error);
    g_key_file_unref(file);
}

void TSyncManager::saveConfig() const {
    if (serverName.empty()) return;
    GKeyFile* file = g_key_file_new();
    g_key_file_set_string(file, "sync", "workspace", workspace.string().c_str());
    g_key_file_set_boolean(file, "sync", "autoStart", autoStart);
    g_key_file_set_boolean(file, "sync", "scripts", syncScripts);
    g_key_file_set_boolean(file, "sync", "files", syncFiles);
    g_key_file_set_boolean(file, "sync", "serverConfig", syncConfig);
    g_key_file_set_integer(file, "sync", "direction", static_cast<gint>(syncDirection));
    g_key_file_set_boolean(file, "sync", "allowRemoteDeletes", uploadsAllowed() && allowRemoteDeletes);
    g_key_file_set_string(file, "sync", "fileFolders", fileFolders.c_str());
    g_key_file_set_string(file, "sync", "excludeFiles", fileExcludes.c_str());
    g_key_file_set_boolean(file, "git", "autoCommit", autoCommit);
    g_key_file_set_boolean(file, "git", "autoPush", autoPush);
    g_key_file_set_boolean(file, "git", "autoPull", autoPull);
    g_key_file_set_string(file, "git", "remoteUrl", gitRemoteUrl.c_str());
    gsize length = 0;
    gchar* data = g_key_file_to_data(file, &length, nullptr);
    std::error_code error;
    std::filesystem::create_directories(configPath().parent_path(), error);
    g_file_set_contents(configPath().string().c_str(), data, static_cast<gssize>(length), nullptr);
    g_free(data);
    g_key_file_unref(file);
}

void TSyncManager::loadManifest() {
    if (workspace.empty()) return;
    GKeyFile* file = g_key_file_new();
    if (!g_key_file_load_from_file(file, manifestPath().string().c_str(), G_KEY_FILE_NONE, nullptr)) { g_key_file_unref(file); return; }
    gsize count = 0;
    gchar** groups = g_key_file_get_groups(file, &count);
    for (gsize index = 0; index < count; ++index) {
        const std::string group = groups[index];
        gchar* typeValue = g_key_file_get_string(file, group.c_str(), "type", nullptr);
        gchar* nameValue = g_key_file_get_string(file, group.c_str(), "name", nullptr);
        gchar* imageValue = g_key_file_get_string(file, group.c_str(), "image", nullptr);
        gchar* npcTypeValue = g_key_file_get_string(file, group.c_str(), "npcType", nullptr);
        gchar* levelValue = g_key_file_get_string(file, group.c_str(), "level", nullptr);
        gchar* pathValue = g_key_file_get_string(file, group.c_str(), "path", nullptr);
        gchar* hashValue = g_key_file_get_string(file, group.c_str(), "localHash", nullptr);
        if (pathValue == nullptr || !isSafeRelativePath(std::filesystem::path(pathValue))) {
            g_free(typeValue);
            g_free(nameValue);
            g_free(imageValue);
            g_free(npcTypeValue);
            g_free(levelValue);
            g_free(pathValue);
            g_free(hashValue);
            continue;
        }
        if (typeValue != nullptr && std::string(typeValue) == "config" && nameValue != nullptr && pathValue != nullptr) {
            ConfigEntry entry;
            entry.type = nameValue;
            entry.relativePath = pathValue;
            if (hashValue != nullptr) {
                try { entry.localHash = static_cast<std::size_t>(std::stoull(hashValue)); } catch (...) { entry.localHash = contentHash(readFile(workspace / entry.relativePath)); }
            }
            configEntries[entry.type] = std::move(entry);
            g_free(typeValue);
            g_free(nameValue);
            g_free(imageValue);
            g_free(npcTypeValue);
            g_free(levelValue);
            g_free(pathValue);
            g_free(hashValue);
            continue;
        }
        if (typeValue != nullptr && std::string(typeValue) == "file" && nameValue != nullptr && pathValue != nullptr) {
            const std::string remotePath = normalizeRemotePath(nameValue);
            if (remotePath.empty()) {
                g_free(typeValue);
                g_free(nameValue);
                g_free(imageValue);
                g_free(npcTypeValue);
                g_free(levelValue);
                g_free(pathValue);
                g_free(hashValue);
                continue;
            }
            FileEntry entry;
            entry.remotePath = remotePath;
            entry.relativePath = pathValue;
            entry.serverSize = g_key_file_get_integer(file, group.c_str(), "size", nullptr);
            entry.serverModified = g_key_file_get_integer(file, group.c_str(), "modified", nullptr);
            entry.localHash = contentHash(readFile(workspace / entry.relativePath));
            fileEntries[entry.remotePath] = std::move(entry);
            g_free(typeValue);
            g_free(nameValue);
            g_free(imageValue);
            g_free(npcTypeValue);
            g_free(levelValue);
            g_free(pathValue);
            g_free(hashValue);
            continue;
        }
        if (typeValue != nullptr && nameValue != nullptr && pathValue != nullptr) {
            Entry entry;
            entry.type = std::string(typeValue) == "npc" ? ScriptType::NPC : std::string(typeValue) == "class" ? ScriptType::Class : ScriptType::Weapon;
            entry.name = nameValue;
            entry.image = imageValue == nullptr ? "" : imageValue;
            entry.npcType = npcTypeValue == nullptr ? "" : npcTypeValue;
            entry.level = levelValue == nullptr ? "" : levelValue;
            entry.id = g_key_file_get_integer(file, group.c_str(), "id", nullptr);
            entry.serverModified = g_key_file_get_integer(file, group.c_str(), "modified", nullptr);
            entry.relativePath = pathValue;
            entry.localHash = contentHash(readFile(absolutePath(entry)));
            entries[keyFor(entry.type, entry.name, entry.id)] = std::move(entry);
        }
        g_free(typeValue);
        g_free(nameValue);
        g_free(imageValue);
        g_free(npcTypeValue);
        g_free(levelValue);
        g_free(pathValue);
        g_free(hashValue);
    }
    g_strfreev(groups);
    g_key_file_unref(file);
}

void TSyncManager::saveManifest() const {
    if (workspace.empty()) return;
    GKeyFile* file = g_key_file_new();
    for (const auto& pair : entries) {
        const Entry& entry = pair.second;
        g_key_file_set_string(file, pair.first.c_str(), "type", scriptTypeName(entry.type));
        g_key_file_set_string(file, pair.first.c_str(), "name", entry.name.c_str());
        g_key_file_set_string(file, pair.first.c_str(), "image", entry.image.c_str());
        g_key_file_set_string(file, pair.first.c_str(), "npcType", entry.npcType.c_str());
        g_key_file_set_string(file, pair.first.c_str(), "level", entry.level.c_str());
        g_key_file_set_integer(file, pair.first.c_str(), "id", entry.id);
        g_key_file_set_integer(file, pair.first.c_str(), "modified", entry.serverModified);
        g_key_file_set_string(file, pair.first.c_str(), "path", entry.relativePath.generic_string().c_str());
    }
    for (const auto& pair : fileEntries) {
        const FileEntry& entry = pair.second;
        const std::string group = "file:" + stableSuffix(entry.remotePath);
        g_key_file_set_string(file, group.c_str(), "type", "file");
        g_key_file_set_string(file, group.c_str(), "name", entry.remotePath.c_str());
        g_key_file_set_string(file, group.c_str(), "path", entry.relativePath.generic_string().c_str());
        g_key_file_set_integer(file, group.c_str(), "size", entry.serverSize);
        g_key_file_set_integer(file, group.c_str(), "modified", entry.serverModified);
    }
    for (const auto& pair : configEntries) {
        const ConfigEntry& entry = pair.second;
        const std::string group = "config:" + entry.type;
        g_key_file_set_string(file, group.c_str(), "type", "config");
        g_key_file_set_string(file, group.c_str(), "name", entry.type.c_str());
        g_key_file_set_string(file, group.c_str(), "path", entry.relativePath.generic_string().c_str());
        g_key_file_set_string(file, group.c_str(), "localHash", std::to_string(entry.localHash).c_str());
    }
    gsize length = 0;
    gchar* data = g_key_file_to_data(file, &length, nullptr);
    std::error_code error;
    std::filesystem::create_directories(manifestPath().parent_path(), error);
    g_file_set_contents(manifestPath().string().c_str(), data, static_cast<gssize>(length), nullptr);
    g_free(data);
    g_key_file_unref(file);
}

void TSyncManager::start() {
    if (connection == nullptr || workspace.empty()) return;
    oneShotActive = false;
    prepareWorkspace();
    enabled = true;
    setPhase("Starting synchronization");
    logEvent("Synchronization started");
    saveConfig();
    requestPull();
    if (timer == 0) timer = g_timeout_add_seconds(1, onTick, this);
}

void TSyncManager::prepareWorkspace() {
    std::error_code error;
    std::filesystem::create_directories(workspace / ".graal-sync", error);
    std::filesystem::create_directories(workspace / "weapons", error);
    std::filesystem::create_directories(workspace / "classes", error);
    std::filesystem::create_directories(workspace / "npcs", error);
    std::filesystem::create_directories(workspace / "files", error);
    std::filesystem::create_directories(workspace / "config", error);
}

void TSyncManager::stop() {
    enabled = false;
    oneShotActive = false;
    progressUnseen = false;
    if (timer != 0) { g_source_remove(timer); timer = 0; }
    if (manifestDirty) { saveManifest(); manifestDirty = false; }
    pendingFileFolders.clear();
    pendingFileDownloads.clear();
    pendingArchiveFolders.clear();
    archiveProbeFolders.clear();
    pendingScriptMetadataFolders.clear();
    activeScriptMetadataFolder.clear();
    activeScriptMetadataStarted = 0;
    activeFileFolder.clear();
    activeFileDownload.clear();
    activeFileDownloadFolder.clear();
    discoveringScriptMetadata = false;
    setPhase("Stopped");
    logEvent("Synchronization stopped");
    saveConfig();
}

void TSyncManager::requestPull() {
    if (connection == nullptr || !rc_is_nc_authenticated(connection)) return;
    progressUnseen = true;
    syncWorkDone = 0;
    syncWorkTotal = 0;
    syncStarted = g_get_monotonic_time();
    completedSyncItems.clear();
    discoveringFileRoots = false;
    discoveringScriptMetadata = false;
    pendingScriptMetadataFolders.clear();
    activeScriptMetadataFolder.clear();
    activeScriptMetadataStarted = 0;
    if (!downloadsAllowed()) {
        setPhase("Watching for local changes");
        nextPull = g_get_monotonic_time() + pullInterval;
        return;
    }
    setPhase("Requesting server scripts");
    if (syncScripts) {
        RCWeapon* weapons = nullptr;
        const int weaponCount = rc_get_weapons(connection, &weapons);
        for (int index = 0; index < weaponCount; ++index) if (weapons[index].name != nullptr) { ++syncWorkTotal; rc_request_weapon_script(connection, weapons[index].name); }
        RCClass* classes = nullptr;
        const int classCount = rc_get_classes(connection, &classes);
        for (int index = 0; index < classCount; ++index) if (classes[index].name != nullptr) { ++syncWorkTotal; rc_request_class_script(connection, classes[index].name); }
        RCNPC* npcs = nullptr;
        const int npcCount = rc_get_npcs(connection, &npcs);
        for (int index = 0; index < npcCount; ++index) {
            ++syncWorkTotal;
            rc_request_npc_script(connection, npcs[index].id);
            rc_get_npc_flags(connection, npcs[index].id);
        }
    }
    if (syncScripts) beginScriptMetadataLookup(); else if (syncFiles) beginFilePull();
    if (syncConfig) {
        syncWorkTotal += 3;
        rc_request_server_options(connection);
        rc_request_server_flags(connection);
        rc_request_folder_config(connection);
        logEvent("Requested Server Options, Server Flags, and Folder Config");
    }
    nextPull = g_get_monotonic_time() + pullInterval;
}

void TSyncManager::refreshCache() {
    if (connection == nullptr || !syncScripts || !downloadsAllowed()) return;
    const int previousDownloads = downloadedChanges;
    RCWeapon* weapons = nullptr;
    const int weaponCount = rc_get_weapons(connection, &weapons);
    for (int index = 0; index < weaponCount; ++index) if (weapons[index].name != nullptr && weapons[index].script != nullptr) addOrUpdateEntry(ScriptType::Weapon, weapons[index].name, 0, weapons[index].image == nullptr ? "" : weapons[index].image, "", "", weapons[index].script);
    RCClass* classes = nullptr;
    const int classCount = rc_get_classes(connection, &classes);
    for (int index = 0; index < classCount; ++index) if (classes[index].name != nullptr && classes[index].script != nullptr) addOrUpdateEntry(ScriptType::Class, classes[index].name, 0, "", "", "", classes[index].script);
    RCNPC* npcs = nullptr;
    const int npcCount = rc_get_npcs(connection, &npcs);
    for (int index = 0; index < npcCount; ++index) if (npcs[index].script != nullptr) addOrUpdateEntry(ScriptType::NPC, npcs[index].name == nullptr ? "" : npcs[index].name, npcs[index].id, npcs[index].image == nullptr ? "" : npcs[index].image, npcs[index].type == nullptr ? "" : npcs[index].type, "", npcs[index].script);
    if (downloadedChanges != previousDownloads) {
        saveManifest();
        if (autoCommit) gitCommitDownloaded();
    }
}

void TSyncManager::addOrUpdateEntry(ScriptType type, const std::string& name, int id, const std::string& image, const std::string& npcType, const std::string& level, const std::string& script) {
    const std::string key = keyFor(type, name, id);
    auto found = entries.find(key);
    const std::string fileName = type == ScriptType::Class ? safeName(name) + ".txt" : type == ScriptType::NPC ? "npc" + encodeGraalFilename(name) + ".txt" : "weapon" + encodeGraalFilename(name) + ".txt";
    const std::filesystem::path desiredPath = (type == ScriptType::NPC ? "npcs" : type == ScriptType::Class ? "classes" : "weapons") / std::filesystem::path(fileName);
    if (found == entries.end()) {
        Entry entry;
        entry.type = type;
        entry.name = name;
        entry.image = image;
        entry.id = id;
        entry.relativePath = desiredPath;
        found = entries.emplace(key, std::move(entry)).first;
    } else if (found->second.relativePath != desiredPath) {
        const std::filesystem::path oldPath = absolutePath(found->second);
        const std::filesystem::path newPath = workspace / desiredPath;
        std::error_code error;
        std::filesystem::create_directories(newPath.parent_path(), error);
        if (std::filesystem::exists(oldPath) && !std::filesystem::exists(newPath)) std::filesystem::rename(oldPath, newPath, error);
        found->second.relativePath = desiredPath;
    }
    found->second.name = name;
    found->second.image = image;
    found->second.npcType = npcType;
    found->second.level = level;
    std::vector<std::string> metadataKeys;
    if (type == ScriptType::Weapon) metadataKeys.push_back("weapons:weapon" + lowerAscii(name));
    if (type == ScriptType::Class) metadataKeys.push_back("scripts:" + lowerAscii(name));
    if (type == ScriptType::NPC) {
        metadataKeys.push_back("npcs:npc" + lowerAscii(name) + "-" + std::to_string(id));
        metadataKeys.push_back("npcs:npc" + lowerAscii(name));
    }
    for (const std::string& metadataKey : metadataKeys) {
        const auto modified = scriptModifiedTimes.find(metadataKey);
        if (modified == scriptModifiedTimes.end()) continue;
        found->second.serverModified = modified->second;
        break;
    }
    syncEntry(found->second, script);
}

void TSyncManager::syncEntry(Entry& entry, const std::string& script) {
    if (completedSyncItems.insert("script:" + keyFor(entry.type, entry.name, entry.id)).second) ++syncWorkDone;
    const std::size_t hash = contentHash(script);
    if (entry.pendingUploadUntil > g_get_monotonic_time()) {
        if (hash != entry.pendingUploadHash) return;
        entry.pendingUploadHash = 0;
        entry.pendingUploadUntil = 0;
    }
    std::string document = script;
    if (entry.type == ScriptType::Weapon) document = "GRAWP001\nREALNAME " + entry.name + "\nIMAGE " + entry.image + "\nSCRIPT\n" + script + "\nSCRIPTEND\n";
    if (entry.type == ScriptType::NPC) {
        document = "GRNPC001\nNAME " + entry.name + "\nID " + std::to_string(entry.id) + "\nTYPE " + (entry.npcType.empty() ? "OBJECT" : entry.npcType) + "\n";
        if (!entry.image.empty()) document += "IMAGE " + entry.image + "\n";
        if (!entry.level.empty()) document += "LEVEL " + entry.level + "\n";
        char* flags = connection == nullptr ? nullptr : rc_get_cached_npc_flags(connection, entry.id);
        if (flags != nullptr) {
            std::istringstream lines(flags);
            std::string line;
            while (std::getline(lines, line)) if (!line.empty()) document += "FLAG " + line + "\n";
            rc_free(flags);
        }
        document += "NPCSCRIPT\n" + script + "\nNPCSCRIPTEND\n";
    }
    if (entry.serverModified > 0 && std::filesystem::exists(absolutePath(entry))) applyModifiedTime(absolutePath(entry), entry.serverModified);
    if (entry.serverHash == hash && entry.localHash == contentHash(document) && std::filesystem::exists(absolutePath(entry))) return;
    const std::string local = readFile(absolutePath(entry));
    const std::size_t localHash = contentHash(local);
    if (std::filesystem::exists(absolutePath(entry)) && entry.localHash != 0 && localHash != entry.localHash) return;
    if (local != document && writeFile(absolutePath(entry), document)) {
        ++downloadedChanges;
        logEvent("Downloaded " + entry.relativePath.generic_string());
    }
    applyModifiedTime(absolutePath(entry), entry.serverModified);
    entry.serverHash = hash;
    entry.localHash = contentHash(document);
}

void TSyncManager::beginScriptMetadataLookup() {
    if (connection == nullptr || !syncScripts || !downloadsAllowed()) { if (syncFiles) beginFilePull(); return; }
    discoveringScriptMetadata = true;
    setPhase("Checking script modification dates");
    if (!rc_sync_filebrowser_start(connection)) {
        discoveringScriptMetadata = false;
        if (syncFiles) beginFilePull();
    }
}

void TSyncManager::requestNextScriptMetadataFolder() {
    if (!activeScriptMetadataFolder.empty() || connection == nullptr) return;
    if (pendingScriptMetadataFolders.empty()) { if (syncFiles) beginFilePull(); return; }
    activeScriptMetadataFolder = pendingScriptMetadataFolders.front();
    pendingScriptMetadataFolders.pop_front();
    activeScriptMetadataStarted = g_get_monotonic_time();
    std::string request = activeScriptMetadataFolder + "/";
    setPhase("Checking " + activeScriptMetadataFolder + " modification dates");
    if (!rc_sync_filebrowser_cd(connection, request.c_str())) {
        activeScriptMetadataFolder.clear();
        activeScriptMetadataStarted = 0;
        requestNextScriptMetadataFolder();
    }
}

void TSyncManager::applyScriptMetadata(const std::string& folder, const void* rawItems, int count) {
    const auto* items = static_cast<const RCFileBrowserEntry*>(rawItems);
    for (int index = 0; index < count; ++index) {
        if (items[index].is_directory || items[index].modified <= 0 || items[index].path == nullptr) continue;
        std::string stem = std::filesystem::path(normalizeRemotePath(items[index].path)).filename().string();
        if (lowerAscii(std::filesystem::path(stem).extension().string()) == ".txt") stem.resize(stem.size() - 4);
        stem = decodeGraalFilename(stem);
        const std::string lowerStem = lowerAscii(stem);
        scriptModifiedTimes[folder + ":" + lowerStem] = items[index].modified;
        for (auto& pair : entries) {
            Entry& entry = pair.second;
            if ((folder == "weapons" && entry.type != ScriptType::Weapon) || (folder == "scripts" && entry.type != ScriptType::Class) || (folder == "npcs" && entry.type != ScriptType::NPC)) continue;
            std::string candidate = lowerStem;
            if (entry.type == ScriptType::Weapon && candidate.rfind("weapon", 0) == 0) candidate.erase(0, 6);
            if (entry.type == ScriptType::NPC && candidate.rfind("npc", 0) == 0) candidate.erase(0, 3);
            const std::string name = lowerAscii(entry.name);
            const bool matches = candidate == name || entry.type == ScriptType::NPC && entry.id > 0 && candidate == name + "-" + std::to_string(entry.id);
            if (!matches) continue;
            entry.serverModified = items[index].modified;
            const std::filesystem::path path = absolutePath(entry);
            if (std::filesystem::exists(path)) applyModifiedTime(path, entry.serverModified);
            manifestDirty = true;
            break;
        }
    }
}

void TSyncManager::scanLocalChanges() {
    if (!uploadsAllowed()) return;
    bool pulled = !autoPull;
    for (auto iterator = entries.begin(); iterator != entries.end();) {
        auto current = iterator++;
        auto& pair = *current;
        Entry& entry = pair.second;
        const std::filesystem::path path = absolutePath(entry);
        if (!std::filesystem::exists(path)) {
            if (allowRemoteDeletes && entry.localHash != 0 && deleteEntry(entry)) {
                ++uploadedChanges;
                setPhase("Deleted " + entry.relativePath.generic_string() + " from server");
                logEvent("Deleted " + entry.relativePath.generic_string() + " from server");
                entries.erase(current);
                manifestDirty = true;
            }
            continue;
        }
        const std::string document = readFile(path);
        const std::size_t documentHash = contentHash(document);
        if (documentHash == entry.localHash) continue;
        const std::string script = extractWrappedScript(entry.type, document);
        const std::size_t scriptHash = contentHash(script);
        if (!pulled) {
            std::string output;
            if (!runGit({"pull", "--ff-only"}, output)) { showResult("Sync Git pull failed", output, GTK_MESSAGE_WARNING); return; }
            pulled = true;
        }
        if (uploadEntry(entry, script)) {
            entry.localHash = documentHash;
            entry.serverHash = scriptHash;
            entry.pendingUploadHash = scriptHash;
            entry.pendingUploadUntil = g_get_monotonic_time() + 10LL * G_USEC_PER_SEC;
            ++uploadedChanges;
            setPhase("Uploaded " + entry.relativePath.generic_string());
            logEvent("Uploaded " + entry.relativePath.generic_string());
        }
    }
    scanLocalFiles();
    scanLocalConfig();
}

void TSyncManager::beginFilePull() {
    if (connection == nullptr || !syncFiles || !downloadsAllowed()) return;
    pendingFileFolders.clear();
    pendingFileDownloads.clear();
    pendingArchiveFolders.clear();
    archiveProbeFolders.clear();
    activeFileFolder.clear();
    activeFileDownload.clear();
    activeFileDownloadFolder.clear();
    const std::vector<std::string> folders = splitFolders(fileFolders);
    std::ostringstream canonicalFolders;
    for (std::size_t index = 0; index < folders.size(); ++index) {
        if (index != 0) canonicalFolders << "; ";
        canonicalFolders << folders[index];
    }
    if (fileFolders != canonicalFolders.str()) {
        fileFolders = canonicalFolders.str();
        saveConfig();
        refreshWindow();
    }
    discoveringFileRoots = true;
    setPhase("Discovering server file roots");
    logEvent("Discovering available server file roots");
    if (!rc_sync_filebrowser_start(connection)) {
        discoveringFileRoots = false;
        for (const std::string& folder : folders) pendingFileFolders.push_back(folder);
        syncWorkTotal += pendingFileFolders.size();
        setPhase("Using saved server file roots");
        logEvent("Unable to refresh server file roots; using saved selection");
        requestNextFileFolder();
    }
    refreshWindow();
}

void TSyncManager::onFileRoots(int count, void* data) {
    auto* manager = static_cast<TSyncManager*>(data);
    if (manager->connection == nullptr) return;
    RCFileBrowserFolder* roots = nullptr;
    const int rootCount = count > 0 ? rc_copy_filebrowser_folders(manager->connection, &roots) : 0;
    std::vector<std::string> folders;
    for (int index = 0; index < rootCount; ++index) {
        std::string pattern = roots[index].pattern == nullptr ? "" : roots[index].pattern;
        std::replace(pattern.begin(), pattern.end(), '\\', '/');
        const std::size_t wildcard = pattern.find_first_of("*?");
        if (wildcard != std::string::npos) pattern.resize(wildcard);
        while (!pattern.empty() && pattern.back() == '/') pattern.pop_back();
        pattern = normalizeRemotePath(pattern);
        if (!pattern.empty() && std::find(folders.begin(), folders.end(), pattern) == folders.end()) folders.push_back(pattern);
    }
    rc_free_filebrowser_folders(roots, rootCount);
    if (manager->discoveringScriptMetadata) {
        manager->discoveringScriptMetadata = false;
        for (const std::string& wanted : {"weapons", "scripts", "npcs"}) {
            const bool readable = std::any_of(folders.begin(), folders.end(), [&](const std::string& root) { return root == wanted || wanted.size() > root.size() && wanted.compare(0, root.size(), root) == 0 && wanted[root.size()] == '/'; });
            if (readable) manager->pendingScriptMetadataFolders.push_back(wanted);
        }
        manager->requestNextScriptMetadataFolder();
        return;
    }
    if (manager->choosingFileRoots) {
        manager->choosingFileRoots = false;
        manager->showFileRoots(folders);
        return;
    }
    manager->discoveringFileRoots = false;
    if (folders.empty()) {
        manager->setPhase("No file roots available");
        manager->logEvent("The server did not advertise any usable file roots");
        manager->refreshWindow();
        return;
    }
    std::vector<std::string> selected = splitFolders(manager->fileFolders);
    if (selected.empty()) selected = folders;
    else {
        for (const std::string& folder : folders) {
            const bool included = std::any_of(selected.begin(), selected.end(), [&](const std::string& root) { return folder == root || folder.size() > root.size() && folder.compare(0, root.size(), root) == 0 && folder[root.size()] == '/'; });
            if (included && std::find(selected.begin(), selected.end(), folder) == selected.end()) selected.push_back(folder);
        }
    }
    std::sort(selected.begin(), selected.end());
    std::ostringstream selectedText;
    for (std::size_t index = 0; index < selected.size(); ++index) {
        if (index != 0) selectedText << "; ";
        selectedText << selected[index];
        if (lowerAscii(std::filesystem::path(selected[index]).extension().string()) == ".zip") manager->pendingArchiveFolders.insert(selected[index]);
        else manager->pendingFileFolders.push_back(selected[index]);
    }
    for (const std::string& archive : manager->pendingArchiveFolders) {
        const std::string parent = normalizeRemotePath(std::filesystem::path(archive).parent_path().generic_string());
        if (parent.empty() || std::find(manager->pendingFileFolders.begin(), manager->pendingFileFolders.end(), parent) != manager->pendingFileFolders.end()) continue;
        manager->pendingFileFolders.push_back(parent);
        manager->archiveProbeFolders.insert(parent);
    }
    manager->syncWorkTotal += manager->pendingFileFolders.size();
    manager->fileFolders = selectedText.str();
    manager->saveConfig();
    manager->logEvent("Queued " + std::to_string(selected.size()) + " selected server file roots");
    manager->requestNextFileFolder();
    manager->refreshWindow();
}

void TSyncManager::requestNextFileFolder() {
    if (!activeFileFolder.empty() || connection == nullptr) return;
    if (pendingFileFolders.empty()) { requestNextFileDownload(); return; }
    activeFileFolder = pendingFileFolders.front();
    pendingFileFolders.pop_front();
    std::string request = activeFileFolder;
    if (!request.empty() && request.back() != '/') request += '/';
    setPhase("Listing " + activeFileFolder);
    logEvent("Listing " + activeFileFolder);
    if (!rc_sync_filebrowser_cd(connection, request.c_str())) {
        ++syncWorkDone;
        activeFileFolder.clear();
        requestNextFileFolder();
    }
}

void TSyncManager::onFileList(const char* folder, int count, void* data) {
    auto* manager = static_cast<TSyncManager*>(data);
    if (manager->connection == nullptr) return;
    const std::string responseFolder = normalizeRemotePath(folder == nullptr ? "" : folder);
    if (!manager->activeScriptMetadataFolder.empty() && responseFolder == normalizeRemotePath(manager->activeScriptMetadataFolder)) {
        RCFileBrowserEntry* items = nullptr;
        const int itemCount = count > 0 ? rc_copy_filebrowser_files(manager->connection, &items) : 0;
        manager->applyScriptMetadata(responseFolder, items, itemCount);
        rc_free_filebrowser_files(items, itemCount);
        manager->activeScriptMetadataFolder.clear();
        manager->activeScriptMetadataStarted = 0;
        manager->requestNextScriptMetadataFolder();
        return;
    }
    if (manager->browsingFileRoots && responseFolder == normalizeRemotePath(manager->filePickerLoadingFolder)) {
        RCFileBrowserEntry* items = nullptr;
        const int itemCount = count > 0 ? rc_copy_filebrowser_files(manager->connection, &items) : 0;
        GtkTreeIter parentIter;
        if (manager->findFileRoot(responseFolder, parentIter)) {
            GtkTreeIter childIter;
            gboolean validChild = gtk_tree_model_iter_children(GTK_TREE_MODEL(manager->fileRootsStore), &childIter, &parentIter);
            while (validChild) {
                gboolean placeholder = false;
                gtk_tree_model_get(GTK_TREE_MODEL(manager->fileRootsStore), &childIter, FileRootPlaceholder, &placeholder, -1);
                validChild = placeholder ? gtk_tree_store_remove(manager->fileRootsStore, &childIter) : gtk_tree_model_iter_next(GTK_TREE_MODEL(manager->fileRootsStore), &childIter);
            }
            std::vector<std::string> added;
            gboolean parentSelected = false;
            gtk_tree_model_get(GTK_TREE_MODEL(manager->fileRootsStore), &parentIter, FileRootSelected, &parentSelected, -1);
            for (int index = 0; index < itemCount; ++index) {
                if (!items[index].is_directory || isBrowsableArchive(items[index])) continue;
                std::string remote = normalizeRemotePath(items[index].path == nullptr ? "" : items[index].path);
                if (remote.empty()) continue;
                const std::string prefix = responseFolder.empty() ? "" : responseFolder + "/";
                if (!prefix.empty() && remote.compare(0, prefix.size(), prefix) != 0) remote = prefix + remote;
                std::string remainder = prefix.empty() ? remote : remote.substr(prefix.size());
                const std::size_t slash = remainder.find('/');
                if (slash != std::string::npos) remainder.resize(slash);
                remote = normalizeRemotePath(prefix + remainder);
                if (remote.empty() || std::find(added.begin(), added.end(), remote) != added.end()) continue;
                added.push_back(remote);
                const std::string label = std::filesystem::path(remote).filename().string();
                GtkTreeIter folderIter;
                bool exists = false;
                if (gtk_tree_model_iter_children(GTK_TREE_MODEL(manager->fileRootsStore), &folderIter, &parentIter)) {
                    do {
                        gchar* existingPath = nullptr;
                        gboolean placeholder = false;
                        gtk_tree_model_get(GTK_TREE_MODEL(manager->fileRootsStore), &folderIter, FileRootPath, &existingPath, FileRootPlaceholder, &placeholder, -1);
                        exists = !placeholder && existingPath != nullptr && normalizeRemotePath(existingPath) == remote;
                        g_free(existingPath);
                        if (exists) break;
                    } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(manager->fileRootsStore), &folderIter));
                }
                if (!exists) {
                    gtk_tree_store_append(manager->fileRootsStore, &folderIter, &parentIter);
                    gtk_tree_store_set(manager->fileRootsStore, &folderIter, FileRootSelected, parentSelected || std::find(manager->filePickerSelected.begin(), manager->filePickerSelected.end(), remote) != manager->filePickerSelected.end(), FileRootPath, remote.c_str(), FileRootLabel, label.c_str(), FileRootIcon, manager->fileRootFolderIcon, FileRootLoaded, false, FileRootPlaceholder, false, -1);
                    GtkTreeIter placeholder;
                    gtk_tree_store_append(manager->fileRootsStore, &placeholder, &folderIter);
                    gtk_tree_store_set(manager->fileRootsStore, &placeholder, FileRootSelected, false, FileRootPath, "", FileRootLabel, "Loading...", FileRootLoaded, true, FileRootPlaceholder, true, -1);
                }
                if (parentSelected) {
                    if (std::find(manager->filePickerSelected.begin(), manager->filePickerSelected.end(), remote) == manager->filePickerSelected.end()) manager->filePickerSelected.push_back(remote);
                }
            }
            gtk_tree_store_set(manager->fileRootsStore, &parentIter, FileRootLoaded, true, -1);
        }
        rc_free_filebrowser_files(items, itemCount);
        manager->filePickerLoadingFolder.clear();
        manager->requestNextFilePickerFolder();
        if (manager->filePickerLoadingFolder.empty()) manager->setPhase("Selecting server folders");
        return;
    }
    if (!manager->activeFileDownload.empty() && !manager->activeFileDownloadFolder.empty() && responseFolder == normalizeRemotePath(manager->activeFileDownloadFolder)) {
        const std::string name = std::filesystem::path(manager->activeFileDownload).filename().string();
        manager->setPhase("Downloading " + manager->activeFileDownload);
        manager->logEvent("Downloading " + manager->activeFileDownload);
        if (!rc_sync_filebrowser_download(manager->connection, name.c_str())) {
            manager->logEvent("Unable to download " + manager->activeFileDownload);
            ++manager->syncWorkDone;
            manager->activeFileDownload.clear();
            manager->activeFileDownloadFolder.clear();
            manager->activeFileDownloadStarted = 0;
            manager->activeFileDownloadAttempts = 0;
            manager->requestNextFileDownload();
        }
        return;
    }
    if (responseFolder != normalizeRemotePath(manager->activeFileFolder)) return;
    RCFileBrowserEntry* items = nullptr;
    const int itemCount = count > 0 ? rc_copy_filebrowser_files(manager->connection, &items) : 0;
    const bool archiveProbeOnly = manager->archiveProbeFolders.erase(responseFolder) != 0;
    std::unordered_set<std::string> archiveFiles;
    for (int index = 0; index < itemCount; ++index) {
        if (items[index].is_directory && !isBrowsableArchive(items[index])) continue;
        std::string remote = normalizeRemotePath(items[index].path == nullptr ? "" : items[index].path);
        const std::string prefix = responseFolder.empty() ? "" : responseFolder + "/";
        if (!prefix.empty() && remote.compare(0, prefix.size(), prefix) != 0) remote = prefix + remote;
        remote = normalizeRemotePath(remote);
        if (!remote.empty() && manager->pendingArchiveFolders.find(remote) != manager->pendingArchiveFolders.end()) archiveFiles.insert(remote);
    }
    for (auto iterator = manager->pendingArchiveFolders.begin(); iterator != manager->pendingArchiveFolders.end();) {
        const std::string parentPath = normalizeRemotePath(std::filesystem::path(*iterator).parent_path().generic_string());
        if (parentPath != responseFolder) { ++iterator; continue; }
        if (archiveFiles.find(*iterator) == archiveFiles.end()) {
            manager->pendingFileFolders.push_back(*iterator);
            ++manager->syncWorkTotal;
        }
        iterator = manager->pendingArchiveFolders.erase(iterator);
    }
    int folderCount = 0;
    int fileCount = 0;
    int queuedCount = 0;
    for (int index = 0; index < itemCount; ++index) {
        std::string remote = normalizeRemotePath(items[index].path == nullptr ? "" : items[index].path);
        if (remote.empty()) continue;
        const std::string prefix = responseFolder.empty() ? "" : responseFolder + "/";
        if (!prefix.empty() && remote.compare(0, prefix.size(), prefix) != 0) remote = prefix + remote;
        remote = normalizeRemotePath(remote);
        if (remote.empty()) continue;
        if (archiveProbeOnly && archiveFiles.find(remote) == archiveFiles.end()) continue;
        if (items[index].is_directory && !isBrowsableArchive(items[index])) {
            ++folderCount;
            if (manager->pendingFileFolders.size() < 5000 && std::find(manager->pendingFileFolders.begin(), manager->pendingFileFolders.end(), remote) == manager->pendingFileFolders.end()) {
                manager->pendingFileFolders.push_back(remote);
                ++manager->syncWorkTotal;
            }
            continue;
        }
        ++fileCount;
        if (manager->isFileExcluded(remote)) continue;
        FileEntry& entry = manager->fileEntries[remote];
        if (entry.remotePath.empty()) {
            entry.remotePath = remote;
            entry.relativePath = std::filesystem::path("files") / std::filesystem::path(remote);
            entry.localHash = contentHash(readFile(manager->workspace / entry.relativePath));
        }
        const bool changed = entry.serverSize != items[index].size || entry.serverModified != items[index].modified || !std::filesystem::exists(manager->workspace / entry.relativePath);
        entry.serverSize = items[index].size;
        entry.serverModified = items[index].modified;
        if (entry.pendingUploadUntil > g_get_monotonic_time()) continue;
        if (changed && manager->pendingFileDownloads.size() < 10000 && std::find(manager->pendingFileDownloads.begin(), manager->pendingFileDownloads.end(), remote) == manager->pendingFileDownloads.end()) {
            manager->pendingFileDownloads.push_back(remote);
            ++queuedCount;
            ++manager->syncWorkTotal;
        }
    }
    rc_free_filebrowser_files(items, itemCount);
    ++manager->syncWorkDone;
    manager->activeFileFolder.clear();
    manager->manifestDirty = true;
    if (!archiveProbeOnly && folderCount == 0 && fileCount == 0) {
        std::vector<std::string> selected = splitFolders(manager->fileFolders);
        const auto empty = std::find(selected.begin(), selected.end(), responseFolder);
        if (empty != selected.end()) {
            selected.erase(empty);
            std::ostringstream value;
            for (std::size_t index = 0; index < selected.size(); ++index) {
                if (index != 0) value << "; ";
                value << selected[index];
            }
            manager->fileFolders = value.str();
            manager->saveConfig();
            manager->logEvent("Ignored empty folder " + responseFolder);
        }
    }
    manager->logEvent("Listed " + responseFolder + ": " + std::to_string(folderCount) + " folders, " + std::to_string(fileCount) + " files, " + std::to_string(queuedCount) + " queued");
    if (!manager->pendingFileDownloads.empty()) manager->requestNextFileDownload(); else manager->requestNextFileFolder();
}

void TSyncManager::requestNextFileDownload() {
    if (!activeFileDownload.empty() || connection == nullptr) return;
    if (pendingFileDownloads.empty()) { setPhase("Watching for changes"); return; }
    activeFileDownload = pendingFileDownloads.front();
    pendingFileDownloads.pop_front();
    activeFileDownloadFolder.clear();
    activeFileDownloadStarted = g_get_monotonic_time();
    activeFileDownloadAttempts = 1;
    activeFileDownloadReceived = 0;
    activeFileDownloadTotal = 0;
    setPhase("Downloading " + activeFileDownload);
    logEvent("Downloading " + activeFileDownload);
    activeFileDownloadFolder = normalizeRemotePath(std::filesystem::path(activeFileDownload).parent_path().generic_string());
    if (!activeFileDownloadFolder.empty()) {
        std::string request = activeFileDownloadFolder;
        if (request.back() != '/') request += '/';
        if (rc_sync_filebrowser_cd(connection, request.c_str())) return;
    } else if (rc_sync_filebrowser_download(connection, std::filesystem::path(activeFileDownload).filename().string().c_str())) return;
    logEvent("Unable to download " + activeFileDownload);
    ++syncWorkDone;
    activeFileDownload.clear();
    activeFileDownloadStarted = 0;
    activeFileDownloadAttempts = 0;
    if (!pendingFileDownloads.empty()) requestNextFileDownload(); else requestNextFileFolder();
}

void TSyncManager::onFileReceived(const char* path, const void* content, int length, void* data) {
    auto* manager = static_cast<TSyncManager*>(data);
    if (path == nullptr || length < 0 || (content == nullptr && length != 0) || manager->activeFileDownload.empty()) return;
    const void* safeContent = content == nullptr ? static_cast<const void*>("") : content;
    const std::string received = normalizeRemotePath(path);
    const std::string expected = normalizeRemotePath(manager->activeFileDownload);
    const bool matches = received == expected || expected.ends_with("/" + received) || received.ends_with("/" + expected);
    if (!matches) return;
    auto found = manager->fileEntries.find(expected);
    if (found != manager->fileEntries.end()) {
        FileEntry& entry = found->second;
        const std::filesystem::path localPath = manager->workspace / entry.relativePath;
        const std::string local = readFile(localPath);
        const std::size_t currentHash = contentHash(local);
        const std::string server(static_cast<const char*>(safeContent), static_cast<std::size_t>(length));
        if (entry.localHash == 0 || currentHash == entry.localHash) {
            if (local != server && writeFile(localPath, server)) ++manager->downloadedChanges;
            applyModifiedTime(localPath, entry.serverModified);
            entry.localHash = contentHash(server);
        }
    }
    manager->activeFileDownload.clear();
    manager->activeFileDownloadFolder.clear();
    manager->activeFileDownloadStarted = 0;
    manager->activeFileDownloadAttempts = 0;
    manager->activeFileDownloadReceived = 0;
    manager->activeFileDownloadTotal = 0;
    ++manager->syncWorkDone;
    manager->logEvent("Downloaded " + expected);
    manager->manifestDirty = true;
    if (!manager->pendingFileDownloads.empty()) manager->requestNextFileDownload(); else manager->requestNextFileFolder();
    manager->scheduleWindowRefresh();
}

void TSyncManager::onServerData(const char* type, const char* content, void* data) {
    auto* manager = static_cast<TSyncManager*>(data);
    if (!manager->syncConfig || !manager->downloadsAllowed() || type == nullptr || content == nullptr) return;
    manager->syncConfigEntry(type, content);
}

void TSyncManager::syncConfigEntry(const std::string& type, const std::string& content) {
    std::string fileName;
    if (type == "options") fileName = "serveroptions.txt";
    else if (type == "flags") fileName = "serverflags.txt";
    else if (type == "folder_config") fileName = "folderconfig.txt";
    else return;
    if (completedSyncItems.insert("config:" + type).second) ++syncWorkDone;
    ConfigEntry& entry = configEntries[type];
    entry.type = type;
    entry.relativePath = std::filesystem::path("config") / fileName;
    const std::size_t hash = contentHash(content);
    if (entry.pendingUploadUntil > g_get_monotonic_time()) {
        if (hash != entry.pendingUploadHash) return;
        entry.pendingUploadHash = 0;
        entry.pendingUploadUntil = 0;
    }
    const std::filesystem::path path = workspace / entry.relativePath;
    if (entry.serverHash == hash && entry.localHash == hash && std::filesystem::exists(path)) return;
    const std::string local = readFile(path);
    const std::size_t localHash = contentHash(local);
    if (std::filesystem::exists(path) && entry.localHash != 0 && localHash != entry.localHash) return;
    if (local != content && writeFile(path, content)) {
        ++downloadedChanges;
        logEvent("Downloaded " + entry.relativePath.generic_string());
    }
    entry.localHash = hash;
    entry.serverHash = hash;
    saveManifest();
    refreshWindow();
}

void TSyncManager::scanLocalFiles() {
    if (!syncFiles || connection == nullptr) return;
    for (auto iterator = fileEntries.begin(); iterator != fileEntries.end();) {
        auto current = iterator++;
        auto& pair = *current;
        FileEntry& entry = pair.second;
        if (isFileExcluded(entry.remotePath)) continue;
        const std::filesystem::path path = workspace / entry.relativePath;
        if (!std::filesystem::exists(path)) {
            if (allowRemoteDeletes && entry.localHash != 0 && rc_filebrowser_delete(connection, entry.remotePath.c_str())) {
                ++uploadedChanges;
                setPhase("Deleted " + entry.remotePath + " from server");
                logEvent("Deleted " + entry.remotePath + " from server");
                fileEntries.erase(current);
                manifestDirty = true;
            }
            continue;
        }
        const std::string content = readFile(path);
        const std::size_t hash = contentHash(content);
        if (hash == entry.localHash) continue;
        if (!rc_upload_file(connection, entry.remotePath.c_str(), content.data(), static_cast<int>(content.size()))) continue;
        entry.localHash = hash;
        entry.pendingUploadHash = hash;
        entry.pendingUploadUntil = g_get_monotonic_time() + 10LL * G_USEC_PER_SEC;
        ++uploadedChanges;
        setPhase("Uploaded " + entry.remotePath);
        logEvent("Uploaded " + entry.remotePath);
    }
}

bool TSyncManager::isFileExcluded(const std::string& path) const {
    std::string normalized = normalizeRemotePath(path);
    std::string name = std::filesystem::path(normalized).filename().string();
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    for (std::string pattern : splitPatterns(fileExcludes)) {
        std::transform(pattern.begin(), pattern.end(), pattern.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (!pattern.empty() && pattern.front() == '.') pattern.insert(pattern.begin(), '*');
        if (g_pattern_match_simple(pattern.c_str(), normalized.c_str()) || g_pattern_match_simple(pattern.c_str(), name.c_str())) return true;
    }
    return false;
}

void TSyncManager::scanLocalConfig() {
    if (!syncConfig || connection == nullptr) return;
    for (auto& pair : configEntries) {
        ConfigEntry& entry = pair.second;
        const std::filesystem::path path = workspace / entry.relativePath;
        if (!std::filesystem::exists(path)) continue;
        const std::string content = readFile(path);
        const std::size_t hash = contentHash(content);
        if (hash == entry.localHash) continue;
        int result = 0;
        if (entry.type == "options") result = rc_upload_server_options(connection, content.c_str());
        else if (entry.type == "flags") result = rc_upload_server_flags(connection, content.c_str());
        else if (entry.type == "folder_config") result = rc_upload_folder_config(connection, content.c_str());
        if (!result) continue;
        entry.localHash = hash;
        entry.serverHash = hash;
        entry.pendingUploadHash = hash;
        entry.pendingUploadUntil = g_get_monotonic_time() + 10LL * G_USEC_PER_SEC;
        ++uploadedChanges;
        setPhase("Uploaded " + entry.relativePath.generic_string());
        logEvent("Uploaded " + entry.relativePath.generic_string());
    }
    saveManifest();
}

bool TSyncManager::uploadEntry(Entry& entry, const std::string& script) {
    if (connection == nullptr || !rc_is_nc_authenticated(connection)) return false;
    if (entry.type == ScriptType::Class) return rc_update_class(connection, entry.name.c_str(), script.c_str()) != 0;
    if (entry.type == ScriptType::NPC) return rc_update_npc(connection, entry.id, script.c_str()) != 0;
    return rc_update_weapon(connection, entry.name.c_str(), entry.image.c_str(), script.c_str()) != 0;
}

bool TSyncManager::deleteEntry(const Entry& entry) {
    if (connection == nullptr || !rc_is_nc_authenticated(connection)) return false;
    if (entry.type == ScriptType::Class) return rc_delete_class(connection, entry.name.c_str()) != 0;
    if (entry.type == ScriptType::NPC) return rc_delete_npc(connection, entry.id) != 0;
    return rc_delete_weapon(connection, entry.name.c_str()) != 0;
}

bool TSyncManager::downloadsAllowed() const { return syncDirection != SyncDirection::UploadOnly; }
bool TSyncManager::uploadsAllowed() const { return syncDirection != SyncDirection::DownloadOnly; }
bool TSyncManager::isFileSyncActive() const { return syncFiles && (enabled || oneShotActive); }

int TSyncManager::progressPercent() const {
    if (!enabled && !oneShotActive) return 0;
    if (syncWorkTotal == 0) return 100;
    return static_cast<int>(std::min<std::size_t>(100, syncWorkDone * 100 / syncWorkTotal));
}
std::string TSyncManager::currentPhaseText() const {
    if (activeFileDownload.empty() || activeFileDownloadTotal <= 0) return phase;
    const long long percent = std::clamp(activeFileDownloadReceived * 100 / activeFileDownloadTotal, 0LL, 100LL);
    std::string eta;
    if (activeFileDownloadReceived > 0 && activeFileDownloadStarted > 0) {
        const gint64 elapsed = std::max<gint64>(1, (g_get_monotonic_time() - activeFileDownloadStarted) / G_USEC_PER_SEC);
        eta = ", ETA " + formatEta(elapsed * (activeFileDownloadTotal - activeFileDownloadReceived) / activeFileDownloadReceived);
    }
    return phase + " (" + std::to_string(percent) + "%" + eta + ")";
}

gboolean TSyncManager::onTick(gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    if (!manager->enabled && !manager->oneShotActive) { manager->timer = 0; return G_SOURCE_REMOVE; }
    const gint64 now = g_get_monotonic_time();
    if (!manager->activeScriptMetadataFolder.empty() && manager->activeScriptMetadataStarted > 0 && now - manager->activeScriptMetadataStarted >= 3LL * G_USEC_PER_SEC) {
        manager->activeScriptMetadataFolder.clear();
        manager->activeScriptMetadataStarted = 0;
        manager->requestNextScriptMetadataFolder();
    }
    if (!manager->activeFileDownload.empty()) {
        long long received = 0;
        long long total = 0;
        if (rc_filebrowser_transfer_progress(manager->connection, manager->activeFileDownload.c_str(), &received, &total)) {
            manager->activeFileDownloadReceived = received;
            manager->activeFileDownloadTotal = total;
        }
    }
    gint64 downloadTimeout = 30LL * G_USEC_PER_SEC;
    const auto activeEntry = manager->fileEntries.find(manager->activeFileDownload);
    if (activeEntry != manager->fileEntries.end() && activeEntry->second.serverSize > 0) downloadTimeout += std::min<gint64>(270LL * G_USEC_PER_SEC, (static_cast<gint64>(activeEntry->second.serverSize) / (128 * 1024) + 1) * G_USEC_PER_SEC);
    if (!manager->activeFileDownload.empty() && manager->activeFileDownloadStarted > 0 && now - manager->activeFileDownloadStarted >= downloadTimeout) {
        if (manager->activeFileDownloadAttempts < 3) {
            ++manager->activeFileDownloadAttempts;
            manager->activeFileDownloadStarted = now;
            manager->logEvent("Retrying download " + manager->activeFileDownload + " (" + std::to_string(manager->activeFileDownloadAttempts) + "/3)");
            const std::string name = std::filesystem::path(manager->activeFileDownload).filename().string();
            if (!rc_sync_filebrowser_download(manager->connection, name.c_str())) manager->activeFileDownloadStarted = now - downloadTimeout;
        } else {
            manager->logEvent("Skipped unresponsive download " + manager->activeFileDownload);
            ++manager->syncWorkDone;
            manager->activeFileDownload.clear();
            manager->activeFileDownloadStarted = 0;
            manager->activeFileDownloadAttempts = 0;
            manager->activeFileDownloadReceived = 0;
            manager->activeFileDownloadTotal = 0;
            if (!manager->pendingFileDownloads.empty()) manager->requestNextFileDownload(); else manager->requestNextFileFolder();
        }
    }
    manager->refreshCache();
    manager->scanLocalChanges();
    if (manager->manifestDirty) { manager->saveManifest(); manager->manifestDirty = false; }
    const bool workPending = manager->discoveringFileRoots || manager->discoveringScriptMetadata || !manager->activeScriptMetadataFolder.empty() || !manager->pendingScriptMetadataFolders.empty() || !manager->activeFileFolder.empty() || !manager->activeFileDownload.empty() || !manager->pendingFileFolders.empty() || !manager->pendingFileDownloads.empty() || manager->syncWorkDone < manager->syncWorkTotal;
    if (manager->oneShotActive && !workPending) {
        manager->oneShotActive = false;
        manager->setPhase("Sync complete");
        if (manager->manifestDirty) { manager->saveManifest(); manager->manifestDirty = false; }
        manager->refreshWindow();
        manager->timer = 0;
        return G_SOURCE_REMOVE;
    }
    if (manager->enabled && g_get_monotonic_time() >= manager->nextPull) manager->requestPull();
    manager->refreshWindow();
    return G_SOURCE_CONTINUE;
}

std::string TSyncManager::statusText() const {
    std::ostringstream status;
    status << "State: ";
    if (!enabled && !oneShotActive) status << "Stopped";
    else if (connection == nullptr || !rc_is_nc_authenticated(connection)) status << "Waiting for NPC-Server";
    else status << "Running";
    status << "\nCurrent: " << currentPhaseText();
    if (syncWorkTotal > 0) {
        status << "\nOverall: " << progressPercent() << "%";
        if (syncWorkDone > 0 && syncStarted > 0 && syncWorkDone < syncWorkTotal) {
            const gint64 elapsed = std::max<gint64>(1, (g_get_monotonic_time() - syncStarted) / G_USEC_PER_SEC);
            status << " (ETA " << formatEta(elapsed * static_cast<gint64>(syncWorkTotal - syncWorkDone) / static_cast<gint64>(syncWorkDone)) << ")";
        }
    }
    status << "\nQueue: " << pendingFileFolders.size() << " folders, " << pendingFileDownloads.size() << " downloads";
    if (browsingFileRoots) status << ", " << pendingFilePickerFolders.size() << " folder views";
    status << "\nTracked: " << entries.size() << " scripts, " << fileEntries.size() << " files, " << configEntries.size() << " configuration files";
    status << "\nTransfers: " << downloadedChanges << " downloaded, " << uploadedChanges << " uploaded";
    const char* direction = syncDirection == SyncDirection::DownloadOnly ? "download only" : syncDirection == SyncDirection::UploadOnly ? "upload only" : "two-way";
    status << "\nModes: scripts " << (syncScripts ? "on" : "off") << ", files " << (syncFiles ? "on" : "off") << ", configuration " << (syncConfig ? "on" : "off") << ", direction " << direction << ", remote deletes " << (allowRemoteDeletes ? "on" : "off") << ", auto-start " << (autoStart ? "on" : "off");
    if (!activity.empty()) status << "\nLast: " << activity.back();
    return status.str();
}

void TSyncManager::setPhase(const std::string& value) {
    phase = value;
    scheduleWindowRefresh();
}

void TSyncManager::logEvent(const std::string& value) {
    const std::time_t now = std::time(nullptr);
    std::tm local = {};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream line;
    line << std::put_time(&local, "%H:%M:%S") << "  " << value;
    activity.push_back(line.str());
    ++activitySerial;
    while (activity.size() > 300) { activity.pop_front(); ++activityDiscarded; }
    scheduleWindowRefresh();
}

void TSyncManager::createWindow() {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_object_add_weak_pointer(G_OBJECT(window), reinterpret_cast<gpointer*>(&window));
    gtk_window_set_title(GTK_WINDOW(window), ("Sync & Git Backups (Alpha) - " + serverName).c_str());
    gtk_window_set_default_size(GTK_WINDOW(window), 560, 500);
    gtk_window_set_modal(GTK_WINDOW(window), false);
    g_signal_connect(window, "delete-event", G_CALLBACK(onWindowDelete), this);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(root), 10);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);
    GtkWidget* setupBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(setupBox), 8);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), setupBox, gtk_label_new("Setup"));
    GtkWidget* statusFrame = gtk_frame_new("Sync status");
    gtk_box_pack_start(GTK_BOX(setupBox), statusFrame, false, false, 0);
    statusLabel = gtk_label_new("");
    gtk_widget_set_halign(statusLabel, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(statusLabel), true);
    gtk_widget_set_margin_start(statusLabel, 10);
    gtk_widget_set_margin_end(statusLabel, 10);
    gtk_widget_set_margin_top(statusLabel, 8);
    gtk_widget_set_margin_bottom(statusLabel, 8);
    gtk_container_add(GTK_CONTAINER(statusFrame), statusLabel);
    GtkWidget* workspaceFrame = gtk_frame_new("Workspace");
    gtk_box_pack_start(GTK_BOX(setupBox), workspaceFrame, false, false, 0);
    GtkWidget* workspaceRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(workspaceRow), 8);
    gtk_container_add(GTK_CONTAINER(workspaceFrame), workspaceRow);
    workspaceField = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(workspaceField), false);
    gtk_widget_set_hexpand(workspaceField, true);
    gtk_box_pack_start(GTK_BOX(workspaceRow), workspaceField, true, true, 0);
    GtkWidget* chooseButton = gtk_button_new_with_label("Choose…");
    GtkWidget* openButton = gtk_button_new_with_label("Open");
    g_signal_connect(chooseButton, "clicked", G_CALLBACK(onChooseWorkspace), this);
    g_signal_connect(openButton, "clicked", G_CALLBACK(onOpenWorkspace), this);
    gtk_box_pack_start(GTK_BOX(workspaceRow), chooseButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(workspaceRow), openButton, false, false, 0);
    GtkWidget* syncFrame = gtk_frame_new("Server synchronization");
    gtk_box_pack_start(GTK_BOX(setupBox), syncFrame, false, false, 0);
    GtkWidget* syncBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(syncBox), 8);
    gtk_container_add(GTK_CONTAINER(syncFrame), syncBox);
    GtkWidget* syncDescription = gtk_label_new("Synchronizes scripts, selected server files, and optional server configuration in the selected direction.");
    gtk_label_set_line_wrap(GTK_LABEL(syncDescription), true);
    gtk_widget_set_halign(syncDescription, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(syncBox), syncDescription, false, false, 0);
    GtkWidget* syncModes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    syncScriptsCheck = gtk_check_button_new_with_label("Script sync");
    syncFilesCheck = gtk_check_button_new_with_label("File sync");
    syncConfigCheck = gtk_check_button_new_with_label("Server configuration");
    g_signal_connect(syncScriptsCheck, "toggled", G_CALLBACK(onSyncScripts), this);
    g_signal_connect(syncFilesCheck, "toggled", G_CALLBACK(onSyncFiles), this);
    g_signal_connect(syncConfigCheck, "toggled", G_CALLBACK(onSyncConfig), this);
    gtk_box_pack_start(GTK_BOX(syncModes), syncScriptsCheck, false, false, 0);
    gtk_box_pack_start(GTK_BOX(syncModes), syncFilesCheck, false, false, 0);
    gtk_box_pack_start(GTK_BOX(syncModes), syncConfigCheck, false, false, 0);
    gtk_box_pack_start(GTK_BOX(syncBox), syncModes, false, false, 0);
    GtkWidget* mutationModes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(mutationModes), gtk_label_new("Direction:"), false, false, 0);
    syncDirectionCombo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(syncDirectionCombo), "Download only");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(syncDirectionCombo), "Upload only");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(syncDirectionCombo), "Two-way");
    allowRemoteDeletesCheck = gtk_check_button_new_with_label("Allow local deletions to delete server content");
    gtk_widget_set_tooltip_text(syncDirectionCombo, "Download only is the safe default. Upload and two-way modes can change server content.");
    gtk_widget_set_tooltip_text(allowRemoteDeletesCheck, "Destructive. A tracked local file deletion also deletes the matching server item.");
    g_signal_connect(syncDirectionCombo, "changed", G_CALLBACK(onSyncDirectionChanged), this);
    g_signal_connect(allowRemoteDeletesCheck, "toggled", G_CALLBACK(onAllowRemoteDeletes), this);
    gtk_box_pack_start(GTK_BOX(mutationModes), syncDirectionCombo, false, false, 0);
    gtk_box_pack_start(GTK_BOX(mutationModes), allowRemoteDeletesCheck, false, false, 0);
    gtk_box_pack_start(GTK_BOX(syncBox), mutationModes, false, false, 0);
    autoStartCheck = gtk_check_button_new_with_label("Start synchronization automatically when connected");
    g_signal_connect(autoStartCheck, "toggled", G_CALLBACK(onAutoStart), this);
    gtk_box_pack_start(GTK_BOX(syncBox), autoStartCheck, false, false, 0);
    GtkWidget* fileFoldersRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* fileFoldersLabel = gtk_label_new("Remote file folders:");
    fileFoldersField = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(fileFoldersField), false);
    gtk_entry_set_placeholder_text(GTK_ENTRY(fileFoldersField), "Leave empty to discover server roots");
    gtk_widget_set_hexpand(fileFoldersField, true);
    g_signal_connect(fileFoldersField, "changed", G_CALLBACK(onFileFoldersChanged), this);
    GtkWidget* chooseFoldersButton = gtk_button_new_with_label("Choose...");
    g_signal_connect(chooseFoldersButton, "clicked", G_CALLBACK(onChooseFileFolders), this);
    gtk_box_pack_start(GTK_BOX(fileFoldersRow), fileFoldersLabel, false, false, 0);
    gtk_box_pack_start(GTK_BOX(fileFoldersRow), fileFoldersField, true, true, 0);
    gtk_box_pack_start(GTK_BOX(fileFoldersRow), chooseFoldersButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(syncBox), fileFoldersRow, false, false, 0);
    GtkWidget* fileExcludesRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(fileExcludesRow), gtk_label_new("Exclude files:"), false, false, 0);
    fileExcludesField = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(fileExcludesField), "*.tmp; *.bak; filename.ext");
    gtk_widget_set_hexpand(fileExcludesField, true);
    g_signal_connect(fileExcludesField, "changed", G_CALLBACK(onFileExcludesChanged), this);
    gtk_box_pack_start(GTK_BOX(fileExcludesRow), fileExcludesField, true, true, 0);
    gtk_box_pack_start(GTK_BOX(syncBox), fileExcludesRow, false, false, 0);
    GtkWidget* syncButtons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    startButton = gtk_button_new_with_label("Start");
    stopButton = gtk_button_new_with_label("Stop");
    pullButton = gtk_button_new_with_label("Sync now");
    g_signal_connect(startButton, "clicked", G_CALLBACK(onStart), this);
    g_signal_connect(stopButton, "clicked", G_CALLBACK(onStop), this);
    g_signal_connect(pullButton, "clicked", G_CALLBACK(onPull), this);
    gtk_box_pack_start(GTK_BOX(syncButtons), startButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(syncButtons), stopButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(syncButtons), pullButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(syncBox), syncButtons, false, false, 0);
    GtkWidget* gitPage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(gitPage), 8);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), gitPage, gtk_label_new("Git"));
    GtkWidget* gitFrame = gtk_frame_new("Git backups");
    gtk_box_pack_start(GTK_BOX(gitPage), gitFrame, false, false, 0);
    GtkWidget* gitBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(gitBox), 8);
    gtk_container_add(GTK_CONTAINER(gitFrame), gitBox);
    GtkWidget* remoteRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(remoteRow), gtk_label_new("Upstream URL:"), false, false, 0);
    gitRemoteField = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(gitRemoteField), "git@github.com:owner/repository.git");
    gtk_widget_set_hexpand(gitRemoteField, true);
    g_signal_connect(gitRemoteField, "changed", G_CALLBACK(onGitRemoteChanged), this);
    gtk_box_pack_start(GTK_BOX(remoteRow), gitRemoteField, true, true, 0);
    gtk_box_pack_start(GTK_BOX(gitBox), remoteRow, false, false, 0);
    GtkWidget* gitButtons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    const std::pair<const char*, GCallback> gitActions[] = {{"Initialize", G_CALLBACK(onGitInitialize)}, {"Commit", G_CALLBACK(onGitCommit)}, {"Pull", G_CALLBACK(onGitPull)}, {"Push", G_CALLBACK(onGitPush)}};
    for (const auto& action : gitActions) {
        GtkWidget* button = gtk_button_new_with_label(action.first);
        g_signal_connect(button, "clicked", action.second, this);
        gtk_box_pack_start(GTK_BOX(gitButtons), button, false, false, 0);
    }
    gtk_box_pack_start(GTK_BOX(gitBox), gitButtons, false, false, 0);
    autoCommitCheck = gtk_check_button_new_with_label("Commit server downloads automatically");
    autoPushCheck = gtk_check_button_new_with_label("Push after automatic commits");
    autoPullCheck = gtk_check_button_new_with_label("Pull before local uploads");
    g_signal_connect(autoCommitCheck, "toggled", G_CALLBACK(onAutoCommit), this);
    g_signal_connect(autoPushCheck, "toggled", G_CALLBACK(onAutoPush), this);
    g_signal_connect(autoPullCheck, "toggled", G_CALLBACK(onAutoPull), this);
    gtk_box_pack_start(GTK_BOX(gitBox), autoCommitCheck, false, false, 0);
    gtk_box_pack_start(GTK_BOX(gitBox), autoPushCheck, false, false, 0);
    gtk_box_pack_start(GTK_BOX(gitBox), autoPullCheck, false, false, 0);
    GtkWidget* activityBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(activityBox), 8);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), activityBox, gtk_label_new("Activity"));
    phaseLabel = gtk_label_new("");
    queueLabel = gtk_label_new("");
    totalsLabel = gtk_label_new("");
    gtk_widget_set_halign(phaseLabel, GTK_ALIGN_START);
    gtk_widget_set_halign(queueLabel, GTK_ALIGN_START);
    gtk_widget_set_halign(totalsLabel, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(activityBox), phaseLabel, false, false, 0);
    gtk_box_pack_start(GTK_BOX(activityBox), queueLabel, false, false, 0);
    gtk_box_pack_start(GTK_BOX(activityBox), totalsLabel, false, false, 0);
    GtkWidget* activityFrame = gtk_frame_new("Activity log");
    gtk_widget_set_vexpand(activityFrame, true);
    GtkWidget* activityScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(activityScroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(activityScroll, true);
    activityView = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(activityView), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(activityView), false);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(activityView), GTK_WRAP_WORD_CHAR);
    gtk_container_set_border_width(GTK_CONTAINER(activityView), 6);
    GtkTextBuffer* activityBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(activityView));
    gtk_text_buffer_create_tag(activityBuffer, "activity-time", "foreground", "#7f849c", nullptr);
    gtk_text_buffer_create_tag(activityBuffer, "activity-download", "foreground", "#4da3ff", nullptr);
    gtk_text_buffer_create_tag(activityBuffer, "activity-upload", "foreground", "#3fb950", nullptr);
    gtk_text_buffer_create_tag(activityBuffer, "activity-queue", "foreground", "#d29922", nullptr);
    gtk_text_buffer_create_tag(activityBuffer, "activity-error", "foreground", "#f85149", nullptr);
    gtk_text_buffer_create_tag(activityBuffer, "activity-lifecycle", "foreground", "#a371f7", nullptr);
    gtk_container_add(GTK_CONTAINER(activityScroll), activityView);
    gtk_container_add(GTK_CONTAINER(activityFrame), activityScroll);
    gtk_box_pack_start(GTK_BOX(activityBox), activityFrame, true, true, 0);
    GtkWidget* credentialsBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(credentialsBox), 12);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), credentialsBox, gtk_label_new("Credentials"));
    GtkWidget* credentialDescription = gtk_label_new("Optional SSH private key for this Git upstream. It is encrypted with AES-256-GCM in per-user GScriptRC data and decrypted only while Git runs.");
    gtk_label_set_line_wrap(GTK_LABEL(credentialDescription), true);
    gtk_widget_set_halign(credentialDescription, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(credentialsBox), credentialDescription, false, false, 0);
    gitKeyLabel = gtk_label_new("");
    gtk_widget_set_halign(gitKeyLabel, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(credentialsBox), gitKeyLabel, false, false, 0);
    GtkWidget* credentialButtons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* importKeyButton = gtk_button_new_with_label("Import SSH key");
    GtkWidget* clearKeyButton = gtk_button_new_with_label("Remove stored key");
    g_signal_connect(importKeyButton, "clicked", G_CALLBACK(onImportGitKey), this);
    g_signal_connect(clearKeyButton, "clicked", G_CALLBACK(onClearGitKey), this);
    gtk_box_pack_start(GTK_BOX(credentialButtons), importKeyButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(credentialButtons), clearKeyButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(credentialsBox), credentialButtons, false, false, 0);
    GtkWidget* tokenLabel = gtk_label_new("GitHub/Git HTTPS personal access token:");
    gtk_widget_set_halign(tokenLabel, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(credentialsBox), tokenLabel, false, false, 0);
    gitTokenField = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(gitTokenField), false);
    gtk_entry_set_input_purpose(GTK_ENTRY(gitTokenField), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_entry_set_placeholder_text(GTK_ENTRY(gitTokenField), "Encrypted token is stored separately from the upstream URL");
    gtk_box_pack_start(GTK_BOX(credentialsBox), gitTokenField, false, false, 0);
    GtkWidget* tokenButtons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* saveTokenButton = gtk_button_new_with_label("Save encrypted token");
    GtkWidget* clearTokenButton = gtk_button_new_with_label("Remove stored token");
    g_signal_connect(saveTokenButton, "clicked", G_CALLBACK(onSaveGitToken), this);
    g_signal_connect(clearTokenButton, "clicked", G_CALLBACK(onClearGitToken), this);
    gtk_box_pack_start(GTK_BOX(tokenButtons), saveTokenButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(tokenButtons), clearTokenButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(credentialsBox), tokenButtons, false, false, 0);
    GtkWidget* closeButton = gtk_button_new_with_label("Close");
    g_signal_connect_swapped(closeButton, "clicked", G_CALLBACK(gtk_widget_hide), window);
    gtk_widget_set_halign(closeButton, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(root), closeButton, false, false, 0);
    refreshWindow();
}

void TSyncManager::showWindow() {
    if (window == nullptr) createWindow();
    if (progressUnseen && progressPercent() >= 100) progressUnseen = false;
    gtk_window_set_title(GTK_WINDOW(window), ("Sync & Git Backups (Alpha) - " + serverName).c_str());
    refreshWindow();
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

void TSyncManager::refreshWindow() {
    const int progress = progressPercent();
    if (progressUnseen && progress >= 100 && window != nullptr && gtk_widget_get_visible(window)) progressUnseen = false;
    if (onProgress) onProgress(progress, progressUnseen);
    if (window == nullptr) return;
    const std::string title = "Sync & Git Backups (Alpha) - " + serverName + (enabled || oneShotActive ? " (" + std::to_string(progressPercent()) + "%)" : "");
    gtk_window_set_title(GTK_WINDOW(window), title.c_str());
    gtk_label_set_text(GTK_LABEL(statusLabel), statusText().c_str());
    gtk_label_set_text(GTK_LABEL(phaseLabel), ("Current: " + currentPhaseText()).c_str());
    gtk_label_set_text(GTK_LABEL(queueLabel), ("Queued: " + std::to_string(pendingFileFolders.size()) + " folders, " + std::to_string(pendingFileDownloads.size()) + " downloads").c_str());
    gtk_label_set_text(GTK_LABEL(totalsLabel), ("Tracked: " + std::to_string(entries.size()) + " scripts, " + std::to_string(fileEntries.size()) + " files, " + std::to_string(configEntries.size()) + " configuration files | Downloaded: " + std::to_string(downloadedChanges) + " | Uploaded: " + std::to_string(uploadedChanges)).c_str());
    GtkTextBuffer* activityBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(activityView));
    if (renderedActivitySerial < activityDiscarded || renderedActivitySerial > activitySerial) {
        gtk_text_buffer_set_text(activityBuffer, "", 0);
        renderedActivitySerial = activityDiscarded;
        renderedActivityDiscarded = activityDiscarded;
    } else if (renderedActivityDiscarded < activityDiscarded) {
        GtkTextIter activityStart;
        GtkTextIter retainedStart;
        gtk_text_buffer_get_start_iter(activityBuffer, &activityStart);
        retainedStart = activityStart;
        gtk_text_iter_forward_lines(&retainedStart, static_cast<gint>(activityDiscarded - renderedActivityDiscarded));
        gtk_text_buffer_delete(activityBuffer, &activityStart, &retainedStart);
        renderedActivityDiscarded = activityDiscarded;
    }
    GtkTextIter activityEnd;
    gtk_text_buffer_get_end_iter(activityBuffer, &activityEnd);
    const std::size_t firstNewActivity = renderedActivitySerial - activityDiscarded;
    for (std::size_t index = firstNewActivity; index < activity.size(); ++index) {
        const std::string& line = activity[index];
        const std::size_t timestampLength = std::min<std::size_t>(8, line.size());
        gtk_text_buffer_insert_with_tags_by_name(activityBuffer, &activityEnd, line.data(), static_cast<gint>(timestampLength), "activity-time", nullptr);
        const std::string message = line.substr(timestampLength);
        std::string lowered = message;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        const char* tag = nullptr;
        if (lowered.find("failed") != std::string::npos || lowered.find("unable") != std::string::npos || lowered.find("error") != std::string::npos) tag = "activity-error";
        else if (lowered.find("downloaded") != std::string::npos) tag = "activity-download";
        else if (lowered.find("uploaded") != std::string::npos) tag = "activity-upload";
        else if (lowered.find("queued") != std::string::npos || lowered.find("listing") != std::string::npos || lowered.find("request") != std::string::npos || lowered.find("selected") != std::string::npos || lowered.find("discovered") != std::string::npos) tag = "activity-queue";
        else if (lowered.find("started") != std::string::npos || lowered.find("stopped") != std::string::npos || lowered.find("watching") != std::string::npos) tag = "activity-lifecycle";
        if (tag == nullptr) gtk_text_buffer_insert(activityBuffer, &activityEnd, message.c_str(), -1);
        else gtk_text_buffer_insert_with_tags_by_name(activityBuffer, &activityEnd, message.c_str(), -1, tag, nullptr);
        gtk_text_buffer_insert(activityBuffer, &activityEnd, "\n", 1);
    }
    renderedActivitySerial = activitySerial;
    GtkTextMark* activityEndMark = gtk_text_buffer_get_mark(activityBuffer, "activity-end");
    if (activityEndMark == nullptr) activityEndMark = gtk_text_buffer_create_mark(activityBuffer, "activity-end", &activityEnd, false);
    else gtk_text_buffer_move_mark(activityBuffer, activityEndMark, &activityEnd);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(activityView), activityEndMark, 0.0, false, 0.0, 1.0);
    gtk_entry_set_text(GTK_ENTRY(workspaceField), workspace.empty() ? "No workspace selected" : workspace.string().c_str());
    gtk_widget_set_sensitive(startButton, !enabled && !oneShotActive && connection != nullptr && !workspace.empty());
    gtk_widget_set_sensitive(stopButton, enabled || oneShotActive);
    gtk_widget_set_sensitive(pullButton, connection != nullptr && !workspace.empty());
    g_signal_handlers_block_by_func(syncScriptsCheck, reinterpret_cast<gpointer>(onSyncScripts), this);
    g_signal_handlers_block_by_func(syncFilesCheck, reinterpret_cast<gpointer>(onSyncFiles), this);
    g_signal_handlers_block_by_func(syncConfigCheck, reinterpret_cast<gpointer>(onSyncConfig), this);
    g_signal_handlers_block_by_func(syncDirectionCombo, reinterpret_cast<gpointer>(onSyncDirectionChanged), this);
    g_signal_handlers_block_by_func(allowRemoteDeletesCheck, reinterpret_cast<gpointer>(onAllowRemoteDeletes), this);
    g_signal_handlers_block_by_func(autoStartCheck, reinterpret_cast<gpointer>(onAutoStart), this);
    g_signal_handlers_block_by_func(fileFoldersField, reinterpret_cast<gpointer>(onFileFoldersChanged), this);
    g_signal_handlers_block_by_func(fileExcludesField, reinterpret_cast<gpointer>(onFileExcludesChanged), this);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(syncScriptsCheck), syncScripts);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(syncFilesCheck), syncFiles);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(syncConfigCheck), syncConfig);
    gtk_combo_box_set_active(GTK_COMBO_BOX(syncDirectionCombo), static_cast<gint>(syncDirection));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(allowRemoteDeletesCheck), uploadsAllowed() && allowRemoteDeletes);
    gtk_widget_set_sensitive(allowRemoteDeletesCheck, uploadsAllowed());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autoStartCheck), autoStart);
    gtk_entry_set_text(GTK_ENTRY(fileFoldersField), fileFolders.c_str());
    gtk_entry_set_text(GTK_ENTRY(fileExcludesField), fileExcludes.c_str());
    gtk_widget_set_sensitive(fileFoldersField, syncFiles);
    gtk_widget_set_sensitive(fileExcludesField, syncFiles);
    g_signal_handlers_unblock_by_func(syncScriptsCheck, reinterpret_cast<gpointer>(onSyncScripts), this);
    g_signal_handlers_unblock_by_func(syncFilesCheck, reinterpret_cast<gpointer>(onSyncFiles), this);
    g_signal_handlers_unblock_by_func(syncConfigCheck, reinterpret_cast<gpointer>(onSyncConfig), this);
    g_signal_handlers_unblock_by_func(syncDirectionCombo, reinterpret_cast<gpointer>(onSyncDirectionChanged), this);
    g_signal_handlers_unblock_by_func(allowRemoteDeletesCheck, reinterpret_cast<gpointer>(onAllowRemoteDeletes), this);
    g_signal_handlers_unblock_by_func(autoStartCheck, reinterpret_cast<gpointer>(onAutoStart), this);
    g_signal_handlers_unblock_by_func(fileFoldersField, reinterpret_cast<gpointer>(onFileFoldersChanged), this);
    g_signal_handlers_unblock_by_func(fileExcludesField, reinterpret_cast<gpointer>(onFileExcludesChanged), this);
    g_signal_handlers_block_by_func(gitRemoteField, reinterpret_cast<gpointer>(onGitRemoteChanged), this);
    gtk_entry_set_text(GTK_ENTRY(gitRemoteField), gitRemoteUrl.c_str());
    g_signal_handlers_unblock_by_func(gitRemoteField, reinterpret_cast<gpointer>(onGitRemoteChanged), this);
    auto credentialPath = configPath();
    credentialPath.replace_extension(".credentials");
    gtk_label_set_text(GTK_LABEL(gitKeyLabel), std::filesystem::exists(credentialPath) ? "Encrypted SSH key stored" : "No SSH key stored");
    auto tokenPath = configPath();
    tokenPath.replace_extension(".token");
    gtk_entry_set_placeholder_text(GTK_ENTRY(gitTokenField), std::filesystem::exists(tokenPath) ? "Encrypted token stored" : "No token stored");
    g_signal_handlers_block_by_func(autoCommitCheck, reinterpret_cast<gpointer>(onAutoCommit), this);
    g_signal_handlers_block_by_func(autoPushCheck, reinterpret_cast<gpointer>(onAutoPush), this);
    g_signal_handlers_block_by_func(autoPullCheck, reinterpret_cast<gpointer>(onAutoPull), this);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autoCommitCheck), autoCommit);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autoPushCheck), autoPush);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autoPullCheck), autoPull);
    g_signal_handlers_unblock_by_func(autoCommitCheck, reinterpret_cast<gpointer>(onAutoCommit), this);
    g_signal_handlers_unblock_by_func(autoPushCheck, reinterpret_cast<gpointer>(onAutoPush), this);
    g_signal_handlers_unblock_by_func(autoPullCheck, reinterpret_cast<gpointer>(onAutoPull), this);
}

void TSyncManager::scheduleWindowRefresh() {
    if (refreshSource != 0) return;
    refreshSource = g_timeout_add(100, +[](gpointer data) -> gboolean {
        auto* manager = static_cast<TSyncManager*>(data);
        manager->refreshSource = 0;
        manager->refreshWindow();
        return G_SOURCE_REMOVE;
    }, this);
}

gboolean TSyncManager::onWindowDelete(GtkWidget* widget, GdkEvent*, gpointer) {
    gtk_widget_hide(widget);
    return true;
}

void TSyncManager::onChooseWorkspace(GtkButton*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    GtkWidget* chooser = gtk_file_chooser_dialog_new("Choose script sync workspace", nullptr, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Close", GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_modal(GTK_WINDOW(chooser), false);
    if (!manager->workspace.empty()) gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), manager->workspace.string().c_str());
    g_signal_connect(chooser, "response", G_CALLBACK(onWorkspaceChosen), manager);
    gtk_widget_show(chooser);
}

void TSyncManager::onWorkspaceChosen(GtkDialog* dialog, gint response, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    if (response == GTK_RESPONSE_ACCEPT) {
        gchar* selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (selected != nullptr) {
            manager->stop();
            manager->workspace = selected;
            manager->entries.clear();
            manager->fileEntries.clear();
            manager->loadManifest();
            manager->saveConfig();
            manager->refreshWindow();
            g_free(selected);
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void TSyncManager::onStart(GtkButton*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    if (manager->workspace.empty()) { onChooseWorkspace(nullptr, manager); return; }
    manager->start();
    manager->refreshWindow();
}

void TSyncManager::onStop(GtkButton*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    manager->stop();
    manager->refreshWindow();
}
void TSyncManager::onPull(GtkButton*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    if (manager->workspace.empty()) { onChooseWorkspace(nullptr, manager); return; }
    manager->prepareWorkspace();
    if (!manager->enabled) {
        manager->oneShotActive = true;
        manager->logEvent("One-shot synchronization started");
        if (manager->timer == 0) manager->timer = g_timeout_add_seconds(1, onTick, manager);
    }
    manager->requestPull();
    manager->refreshCache();
    manager->refreshWindow();
}

void TSyncManager::onOpenWorkspace(GtkButton*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    GError* error = nullptr;
    gchar* uri = g_filename_to_uri(manager->workspace.string().c_str(), nullptr, &error);
    if (uri != nullptr && g_app_info_launch_default_for_uri(uri, nullptr, &error)) { g_free(uri); return; }
    manager->showResult("Unable to open workspace", error == nullptr ? "No file manager is available." : error->message, GTK_MESSAGE_WARNING);
    g_free(uri);
    if (error != nullptr) g_error_free(error);
}

bool TSyncManager::runGit(const std::vector<std::string>& arguments, std::string& output) const {
    if (workspace.empty()) { output = "Choose a sync workspace first."; return false; }
    std::filesystem::path gitExecutable;
    if (gchar* found = g_find_program_in_path("git")) { gitExecutable = found; g_free(found); }
#ifdef _WIN32
    if (gitExecutable.empty()) {
        const std::vector<std::filesystem::path> candidates = {
            applicationDirectory / "git.exe",
            applicationDirectory / "tools" / "git" / "cmd" / "git.exe",
            std::filesystem::path(g_getenv("ProgramFiles") == nullptr ? "" : g_getenv("ProgramFiles")) / "Git" / "cmd" / "git.exe",
            std::filesystem::path(g_getenv("ProgramFiles(x86)") == nullptr ? "" : g_getenv("ProgramFiles(x86)")) / "Git" / "cmd" / "git.exe",
            std::filesystem::path(g_getenv("LOCALAPPDATA") == nullptr ? "" : g_getenv("LOCALAPPDATA")) / "Programs" / "Git" / "cmd" / "git.exe"
        };
        for (const auto& candidate : candidates) if (!candidate.empty() && std::filesystem::is_regular_file(candidate)) { gitExecutable = candidate; break; }
    }
#else
    if (gitExecutable.empty() && std::filesystem::is_regular_file("/usr/bin/git")) gitExecutable = "/usr/bin/git";
    if (gitExecutable.empty() && std::filesystem::is_regular_file("/usr/local/bin/git")) gitExecutable = "/usr/local/bin/git";
#endif
    if (gitExecutable.empty()) { output = "Git executable was not found."; return false; }
    std::vector<std::string> storage{gitExecutable.string(), "-C", workspace.string()};
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<gchar*> argv;
    for (std::string& value : storage) argv.push_back(value.data());
    argv.push_back(nullptr);
    gchar* standardOutput = nullptr;
    gchar* standardError = nullptr;
    gint status = 0;
    GError* error = nullptr;
    std::string privateKey;
    std::string token;
    std::filesystem::path temporaryKey;
    std::filesystem::path askPassPath;
    gchar** environment = g_get_environ();
    if (loadGitKey(privateKey)) {
        temporaryKey = workspace / ".graal-sync" / ".git-ssh-key";
        if (!writeFile(temporaryKey, privateKey)) { OPENSSL_cleanse(privateKey.data(), privateKey.size()); output = "Unable to prepare the stored SSH key."; return false; }
        std::error_code permissionError;
#ifndef _WIN32
        std::filesystem::permissions(temporaryKey, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, permissionError);
#endif
        const std::string command = "ssh -i \"" + temporaryKey.string() + "\" -o IdentitiesOnly=yes";
        environment = g_environ_setenv(environment, "GIT_SSH_COMMAND", command.c_str(), true);
        OPENSSL_cleanse(privateKey.data(), privateKey.size());
    }
    if (loadGitToken(token)) {
#ifdef _WIN32
        askPassPath = workspace / ".graal-sync" / ".git-askpass.cmd";
        const std::string helper = "@echo off\r\nset \"prompt=%~1\"\r\nif /I \"%prompt:~0,8%\"==\"Username\" (echo x-access-token) else (echo %GSCRIPTRC_GIT_TOKEN%)\r\n";
#else
        askPassPath = workspace / ".graal-sync" / ".git-askpass.sh";
        const std::string helper = "#!/bin/sh\ncase \"$1\" in *Username*) printf '%s\\n' x-access-token;; *) printf '%s\\n' \"$GSCRIPTRC_GIT_TOKEN\";; esac\n";
#endif
        if (!writeFile(askPassPath, helper)) { OPENSSL_cleanse(token.data(), token.size()); g_strfreev(environment); output = "Unable to prepare Git token authentication."; return false; }
#ifndef _WIN32
        std::error_code helperPermissionError;
        std::filesystem::permissions(askPassPath, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::owner_exec, std::filesystem::perm_options::replace, helperPermissionError);
#endif
        environment = g_environ_setenv(environment, "GIT_ASKPASS", askPassPath.string().c_str(), true);
        environment = g_environ_setenv(environment, "GIT_TERMINAL_PROMPT", "0", true);
        environment = g_environ_setenv(environment, "GSCRIPTRC_GIT_TOKEN", token.c_str(), true);
    }
    const gboolean launched = g_spawn_sync(nullptr, argv.data(), environment, static_cast<GSpawnFlags>(0), nullptr, nullptr, &standardOutput, &standardError, &status, &error);
    g_strfreev(environment);
    if (!token.empty()) OPENSSL_cleanse(token.data(), token.size());
    if (!temporaryKey.empty()) {
        std::error_code removeError;
        std::filesystem::remove(temporaryKey, removeError);
    }
    if (!askPassPath.empty()) {
        std::error_code removeError;
        std::filesystem::remove(askPassPath, removeError);
    }
    output = standardOutput == nullptr ? "" : standardOutput;
    if (standardError != nullptr && *standardError != '\0') { if (!output.empty()) output += "\n"; output += standardError; }
    if (error != nullptr) { if (output.empty()) output = error->message; g_error_free(error); }
    g_free(standardOutput);
    g_free(standardError);
    return launched && g_spawn_check_wait_status(status, nullptr);
}

bool TSyncManager::configureGitRemote(std::string& output) const {
    if (gitRemoteUrl.empty()) return true;
    const std::size_t scheme = gitRemoteUrl.find("://");
    if (scheme != std::string::npos) {
        const std::size_t at = gitRemoteUrl.find('@', scheme + 3);
        const std::size_t slash = gitRemoteUrl.find('/', scheme + 3);
        if (at != std::string::npos && (slash == std::string::npos || at < slash)) { output = "Credentials must not be embedded in the upstream URL."; return false; }
    }
    std::string current;
    if (runGit({"remote", "get-url", "origin"}, current)) return runGit({"remote", "set-url", "origin", gitRemoteUrl}, output);
    return runGit({"remote", "add", "origin", gitRemoteUrl}, output);
}

bool TSyncManager::storeGitKey(const std::string& content) {
    if (content.empty() || content.find("PRIVATE KEY") == std::string::npos) return false;
    std::vector<unsigned char> encrypted;
    if (!encryptCredential(content, encrypted)) return false;
    auto path = configPath();
    path.replace_extension(".credentials");
    const bool ok = writeBytes(path, encrypted);
    if (!encrypted.empty()) OPENSSL_cleanse(encrypted.data(), encrypted.size());
    return ok;
}

bool TSyncManager::loadGitKey(std::string& content) const {
    auto path = configPath();
    path.replace_extension(".credentials");
    std::vector<unsigned char> encrypted;
    if (!readBytes(path, encrypted)) return false;
    const bool ok = decryptCredential(encrypted, content);
    if (!encrypted.empty()) OPENSSL_cleanse(encrypted.data(), encrypted.size());
    return ok;
}

void TSyncManager::clearGitKey() {
    auto path = configPath();
    path.replace_extension(".credentials");
    std::error_code error;
    std::filesystem::remove(path, error);
}

bool TSyncManager::storeGitToken(const std::string& content) {
    if (content.empty()) return false;
    std::vector<unsigned char> encrypted;
    if (!encryptCredential(content, encrypted)) return false;
    auto path = configPath();
    path.replace_extension(".token");
    const bool ok = writeBytes(path, encrypted);
    if (!encrypted.empty()) OPENSSL_cleanse(encrypted.data(), encrypted.size());
    return ok;
}

bool TSyncManager::loadGitToken(std::string& content) const {
    auto path = configPath();
    path.replace_extension(".token");
    std::vector<unsigned char> encrypted;
    if (!readBytes(path, encrypted)) return false;
    const bool ok = decryptCredential(encrypted, content);
    if (!encrypted.empty()) OPENSSL_cleanse(encrypted.data(), encrypted.size());
    return ok;
}

void TSyncManager::clearGitToken() {
    auto path = configPath();
    path.replace_extension(".token");
    std::error_code error;
    std::filesystem::remove(path, error);
}

void TSyncManager::gitCommitDownloaded() {
    std::string output;
    if (!runGit({"add", "--", "weapons", "classes", "npcs", "files", "config", ".graal-sync/manifest.conf"}, output)) return;
    if (!runGit({"commit", "-m", "[graal-sync] " + serverName}, output)) return;
    if (autoPush && configureGitRemote(output)) runGit(gitRemoteUrl.empty() ? std::vector<std::string>{"push"} : std::vector<std::string>{"push", "-u", "origin", "HEAD"}, output);
}

void TSyncManager::onGitInitialize(GtkButton*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    std::string output;
    bool ok = manager->runGit({"init"}, output);
    if (ok) ok = manager->configureGitRemote(output);
    manager->logEvent(ok ? "Git repository initialized" : "Git initialization failed");
    manager->showResult(ok ? "Git repository initialized" : "Git initialization failed", output.empty() ? manager->workspace.string() : output, ok ? GTK_MESSAGE_INFO : GTK_MESSAGE_WARNING);
}

void TSyncManager::onGitCommit(GtkButton*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    std::string output;
    bool ok = manager->runGit({"add", "--", "weapons", "classes", "npcs", "files", "config", ".graal-sync/manifest.conf"}, output);
    if (ok) ok = manager->runGit({"commit", "-m", "[graal-sync] " + manager->serverName}, output);
    manager->showResult(ok ? "Sync changes committed" : "Git commit failed", output, ok ? GTK_MESSAGE_INFO : GTK_MESSAGE_WARNING);
}

void TSyncManager::onGitPull(GtkButton*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    std::string output;
    bool ok = manager->configureGitRemote(output);
    if (ok) ok = manager->runGit({"pull", "--ff-only"}, output);
    manager->logEvent(ok ? "Git pull complete" : "Git pull failed");
    manager->showResult(ok ? "Git pull complete" : "Git pull failed", output, ok ? GTK_MESSAGE_INFO : GTK_MESSAGE_WARNING);
}

void TSyncManager::onGitPush(GtkButton*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    std::string output;
    bool ok = manager->configureGitRemote(output);
    if (ok) ok = manager->runGit(manager->gitRemoteUrl.empty() ? std::vector<std::string>{"push"} : std::vector<std::string>{"push", "-u", "origin", "HEAD"}, output);
    manager->logEvent(ok ? "Git push complete" : "Git push failed");
    manager->showResult(ok ? "Git push complete" : "Git push failed", output, ok ? GTK_MESSAGE_INFO : GTK_MESSAGE_WARNING);
}

void TSyncManager::onAutoCommit(GtkToggleButton* item, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    manager->autoCommit = gtk_toggle_button_get_active(item);
    manager->saveConfig();
}

void TSyncManager::onAutoPush(GtkToggleButton* item, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    manager->autoPush = gtk_toggle_button_get_active(item);
    manager->saveConfig();
}

void TSyncManager::onAutoPull(GtkToggleButton* item, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    manager->autoPull = gtk_toggle_button_get_active(item);
    manager->saveConfig();
}

void TSyncManager::onAutoStart(GtkToggleButton* item, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    manager->autoStart = gtk_toggle_button_get_active(item);
    manager->saveConfig();
}

void TSyncManager::onGitRemoteChanged(GtkEditable* editable, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    manager->gitRemoteUrl = gtk_entry_get_text(GTK_ENTRY(editable));
    manager->saveConfig();
}

void TSyncManager::onImportGitKey(GtkButton*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    GtkWidget* chooser = gtk_file_chooser_dialog_new("Import SSH private key", nullptr, GTK_FILE_CHOOSER_ACTION_OPEN, "_Close", GTK_RESPONSE_CANCEL, "_Import", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_modal(GTK_WINDOW(chooser), false);
    g_signal_connect(chooser, "response", G_CALLBACK(onGitKeyChosen), manager);
    gtk_widget_show(chooser);
}

void TSyncManager::onGitKeyChosen(GtkDialog* dialog, gint response, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    if (response == GTK_RESPONSE_ACCEPT) {
        gchar* selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (selected != nullptr) {
            std::string key = readFile(selected);
            const bool ok = manager->storeGitKey(key);
            if (!key.empty()) OPENSSL_cleanse(key.data(), key.size());
            manager->logEvent(ok ? "Encrypted SSH key imported" : "SSH key import failed");
            if (!ok) manager->showResult("SSH key import failed", "The selected file is not a supported private key or could not be encrypted.", GTK_MESSAGE_WARNING);
            g_free(selected);
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
    manager->refreshWindow();
}

void TSyncManager::onClearGitKey(GtkButton*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    manager->clearGitKey();
    manager->logEvent("Stored SSH key removed");
    manager->refreshWindow();
}

void TSyncManager::onSaveGitToken(GtkButton*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    std::string token = gtk_entry_get_text(GTK_ENTRY(manager->gitTokenField));
    const bool ok = manager->storeGitToken(token);
    if (!token.empty()) OPENSSL_cleanse(token.data(), token.size());
    gtk_entry_set_text(GTK_ENTRY(manager->gitTokenField), "");
    manager->logEvent(ok ? "Encrypted Git token saved" : "Git token save failed");
    if (!ok) manager->showResult("Git token save failed", "Enter a token before saving.", GTK_MESSAGE_WARNING);
    manager->refreshWindow();
}

void TSyncManager::onClearGitToken(GtkButton*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    manager->clearGitToken();
    gtk_entry_set_text(GTK_ENTRY(manager->gitTokenField), "");
    manager->logEvent("Stored Git token removed");
    manager->refreshWindow();
}

void TSyncManager::onSyncScripts(GtkToggleButton* item, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    manager->syncScripts = gtk_toggle_button_get_active(item);
    manager->saveConfig();
}

void TSyncManager::onSyncFiles(GtkToggleButton* item, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    manager->syncFiles = gtk_toggle_button_get_active(item);
    manager->saveConfig();
    manager->refreshWindow();
    if (manager->enabled && manager->syncFiles) manager->beginFilePull();
}

void TSyncManager::onSyncConfig(GtkToggleButton* item, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    manager->syncConfig = gtk_toggle_button_get_active(item);
    manager->saveConfig();
    if (manager->syncConfig && manager->downloadsAllowed() && manager->connection != nullptr) {
        std::error_code error;
        if (!manager->workspace.empty()) std::filesystem::create_directories(manager->workspace / "config", error);
        rc_request_server_options(manager->connection);
        rc_request_server_flags(manager->connection);
        rc_request_folder_config(manager->connection);
        manager->logEvent("Requested Server Options, Server Flags, and Folder Config");
    }
}

void TSyncManager::onSyncDirectionChanged(GtkComboBox* item, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    const gint active = gtk_combo_box_get_active(item);
    manager->syncDirection = active >= 0 && active <= 2 ? static_cast<SyncDirection>(active) : SyncDirection::DownloadOnly;
    if (!manager->uploadsAllowed()) manager->allowRemoteDeletes = false;
    manager->syncWorkDone = 0;
    manager->syncWorkTotal = 0;
    manager->saveConfig();
    manager->refreshWindow();
    if (manager->enabled && manager->downloadsAllowed()) manager->requestPull();
}

void TSyncManager::onAllowRemoteDeletes(GtkToggleButton* item, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    manager->allowRemoteDeletes = manager->uploadsAllowed() && gtk_toggle_button_get_active(item);
    manager->saveConfig();
}

void TSyncManager::onFileFoldersChanged(GtkEditable* editable, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    manager->fileFolders = gtk_entry_get_text(GTK_ENTRY(editable));
    manager->saveConfig();
}

void TSyncManager::onFileExcludesChanged(GtkEditable* editable, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    manager->fileExcludes = gtk_entry_get_text(GTK_ENTRY(editable));
    manager->saveConfig();
}

void TSyncManager::onChooseFileFolders(GtkButton*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    if (manager->connection == nullptr) { manager->showResult("File folders unavailable", "Connect to a list server first.", GTK_MESSAGE_WARNING); return; }
    manager->pendingFileFolders.clear();
    manager->pendingFileDownloads.clear();
    manager->pendingArchiveFolders.clear();
    manager->archiveProbeFolders.clear();
    manager->activeFileFolder.clear();
    manager->activeFileDownload.clear();
    manager->activeFileDownloadFolder.clear();
    manager->parentVisibleBeforeFilePicker = manager->parent != nullptr && gtk_widget_get_visible(manager->parent);
    manager->choosingFileRoots = true;
    manager->setPhase("Loading available file roots");
    manager->logEvent("Requesting selectable server file roots");
    if (!rc_sync_filebrowser_start(manager->connection)) {
        manager->choosingFileRoots = false;
        manager->setPhase("File-root request failed");
        manager->showResult("File folders unavailable", "The server did not accept the file-root request.", GTK_MESSAGE_WARNING);
    }
}

void TSyncManager::showFileRoots(const std::vector<std::string>& roots) {
    if (fileRootsDialog != nullptr) { fileRootsStore = nullptr; gtk_widget_destroy(fileRootsDialog); }
    fileRootsDialog = gtk_dialog_new_with_buttons("Select server folders", nullptr, static_cast<GtkDialogFlags>(0), "_Close", GTK_RESPONSE_CLOSE, "_Apply", GTK_RESPONSE_APPLY, nullptr);
    g_object_add_weak_pointer(G_OBJECT(fileRootsDialog), reinterpret_cast<gpointer*>(&fileRootsDialog));
    gtk_window_set_modal(GTK_WINDOW(fileRootsDialog), false);
    gtk_window_set_default_size(GTK_WINDOW(fileRootsDialog), 430, 380);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(fileRootsDialog));
    GtkWidget* description = gtk_label_new("Expand the server folder tree and choose folders to synchronize.");
    gtk_widget_set_halign(description, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), description, false, false, 8);
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, true);
    if (fileRootFolderIcon == nullptr) fileRootFolderIcon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), "folder", 16, GTK_ICON_LOOKUP_FORCE_SIZE, nullptr);
    if (fileRootOpenFolderIcon == nullptr) fileRootOpenFolderIcon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), "folder-open", 16, GTK_ICON_LOOKUP_FORCE_SIZE, nullptr);
    fileRootsStore = gtk_tree_store_new(FileRootColumnCount, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(fileRootsStore));
    g_object_unref(fileRootsStore);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), false);
    GtkCellRenderer* toggle = gtk_cell_renderer_toggle_new();
    g_signal_connect(toggle, "toggled", G_CALLBACK(onFileRootToggled), this);
    GtkTreeViewColumn* selectedColumn = gtk_tree_view_column_new_with_attributes("", toggle, "active", FileRootSelected, nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), selectedColumn);
    GtkTreeViewColumn* folderColumn = gtk_tree_view_column_new();
    GtkCellRenderer* icon = gtk_cell_renderer_pixbuf_new();
    GtkCellRenderer* text = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(folderColumn, icon, false);
    gtk_tree_view_column_pack_start(folderColumn, text, true);
    gtk_tree_view_column_add_attribute(folderColumn, icon, "pixbuf", FileRootIcon);
    gtk_tree_view_column_add_attribute(folderColumn, text, "text", FileRootLabel);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), folderColumn);
    g_signal_connect(tree, "row-expanded", G_CALLBACK(onFileRootExpanded), this);
    g_signal_connect(tree, "row-collapsed", G_CALLBACK(onFileRootCollapsed), this);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    GtkWidget* folderFrame = gtk_frame_new("Server folders");
    gtk_container_set_border_width(GTK_CONTAINER(scroll), 6);
    gtk_container_add(GTK_CONTAINER(folderFrame), scroll);
    gtk_box_pack_start(GTK_BOX(content), folderFrame, true, true, 0);
    filePickerSelected = splitFolders(fileFolders);
    pendingFilePickerFolders.clear();
    filePickerLoadingFolder.clear();
    for (const std::string& root : roots) {
        std::string current;
        std::size_t offset = 0;
        GtkTreeIter parent;
        bool hasParent = false;
        while (offset < root.size()) {
            const std::size_t slash = root.find('/', offset);
            const std::string part = root.substr(offset, slash == std::string::npos ? std::string::npos : slash - offset);
            if (!part.empty()) {
                if (!current.empty()) current += '/';
                current += part;
                const bool selected = std::any_of(filePickerSelected.begin(), filePickerSelected.end(), [&](const std::string& root) { return current == root || current.size() > root.size() && current.compare(0, root.size(), root) == 0 && current[root.size()] == '/'; });
                if (selected && std::find(filePickerSelected.begin(), filePickerSelected.end(), current) == filePickerSelected.end()) filePickerSelected.push_back(current);
                GtkTreeIter folderIter;
                if (!findFileRoot(current, folderIter)) {
                    gtk_tree_store_append(fileRootsStore, &folderIter, hasParent ? &parent : nullptr);
                    gtk_tree_store_set(fileRootsStore, &folderIter, FileRootSelected, selected, FileRootPath, current.c_str(), FileRootLabel, part.c_str(), FileRootIcon, fileRootFolderIcon, FileRootLoaded, false, FileRootPlaceholder, false, -1);
                    GtkTreeIter placeholder;
                    gtk_tree_store_append(fileRootsStore, &placeholder, &folderIter);
                    gtk_tree_store_set(fileRootsStore, &placeholder, FileRootSelected, false, FileRootPath, "", FileRootLabel, "Loading...", FileRootLoaded, true, FileRootPlaceholder, true, -1);
                }
                parent = folderIter;
                hasParent = true;
            }
            if (slash == std::string::npos) break;
            offset = slash + 1;
        }
    }
    if (roots.empty()) {
        GtkTreeIter empty;
        gtk_tree_store_append(fileRootsStore, &empty, nullptr);
        gtk_tree_store_set(fileRootsStore, &empty, FileRootSelected, false, FileRootPath, "", FileRootLabel, "No usable server file roots were advertised.", FileRootLoaded, true, FileRootPlaceholder, true, -1);
    }
    browsingFileRoots = true;
    g_signal_connect(fileRootsDialog, "response", G_CALLBACK(onFileRootsResponse), this);
    gtk_widget_show_all(fileRootsDialog);
}

void TSyncManager::onFileRootsResponse(GtkDialog* dialog, gint response, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    if (response == GTK_RESPONSE_APPLY) {
        std::vector<std::string> selected;
        for (const std::string& value : manager->filePickerSelected) {
            const std::string normalized = normalizeRemotePath(value);
            if (!normalized.empty() && std::find(selected.begin(), selected.end(), normalized) == selected.end()) selected.push_back(normalized);
        }
        std::sort(selected.begin(), selected.end());
        std::ostringstream value;
        for (std::size_t index = 0; index < selected.size(); ++index) {
            if (index != 0) value << "; ";
            value << selected[index];
        }
        manager->fileFolders = value.str();
        manager->saveConfig();
        manager->logEvent("Selected " + std::to_string(selected.size()) + " server file roots");
        manager->refreshWindow();
        if (manager->enabled && manager->syncFiles && manager->downloadsAllowed() && !selected.empty()) manager->beginFilePull();
    }
    manager->browsingFileRoots = false;
    manager->filePickerLoadingFolder.clear();
    manager->pendingFilePickerFolders.clear();
    manager->filePickerSelected.clear();
    manager->fileRootsStore = nullptr;
    gtk_widget_destroy(GTK_WIDGET(dialog));
    if (!manager->enabled) manager->setPhase("Stopped");
    if (manager->parentVisibleBeforeFilePicker && manager->parent != nullptr && !gtk_widget_get_visible(manager->parent)) gtk_widget_show(manager->parent);
    manager->parentVisibleBeforeFilePicker = false;
}

void TSyncManager::onFileRootToggled(GtkCellRendererToggle*, gchar* path, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    if (manager->fileRootsStore == nullptr) return;
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(manager->fileRootsStore), &iter, path)) return;
    gboolean selected = false;
    gboolean placeholder = false;
    gtk_tree_model_get(GTK_TREE_MODEL(manager->fileRootsStore), &iter, FileRootSelected, &selected, FileRootPlaceholder, &placeholder, -1);
    if (placeholder) return;
    const bool next = !selected;
    std::function<void(GtkTreeIter)> apply = [&](GtkTreeIter current) {
        gboolean currentPlaceholder = false;
        gchar* root = nullptr;
        gtk_tree_model_get(GTK_TREE_MODEL(manager->fileRootsStore), &current, FileRootPath, &root, FileRootPlaceholder, &currentPlaceholder, -1);
        if (!currentPlaceholder && root != nullptr && *root != '\0') {
            gtk_tree_store_set(manager->fileRootsStore, &current, FileRootSelected, next, -1);
            const auto found = std::find(manager->filePickerSelected.begin(), manager->filePickerSelected.end(), root);
            if (next && found == manager->filePickerSelected.end()) manager->filePickerSelected.emplace_back(root);
            if (!next && found != manager->filePickerSelected.end()) manager->filePickerSelected.erase(found);
        }
        g_free(root);
        GtkTreeIter child;
        if (gtk_tree_model_iter_children(GTK_TREE_MODEL(manager->fileRootsStore), &child, &current)) do apply(child); while (gtk_tree_model_iter_next(GTK_TREE_MODEL(manager->fileRootsStore), &child));
    };
    apply(iter);
    if (!next) {
        GtkTreeIter child = iter;
        GtkTreeIter parent;
        while (gtk_tree_model_iter_parent(GTK_TREE_MODEL(manager->fileRootsStore), &parent, &child)) {
            gchar* root = nullptr;
            gtk_tree_model_get(GTK_TREE_MODEL(manager->fileRootsStore), &parent, FileRootPath, &root, -1);
            gtk_tree_store_set(manager->fileRootsStore, &parent, FileRootSelected, false, -1);
            const auto found = root == nullptr ? manager->filePickerSelected.end() : std::find(manager->filePickerSelected.begin(), manager->filePickerSelected.end(), root);
            if (found != manager->filePickerSelected.end()) manager->filePickerSelected.erase(found);
            g_free(root);
            child = parent;
        }
    }
}

void TSyncManager::onFileRootExpanded(GtkTreeView*, GtkTreeIter* iter, GtkTreePath*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    if (manager->fileRootsStore == nullptr) return;
    gboolean loaded = false;
    gboolean placeholder = false;
    gchar* path = nullptr;
    gtk_tree_model_get(GTK_TREE_MODEL(manager->fileRootsStore), iter, FileRootPath, &path, FileRootLoaded, &loaded, FileRootPlaceholder, &placeholder, -1);
    if (!loaded && !placeholder && path != nullptr && *path != '\0') {
        const std::string folder = normalizeRemotePath(path);
        const bool active = normalizeRemotePath(manager->filePickerLoadingFolder) == folder;
        const bool queued = std::find(manager->pendingFilePickerFolders.begin(), manager->pendingFilePickerFolders.end(), folder) != manager->pendingFilePickerFolders.end();
        if (!active && !queued) manager->pendingFilePickerFolders.push_back(folder);
    }
    if (!placeholder) gtk_tree_store_set(manager->fileRootsStore, iter, FileRootIcon, manager->fileRootOpenFolderIcon, -1);
    g_free(path);
    manager->requestNextFilePickerFolder();
}

void TSyncManager::onFileRootCollapsed(GtkTreeView*, GtkTreeIter* iter, GtkTreePath*, gpointer data) {
    auto* manager = static_cast<TSyncManager*>(data);
    if (manager->fileRootsStore != nullptr) gtk_tree_store_set(manager->fileRootsStore, iter, FileRootIcon, manager->fileRootFolderIcon, -1);
}

void TSyncManager::requestNextFilePickerFolder() {
    if (!browsingFileRoots || connection == nullptr || !filePickerLoadingFolder.empty()) return;
    while (!pendingFilePickerFolders.empty()) {
        filePickerLoadingFolder = pendingFilePickerFolders.front();
        pendingFilePickerFolders.pop_front();
        std::string request = filePickerLoadingFolder;
        if (!request.empty() && request.back() != '/') request += '/';
        setPhase("Browsing " + filePickerLoadingFolder);
        logEvent("Loading folder " + filePickerLoadingFolder);
        if (rc_sync_filebrowser_cd(connection, request.c_str())) return;
        logEvent("Unable to open folder " + filePickerLoadingFolder);
        filePickerLoadingFolder.clear();
    }
    if (fileRootsDialog != nullptr) gtk_dialog_set_response_sensitive(GTK_DIALOG(fileRootsDialog), GTK_RESPONSE_APPLY, true);
}

bool TSyncManager::findFileRoot(const std::string& path, GtkTreeIter& result) const {
    if (fileRootsStore == nullptr) return false;
    std::function<bool(GtkTreeIter)> find = [&](GtkTreeIter iter) {
        do {
            gchar* value = nullptr;
            gtk_tree_model_get(GTK_TREE_MODEL(fileRootsStore), &iter, FileRootPath, &value, -1);
            const bool match = value != nullptr && normalizeRemotePath(value) == normalizeRemotePath(path);
            g_free(value);
            if (match) { result = iter; return true; }
            GtkTreeIter child;
            if (gtk_tree_model_iter_children(GTK_TREE_MODEL(fileRootsStore), &child, &iter) && find(child)) return true;
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(fileRootsStore), &iter));
        return false;
    };
    GtkTreeIter first;
    return gtk_tree_model_get_iter_first(GTK_TREE_MODEL(fileRootsStore), &first) && find(first);
}

void TSyncManager::showResult(const std::string& title, const std::string& message, GtkMessageType type) const {
    GtkWidget* dialog = gtk_message_dialog_new(nullptr, static_cast<GtkDialogFlags>(0), type, GTK_BUTTONS_CLOSE, "%s", message.empty() ? title.c_str() : message.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());
    gtk_window_set_modal(GTK_WINDOW(dialog), false);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
    gtk_widget_show(dialog);
}
