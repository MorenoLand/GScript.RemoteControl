#include "TServerList.h"
#include "Debug.h"
#include "ErrorWindow.h"
#include "TreeSearch.h"

#include <grclib.h>

#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {

    constexpr const char* defaultListserverHost = "listserver.graalonline.com";
    constexpr int listserverPort = 14922;

#ifdef _WIN32
    std::string readListserverValue(HKEY key, const char* name) {
        char value[256] = {};
        DWORD length = sizeof(value);
        return RegQueryValueExA(key, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(value), &length) == ERROR_SUCCESS ? value : "";
    }

    void writeListserverValue(HKEY key, const char* name, const std::string& value) { RegSetValueExA(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>(value.size() + 1)); }
#endif

    int getServerListIcon(const std::string& value) {
        if (value.size() < 2 || value[1] != ' ') return -1;
        if (value[0] == 'P') return 0;
        if (value[0] == 'U') return 1;
        return -1;
    }

    std::string getServerListName(const std::string& value) {
        return value.size() > 1 && value[1] == ' ' ? value.substr(2) : value;
    }

}

TServerList::TServerList(std::function<void()> onClose, std::function<void(void*, int, const std::string&, const std::string&, const std::string&)> onConnected, std::function<void()> onServerSelected, bool nextDarkMode, const std::string& nextTheme, std::function<void(bool, const std::string&)> onThemeChanged) : onCloseCallback(std::move(onClose)), onConnectedCallback(std::move(onConnected)), onServerSelectedCallback(std::move(onServerSelected)), onThemeChangedCallback(std::move(onThemeChanged)), darkMode(nextDarkMode), theme(nextTheme) {
    listserverHost = defaultListserverHost;
#ifdef _WIN32
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Graal\\RemoteControl", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        const std::string host = readListserverValue(key, "listserverhost");
        const std::string port = readListserverValue(key, "listserverport");
        if (!host.empty()) listserverHost = host;
        const int parsedPort = std::atoi(port.c_str());
        if (parsedPort > 0 && parsedPort <= 65535) listserverPort = parsedPort;
        RegCloseKey(key);
    }
#endif
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "ServerList");
    gtk_window_set_title(GTK_WINDOW(window), "Servers");
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(window), 520, 350);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);

    GtkWidget* frame = gtk_frame_new(" Servers ");
    gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
    gtk_box_pack_start(GTK_BOX(root), frame, true, true, 0);

    GtkWidget* pane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_set_border_width(GTK_CONTAINER(pane), 5);
    gtk_paned_set_position(GTK_PANED(pane), 250);
    gtk_container_add(GTK_CONTAINER(frame), pane);

    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_paned_pack1(GTK_PANED(pane), scrolled, true, true);

    GError* error = nullptr;
    serverIcons[0] = gdk_pixbuf_new_from_file("images/rcicon_gold.png", &error);
    if (error != nullptr) g_error_free(error);
    error = nullptr;
    serverIcons[1] = gdk_pixbuf_new_from_file("images/rcicon_uc.png", &error);
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
    gtk_paned_pack2(GTK_PANED(pane), detailsFrame, false, true);

    GtkWidget* details = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_container_set_border_width(GTK_CONTAINER(details), 5);
    gtk_container_add(GTK_CONTAINER(detailsFrame), details);

    GtkWidget* languageRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget* languageLabel = gtk_label_new("Language:");
    gtk_widget_set_size_request(languageLabel, 80, -1);
    gtk_label_set_xalign(GTK_LABEL(languageLabel), 0.0F);
    languageField = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(languageField), false);
    gtk_box_pack_start(GTK_BOX(languageRow), languageLabel, false, false, 0);
    gtk_box_pack_end(GTK_BOX(languageRow), languageField, true, true, 0);
    gtk_box_pack_start(GTK_BOX(details), languageRow, false, true, 0);

    GtkWidget* versionRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget* versionLabel = gtk_label_new("Version:");
    gtk_widget_set_size_request(versionLabel, 80, -1);
    gtk_label_set_xalign(GTK_LABEL(versionLabel), 0.0F);
    versionField = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(versionField), false);
    gtk_box_pack_start(GTK_BOX(versionRow), versionLabel, false, false, 0);
    gtk_box_pack_end(GTK_BOX(versionRow), versionField, true, true, 0);
    gtk_box_pack_start(GTK_BOX(details), versionRow, false, true, 0);

    GtkWidget* descriptionLabel = gtk_label_new("Description:");
    gtk_label_set_xalign(GTK_LABEL(descriptionLabel), 0.0F);
    gtk_box_pack_start(GTK_BOX(details), descriptionLabel, false, false, 0);
    GtkWidget* descriptionScrolled = gtk_scrolled_window_new(nullptr, nullptr);
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
    gtk_editable_set_editable(GTK_EDITABLE(homepageField), false);
    GtkWidget* homepageButton = gtk_button_new_with_label(">");
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
    GtkWidget* connectButton = gtk_button_new_with_label("Connect");
    gtk_container_add(GTK_CONTAINER(buttons), refreshButton);
    gtk_container_add(GTK_CONTAINER(buttons), connectButton);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, true, 0);

    g_signal_connect(refreshButton, "clicked", G_CALLBACK(onRefresh), this);
    g_signal_connect(connectButton, "clicked", G_CALLBACK(onConnect), this);
    g_signal_connect(homepageButton, "clicked", G_CALLBACK(onHomepage), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TServerList::~TServerList() {
    if (worker.joinable()) worker.join();
    std::lock_guard lock(connectionMutex);
    if (connection != nullptr) rc_disconnect(connection);
    for (GdkPixbuf* icon : serverIcons) if (icon != nullptr) g_object_unref(icon);
    if (window != nullptr) gtk_widget_destroy(window);
    if (store != nullptr) g_object_unref(store);
}

void TServerList::open(const std::string& account, const std::string& password, const std::string& nickname) {
    this->account = account;
    this->password = password;
    this->nickname = nickname;
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

void TServerList::show() {
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

void TServerList::openListServerSettings() {
    struct SettingsState { TServerList* serverList; GtkWidget* host; GtkWidget* port; GtkWidget* darkMode; GtkWidget* theme; GtkWidget* error; };
    GtkWidget* dialog = gtk_dialog_new_with_buttons("RC settings", GTK_WINDOW(window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Apply", GTK_RESPONSE_OK, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 250, -1);
    gtk_window_set_resizable(GTK_WINDOW(dialog), false);
    GtkWidget* frame = gtk_frame_new(" List server ");
    gtk_container_set_border_width(GTK_CONTAINER(frame), 8);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), frame, true, true, 0);
    GtkWidget* grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_container_add(GTK_CONTAINER(frame), grid);
    GtkWidget* host = gtk_entry_new();
    GtkWidget* port = gtk_entry_new();
    GtkWidget* error = gtk_label_new("");
    gtk_entry_set_text(GTK_ENTRY(host), listserverHost.c_str());
    gtk_entry_set_text(GTK_ENTRY(port), std::to_string(listserverPort).c_str());
    gtk_label_set_xalign(GTK_LABEL(error), 0.0F);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Host:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), host, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Port:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), port, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), error, 0, 2, 2, 1);
    GtkWidget* darkMode = gtk_check_button_new_with_label("Use dark mode");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(darkMode), this->darkMode);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), darkMode, false, false, 8);
    GtkWidget* theme = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "dark", "Dark");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "dracula", "Dracula");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "material", "Material");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "ayu-mirage", "Ayu Mirage");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "nord", "Nord");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "monokai", "Monokai");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "one-dark", "One Dark");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme), "light", "Light");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(theme), this->theme.c_str());
    GtkWidget* themeGrid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(themeGrid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(themeGrid), 8);
    gtk_grid_attach(GTK_GRID(themeGrid), gtk_label_new("Theme:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(themeGrid), theme, 1, 0, 1, 1);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), themeGrid, false, false, 0);
    auto* state = new SettingsState{this, host, port, darkMode, theme, error};
    g_signal_connect(theme, "changed", G_CALLBACK(+[](GtkComboBox*, gpointer data) {
        auto* state = static_cast<SettingsState*>(data);
        const char* selected = gtk_combo_box_get_active_id(GTK_COMBO_BOX(state->theme));
        if (selected == nullptr) return;
        state->serverList->theme = selected;
        state->serverList->darkMode = state->serverList->theme != "light";
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->darkMode), state->serverList->darkMode);
        state->serverList->onThemeChangedCallback(state->serverList->darkMode, state->serverList->theme);
    }), state);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* settings, gint response, gpointer data) {
        auto* state = static_cast<SettingsState*>(data);
        if (response != GTK_RESPONSE_OK) { gtk_widget_destroy(GTK_WIDGET(settings)); return; }
        const std::string host = gtk_entry_get_text(GTK_ENTRY(state->host));
        const int port = std::atoi(gtk_entry_get_text(GTK_ENTRY(state->port)));
        if (host.empty() || port <= 0 || port > 65535) { gtk_label_set_text(GTK_LABEL(state->error), "Enter a host and port from 1 to 65535."); return; }
        state->serverList->setListServer(host, port);
        state->serverList->theme = gtk_combo_box_get_active_id(GTK_COMBO_BOX(state->theme));
        state->serverList->darkMode = state->serverList->theme != "light" && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->darkMode)) != FALSE;
        state->serverList->onThemeChangedCallback(state->serverList->darkMode, state->serverList->theme);
        gtk_widget_destroy(GTK_WIDGET(settings));
    }), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) { delete static_cast<SettingsState*>(data); }), state);
    gtk_widget_show_all(dialog);
}

void TServerList::setListServer(const std::string& host, int port) {
    listserverHost = host;
    listserverPort = port;
#ifdef _WIN32
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Graal\\RemoteControl", 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition) == ERROR_SUCCESS) {
        writeListserverValue(key, "listserverhost", listserverHost);
        writeListserverValue(key, "listserverport", std::to_string(listserverPort));
        RegCloseKey(key);
    }
#endif
}

void TServerList::onRefresh(GtkButton*, gpointer data) { static_cast<TServerList*>(data)->refresh(); }

void TServerList::onConnect(GtkButton*, gpointer data) { static_cast<TServerList*>(data)->connect(); }

void TServerList::onSelectionChanged(GtkTreeSelection* selection, gpointer data) {
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
    int index = -1;
    gtk_tree_model_get(model, &iter, 4, &index, -1);
    static_cast<TServerList*>(data)->showEntry(index);
}

void TServerList::onRowActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data) { static_cast<TServerList*>(data)->connect(); }

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
        result->serverList->onCloseCallback();
        createErrorWindow("Error", result->error.c_str());
    }
    gtk_widget_set_sensitive(result->serverList->refreshButton, true);
    result.release();
    return G_SOURCE_REMOVE;
}

void TServerList::refresh() {
    if (worker.joinable()) return;
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
        g_idle_add_full(G_PRIORITY_DEFAULT, finishLoad, new LoadResult{this, std::move(nextEntries), std::move(error)}, +[](gpointer data) { delete static_cast<LoadResult*>(data); });
    });
}

void TServerList::connect() {
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
    int index = -1;
    gtk_tree_model_get(model, &iter, 4, &index, -1);
    if (onServerSelectedCallback) onServerSelectedCallback();
    std::lock_guard lock(connectionMutex);
    if (connection == nullptr) return;
    remoteControlDebugLog("connecting to server index %d", index);
    if (rc_connect_to_server(connection, index)) {
        void* remoteConnection = connection;
        connection = nullptr;
        gtk_widget_hide(window);
        onConnectedCallback(remoteConnection, index, entries[index].name, nickname, account);
    }
    else {
        const char* reason = rc_last_error(connection);
        remoteControlDebugLog("server connection failed: %s", reason == nullptr ? "You have been disconnected!" : reason);
        createErrorWindow("Connection Error", reason == nullptr ? "You have been disconnected!" : reason);
    }
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
