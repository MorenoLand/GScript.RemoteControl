#include "TServerList.h"

#include <cstdlib>
#include <fstream>

namespace RC {
std::vector<SavedListServer> loadListServerProfiles(const std::filesystem::path& path, const std::string& defaultHost, int defaultPort) {
    std::vector<SavedListServer> endpoints;
    std::ifstream stream(path);
    std::string line;
    while (std::getline(stream, line)) {
        const std::size_t first = line.find('\t');
        const std::size_t second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (first != std::string::npos && second != std::string::npos) {
            const int port = std::atoi(line.substr(second + 1).c_str());
            if (port > 0 && port <= 65535) endpoints.push_back({line.substr(0, first), line.substr(first + 1, second - first - 1), port});
            continue;
        }
        const std::size_t separator = line.rfind(':');
        if (separator == std::string::npos) continue;
        const int port = std::atoi(line.substr(separator + 1).c_str());
        const std::string host = line.substr(0, separator);
        if (!host.empty() && port > 0 && port <= 65535) endpoints.push_back({host, host, port});
    }
    bool hasOfficial = false;
    for (const SavedListServer& endpoint : endpoints) if (endpoint.name == "Official" && endpoint.host == defaultHost && endpoint.port == defaultPort) hasOfficial = true;
    if (!hasOfficial) endpoints.insert(endpoints.begin(), {"Official", defaultHost, defaultPort});
    return endpoints;
}

bool saveListServerProfiles(const std::filesystem::path& path, const std::vector<SavedListServer>& endpoints) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) return false;
    for (const SavedListServer& endpoint : endpoints) stream << endpoint.name << '\t' << endpoint.host << '\t' << endpoint.port << '\n';
    return stream.good();
}
}
