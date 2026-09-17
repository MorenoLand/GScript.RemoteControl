#include "TRCAccounts.h"
#include "TEncryption.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <glib.h>
#include <openssl/crypto.h>
#include <openssl/des.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

    constexpr std::array<unsigned char, 8> fileMagic = {'G', 'S', 'R', 'C', 'A', 'C', 'C', '1'};
    constexpr std::array<unsigned char, 4> passwordMagic = {'P', 'W', 'G', '1'};
    constexpr const char* accountFormat = "GScriptRCAccounts3";
    constexpr const char* legacyAccountFormat = "GScriptRCAccounts2";
    constexpr std::size_t keySize = RC::Encryption::keySize;

    std::string lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    void appendUnique(std::vector<std::string>& values, const std::string& value) {
        if (value.empty()) return;
        const std::string target = lower(value);
        if (std::none_of(values.begin(), values.end(), [&](const std::string& item) { return lower(item) == target; })) values.push_back(value);
    }

    void appendUint32(std::vector<unsigned char>& output, std::uint32_t value) {
        output.push_back(static_cast<unsigned char>(value));
        output.push_back(static_cast<unsigned char>(value >> 8));
        output.push_back(static_cast<unsigned char>(value >> 16));
        output.push_back(static_cast<unsigned char>(value >> 24));
    }

    bool readUint32(const std::vector<unsigned char>& input, std::size_t& offset, std::uint32_t& value) {
        if (offset + 4 > input.size()) return false;
        value = static_cast<std::uint32_t>(input[offset]) | (static_cast<std::uint32_t>(input[offset + 1]) << 8) | (static_cast<std::uint32_t>(input[offset + 2]) << 16) | (static_cast<std::uint32_t>(input[offset + 3]) << 24);
        offset += 4;
        return true;
    }

    void appendUint64(std::vector<unsigned char>& output, std::uint64_t value) {
        appendUint32(output, static_cast<std::uint32_t>(value));
        appendUint32(output, static_cast<std::uint32_t>(value >> 32));
    }

    bool readUint64(const std::vector<unsigned char>& input, std::size_t& offset, std::uint64_t& value) {
        std::uint32_t low = 0;
        std::uint32_t high = 0;
        if (!readUint32(input, offset, low) || !readUint32(input, offset, high)) return false;
        value = static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32);
        return true;
    }

    void appendString(std::vector<unsigned char>& output, const std::string& value) {
        appendUint32(output, static_cast<std::uint32_t>(value.size()));
        output.insert(output.end(), value.begin(), value.end());
    }

    bool readString(const std::vector<unsigned char>& input, std::size_t& offset, std::string& value) {
        std::uint32_t length = 0;
        if (!readUint32(input, offset, length) || offset + length > input.size()) return false;
        value.assign(reinterpret_cast<const char*>(input.data() + offset), length);
        offset += length;
        return true;
    }

    std::string encodePassword(const std::string& password, const std::array<unsigned char, keySize>& key) {
        if (password.empty()) return {};
        std::string encoded;
        return RC::Encryption::encryptString(password, key, passwordMagic, encoded) ? encoded : std::string();
    }

    bool decodeLegacyPassword(const std::string& encoded, const std::array<unsigned char, keySize>& key, std::string& password) {
        if (encoded.empty()) { password.clear(); return true; }
        if (encoded.size() % 8 != 0 || encoded.size() < 8) return false;
        DES_cblock keyBlock;
        std::copy_n(key.begin(), sizeof(keyBlock), keyBlock);
        DES_key_schedule schedule;
        DES_set_key_unchecked(&keyBlock, &schedule);
        std::vector<unsigned char> plaintext(encoded.size());
        for (std::size_t offset = 0; offset < encoded.size(); offset += 8) {
            DES_cblock inputBlock;
            DES_cblock outputBlock;
            std::copy_n(reinterpret_cast<const unsigned char*>(encoded.data() + offset), sizeof(inputBlock), inputBlock);
            DES_ecb_encrypt(&inputBlock, &outputBlock, &schedule, DES_DECRYPT);
            std::copy_n(outputBlock, sizeof(outputBlock), plaintext.data() + offset);
        }
        const std::uint32_t length = static_cast<std::uint32_t>(plaintext[0]) | (static_cast<std::uint32_t>(plaintext[1]) << 8) | (static_cast<std::uint32_t>(plaintext[2]) << 16) | (static_cast<std::uint32_t>(plaintext[3]) << 24);
        if (length > plaintext.size() - 4) { OPENSSL_cleanse(plaintext.data(), plaintext.size()); return false; }
        password.assign(reinterpret_cast<const char*>(plaintext.data() + 4), length);
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        return true;
    }

    bool decodePassword(const std::string& encoded, const std::array<unsigned char, keySize>& key, std::string& password, bool& migratedLegacy) {
        if (encoded.empty()) { password.clear(); return true; }
        if (encoded.size() < passwordMagic.size() || !std::equal(passwordMagic.begin(), passwordMagic.end(), encoded.begin())) {
            if (!decodeLegacyPassword(encoded, key, password)) return false;
            migratedLegacy = true;
            return true;
        }
        return RC::Encryption::decryptString(encoded, key, passwordMagic, password);
    }

    std::vector<unsigned char> serialize(std::uint64_t selectedId, const std::vector<RC::RCAccount>& accounts, const std::array<unsigned char, keySize>& key) {
        std::vector<unsigned char> output;
        appendString(output, accountFormat);
        appendUint64(output, selectedId);
        appendUint32(output, static_cast<std::uint32_t>(accounts.size()));
        for (const RC::RCAccount& account : accounts) {
            appendUint64(output, account.id);
            appendString(output, account.name);
            appendString(output, encodePassword(account.password, key));
            appendUint32(output, static_cast<std::uint32_t>(account.listServers.size()));
            for (const std::string& server : account.listServers) appendString(output, server);
            appendUint32(output, account.directMode ? 1U : 0U);
        }
        return output;
    }

    bool deserialize(const std::vector<unsigned char>& input, const std::array<unsigned char, keySize>& key, std::string& selected, std::uint64_t& selectedId, std::vector<RC::RCAccount>& accounts, bool& migratedLegacy) {
        std::size_t offset = 0;
        std::string header;
        std::uint32_t accountCount = 0;
        if (!readString(input, offset, header)) return false;
        const bool currentFormat = header == accountFormat || header == legacyAccountFormat;
        const bool directFormat = header == accountFormat;
        if (currentFormat) {
            if (!readUint64(input, offset, selectedId)) return false;
            if (!directFormat) migratedLegacy = true;
        } else {
            selected = header;
            selectedId = 0;
            migratedLegacy = true;
        }
        if (!readUint32(input, offset, accountCount) || accountCount > 1000) return false;
        std::vector<RC::RCAccount> parsed;
        std::uint64_t nextId = 1;
        for (std::uint32_t accountIndex = 0; accountIndex < accountCount; ++accountIndex) {
            RC::RCAccount account;
            std::string encodedPassword;
            std::uint32_t serverCount = 0;
            if (currentFormat) {
                if (!readUint64(input, offset, account.id) || account.id == 0) return false;
                nextId = std::max(nextId, account.id + 1);
            } else {
                account.id = nextId++;
            }
            if (!readString(input, offset, account.name) || !readString(input, offset, encodedPassword) || !decodePassword(encodedPassword, key, account.password, migratedLegacy) || !readUint32(input, offset, serverCount) || serverCount > 1000) return false;
            for (std::uint32_t serverIndex = 0; serverIndex < serverCount; ++serverIndex) {
                std::string server;
                if (!readString(input, offset, server)) return false;
                appendUnique(account.listServers, server);
            }
            if (directFormat) {
                std::uint32_t directMode = 0;
                if (!readUint32(input, offset, directMode)) return false;
                account.directMode = directMode != 0;
            }
            if (!account.listServers.empty() && account.listServers.front().rfind("direct://", 0) == 0) {
                account.directMode = true;
                account.listServers.front().erase(0, std::string("direct://").size());
                migratedLegacy = true;
            }
            if (account.listServers.size() > 1) { account.listServers.resize(1); migratedLegacy = true; }
            if (!account.name.empty()) {
                const bool duplicate = std::any_of(parsed.begin(), parsed.end(), [&](const RC::RCAccount& current) { return lower(current.name) == lower(account.name) && current.password == account.password && current.listServers == account.listServers && current.directMode == account.directMode; });
                if (duplicate) {
                    if (selectedId == account.id) selectedId = parsed[static_cast<std::size_t>(std::find_if(parsed.begin(), parsed.end(), [&](const RC::RCAccount& current) { return lower(current.name) == lower(account.name) && current.password == account.password && current.listServers == account.listServers && current.directMode == account.directMode; }) - parsed.begin())].id;
                    migratedLegacy = true;
                } else parsed.push_back(std::move(account));
            }
        }
        if (offset != input.size()) return false;
        accounts = std::move(parsed);
        return true;
    }

    bool readFile(const std::filesystem::path& path, std::vector<unsigned char>& data) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return false;
        stream.seekg(0, std::ios::end);
        const std::streamoff size = stream.tellg();
        if (size < 0 || size > 16 * 1024 * 1024) return false;
        stream.seekg(0);
        data.resize(static_cast<std::size_t>(size));
        return data.empty() || static_cast<bool>(stream.read(reinterpret_cast<char*>(data.data()), size));
    }

    bool writePrivateFile(const std::filesystem::path& path, const std::vector<unsigned char>& data) {
        return RC::Encryption::writePrivateFile(path, data);
    }

    bool loadOrCreateKey(const std::filesystem::path& directory, std::array<unsigned char, keySize>& key) {
        return RC::Encryption::loadOrCreateKey(directory, "accounts.key", key);
    }

    bool encrypt(const std::vector<unsigned char>& plaintext, const std::array<unsigned char, keySize>& key, std::vector<unsigned char>& output) {
        return RC::Encryption::encrypt(plaintext, key, fileMagic, output);
    }

    bool decrypt(const std::vector<unsigned char>& input, const std::array<unsigned char, keySize>& key, std::vector<unsigned char>& plaintext) {
        return RC::Encryption::decrypt(input, key, fileMagic, plaintext);
    }

}

namespace RC {

    RCAccounts::RCAccounts(const std::filesystem::path& requestedDirectory) {
        storageDirectory = requestedDirectory.empty() ? std::filesystem::path(g_get_user_config_dir()) / "GScriptRC" : requestedDirectory;
        std::error_code error;
        std::filesystem::create_directories(storageDirectory, error);
#ifndef _WIN32
        if (!error) chmod(storageDirectory.string().c_str(), S_IRWXU);
#endif
        load();
    }

    bool RCAccounts::load() {
        std::array<unsigned char, keySize> key = {};
        std::vector<unsigned char> encrypted;
        std::vector<unsigned char> plaintext;
        if (!loadOrCreateKey(storageDirectory, key) || !readFile(storageDirectory / "accounts.dat", encrypted)) { OPENSSL_cleanse(key.data(), key.size()); return false; }
        bool migratedLegacy = false;
        const bool ok = decrypt(encrypted, key, plaintext) && deserialize(plaintext, key, activeAccount, activeAccountId, accountEntries, migratedLegacy);
        OPENSSL_cleanse(key.data(), key.size());
        if (!plaintext.empty()) OPENSSL_cleanse(plaintext.data(), plaintext.size());
        if (!ok) return false;
        rebuildNames();
        activeAccountIndex = indexForId(activeAccountId);
        std::ifstream selectedIndexFile(storageDirectory / "selected-account");
        std::size_t storedIndex = 0;
        if (activeAccountIndex == static_cast<std::size_t>(-1) && selectedIndexFile >> storedIndex && storedIndex < accountEntries.size()) { activeAccountIndex = storedIndex; migratedLegacy = true; }
        if (activeAccountIndex == static_cast<std::size_t>(-1)) for (std::size_t index = 0; index < accountEntries.size(); ++index) if (lower(accountEntries[index].name) == lower(activeAccount)) { activeAccountIndex = index; break; }
        if (activeAccountIndex == static_cast<std::size_t>(-1) && !accountEntries.empty()) activeAccountIndex = 0;
        if (activeAccountIndex != static_cast<std::size_t>(-1)) { activeAccount = accountEntries[activeAccountIndex].name; activeAccountId = accountEntries[activeAccountIndex].id; }
        nextAccountId = 1;
        for (const RCAccount& account : accountEntries) nextAccountId = std::max(nextAccountId, account.id + 1);
        activePassword = activeAccountIndex == static_cast<std::size_t>(-1) ? std::string() : accountEntries[activeAccountIndex].password;
        if (migratedLegacy) persist();
        return true;
    }

    bool RCAccounts::persist() {
        std::array<unsigned char, keySize> key = {};
        if (!loadOrCreateKey(storageDirectory, key)) return false;
        std::vector<unsigned char> plaintext = serialize(activeAccountId, accountEntries, key);
        std::vector<unsigned char> encrypted;
        const bool ok = encrypt(plaintext, key, encrypted) && writePrivateFile(storageDirectory / "accounts.dat", encrypted);
        if (ok) { std::ofstream selectedIndexFile(storageDirectory / "selected-account", std::ios::trunc); if (activeAccountIndex != static_cast<std::size_t>(-1)) selectedIndexFile << activeAccountIndex << '\n'; }
        OPENSSL_cleanse(key.data(), key.size());
        if (!plaintext.empty()) OPENSSL_cleanse(plaintext.data(), plaintext.size());
        return ok;
    }

    const std::vector<std::string>& RCAccounts::names() const { return accountNames; }
    const std::vector<RCAccount>& RCAccounts::entries() const { return accountEntries; }
    const std::string& RCAccounts::accountName() const { return activeAccount; }
    std::size_t RCAccounts::activeIndex() const { return activeAccountIndex; }
    std::uint64_t RCAccounts::activeId() const { return activeAccountId; }
    std::uint64_t RCAccounts::idForIndex(std::size_t index) const { return index < accountEntries.size() ? accountEntries[index].id : 0; }
    std::size_t RCAccounts::indexForId(std::uint64_t id) const {
        const auto found = std::find_if(accountEntries.begin(), accountEntries.end(), [&](const RCAccount& account) { return account.id == id; });
        return found == accountEntries.end() ? static_cast<std::size_t>(-1) : static_cast<std::size_t>(found - accountEntries.begin());
    }
    const std::string& RCAccounts::password() const { return activePassword; }

    std::string RCAccounts::passwordFor(const std::string& accountName) const {
        const std::string target = lower(accountName);
        const auto found = std::find_if(accountEntries.begin(), accountEntries.end(), [&](const RCAccount& account) { return lower(account.name) == target; });
        return found == accountEntries.end() ? std::string() : found->password;
    }
    std::string RCAccounts::passwordForIndex(std::size_t index) const { return index < accountEntries.size() ? accountEntries[index].password : std::string(); }

    std::vector<std::string> RCAccounts::listServersFor(const std::string& accountName) const {
        const std::string target = lower(accountName);
        const auto found = std::find_if(accountEntries.begin(), accountEntries.end(), [&](const RCAccount& account) { return lower(account.name) == target; });
        return found == accountEntries.end() ? std::vector<std::string>() : found->listServers;
    }
    std::vector<std::string> RCAccounts::listServersForIndex(std::size_t index) const { return index < accountEntries.size() ? accountEntries[index].listServers : std::vector<std::string>(); }

    void RCAccounts::save(const std::string& accountName, const std::string& password, bool dontSavePassword, const std::string& listServer, bool directMode) {
        if (accountName.empty()) return;
        const std::string target = lower(accountName);
        auto found = std::find_if(accountEntries.begin(), accountEntries.end(), [&](const RCAccount& account) { return lower(account.name) == target; });
        if (found == accountEntries.end()) { accountEntries.push_back({nextAccountId++, accountName, {}, {}}); found = std::prev(accountEntries.end()); }
        found->name = accountName;
        found->password = dontSavePassword ? std::string() : password;
        found->directMode = directMode;
        found->listServers.clear();
        if (!listServer.empty()) found->listServers.push_back(listServer);
        activeAccount = accountName;
        activePassword = found->password;
        activeAccountIndex = static_cast<std::size_t>(found - accountEntries.begin());
        activeAccountId = found->id;
        rebuildNames();
        persist();
    }
    void RCAccounts::saveAt(std::size_t index, const std::string& password, bool dontSavePassword, const std::string& listServer, bool directMode) {
        if (index >= accountEntries.size()) return;
        accountEntries[index].password = dontSavePassword ? std::string() : password;
        accountEntries[index].directMode = directMode;
        accountEntries[index].listServers.clear();
        if (!listServer.empty()) accountEntries[index].listServers.push_back(listServer);
        activeAccount = accountEntries[index].name;
        activePassword = accountEntries[index].password;
        activeAccountIndex = index;
        activeAccountId = accountEntries[index].id;
        rebuildNames();
        persist();
    }

    void RCAccounts::associate(const std::string& accountName, const std::string& listServer) {
        if (accountName.empty() || listServer.empty()) return;
        const std::string password = passwordFor(accountName);
        const std::string target = lower(accountName);
        const auto found = std::find_if(accountEntries.begin(), accountEntries.end(), [&](const RCAccount& account) { return lower(account.name) == target; });
        save(accountName, password, password.empty(), listServer, found != accountEntries.end() && found->directMode);
    }

    void RCAccounts::update(const std::string& previousName, const std::string& accountName, const std::string& password, bool dontSavePassword, const std::vector<std::string>& listServers, bool directMode) {
        if (accountName.empty()) return;
        const std::string previousTarget = lower(previousName);
        auto found = previousName.empty() ? accountEntries.end() : std::find_if(accountEntries.begin(), accountEntries.end(), [&](const RCAccount& account) { return lower(account.name) == previousTarget; });
        if (found == accountEntries.end()) { accountEntries.push_back({nextAccountId++, {}, {}, {}}); found = std::prev(accountEntries.end()); }
        found->name = accountName;
        found->password = dontSavePassword ? std::string() : password;
        found->directMode = directMode;
        found->listServers.clear();
        if (!listServers.empty()) found->listServers.push_back(listServers.front());
        activeAccount = accountName;
        activePassword = found->password;
        activeAccountIndex = static_cast<std::size_t>(found - accountEntries.begin());
        activeAccountId = found->id;
        rebuildNames();
        persist();
    }
    void RCAccounts::updateAt(std::size_t index, const std::string& accountName, const std::string& password, bool dontSavePassword, const std::vector<std::string>& listServers, bool directMode) {
        if (index >= accountEntries.size() || accountName.empty()) return;
        accountEntries[index].name = accountName;
        accountEntries[index].password = dontSavePassword ? std::string() : password;
        accountEntries[index].directMode = directMode;
        accountEntries[index].listServers.clear();
        if (!listServers.empty()) accountEntries[index].listServers.push_back(listServers.front());
        activeAccount = accountName;
        activePassword = accountEntries[index].password;
        activeAccountIndex = index;
        activeAccountId = accountEntries[index].id;
        rebuildNames();
        persist();
    }

    void RCAccounts::remove(const std::string& accountName) {
        const std::string target = lower(accountName);
        accountEntries.erase(std::remove_if(accountEntries.begin(), accountEntries.end(), [&](const RCAccount& account) { return lower(account.name) == target; }), accountEntries.end());
        if (lower(activeAccount) == target) { activeAccountIndex = accountEntries.empty() ? static_cast<std::size_t>(-1) : 0; activeAccount = accountEntries.empty() ? std::string() : accountEntries.front().name; activePassword = accountEntries.empty() ? std::string() : accountEntries.front().password; activeAccountId = accountEntries.empty() ? 0 : accountEntries.front().id; }
        rebuildNames();
        persist();
    }
    void RCAccounts::removeAt(std::size_t index) {
        if (index >= accountEntries.size()) return;
        const bool active = activeAccountIndex == index;
        accountEntries.erase(accountEntries.begin() + static_cast<std::ptrdiff_t>(index));
        if (active) { activeAccountIndex = accountEntries.empty() ? static_cast<std::size_t>(-1) : 0; activeAccount = accountEntries.empty() ? std::string() : accountEntries.front().name; activePassword = accountEntries.empty() ? std::string() : accountEntries.front().password; activeAccountId = accountEntries.empty() ? 0 : accountEntries.front().id; }
        else if (activeAccountIndex > index && activeAccountIndex != static_cast<std::size_t>(-1)) --activeAccountIndex;
        rebuildNames();
        persist();
    }

    void RCAccounts::rebuildNames() {
        accountNames.clear();
        for (const RCAccount& account : accountEntries) accountNames.push_back(account.name);
    }

}
