#include "TServerList.h"

#include <grclib.h>

namespace {

    constexpr const char* listserverHost = "listserver.graalonline.com";
    constexpr int listserverPort = 14922;

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

TServerList::TServerList(std::function<void()> onClose, std::function<void(void*, const std::string&, const std::string&)> onConnected) : onCloseCallback(std::move(onClose)), onConnectedCallback(std::move(onConnected)) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "ServerList");
    gtk_window_set_title(GTK_WINDOW(window), "Graal Servers");
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
    g_object_set(playerRenderer, "xalign", 1.0F, nullptr);
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
    GtkCssProvider* descriptionProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(descriptionProvider, "textview, textview text { background-color: #1e1e1e; color: #d4d4d4; }", -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(descriptionField), GTK_STYLE_PROVIDER(descriptionProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(descriptionProvider);
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
    open(account, password, nickname);
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
    gtk_label_set_text(GTK_LABEL(result->serverList->statusField), result->error.c_str());
    gtk_widget_set_sensitive(result->serverList->refreshButton, true);
    return G_SOURCE_REMOVE;
}

void TServerList::refresh() {
    if (worker.joinable()) return;
    gtk_label_set_text(GTK_LABEL(statusField), "Loading server list...");
    gtk_widget_set_sensitive(refreshButton, false);
    worker = std::jthread([this] {
        void* nextConnection = rc_connect(listserverHost, listserverPort, account.c_str(), password.c_str());
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
        g_idle_add_full(G_PRIORITY_DEFAULT, finishLoad, new LoadResult{this, std::move(nextEntries), std::move(error)}, nullptr);
    });
}

void TServerList::connect() {
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
    int index = -1;
    gtk_tree_model_get(model, &iter, 4, &index, -1);
    std::lock_guard lock(connectionMutex);
    if (connection == nullptr) return;
    if (rc_connect_to_server(connection, index)) {
        gtk_widget_hide(window);
        onConnectedCallback(connection, entries[index].name, nickname);
    }
    else gtk_label_set_text(GTK_LABEL(statusField), rc_last_error(connection));
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
