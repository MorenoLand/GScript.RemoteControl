#include <glib.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {
void require(bool condition) { if (!condition) std::abort(); }
}

int main() {
    const char* windows = g_getenv("WINDIR");
    require(windows != nullptr);
    const auto spawn = [](std::vector<std::string> storage) {
        std::vector<gchar*> arguments;
        for (std::string& value : storage) arguments.push_back(value.data());
        arguments.push_back(nullptr);
        gchar* standardOutput = nullptr;
        gchar* standardError = nullptr;
        gint status = 0;
        GError* error = nullptr;
        const gboolean launched = g_spawn_sync(nullptr, arguments.data(), nullptr, static_cast<GSpawnFlags>(0), nullptr, nullptr, &standardOutput, &standardError, &status, &error);
        require(launched);
        require(error == nullptr);
        require(g_spawn_check_wait_status(status, nullptr));
        g_free(standardOutput);
        g_free(standardError);
    };
    spawn({(std::filesystem::path(windows) / "System32" / "cmd.exe").string(), "/c", "exit", "0"});
    gchar* git = g_find_program_in_path("git");
    require(git != nullptr);
    const std::filesystem::path workspace = std::filesystem::current_path() / "git-spawn-runtime-test";
    std::filesystem::remove_all(workspace);
    std::filesystem::create_directories(workspace);
    spawn({git, "-C", workspace.string(), "init"});
    require(std::filesystem::is_directory(workspace / ".git"));
    std::filesystem::remove_all(workspace);
    g_free(git);
    return 0;
}
