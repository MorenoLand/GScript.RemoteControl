#include "TServerList.h"
#include "TButtonIcons.h"
#include "TRCAccounts.h"
#include "TDebug.h"
#include "TErrorWindow.h"
#include "TTreeSearch.h"
#include "TAssetPaths.h"
#include "TTheme.h"

#include <grclib.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <glib.h>
#include <map>
namespace {

    constexpr const char* defaultListserverHost = "listserver.graalonline.com";
    constexpr int listserverPort = 14922;

    std::filesystem::path listserverSettingsPath() { return std::filesystem::path(g_get_user_config_dir()) / "GScriptRC" / "listservers.conf"; }

    bool associationEndpoint(const std::string& association, std::string& name, std::string& host, int& port) {
        if (association.empty()) return false;
        const std::vector<SavedListServer> profiles = RC::loadListServerProfiles(listserverSettingsPath(), defaultListserverHost, listserverPort);
        for (const SavedListServer& profile : profiles) {
            const std::string endpoint = profile.host + ":" + std::to_string(profile.port);
            if (association == profile.name || association == endpoint || association.find(endpoint) != std::string::npos) { name = profile.name; host = profile.host; port = profile.port; return true; }
        }
        const std::size_t separator = association.rfind(':');
        if (separator == std::string::npos) return false;
        const int parsedPort = std::atoi(association.substr(separator + 1).c_str());
        if (parsedPort <= 0 || parsedPort > 65535) return false;
        name = association.substr(0, separator);
        host = name;
        port = parsedPort;
        return true;
    }

    std::string savedListserverText(const SavedListServer& server) { return server.name + " — " + server.host + ":" + std::to_string(server.port); }

    void saveListserverEndpoints(const std::vector<SavedListServer>& endpoints, int selectedIndex = -1) {
        std::vector<SavedListServer> ordered = endpoints;
        if (selectedIndex > 0 && static_cast<std::size_t>(selectedIndex) < ordered.size()) {
            SavedListServer selected = ordered[static_cast<std::size_t>(selectedIndex)];
            ordered.erase(ordered.begin() + selectedIndex);
            ordered.insert(ordered.begin(), selected);
        }
        RC::saveListServerProfiles(listserverSettingsPath(), ordered);
    }

    int getServerListIcon(const std::string& value) {
        if (value.size() < 2 || value[1] != ' ') return 2;
        if (value[0] == 'P') return 0;
        if (value[0] == 'U') return 1;
        if (value[0] == '3') return 3;
        if (value[0] == 'H') return 2;
        return 2;
    }

    std::string getServerListName(const std::string& value) {
        return value.size() > 1 && value[1] == ' ' ? value.substr(2) : value;
    }

}

TServerList::TServerList(std::function<void()> onClose, std::function<void(TServerList*, void*, int, const std::string&, const std::string&, const std::string&, bool)> onConnected, std::function<void()> onServerSelected, bool nextDarkMode, const std::string& nextTheme, std::function<void(bool, const std::string&)> onThemeChanged, std::function<std::vector<RC::RCAccount>()> accountChoices, std::function<std::vector<RC::RCAccount>(const std::string&)> accountChoicesForListServer, std::function<void()> onOpenAnother) : TServerList(std::filesystem::current_path(), std::move(onClose), std::move(onConnected), std::move(onServerSelected), nextDarkMode, nextTheme, std::move(onThemeChanged), std::move(accountChoices), std::move(accountChoicesForListServer), std::move(onOpenAnother)) {}

TServerList::TServerList(const std::filesystem::path& nextApplicationDirectory, std::function<void()> onClose, std::function<void(TServerList*, void*, int, const std::string&, const std::string&, const std::string&, bool)> onConnected, std::function<void()> onServerSelected, bool nextDarkMode, const std::string& nextTheme, std::function<void(bool, const std::string&)> onThemeChanged, std::function<std::vector<RC::RCAccount>()> accountChoices, std::function<std::vector<RC::RCAccount>(const std::string&)> accountChoicesForListServer, std::function<void()> onOpenAnother) : onCloseCallback(std::move(onClose)), onConnectedCallback(std::move(onConnected)), onServerSelectedCallback(std::move(onServerSelected)), onThemeChangedCallback(std::move(onThemeChanged)), accountChoicesCallback(std::move(accountChoices)), accountChoicesForListServerCallback(std::move(accountChoicesForListServer)), onOpenAnotherCallback(std::move(onOpenAnother)), applicationDirectory(nextApplicationDirectory), darkMode(nextDarkMode), theme(nextTheme) {
    listserverHost = defaultListserverHost;
    listserverEndpoints = RC::loadListServerProfiles(listserverSettingsPath(), defaultListserverHost, listserverPort);
    listserverName = listserverEndpoints.front().name;
    listserverHost = listserverEndpoints.front().host;
    listserverPort = listserverEndpoints.front().port;
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    applyRemoteControlWindowChrome(window);
    gtk_widget_set_name(window, "ServerList");
    gtk_window_set_title(GTK_WINDOW(window), (listserverName + " Servers").c_str());
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(window), 520, 350);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);

    GtkWidget* frame = gtk_frame_new(" Servers ");
    gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
    gtk_box_pack_start(GTK_BOX(root), frame, true, true, 0);

    GtkWidget* pane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_set_border_width(GTK_CONTAINER(pane), 5);
    gtk_paned_set_position(GTK_PANED(pane), 260);
    gtk_paned_set_wide_handle(GTK_PANED(pane), false);
    gtk_container_add(GTK_CONTAINER(frame), pane);

    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_paned_pack1(GTK_PANED(pane), scrolled, true, true);

    GError* error = nullptr;
    serverIcons[0] = gdk_pixbuf_new_from_file(resolveRuntimeImage(applicationDirectory, "rcicon_gold.png").string().c_str(), &error);
    if (error != nullptr) g_error_free(error);
    error = nullptr;
    serverIcons[1] = gdk_pixbuf_new_from_file(resolveRuntimeImage(applicationDirectory, "rcicon_uc.png").string().c_str(), &error);
    if (error != nullptr) g_error_free(error);
    error = nullptr;
    serverIcons[2] = gdk_pixbuf_new_from_file(resolveRuntimeImage(applicationDirectory, "rcicon_bronze.png").string().c_str(), &error);
    if (error != nullptr) g_error_free(error);
    error = nullptr;
    serverIcons[3] = gdk_pixbuf_new_from_file(resolveRuntimeImage(applicationDirectory, "rcicon_silver.png").string().c_str(), &error);
    if (error != nullptr) g_error_free(error);

    store = gtk_list_store_new(5, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
    tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_widget_set_name(tree, "ServerListField");
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(tree), true);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(tree), 1);
    gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(tree), treeSearchContains, nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scrolled), tree);

    GtkCellRenderer* serverRenderer = gtk_cell_renderer_text_new();
    GtkCellRenderer* iconRenderer = gtk_cell_renderer_pixbuf_new();
    GtkTreeViewColumn* iconColumn = gtk_tree_view_column_new_with_attributes("", iconRenderer, "pixbuf", 0, nullptr);
    gtk_tree_view_column_set_fixed_width(iconColumn, 24);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), iconColumn);

    GtkTreeViewColumn* serverColumn = gtk_tree_view_column_new_with_attributes("Server", serverRenderer, "text", 1, nullptr);
    gtk_tree_view_column_set_resizable(serverColumn, true);
    gtk_tree_view_column_set_fixed_width(serverColumn, 150);
    gtk_tree_view_column_set_sizing(serverColumn, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_sort_column_id(serverColumn, 1);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), serverColumn);

    GtkCellRenderer* playerRenderer = gtk_cell_renderer_text_new();
    g_object_set(playerRenderer, "xalign", 1.0F, "xpad", 6, nullptr);
    GtkTreeViewColumn* playerColumn = gtk_tree_view_column_new_with_attributes("Players", playerRenderer, "text", 2, nullptr);
    gtk_tree_view_column_set_resizable(playerColumn, true);
    gtk_tree_view_column_set_fixed_width(playerColumn, 70);
    gtk_tree_view_column_set_sizing(playerColumn, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_sort_column_id(playerColumn, 3);
    gtk_tree_view_column_set_alignment(playerColumn, 1.0F);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), playerColumn);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), 3, GTK_SORT_DESCENDING);

    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    g_signal_connect(selection, "changed", G_CALLBACK(onSelectionChanged), this);
    g_signal_connect(tree, "row-activated", G_CALLBACK(onRowActivated), this);

    GtkWidget* detailsFrame = gtk_frame_new(" Server info ");
    gtk_container_set_border_width(GTK_CONTAINER(detailsFrame), 5);
    gtk_widget_set_hexpand(detailsFrame, true);
    gtk_widget_set_size_request(detailsFrame, 0, -1);
    gtk_paned_pack2(GTK_PANED(pane), detailsFrame, true, true);
    GtkCssProvider* paneCss = gtk_css_provider_new();
    gtk_css_provider_load_from_data(paneCss, "paned > separator { min-width: 1px; min-height: 1px; background-color: transparent; border: none; box-shadow: none; }", -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(pane), GTK_STYLE_PROVIDER(paneCss), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(paneCss);

    GtkWidget* details = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_container_set_border_width(GTK_CONTAINER(details), 5);
    gtk_container_add(GTK_CONTAINER(detailsFrame), details);

    GtkWidget* languageRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget* languageLabel = gtk_label_new("Language:");
    gtk_widget_set_size_request(languageLabel, 80, -1);
    gtk_label_set_xalign(GTK_LABEL(languageLabel), 0.0F);
    languageField = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(languageField), 10);
    gtk_editable_set_editable(GTK_EDITABLE(languageField), false);
    gtk_box_pack_start(GTK_BOX(languageRow), languageLabel, false, false, 0);
    gtk_box_pack_end(GTK_BOX(languageRow), languageField, true, true, 0);
    gtk_box_pack_start(GTK_BOX(details), languageRow, false, true, 0);

    GtkWidget* versionRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget* versionLabel = gtk_label_new("Version:");
    gtk_widget_set_size_request(versionLabel, 80, -1);
    gtk_label_set_xalign(GTK_LABEL(versionLabel), 0.0F);
    versionField = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(versionField), 10);
    gtk_editable_set_editable(GTK_EDITABLE(versionField), false);
    gtk_box_pack_start(GTK_BOX(versionRow), versionLabel, false, false, 0);
    gtk_box_pack_end(GTK_BOX(versionRow), versionField, true, true, 0);
    gtk_box_pack_start(GTK_BOX(details), versionRow, false, true, 0);

    GtkWidget* descriptionLabel = gtk_label_new("Description:");
    gtk_label_set_xalign(GTK_LABEL(descriptionLabel), 0.0F);
    gtk_box_pack_start(GTK_BOX(details), descriptionLabel, false, false, 0);
    GtkWidget* descriptionScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_name(descriptionScrolled, "ServerDescription");
    GtkCssProvider* descriptionCss = gtk_css_provider_new();
    gtk_css_provider_load_from_data(descriptionCss, "#ServerDescription { border: 1px solid #555555; border-radius: 4px; background-color: transparent; }", -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(descriptionScrolled), GTK_STYLE_PROVIDER(descriptionCss), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(descriptionCss);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(descriptionScrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    descriptionField = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(descriptionField), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(descriptionField), false);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(descriptionField), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(descriptionScrolled), descriptionField);
    gtk_box_pack_start(GTK_BOX(details), descriptionScrolled, true, true, 0);

    GtkWidget* homepageRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget* homepageLabel = gtk_label_new("Homepage:");
    gtk_widget_set_size_request(homepageLabel, 80, -1);
    gtk_label_set_xalign(GTK_LABEL(homepageLabel), 0.0F);
    homepageField = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(homepageField), 10);
    gtk_editable_set_editable(GTK_EDITABLE(homepageField), false);
    GtkWidget* homepageButton = gtk_button_new_with_label(">");
    gtk_widget_set_size_request(homepageButton, 24, -1);
    gtk_box_pack_start(GTK_BOX(homepageRow), homepageLabel, false, false, 0);
    gtk_box_pack_start(GTK_BOX(homepageRow), homepageField, true, true, 0);
    gtk_box_pack_end(GTK_BOX(homepageRow), homepageButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(details), homepageRow, false, true, 0);

    statusField = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(statusField), 0.0F);
    gtk_box_pack_start(GTK_BOX(details), statusField, false, false, 0);

    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_set_border_width(GTK_CONTAINER(buttons), 5);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    refreshButton = gtk_button_new_with_label("Refresh");
    connectButton = gtk_button_new_with_label("Connect");
    gtk_button_set_image(GTK_BUTTON(refreshButton), gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
    gtk_button_set_image(GTK_BUTTON(connectButton), gtk_image_new_from_stock(GTK_STOCK_CONNECT, GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(refreshButton), true);
    gtk_button_set_always_show_image(GTK_BUTTON(connectButton), true);
    gtk_container_add(GTK_CONTAINER(buttons), refreshButton);
    gtk_container_add(GTK_CONTAINER(buttons), connectButton);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, true, 0);

    g_signal_connect(refreshButton, "clicked", G_CALLBACK(onRefresh), this);
    g_signal_connect(connectButton, "clicked", G_CALLBACK(onConnect), this);
    g_signal_connect(tree, "button-press-event", G_CALLBACK(onTreeButtonPress), this);
    g_signal_connect(homepageButton, "clicked", G_CALLBACK(onHomepage), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TServerList::~TServerList() {
    callbackAlive->store(false);
    if (connectWorker.joinable()) connectWorker.join();
    if (worker.joinable()) worker.join();
    std::lock_guard lock(connectionMutex);
    if (connection != nullptr) rc_disconnect(connection);
    for (GdkPixbuf* icon : serverIcons) if (icon != nullptr) g_object_unref(icon);
    if (window != nullptr) gtk_widget_destroy(window);
    if (store != nullptr) g_object_unref(store);
}

void TServerList::open(std::uint64_t accountId, const std::string& account, const std::string& password, const std::string& nickname, const std::string& listServer) {
    this->accountId = accountId;
    this->account = account;
    this->password = password;
    this->nickname = nickname;
    std::string selectedName;
    std::string selectedHost;
    int selectedPort = 0;
    if (associationEndpoint(listServer, selectedName, selectedHost, selectedPort)) { listserverName = selectedName; listserverHost = selectedHost; listserverPort = selectedPort; }
    gtk_window_set_title(GTK_WINDOW(window), (listserverName + " Servers").c_str());
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    refresh();
}

void TServerList::reopen() {
    if (account.empty()) return;
    if (worker.joinable()) return;
    disconnectCurrentConnection();
    remoteControlDebugLog("opening a fresh listserver connection");
    show();
    refresh();
}

std::vector<std::string> TServerList::mcpServerNames() const { std::vector<std::string> names; for (const ServerEntry& entry : entries) names.push_back(entry.name); return names; }
bool TServerList::mcpConnect(const std::string& name, std::string& error) {
    int selected = -1;
    for (std::size_t index = 0; index < entries.size(); ++index) if (g_ascii_strcasecmp(entries[index].name.c_str(), name.c_str()) == 0) { selected = static_cast<int>(index); break; }
    if (selected < 0) { error = entries.empty() ? "Server list is not loaded" : "Named server was not found"; return false; }
    { std::lock_guard lock(connectionMutex); if (connection == nullptr) { error = "Listserver connection is not ready"; return false; } }
    GtkTreeModel* model = GTK_TREE_MODEL(store);
    GtkTreeIter iter;
    bool found = gtk_tree_model_get_iter_first(model, &iter);
    while (found) {
        int index = -1;
        gtk_tree_model_get(model, &iter, 4, &index, -1);
        if (index == selected) { gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &iter); connect(); return true; }
        found = gtk_tree_model_iter_next(model, &iter);
    }
    error = "Named server row is unavailable";
    return false;
}

void TServerList::show() {
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

void TServerList::openListServerSettings() {
    struct SettingsState { TServerList* serverList; GtkWidget* dialog; GtkWidget* endpoint; GtkWidget* name; GtkWidget* host; GtkWidget* port; GtkWidget* theme; GtkWidget* error; std::vector<SavedListServer> endpoints; guint saveTimer = 0; int editIndex = 0; bool updating = false; };
    GtkWidget* dialog = gtk_dialog_new();
    applyRemoteControlWindowChrome(dialog);
    gtk_window_set_title(GTK_WINDOW(dialog), "RC settings");
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
    gtk_window_set_modal(GTK_WINDOW(dialog), false);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 292, -1);
    gtk_window_set_resizable(GTK_WINDOW(dialog), false);
    GtkWidget* actionArea = gtk_dialog_get_action_area(GTK_DIALOG(dialog));
    gtk_widget_set_no_show_all(actionArea, true);
    gtk_widget_hide(actionArea);
    GtkWidget* frame = gtk_frame_new(" Saved list servers ");
    gtk_container_set_border_width(GTK_CONTAINER(frame), 6);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), frame, true, true, 0);
    GtkWidget* grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 6);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_container_add(GTK_CONTAINER(frame), grid);
    GtkWidget* endpoint = gtk_combo_box_text_new();
    gtk_widget_set_name(endpoint, "ListServerProfileCombo");
    gtk_widget_set_hexpand(endpoint, true);
    GList* endpointRenderers = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(endpoint));
    for (GList* item = endpointRenderers; item != nullptr; item = item->next) if (GTK_IS_CELL_RENDERER_TEXT(item->data)) g_object_set(item->data, "ellipsize", PANGO_ELLIPSIZE_END, "max-width-chars", 24, nullptr);
    g_list_free(endpointRenderers);
    GtkWidget* newEndpoint = gtk_button_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_MENU);
    GtkWidget* removeEndpoint = gtk_button_new_from_icon_name("edit-delete-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_name(newEndpoint, "NewListServerProfile");
    gtk_widget_set_name(removeEndpoint, "DeleteListServerProfile");
    gtk_widget_set_size_request(newEndpoint, 26, 26);
    gtk_widget_set_size_request(removeEndpoint, 26, 26);
    gtk_widget_set_tooltip_text(newEndpoint, "New list server profile");
    gtk_widget_set_tooltip_text(removeEndpoint, "Delete selected list server profile");
    atk_object_set_name(gtk_widget_get_accessible(newEndpoint), "New list server profile");
    atk_object_set_name(gtk_widget_get_accessible(removeEndpoint), "Delete selected list server profile");
    GtkWidget* name = gtk_entry_new();
    GtkWidget* host = gtk_entry_new();
    GtkWidget* port = gtk_entry_new();
    gtk_widget_set_name(name, "ListServerNameField");
    gtk_widget_set_name(host, "ListServerHostField");
    gtk_widget_set_name(port, "ListServerPortField");
    GtkWidget* error = gtk_label_new("");
    for (const SavedListServer& value : listserverEndpoints) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(endpoint), savedListserverText(value).c_str());
    int selectedProfile = 0;
    for (std::size_t index = 0; index < listserverEndpoints.size(); ++index) if (listserverEndpoints[index].name == listserverName && listserverEndpoints[index].host == listserverHost && listserverEndpoints[index].port == listserverPort) { selectedProfile = static_cast<int>(index); break; }
    gtk_combo_box_set_active(GTK_COMBO_BOX(endpoint), selectedProfile);
    gtk_entry_set_text(GTK_ENTRY(name), listserverName.c_str());
    gtk_entry_set_text(GTK_ENTRY(host), listserverHost.c_str());
    gtk_entry_set_text(GTK_ENTRY(port), std::to_string(listserverPort).c_str());
    gtk_label_set_xalign(GTK_LABEL(error), 0.0F);
    GtkWidget* endpointLabel = gtk_label_new("Saved profile:");
    gtk_label_set_xalign(GTK_LABEL(endpointLabel), 0.0F);
    GtkWidget* endpointRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(endpointRow), endpoint, true, true, 0);
    gtk_box_pack_start(GTK_BOX(endpointRow), newEndpoint, false, false, 0);
    gtk_box_pack_start(GTK_BOX(endpointRow), removeEndpoint, false, false, 0);
    GtkWidget* nameLabel = gtk_label_new("Name:");
    GtkWidget* hostLabel = gtk_label_new("Host:");
    GtkWidget* portLabel = gtk_label_new("Port:");
    gtk_label_set_xalign(GTK_LABEL(nameLabel), 0.0F);
    gtk_label_set_xalign(GTK_LABEL(hostLabel), 0.0F);
    gtk_label_set_xalign(GTK_LABEL(portLabel), 0.0F);
    gtk_grid_attach(GTK_GRID(grid), endpointLabel, 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), endpointRow, 0, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), nameLabel, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), hostLabel, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), host, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), portLabel, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), port, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), error, 0, 5, 2, 1);
    GtkWidget* theme = gtk_combo_box_text_new();
    gtk_widget_set_name(theme, "SettingsThemePicker");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "system", "System (OS theme)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "dark", "Dark");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "dracula", "Dracula");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "material", "Material");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "ayu-mirage", "Ayu Mirage");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "nord", "Nord");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "monokai", "Monokai");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "one-dark", "One Dark");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "tokyo-night", "Tokyo Night");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "gruvbox", "Gruvbox");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "solarized", "Solarized Dark");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "catppuccin", "Catppuccin Mocha");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "light", "Light");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(theme), this->theme.c_str());
    GtkWidget* themeRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_container_set_border_width(GTK_CONTAINER(themeRow), 3);
    GtkWidget* themeLabel = gtk_label_new("Theme:");
    GtkWidget* cancelButton = gtk_button_new_with_label("Close");
    applyGtkButtonIcon(cancelButton, GTK_STOCK_CLOSE);
    gtk_widget_set_name(cancelButton, "SettingsCloseButton");
    GtkCssProvider* settingsButtonCss = gtk_css_provider_new();
    gtk_css_provider_load_from_data(settingsButtonCss, "#SettingsCloseButton { min-width: 0; padding: 4px 8px; }", -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(cancelButton), GTK_STYLE_PROVIDER(settingsButtonCss), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(settingsButtonCss);
    gtk_box_pack_start(GTK_BOX(themeRow), themeLabel, false, false, 0);
    gtk_box_pack_start(GTK_BOX(themeRow), theme, false, false, 0);
    gtk_box_pack_start(GTK_BOX(themeRow), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0), true, true, 0);
    gtk_box_pack_start(GTK_BOX(themeRow), cancelButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), themeRow, false, false, 0);
    auto* state = new SettingsState{this, dialog, endpoint, name, host, port, theme, error, listserverEndpoints, 0, selectedProfile, false};
    g_signal_connect(newEndpoint, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* state = static_cast<SettingsState*>(data);
        state->updating = true;
        gtk_combo_box_set_active(GTK_COMBO_BOX(state->endpoint), -1);
        gtk_entry_set_text(GTK_ENTRY(state->name), "");
        gtk_entry_set_text(GTK_ENTRY(state->host), "");
        gtk_entry_set_text(GTK_ENTRY(state->port), "14922");
        gtk_widget_set_sensitive(state->name, true);
        gtk_widget_set_sensitive(state->host, true);
        gtk_widget_set_sensitive(state->port, true);
        state->editIndex = -1;
        state->updating = false;
        gtk_label_set_text(GTK_LABEL(state->error), "");
        gtk_widget_grab_focus(state->name);
    }), state);
    g_signal_connect(removeEndpoint, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* state = static_cast<SettingsState*>(data);
        const int index = gtk_combo_box_get_active(GTK_COMBO_BOX(state->endpoint));
        if (index < 0 || static_cast<std::size_t>(index) >= state->endpoints.size()) return;
        const SavedListServer& selected = state->endpoints[static_cast<std::size_t>(index)];
        if (selected.name == "Retail" && selected.host == defaultListserverHost && selected.port == 14922) { gtk_label_set_text(GTK_LABEL(state->error), "The Retail profile is always available."); return; }
        state->endpoints.erase(state->endpoints.begin() + index);
        if (state->saveTimer != 0) { g_source_remove(state->saveTimer); state->saveTimer = 0; }
        const int nextIndex = std::min(index, static_cast<int>(state->endpoints.size()) - 1);
        state->serverList->listserverEndpoints = state->endpoints;
        saveListserverEndpoints(state->endpoints, nextIndex);
        state->updating = true;
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(state->endpoint));
        for (const SavedListServer& value : state->endpoints) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->endpoint), savedListserverText(value).c_str());
        gtk_combo_box_set_active(GTK_COMBO_BOX(state->endpoint), nextIndex);
        state->editIndex = nextIndex;
        const SavedListServer& next = state->endpoints[static_cast<std::size_t>(nextIndex)];
        gtk_entry_set_text(GTK_ENTRY(state->name), next.name.c_str());
        gtk_entry_set_text(GTK_ENTRY(state->host), next.host.c_str());
        gtk_entry_set_text(GTK_ENTRY(state->port), std::to_string(next.port).c_str());
        state->serverList->listserverName = next.name;
        state->serverList->listserverHost = next.host;
        state->serverList->listserverPort = next.port;
        const bool protectedProfile = next.name == "Retail" && next.host == defaultListserverHost && next.port == 14922;
        gtk_widget_set_sensitive(state->name, !protectedProfile);
        gtk_widget_set_sensitive(state->host, !protectedProfile);
        gtk_widget_set_sensitive(state->port, !protectedProfile);
        state->updating = false;
        gtk_label_set_text(GTK_LABEL(state->error), "Removed.");
    }), state);
    g_signal_connect(cancelButton, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { gtk_dialog_response(GTK_DIALOG(data), GTK_RESPONSE_CLOSE); }), dialog);
    g_signal_connect(endpoint, "changed", G_CALLBACK(+[](GtkComboBox* combo, gpointer data) {
        auto* state = static_cast<SettingsState*>(data);
        if (state->updating) return;
        const int index = gtk_combo_box_get_active(combo);
        if (index < 0 || static_cast<std::size_t>(index) >= state->endpoints.size()) return;
        if (state->saveTimer != 0) {
            g_source_remove(state->saveTimer);
            state->saveTimer = 0;
            state->serverList->listserverEndpoints = state->endpoints;
        }
        const SavedListServer& selected = state->endpoints[static_cast<std::size_t>(index)];
        state->updating = true;
        state->editIndex = index;
        gtk_entry_set_text(GTK_ENTRY(state->name), selected.name.c_str());
        gtk_entry_set_text(GTK_ENTRY(state->host), selected.host.c_str());
        gtk_entry_set_text(GTK_ENTRY(state->port), std::to_string(selected.port).c_str());
        const bool protectedProfile = selected.name == "Retail" && selected.host == defaultListserverHost && selected.port == 14922;
        gtk_widget_set_sensitive(state->name, !protectedProfile);
        gtk_widget_set_sensitive(state->host, !protectedProfile);
        gtk_widget_set_sensitive(state->port, !protectedProfile);
        state->serverList->listserverName = selected.name;
        state->serverList->listserverHost = selected.host;
        state->serverList->listserverPort = selected.port;
        saveListserverEndpoints(state->endpoints, index);
        state->updating = false;
        gtk_label_set_text(GTK_LABEL(state->error), "");
    }), state);
    g_signal_connect(theme, "changed", G_CALLBACK(+[](GtkComboBox*, gpointer data) {
        auto* state = static_cast<SettingsState*>(data);
        const char* selected = gtk_combo_box_get_active_id(GTK_COMBO_BOX(state->theme));
        if (selected == nullptr) return;
        state->serverList->theme = selected;
        state->serverList->darkMode = state->serverList->theme == "system" ? state->serverList->darkMode : state->serverList->theme != "light";
        state->serverList->onThemeChangedCallback(state->serverList->darkMode, state->serverList->theme);
    }), state);
    auto queueSave = +[](GtkEditable*, gpointer data) {
        auto* state = static_cast<SettingsState*>(data);
        if (state->updating) return;
        gtk_dialog_response(GTK_DIALOG(state->dialog), GTK_RESPONSE_APPLY);
    };
    g_signal_connect(name, "changed", G_CALLBACK(queueSave), state);
    g_signal_connect(host, "changed", G_CALLBACK(queueSave), state);
    g_signal_connect(port, "changed", G_CALLBACK(queueSave), state);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* settings, gint response, gpointer data) {
        auto* state = static_cast<SettingsState*>(data);
        if (response == GTK_RESPONSE_CLOSE || response == GTK_RESPONSE_DELETE_EVENT) {
            if (state->saveTimer != 0) {
                g_source_remove(state->saveTimer);
                state->saveTimer = 0;
                state->serverList->listserverEndpoints = state->endpoints;
                saveListserverEndpoints(state->endpoints, state->editIndex);
            }
            gtk_widget_destroy(GTK_WIDGET(settings));
            return;
        }
        if (response != GTK_RESPONSE_APPLY || state->updating) return;
        const std::string name = gtk_entry_get_text(GTK_ENTRY(state->name));
        const std::string host = gtk_entry_get_text(GTK_ENTRY(state->host));
        const int port = std::atoi(gtk_entry_get_text(GTK_ENTRY(state->port)));
        if (name.empty() || host.empty() || port <= 0 || port > 65535) { gtk_label_set_text(GTK_LABEL(state->error), "Complete the name, host, and port."); return; }
        SavedListServer saved;
        saved.name = name;
        saved.host = host;
        saved.port = port;
        if (state->editIndex >= 0 && static_cast<std::size_t>(state->editIndex) < state->endpoints.size()) {
            state->endpoints[static_cast<std::size_t>(state->editIndex)] = saved;
        } else {
            state->endpoints.push_back(saved);
            state->editIndex = static_cast<int>(state->endpoints.size()) - 1;
        }
        if (state->saveTimer != 0) g_source_remove(state->saveTimer);
        state->saveTimer = g_timeout_add(400, +[](gpointer value) -> gboolean {
            auto* state = static_cast<SettingsState*>(value);
            state->saveTimer = 0;
            state->serverList->listserverEndpoints = state->endpoints;
            const SavedListServer& selected = state->endpoints[static_cast<std::size_t>(state->editIndex)];
            state->serverList->listserverName = selected.name;
            state->serverList->listserverHost = selected.host;
            state->serverList->listserverPort = selected.port;
            saveListserverEndpoints(state->endpoints, state->editIndex);
            state->updating = true;
            gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(state->endpoint));
            for (const SavedListServer& endpoint : state->endpoints) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->endpoint), savedListserverText(endpoint).c_str());
            gtk_combo_box_set_active(GTK_COMBO_BOX(state->endpoint), state->editIndex);
            state->updating = false;
            gtk_label_set_text(GTK_LABEL(state->error), "Saved.");
            return G_SOURCE_REMOVE;
        }, state);
    }), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* state = static_cast<SettingsState*>(data);
        if (state->saveTimer != 0) g_source_remove(state->saveTimer);
        delete state;
    }), state);
    const SavedListServer& initial = state->endpoints[static_cast<std::size_t>(state->editIndex)];
    const bool protectedProfile = initial.name == "Retail" && initial.host == defaultListserverHost && initial.port == 14922;
    gtk_widget_set_sensitive(name, !protectedProfile);
    gtk_widget_set_sensitive(host, !protectedProfile);
    gtk_widget_set_sensitive(port, !protectedProfile);
    gtk_widget_show_all(dialog);
}

void TServerList::openAnotherListServer(const RC::RCAccount* selectedAccount, const std::string& selectedListServer) {
    additionalLists.push_back(std::make_unique<TServerList>(applicationDirectory, [] {}, onConnectedCallback, onServerSelectedCallback, darkMode, theme, onThemeChangedCallback, accountChoicesCallback, accountChoicesForListServerCallback, [this] { openAnotherListServer(); }));
    TServerList* list = additionalLists.back().get();
    list->defaultAdditionalConnection = true;
    list->setLoginParent(loginParent);
    if (selectedAccount == nullptr) list->open(accountId, account, password, nickname, "");
    else list->open(selectedAccount->id, selectedAccount->name, selectedAccount->password, nickname, selectedListServer.empty() ? listserverName : selectedListServer);
}

void TServerList::setListServer(const std::string& name, const std::string& host, int port) {
    listserverName = name;
    listserverHost = host;
    listserverPort = port;
    listserverEndpoints.insert(listserverEndpoints.begin(), {name, host, port});
    saveListserverEndpoints(listserverEndpoints);
    gtk_window_set_title(GTK_WINDOW(window), (listserverName + " Servers").c_str());
}

std::string TServerList::currentListServer() const { return listserverName + " — " + listserverHost + ":" + std::to_string(listserverPort); }

void TServerList::onRefresh(GtkButton*, gpointer data) { static_cast<TServerList*>(data)->refresh(); }

void TServerList::onConnect(GtkButton*, gpointer data) { TServerList* list = static_cast<TServerList*>(data); list->connect(list->defaultAdditionalConnection); }

void TServerList::onSelectionChanged(GtkTreeSelection* selection, gpointer data) {
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
    int index = -1;
    gtk_tree_model_get(model, &iter, 4, &index, -1);
    static_cast<TServerList*>(data)->showEntry(index);
}

void TServerList::onRowActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data) { TServerList* list = static_cast<TServerList*>(data); list->connect(list->defaultAdditionalConnection); }

gboolean TServerList::onTreeButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    TServerList* list = static_cast<TServerList*>(data);
    if (event->button == GDK_BUTTON_PRIMARY && (event->state & GDK_CONTROL_MASK) != 0) {
        GtkTreePath* path = nullptr;
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), static_cast<int>(event->x), static_cast<int>(event->y), &path, nullptr, nullptr, nullptr)) {
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), path, nullptr, false);
            gtk_tree_path_free(path);
            list->connect(true);
            return true;
        }
    }
    if (event->button != GDK_BUTTON_SECONDARY) return false;
    GtkTreePath* path = nullptr;
    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), static_cast<int>(event->x), static_cast<int>(event->y), &path, nullptr, nullptr, nullptr);
    if (path != nullptr) gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), path, nullptr, false);
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* anotherList = gtk_menu_item_new_with_label("Open another server list");
    GtkWidget* additional = gtk_menu_item_new_with_label("Open additional connection");
    GtkWidget* normal = gtk_menu_item_new_with_label("Connect");
    GtkWidget* anotherListMenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(anotherList), anotherListMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), anotherList);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), additional);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), normal);
    const auto choices = list->accountChoicesCallback ? list->accountChoicesCallback() : std::vector<RC::RCAccount>();
    std::map<std::string, int> nameCounts;
    for (const RC::RCAccount& account : choices) ++nameCounts[account.name];
    for (const RC::RCAccount& account : choices) {
        std::string associations;
        for (const std::string& association : account.listServers) { if (!associations.empty()) associations += ", "; associations += association; }
        std::string label = account.name + " [" + (associations.empty() ? list->listserverName : associations) + "]";
        if (nameCounts[account.name] > 1) label += " (#" + std::to_string(account.id) + ")";
        GtkWidget* item = gtk_menu_item_new_with_label(label.c_str());
        g_object_set_data_full(G_OBJECT(item), "rc-account-choice", new RC::RCAccount(account), [](gpointer value) { delete static_cast<RC::RCAccount*>(value); });
        g_signal_connect(item, "activate", G_CALLBACK(+[](GtkMenuItem* item, gpointer value) { const auto* account = static_cast<const RC::RCAccount*>(g_object_get_data(G_OBJECT(item), "rc-account-choice")); if (account != nullptr) static_cast<TServerList*>(value)->openAnotherListServer(account, account->listServers.empty() ? std::string() : account->listServers.front()); }), list);
        gtk_menu_shell_append(GTK_MENU_SHELL(anotherListMenu), item);
    }
    if (choices.empty()) { GtkWidget* empty = gtk_menu_item_new_with_label("No saved accounts"); gtk_widget_set_sensitive(empty, false); gtk_menu_shell_append(GTK_MENU_SHELL(anotherListMenu), empty); }
    g_signal_connect(additional, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer value) { static_cast<TServerList*>(value)->connect(true); }), list);
    g_signal_connect(normal, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer value) { TServerList* list = static_cast<TServerList*>(value); list->connect(list->defaultAdditionalConnection); }), list);
    if (list->accountChoicesCallback || list->accountChoicesForListServerCallback) {
        GtkWidget* accounts = gtk_menu_item_new_with_label("Connect using account");
        GtkWidget* accountMenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(accounts), accountMenu);
        for (const RC::RCAccount& account : choices) {
            const std::string profile = account.listServers.empty() ? list->listserverName : account.listServers.front();
            const std::string label = profile.empty() ? account.name : account.name + " [" + profile + "]";
            GtkWidget* item = gtk_menu_item_new_with_label(label.c_str());
            g_object_set_data_full(G_OBJECT(item), "rc-account-choice", new RC::RCAccount(account), [](gpointer value) { delete static_cast<RC::RCAccount*>(value); });
            g_signal_connect(item, "activate", G_CALLBACK(+[](GtkMenuItem* item, gpointer value) { const auto* account = static_cast<const RC::RCAccount*>(g_object_get_data(G_OBJECT(item), "rc-account-choice")); if (account != nullptr) static_cast<TServerList*>(value)->connectWithAccount(*account); }), list);
            gtk_menu_shell_append(GTK_MENU_SHELL(accountMenu), item);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), accounts);
    }
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
    gtk_tree_path_free(path);
    return true;
}

void TServerList::onHomepage(GtkButton*, gpointer data) {
    TServerList* serverList = static_cast<TServerList*>(data);
    const char* homepage = gtk_entry_get_text(GTK_ENTRY(serverList->homepageField));
    if (homepage == nullptr || *homepage == '\0') return;
    GError* error = nullptr;
    if (!gtk_show_uri_on_window(GTK_WINDOW(serverList->window), homepage, GDK_CURRENT_TIME, &error) && error != nullptr) {
        gtk_label_set_text(GTK_LABEL(serverList->statusField), error->message);
        g_error_free(error);
    }
}

gboolean TServerList::onDelete(GtkWidget*, GdkEvent*, gpointer data) {
    TServerList* serverList = static_cast<TServerList*>(data);
    gtk_widget_hide(serverList->window);
    serverList->onCloseCallback();
    return true;
}

gboolean TServerList::finishLoad(gpointer data) {
    std::unique_ptr<LoadResult> result(static_cast<LoadResult*>(data));
    if (!result->alive->load()) return G_SOURCE_REMOVE;
    if (result->serverList->worker.joinable()) result->serverList->worker.join();
    result->serverList->entries = std::move(result->entries);
    gtk_list_store_clear(result->serverList->store);
    for (std::size_t index = 0; index < result->serverList->entries.size(); ++index) {
        const ServerEntry& entry = result->serverList->entries[index];
        GtkTreeIter iter;
        gtk_list_store_append(result->serverList->store, &iter);
        const std::string players = std::to_string(entry.players);
        const GdkPixbuf* icon = entry.icon < 0 ? nullptr : result->serverList->serverIcons[entry.icon];
        gtk_list_store_set(result->serverList->store, &iter, 0, icon, 1, entry.name.c_str(), 2, players.c_str(), 3, entry.players, 4, static_cast<int>(index), -1);
    }
    if (result->error.empty()) gtk_label_set_text(GTK_LABEL(result->serverList->statusField), "");
    else {
        gtk_label_set_text(GTK_LABEL(result->serverList->statusField), "");
        result->serverList->disconnectCurrentConnection();
        gtk_widget_hide(result->serverList->window);
        const std::function<void()> reopenListServer = result->serverList->onCloseCallback;
        createErrorWindow("Error", result->error.c_str(), result->serverList->loginParent != nullptr ? result->serverList->loginParent : GTK_WINDOW(result->serverList->window), [reopenListServer] { if (reopenListServer) reopenListServer(); });
    }
    gtk_widget_set_sensitive(result->serverList->refreshButton, true);
    result.release();
    return G_SOURCE_REMOVE;
}

gboolean TServerList::finishConnect(gpointer data) {
    auto* result = static_cast<ConnectResult*>(data);
    TServerList* serverList = result->serverList;
    if (serverList == nullptr || !result->alive->load()) return G_SOURCE_REMOVE;
    if (serverList->connectWorker.joinable()) serverList->connectWorker.join();
    serverList->connecting = false;
    gtk_widget_set_sensitive(serverList->refreshButton, true);
    gtk_widget_set_sensitive(serverList->connectButton, true);
    if (!result->error.empty() || result->connection == nullptr || result->serverIndex < 0) {
        const std::string message = result->error.empty() ? "Server is no longer available." : result->error;
        if (result->connection != nullptr) { rc_disconnect(result->connection); result->connection = nullptr; }
        createErrorWindow("Connection Error", message.c_str(), serverList->loginParent != nullptr ? serverList->loginParent : GTK_WINDOW(serverList->window));
        return G_SOURCE_REMOVE;
    }
    void* remoteConnection = result->connection;
    result->connection = nullptr;
    serverList->defaultAdditionalConnection = false;
    gtk_widget_hide(serverList->window);
    serverList->onConnectedCallback(serverList, remoteConnection, result->serverIndex, result->serverName, result->nickname, result->account, result->additional);
    return G_SOURCE_REMOVE;
}

void TServerList::refresh() {
    if (worker.joinable() || connecting) return;
    gtk_label_set_text(GTK_LABEL(statusField), "Loading server list...");
    gtk_widget_set_sensitive(refreshButton, false);
    worker = std::jthread([this] {
        remoteControlDebugLog("connecting to listserver %s:%d", listserverHost.c_str(), listserverPort);
        void* nextConnection = rc_connect(listserverHost.c_str(), listserverPort, account.c_str(), password.c_str());
        std::vector<ServerEntry> nextEntries;
        std::string error;
        if (nextConnection == nullptr) error = "Unable to create listserver connection.";
        else {
            RCServer* servers = nullptr;
            const int count = rc_get_servers(nextConnection, &servers);
            for (int index = 0; index < count; ++index) {
                const std::string rawName = servers[index].name == nullptr ? "" : servers[index].name;
                nextEntries.push_back({getServerListName(rawName), servers[index].language == nullptr ? "" : servers[index].language, servers[index].description == nullptr ? "" : servers[index].description, servers[index].version == nullptr ? "" : servers[index].version, servers[index].homepage == nullptr ? "" : servers[index].homepage, servers[index].players, getServerListIcon(rawName)});
            }
            const char* lastError = rc_last_error(nextConnection);
            if (count == 0 && lastError != nullptr) error = lastError;
        }
        {
            std::lock_guard lock(connectionMutex);
            if (connection != nullptr) rc_disconnect(connection);
            connection = nextConnection;
        }
        g_idle_add_full(G_PRIORITY_DEFAULT, finishLoad, new LoadResult{callbackAlive, this, std::move(nextEntries), std::move(error)}, +[](gpointer data) { delete static_cast<LoadResult*>(data); });
    });
}

void TServerList::connect(bool additional) {
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
    int index = -1;
    gtk_tree_model_get(model, &iter, 4, &index, -1);
    if (index < 0 || static_cast<std::size_t>(index) >= entries.size() || connecting) return;
    if (!additional && onServerSelectedCallback) onServerSelectedCallback();
    void* nextConnection = nullptr;
    {
        std::lock_guard lock(connectionMutex);
        if (connection == nullptr) return;
        nextConnection = connection;
        connection = nullptr;
    }
    connecting = true;
    gtk_widget_set_sensitive(refreshButton, false);
    gtk_widget_set_sensitive(connectButton, false);
    const std::string selectedServerName = entries[static_cast<std::size_t>(index)].name;
    const std::string selectedNickname = nickname;
    const std::string selectedAccount = account;
    const std::shared_ptr<std::atomic<bool>> alive = callbackAlive;
    remoteControlDebugLog("connecting to server index %d", index);
    connectWorker = std::jthread([this, alive, nextConnection, index, additional, selectedServerName, selectedNickname, selectedAccount] {
        const bool success = rc_connect_to_server(nextConnection, index) != 0;
        std::string error;
        if (!success) {
            const char* reason = rc_last_error(nextConnection);
            error = reason == nullptr ? "You have been disconnected!" : reason;
            remoteControlDebugLog("server connection failed: %s", error.c_str());
        }
        if (!alive->load()) {
            rc_disconnect(nextConnection);
            return;
        }
        g_idle_add_full(G_PRIORITY_DEFAULT, finishConnect, new ConnectResult{alive, this, nextConnection, index, additional, selectedServerName, selectedNickname, selectedAccount, std::move(error)}, +[](gpointer data) {
            auto* result = static_cast<ConnectResult*>(data);
            if (result->connection != nullptr) rc_disconnect(result->connection);
            delete result;
        });
    });
}

void TServerList::connectWithAccount(const RC::RCAccount& selectedAccount) {
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
    int index = -1;
    gtk_tree_model_get(model, &iter, 4, &index, -1);
    if (index < 0 || static_cast<std::size_t>(index) >= entries.size() || connecting) return;
    connecting = true;
    gtk_widget_set_sensitive(refreshButton, false);
    gtk_widget_set_sensitive(connectButton, false);
    const std::string host = listserverHost;
    const int port = listserverPort;
    const std::string selectedServerName = entries[static_cast<std::size_t>(index)].name;
    const std::string selectedNickname = nickname;
    const std::string selectedAccountName = selectedAccount.name;
    const std::string selectedPassword = selectedAccount.password;
    const std::shared_ptr<std::atomic<bool>> alive = callbackAlive;
    connectWorker = std::jthread([this, alive, host, port, index, selectedServerName, selectedNickname, selectedAccountName, selectedPassword] {
        void* nextConnection = rc_connect(host.c_str(), port, selectedAccountName.c_str(), selectedPassword.c_str());
        std::string error;
        int serverIndex = -1;
        if (nextConnection == nullptr) error = "Unable to create listserver connection.";
        else {
            RCServer* servers = nullptr;
            const int count = rc_get_servers(nextConnection, &servers);
            for (int server = 0; server < count; ++server) {
                const std::string rawName = servers[server].name == nullptr ? "" : servers[server].name;
                if (getServerListName(rawName) == selectedServerName) { serverIndex = server; break; }
            }
            if (serverIndex < 0 || !rc_connect_to_server(nextConnection, serverIndex)) {
                const char* reason = rc_last_error(nextConnection);
                error = reason == nullptr ? "Server is no longer available." : reason;
            }
        }
        if (!alive->load()) {
            if (nextConnection != nullptr) rc_disconnect(nextConnection);
            return;
        }
        g_idle_add_full(G_PRIORITY_DEFAULT, finishConnect, new ConnectResult{alive, this, nextConnection, serverIndex, true, selectedServerName, selectedNickname, selectedAccountName, std::move(error)}, +[](gpointer data) {
            auto* result = static_cast<ConnectResult*>(data);
            if (result->connection != nullptr) rc_disconnect(result->connection);
            delete result;
        });
    });
}

void TServerList::disconnectCurrentConnection() {
    std::lock_guard lock(connectionMutex);
    if (connection == nullptr) return;
    rc_disconnect(connection);
    connection = nullptr;
}

void TServerList::showEntry(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= entries.size()) return;
    const ServerEntry& entry = entries[index];
    gtk_entry_set_text(GTK_ENTRY(languageField), entry.language.c_str());
    gtk_entry_set_text(GTK_ENTRY(versionField), entry.version.c_str());
    gtk_entry_set_text(GTK_ENTRY(homepageField), entry.homepage.c_str());
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(descriptionField));
    gtk_text_buffer_set_text(buffer, entry.description.c_str(), -1);
}
