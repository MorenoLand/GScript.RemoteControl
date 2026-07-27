#include "TRCAccounts.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

std::vector<unsigned char> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

int main() {
    const std::filesystem::path directory = std::filesystem::current_path() / "account-storage-test";
    std::filesystem::remove_all(directory);
    {
        RC::RCAccounts accounts(directory);
        accounts.save("Deny", "portable-secret", false, "login-one.example:14922");
        accounts.associate("Deny", "login-two.example:14922");
        accounts.update("Deny", "Deny", "portable-secret", false, {"login-one.example:14922", "login-two.example:14922"});
        accounts.save("Test", "second-secret", false, "login-two.example:14922");
        assert(accounts.entries().size() == 2);
    }
    const std::vector<unsigned char> firstCiphertext = readBytes(directory / "accounts.dat");
    {
        RC::RCAccounts accounts(directory);
        assert(accounts.accountName() == "Test");
        assert(accounts.passwordFor("Deny") == "portable-secret");
        const std::vector<std::string> servers = accounts.listServersFor("Deny");
        assert(servers.size() == 1);
        assert(std::find(servers.begin(), servers.end(), "login-one.example:14922") != servers.end());
        assert(std::find(servers.begin(), servers.end(), "login-two.example:14922") == servers.end());
        const std::vector<unsigned char> encrypted = readBytes(directory / "accounts.dat");
        const std::string bytes(encrypted.begin(), encrypted.end());
        assert(bytes.find("portable-secret") == std::string::npos);
        assert(bytes.find("second-secret") == std::string::npos);
        accounts.update("Test", "Test", "second-secret", false, {"login-two.example:14922"});
        const std::vector<unsigned char> secondCiphertext = readBytes(directory / "accounts.dat");
        assert(firstCiphertext != secondCiphertext);
        assert(secondCiphertext.size() == firstCiphertext.size());
        accounts.save("Deny", "must-not-persist", true, "login-one.example:14922");
    }
    {
        RC::RCAccounts accounts(directory);
        assert(accounts.accountName() == "Deny");
        assert(accounts.passwordFor("Deny").empty());
        assert(accounts.listServersFor("Deny").size() == 1);
        accounts.remove("Test");
        assert(accounts.entries().size() == 1);
    }
    const std::filesystem::path tamperDirectory = std::filesystem::current_path() / "account-storage-tamper-test";
    std::filesystem::remove_all(tamperDirectory);
    {
        RC::RCAccounts accounts(tamperDirectory);
        accounts.save("Tamper", "authenticated-secret", false, "Retail — listserver.graalonline.com:14922");
    }
    std::vector<unsigned char> tampered = readBytes(tamperDirectory / "accounts.dat");
    assert(!tampered.empty());
    tampered.back() ^= 0x5a;
    {
        std::ofstream stream(tamperDirectory / "accounts.dat", std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(tampered.data()), static_cast<std::streamsize>(tampered.size()));
    }
    {
        RC::RCAccounts accounts(tamperDirectory);
        assert(accounts.entries().empty());
        assert(accounts.passwordFor("Tamper").empty());
    }
    const std::filesystem::path duplicateDirectory = std::filesystem::current_path() / "account-duplicate-test";
    std::filesystem::remove_all(duplicateDirectory);
    {
        RC::RCAccounts accounts(duplicateDirectory);
        accounts.save("Twin", "first", false, "retail.example:14922");
        accounts.update("", "Twin", "second", false, {"moreno.example:14922"});
        assert(accounts.entries().size() == 2);
        assert(accounts.idForIndex(0) != 0);
        assert(accounts.idForIndex(1) != 0);
        assert(accounts.idForIndex(0) != accounts.idForIndex(1));
        accounts.updateAt(1, "Twin", "second", false, {"moreno.example:14922"});
    }
    {
        RC::RCAccounts accounts(duplicateDirectory);
        const std::uint64_t retailId = accounts.idForIndex(0);
        const std::uint64_t morenoId = accounts.idForIndex(1);
        assert(accounts.activeIndex() == 1);
        assert(accounts.activeId() == morenoId);
        assert(accounts.indexForId(retailId) == 0);
        assert(accounts.indexForId(morenoId) == 1);
        assert(accounts.passwordForIndex(0) == "first");
        assert(accounts.passwordForIndex(1) == "second");
        assert(accounts.listServersForIndex(1).front() == "moreno.example:14922");
        accounts.updateAt(1, "Twin", "first", false, {"retail.example:14922"});
    }
    {
        RC::RCAccounts accounts(duplicateDirectory);
        assert(accounts.entries().size() == 1);
        assert(accounts.idForIndex(0) != 0);
        assert(accounts.activeId() == accounts.idForIndex(0));
        assert(accounts.passwordForIndex(0) == "first");
        assert(accounts.listServersForIndex(0).front() == "retail.example:14922");
    }
    std::filesystem::remove_all(directory);
    std::filesystem::remove_all(tamperDirectory);
    std::filesystem::remove_all(duplicateDirectory);
    return 0;
}
