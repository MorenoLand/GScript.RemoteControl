#pragma once

#include <functional>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct _GFileMonitor;

class TExternalEditor {
public:
    TExternalEditor(std::filesystem::path workspace, std::string command);
    ~TExternalEditor();
    void open(const std::string& server, const std::string& category, const std::string& name, const std::string& content, std::function<void(const std::string&)> onSaved);
private:
    struct Session;
    static void onChanged(_GFileMonitor*, void*, void*, int, void*);
    static int onDebounced(void*);
    std::filesystem::path workspace;
    std::string command;
    std::vector<std::unique_ptr<Session>> sessions;
};
