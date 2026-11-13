#pragma once

#include <filesystem>
#include <system_error>

inline bool rcExists(const std::filesystem::path& path) { std::error_code error; return std::filesystem::exists(path, error); }
inline bool rcIsRegularFile(const std::filesystem::path& path) { std::error_code error; return std::filesystem::is_regular_file(path, error); }
inline bool rcIsDirectory(const std::filesystem::path& path) { std::error_code error; return std::filesystem::is_directory(path, error); }
inline void rcCreateDirectories(const std::filesystem::path& path) { std::error_code error; std::filesystem::create_directories(path, error); }
inline void rcRemove(const std::filesystem::path& path) { std::error_code error; std::filesystem::remove(path, error); }
