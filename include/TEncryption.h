#pragma once
#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace RC::Encryption {
    constexpr std::size_t keySize = 32;
    using Key = std::array<unsigned char, keySize>;

    bool writePrivateFile(const std::filesystem::path& path, const std::vector<unsigned char>& data);
    bool loadOrCreateKey(const std::filesystem::path& directory, const std::string& filename, Key& key);
    bool encrypt(const std::vector<unsigned char>& plaintext, const Key& key, std::span<const unsigned char> magic, std::vector<unsigned char>& output);
    bool decrypt(const std::vector<unsigned char>& input, const Key& key, std::span<const unsigned char> magic, std::vector<unsigned char>& plaintext);
    bool encryptString(const std::string& plaintext, const Key& key, std::span<const unsigned char> magic, std::string& output);
    bool decryptString(const std::string& input, const Key& key, std::span<const unsigned char> magic, std::string& plaintext);
}
