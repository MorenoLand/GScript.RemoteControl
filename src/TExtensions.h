#pragma once

#include <filesystem>
#include <gtk/gtk.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace RC {

    struct ExtensionProcess {
        GPid pid = 0;
        gint input = -1;
        gint output = -1;
        gint errorOutput = -1;
    };

    struct ExtensionManifest {
        std::filesystem::path directory;
        std::string id;
        std::string name;
        std::string version;
        std::string entry;
        std::string runtime;
        bool autoDiscover = false;
        int api = 1;
        std::string error;
    };

    bool loadExtensionManifest(const std::filesystem::path& path, ExtensionManifest& manifest);
    std::vector<ExtensionManifest> scanExtensionManifests(const std::filesystem::path& root);
    void appendExtensionLog(std::string& log, const std::string& line, std::size_t limit = 65536);
    bool spawnExtensionProcess(const std::filesystem::path& directory, const std::vector<std::string>& arguments, ExtensionProcess& process, std::string& error);

}

class TExtensionsManager {
public:
    TExtensionsManager(GtkWindow* parent, const std::filesystem::path& applicationDirectory, std::function<void(const std::string&, const std::string&)> outputCallback = {}, std::function<void(const std::string&)> closeCallback = {});
    ~TExtensionsManager();
    void showWindow();

private:
    struct AsyncState;
    struct ExtensionState;
    struct LaunchResult;
    struct ReaderLine;
    static void onEnable(GtkToggleButton*, gpointer);
    static void onOpen(GtkButton*, gpointer);
    static void onLog(GtkButton*, gpointer);
    static void onOutputTab(GtkButton*, gpointer);
    static gboolean onLaunchComplete(gpointer);
#ifdef _WIN32
    static gboolean onReaderLine(gpointer);
#endif
    static gboolean onOutput(GIOChannel*, GIOCondition, gpointer);
    static void onChildExit(GPid, gint, gpointer);
    void scan();
    void refresh();
    void setEnabled(std::size_t index, bool enabled);
    void setOutputTab(std::size_t index, bool enabled);
    void saveState();
    void routeLog(std::size_t index);
    void stop(std::size_t index);
    void launch(std::size_t index);
    void finishLaunch(std::size_t index, RC::ExtensionProcess process, const std::string& error);
    void sendRequest(std::size_t index, const std::string& request);
    void handleProtocolLine(std::size_t index, const std::string& line);
    void showLog(std::size_t index);
    std::filesystem::path statePath() const;

    GtkWindow* parent = nullptr;
    std::filesystem::path applicationDirectory;
    std::vector<ExtensionState> extensions;
    GtkWidget* window = nullptr;
    GtkWidget* list = nullptr;
    std::function<void(const std::string&, const std::string&)> outputCallback;
    std::function<void(const std::string&)> closeCallback;
    std::shared_ptr<AsyncState> asyncState;
};
