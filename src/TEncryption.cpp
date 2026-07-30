#include "TEncryption.h"
#include <algorithm>
#include <fstream>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {
    constexpr std::size_t nonceSize = 12;
    constexpr std::size_t tagSize = 16;
}

namespace RC::Encryption {
    bool writePrivateFile(const std::filesystem::path& path, const std::vector<unsigned char>& data) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
#ifndef _WIN32
        chmod(path.parent_path().string().c_str(), S_IRWXU);
#endif
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream || (!data.empty() && !stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size())))) return false;
        stream.close();
#ifndef _WIN32
        chmod(path.string().c_str(), S_IRUSR | S_IWUSR);
#endif
        return static_cast<bool>(stream);
    }

    bool loadOrCreateKey(const std::filesystem::path& directory, const std::string& filename, Key& key) {
        const std::filesystem::path path = directory / filename;
        std::ifstream stream(path, std::ios::binary);
        if (stream) {
            std::vector<unsigned char> existing((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            if (existing.size() == key.size()) {
                std::copy(existing.begin(), existing.end(), key.begin());
                OPENSSL_cleanse(existing.data(), existing.size());
                return true;
            }
            if (!existing.empty()) OPENSSL_cleanse(existing.data(), existing.size());
        }
        if (RAND_bytes(key.data(), static_cast<int>(key.size())) != 1) return false;
        return writePrivateFile(path, std::vector<unsigned char>(key.begin(), key.end()));
    }

    bool encrypt(const std::vector<unsigned char>& plaintext, const Key& key, std::span<const unsigned char> magic, std::vector<unsigned char>& output) {
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
        output.assign(magic.begin(), magic.end());
        output.insert(output.end(), nonce.begin(), nonce.end());
        output.insert(output.end(), tag.begin(), tag.end());
        output.insert(output.end(), ciphertext.begin(), ciphertext.end());
        return true;
    }

    bool decrypt(const std::vector<unsigned char>& input, const Key& key, std::span<const unsigned char> magic, std::vector<unsigned char>& plaintext) {
        if (input.size() < magic.size() + nonceSize + tagSize || !std::equal(magic.begin(), magic.end(), input.begin())) return false;
        const unsigned char* nonce = input.data() + magic.size();
        const unsigned char* tag = nonce + nonceSize;
        const unsigned char* ciphertext = tag + tagSize;
        const std::size_t ciphertextSize = input.size() - magic.size() - nonceSize - tagSize;
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
        if (!ok) {
            OPENSSL_cleanse(plaintext.data(), plaintext.size());
            plaintext.clear();
            return false;
        }
        plaintext.resize(static_cast<std::size_t>(written + finalWritten));
        return true;
    }

    bool encryptString(const std::string& plaintext, const Key& key, std::span<const unsigned char> magic, std::string& output) {
        std::vector<unsigned char> encrypted;
        if (!encrypt(std::vector<unsigned char>(plaintext.begin(), plaintext.end()), key, magic, encrypted)) return false;
        output.assign(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
        return true;
    }

    bool decryptString(const std::string& input, const Key& key, std::span<const unsigned char> magic, std::string& plaintext) {
        std::vector<unsigned char> decrypted;
        if (!decrypt(std::vector<unsigned char>(input.begin(), input.end()), key, magic, decrypted)) return false;
        plaintext.assign(reinterpret_cast<const char*>(decrypted.data()), decrypted.size());
        if (!decrypted.empty()) OPENSSL_cleanse(decrypted.data(), decrypted.size());
        return true;
    }
}
