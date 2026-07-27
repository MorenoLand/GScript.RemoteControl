#include "TServerList.h"

#include <cstdlib>
#include <filesystem>

namespace {
void require(bool condition) { if (!condition) std::abort(); }
}

int main() {
    const std::filesystem::path directory = std::filesystem::current_path() / "listserver-profiles-test";
    const std::filesystem::path path = directory / "listservers.conf";
    std::filesystem::remove_all(directory);
    std::vector<SavedListServer> profiles = RC::loadListServerProfiles(path, "listserver.graalonline.com", 14922);
    require(profiles.size() == 1);
    require(profiles[0].name == "Retail");
    profiles.push_back({"Moreno", "listserver.moreno.land", 14922});
    require(RC::saveListServerProfiles(path, profiles));
    profiles = RC::loadListServerProfiles(path, "listserver.graalonline.com", 14922);
    require(profiles.size() == 2);
    require(profiles[0].name == "Retail");
    require(profiles[1].name == "Moreno");
    require(RC::saveListServerProfiles(path, {{"Official", "listserver.graalonline.com", 14922}, {"Moreno", "listserver.moreno.land", 14922}}));
    profiles = RC::loadListServerProfiles(path, "listserver.graalonline.com", 14922);
    require(profiles.size() == 2);
    require(profiles[0].name == "Retail");
    require(profiles[1].name == "Moreno");
    require(profiles[1].host == "listserver.moreno.land");
    require(RC::saveListServerProfiles(path, {{"Moreno", "listserver.moreno.land", 14922}}));
    profiles = RC::loadListServerProfiles(path, "listserver.graalonline.com", 14922);
    require(profiles.size() == 2);
    require(profiles[0].name == "Retail");
    require(profiles[1].name == "Moreno");
    std::filesystem::remove_all(directory);
    return 0;
}
