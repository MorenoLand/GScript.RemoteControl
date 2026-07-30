#pragma once

#include "TRCAccounts.h"
#include "TServerList.h"

#include <string>
#include <vector>

namespace RC {
std::string listServerAssociation(const SavedListServer& profile);
std::string normalizeListServerAssociation(const std::string& association, const std::vector<SavedListServer>& profiles);
std::vector<std::string> accountServerBadgeLabels(const RCAccount& account, const std::vector<SavedListServer>& profiles);
std::string accountRowMarkup(const RCAccount& account, const std::vector<SavedListServer>& profiles);
std::string accountServerDetail(const RCAccount& account, const std::vector<SavedListServer>& profiles);
}
