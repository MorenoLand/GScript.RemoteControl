#include "TLocalBanWindow.h"

#include <grclib.h>

#include <chrono>
#include <cstdlib>
#include <map>
#include <sstream>

namespace {
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

TLocalBanWindow::TLocalBanWindow() {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "BanWindow");
    gtk_window_set_title(GTK_WINDOW(window), "Edit Access");
    gtk_window_set_default_size(GTK_WINDOW(window), 430, 250);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* notebook = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(notebook), 5);
    const char* titles[] = {"Local Ban", "Global Ban", "Computer Ban", "Global Computer Ban"};
    for (int index = 0; index < 4; ++index) {
        GtkWidget* page = gtk_grid_new();
        gtk_container_set_border_width(GTK_CONTAINER(page), 5);
        gtk_grid_set_row_spacing(GTK_GRID(page), 6);
        gtk_grid_set_column_spacing(GTK_GRID(page), 8);
        scopes[index].banned = gtk_check_button_new_with_label("Banned for");
        scopes[index].type = gtk_combo_box_text_new();
        scopes[index].timeLeft = gtk_label_new("Ban time left: -");
        scopes[index].reset = gtk_check_button_new_with_label("Reset ban time");
        scopes[index].reason = gtk_entry_new();
        gtk_grid_attach(GTK_GRID(page), scopes[index].banned, 0, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(page), scopes[index].type, 1, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(page), scopes[index].timeLeft, 0, 1, 2, 1);
        gtk_grid_attach(GTK_GRID(page), scopes[index].reset, 1, 1, 1, 1);
        gtk_grid_attach(GTK_GRID(page), gtk_label_new("Reason for update:"), 0, 2, 1, 1);
        gtk_grid_attach(GTK_GRID(page), scopes[index].reason, 1, 2, 1, 1);
        gtk_widget_set_hexpand(scopes[index].reason, true);
        g_object_set_data(G_OBJECT(scopes[index].type), "scope", GINT_TO_POINTER(index));
        g_signal_connect(scopes[index].type, "changed", G_CALLBACK(onBanTypeChanged), this);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, gtk_label_new(titles[index]));
    }
    gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_set_border_width(GTK_CONTAINER(buttons), 5);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* history = gtk_button_new_with_label("Ban History");
    GtkWidget* activity = gtk_button_new_with_label("Staff Activity");
    GtkWidget* apply = gtk_button_new_with_label("Apply");
    GtkWidget* close = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), history);
    gtk_container_add(GTK_CONTAINER(buttons), activity);
    gtk_container_add(GTK_CONTAINER(buttons), apply);
    gtk_container_add(GTK_CONTAINER(buttons), close);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 0);
    g_signal_connect(history, "clicked", G_CALLBACK(onBanHistory), this);
    g_signal_connect(activity, "clicked", G_CALLBACK(onStaffActivity), this);
    g_signal_connect(apply, "clicked", G_CALLBACK(onApply), this);
    g_signal_connect(close, "clicked", G_CALLBACK(onCancel), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TLocalBanWindow::~TLocalBanWindow() { if (window != nullptr) gtk_widget_destroy(window); }

void TLocalBanWindow::setBanTypes(const char* types) {
    banTypes.clear();
    banDurations.clear();
    std::istringstream input(types == nullptr ? "" : types);
    for (std::string type; std::getline(input, type);) {
        if (type.empty()) continue;
        const size_t comma = type.rfind(',');
        banTypes.push_back(comma == std::string::npos ? type : type.substr(0, comma));
        banDurations.push_back(comma == std::string::npos ? 0 : std::atoi(type.c_str() + comma + 1));
    }
    for (int index = 0; index < 4; ++index) {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(scopes[index].type));
        for (const std::string& type : banTypes) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scopes[index].type), type.c_str());
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

void TLocalBanWindow::open(void* nextConnection, const std::string& nextAccount, const std::string& nextComputerId, const std::string& details) {
    connection = nextConnection;
    account = nextAccount;
    computerId = nextComputerId;
    gtk_window_set_title(GTK_WINDOW(window), ("Edit Access of " + account + (computerId.empty() ? "" : " (computer: " + computerId + ")")).c_str());
    for (int index = 0; index < 4; ++index) {
        scopes[index].target = index < 2 ? account : (computerId.empty() ? "" : "pc:" + computerId);
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
    }
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
        if (banType != fields.end()) for (int type = 0; type < static_cast<int>(banTypes.size()); ++type) if (banTypes[type] == banType->second) gtk_combo_box_set_active(GTK_COMBO_BOX(scopes[index].type), type);
        const auto release = fields.find("releasetime");
        if (release != fields.end()) scopes[index].releaseTime = release->second;
        const auto reason = fields.find("reason");
        if (reason != fields.end()) gtk_entry_set_text(GTK_ENTRY(scopes[index].reason), reason->second.c_str());
        updateTimeLeft(index);
    }
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

void TLocalBanWindow::onApply(GtkButton*, gpointer data) {
    TLocalBanWindow* editor = static_cast<TLocalBanWindow*>(data);
    for (int index = 0; index < 4; ++index) {
        Scope& scope = editor->scopes[index];
        if (scope.target.empty()) continue;
        const bool banned = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scope.banned));
        const bool reset = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scope.reset));
        if (!banned && !reset) continue;
        gchar* type = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(scope.type));
        const char* world = (index & 1) == 0 ? "local" : "all";
        rc_set_ban(editor->connection, scope.target.c_str(), world, banned, type == nullptr ? "" : type, reset ? "" : scope.releaseTime.c_str(), gtk_entry_get_text(GTK_ENTRY(scope.reason)));
        g_free(type);
    }
    gtk_widget_hide(editor->window);
}

void TLocalBanWindow::onCancel(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TLocalBanWindow*>(data)->window); }
void TLocalBanWindow::onBanTypeChanged(GtkComboBox* combo, gpointer data) { static_cast<TLocalBanWindow*>(data)->updateTimeLeft(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(combo), "scope"))); }
void TLocalBanWindow::onBanHistory(GtkButton*, gpointer data) { TLocalBanWindow* editor = static_cast<TLocalBanWindow*>(data); rc_request_ban_history(editor->connection, editor->account.c_str()); }
void TLocalBanWindow::onStaffActivity(GtkButton*, gpointer data) { TLocalBanWindow* editor = static_cast<TLocalBanWindow*>(data); rc_request_staff_activity(editor->connection, editor->account.c_str()); }
gboolean TLocalBanWindow::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TLocalBanWindow*>(data)->window); return true; }
