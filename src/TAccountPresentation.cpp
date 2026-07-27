#include "TAccountPresentation.h"

#include <algorithm>
#include <cctype>
#include <glib.h>

namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

std::string endpoint(const SavedListServer& profile) { return profile.host + ":" + std::to_string(profile.port); }

std::string escaped(const std::string& value) {
    gchar* text = g_markup_escape_text(value.c_str(), static_cast<gssize>(value.size()));
    std::string result = text == nullptr ? std::string() : text;
    g_free(text);
    return result;
}

void appendUnique(std::vector<std::string>& values, const std::string& value) {
    const std::string target = lower(value);
    if (std::none_of(values.begin(), values.end(), [&](const std::string& current) { return lower(current) == target; })) values.push_back(value);
}
}

namespace RC {
std::string listServerAssociation(const SavedListServer& profile) { return profile.name + " — " + endpoint(profile); }

std::string normalizeListServerAssociation(const std::string& association, const std::vector<SavedListServer>& profiles) {
    const std::string target = lower(association);
    for (const SavedListServer& profile : profiles) {
        const std::string profileEndpoint = lower(endpoint(profile));
        if (target == lower(profile.name) || target == profileEndpoint || target == lower(listServerAssociation(profile)) || (target.size() > profileEndpoint.size() && target.ends_with(" — " + profileEndpoint))) return listServerAssociation(profile);
    }
    return association;
}

std::vector<std::string> accountServerBadgeLabels(const RCAccount& account, const std::vector<SavedListServer>& profiles) {
    std::vector<std::string> labels;
    if (!account.listServers.empty()) {
        const std::string normalized = normalizeListServerAssociation(account.listServers.front(), profiles);
        const std::size_t separator = normalized.find(" — ");
        appendUnique(labels, separator == std::string::npos ? normalized : normalized.substr(0, separator));
    }
    if (labels.empty()) labels.push_back("Unassigned");
    return labels;
}

std::string accountRowMarkup(const RCAccount& account, const std::vector<SavedListServer>& profiles) {
    std::string markup = "<b>" + escaped(account.name) + "</b>";
    for (const std::string& badge : accountServerBadgeLabels(account, profiles)) markup += "  <span size=\"small\" weight=\"semibold\">[" + escaped(badge) + "]</span>";
    return markup;
}

std::string accountServerDetail(const RCAccount& account, const std::vector<SavedListServer>& profiles) {
    std::string detail = account.name + " — ";
    if (account.listServers.empty()) return detail + "no saved list server";
    for (std::size_t index = 0; index < account.listServers.size(); ++index) {
        if (index != 0) detail += ", ";
        detail += normalizeListServerAssociation(account.listServers[index], profiles);
    }
    return detail;
}
}
