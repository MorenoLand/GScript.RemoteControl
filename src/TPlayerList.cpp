#include "TPlayerList.h"
#include "TLocalBanWindow.h"

#include <grclib.h>

#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <fstream>

namespace {
    constexpr int PlayerIconColumn = 0;
    constexpr int PlayerNickColumn = 1;
    constexpr int PlayerAccountColumn = 2;
    constexpr int PlayerLevelColumn = 3;
    constexpr int PlayerIdColumn = 4;

    struct PMWindowData {
        void* connection;
        std::filesystem::path historyDirectory;
        GtkWidget* window;
        GtkWidget* reply;
        int playerId;
        std::string account;
        std::string nick;
    };

    gboolean onPMWindowDelete(GtkWidget*, GdkEvent*, gpointer data) { delete static_cast<PMWindowData*>(data); return false; }

    void onPMSend(GtkButton*, gpointer data) {
        PMWindowData* windowData = static_cast<PMWindowData*>(data);
        const char* text = gtk_entry_get_text(GTK_ENTRY(windowData->reply));
        if (text == nullptr || *text == '\0') return;
        if (rc_send_private_message(windowData->connection, windowData->playerId, text) == 0) return;
        std::filesystem::create_directories(windowData->historyDirectory);
        std::ofstream output(windowData->historyDirectory / (windowData->account + ".txt"), std::ios::app | std::ios::binary);
        output << "You:\n" << text << "\n";
        gtk_entry_set_text(GTK_ENTRY(windowData->reply), "");
    }

    void onPMHistory(GtkButton*, gpointer data) {
        PMWindowData* windowData = static_cast<PMWindowData*>(data);
        GtkWidget* history = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(history), ("History: " + windowData->account + " - " + windowData->nick).c_str());
        gtk_window_set_default_size(GTK_WINDOW(history), 440, 320);
        GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
        GtkWidget* field = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(field), false);
        gtk_container_add(GTK_CONTAINER(scrolled), field);
        gtk_container_add(GTK_CONTAINER(history), scrolled);
        const std::filesystem::path path = windowData->historyDirectory / (windowData->account + ".txt");
        std::ifstream input(path, std::ios::binary);
        std::ostringstream content;
        content << input.rdbuf();
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(field));
        gtk_text_buffer_set_text(buffer, content.str().c_str(), -1);
        gtk_widget_show_all(history);
    }

    bool getMessage(GtkWindow* parent, const char* title, const char* label, std::string& message) {
        GtkWidget* dialog = gtk_dialog_new_with_buttons(title, parent, GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Send", GTK_RESPONSE_ACCEPT, nullptr);
        GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget* text = gtk_text_view_new();
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD_CHAR);
        gtk_widget_set_size_request(text, 360, 120);
        gtk_box_pack_start(GTK_BOX(content), gtk_label_new(label), false, false, 5);
        gtk_box_pack_start(GTK_BOX(content), text, true, true, 5);
        gtk_widget_show_all(content);
        const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response == GTK_RESPONSE_ACCEPT) {
            GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
            GtkTextIter start;
            GtkTextIter end;
            gtk_text_buffer_get_bounds(buffer, &start, &end);
            gchar* value = gtk_text_buffer_get_text(buffer, &start, &end, false);
            message = value == nullptr ? "" : value;
            g_free(value);
        }
        gtk_widget_destroy(dialog);
        return response == GTK_RESPONSE_ACCEPT && !message.empty();
    }

    bool getAdminMessage(GtkWindow* parent, std::string& message) {
        GtkWidget* dialog = gtk_dialog_new_with_buttons("Admin Message to all", parent, GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "OK", GTK_RESPONSE_ACCEPT, nullptr);
        GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget* label = gtk_label_new("Message");
        GtkWidget* entry = gtk_entry_new();
        gtk_widget_set_size_request(entry, 300, -1);
        gtk_box_pack_start(GTK_BOX(content), label, false, false, 5);
        gtk_box_pack_start(GTK_BOX(content), entry, false, false, 5);
        gtk_widget_show_all(content);
        const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response == GTK_RESPONSE_ACCEPT) message = gtk_entry_get_text(GTK_ENTRY(entry));
        gtk_widget_destroy(dialog);
        return response == GTK_RESPONSE_ACCEPT && !message.empty();
    }
}

TPlayerList::TPlayerList(const std::filesystem::path& nextApplicationDirectory) : applicationDirectory(nextApplicationDirectory) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "PlayerList");
    gtk_window_set_title(GTK_WINDOW(window), "Players");
    gtk_window_set_default_size(GTK_WINDOW(window), 580, 420);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* notebook = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(notebook), 5);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_set_border_width(GTK_CONTAINER(scrolled), 5);
    store = gtk_tree_store_new(5, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_widget_set_name(tree, "PlayerListField");
    gtk_tree_view_set_fixed_height_mode(GTK_TREE_VIEW(tree), true);
    gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(tree), false);
    g_signal_connect(tree, "row-expanded", G_CALLBACK(onGroupExpanded), this);
    g_signal_connect(tree, "row-collapsed", G_CALLBACK(onGroupCollapsed), this);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), GTK_SELECTION_MULTIPLE);
    g_signal_connect(tree, "button-press-event", G_CALLBACK(onButtonPress), this);
    onlineIcon = gdk_pixbuf_new_from_file("images/plisticononline.png", nullptr);
    channelIcon = gdk_pixbuf_new_from_file("images/rcicon_channelopen.png", nullptr);
    channelClosedIcon = gdk_pixbuf_new_from_file("images/rcicon_channelclosed.png", nullptr);
    pmNormalIcon = gdk_pixbuf_new_from_file("images/pmicon_normal.png", nullptr);
    pmGuildIcon = gdk_pixbuf_new_from_file("images/pmicon_guild.png", nullptr);
    pmAdminIcon = gdk_pixbuf_new_from_file("images/pmicon_admin.png", nullptr);
    pmMassIcon = gdk_pixbuf_new_from_file("images/pmicon_mass.png", nullptr);
    GtkCellRenderer* imageRenderer = gtk_cell_renderer_pixbuf_new();
    GtkTreeViewColumn* imageColumn = gtk_tree_view_column_new_with_attributes("", imageRenderer, "pixbuf", PlayerIconColumn, nullptr);
    gtk_tree_view_column_set_sizing(imageColumn, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(imageColumn, 20);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), imageColumn);
    const struct { const char* title; int column; } columns[] = {{"Nick", PlayerNickColumn}, {"Account", PlayerAccountColumn}, {"Level", PlayerLevelColumn}, {"ID", PlayerIdColumn}};
    for (const auto& column : columns) {
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* viewColumn = gtk_tree_view_column_new_with_attributes(column.title, renderer, "text", column.column, nullptr);
        gtk_tree_view_column_set_sort_column_id(viewColumn, column.column);
        gtk_tree_view_column_set_resizable(viewColumn, true);
        gtk_tree_view_column_set_sizing(viewColumn, GTK_TREE_VIEW_COLUMN_FIXED);
        if (column.column == PlayerNickColumn) gtk_tree_view_column_set_fixed_width(viewColumn, 180);
        else if (column.column == PlayerAccountColumn || column.column == PlayerLevelColumn) gtk_tree_view_column_set_fixed_width(viewColumn, 120);
        else {
            gtk_tree_view_column_set_fixed_width(viewColumn, 60);
            gtk_tree_view_column_set_alignment(viewColumn, 1.0F);
        }
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), viewColumn);
    }
    gtk_container_add(GTK_CONTAINER(scrolled), tree);
    GtkCssProvider* expanderProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(expanderProvider, "treeview.view.expander { color: #ecea84; }", -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(tree), GTK_STYLE_PROVIDER(expanderProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(expanderProvider);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new("This server "));
    GtkCssProvider* tabProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(tabProvider, "#PlayerList notebook > header > tabs > tab { border: 1px solid #777777; border-radius: 4px 4px 0 0; margin-right: 1px; } #PlayerList notebook > header > tabs > tab:checked { border-color: #aaaaaa; }", -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(tabProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(tabProvider);
    for (const char* title : {"Guilds", "Servers", "Channels"}) {
        GtkWidget* page = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_set_border_width(GTK_CONTAINER(page), 5);
        if (std::string(title) == "Guilds" || std::string(title) == "Servers") {
            const bool isServerTab = std::string(title) == "Servers";
            GtkListStore* tabStore = nullptr;
            if (isServerTab) serverStore = gtk_tree_store_new(5, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
            else { guildStore = gtk_list_store_new(3, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING); tabStore = guildStore; }
            GtkWidget* tabTree = gtk_tree_view_new_with_model(isServerTab ? GTK_TREE_MODEL(serverStore) : GTK_TREE_MODEL(tabStore));
            if (isServerTab) {
                serverTree = tabTree;
                gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(tabTree), false);
                g_signal_connect(tabTree, "button-press-event", G_CALLBACK(onServerButtonPress), this);
            }
            GtkCellRenderer* iconRenderer = gtk_cell_renderer_pixbuf_new();
            GtkTreeViewColumn* iconColumn = gtk_tree_view_column_new_with_attributes("", iconRenderer, "pixbuf", 0, nullptr);
            gtk_tree_view_column_set_sizing(iconColumn, GTK_TREE_VIEW_COLUMN_FIXED);
            gtk_tree_view_column_set_fixed_width(iconColumn, 20);
            gtk_tree_view_append_column(GTK_TREE_VIEW(tabTree), iconColumn);
            GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
            const char* firstColumn = std::string(title) == "Guilds" ? "Nick" : "Nick";
            GtkTreeViewColumn* nickColumn = gtk_tree_view_column_new_with_attributes(firstColumn, renderer, "text", 1, nullptr);
            gtk_tree_view_column_set_sort_column_id(nickColumn, 1);
            gtk_tree_view_append_column(GTK_TREE_VIEW(tabTree), nickColumn);
            renderer = gtk_cell_renderer_text_new();
            GtkTreeViewColumn* accountColumn = gtk_tree_view_column_new_with_attributes("Account", renderer, "text", 2, nullptr);
            gtk_tree_view_column_set_sort_column_id(accountColumn, 2);
            gtk_tree_view_append_column(GTK_TREE_VIEW(tabTree), accountColumn);
            if (!isServerTab) gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(tabStore), 0, GTK_SORT_ASCENDING);
            gtk_container_add(GTK_CONTAINER(page), tabTree);
        } else {
            channelStore = gtk_list_store_new(4, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
            GtkWidget* channelTree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(channelStore));
            GtkCellRenderer* iconRenderer = gtk_cell_renderer_pixbuf_new();
            gtk_tree_view_append_column(GTK_TREE_VIEW(channelTree), gtk_tree_view_column_new_with_attributes("", iconRenderer, "pixbuf", 0, nullptr));
            const struct { const char* title; int column; } channelColumns[] = {{"Nick", 1}, {"Players", 2}, {"ID", 3}};
            for (const auto& column : channelColumns) {
                GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
                GtkTreeViewColumn* viewColumn = gtk_tree_view_column_new_with_attributes(column.title, renderer, "text", column.column, nullptr);
                gtk_tree_view_column_set_sort_column_id(viewColumn, column.column);
                gtk_tree_view_append_column(GTK_TREE_VIEW(channelTree), viewColumn);
            }
            gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(channelStore), 0, GTK_SORT_ASCENDING);
            gtk_container_add(GTK_CONTAINER(page), channelTree);
        }
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, gtk_label_new((std::string(title) + " ").c_str()));
    }
    gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);
    GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(bottom), gtk_label_new("Status:"), false, false, 5);
    GtkWidget* status = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(status), "Online");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(status), "Away");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(status), "Offline");
    gtk_combo_box_set_active(GTK_COMBO_BOX(status), 0);
    gtk_box_pack_start(GTK_BOX(bottom), status, false, false, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* refreshButton = gtk_button_new_with_label("Refresh");
    GtkWidget* massPMButton = gtk_button_new_with_label("Mass PM");
    GtkWidget* adminMessageButton = gtk_button_new_with_label("Admin Message");
    GtkWidget* closeButton = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), refreshButton);
    gtk_container_add(GTK_CONTAINER(buttons), massPMButton);
    gtk_container_add(GTK_CONTAINER(buttons), adminMessageButton);
    gtk_container_add(GTK_CONTAINER(buttons), closeButton);
    gtk_box_pack_start(GTK_BOX(bottom), buttons, true, true, 0);
    gtk_box_pack_start(GTK_BOX(root), bottom, false, false, 0);
    g_signal_connect(refreshButton, "clicked", G_CALLBACK(onRefresh), this);
    g_signal_connect(massPMButton, "clicked", G_CALLBACK(onMassPM), this);
    g_signal_connect(adminMessageButton, "clicked", G_CALLBACK(onAdminMessage), this);
    g_signal_connect(closeButton, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TPlayerList::~TPlayerList() { if (pmBlinkSource != 0) g_source_remove(pmBlinkSource); delete localBanWindow; if (onlineIcon != nullptr) g_object_unref(onlineIcon); if (channelIcon != nullptr) g_object_unref(channelIcon); if (channelClosedIcon != nullptr) g_object_unref(channelClosedIcon); if (pmNormalIcon != nullptr) g_object_unref(pmNormalIcon); if (pmGuildIcon != nullptr) g_object_unref(pmGuildIcon); if (pmAdminIcon != nullptr) g_object_unref(pmAdminIcon); if (pmMassIcon != nullptr) g_object_unref(pmMassIcon); if (window != nullptr) gtk_widget_destroy(window); }
void TPlayerList::open(void* nextConnection) { connection = nextConnection; rc_on_pm_servers_updated(connection, onPMServers, this); rc_on_pm_guilds_updated(connection, onPMGuilds, this); rc_on_pm_server_players(connection, onPMServerPlayers, this); rc_on_ban_data(connection, onBanData, this); refresh(); gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); }
void TPlayerList::onRefresh(GtkButton*, gpointer data) { static_cast<TPlayerList*>(data)->refresh(); }
void TPlayerList::onMassPM(GtkButton*, gpointer data) { static_cast<TPlayerList*>(data)->sendMassPM(); }
void TPlayerList::onAdminMessage(GtkButton*, gpointer data) { static_cast<TPlayerList*>(data)->sendAdminMessage(); }
void TPlayerList::onClose(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TPlayerList*>(data)->window); }
gboolean TPlayerList::onButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    if (event->type != GDK_BUTTON_PRESS && event->type != GDK_2BUTTON_PRESS) return false;
    GtkTreePath* path = nullptr;
    if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), static_cast<gint>(event->x), static_cast<gint>(event->y), &path, nullptr, nullptr, nullptr)) return false;
    if (event->button == GDK_BUTTON_PRIMARY && gtk_tree_path_get_depth(path) == 1) {
        if (gtk_tree_view_row_expanded(GTK_TREE_VIEW(widget), path)) gtk_tree_view_collapse_row(GTK_TREE_VIEW(widget), path);
        else gtk_tree_view_expand_row(GTK_TREE_VIEW(widget), path, false);
        gtk_tree_path_free(path);
        return true;
    }
    if (event->type == GDK_2BUTTON_PRESS && event->button == GDK_BUTTON_PRIMARY && gtk_tree_path_get_depth(path) == 2) {
        GtkTreeIter row;
        gtk_tree_model_get_iter(GTK_TREE_MODEL(static_cast<TPlayerList*>(data)->store), &row, path);
        int playerId = 0;
        gchar* account = nullptr;
        gchar* nick = nullptr;
        gtk_tree_model_get(GTK_TREE_MODEL(static_cast<TPlayerList*>(data)->store), &row, PlayerIdColumn, &playerId, PlayerAccountColumn, &account, PlayerNickColumn, &nick, -1);
        static_cast<TPlayerList*>(data)->openPrivateMessage(playerId, account == nullptr ? "" : account, nick == nullptr ? "" : nick);
        g_free(account);
        g_free(nick);
        gtk_tree_path_free(path);
        return true;
    }
    if (event->button != GDK_BUTTON_SECONDARY || gtk_tree_path_get_depth(path) != 2) { gtk_tree_path_free(path); return false; }
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
    gtk_tree_selection_unselect_all(selection);
    gtk_tree_selection_select_path(selection, path);
    gtk_tree_path_free(path);
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* access = gtk_menu_item_new_with_label("Edit Access");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), access);
    g_signal_connect(access, "activate", G_CALLBACK(onEditAccess), data);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
    return true;
}
void TPlayerList::onEditAccess(GtkMenuItem*, gpointer data) { static_cast<TPlayerList*>(data)->editAccess(); }
void TPlayerList::onBanData(const char* account, const char*, const char* details, void* data) {
    TPlayerList* list = static_cast<TPlayerList*>(data);
    if (account == nullptr || *account == '\0') return;
    if (list->localBanWindow == nullptr) list->localBanWindow = new TLocalBanWindow();
    list->localBanWindow->open(list->connection, account, details == nullptr ? "" : details);
}
gboolean TPlayerList::onPMBlink(gpointer data) { TPlayerList* list = static_cast<TPlayerList*>(data); list->pmIconsVisible = !list->pmIconsVisible; list->updatePMIcons(); return G_SOURCE_CONTINUE; }
void TPlayerList::onPMServers(int, void* data) { static_cast<TPlayerList*>(data)->refreshRemoteLists(); }
void TPlayerList::onPMGuilds(int, void* data) { static_cast<TPlayerList*>(data)->refreshRemoteLists(); }
void TPlayerList::onPMServerPlayers(const char* serverName, const char* playerData, void* data) {
    TPlayerList* list = static_cast<TPlayerList*>(data);
    if (list->serverStore == nullptr || serverName == nullptr) return;
    std::vector<std::string>& players = list->serverPlayers[serverName];
    players.clear();
    std::istringstream input(playerData == nullptr ? "" : playerData);
    for (std::string line; std::getline(input, line);) if (!line.empty()) players.push_back(line);
    GtkTreeIter row;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list->serverStore), &row);
    while (valid) {
        gchar* value = nullptr;
        gtk_tree_model_get(GTK_TREE_MODEL(list->serverStore), &row, 1, &value, -1);
        const bool match = value != nullptr && std::string(value) == serverName;
        g_free(value);
        if (match) {
            GtkTreeIter child;
            while (gtk_tree_model_iter_children(GTK_TREE_MODEL(list->serverStore), &child, &row)) gtk_tree_store_remove(list->serverStore, &child);
            for (const std::string& player : players) {
                gtk_tree_store_append(list->serverStore, &child, &row);
                gtk_tree_store_set(list->serverStore, &child, 0, list->onlineIcon, 1, player.c_str(), 2, "", 3, true, -1);
            }
            gtk_tree_store_set(list->serverStore, &row, 0, list->channelIcon, 3, true, 4, true, -1);
            if (list->serverTree != nullptr) {
                GtkTreePath* path = gtk_tree_model_get_path(GTK_TREE_MODEL(list->serverStore), &row);
                gtk_tree_view_expand_row(GTK_TREE_VIEW(list->serverTree), path, false);
                gtk_tree_path_free(path);
            }
            break;
        }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(list->serverStore), &row);
    }
}
gboolean TPlayerList::onServerButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_PRIMARY) return false;
    GtkTreePath* path = nullptr;
    if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), static_cast<gint>(event->x), static_cast<gint>(event->y), &path, nullptr, nullptr, nullptr)) return false;
    onServerActivated(GTK_TREE_VIEW(widget), path, nullptr, data);
    gtk_tree_path_free(path);
    return true;
}

void TPlayerList::onServerActivated(GtkTreeView* tree, GtkTreePath* path, GtkTreeViewColumn*, gpointer data) {
    TPlayerList* list = static_cast<TPlayerList*>(data);
    if (gtk_tree_path_get_depth(path) != 1) return;
    GtkTreeIter row;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(list->serverStore), &row, path)) return;
    gboolean requested = false;
    gboolean received = false;
    gchar* serverName = nullptr;
    gtk_tree_model_get(gtk_tree_view_get_model(tree), &row, 1, &serverName, 3, &requested, 4, &received, -1);
    if (!requested && serverName != nullptr) {
        gtk_tree_store_set(list->serverStore, &row, 3, true, -1);
        rc_request_pm_server_players(list->connection, serverName);
    } else if (received && gtk_tree_view_row_expanded(tree, path)) {
        gtk_tree_view_collapse_row(tree, path);
        gtk_tree_store_set(list->serverStore, &row, 0, list->channelClosedIcon, -1);
    } else if (received) {
        gtk_tree_view_expand_row(tree, path, false);
        gtk_tree_store_set(list->serverStore, &row, 0, list->channelIcon, -1);
    }
    g_free(serverName);
}
void TPlayerList::onGroupExpanded(GtkTreeView*, GtkTreeIter* row, GtkTreePath*, gpointer data) { TPlayerList* list = static_cast<TPlayerList*>(data); gtk_tree_store_set(list->store, row, PlayerIconColumn, list->channelIcon, -1); }
void TPlayerList::onGroupCollapsed(GtkTreeView*, GtkTreeIter* row, GtkTreePath*, gpointer data) { TPlayerList* list = static_cast<TPlayerList*>(data); gtk_tree_store_set(list->store, row, PlayerIconColumn, list->channelClosedIcon, -1); }
gboolean TPlayerList::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TPlayerList*>(data)->window); return true; }
void TPlayerList::refresh() {
    gtk_tree_store_clear(store);
    RCPlayer* players = nullptr;
    const int count = rc_get_players(connection, &players);
    GtkTreeIter admins;
    GtkTreeIter playersGroup;
    gtk_tree_store_append(store, &admins, nullptr);
    gtk_tree_store_set(store, &admins, PlayerIconColumn, channelIcon, PlayerNickColumn, "Admins", PlayerIdColumn, 0, -1);
    gtk_tree_store_append(store, &playersGroup, nullptr);
    gtk_tree_store_set(store, &playersGroup, PlayerIconColumn, channelIcon, PlayerNickColumn, "Players", PlayerIdColumn, 0, -1);
    for (int index = 0; index < count; ++index) {
        GtkTreeIter row;
        const bool admin = players[index].level == nullptr || *players[index].level == '\0';
        gtk_tree_store_append(store, &row, admin ? &admins : &playersGroup);
        const auto pm = pmTypes.find(players[index].id);
        gtk_tree_store_set(store, &row, PlayerIconColumn, pm != pmTypes.end() && pmIconsVisible ? pmIconFor(pm->second) : onlineIcon, PlayerNickColumn, players[index].nick == nullptr ? "" : players[index].nick, PlayerAccountColumn, players[index].account == nullptr ? "" : players[index].account, PlayerLevelColumn, players[index].level == nullptr ? "" : players[index].level, PlayerIdColumn, players[index].id, -1);
    }
    rc_request_pm_server_list(connection);
    rc_request_pm_guild_list(connection);
    refreshRemoteLists();
    gtk_tree_view_expand_all(GTK_TREE_VIEW(tree));
}

GdkPixbuf* TPlayerList::pmIconFor(const std::string& type) const {
    if (type == "mass") return pmMassIcon;
    if (type == "guild") return pmGuildIcon;
    if (type == "admin") return pmAdminIcon;
    return pmNormalIcon;
}

void TPlayerList::notePrivateMessage(int playerId, const char* account, const char*, const char* message, const char* type) {
    if (playerId < 0) return;
    appendHistory(account, "Opposite", message);
    pmTypes[playerId] = type == nullptr ? "normal" : type;
    pmIconsVisible = true;
    if (pmBlinkSource == 0) pmBlinkSource = g_timeout_add(500, onPMBlink, this);
    updatePMIcons();
}

void TPlayerList::appendHistory(const char* account, const char* direction, const char* message) const {
    if (account == nullptr || *account == '\0' || message == nullptr || *message == '\0') return;
    const std::filesystem::path directory = applicationDirectory / "PMs";
    std::filesystem::create_directories(directory);
    std::ofstream output(directory / (std::string(account) + ".txt"), std::ios::app | std::ios::binary);
    output << direction << ":\n" << message << "\n";
}

void TPlayerList::openPrivateMessage(int playerId, const char* account, const char* nick) {
    if (playerId == 0 || account == nullptr || *account == '\0') return;
    PMWindowData* data = new PMWindowData{connection, applicationDirectory / "PMs", nullptr, nullptr, playerId, account, nick == nullptr ? "" : nick};
    data->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(data->window), "PM");
    gtk_window_set_default_size(GTK_WINDOW(data->window), 380, 170);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(root), 8);
    gtk_container_add(GTK_CONTAINER(data->window), root);
    gtk_box_pack_start(GTK_BOX(root), gtk_label_new((data->account + ": " + data->nick).c_str()), false, false, 0);
    data->reply = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(root), data->reply, false, false, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* history = gtk_button_new_with_label("History");
    GtkWidget* send = gtk_button_new_with_label("Send");
    gtk_container_add(GTK_CONTAINER(buttons), history);
    gtk_container_add(GTK_CONTAINER(buttons), send);
    gtk_box_pack_end(GTK_BOX(root), buttons, false, false, 0);
    g_signal_connect(send, "clicked", G_CALLBACK(onPMSend), data);
    g_signal_connect(history, "clicked", G_CALLBACK(onPMHistory), data);
    g_signal_connect(data->window, "delete-event", G_CALLBACK(onPMWindowDelete), data);
    gtk_widget_show_all(data->window);
    gtk_window_present(GTK_WINDOW(data->window));
}

void TPlayerList::updatePMIcons() {
    GtkTreeIter group;
    gboolean validGroup = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &group);
    while (validGroup) {
        GtkTreeIter row;
        gboolean valid = gtk_tree_model_iter_children(GTK_TREE_MODEL(store), &row, &group);
        while (valid) {
            int playerId = 0;
            gtk_tree_model_get(GTK_TREE_MODEL(store), &row, PlayerIdColumn, &playerId, -1);
            const auto pm = pmTypes.find(playerId);
            if (pm != pmTypes.end()) gtk_tree_store_set(store, &row, PlayerIconColumn, pmIconsVisible ? pmIconFor(pm->second) : onlineIcon, -1);
            valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &row);
        }
        validGroup = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &group);
    }
}

void TPlayerList::refreshRemoteLists() {
    if (guildStore != nullptr) {
        gtk_list_store_clear(guildStore);
        const char** guilds = nullptr;
        const int count = rc_get_pm_guilds(connection, &guilds);
        for (int index = 0; index < count; ++index) { GtkTreeIter row; gtk_list_store_append(guildStore, &row); gtk_list_store_set(guildStore, &row, 0, channelClosedIcon, 1, guilds[index], 2, "", -1); }
    }
    if (serverStore != nullptr) {
        gtk_tree_store_clear(serverStore);
        const char** servers = nullptr;
        const int count = rc_get_pm_servers(connection, &servers);
        for (int index = 0; index < count; ++index) {
            GtkTreeIter row;
            gtk_tree_store_append(serverStore, &row, nullptr);
            const auto found = serverPlayers.find(servers[index]);
            const gboolean received = found != serverPlayers.end();
            gtk_tree_store_set(serverStore, &row, 0, received ? channelIcon : channelClosedIcon, 1, servers[index], 2, "", 3, received, 4, received, -1);
            if (found != serverPlayers.end()) for (const std::string& player : found->second) {
                GtkTreeIter child;
                gtk_tree_store_append(serverStore, &child, &row);
                gtk_tree_store_set(serverStore, &child, 0, onlineIcon, 1, player.c_str(), 2, "", 3, true, -1);
            }
        }
    }
}

std::vector<int> TPlayerList::playerIds() const {
    std::vector<int> ids;
    GtkTreeIter group;
    gboolean validGroup = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &group);
    while (validGroup) {
        GtkTreeIter row;
        gboolean valid = gtk_tree_model_iter_children(GTK_TREE_MODEL(store), &row, &group);
        while (valid) { int id = 0; gtk_tree_model_get(GTK_TREE_MODEL(store), &row, PlayerIdColumn, &id, -1); if (id != 0) ids.push_back(id); valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &row); }
        validGroup = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &group);
    }
    return ids;
}

void TPlayerList::sendMassPM() {
    const std::vector<int> ids = playerIds();
    if (ids.empty()) return;
    std::string message;
    if (!getMessage(GTK_WINDOW(window), "Mass PM to all", "Message:", message)) return;
    rc_send_mass_pm(connection, ids.data(), static_cast<int>(ids.size()), message.c_str());
}

void TPlayerList::sendAdminMessage() {
    std::string message;
    if (!getAdminMessage(GTK_WINDOW(window), message)) return;
    rc_send_admin_message_all(connection, message.c_str());
}

void TPlayerList::editAccess() {
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(selection, &model, &row)) return;
    gchar* account = nullptr;
    int playerId = 0;
    gtk_tree_model_get(model, &row, PlayerAccountColumn, &account, PlayerIdColumn, &playerId, -1);
    if (account != nullptr && *account != '\0' && playerId != 0) rc_request_player_ban(connection, account, playerId);
    g_free(account);
}
