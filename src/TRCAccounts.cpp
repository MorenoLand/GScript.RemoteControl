#include "TRCAccounts.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <glib.h>
#include <openssl/crypto.h>
#include <openssl/des.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

    constexpr std::array<unsigned char, 8> fileMagic = {'G', 'S', 'R', 'C', 'A', 'C', 'C', '1'};
    constexpr std::size_t keySize = 32;
    constexpr std::size_t nonceSize = 12;
    constexpr std::size_t tagSize = 16;

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
        const std::size_t payloadSize = 4 + password.size();
        const std::size_t paddedSize = (payloadSize + 7) & ~std::size_t(7);
        std::vector<unsigned char> plaintext(paddedSize);
        plaintext[0] = static_cast<unsigned char>(password.size());
        plaintext[1] = static_cast<unsigned char>(password.size() >> 8);
        plaintext[2] = static_cast<unsigned char>(password.size() >> 16);
        plaintext[3] = static_cast<unsigned char>(password.size() >> 24);
        std::copy(password.begin(), password.end(), plaintext.begin() + 4);
        if (paddedSize > payloadSize) RAND_bytes(plaintext.data() + payloadSize, static_cast<int>(paddedSize - payloadSize));
        DES_cblock keyBlock;
        std::copy_n(key.begin(), sizeof(keyBlock), keyBlock);
        DES_key_schedule schedule;
        DES_set_key_unchecked(&keyBlock, &schedule);
        std::string encoded(paddedSize, '\0');
        for (std::size_t offset = 0; offset < paddedSize; offset += 8) DES_ecb_encrypt(reinterpret_cast<const_DES_cblock*>(plaintext.data() + offset), reinterpret_cast<DES_cblock*>(encoded.data() + offset), &schedule, DES_ENCRYPT);
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        return encoded;
    }

    bool decodePassword(const std::string& encoded, const std::array<unsigned char, keySize>& key, std::string& password) {
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

    std::vector<unsigned char> serialize(const std::string& selected, const std::vector<RC::RCAccount>& accounts, const std::array<unsigned char, keySize>& key) {
        std::vector<unsigned char> output;
        appendString(output, selected);
        appendUint32(output, static_cast<std::uint32_t>(accounts.size()));
        for (const RC::RCAccount& account : accounts) {
            appendString(output, account.name);
            appendString(output, encodePassword(account.password, key));
            appendUint32(output, static_cast<std::uint32_t>(account.listServers.size()));
            for (const std::string& server : account.listServers) appendString(output, server);
        }
        return output;
    }

    bool deserialize(const std::vector<unsigned char>& input, const std::array<unsigned char, keySize>& key, std::string& selected, std::vector<RC::RCAccount>& accounts) {
        std::size_t offset = 0;
        std::uint32_t accountCount = 0;
        if (!readString(input, offset, selected) || !readUint32(input, offset, accountCount) || accountCount > 1000) return false;
        std::vector<RC::RCAccount> parsed;
        for (std::uint32_t accountIndex = 0; accountIndex < accountCount; ++accountIndex) {
            RC::RCAccount account;
            std::string encodedPassword;
            std::uint32_t serverCount = 0;
            if (!readString(input, offset, account.name) || !readString(input, offset, encodedPassword) || !decodePassword(encodedPassword, key, account.password) || !readUint32(input, offset, serverCount) || serverCount > 1000) return false;
            for (std::uint32_t serverIndex = 0; serverIndex < serverCount; ++serverIndex) {
                std::string server;
                if (!readString(input, offset, server)) return false;
                appendUnique(account.listServers, server);
            }
            if (!account.name.empty()) parsed.push_back(std::move(account));
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
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream || (!data.empty() && !stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size())))) return false;
        stream.close();
#ifndef _WIN32
        chmod(path.string().c_str(), S_IRUSR | S_IWUSR);
#endif
        return static_cast<bool>(stream);
    }

    bool loadOrCreateKey(const std::filesystem::path& directory, std::array<unsigned char, keySize>& key) {
        std::vector<unsigned char> existing;
        const std::filesystem::path keyPath = directory / "accounts.key";
        if (readFile(keyPath, existing) && existing.size() == key.size()) { std::copy(existing.begin(), existing.end(), key.begin()); OPENSSL_cleanse(existing.data(), existing.size()); return true; }
        if (RAND_bytes(key.data(), static_cast<int>(key.size())) != 1) return false;
        return writePrivateFile(keyPath, std::vector<unsigned char>(key.begin(), key.end()));
    }

    bool encrypt(const std::vector<unsigned char>& plaintext, const std::array<unsigned char, keySize>& key, std::vector<unsigned char>& output) {
        std::array<unsigned char, nonceSize> nonce = {};
        std::array<unsigned char, tagSize> tag = {};
        if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) return false;
        EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
        if (context == nullptr) return false;
        std::vector<unsigned char> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
        int written = 0;
        int finalWritten = 0;
        const bool ok = EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
            EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) == 1 &&
            EVP_EncryptInit_ex(context, nullptr, nullptr, key.data(), nonce.data()) == 1 &&
            EVP_EncryptUpdate(context, ciphertext.data(), &written, plaintext.data(), static_cast<int>(plaintext.size())) == 1 &&
            EVP_EncryptFinal_ex(context, ciphertext.data() + written, &finalWritten) == 1 &&
            EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data()) == 1;
        EVP_CIPHER_CTX_free(context);
        if (!ok) return false;
        ciphertext.resize(static_cast<std::size_t>(written + finalWritten));
        output.assign(fileMagic.begin(), fileMagic.end());
        output.insert(output.end(), nonce.begin(), nonce.end());
        output.insert(output.end(), tag.begin(), tag.end());
        output.insert(output.end(), ciphertext.begin(), ciphertext.end());
        return true;
    }

    bool decrypt(const std::vector<unsigned char>& input, const std::array<unsigned char, keySize>& key, std::vector<unsigned char>& plaintext) {
        if (input.size() < fileMagic.size() + nonceSize + tagSize || !std::equal(fileMagic.begin(), fileMagic.end(), input.begin())) return false;
        const unsigned char* nonce = input.data() + fileMagic.size();
        const unsigned char* tag = nonce + nonceSize;
        const unsigned char* ciphertext = tag + tagSize;
        const std::size_t ciphertextSize = input.size() - fileMagic.size() - nonceSize - tagSize;
        EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
        if (context == nullptr) return false;
        plaintext.resize(ciphertextSize + EVP_MAX_BLOCK_LENGTH);
        int written = 0;
        int finalWritten = 0;
        const bool ok = EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
            EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, nonceSize, nullptr) == 1 &&
            EVP_DecryptInit_ex(context, nullptr, nullptr, key.data(), nonce) == 1 &&
            EVP_DecryptUpdate(context, plaintext.data(), &written, ciphertext, static_cast<int>(ciphertextSize)) == 1 &&
            EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG, tagSize, const_cast<unsigned char*>(tag)) == 1 &&
            EVP_DecryptFinal_ex(context, plaintext.data() + written, &finalWritten) == 1;
        EVP_CIPHER_CTX_free(context);
        if (!ok) { OPENSSL_cleanse(plaintext.data(), plaintext.size()); plaintext.clear(); return false; }
        plaintext.resize(static_cast<std::size_t>(written + finalWritten));
        return true;
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
        const bool ok = decrypt(encrypted, key, plaintext) && deserialize(plaintext, key, activeAccount, accountEntries);
        OPENSSL_cleanse(key.data(), key.size());
        if (!plaintext.empty()) OPENSSL_cleanse(plaintext.data(), plaintext.size());
        if (!ok) return false;
        rebuildNames();
        activePassword = passwordFor(activeAccount);
        if (activeAccount.empty() && !accountEntries.empty()) { activeAccount = accountEntries.front().name; activePassword = accountEntries.front().password; }
        return true;
    }

    bool RCAccounts::persist() {
        std::array<unsigned char, keySize> key = {};
        if (!loadOrCreateKey(storageDirectory, key)) return false;
        std::vector<unsigned char> plaintext = serialize(activeAccount, accountEntries, key);
        std::vector<unsigned char> encrypted;
        const bool ok = encrypt(plaintext, key, encrypted) && writePrivateFile(storageDirectory / "accounts.dat", encrypted);
        OPENSSL_cleanse(key.data(), key.size());
        if (!plaintext.empty()) OPENSSL_cleanse(plaintext.data(), plaintext.size());
        return ok;
    }

    const std::vector<std::string>& RCAccounts::names() const { return accountNames; }
    const std::vector<RCAccount>& RCAccounts::entries() const { return accountEntries; }
    const std::string& RCAccounts::accountName() const { return activeAccount; }
    const std::string& RCAccounts::password() const { return activePassword; }

    std::string RCAccounts::passwordFor(const std::string& accountName) const {
        const std::string target = lower(accountName);
        const auto found = std::find_if(accountEntries.begin(), accountEntries.end(), [&](const RCAccount& account) { return lower(account.name) == target; });
        return found == accountEntries.end() ? std::string() : found->password;
    }

    std::vector<std::string> RCAccounts::listServersFor(const std::string& accountName) const {
        const std::string target = lower(accountName);
        const auto found = std::find_if(accountEntries.begin(), accountEntries.end(), [&](const RCAccount& account) { return lower(account.name) == target; });
        return found == accountEntries.end() ? std::vector<std::string>() : found->listServers;
    }

    void RCAccounts::save(const std::string& accountName, const std::string& password, bool dontSavePassword, const std::string& listServer) {
        if (accountName.empty()) return;
        const std::string target = lower(accountName);
        auto found = std::find_if(accountEntries.begin(), accountEntries.end(), [&](const RCAccount& account) { return lower(account.name) == target; });
        if (found == accountEntries.end()) { accountEntries.push_back({accountName, {}, {}}); found = std::prev(accountEntries.end()); }
        found->name = accountName;
        found->password = dontSavePassword ? std::string() : password;
        appendUnique(found->listServers, listServer);
        activeAccount = accountName;
        activePassword = found->password;
        rebuildNames();
        persist();
    }

    void RCAccounts::associate(const std::string& accountName, const std::string& listServer) {
        if (accountName.empty() || listServer.empty()) return;
        const std::string password = passwordFor(accountName);
        save(accountName, password, password.empty(), listServer);
    }

    void RCAccounts::update(const std::string& previousName, const std::string& accountName, const std::string& password, bool dontSavePassword, const std::vector<std::string>& listServers) {
        if (accountName.empty()) return;
        const std::string previousTarget = lower(previousName);
        auto found = std::find_if(accountEntries.begin(), accountEntries.end(), [&](const RCAccount& account) { return lower(account.name) == previousTarget; });
        if (found == accountEntries.end()) { accountEntries.push_back({}); found = std::prev(accountEntries.end()); }
        found->name = accountName;
        found->password = dontSavePassword ? std::string() : password;
        found->listServers.clear();
        for (const std::string& server : listServers) appendUnique(found->listServers, server);
        activeAccount = accountName;
        activePassword = found->password;
        rebuildNames();
        persist();
    }

    void RCAccounts::remove(const std::string& accountName) {
        const std::string target = lower(accountName);
        accountEntries.erase(std::remove_if(accountEntries.begin(), accountEntries.end(), [&](const RCAccount& account) { return lower(account.name) == target; }), accountEntries.end());
        if (lower(activeAccount) == target) { activeAccount = accountEntries.empty() ? std::string() : accountEntries.front().name; activePassword = passwordFor(activeAccount); }
        rebuildNames();
        persist();
    }

    void RCAccounts::rebuildNames() {
        accountNames.clear();
        for (const RCAccount& account : accountEntries) accountNames.push_back(account.name);
    }

}
