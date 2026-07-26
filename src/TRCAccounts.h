#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace RC {

    struct RCAccount {
        std::string name;
        std::string password;
        std::vector<std::string> listServers;
    };

    class RCAccounts {
    public:
        explicit RCAccounts(const std::filesystem::path& storageDirectory = {});

        const std::vector<std::string>& names() const;
        const std::vector<RCAccount>& entries() const;
        const std::string& accountName() const;
        const std::string& password() const;
        std::string passwordFor(const std::string& accountName) const;
        std::vector<std::string> listServersFor(const std::string& accountName) const;
        void save(const std::string& accountName, const std::string& password, bool dontSavePassword, const std::string& listServer = {});
        void update(const std::string& previousName, const std::string& accountName, const std::string& password, bool dontSavePassword, const std::vector<std::string>& listServers);
        void associate(const std::string& accountName, const std::string& listServer);
        void remove(const std::string& accountName);

    private:
        bool load();
        bool persist();
        void rebuildNames();

        std::filesystem::path storageDirectory;
        std::string activeAccount;
        std::string activePassword;
        std::vector<std::string> accountNames;
        std::vector<RCAccount> accountEntries;
    };

}
