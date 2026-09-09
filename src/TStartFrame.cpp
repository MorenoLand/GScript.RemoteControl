#include "TStartFrame.h"
#include "TAccountPresentation.h"
#include "TButtonIcons.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace {
    enum AccountColumns { AccountNameColumn, AccountMarkupColumn, AccountDetailColumn, AccountIndexColumn, AccountColumnCount };
    struct AccountManageHideRequest { TStartFrame* frame; std::shared_ptr<bool> alive; };

    std::vector<std::string> splitServers(const std::string& value) {
        std::vector<std::string> servers;
        std::string item;
        auto add = [&] {
            const std::size_t first = item.find_first_not_of(" \t\r\n");
            const std::size_t last = item.find_last_not_of(" \t\r\n");
            if (first != std::string::npos) servers.push_back(item.substr(first, last - first + 1));
            item.clear();
        };
        for (char character : value) {
            if (character == ',' || character == ';' || character == '\n') add();
            else item += character;
        }
        add();
        return servers;
    }

    struct AccountServerPickerOption { std::string label; std::string value; };
    struct AccountServerPickerState { GtkWidget* combo; std::filesystem::path profilePath; std::vector<AccountServerPickerOption> options; std::string selected; std::shared_ptr<bool> alive = std::make_shared<bool>(true); bool refreshing = false; };

    void refreshAccountServerPicker(AccountServerPickerState* state) {
        state->refreshing = true;
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(state->combo));
        for (const AccountServerPickerOption& option : state->options) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->combo), option.label.c_str());
        const auto selected = std::find_if(state->options.begin(), state->options.end(), [&](const AccountServerPickerOption& option) { return option.value == state->selected; });
        gtk_combo_box_set_active(GTK_COMBO_BOX(state->combo), selected == state->options.end() ? -1 : static_cast<int>(selected - state->options.begin()));
        atk_object_set_description(gtk_widget_get_accessible(state->combo), state->selected.empty() ? "No list-server profile selected" : state->selected.c_str());
        state->refreshing = false;
    }

    void loadAccountServerPicker(AccountServerPickerState* state) {
        const std::vector<SavedListServer> profiles = RC::loadListServerProfiles(state->profilePath, "listserver.graalonline.com", 14922);
        state->options.clear();
        for (const SavedListServer& profile : profiles) state->options.push_back({profile.name, RC::listServerAssociation(profile)});
        if (!state->selected.empty()) state->selected = RC::normalizeListServerAssociation(state->selected, profiles);
        if (!state->selected.empty() && std::none_of(state->options.begin(), state->options.end(), [&](const AccountServerPickerOption& option) { return option.value == state->selected; })) state->options.push_back({state->selected, state->selected});
        refreshAccountServerPicker(state);
    }

    void onAccountServerPicked(GtkComboBox* combo, gpointer data) {
        auto* state = static_cast<AccountServerPickerState*>(data);
        if (state->refreshing) return;
        const int index = gtk_combo_box_get_active(combo);
        if (index < 0 || static_cast<std::size_t>(index) >= state->options.size()) return;
        state->selected = state->options[static_cast<std::size_t>(index)].value;
        refreshAccountServerPicker(state);
    }

    struct AccountServerSettingsState { TStartFrame::ListServerSettingsCallback* openSettings; GtkWindow* editDialog; AccountServerPickerState* picker; };

    struct AccountServerSettingsClosedState { AccountServerPickerState* picker; std::weak_ptr<bool> alive; };
    void onAccountServerSettingsClosed(GtkWidget*, gpointer data) { auto* state = static_cast<AccountServerSettingsClosedState*>(data); const std::shared_ptr<bool> alive = state == nullptr ? nullptr : state->alive.lock(); if (state != nullptr && alive != nullptr && *alive && state->picker != nullptr) loadAccountServerPicker(state->picker); }
    void destroyAccountServerSettingsClosedState(gpointer data, GClosure*) { delete static_cast<AccountServerSettingsClosedState*>(data); }

    void onAccountServerSettings(GtkButton*, gpointer data) {
        auto* state = static_cast<AccountServerSettingsState*>(data);
        if (!*state->openSettings) return;
        (*state->openSettings)();
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item != nullptr; item = item->next) {
            GtkWidget* candidate = GTK_WIDGET(item->data);
            if (g_strcmp0(gtk_window_get_title(GTK_WINDOW(candidate)), "RC settings") != 0) continue;
            auto* closedState = new AccountServerSettingsClosedState{state->picker, state->picker->alive};
            g_signal_connect_data(candidate, "destroy", G_CALLBACK(onAccountServerSettingsClosed), closedState, destroyAccountServerSettingsClosedState, static_cast<GConnectFlags>(0));
            gtk_window_present(GTK_WINDOW(candidate));
            break;
        }
        g_list_free(windows);
    }

}

TStartFrame::TStartFrame(RC::RCOptions& options, const std::filesystem::path& applicationDirectory, ConnectCallback onConnect, ListServerSettingsCallback onListServerSettings, ListServerEndpointCallback listServerEndpoint)
    : options(options), accounts(), applicationDirectory(applicationDirectory), onConnectCallback(std::move(onConnect)), onListServerSettingsCallback(std::move(onListServerSettings)), listServerEndpointCallback(std::move(listServerEndpoint)) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "StartFrame");
    gtk_window_set_title(GTK_WINDOW(window), "Remote Control");
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(window), 264, 220);
    gtk_window_set_resizable(GTK_WINDOW(window), true);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* frame = gtk_frame_new(" Options ");
    gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
    gtk_box_pack_start(GTK_BOX(root), frame, true, true, 0);
    GtkWidget* optionsBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_container_set_border_width(GTK_CONTAINER(optionsBox), 5);
    gtk_container_add(GTK_CONTAINER(frame), optionsBox);

    auto addField = [optionsBox](const char* label, GtkWidget*& field, bool password) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        GtkWidget* caption = gtk_label_new(label);
        gtk_widget_set_size_request(caption, 80, -1);
        gtk_label_set_xalign(GTK_LABEL(caption), 0.0F);
        field = gtk_entry_new();
        gtk_widget_set_hexpand(field, true);
        if (password) gtk_entry_set_visibility(GTK_ENTRY(field), false);
        gtk_box_pack_start(GTK_BOX(row), caption, false, false, 0);
        gtk_box_pack_start(GTK_BOX(row), field, true, true, 0);
        gtk_box_pack_start(GTK_BOX(optionsBox), row, false, true, 0);
    };

    addField("Nickname:", nicknameField, false);
    GtkWidget* accountLabel = gtk_label_new("Account:");
    gtk_widget_set_size_request(accountLabel, 80, -1);
    gtk_label_set_xalign(GTK_LABEL(accountLabel), 0.0F);
    GtkWidget* accountRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget* accountEvent = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(accountEvent), false);
    gtk_widget_add_events(accountEvent, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    GtkWidget* accountOverlay = gtk_overlay_new();
    gtk_widget_set_hexpand(accountOverlay, true);
    GtkListStore* accountModel = gtk_list_store_new(AccountColumnCount, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    accountCombo = gtk_combo_box_new_with_model_and_entry(GTK_TREE_MODEL(accountModel));
    g_object_unref(accountModel);
    gtk_combo_box_set_entry_text_column(GTK_COMBO_BOX(accountCombo), AccountNameColumn);
    gtk_combo_box_set_popup_fixed_width(GTK_COMBO_BOX(accountCombo), false);
    gtk_cell_layout_clear(GTK_CELL_LAYOUT(accountCombo));
    GtkCellRenderer* accountRenderer = gtk_cell_renderer_text_new();
    g_object_set(accountRenderer, "ellipsize", PANGO_ELLIPSIZE_END, "max-width-chars", 34, nullptr);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(accountCombo), accountRenderer, true);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(accountCombo), accountRenderer, "markup", AccountMarkupColumn);
    gtk_widget_set_hexpand(accountCombo, true);
    gtk_widget_set_name(accountCombo, "AccountPicker");
    accountField = gtk_bin_get_child(GTK_BIN(accountCombo));
    gtk_widget_set_name(accountField, "AccountField");
    gtk_widget_set_tooltip_text(accountCombo, nullptr);
    gtk_widget_set_tooltip_text(accountField, nullptr);
    gtk_entry_set_placeholder_text(GTK_ENTRY(accountField), nullptr);
    gtk_entry_set_width_chars(GTK_ENTRY(accountField), 8);
    accountManageButton = gtk_button_new_from_icon_name("document-edit-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_name(accountManageButton, "AccountManageButton");
    gtk_style_context_add_class(gtk_widget_get_style_context(accountManageButton), "account-manage-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(accountManageButton), "flat");
    gtk_widget_set_halign(accountManageButton, GTK_ALIGN_END);
    gtk_widget_set_valign(accountManageButton, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_end(accountManageButton, 40);
    gtk_widget_set_size_request(accountManageButton, 20, 20);
    gtk_widget_add_events(accountManageButton, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    gtk_widget_set_can_focus(accountManageButton, true);
    gtk_widget_set_focus_on_click(accountManageButton, true);
    gtk_widget_set_tooltip_text(accountManageButton, "Manage accounts");
    atk_object_set_name(gtk_widget_get_accessible(accountManageButton), "Manage accounts");
    GtkCssProvider* accountCss = gtk_css_provider_new();
    gtk_css_provider_load_from_data(accountCss, "#AccountField { padding-right: 52px; } #AccountManageButton, #AccountManageButton:hover, #AccountManageButton:active, #AccountManageButton:focus { min-width: 20px; min-height: 20px; padding: 0; border: none; border-radius: 2px; box-shadow: none; background-image: none; background-color: transparent; }", -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(accountField), GTK_STYLE_PROVIDER(accountCss), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    gtk_style_context_add_provider(gtk_widget_get_style_context(accountManageButton), GTK_STYLE_PROVIDER(accountCss), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(accountCss);
    gtk_widget_set_no_show_all(accountManageButton, true);
    gtk_container_add(GTK_CONTAINER(accountOverlay), accountCombo);
    gtk_overlay_add_overlay(GTK_OVERLAY(accountOverlay), accountManageButton);
    gtk_container_add(GTK_CONTAINER(accountEvent), accountOverlay);
    gtk_box_pack_start(GTK_BOX(accountRow), accountLabel, false, false, 0);
    gtk_box_pack_start(GTK_BOX(accountRow), accountEvent, true, true, 0);
    gtk_box_pack_start(GTK_BOX(optionsBox), accountRow, false, true, 0);

    addField("Password:", passwordField, true);
    gtk_widget_set_name(nicknameField, "NicknameField");
    gtk_widget_set_name(passwordField, "PasswordField");
    gtk_entry_set_text(GTK_ENTRY(nicknameField), options.nickname.c_str());

    passwordCheck = gtk_check_button_new_with_label("Don't save password");
    gtk_widget_set_name(passwordCheck, "PasswordCheck");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(passwordCheck), options.dontsavepassword);
    gtk_box_pack_start(GTK_BOX(optionsBox), passwordCheck, false, false, 0);
    graphicsCheck = gtk_check_button_new_with_label("Graphical Menu");
    gtk_widget_set_name(graphicsCheck, "GraphicsCheck");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(graphicsCheck), options.graphicalmenu);
    gtk_box_pack_start(GTK_BOX(optionsBox), graphicsCheck, false, false, 0);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(buttons), 5);
    GtkWidget* listServerSettings = gtk_button_new_from_icon_name("preferences-system-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_size_request(listServerSettings, 42, -1);
    gtk_widget_set_tooltip_text(listServerSettings, "List server settings");
    atk_object_set_name(gtk_widget_get_accessible(listServerSettings), "List server settings");
    gtk_box_pack_start(GTK_BOX(buttons), listServerSettings, false, false, 0);
    gtk_box_pack_start(GTK_BOX(buttons), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0), true, true, 0);
    GtkWidget* connectButton = gtk_button_new_with_label("OK");
    GtkWidget* cancelButton = gtk_button_new_with_label("Close");
    gtk_button_set_image(GTK_BUTTON(connectButton), gtk_image_new_from_stock(GTK_STOCK_OK, GTK_ICON_SIZE_BUTTON));
    gtk_button_set_image(GTK_BUTTON(cancelButton), gtk_image_new_from_stock(GTK_STOCK_CLOSE, GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(connectButton), true);
    gtk_button_set_always_show_image(GTK_BUTTON(cancelButton), true);
    gtk_box_pack_end(GTK_BOX(buttons), cancelButton, false, false, 0);
    gtk_box_pack_end(GTK_BOX(buttons), connectButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, true, 0);

    g_signal_connect(connectButton, "clicked", G_CALLBACK(TStartFrame::onConnect), this);
    g_signal_connect(accountCombo, "changed", G_CALLBACK(TStartFrame::onAccountChanged), this);
    g_signal_connect(accountField, "changed", G_CALLBACK(TStartFrame::onAccountEntryChanged), this);
    g_signal_connect(accountManageButton, "clicked", G_CALLBACK(TStartFrame::onManageAccounts), this);
    g_signal_connect(accountEvent, "enter-notify-event", G_CALLBACK(TStartFrame::onAccountPointerEnter), this);
    g_signal_connect(accountEvent, "leave-notify-event", G_CALLBACK(TStartFrame::onAccountPointerLeave), this);
    g_signal_connect(accountManageButton, "enter-notify-event", G_CALLBACK(TStartFrame::onAccountManagePointerEnter), this);
    g_signal_connect(accountManageButton, "leave-notify-event", G_CALLBACK(TStartFrame::onAccountManagePointerLeave), this);
    g_signal_connect(accountField, "focus-in-event", G_CALLBACK(TStartFrame::onAccountFocusIn), this);
    g_signal_connect(accountField, "focus-out-event", G_CALLBACK(TStartFrame::onAccountFocusOut), this);
    g_signal_connect(listServerSettings, "clicked", G_CALLBACK(TStartFrame::onListServerSettings), this);
    g_signal_connect(cancelButton, "clicked", G_CALLBACK(onCancel), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
    g_signal_connect(window, "destroy", G_CALLBACK(onDestroy), this);

    refreshAccountMenu();
    requestedAccountIndex = accounts.activeIndex() == static_cast<std::size_t>(-1) ? -1 : static_cast<int>(accounts.activeIndex());
    selectAccount(accounts.accountName());
    requestedAccountIndex = -1;
}

TStartFrame::~TStartFrame() { *callbackAlive = false; if (window != nullptr) gtk_widget_destroy(window); }
void TStartFrame::show() { gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); }
void TStartFrame::toggleVisibility() { if (gtk_widget_get_visible(window)) gtk_widget_hide(window); else show(); }
bool TStartFrame::mcpVisible() const { return gtk_widget_get_visible(window); }
std::string TStartFrame::mcpAccount() const { return getText(accountField); }
std::string TStartFrame::mcpNickname() const { return getText(nicknameField); }
bool TStartFrame::mcpHasPassword() const { return !getText(passwordField).empty(); }
std::vector<RC::RCAccount> TStartFrame::accountsForListServer(const std::string& listServer) const {
    std::vector<RC::RCAccount> result;
    const std::vector<SavedListServer> profiles = RC::loadListServerProfiles(std::filesystem::path(g_get_user_config_dir()) / "GScriptRC" / "listservers.conf", "listserver.graalonline.com", 14922);
    const auto profile = std::find_if(profiles.begin(), profiles.end(), [&](const SavedListServer& value) { return g_ascii_strcasecmp(value.name.c_str(), listServer.c_str()) == 0; });
    const std::string endpoint = profile == profiles.end() ? std::string() : profile->host + ":" + std::to_string(profile->port);
    for (const RC::RCAccount& account : accounts.entries()) if (std::any_of(account.listServers.begin(), account.listServers.end(), [&](const std::string& association) {
        if (g_ascii_strcasecmp(association.c_str(), listServer.c_str()) == 0) return true;
        if (!endpoint.empty() && g_ascii_strcasecmp(association.c_str(), endpoint.c_str()) == 0) return true;
        if (profile != profiles.end() && g_ascii_strcasecmp(RC::normalizeListServerAssociation(association, profiles).c_str(), RC::listServerAssociation(*profile).c_str()) == 0) return true;
        return false;
    })) result.push_back(account);
    return result;
}

bool TStartFrame::mcpSubmit(const std::string& account, const std::string& nickname, std::string& error) {
    if (!account.empty()) selectAccount(account);
    if (!nickname.empty()) gtk_entry_set_text(GTK_ENTRY(nicknameField), nickname.c_str());
    if (selectedAccount.empty()) { error = "Login account is empty"; return false; }
    if (getText(passwordField).empty()) { error = "The existing login form has no password"; return false; }
    connect();
    return true;
}

void TStartFrame::onConnect(GtkButton*, gpointer data) { static_cast<TStartFrame*>(data)->connect(); }
void TStartFrame::onListServerSettings(GtkButton*, gpointer data) { TStartFrame* frame = static_cast<TStartFrame*>(data); if (frame->onListServerSettingsCallback) frame->onListServerSettingsCallback(); }
void TStartFrame::onManageAccounts(GtkButton*, gpointer data) { static_cast<TStartFrame*>(data)->openAccountManager(); }
void TStartFrame::onAccountChanged(GtkComboBox* combo, gpointer data) {
    TStartFrame* frame = static_cast<TStartFrame*>(data);
    if (frame->accountSelectionInProgress) return;
    GtkTreeIter active;
    if (!gtk_combo_box_get_active_iter(combo, &active)) return;
    gchar* value = nullptr;
    gint index = -1;
    gtk_tree_model_get(gtk_combo_box_get_model(combo), &active, AccountNameColumn, &value, AccountIndexColumn, &index, -1);
    if (value == nullptr) return;
    const std::string account = value;
    g_free(value);
    frame->requestedAccountIndex = index;
    frame->selectAccount(account);
    frame->requestedAccountIndex = -1;
    frame->updateAccountTitle(index);
}
void TStartFrame::onAccountEntryChanged(GtkEditable* editable, gpointer data) {
    TStartFrame* frame = static_cast<TStartFrame*>(data);
    if (frame->accountSelectionInProgress) return;
    const std::string typed = gtk_entry_get_text(GTK_ENTRY(editable));
    GtkTreeIter active;
    if (gtk_combo_box_get_active_iter(GTK_COMBO_BOX(frame->accountCombo), &active)) {
        gchar* rowName = nullptr;
        gint rowIndex = -1;
        gtk_tree_model_get(gtk_combo_box_get_model(GTK_COMBO_BOX(frame->accountCombo)), &active, AccountNameColumn, &rowName, AccountIndexColumn, &rowIndex, -1);
        const bool modelSelection = rowName != nullptr && typed == rowName;
        g_free(rowName);
        if (modelSelection) { frame->selectedAccount = typed; frame->selectedAccountIndex = rowIndex; frame->updateAccountTitle(rowIndex); return; }
    }
    frame->selectedAccount = typed;
    frame->selectedAccountIndex = -1;
    frame->updateAccountTitle(-1);
    frame->accountSelectionInProgress = true;
    gtk_combo_box_set_active(GTK_COMBO_BOX(frame->accountCombo), -1);
    if (typed != gtk_entry_get_text(GTK_ENTRY(frame->accountField))) gtk_entry_set_text(GTK_ENTRY(frame->accountField), typed.c_str());
    frame->accountSelectionInProgress = false;
}
gboolean TStartFrame::onAccountPointerEnter(GtkWidget*, GdkEventCrossing*, gpointer data) { TStartFrame* frame = static_cast<TStartFrame*>(data); frame->accountHovered = true; gtk_widget_show(frame->accountManageButton); return false; }
gboolean TStartFrame::onAccountPointerLeave(GtkWidget*, GdkEventCrossing* event, gpointer data) {
    TStartFrame* frame = static_cast<TStartFrame*>(data);
    if (event->detail != GDK_NOTIFY_INFERIOR) frame->accountHovered = false;
    frame->scheduleAccountManageHide();
    return false;
}
gboolean TStartFrame::onAccountManagePointerEnter(GtkWidget*, GdkEventCrossing*, gpointer data) { TStartFrame* frame = static_cast<TStartFrame*>(data); frame->accountManageHovered = true; gtk_widget_show(frame->accountManageButton); return false; }
gboolean TStartFrame::onAccountManagePointerLeave(GtkWidget*, GdkEventCrossing*, gpointer data) {
    TStartFrame* frame = static_cast<TStartFrame*>(data);
    frame->accountManageHovered = false;
    frame->scheduleAccountManageHide();
    return false;
}
gboolean TStartFrame::onAccountFocusIn(GtkWidget*, GdkEventFocus*, gpointer data) { gtk_widget_show(static_cast<TStartFrame*>(data)->accountManageButton); return false; }
gboolean TStartFrame::onAccountFocusOut(GtkWidget*, GdkEventFocus*, gpointer data) {
    TStartFrame* frame = static_cast<TStartFrame*>(data);
    frame->scheduleAccountManageHide();
    return false;
}
gboolean TStartFrame::onAccountManageHideLater(gpointer data) {
    auto* request = static_cast<AccountManageHideRequest*>(data);
    if (!*request->alive) return G_SOURCE_REMOVE;
    TStartFrame* frame = request->frame;
    if (frame->window != nullptr && !frame->accountHovered && !frame->accountManageHovered && !gtk_widget_has_focus(frame->accountField) && !gtk_widget_has_focus(frame->accountManageButton)) gtk_widget_hide(frame->accountManageButton);
    return G_SOURCE_REMOVE;
}
void TStartFrame::scheduleAccountManageHide() { g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, onAccountManageHideLater, new AccountManageHideRequest{this, callbackAlive}, +[](gpointer data) { delete static_cast<AccountManageHideRequest*>(data); }); }
void TStartFrame::onCancel(GtkButton*, gpointer) { if (gtk_main_level() > 0) gtk_main_quit(); }
void TStartFrame::onDestroy(GtkWidget*, gpointer data) { static_cast<TStartFrame*>(data)->window = nullptr; if (gtk_main_level() > 0) gtk_main_quit(); }
gboolean TStartFrame::onDelete(GtkWidget*, GdkEvent*, gpointer) { return false; }

void TStartFrame::selectAccount(const std::string& accountName) {
    accountSelectionInProgress = true;
    selectedAccount = accountName;
    if (selectedAccount.empty() && !accounts.entries().empty()) selectedAccount = accounts.entries().front().name;
    int active = accounts.entries().empty() ? 0 : -1;
    if (requestedAccountIndex >= 0 && static_cast<std::size_t>(requestedAccountIndex) < accounts.entries().size()) active = requestedAccountIndex;
    else for (std::size_t index = 0; index < accounts.entries().size(); ++index) if (accounts.entries()[index].name == selectedAccount) { active = static_cast<int>(index); break; }
    selectedAccountIndex = active >= 0 && static_cast<std::size_t>(active) < accounts.entries().size() ? active : -1;
    gtk_combo_box_set_active(GTK_COMBO_BOX(accountCombo), active);
    gtk_entry_set_text(GTK_ENTRY(accountField), selectedAccount.c_str());
    const std::string password = selectedAccountIndex >= 0 ? accounts.passwordForIndex(static_cast<std::size_t>(selectedAccountIndex)) : accounts.passwordFor(selectedAccount);
    gtk_entry_set_text(GTK_ENTRY(passwordField), password.c_str());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(passwordCheck), password.empty());
    const RC::RCAccount* selected = selectedAccountIndex >= 0 ? &accounts.entries()[static_cast<std::size_t>(selectedAccountIndex)] : nullptr;
    std::string title = "RemoteControl";
    if (selected != nullptr && !selected->listServers.empty()) {
        std::string label = selected->listServers.front();
        std::size_t separator = label.find(" — ");
        if (separator == std::string::npos) separator = label.find(" â€” ");
        if (separator != std::string::npos) label.resize(separator);
        if (!label.empty()) title = label + " - RemoteControl";
    }
    gtk_window_set_title(GTK_WINDOW(window), title.c_str());
    const std::vector<SavedListServer> profiles = RC::loadListServerProfiles(std::filesystem::path(g_get_user_config_dir()) / "GScriptRC" / "listservers.conf", "listserver.graalonline.com", 14922);
    if (selected != nullptr) { const std::vector<std::string> labels = RC::accountServerBadgeLabels(*selected, profiles); if (!labels.empty()) title = labels.front() + " - RemoteControl"; }
    gtk_window_set_title(GTK_WINDOW(window), title.c_str());
    const std::string detail = selected == nullptr ? std::string("Type an account name or choose a saved account") : RC::accountServerDetail(*selected, profiles);
    atk_object_set_description(gtk_widget_get_accessible(accountCombo), detail.c_str());
    accountSelectionInProgress = false;
}

void TStartFrame::updateAccountTitle(int accountIndex) {
    std::string title = "RemoteControl";
    if (accountIndex >= 0 && static_cast<std::size_t>(accountIndex) < accounts.entries().size()) {
        const std::vector<SavedListServer> profiles = RC::loadListServerProfiles(std::filesystem::path(g_get_user_config_dir()) / "GScriptRC" / "listservers.conf", "listserver.graalonline.com", 14922);
        const std::vector<std::string> labels = RC::accountServerBadgeLabels(accounts.entries()[static_cast<std::size_t>(accountIndex)], profiles);
        if (!labels.empty() && labels.front() != "Unassigned") title = labels.front() + " - RemoteControl";
    }
    gtk_window_set_title(GTK_WINDOW(window), title.c_str());
}

void TStartFrame::refreshAccountMenu() {
    GtkListStore* model = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(accountCombo)));
    gtk_list_store_clear(model);
    const std::vector<SavedListServer> profiles = RC::loadListServerProfiles(std::filesystem::path(g_get_user_config_dir()) / "GScriptRC" / "listservers.conf", "listserver.graalonline.com", 14922);
    for (const RC::RCAccount& account : accounts.entries()) {
        GtkTreeIter row;
        const std::string markup = RC::accountRowMarkup(account, profiles);
        gtk_list_store_append(model, &row);
        gtk_list_store_set(model, &row, AccountNameColumn, account.name.c_str(), AccountMarkupColumn, markup.c_str(), AccountIndexColumn, static_cast<int>(&account - accounts.entries().data()), -1);
    }
}

bool TStartFrame::editAccount(const std::string& accountName, GtkWindow* parent, int accountIndex) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(accountName.empty() ? "Add Account" : "Edit Account", nullptr, static_cast<GtkDialogFlags>(0), "Close", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_OK, nullptr);
    applyGtkButtonIcon(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL), GTK_STOCK_CLOSE);
    applyGtkButtonIcon(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK), GTK_STOCK_SAVE);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 292, -1);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box, true, true, 0);
    GtkWidget* name = gtk_entry_new();
    GtkWidget* password = gtk_entry_new();
    GtkWidget* servers = gtk_combo_box_text_new();
    gtk_widget_set_name(servers, "AccountServerPicker");
    GList* serverRenderers = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(servers));
    for (GList* item = serverRenderers; item != nullptr; item = item->next) if (GTK_IS_CELL_RENDERER_TEXT(item->data)) g_object_set(item->data, "ellipsize", PANGO_ELLIPSIZE_END, "max-width-chars", 28, nullptr);
    g_list_free(serverRenderers);
    GtkWidget* serverRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget* serverSettings = gtk_button_new_from_icon_name("document-edit-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_name(serverSettings, "AccountServerSettingsButton");
    gtk_button_set_relief(GTK_BUTTON(serverSettings), GTK_RELIEF_NONE);
    gtk_widget_set_size_request(serverSettings, 20, 20);
    gtk_widget_set_tooltip_text(serverSettings, "Manage saved list servers");
    atk_object_set_name(gtk_widget_get_accessible(serverSettings), "Manage saved list servers");
    GtkWidget* forget = gtk_check_button_new_with_label("Don't save password");
    gtk_entry_set_visibility(GTK_ENTRY(password), false);
    gtk_entry_set_text(GTK_ENTRY(name), accountName.c_str());
    int resolvedIndex = accountIndex;
    if (resolvedIndex < 0) for (std::size_t index = 0; index < accounts.entries().size(); ++index) if (accounts.entries()[index].name == accountName) { resolvedIndex = static_cast<int>(index); break; }
    const std::string existingPassword = resolvedIndex >= 0 ? accounts.passwordForIndex(static_cast<std::size_t>(resolvedIndex)) : accounts.passwordFor(accountName);
    gtk_entry_set_text(GTK_ENTRY(password), existingPassword.c_str());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(forget), !accountName.empty() && existingPassword.empty());
    GtkWidget* nameLabel = gtk_label_new("Account name"); gtk_label_set_xalign(GTK_LABEL(nameLabel), 0.0F);
    GtkWidget* passwordLabel = gtk_label_new("Password"); gtk_label_set_xalign(GTK_LABEL(passwordLabel), 0.0F);
    GtkWidget* serverLabel = gtk_label_new("List server"); gtk_label_set_xalign(GTK_LABEL(serverLabel), 0.0F);
    auto* picker = new AccountServerPickerState{servers, std::filesystem::path(g_get_user_config_dir()) / "GScriptRC" / "listservers.conf"};
    g_object_set_data_full(G_OBJECT(dialog), "account-server-picker", picker, +[](gpointer data) { auto* picker = static_cast<AccountServerPickerState*>(data); if (picker->alive != nullptr) *picker->alive = false; delete picker; });
    const std::vector<std::string> savedServers = resolvedIndex >= 0 ? accounts.listServersForIndex(static_cast<std::size_t>(resolvedIndex)) : accounts.listServersFor(accountName);
    if (!savedServers.empty()) picker->selected = savedServers.front();
    loadAccountServerPicker(picker);
    gtk_widget_set_tooltip_text(servers, "Choose saved list-server profiles to add or remove");
    atk_object_set_name(gtk_widget_get_accessible(servers), "Associated list-server profiles");
    g_signal_connect(servers, "changed", G_CALLBACK(onAccountServerPicked), picker);
    AccountServerSettingsState settingsState{&onListServerSettingsCallback, GTK_WINDOW(dialog), picker};
    g_signal_connect(serverSettings, "clicked", G_CALLBACK(onAccountServerSettings), &settingsState);
    gtk_box_pack_start(GTK_BOX(box), nameLabel, false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), name, false, true, 0);
    gtk_box_pack_start(GTK_BOX(box), passwordLabel, false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), password, false, true, 0);
    gtk_box_pack_start(GTK_BOX(box), forget, false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), serverLabel, false, false, 0);
    gtk_box_pack_start(GTK_BOX(serverRow), servers, true, true, 0);
    gtk_box_pack_start(GTK_BOX(serverRow), serverSettings, false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), serverRow, false, true, 0);
    gtk_widget_show_all(dialog);
    const bool accepted = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK && *gtk_entry_get_text(GTK_ENTRY(name)) != '\0';
    std::string selected;
    if (accepted) {
        selected = gtk_entry_get_text(GTK_ENTRY(name));
        if (resolvedIndex >= 0) accounts.updateAt(static_cast<std::size_t>(resolvedIndex), selected, gtk_entry_get_text(GTK_ENTRY(password)), gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(forget)), picker->selected.empty() ? std::vector<std::string>() : std::vector<std::string>{picker->selected});
        else accounts.update(accountName, selected, gtk_entry_get_text(GTK_ENTRY(password)), gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(forget)), picker->selected.empty() ? std::vector<std::string>() : std::vector<std::string>{picker->selected});
    }
    gtk_widget_destroy(dialog);
    if (accepted) { refreshAccountMenu(); if (resolvedIndex >= 0) requestedAccountIndex = resolvedIndex; else for (std::size_t index = accounts.entries().size(); index > 0; --index) if (accounts.entries()[index - 1].name == selected) { requestedAccountIndex = static_cast<int>(index - 1); break; } selectAccount(selected); requestedAccountIndex = -1; }
    return accepted;
}

void TStartFrame::openAccountManager() {
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Accounts", nullptr, static_cast<GtkDialogFlags>(0), "Add Account", 100, "Edit", 101, "Delete", 102, "Close", GTK_RESPONSE_CLOSE, "Select", GTK_RESPONSE_OK, nullptr);
    applyGtkButtonIcon(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), 100), GTK_STOCK_ADD);
    applyGtkButtonIcon(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), 101), GTK_STOCK_EDIT);
    applyGtkButtonIcon(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), 102), GTK_STOCK_DELETE);
    applyGtkButtonIcon(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE), GTK_STOCK_CLOSE);
    applyGtkButtonIcon(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK), GTK_STOCK_OK);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
    gtk_widget_set_name(dialog, "AccountsDialog");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 330);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* heading = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(heading), "<b>Select an account to sign in</b>");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0F);
    gtk_container_set_border_width(GTK_CONTAINER(heading), 10);
    gtk_box_pack_start(GTK_BOX(content), heading, false, false, 0);
    GtkWidget* list = gtk_list_box_new();
    gtk_widget_set_name(list, "AccountsList");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), false);
    gtk_box_pack_start(GTK_BOX(content), list, true, true, 0);
    g_signal_connect(list, "row-activated", G_CALLBACK(+[](GtkListBox* listBox, GtkListBoxRow* row, gpointer data) { gtk_list_box_select_row(listBox, row); gtk_dialog_response(GTK_DIALOG(data), 101); }), dialog);
    auto populate = [&](const std::string& selectedName, int selectedIndex = -1) {
        GList* rows = gtk_container_get_children(GTK_CONTAINER(list));
        for (GList* item = rows; item != nullptr; item = item->next) gtk_widget_destroy(GTK_WIDGET(item->data));
        g_list_free(rows);
        const std::vector<SavedListServer> profiles = RC::loadListServerProfiles(std::filesystem::path(g_get_user_config_dir()) / "GScriptRC" / "listservers.conf", "listserver.graalonline.com", 14922);
        for (std::size_t index = 0; index < accounts.entries().size(); ++index) {
            const RC::RCAccount& account = accounts.entries()[index];
            GtkWidget* row = gtk_list_box_row_new();
            GtkWidget* rowBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            gtk_container_set_border_width(GTK_CONTAINER(rowBox), 10);
            GtkWidget* accountBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            GtkWidget* name = gtk_label_new(nullptr);
            gchar* escapedName = g_markup_escape_text(account.name.c_str(), -1);
            const std::string markup = "<b>" + std::string(escapedName) + "</b>";
            g_free(escapedName);
            gtk_label_set_markup(GTK_LABEL(name), markup.c_str());
            gtk_label_set_xalign(GTK_LABEL(name), 0.0F);
            gtk_box_pack_start(GTK_BOX(accountBox), name, false, false, 0);
            std::string associationText;
            std::string associationDetail;
            if (account.listServers.empty()) {
                associationText = "No saved list server";
                associationDetail = associationText;
            } else {
                const std::size_t shown = std::min<std::size_t>(account.listServers.size(), 2);
                for (std::size_t serverIndex = 0; serverIndex < shown; ++serverIndex) {
                    if (!associationText.empty()) associationText += "  •  ";
                    associationText += RC::normalizeListServerAssociation(account.listServers[serverIndex], profiles);
                }
                associationDetail = RC::accountServerDetail(account, profiles);
            }
            associationText.clear();
            for (const std::string& badge : RC::accountServerBadgeLabels(account, profiles)) { if (!associationText.empty()) associationText += ", "; associationText += badge; }
            GtkWidget* association = gtk_label_new(associationText.c_str());
            gtk_widget_set_name(association, "AccountServerAssociation");
            gtk_label_set_xalign(GTK_LABEL(association), 0.0F);
            gtk_label_set_ellipsize(GTK_LABEL(association), PANGO_ELLIPSIZE_END);
            gtk_style_context_add_class(gtk_widget_get_style_context(association), "dim-label");
            gtk_box_pack_start(GTK_BOX(accountBox), association, false, false, 0);
            gtk_box_pack_start(GTK_BOX(rowBox), accountBox, true, true, 0);
            gtk_container_add(GTK_CONTAINER(row), rowBox);
            g_object_set_data(G_OBJECT(row), "account-index", GINT_TO_POINTER(static_cast<int>(index + 1)));
            gtk_container_add(GTK_CONTAINER(list), row);
            if ((selectedIndex >= 0 && static_cast<int>(index) == selectedIndex) || (selectedIndex < 0 && account.name == selectedName)) gtk_list_box_select_row(GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(row));
        }
        gtk_widget_show_all(list);
    };
    populate(selectedAccount);
    gtk_widget_show_all(dialog);
    bool running = true;
    while (running) {
        const int response = gtk_dialog_run(GTK_DIALOG(dialog));
        GtkListBoxRow* selectedRow = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
        const int storedIndex = selectedRow == nullptr ? 0 : GPOINTER_TO_INT(g_object_get_data(G_OBJECT(selectedRow), "account-index"));
        const std::string name = storedIndex > 0 && static_cast<std::size_t>(storedIndex) <= accounts.entries().size() ? accounts.entries()[static_cast<std::size_t>(storedIndex - 1)].name : std::string();
        if (response == 100) {
            editAccount({}, GTK_WINDOW(dialog));
            refreshAccountMenu();
            populate(accounts.accountName());
            gtk_window_present(GTK_WINDOW(dialog));
        } else if (response == 101 && !name.empty()) {
            editAccount(name, GTK_WINDOW(dialog), storedIndex - 1);
            refreshAccountMenu();
            populate(accounts.accountName(), storedIndex - 1);
            gtk_window_present(GTK_WINDOW(dialog));
        } else if (response == 102 && !name.empty()) {
            accounts.removeAt(static_cast<std::size_t>(storedIndex - 1));
            refreshAccountMenu();
            selectAccount(accounts.accountName());
            populate(accounts.accountName());
            gtk_window_present(GTK_WINDOW(dialog));
        } else if (response == GTK_RESPONSE_OK && !name.empty()) {
            requestedAccountIndex = storedIndex - 1;
            selectAccount(name);
            requestedAccountIndex = -1;
            running = false;
        } else {
            running = false;
        }
    }
    gtk_widget_destroy(dialog);
}

void TStartFrame::connect() {
    selectedAccount = getText(accountField);
    if (selectedAccount.empty()) { gtk_widget_grab_focus(accountField); return; }
    options.nickname = getText(nicknameField);
    options.dontsavepassword = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(passwordCheck));
    options.graphicalmenu = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(graphicsCheck));
    RC::saveRCOptions(options, applicationDirectory);
    std::string endpoint;
    if (selectedAccountIndex >= 0) {
        const std::vector<std::string> associations = accounts.listServersForIndex(static_cast<std::size_t>(selectedAccountIndex));
        if (!associations.empty()) endpoint = associations.front();
    }
    if (endpoint.empty() && selectedAccountIndex < 0 && listServerEndpointCallback) endpoint = listServerEndpointCallback();
    if (endpoint.empty() && selectedAccountIndex >= 0) {
        const std::vector<SavedListServer> profiles = RC::loadListServerProfiles(std::filesystem::path(g_get_user_config_dir()) / "GScriptRC" / "listservers.conf", "listserver.graalonline.com", 14922);
        if (!profiles.empty()) endpoint = RC::listServerAssociation(profiles.front());
    }
    if (selectedAccountIndex >= 0) accounts.saveAt(static_cast<std::size_t>(selectedAccountIndex), getText(passwordField), options.dontsavepassword, endpoint);
    else accounts.save(selectedAccount, getText(passwordField), options.dontsavepassword, endpoint);
    gtk_widget_hide(window);
    onConnectCallback(selectedAccountIndex >= 0 ? accounts.idForIndex(static_cast<std::size_t>(selectedAccountIndex)) : accounts.activeId(), selectedAccount, getText(passwordField), options.nickname, endpoint);
}

std::string TStartFrame::getText(GtkWidget* widget) const { return gtk_entry_get_text(GTK_ENTRY(widget)); }
