#include "TExtensions.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
namespace {
    struct WindowSearch {
        DWORD processId;
        bool visible;
    };

    BOOL CALLBACK findVisibleProcessWindow(HWND window, LPARAM parameter) {
        auto* search = reinterpret_cast<WindowSearch*>(parameter);
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId == search->processId && IsWindowVisible(window)) search->visible = true;
        return TRUE;
    }
}
#endif

int main() {
    const auto root = std::filesystem::temp_directory_path() / "gscript-rc-extension-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "gs2engine");
    std::ofstream(root / "gs2engine" / "extension.ini") << "id=gs2engine\nname=GS2Engine\nversion=1.0.0\nentry=GS2Engine.exe\nruntime=\n";
    const auto manifests = RC::scanExtensionManifests(root);
    assert(manifests.size() == 1);
    assert(manifests[0].id == "gs2engine");
    std::filesystem::create_directories(root / "smoke");
    std::ofstream(root / "smoke" / "extension.json") << "{\"id\":\"test.extension\",\"name\":\"Test Extension\",\"version\":\"1.0.0\",\"publisher\":\"RC Tests\",\"runtime\":\"gs2engine-stdio\",\"entry\":\"main.gs2\",\"capabilities\":[\"script-read\",\"file-read\"],\"ui\":[\"output-tab\"],\"themes\":[\"theme.dark\"],\"commands\":[\"test.run\"],\"readOnlyViews\":[\"test.status\"],\"api\":1}";
    std::ofstream(root / "smoke" / "main.gs2") << "echo(\"RC GS2 extension smoke ready\");\n";
    const auto jsonManifests = RC::scanExtensionManifests(root);
    assert(jsonManifests.size() == 2);
    const auto smoke = std::find_if(jsonManifests.begin(), jsonManifests.end(), [](const RC::ExtensionManifest& manifest) { return manifest.id == "test.extension"; });
    assert(smoke != jsonManifests.end());
    assert(smoke->runtime == "gs2engine-stdio");
    assert(smoke->publisher == "RC Tests");
    assert(smoke->requestedCapabilities.size() == 2);
    assert(smoke->requestedCapabilities[0] == "script-read");
    assert(smoke->uiContributions.size() == 1);
    assert(smoke->uiContributions[0] == "output-tab");
    assert(smoke->themes.size() == 1 && smoke->themes[0] == "theme.dark");
    assert(smoke->commands.size() == 1 && smoke->commands[0] == "test.run");
    assert(smoke->readOnlyViews.size() == 1 && smoke->readOnlyViews[0] == "test.status");
    assert(smoke->api == 1);
    std::filesystem::create_directories(root / "auto");
    std::ofstream(root / "auto" / "extension.json") << "{\"id\":\"auto.extension\",\"name\":\"Auto Extension\",\"version\":\"1.0.0\",\"runtime\":\"gs2engine-stdio\",\"mode\":\"auto\",\"api\":1}";
    const auto autoManifests = RC::scanExtensionManifests(root);
    const auto automatic = std::find_if(autoManifests.begin(), autoManifests.end(), [](const RC::ExtensionManifest& manifest) { return manifest.id == "auto.extension"; });
    assert(automatic != autoManifests.end());
    assert(automatic->autoDiscover);
    assert(automatic->entry.empty());
    std::filesystem::create_directories(root / "broken");
    std::ofstream(root / "broken" / "extension.json") << "{\"id\":\"broken.extension\",\"name\":\"Broken Extension\"}";
    const auto manifestsWithFailure = RC::scanExtensionManifests(root);
    const auto broken = std::find_if(manifestsWithFailure.begin(), manifestsWithFailure.end(), [](const RC::ExtensionManifest& manifest) { return manifest.id == "broken.extension"; });
    assert(broken != manifestsWithFailure.end());
    assert(!broken->error.empty());
    std::string log;
    RC::appendExtensionLog(log, "one", 8);
    RC::appendExtensionLog(log, "two", 8);
    assert(log.size() <= 8);
#ifdef _WIN32
    const char* hostValue = std::getenv("GS2ENGINE_EXTENSION_HOST");
    const auto entryPath = root / "smoke" / "main.gs2";
    if (hostValue != nullptr && std::filesystem::exists(hostValue) && std::filesystem::exists(entryPath)) {
        RC::ExtensionProcess process;
        std::string error;
        assert(RC::spawnExtensionProcess(std::filesystem::path(hostValue).parent_path(), {hostValue, "--extension-stdio"}, process, error));
        WindowSearch search{GetProcessId(reinterpret_cast<HANDLE>(process.pid)), false};
        EnumWindows(findVisibleProcessWindow, reinterpret_cast<LPARAM>(&search));
        assert(!search.visible);
        GIOChannel* input = g_io_channel_unix_new(process.input);
        GIOChannel* output = g_io_channel_unix_new(process.output);
        GIOChannel* errorOutput = g_io_channel_unix_new(process.errorOutput);
        g_io_channel_set_encoding(input, "UTF-8", nullptr);
        g_io_channel_set_encoding(output, "UTF-8", nullptr);
        gchar* line = nullptr;
        gsize length = 0;
        assert(g_io_channel_read_line(output, &line, &length, nullptr, nullptr) == G_IO_STATUS_NORMAL);
        assert(std::string(line).find("\"type\":\"ready\"") != std::string::npos);
        g_free(line);
        std::string escapedEntry;
        for (char character : entryPath.string()) escapedEntry += character == '\\' ? "\\\\" : std::string(1, character);
        const std::string request = "{\"id\":\"1\",\"method\":\"start\",\"params\":{\"extensionId\":\"test.extension\",\"entry\":\"" + escapedEntry + "\"}}\n";
        gsize written = 0;
        g_io_channel_write_chars(input, request.c_str(), static_cast<gssize>(request.size()), &written, nullptr);
        g_io_channel_flush(input, nullptr);
        bool receivedSmoke = false;
        for (int count = 0; count < 8 && !receivedSmoke; ++count) {
            line = nullptr;
            if (g_io_channel_read_line(output, &line, &length, nullptr, nullptr) == G_IO_STATUS_NORMAL) { receivedSmoke = std::string(line).find("RC GS2 extension smoke ready") != std::string::npos; g_free(line); }
        }
        assert(receivedSmoke);
        const char shutdown[] = "{\"id\":\"2\",\"method\":\"shutdown\"}\n";
        g_io_channel_write_chars(input, shutdown, -1, &written, nullptr);
        g_io_channel_flush(input, nullptr);
        assert(WaitForSingleObject(reinterpret_cast<HANDLE>(process.pid), 5000) == WAIT_OBJECT_0);
        g_io_channel_unref(input);
        g_io_channel_unref(output);
        g_io_channel_unref(errorOutput);
        g_spawn_close_pid(process.pid);
    }
#endif
    std::filesystem::remove_all(root);
}
