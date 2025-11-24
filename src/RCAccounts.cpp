#include "RCAccounts.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <openssl/des.h>
#include <random>

#ifdef _WIN32
#include <iphlpapi.h>
#include <windows.h>
#endif

namespace {

    std::vector<std::string> splitCommaText(const std::string& value) {
        std::vector<std::string> result;
        std::string item;
        for (char character : value) {
            if (character == ',') {
                result.push_back(item);
                item.clear();
            } else item += character;
        }
        if (!value.empty()) result.push_back(item);
        return result;
    }

    std::string joinCommaText(const std::vector<std::string>& values) {
        std::string result;
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index != 0) result += ',';
            result += values[index];
        }
        return result;
    }

    std::string fixCommas(std::string value, bool encode) {
        std::replace(value.begin(), value.end(), encode ? ',' : '\1', encode ? '\1' : ',');
        return value;
    }

    std::array<unsigned char, 8> buildPasswordKey(bool alternate) {
        std::array<unsigned char, 8> key = {0xdc, 0x24};
        std::array<unsigned char, 6> mac = {};
        bool hasMac = false;
#ifdef _WIN32
        if (!alternate) {
            ULONG length = 0;
            if (GetAdaptersInfo(nullptr, &length) == ERROR_BUFFER_OVERFLOW) {
                std::vector<unsigned char> buffer(length);
                if (GetAdaptersInfo(reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data()), &length) == ERROR_SUCCESS) {
                    for (PIP_ADAPTER_INFO adapter = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data()); adapter != nullptr; adapter = adapter->Next) {
                        if (adapter->AddressLength != 0 && adapter->IpAddressList.IpAddress.String[0] != '\0' && std::string(adapter->IpAddressList.IpAddress.String) != "127.0.0.1") {
                            std::copy_n(adapter->Address, std::min<std::size_t>(adapter->AddressLength, mac.size()), mac.begin());
                            hasMac = true;
                            break;
                        }
                    }
                }
            }
        }
        if (alternate) {
            UUID uuid;
            if (UuidCreateSequential(&uuid) == RPC_S_OK || UuidCreate(&uuid) == RPC_S_OK) {
                std::copy_n(uuid.Data4 + 2, mac.size(), mac.begin());
                hasMac = true;
            }
        }
#endif
        if (hasMac) {
            key[2] = mac[2] ^ 0xa7;
            key[3] = mac[3] ^ 0x23;
            key[4] = mac[4] ^ 0x2e;
            key[5] = mac[5] ^ 0x6c;
        } else {
            key[2] = 0x78;
            key[3] = 0x3c;
            key[4] = 0x7c;
            key[5] = 0x2e;
        }
        unsigned int checksum = 0;
#ifdef _WIN32
        HKEY productKey = nullptr;
        const char* productPaths[] = {"Software\\Microsoft\\Windows\\CurrentVersion", "Software\\Microsoft\\Windows NT\\CurrentVersion"};
        for (const char* path : productPaths) {
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &productKey) != ERROR_SUCCESS) continue;
            std::array<unsigned char, 512> productId = {};
            DWORD length = static_cast<DWORD>(productId.size());
            if (RegQueryValueExA(productKey, "DigitalProductId", nullptr, nullptr, productId.data(), &length) == ERROR_SUCCESS) for (DWORD index = 0; index < length; ++index) checksum += productId[index];
            RegCloseKey(productKey);
            if (length != 0) break;
        }
#endif
        key[6] = checksum == 0 ? 0x61 : static_cast<unsigned char>(checksum) ^ 0xf6;
        key[7] = checksum == 0 ? 0x6b : static_cast<unsigned char>(checksum >> 8) ^ 0x3f;
        return key;
    }

    std::string cryptPassword(const std::array<unsigned char, 24>& input, bool decrypt, bool alternate) {
        const auto key = buildPasswordKey(alternate);
        DES_cblock keyBlock;
        std::copy(key.begin(), key.end(), keyBlock);
        DES_key_schedule schedule;
        DES_set_key_unchecked(&keyBlock, &schedule);
        std::array<unsigned char, 24> output = {};
        for (std::size_t offset = 0; offset < input.size(); offset += 8) {
            DES_cblock inputBlock;
            DES_cblock outputBlock;
            std::memcpy(inputBlock, input.data() + offset, sizeof(inputBlock));
            DES_ecb_encrypt(&inputBlock, &outputBlock, &schedule, decrypt ? DES_DECRYPT : DES_ENCRYPT);
            std::memcpy(output.data() + offset, outputBlock, sizeof(outputBlock));
        }
        return std::string(reinterpret_cast<const char*>(output.data()), output.size());
    }

    std::string encodePassword(const std::string& password) {
        std::array<unsigned char, 24> plaintext = {};
        const std::size_t length = std::min<std::size_t>(password.size(), 23);
        plaintext[0] = static_cast<unsigned char>(length);
        std::copy_n(password.begin(), length, plaintext.begin() + 1);
        std::random_device random;
        for (std::size_t index = length + 1; index < plaintext.size(); ++index) plaintext[index] = static_cast<unsigned char>(random());
        const std::string encrypted = cryptPassword(plaintext, false, false);
        std::string result;
        result.reserve(48);
        for (unsigned char value : encrypted) {
            result += static_cast<char>((value >> 4) + 0x20);
            result += static_cast<char>((value & 0x0f) + 0x20);
        }
        return result;
    }

    std::string decodePassword(const std::string& value, bool alternate) {
        if (value.size() != 48) return {};
        std::array<unsigned char, 24> encrypted = {};
        for (std::size_t index = 0; index < encrypted.size(); ++index) encrypted[index] = static_cast<unsigned char>((value[index * 2] - 0x20) << 4) | static_cast<unsigned char>(value[index * 2 + 1] - 0x20);
        const std::string plaintext = cryptPassword(encrypted, true, alternate);
        const unsigned char length = static_cast<unsigned char>(plaintext[0]);
        return length > 23 ? std::string() : plaintext.substr(1, length);
    }

    std::string decryptPassword(const std::string& value) {
        std::string result = decodePassword(value, false);
        return result.empty() ? decodePassword(value, true) : result;
    }

#ifdef _WIN32
    std::string readRegistryValue(HKEY key, const char* name) {
        std::array<char, 256> value = {};
        DWORD length = static_cast<DWORD>(value.size() - 1);
        return RegQueryValueExA(key, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(value.data()), &length) == ERROR_SUCCESS ? std::string(value.data()) : std::string();
    }

    void writeRegistryValue(HKEY key, const char* name, const std::string& value) { RegSetValueExA(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>(value.size() + 1)); }
#endif

}

namespace RC3 {

    RCAccounts::RCAccounts() {
#ifdef _WIN32
        HKEY key = nullptr;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Graal\\RemoteControl", 0, KEY_READ, &key) != ERROR_SUCCESS) return;
        activeAccount = readRegistryValue(key, "accountname");
        const std::string primaryPassword = readRegistryValue(key, "password");
        if (primaryPassword.size() == 49 && primaryPassword[0] == '2') activePassword = decryptPassword(primaryPassword.substr(1));
        accountNames = splitCommaText(readRegistryValue(key, "rc_accounts"));
        accountPasswords = splitCommaText(readRegistryValue(key, "rc_passwords"));
        RegCloseKey(key);
#endif
    }

    const std::vector<std::string>& RCAccounts::names() const { return accountNames; }
    const std::string& RCAccounts::accountName() const { return activeAccount; }
    const std::string& RCAccounts::password() const { return activePassword; }

    std::string RCAccounts::passwordFor(const std::string& accountName) const {
        const auto iterator = std::find(accountNames.begin(), accountNames.end(), accountName);
        if (iterator == accountNames.end()) return accountName == activeAccount ? activePassword : std::string();
        const std::size_t index = static_cast<std::size_t>(std::distance(accountNames.begin(), iterator));
        return index < accountPasswords.size() ? decryptPassword(fixCommas(accountPasswords[index], false)) : std::string();
    }

    void RCAccounts::save(const std::string& accountName, const std::string& password, bool dontSavePassword) {
        if (accountName.empty()) return;
        const auto iterator = std::find(accountNames.begin(), accountNames.end(), accountName);
        const std::size_t index = iterator == accountNames.end() ? accountNames.size() : static_cast<std::size_t>(std::distance(accountNames.begin(), iterator));
        if (iterator == accountNames.end()) accountNames.push_back(accountName);
        if (accountPasswords.size() <= index) accountPasswords.resize(index + 1);
        accountPasswords[index] = dontSavePassword ? std::string() : fixCommas(encodePassword(password), true);
        activeAccount = accountName;
        activePassword = dontSavePassword ? std::string() : password;
#ifdef _WIN32
        HKEY key = nullptr;
        DWORD disposition = 0;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Graal\\RemoteControl", 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition) != ERROR_SUCCESS) return;
        writeRegistryValue(key, "accountname", activeAccount);
        writeRegistryValue(key, "rc_accounts", joinCommaText(accountNames));
        writeRegistryValue(key, "rc_passwords", joinCommaText(accountPasswords));
        writeRegistryValue(key, "password", dontSavePassword ? std::string() : "2" + encodePassword(password));
        RegCloseKey(key);
#endif
    }

}
