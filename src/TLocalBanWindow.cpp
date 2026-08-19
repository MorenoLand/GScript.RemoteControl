#include "TLocalBanWindow.h"

#include <grclib.h>

#include <chrono>
#include <cstdlib>
#include <map>
#include <sstream>

namespace {
    std::string trimBanName(std::string value) {
        const size_t first = value.find_first_not_of(" \t\r\"");
        if (first == std::string::npos) return "";
        const size_t last = value.find_last_not_of(" \t\r\"");
        return value.substr(first, last - first + 1);
    }
    std::string banTimeText(long long seconds) {
        if (seconds <= 0) return "-";
        if (seconds >= 315360000) return "unlimited";
        const long long days = seconds / 86400;
        if (days > 0) return std::to_string(days) + (days == 1 ? " day" : " days");
        const long long hours = seconds / 3600;
        if (hours > 0) return std::to_string(hours) + (hours == 1 ? " hour" : " hours");
        const long long minutes = seconds / 60;
        return std::to_string(minutes) + (minutes == 1 ? " min" : " mins");
    }
}

TLocalBanWindow::TLocalBanWindow(const std::filesystem::path& nextApplicationDirectory) : applicationDirectory(nextApplicationDirectory) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "BanWindow");
    gtk_window_set_title(GTK_WINDOW(window), "Edit Access");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 240);
    GdkGeometry banGeometry{};
    banGeometry.max_width = 600;
    banGeometry.max_height = G_MAXINT;
    gtk_window_set_geometry_hints(GTK_WINDOW(window), nullptr, &banGeometry, static_cast<GdkWindowHints>(GDK_HINT_MAX_SIZE));
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* notebook = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(notebook), 5);
    const char* titles[] = {"Local Ban", "Global Ban", "Computer Ban", "Global Computer Ban"};
    for (int index = 0; index < 4; ++index) {
        GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        scopes[index].page = page;
        gtk_container_set_border_width(GTK_CONTAINER(page), 5);
        scopes[index].banned = gtk_check_button_new_with_label("Banned for");
        scopes[index].type = gtk_combo_box_text_new();
        scopes[index].timeLeft = gtk_label_new("Ban time left: -");
        scopes[index].reset = gtk_check_button_new_with_label("Reset ban time");
        scopes[index].reason = gtk_entry_new();
        GtkWidget* banRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        gtk_container_set_border_width(GTK_CONTAINER(scopes[index].banned), 5);
        gtk_box_pack_start(GTK_BOX(banRow), scopes[index].banned, false, false, 0);
        gtk_box_pack_end(GTK_BOX(banRow), scopes[index].type, true, true, 0);
        GtkWidget* timeRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        gtk_box_pack_start(GTK_BOX(timeRow), scopes[index].timeLeft, true, true, 0);
        gtk_container_set_border_width(GTK_CONTAINER(scopes[index].reset), 5);
        gtk_box_pack_end(GTK_BOX(timeRow), scopes[index].reset, false, false, 0);
        GtkWidget* reasonRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        GtkWidget* reasonLabel = gtk_label_new("Reason for update:");
        gtk_widget_set_size_request(reasonLabel, 120, -1);
        gtk_widget_set_halign(reasonLabel, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(reasonRow), reasonLabel, false, false, 0);
        gtk_box_pack_end(GTK_BOX(reasonRow), scopes[index].reason, true, true, 0);
        GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_container_set_border_width(GTK_CONTAINER(buttons), 5);
        gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
        GtkWidget* apply = gtk_button_new_with_label("Apply");
        g_object_set_data(G_OBJECT(apply), "scope", GINT_TO_POINTER(index));
        gtk_container_add(GTK_CONTAINER(buttons), apply);
        gtk_box_pack_start(GTK_BOX(page), banRow, false, false, 0);
        gtk_box_pack_start(GTK_BOX(page), timeRow, false, false, 0);
        gtk_box_pack_start(GTK_BOX(page), reasonRow, false, false, 0);
        gtk_box_pack_start(GTK_BOX(page), buttons, false, false, 0);
        g_object_set_data(G_OBJECT(scopes[index].type), "scope", GINT_TO_POINTER(index));
        g_signal_connect(scopes[index].type, "changed", G_CALLBACK(onBanTypeChanged), this);
        g_signal_connect(scopes[index].banned, "toggled", G_CALLBACK(onBannedChanged), this);
        g_signal_connect(apply, "clicked", G_CALLBACK(onApply), this);
        GtkWidget* tab = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        scopes[index].tab = tab;
        scopes[index].tabIcon = gtk_image_new_from_file((applicationDirectory / "images" / "rcicon_unbanned.png").string().c_str());
        gtk_box_pack_start(GTK_BOX(tab), scopes[index].tabIcon, false, false, 0);
        gtk_box_pack_start(GTK_BOX(tab), gtk_label_new(titles[index]), false, false, 0);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, tab);
        gtk_widget_show_all(tab);
    }
    GtkCssProvider* banTabs = gtk_css_provider_new();
    gtk_css_provider_load_from_data(banTabs, "#BanWindow notebook > header { border-bottom: 1px solid #777777; } #BanWindow notebook > header > tabs > tab { border: 1px solid #777777; border-bottom: 0; border-radius: 4px 4px 0 0; margin-right: 3px; padding: 4px 8px; } #BanWindow notebook > header > tabs > tab:checked { border-color: #aaaaaa; margin-bottom: -1px; } #BanWindow notebook > stack { border: 1px solid #777777; border-top: 0; }", -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(banTabs), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(banTabs);
    gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_set_border_width(GTK_CONTAINER(buttons), 5);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* history = gtk_button_new_with_label("Ban History");
    GtkWidget* activity = gtk_button_new_with_label("Staff Activity");
    GtkWidget* close = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), history);
    gtk_container_add(GTK_CONTAINER(buttons), activity);
    gtk_container_add(GTK_CONTAINER(buttons), close);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 0);
    g_signal_connect(history, "clicked", G_CALLBACK(onBanHistory), this);
    g_signal_connect(activity, "clicked", G_CALLBACK(onStaffActivity), this);
    g_signal_connect(close, "clicked", G_CALLBACK(onCancel), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TLocalBanWindow::~TLocalBanWindow() { if (window != nullptr) gtk_widget_destroy(window); }

void TLocalBanWindow::setUseNewBanType(bool enabled) { useNewBanType = enabled; }

void TLocalBanWindow::setBanTypes(const char* types) {
    banTypes.clear();
    banDurations.clear();
    std::istringstream input(types == nullptr ? "" : types);
    for (std::string type; std::getline(input, type);) {
        if (type.empty()) continue;
        const size_t comma = type.rfind(',');
        banTypes.push_back(trimBanName(comma == std::string::npos ? type : type.substr(0, comma)));
        banDurations.push_back(comma == std::string::npos ? 0 : std::atoi(type.c_str() + comma + 1));
    }
    for (int index = 0; index < 4; ++index) {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(scopes[index].type));
        for (size_t type = 0; type < banTypes.size(); ++type) {
            const std::string label = banTypes[type] + " (" + banTimeText(banDurations[type]) + ")";
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(scopes[index].type), banTypes[type].c_str(), label.c_str());
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(scopes[index].type), banTypes.empty() ? -1 : 0);
    }
}

void TLocalBanWindow::updateTimeLeft(int scope) {
    Scope& entry = scopes[scope];
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(entry.banned))) { gtk_label_set_text(GTK_LABEL(entry.timeLeft), "Ban time left: -"); return; }
    const int type = gtk_combo_box_get_active(GTK_COMBO_BOX(entry.type));
    if (type < 0 || type >= static_cast<int>(banDurations.size())) { gtk_label_set_text(GTK_LABEL(entry.timeLeft), "Ban time left: -"); return; }
    const long long seconds = banDurations[type];
    gtk_label_set_text(GTK_LABEL(entry.timeLeft), ("Ban time left: " + banTimeText(seconds)).c_str());
}

void TLocalBanWindow::updateTabIcon(int scope) {
    const std::filesystem::path icon = applicationDirectory / "images" / (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scopes[scope].banned)) ? "rcicon_banned.png" : "rcicon_unbanned.png");
    gtk_image_set_from_file(GTK_IMAGE(scopes[scope].tabIcon), icon.string().c_str());
}

void TLocalBanWindow::open(void* nextConnection, const std::string& nextAccount, const std::string& nextComputerId, const std::string& details) {
    connection = nextConnection;
    account = nextAccount;
    computerId = nextComputerId;
    gtk_window_set_title(GTK_WINDOW(window), ("Edit Access of " + account + (computerId.empty() ? "" : " (computer: " + computerId + ")")).c_str());
    for (int index = 0; index < 4; ++index) {
        scopes[index].target = index < 2 ? account : (computerId.empty() ? "" : "pc:" + computerId);
        const bool available = useNewBanType ? !scopes[index].target.empty() : index == 0 && !account.empty();
        gtk_widget_set_visible(scopes[index].page, available);
        gtk_widget_set_visible(scopes[index].tab, available);
        gtk_widget_set_sensitive(scopes[index].banned, !scopes[index].target.empty());
        gtk_widget_set_sensitive(scopes[index].type, !scopes[index].target.empty());
        gtk_widget_set_sensitive(scopes[index].reset, !scopes[index].target.empty());
        gtk_widget_set_sensitive(scopes[index].reason, !scopes[index].target.empty());
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scopes[index].banned), false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scopes[index].reset), false);
        gtk_combo_box_set_active(GTK_COMBO_BOX(scopes[index].type), banTypes.empty() ? -1 : 0);
        gtk_entry_set_text(GTK_ENTRY(scopes[index].reason), "");
        scopes[index].releaseTime.clear();
        gtk_label_set_text(GTK_LABEL(scopes[index].timeLeft), "Ban time left: -");
        updateTabIcon(index);
    }
    if (!useNewBanType) {
        bool banned = false;
        std::istringstream records(details);
        for (std::string record; std::getline(records, record);) {
            const size_t separator = record.find('=');
            if (separator == std::string::npos) continue;
            const std::string key = record.substr(0, separator);
            const std::string value = record.substr(separator + 1);
            if (key == "banned") banned = value == "1" || value == "true";
        }
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scopes[0].banned), banned);
        updateTimeLeft(0);
        updateTabIcon(0);
    } else {
    std::istringstream records(details);
    for (std::string record; std::getline(records, record);) {
        std::map<std::string, std::string> fields;
        std::istringstream values(record);
        for (std::string value; std::getline(values, value, ',');) {
            const size_t separator = value.find('=');
            if (separator != std::string::npos) fields[value.substr(0, separator)] = value.substr(separator + 1);
        }
        const auto target = fields.find("account");
        const auto world = fields.find("world");
        if (target == fields.end() || world == fields.end()) continue;
        const bool computer = target->second.rfind("pc:", 0) == 0;
        const int index = (computer ? 2 : 0) + (world->second == "all" ? 1 : 0);
        if (scopes[index].target.empty()) continue;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scopes[index].banned), true);
        const auto banType = fields.find("bantype");
        if (banType != fields.end()) for (int type = 0; type < static_cast<int>(banTypes.size()); ++type) if (banTypes[type] == trimBanName(banType->second)) gtk_combo_box_set_active(GTK_COMBO_BOX(scopes[index].type), type);
        const auto release = fields.find("releasetime");
        if (release != fields.end()) scopes[index].releaseTime = release->second;
        const auto reason = fields.find("reason");
        if (reason != fields.end()) gtk_entry_set_text(GTK_ENTRY(scopes[index].reason), reason->second.c_str());
        updateTimeLeft(index);
        updateTabIcon(index);
    }
    }
    gtk_widget_show_all(window);
    for (int index = 0; index < 4; ++index) {
        const bool available = useNewBanType ? !scopes[index].target.empty() : index == 0 && !account.empty();
        gtk_widget_set_visible(scopes[index].page, available);
        gtk_widget_set_visible(scopes[index].tab, available);
    }
    gtk_window_present(GTK_WINDOW(window));
}

void TLocalBanWindow::onApply(GtkButton* button, gpointer data) {
    TLocalBanWindow* editor = static_cast<TLocalBanWindow*>(data);
    const int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "scope"));
    Scope& scope = editor->scopes[index];
    if (scope.target.empty()) return;
    const bool banned = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scope.banned));
    const bool reset = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scope.reset));
    const char* type = gtk_combo_box_get_active_id(GTK_COMBO_BOX(scope.type));
    const char* world = (index & 1) == 0 ? "local" : "all";
    if (!editor->useNewBanType) {
        if (index != 0 || editor->account.empty()) return;
        rc_set_legacy_player_ban(editor->connection, editor->account.c_str(), banned, gtk_entry_get_text(GTK_ENTRY(scope.reason)));
    } else rc_set_ban(editor->connection, scope.target.c_str(), world, banned, type == nullptr ? "" : type, reset ? "" : scope.releaseTime.c_str(), gtk_entry_get_text(GTK_ENTRY(scope.reason)));
    gtk_widget_hide(editor->window);
}

void TLocalBanWindow::onCancel(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TLocalBanWindow*>(data)->window); }
void TLocalBanWindow::onBannedChanged(GtkToggleButton* button, gpointer data) { TLocalBanWindow* editor = static_cast<TLocalBanWindow*>(data); for (int index = 0; index < 4; ++index) if (editor->scopes[index].banned == GTK_WIDGET(button)) { editor->updateTimeLeft(index); editor->updateTabIcon(index); break; } }
void TLocalBanWindow::onBanTypeChanged(GtkComboBox* combo, gpointer data) { static_cast<TLocalBanWindow*>(data)->updateTimeLeft(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(combo), "scope"))); }
void TLocalBanWindow::onBanHistory(GtkButton*, gpointer data) {
    TLocalBanWindow* editor = static_cast<TLocalBanWindow*>(data);
    const std::string target = editor->account.empty() ? "pc:" + editor->computerId : editor->account;
    if (!target.empty()) rc_request_ban_history(editor->connection, target.c_str());
}
void TLocalBanWindow::onStaffActivity(GtkButton*, gpointer data) {
    TLocalBanWindow* editor = static_cast<TLocalBanWindow*>(data);
    if (!editor->account.empty()) rc_request_staff_activity(editor->connection, editor->account.c_str());
}
gboolean TLocalBanWindow::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TLocalBanWindow*>(data)->window); return true; }
