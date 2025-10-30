#include "TAccountsWindow.h"

#include <grclib.h>

#include <map>
#include <sstream>

namespace {
    GtkWidget* labeledEntry(GtkGrid* grid, const char* label, int row) {
        GtkWidget* field = gtk_entry_new();
        gtk_grid_attach(grid, gtk_label_new(label), 0, row, 1, 1);
        gtk_grid_attach(grid, field, 1, row, 1, 1);
        return field;
    }

    std::map<std::string, std::string> valuesFromText(const char* content) {
        std::map<std::string, std::string> values;
        std::istringstream input(content == nullptr ? "" : content);
        for (std::string line; std::getline(input, line);) {
            const size_t equals = line.find('=');
            if (equals != std::string::npos) values[line.substr(0, equals)] = line.substr(equals + 1);
        }
        return values;
    }
}

TAccountsWindow::TAccountsWindow() {
    queryWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(queryWindow, "GetAccountsWindow");
    gtk_window_set_title(GTK_WINDOW(queryWindow), "Get Accounts List");
    gtk_window_set_default_size(GTK_WINDOW(queryWindow), 400, 220);
    GtkWidget* queryRoot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(queryWindow), queryRoot);
    GtkWidget* frame = gtk_frame_new(" Account specification ");
    gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
    GtkWidget* fields = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(fields), 4);
    gtk_grid_set_column_spacing(GTK_GRID(fields), 5);
    gtk_container_add(GTK_CONTAINER(frame), fields);
    accountField = labeledEntry(GTK_GRID(fields), "Account name spec:", 0);
    conditionsField = labeledEntry(GTK_GRID(fields), "Conditions:", 1);
    GtkWidget* examples = gtk_label_new("example: Stef%\nVariables: email='xxx', adminlevel=1, adminworlds like '%all%', blocked=1");
    gtk_label_set_xalign(GTK_LABEL(examples), 0.0F);
    gtk_label_set_line_wrap(GTK_LABEL(examples), true);
    gtk_grid_attach(GTK_GRID(fields), examples, 0, 2, 2, 1);
    gtk_box_pack_start(GTK_BOX(queryRoot), frame, true, true, 0);
    GtkWidget* queryButtons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_set_border_width(GTK_CONTAINER(queryButtons), 5);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(queryButtons), GTK_BUTTONBOX_END);
    GtkWidget* getList = gtk_button_new_with_label("Get List");
    GtkWidget* queryClose = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(queryButtons), getList);
    gtk_container_add(GTK_CONTAINER(queryButtons), queryClose);
    gtk_box_pack_start(GTK_BOX(queryRoot), queryButtons, false, false, 0);
    g_signal_connect(getList, "clicked", G_CALLBACK(onGetList), this);
    g_signal_connect(queryClose, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(queryWindow, "delete-event", G_CALLBACK(onDelete), this);

    listWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(listWindow, "AccountsListWindow");
    gtk_window_set_title(GTK_WINDOW(listWindow), "Accounts List");
    gtk_window_set_default_size(GTK_WINDOW(listWindow), 450, 320);
    GtkWidget* listRoot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(listWindow), listRoot);
    GtkWidget* listFrame = gtk_frame_new(" Accounts ");
    gtk_container_set_border_width(GTK_CONTAINER(listFrame), 5);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
    gtk_container_set_border_width(GTK_CONTAINER(scrolled), 5);
    store = gtk_list_store_new(1, G_TYPE_STRING);
    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), false);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(tree), true);
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column_new_with_attributes("", renderer, "text", 0, nullptr));
    gtk_container_add(GTK_CONTAINER(scrolled), tree);
    gtk_container_add(GTK_CONTAINER(listFrame), scrolled);
    gtk_box_pack_start(GTK_BOX(listRoot), listFrame, true, true, 0);
    GtkWidget* listButtons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_set_border_width(GTK_CONTAINER(listButtons), 5);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(listButtons), GTK_BUTTONBOX_END);
    GtkWidget* getAccounts = gtk_button_new_with_label("Get Accounts");
    GtkWidget* add = gtk_button_new_with_label("Add");
    GtkWidget* listClose = gtk_button_new_with_label("Close");
    gtk_widget_set_size_request(getAccounts, 80, 24);
    gtk_widget_set_size_request(add, 80, 24);
    gtk_widget_set_size_request(listClose, 80, 24);
    gtk_container_add(GTK_CONTAINER(listButtons), getAccounts);
    gtk_container_add(GTK_CONTAINER(listButtons), add);
    gtk_container_add(GTK_CONTAINER(listButtons), listClose);
    gtk_box_pack_start(GTK_BOX(listRoot), listButtons, false, false, 0);
    g_signal_connect(getAccounts, "clicked", G_CALLBACK(onGetAccounts), this);
    g_signal_connect(add, "clicked", G_CALLBACK(onAdd), this);
    g_signal_connect(listClose, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(tree, "row-activated", G_CALLBACK(onAccountActivated), this);
    g_signal_connect(listWindow, "delete-event", G_CALLBACK(onDelete), this);

    editorWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(editorWindow, "AccountEditorWindow");
    gtk_window_set_default_size(GTK_WINDOW(editorWindow), 300, 330);
    gtk_window_set_resizable(GTK_WINDOW(editorWindow), false);
    GtkWidget* editorRoot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(editorWindow), editorRoot);
    GtkWidget* editorLabel = gtk_label_new("Account attributes");
    gtk_widget_set_halign(editorLabel, GTK_ALIGN_START);
    gtk_widget_set_margin_start(editorLabel, 14);
    gtk_widget_set_margin_top(editorLabel, 8);
    GtkWidget* editorGrid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(editorGrid), 14);
    gtk_grid_set_row_spacing(GTK_GRID(editorGrid), 3);
    gtk_grid_set_column_spacing(GTK_GRID(editorGrid), 5);
    nameField = labeledEntry(GTK_GRID(editorGrid), "Account name:", 0);
    passwordField = labeledEntry(GTK_GRID(editorGrid), "Password:", 1);
    emailField = labeledEntry(GTK_GRID(editorGrid), "E-mail address:", 2);
    levelField = labeledEntry(GTK_GRID(editorGrid), "Admin level:", 3);
    worldsField = labeledEntry(GTK_GRID(editorGrid), "Admin worlds:", 4);
    bannedCheck = gtk_check_button_new_with_label("Banned");
    guestCheck = gtk_check_button_new_with_label("Guest");
    gtk_grid_attach(GTK_GRID(editorGrid), bannedCheck, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(editorGrid), guestCheck, 1, 5, 1, 1);
    banTimeField = labeledEntry(GTK_GRID(editorGrid), "Ban-Time:", 6);
    gtk_editable_set_editable(GTK_EDITABLE(banTimeField), false);
    gtk_grid_attach(GTK_GRID(editorGrid), gtk_label_new("Ban-Reason / Comments:"), 0, 7, 2, 1);
    reasonField = gtk_text_view_new();
    gtk_widget_set_name(reasonField, "AccountReason");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(reasonField), GTK_WRAP_WORD_CHAR);
    GtkCssProvider* reasonProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(reasonProvider, "#AccountEditorWindow entry { min-height: 0; padding: 2px; } #AccountReason, #AccountReason text, #AccountReasonScroll, #AccountReasonScroll viewport { background-color: #1e1e1e; color: #dddddd; }", -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(editorWindow), GTK_STYLE_PROVIDER(reasonProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(reasonProvider);
    GdkColor reasonBackground;
    GdkColor reasonText;
    gdk_color_parse("#1e1e1e", &reasonBackground);
    gdk_color_parse("#dddddd", &reasonText);
    gtk_widget_modify_base(reasonField, GTK_STATE_NORMAL, &reasonBackground);
    gtk_widget_modify_text(reasonField, GTK_STATE_NORMAL, &reasonText);
    GtkWidget* reasonScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_name(reasonScrolled, "AccountReasonScroll");
    gtk_widget_set_size_request(reasonScrolled, 0, 52);
    gtk_container_add(GTK_CONTAINER(reasonScrolled), reasonField);
    gtk_grid_attach(GTK_GRID(editorGrid), reasonScrolled, 0, 8, 2, 1);
    gtk_box_pack_start(GTK_BOX(editorRoot), editorLabel, false, false, 0);
    gtk_box_pack_start(GTK_BOX(editorRoot), editorGrid, true, true, 0);
    GtkWidget* editorButtons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_set_border_width(GTK_CONTAINER(editorButtons), 5);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(editorButtons), GTK_BUTTONBOX_END);
    GtkWidget* apply = gtk_button_new_with_label("Apply");
    GtkWidget* editorCancel = gtk_button_new_with_label("Cancel");
    gtk_container_add(GTK_CONTAINER(editorButtons), apply);
    gtk_container_add(GTK_CONTAINER(editorButtons), editorCancel);
    gtk_box_pack_start(GTK_BOX(editorRoot), editorButtons, false, false, 0);
    g_signal_connect(apply, "clicked", G_CALLBACK(onApply), this);
    g_signal_connect(editorCancel, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(editorWindow, "delete-event", G_CALLBACK(onDelete), this);
}

TAccountsWindow::~TAccountsWindow() { if (queryWindow != nullptr) gtk_widget_destroy(queryWindow); if (listWindow != nullptr) gtk_widget_destroy(listWindow); if (editorWindow != nullptr) gtk_widget_destroy(editorWindow); }
void TAccountsWindow::open(void* nextConnection) { connection = nextConnection; rc_on_player_text_data(connection, onPlayerTextData, this); gtk_widget_show_all(listWindow); gtk_window_present(GTK_WINDOW(listWindow)); }
void TAccountsWindow::openQuery() { gtk_widget_show_all(queryWindow); gtk_window_present(GTK_WINDOW(queryWindow)); gtk_widget_grab_focus(accountField); }
void TAccountsWindow::setAccounts(const char* accounts) { gtk_list_store_clear(store); std::istringstream input(accounts == nullptr ? "" : accounts); for (std::string account; std::getline(input, account);) { GtkTreeIter row; gtk_list_store_append(store, &row); gtk_list_store_set(store, &row, 0, account.c_str(), -1); } gtk_widget_show_all(listWindow); gtk_window_present(GTK_WINDOW(listWindow)); }
void TAccountsWindow::showEditor(void* nextConnection, const std::string& account, const char* content) { connection = nextConnection; openEditor(account, content); }
void TAccountsWindow::openEditor(const std::string& account, const char* content) {
    editingAccount = account;
    const std::map<std::string, std::string> values = valuesFromText(content);
    gtk_window_set_title(GTK_WINDOW(editorWindow), account.empty() ? "Add new account" : ("Edit account of " + account).c_str());
    gtk_entry_set_text(GTK_ENTRY(nameField), values.count("account") ? values.at("account").c_str() : account.c_str());
    gtk_entry_set_text(GTK_ENTRY(passwordField), values.count("password") ? values.at("password").c_str() : "");
    gtk_entry_set_text(GTK_ENTRY(emailField), values.count("email") ? values.at("email").c_str() : "");
    gtk_entry_set_text(GTK_ENTRY(levelField), values.count("admin_level") ? values.at("admin_level").c_str() : "0");
    gtk_entry_set_text(GTK_ENTRY(worldsField), values.count("admin_worlds") ? values.at("admin_worlds").c_str() : "all");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bannedCheck), values.count("banned") && (values.at("banned") == "1" || values.at("banned") == "true"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(guestCheck), values.count("guest") && (values.at("guest") == "1" || values.at("guest") == "true"));
    gtk_entry_set_text(GTK_ENTRY(banTimeField), values.count("ban_length") ? values.at("ban_length").c_str() : "");
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(reasonField));
    gtk_text_buffer_set_text(buffer, values.count("ban_reason") ? values.at("ban_reason").c_str() : "", -1);
    gtk_widget_show_all(editorWindow);
    gtk_window_present(GTK_WINDOW(editorWindow));
}
std::string TAccountsWindow::accountText() const {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(reasonField));
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar* reason = gtk_text_buffer_get_text(buffer, &start, &end, false);
    std::string content = "account=" + std::string(gtk_entry_get_text(GTK_ENTRY(nameField))) + "\npassword=" + gtk_entry_get_text(GTK_ENTRY(passwordField)) + "\nemail=" + gtk_entry_get_text(GTK_ENTRY(emailField)) + "\nadmin_level=" + gtk_entry_get_text(GTK_ENTRY(levelField)) + "\nadmin_worlds=" + gtk_entry_get_text(GTK_ENTRY(worldsField)) + "\nbanned=" + (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(bannedCheck)) ? "1" : "0") + "\nguest=" + (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(guestCheck)) ? "1" : "0") + "\nban_length=" + gtk_entry_get_text(GTK_ENTRY(banTimeField)) + "\nban_reason=" + (reason == nullptr ? "" : reason);
    g_free(reason);
    return content;
}
void TAccountsWindow::onGetList(GtkButton*, gpointer data) { TAccountsWindow* window = static_cast<TAccountsWindow*>(data); rc_request_account_list(window->connection, gtk_entry_get_text(GTK_ENTRY(window->accountField)), gtk_entry_get_text(GTK_ENTRY(window->conditionsField))); }
void TAccountsWindow::onGetAccounts(GtkButton*, gpointer data) { static_cast<TAccountsWindow*>(data)->openQuery(); }
void TAccountsWindow::onAdd(GtkButton*, gpointer data) { static_cast<TAccountsWindow*>(data)->openEditor("", ""); }
void TAccountsWindow::onAccountActivated(GtkTreeView* tree, GtkTreePath*, GtkTreeViewColumn*, gpointer data) { TAccountsWindow* window = static_cast<TAccountsWindow*>(data); GtkTreeModel* model = nullptr; GtkTreeIter row; if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(tree), &model, &row)) return; gchar* account = nullptr; gtk_tree_model_get(model, &row, 0, &account, -1); if (account != nullptr) rc_request_player_account(window->connection, account); g_free(account); }
void TAccountsWindow::onApply(GtkButton*, gpointer data) { TAccountsWindow* window = static_cast<TAccountsWindow*>(data); const std::string content = window->accountText(); if (window->editingAccount.empty()) rc_add_player_account(window->connection, content.c_str()); else rc_set_player_account(window->connection, window->editingAccount.c_str(), content.c_str()); }
void TAccountsWindow::onClose(GtkButton* button, gpointer) { gtk_widget_hide(gtk_widget_get_toplevel(GTK_WIDGET(button))); }
gboolean TAccountsWindow::onDelete(GtkWidget* widget, GdkEvent*, gpointer) { gtk_widget_hide(widget); return true; }
void TAccountsWindow::onPlayerTextData(const char* type, const char* account, const char* content, void* data) { if (type != nullptr && std::string(type) == "account") static_cast<TAccountsWindow*>(data)->openEditor(account == nullptr ? "" : account, content); }
