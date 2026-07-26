#pragma once

#include <string>
#include <vector>

namespace RC {

    class RCAccounts {
    public:
        RCAccounts();

        const std::vector<std::string>& names() const;
        const std::string& accountName() const;
        const std::string& password() const;
        std::string passwordFor(const std::string& accountName) const;
        void save(const std::string& accountName, const std::string& password, bool dontSavePassword);

    private:
        std::string activeAccount;
        std::string activePassword;
        std::vector<std::string> accountNames;
        std::vector<std::string> accountPasswords;
    };

}
