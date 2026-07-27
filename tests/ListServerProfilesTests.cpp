#include "TServerList.h"

#include <cassert>
#include <filesystem>

int main() {
    const std::filesystem::path directory = std::filesystem::current_path() / "listserver-profiles-test";
    const std::filesystem::path path = directory / "listservers.conf";
    std::filesystem::remove_all(directory);
    std::vector<SavedListServer> profiles = RC::loadListServerProfiles(path, "listserver.graalonline.com", 14922);
    assert(profiles.size() == 1);
    assert(profiles[0].name == "Official");
    profiles.push_back({"Moreno", "listserver.moreno.land", 14922});
    assert(RC::saveListServerProfiles(path, profiles));
    profiles = RC::loadListServerProfiles(path, "listserver.graalonline.com", 14922);
    assert(profiles.size() == 2);
    assert(profiles[0].name == "Official");
    assert(profiles[1].name == "Moreno");
    assert(profiles[1].host == "listserver.moreno.land");
    assert(RC::saveListServerProfiles(path, {{"Moreno", "listserver.moreno.land", 14922}}));
    profiles = RC::loadListServerProfiles(path, "listserver.graalonline.com", 14922);
    assert(profiles.size() == 2);
    assert(profiles[0].name == "Official");
    assert(profiles[1].name == "Moreno");
    std::filesystem::remove_all(directory);
    return 0;
}
