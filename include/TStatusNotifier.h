#pragma once

#include <functional>
#include <string>
#include <vector>

namespace RC::StatusNotifier {

struct MenuEntry {
    int id = 0;
    std::string label;
    bool enabled = true;
    bool separator = false;
};

struct Handlers {
    std::function<void()> activate;
    std::function<std::vector<MenuEntry>()> menu;
    std::function<void(int)> menuActivated;
};

bool start(const std::string& iconPath, const std::string& tooltip, Handlers handlers);
bool active();
void setIcon(const std::string& iconPath);
void setTooltip(const std::string& tooltip);
void stop();

}
