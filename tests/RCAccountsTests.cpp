#include "TRCAccounts.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

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
    {
        RC::RCAccounts accounts(directory);
        assert(accounts.accountName() == "Test");
        assert(accounts.passwordFor("Deny") == "portable-secret");
        const std::vector<std::string> servers = accounts.listServersFor("Deny");
        assert(servers.size() == 2);
        assert(std::find(servers.begin(), servers.end(), "login-one.example:14922") != servers.end());
        assert(std::find(servers.begin(), servers.end(), "login-two.example:14922") != servers.end());
        std::ifstream encrypted(directory / "accounts.dat", std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(encrypted)), std::istreambuf_iterator<char>());
        assert(bytes.find("portable-secret") == std::string::npos);
        assert(bytes.find("second-secret") == std::string::npos);
        accounts.save("Deny", "must-not-persist", true, "login-one.example:14922");
    }
    {
        RC::RCAccounts accounts(directory);
        assert(accounts.accountName() == "Deny");
        assert(accounts.passwordFor("Deny").empty());
        assert(accounts.listServersFor("Deny").size() == 2);
        accounts.remove("Test");
        assert(accounts.entries().size() == 1);
    }
    std::filesystem::remove_all(directory);
    return 0;
}
