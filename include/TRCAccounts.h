#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>

namespace RC {

    struct RCAccount {
        std::uint64_t id = 0;
        std::string name;
        std::string password;
        std::vector<std::string> listServers;
        bool directMode = false;
    };

    class RCAccounts {
    public:
        explicit RCAccounts(const std::filesystem::path& storageDirectory = {});

        const std::vector<std::string>& names() const;
        const std::vector<RCAccount>& entries() const;
        const std::string& accountName() const;
        std::size_t activeIndex() const;
        std::uint64_t activeId() const;
        std::uint64_t idForIndex(std::size_t index) const;
        std::size_t indexForId(std::uint64_t id) const;
        const std::string& password() const;
        std::string passwordFor(const std::string& accountName) const;
        std::string passwordForIndex(std::size_t index) const;
        std::vector<std::string> listServersFor(const std::string& accountName) const;
        std::vector<std::string> listServersForIndex(std::size_t index) const;
        void save(const std::string& accountName, const std::string& password, bool dontSavePassword, const std::string& listServer = {}, bool directMode = false);
        void saveAt(std::size_t index, const std::string& password, bool dontSavePassword, const std::string& listServer = {}, bool directMode = false);
        void update(const std::string& previousName, const std::string& accountName, const std::string& password, bool dontSavePassword, const std::vector<std::string>& listServers, bool directMode = false);
        void updateAt(std::size_t index, const std::string& accountName, const std::string& password, bool dontSavePassword, const std::vector<std::string>& listServers, bool directMode = false);
        void associate(const std::string& accountName, const std::string& listServer);
        void remove(const std::string& accountName);
        void removeAt(std::size_t index);

    private:
        bool load();
        bool persist();
        void rebuildNames();

        std::filesystem::path storageDirectory;
        std::string activeAccount;
        std::string activePassword;
        std::vector<std::string> accountNames;
        std::vector<RCAccount> accountEntries;
        std::size_t activeAccountIndex = static_cast<std::size_t>(-1);
        std::uint64_t activeAccountId = 0;
        std::uint64_t nextAccountId = 1;
    };

}
