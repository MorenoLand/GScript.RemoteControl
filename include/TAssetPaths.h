#pragma once

#include <filesystem>
#include <glib.h>
#include <vector>

inline std::filesystem::path resolveRuntimeImage(const std::filesystem::path& applicationDirectory, const char* name) {
    std::vector<std::filesystem::path> candidates;
    candidates.push_back(applicationDirectory / "images" / name);
    if (const char* appDir = g_getenv("APPDIR"); appDir != nullptr && *appDir != '\0') candidates.push_back(std::filesystem::path(appDir) / "images" / name);
    candidates.push_back(applicationDirectory.parent_path() / "images" / name);
    candidates.push_back(std::filesystem::current_path() / "images" / name);
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
    }
    return candidates.front();
}
