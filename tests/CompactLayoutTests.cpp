#include "TAccountPresentation.h"
#include "TErrorWindow.h"
#include "TRCOptions.h"
#include "TServerList.h"
#include "TStartFrame.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>

bool remoteControlDebug = false;

namespace {
void require(bool condition, const char* message) { if (!condition) { std::fprintf(stderr, "%s\n", message); std::abort(); } }

GtkWidget* windowByTitle(const char* title) {
    GList* windows = gtk_window_list_toplevels();
    GtkWidget* match = nullptr;
    for (GList* item = windows; item != nullptr; item = item->next) {
        GtkWidget* widget = GTK_WIDGET(item->data);
        if (g_strcmp0(gtk_window_get_title(GTK_WINDOW(widget)), title) == 0) { match = widget; break; }
    }
    g_list_free(windows);
    return match;
}

GtkWidget* widgetByName(GtkWidget* root, const char* name) {
    if (g_strcmp0(gtk_widget_get_name(root), name) == 0) return root;
    if (!GTK_IS_CONTAINER(root)) return nullptr;
    GList* children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget* match = nullptr;
    for (GList* item = children; item != nullptr && match == nullptr; item = item->next) match = widgetByName(GTK_WIDGET(item->data), name);
    g_list_free(children);
    return match;
}

GtkWidget* buttonByLabel(GtkWidget* root, const char* label) {
    if (GTK_IS_BUTTON(root) && g_strcmp0(gtk_button_get_label(GTK_BUTTON(root)), label) == 0) return root;
    if (!GTK_IS_CONTAINER(root)) return nullptr;
    GList* children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget* match = nullptr;
    for (GList* item = children; item != nullptr && match == nullptr; item = item->next) match = buttonByLabel(GTK_WIDGET(item->data), label);
    g_list_free(children);
    return match;
}

void settleGtk() { while (gtk_events_pending()) gtk_main_iteration(); }

void printWidths(GtkWidget* widget, int depth) {
    gint minimum = 0;
    gint natural = 0;
    gtk_widget_get_preferred_width(widget, &minimum, &natural);
    std::fprintf(stderr, "%*s%s %s %d/%d\n", depth * 2, "", G_OBJECT_TYPE_NAME(widget), gtk_widget_get_name(widget), minimum, natural);
    if (!GTK_IS_CONTAINER(widget)) return;
    GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (GList* item = children; item != nullptr; item = item->next) printWidths(GTK_WIDGET(item->data), depth + 1);
    g_list_free(children);
}

struct EditDialogProbe { bool found = false; bool pickerFound = false; bool pickerSummary = false; bool settingsButton = false; bool closeButton = false; gint minimum = 0; gint natural = 0; };
struct AccountManagerProbe { int phase = 0; bool editTransient = false; bool managerSurvived = false; bool associationBorderless = false; bool associationNames = false; };

gboolean probeAccountManager(gpointer data) {
    auto* probe = static_cast<AccountManagerProbe*>(data);
    GtkWidget* manager = windowByTitle("Accounts");
    if (manager == nullptr) return G_SOURCE_CONTINUE;
    if (probe->phase == 0) {
        GtkWidget* association = widgetByName(manager, "AccountServerAssociation");
        probe->associationBorderless = association != nullptr && GTK_IS_LABEL(association);
        probe->associationNames = association != nullptr && std::string(gtk_label_get_text(GTK_LABEL(association))).find("Retail") != std::string::npos && std::string(gtk_label_get_text(GTK_LABEL(association))).find("Moreno") == std::string::npos;
        probe->phase = 1;
        GtkWidget* list = widgetByName(manager, "AccountsList");
        require(list != nullptr && !gtk_list_box_get_activate_on_single_click(GTK_LIST_BOX(list)), "Accounts single click still activates rows");
        GtkListBoxRow* row = list == nullptr ? nullptr : gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
        if (list != nullptr && row != nullptr) g_signal_emit_by_name(list, "row-activated", row);
        return G_SOURCE_CONTINUE;
    }
    GtkWidget* editor = windowByTitle("Edit Account");
    if (probe->phase == 1 && editor != nullptr) {
        probe->editTransient = gtk_window_get_transient_for(GTK_WINDOW(editor)) == GTK_WINDOW(manager);
        probe->phase = 2;
        gtk_dialog_response(GTK_DIALOG(editor), GTK_RESPONSE_CANCEL);
        return G_SOURCE_CONTINUE;
    }
    if (probe->phase == 2 && editor == nullptr) {
        probe->managerSurvived = windowByTitle("Accounts") == manager;
        gtk_dialog_response(GTK_DIALOG(manager), GTK_RESPONSE_CLOSE);
        probe->phase = 3;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
}

int main(int argc, char** argv) {
    const std::filesystem::path configRoot = std::filesystem::current_path() / "compact-layout-config";
    std::filesystem::remove_all(configRoot);
    g_setenv("XDG_CONFIG_HOME", configRoot.string().c_str(), true);
    require(gtk_init_check(&argc, &argv), "GTK initialization failed");
    const std::filesystem::path profilePath = std::filesystem::path(g_get_user_config_dir()) / "GScriptRC" / "listservers.conf";
    std::vector<SavedListServer> profiles{{"Retail", "listserver.graalonline.com", 14922}, {"Moreno", "listserver.moreno.land", 14922}};
    require(RC::saveListServerProfiles(profilePath, profiles), "Could not seed list-server profiles");
    {
        RC::RCAccounts seed;
        seed.save("SavedAccount", "test-password", false, "Official — listserver.graalonline.com:14922");
        seed.update("", "OtherAccount", "other-password", false, {"Moreno — listserver.moreno.land:14922"});
        seed.saveAt(0, "test-password", false, "Official — listserver.graalonline.com:14922");
    }
    RC::RCOptions options;
    TStartFrame start(options, std::filesystem::current_path(), [](std::uint64_t, const std::string&, const std::string&, const std::string&, const std::string&) {}, [] {}, [] { return std::string("Retail"); });
    start.show();
    settleGtk();
    GtkWidget* login = nullptr;
    for (GList* item = gtk_window_list_toplevels(); item != nullptr; item = item->next) { GtkWidget* candidate = GTK_WIDGET(item->data); const char* title = gtk_window_get_title(GTK_WINDOW(candidate)); if (title != nullptr && (std::string(title).rfind("RemoteControl", 0) == 0 || std::string(title).find(" - RemoteControl") != std::string::npos)) { login = candidate; break; } }
    require(login != nullptr, "Login window not found");
    require(std::string(gtk_window_get_title(GTK_WINDOW(login))) == "Retail - RemoteControl", "Login title does not identify the selected list server");
    GtkWidget* placementError = createErrorWindow("Placement test", "test", GTK_WINDOW(login));
    settleGtk();
    require(gtk_window_get_transient_for(GTK_WINDOW(placementError)) == GTK_WINDOW(login), "Error dialog is not transient to its login parent");
    gtk_widget_destroy(placementError);
    gint loginMinimum = 0;
    gint loginNatural = 0;
    gtk_widget_get_preferred_width(login, &loginMinimum, &loginNatural);
    std::fprintf(stderr, "login minimum/natural: %d/%d\n", loginMinimum, loginNatural);
    require(loginMinimum <= 340, "Login minimum width exceeds compact reference");
    require(loginNatural <= 340, "Login natural width exceeds compact reference");
    GtkWidget* accountCombo = widgetByName(login, "AccountPicker");
    require(accountCombo != nullptr, "Account combo not found");
    GtkTreeIter accountRow;
    GtkTreeModel* accountModel = gtk_combo_box_get_model(GTK_COMBO_BOX(accountCombo));
    require(gtk_tree_model_get_n_columns(accountModel) == 4, "Account model does not retain stable row identity");
    require(gtk_tree_model_get_iter_first(accountModel, &accountRow), "Saved account row not found");
    gchar* accountMarkup = nullptr;
    gtk_tree_model_get(accountModel, &accountRow, 1, &accountMarkup, -1);
    const std::string markup = accountMarkup == nullptr ? std::string() : accountMarkup;
    g_free(accountMarkup);
    require(markup.find("[Retail]") != std::string::npos, "Retail account badge missing");
    require(markup.find("[Moreno]") == std::string::npos, "Account exposes multiple server badges");
    require(markup.find("listserver.graalonline.com") == std::string::npos, "Account badge exposes host text");
    gint storedAccountIndex = -1;
    gtk_tree_model_get(accountModel, &accountRow, 3, &storedAccountIndex, -1);
    require(storedAccountIndex == 0, "Saved account row index is not stable");
    GtkWidget* accountField = gtk_bin_get_child(GTK_BIN(accountCombo));
    require(gtk_combo_box_get_active(GTK_COMBO_BOX(accountCombo)) == 0, "Saved account was not initially selected");
    gtk_entry_set_text(GTK_ENTRY(accountField), "");
    settleGtk();
    require(std::string(gtk_entry_get_text(GTK_ENTRY(accountField))).empty(), "Cleared account text reverted");
    require(gtk_combo_box_get_active(GTK_COMBO_BOX(accountCombo)) == -1, "Typing did not detach saved account selection");
    gtk_entry_set_text(GTK_ENTRY(accountField), "NewAccount");
    settleGtk();
    require(std::string(gtk_entry_get_text(GTK_ENTRY(accountField))) == "NewAccount", "Typed account text was not preserved");
    gtk_combo_box_set_active(GTK_COMBO_BOX(accountCombo), 0);
    settleGtk();
    require(std::string(gtk_entry_get_text(GTK_ENTRY(accountField))) == "SavedAccount", "Explicit account selection did not restore saved account");
    gtk_combo_box_set_active(GTK_COMBO_BOX(accountCombo), 1);
    settleGtk();
    require(std::string(gtk_window_get_title(GTK_WINDOW(login))) == "Moreno - RemoteControl", "Login title did not follow the selected account row");
    gtk_combo_box_set_active(GTK_COMBO_BOX(accountCombo), 0);
    settleGtk();
    EditDialogProbe editProbe;
    g_timeout_add(50, +[](gpointer data) -> gboolean {
        auto* probe = static_cast<EditDialogProbe*>(data);
        GtkWidget* dialog = windowByTitle("Edit Account");
        if (dialog == nullptr) return G_SOURCE_CONTINUE;
        probe->found = true;
        gtk_widget_get_preferred_width(dialog, &probe->minimum, &probe->natural);
        GtkWidget* picker = widgetByName(dialog, "AccountServerPicker");
        probe->pickerFound = picker != nullptr && gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(picker)), nullptr) == 2;
        if (picker != nullptr) {
            gchar* active = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(picker));
            probe->pickerSummary = active != nullptr && std::string(active) == "Retail";
            g_free(active);
        }
        probe->settingsButton = widgetByName(dialog, "AccountServerSettingsButton") != nullptr;
        probe->closeButton = buttonByLabel(dialog, "Close") != nullptr && buttonByLabel(dialog, "Cancel") == nullptr;
        gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
        return G_SOURCE_REMOVE;
    }, &editProbe);
    require(!start.editAccount("SavedAccount"), "Cancelled account edit was accepted");
    require(editProbe.found && editProbe.pickerFound, "Edit Account saved-profile picker was not populated");
    require(editProbe.pickerSummary, "Edit Account combo did not display its selected profile");
    require(editProbe.settingsButton, "Edit Account list-server settings button was not found");
    require(editProbe.closeButton, "Edit Account close action is mislabeled");
    std::fprintf(stderr, "edit account minimum/natural: %d/%d\n", editProbe.minimum, editProbe.natural);
    require(editProbe.minimum <= 320 && editProbe.natural <= 320, "Edit Account exceeds compact width baseline");
    AccountManagerProbe managerProbe;
    g_timeout_add(25, probeAccountManager, &managerProbe);
    GtkWidget* manageAccounts = widgetByName(login, "AccountManageButton");
    require(manageAccounts != nullptr, "Account manager button not found");
    gtk_button_clicked(GTK_BUTTON(manageAccounts));
    require(managerProbe.editTransient, "Edit Account was not transient to Accounts");
    require(managerProbe.managerSurvived, "Accounts closed when Edit Account closed");
    require(managerProbe.associationBorderless, "Account association is not a borderless label");
    require(managerProbe.associationNames, "Account association row does not show profile names");

    bool themeApplied = false;
    TServerList servers([] {}, [](TServerList*, void*, int, const std::string&, const std::string&, const std::string&, bool) {}, [] {}, true, "dark", [&](bool, const std::string& theme) { themeApplied = theme == "light"; });
    servers.openListServerSettings();
    settleGtk();
    GtkWidget* settings = windowByTitle("RC settings");
    require(settings != nullptr, "Settings window not found");
    gint settingsMinimum = 0;
    gint settingsNatural = 0;
    gtk_widget_get_preferred_width(settings, &settingsMinimum, &settingsNatural);
    std::fprintf(stderr, "settings minimum/natural: %d/%d\n", settingsMinimum, settingsNatural);
    if (settingsMinimum > 320 || settingsNatural > 320) printWidths(settings, 0);
    require(settingsMinimum <= 300, "Settings minimum width exceeds compact baseline");
    require(settingsNatural <= 300, "Settings natural width exceeds compact baseline");
    require(buttonByLabel(settings, "Save") == nullptr, "Settings still exposes a Save button");
    GtkWidget* name = widgetByName(settings, "ListServerNameField");
    GtkWidget* host = widgetByName(settings, "ListServerHostField");
    GtkWidget* port = widgetByName(settings, "ListServerPortField");
    require(name != nullptr && host != nullptr && port != nullptr, "Settings profile fields not found");
    require(!gtk_widget_get_sensitive(name) && !gtk_widget_get_sensitive(host) && !gtk_widget_get_sensitive(port), "Retail profile is editable");
    GtkWidget* addProfile = widgetByName(settings, "NewListServerProfile");
    require(addProfile != nullptr, "New profile button not found");
    gtk_button_clicked(GTK_BUTTON(addProfile));
    gtk_entry_set_text(GTK_ENTRY(name), "Test");
    gtk_entry_set_text(GTK_ENTRY(host), "listserver.test");
    gtk_entry_set_text(GTK_ENTRY(port), "14922");
    g_usleep(500000);
    settleGtk();
    profiles = RC::loadListServerProfiles(profilePath, "listserver.graalonline.com", 14922);
    require(profiles.size() == 3, "Debounced profile add did not persist");
    const auto testProfile = std::find_if(profiles.begin(), profiles.end(), [](const SavedListServer& profile) { return profile.name == "Test"; });
    require(testProfile != profiles.end() && testProfile->host == "listserver.test", "Persisted profile values are incorrect");
    require(profiles.front().name == "Test", "Selected profile was not persisted");
    GtkWidget* theme = widgetByName(settings, "SettingsThemePicker");
    require(theme != nullptr, "Theme picker not found");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(theme), "light");
    settleGtk();
    require(themeApplied, "Theme selection did not apply immediately");
    gtk_button_clicked(GTK_BUTTON(buttonByLabel(settings, "Close")));
    settleGtk();
    require(windowByTitle("RC settings") == nullptr, "Settings Close did not close");
    std::filesystem::remove_all(configRoot);
    return 0;
}
