#include "TStartFrame.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace {

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

    std::string joinServers(const std::vector<std::string>& servers) {
        std::string result;
        for (std::size_t index = 0; index < servers.size(); ++index) { if (index != 0) result += ", "; result += servers[index]; }
        return result;
    }

}

TStartFrame::TStartFrame(RC::RCOptions& options, const std::filesystem::path& applicationDirectory, ConnectCallback onConnect, ListServerSettingsCallback onListServerSettings, ListServerEndpointCallback listServerEndpoint)
    : options(options), accounts(), applicationDirectory(applicationDirectory), onConnectCallback(std::move(onConnect)), onListServerSettingsCallback(std::move(onListServerSettings)), listServerEndpointCallback(std::move(listServerEndpoint)) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "StartFrame");
    gtk_window_set_title(GTK_WINDOW(window), "Remote Control");
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(window), 280, 220);
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
    accountCombo = gtk_combo_box_text_new_with_entry();
    gtk_widget_set_hexpand(accountCombo, true);
    gtk_widget_set_name(accountCombo, "AccountPicker");
    accountField = gtk_bin_get_child(GTK_BIN(accountCombo));
    gtk_widget_set_name(accountField, "AccountField");
    gtk_entry_set_placeholder_text(GTK_ENTRY(accountField), "Add an account");
    accountManageButton = gtk_button_new_from_icon_name("document-edit-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_name(accountManageButton, "AccountManageButton");
    gtk_style_context_add_class(gtk_widget_get_style_context(accountManageButton), "account-manage-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(accountManageButton), "flat");
    gtk_widget_set_halign(accountManageButton, GTK_ALIGN_END);
    gtk_widget_set_valign(accountManageButton, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_end(accountManageButton, 32);
    gtk_widget_set_size_request(accountManageButton, 20, 20);
    gtk_widget_set_can_focus(accountManageButton, true);
    gtk_widget_set_focus_on_click(accountManageButton, true);
    gtk_widget_set_tooltip_text(accountManageButton, "Manage accounts");
    atk_object_set_name(gtk_widget_get_accessible(accountManageButton), "Manage accounts");
    GtkCssProvider* accountCss = gtk_css_provider_new();
    gtk_css_provider_load_from_data(accountCss, "#AccountField { padding-right: 24px; } #AccountManageButton, #AccountManageButton:hover, #AccountManageButton:active, #AccountManageButton:focus { min-width: 20px; min-height: 20px; padding: 0; border: none; border-radius: 2px; box-shadow: none; background-image: none; background-color: transparent; }", -1, nullptr);
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
    GtkWidget* cancelButton = gtk_button_new_with_label("Cancel");
    gtk_box_pack_end(GTK_BOX(buttons), cancelButton, false, false, 0);
    gtk_box_pack_end(GTK_BOX(buttons), connectButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, true, 0);

    g_signal_connect(connectButton, "clicked", G_CALLBACK(TStartFrame::onConnect), this);
    g_signal_connect(accountCombo, "changed", G_CALLBACK(TStartFrame::onAccountChanged), this);
    g_signal_connect(accountField, "changed", G_CALLBACK(TStartFrame::onAccountEntryChanged), this);
    g_signal_connect(accountManageButton, "clicked", G_CALLBACK(TStartFrame::onManageAccounts), this);
    g_signal_connect(accountEvent, "enter-notify-event", G_CALLBACK(TStartFrame::onAccountPointerEnter), this);
    g_signal_connect(accountEvent, "leave-notify-event", G_CALLBACK(TStartFrame::onAccountPointerLeave), this);
    g_signal_connect(accountField, "focus-in-event", G_CALLBACK(TStartFrame::onAccountFocusIn), this);
    g_signal_connect(accountField, "focus-out-event", G_CALLBACK(TStartFrame::onAccountFocusOut), this);
    g_signal_connect(listServerSettings, "clicked", G_CALLBACK(TStartFrame::onListServerSettings), this);
    g_signal_connect(cancelButton, "clicked", G_CALLBACK(onCancel), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
    g_signal_connect(window, "destroy", G_CALLBACK(onDestroy), this);

    refreshAccountMenu();
    selectAccount(accounts.accountName());
}

TStartFrame::~TStartFrame() { if (window != nullptr) gtk_widget_destroy(window); }
void TStartFrame::show() { gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); }
void TStartFrame::toggleVisibility() { if (gtk_widget_get_visible(window)) gtk_widget_hide(window); else show(); }
bool TStartFrame::mcpVisible() const { return gtk_widget_get_visible(window); }
std::string TStartFrame::mcpAccount() const { return getText(accountField); }
std::string TStartFrame::mcpNickname() const { return getText(nicknameField); }
bool TStartFrame::mcpHasPassword() const { return !getText(passwordField).empty(); }

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
    gchar* value = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    if (value == nullptr) return;
    const std::string account = value;
    g_free(value);
    if (account != frame->selectedAccount) frame->selectAccount(account);
}
void TStartFrame::onAccountEntryChanged(GtkEditable* editable, gpointer data) { static_cast<TStartFrame*>(data)->selectedAccount = gtk_entry_get_text(GTK_ENTRY(editable)); }
gboolean TStartFrame::onAccountPointerEnter(GtkWidget*, GdkEventCrossing*, gpointer data) { TStartFrame* frame = static_cast<TStartFrame*>(data); frame->accountHovered = true; gtk_widget_show(frame->accountManageButton); return false; }
gboolean TStartFrame::onAccountPointerLeave(GtkWidget*, GdkEventCrossing* event, gpointer data) {
    TStartFrame* frame = static_cast<TStartFrame*>(data);
    if (event->detail != GDK_NOTIFY_INFERIOR) frame->accountHovered = false;
    if (event->detail != GDK_NOTIFY_INFERIOR && !gtk_widget_has_focus(frame->accountField)) gtk_widget_hide(frame->accountManageButton);
    return false;
}
gboolean TStartFrame::onAccountFocusIn(GtkWidget*, GdkEventFocus*, gpointer data) { gtk_widget_show(static_cast<TStartFrame*>(data)->accountManageButton); return false; }
gboolean TStartFrame::onAccountFocusOut(GtkWidget*, GdkEventFocus*, gpointer data) {
    TStartFrame* frame = static_cast<TStartFrame*>(data);
    g_idle_add(+[](gpointer value) -> gboolean {
        TStartFrame* frame = static_cast<TStartFrame*>(value);
        if (!frame->accountHovered && !gtk_widget_has_focus(frame->accountField) && !gtk_widget_has_focus(frame->accountManageButton)) gtk_widget_hide(frame->accountManageButton);
        return G_SOURCE_REMOVE;
    }, frame);
    return false;
}
void TStartFrame::onCancel(GtkButton*, gpointer) { if (gtk_main_level() > 0) gtk_main_quit(); }
void TStartFrame::onDestroy(GtkWidget*, gpointer data) { static_cast<TStartFrame*>(data)->window = nullptr; if (gtk_main_level() > 0) gtk_main_quit(); }
gboolean TStartFrame::onDelete(GtkWidget*, GdkEvent*, gpointer) { return false; }

void TStartFrame::selectAccount(const std::string& accountName) {
    selectedAccount = accountName;
    if (selectedAccount.empty() && !accounts.entries().empty()) selectedAccount = accounts.entries().front().name;
    int active = accounts.entries().empty() ? 0 : -1;
    for (std::size_t index = 0; index < accounts.entries().size(); ++index) if (accounts.entries()[index].name == selectedAccount) { active = static_cast<int>(index); break; }
    gtk_combo_box_set_active(GTK_COMBO_BOX(accountCombo), active);
    gtk_entry_set_text(GTK_ENTRY(accountField), selectedAccount.c_str());
    gtk_entry_set_text(GTK_ENTRY(passwordField), accounts.passwordFor(selectedAccount).c_str());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(passwordCheck), accounts.passwordFor(selectedAccount).empty());
}

void TStartFrame::refreshAccountMenu() {
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(accountCombo));
    for (const RC::RCAccount& account : accounts.entries()) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(accountCombo), account.name.c_str());
}

bool TStartFrame::editAccount(const std::string& accountName) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(accountName.empty() ? "Add Account" : "Edit Account", GTK_WINDOW(window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_OK, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 430, -1);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box, true, true, 0);
    GtkWidget* name = gtk_entry_new();
    GtkWidget* password = gtk_entry_new();
    GtkWidget* servers = gtk_entry_new();
    GtkWidget* forget = gtk_check_button_new_with_label("Don't save password");
    gtk_entry_set_visibility(GTK_ENTRY(password), false);
    gtk_entry_set_placeholder_text(GTK_ENTRY(servers), "host:port, another-host:port");
    gtk_entry_set_text(GTK_ENTRY(name), accountName.c_str());
    gtk_entry_set_text(GTK_ENTRY(password), accounts.passwordFor(accountName).c_str());
    gtk_entry_set_text(GTK_ENTRY(servers), joinServers(accounts.listServersFor(accountName)).c_str());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(forget), !accountName.empty() && accounts.passwordFor(accountName).empty());
    GtkWidget* nameLabel = gtk_label_new("Account name"); gtk_label_set_xalign(GTK_LABEL(nameLabel), 0.0F);
    GtkWidget* passwordLabel = gtk_label_new("Password"); gtk_label_set_xalign(GTK_LABEL(passwordLabel), 0.0F);
    GtkWidget* serverLabel = gtk_label_new("List servers"); gtk_label_set_xalign(GTK_LABEL(serverLabel), 0.0F);
    gtk_box_pack_start(GTK_BOX(box), nameLabel, false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), name, false, true, 0);
    gtk_box_pack_start(GTK_BOX(box), passwordLabel, false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), password, false, true, 0);
    gtk_box_pack_start(GTK_BOX(box), forget, false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), serverLabel, false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), servers, false, true, 0);
    gtk_widget_show_all(dialog);
    const bool accepted = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK && *gtk_entry_get_text(GTK_ENTRY(name)) != '\0';
    std::string selected;
    if (accepted) {
        selected = gtk_entry_get_text(GTK_ENTRY(name));
        accounts.update(accountName, selected, gtk_entry_get_text(GTK_ENTRY(password)), gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(forget)), splitServers(gtk_entry_get_text(GTK_ENTRY(servers))));
    }
    gtk_widget_destroy(dialog);
    if (accepted) { refreshAccountMenu(); selectAccount(selected); }
    return accepted;
}

void TStartFrame::openAccountManager() {
    bool reopen = true;
    while (reopen) {
        reopen = false;
        GtkWidget* dialog = gtk_dialog_new_with_buttons("Accounts", GTK_WINDOW(window), GTK_DIALOG_MODAL, "Add Account", 100, "Edit", 101, "Delete", 102, "Cancel", GTK_RESPONSE_CANCEL, "Select", GTK_RESPONSE_OK, nullptr);
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
        gtk_box_pack_start(GTK_BOX(content), list, true, true, 0);
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
            GtkWidget* badgeBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            if (account.listServers.empty()) {
                GtkWidget* empty = gtk_label_new("No saved list server");
                gtk_style_context_add_class(gtk_widget_get_style_context(empty), "dim-label");
                gtk_box_pack_start(GTK_BOX(badgeBox), empty, false, false, 0);
            } else {
                const std::size_t shown = std::min<std::size_t>(account.listServers.size(), 2);
                for (std::size_t serverIndex = 0; serverIndex < shown; ++serverIndex) {
                    GtkWidget* badge = gtk_frame_new(nullptr);
                    GtkWidget* label = gtk_label_new(account.listServers[serverIndex].c_str());
                    gtk_widget_set_margin_start(label, 4);
                    gtk_widget_set_margin_end(label, 4);
                    gtk_widget_set_margin_top(label, 2);
                    gtk_widget_set_margin_bottom(label, 2);
                    gtk_container_add(GTK_CONTAINER(badge), label);
                    gtk_box_pack_start(GTK_BOX(badgeBox), badge, false, false, 0);
                }
                if (account.listServers.size() > shown) gtk_box_pack_start(GTK_BOX(badgeBox), gtk_label_new(("+" + std::to_string(account.listServers.size() - shown)).c_str()), false, false, 0);
            }
            gtk_box_pack_start(GTK_BOX(accountBox), badgeBox, false, false, 0);
            gtk_box_pack_start(GTK_BOX(rowBox), accountBox, true, true, 0);
            const std::string countText = std::to_string(account.listServers.size()) + (account.listServers.size() == 1 ? " server" : " servers");
            GtkWidget* count = gtk_label_new(countText.c_str());
            gtk_box_pack_end(GTK_BOX(rowBox), count, false, false, 0);
            gtk_container_add(GTK_CONTAINER(row), rowBox);
            g_object_set_data(G_OBJECT(row), "account-index", GINT_TO_POINTER(static_cast<int>(index + 1)));
            gtk_container_add(GTK_CONTAINER(list), row);
            if (account.name == selectedAccount) gtk_list_box_select_row(GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(row));
        }
        gtk_widget_show_all(dialog);
        const int response = gtk_dialog_run(GTK_DIALOG(dialog));
        GtkListBoxRow* selectedRow = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
        const int storedIndex = selectedRow == nullptr ? 0 : GPOINTER_TO_INT(g_object_get_data(G_OBJECT(selectedRow), "account-index"));
        const std::string name = storedIndex > 0 && static_cast<std::size_t>(storedIndex) <= accounts.entries().size() ? accounts.entries()[static_cast<std::size_t>(storedIndex - 1)].name : std::string();
        gtk_widget_destroy(dialog);
        if (response == 100) { editAccount({}); reopen = true; }
        else if (response == 101 && !name.empty()) { editAccount(name); reopen = true; }
        else if (response == 102 && !name.empty()) { accounts.remove(name); refreshAccountMenu(); selectAccount(accounts.accountName()); reopen = true; }
        else if (response == GTK_RESPONSE_OK && !name.empty()) selectAccount(name);
    }
}

void TStartFrame::connect() {
    selectedAccount = getText(accountField);
    if (selectedAccount.empty()) { gtk_widget_grab_focus(accountField); return; }
    options.nickname = getText(nicknameField);
    options.dontsavepassword = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(passwordCheck));
    options.graphicalmenu = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(graphicsCheck));
    RC::saveRCOptions(options, applicationDirectory);
    const std::string endpoint = listServerEndpointCallback ? listServerEndpointCallback() : std::string();
    accounts.save(selectedAccount, getText(passwordField), options.dontsavepassword, endpoint);
    gtk_widget_hide(window);
    onConnectCallback(selectedAccount, getText(passwordField), options.nickname);
}

std::string TStartFrame::getText(GtkWidget* widget) const { return gtk_entry_get_text(GTK_ENTRY(widget)); }
