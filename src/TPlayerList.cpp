#include "TPlayerList.h"
#include "Backup.h"
#include "TLocalBanWindow.h"

#include <grclib.h>

#include <string>
#include <cstdlib>
#include <vector>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <map>

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
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(windowData->reply));
        GtkTextIter start;
        GtkTextIter end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, false);
        if (text == nullptr || *text == '\0') { g_free(text); return; }
        if (rc_send_private_message(windowData->connection, windowData->playerId, text) == 0) { g_free(text); return; }
        std::filesystem::create_directories(windowData->historyDirectory);
        std::ofstream output(windowData->historyDirectory / (windowData->account + ".txt"), std::ios::app | std::ios::binary);
        output << "You:\n" << text << "\n";
        g_free(text);
        gtk_text_buffer_set_text(buffer, "", -1);
    }

    void onPMHistory(GtkButton*, gpointer data) {
        PMWindowData* windowData = static_cast<PMWindowData*>(data);
        GtkWidget* history = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_widget_set_name(history, "PrivateMessageHistory");
        gtk_window_set_title(GTK_WINDOW(history), ("History: " + windowData->account + " - " + windowData->nick).c_str());
        gtk_window_set_default_size(GTK_WINDOW(history), 440, 320);
        GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
        GtkWidget* field = gtk_text_view_new();
        gtk_widget_set_name(field, "PrivateMessageHistoryText");
        gtk_text_view_set_editable(GTK_TEXT_VIEW(field), false);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(field), false);
        GtkCssProvider* provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider, "#PrivateMessageHistory, #PrivateMessageHistory scrolledwindow, #PrivateMessageHistory viewport, #PrivateMessageHistoryText, #PrivateMessageHistoryText text { background-color: #1e1e1e; color: #d4d4d4; }", -1, nullptr);
        gtk_style_context_add_provider(gtk_widget_get_style_context(history), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
        gtk_style_context_add_provider(gtk_widget_get_style_context(field), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
        g_object_unref(provider);
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

    bool getMessage(GtkWindow* parent, const char* title, const char* label, std::string& message, const char* acceptLabel = "Send") {
        GtkWidget* dialog = gtk_dialog_new_with_buttons(title, parent, GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, acceptLabel, GTK_RESPONSE_ACCEPT, nullptr);
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
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), GTK_SELECTION_SINGLE);
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
            gtk_tree_view_column_set_alignment(viewColumn, 1.0F);
            g_object_set(renderer, "xalign", 1.0F, nullptr);
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
    gtk_css_provider_load_from_data(tabProvider, "#PlayerList notebook > header > tabs > tab { min-height: 0; border: 1px solid #777777; border-bottom: 0; border-radius: 4px 4px 0 0; margin-right: 1px; padding: 5px 8px; } #PlayerList notebook > header > tabs > tab label { margin: 0; padding: 0; font-size: 12px; } #PlayerList notebook > header > tabs > tab:checked { border-color: #aaaaaa; margin-bottom: -1px; }", -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(tabProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(tabProvider);
    for (const char* title : {"Guilds", "Servers", "Channels"}) {
        GtkWidget* page = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_set_border_width(GTK_CONTAINER(page), 5);
        if (std::string(title) == "Guilds" || std::string(title) == "Servers") {
            const bool isServerTab = std::string(title) == "Servers";
            GtkListStore* tabStore = nullptr;
            if (isServerTab) serverStore = gtk_tree_store_new(6, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_INT);
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
    GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(bottom, 4);
    gtk_widget_set_margin_end(bottom, 4);
    gtk_widget_set_margin_top(bottom, 2);
    gtk_widget_set_margin_bottom(bottom, 4);
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
    for (GtkWidget* button : {refreshButton, massPMButton, adminMessageButton, closeButton}) gtk_widget_set_size_request(button, -1, 28);
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
void TPlayerList::open(void* nextConnection) { setConnection(nextConnection); rc_on_pm_servers_updated(connection, onPMServers, this); rc_on_pm_guilds_updated(connection, onPMGuilds, this); rc_on_pm_server_players(connection, onPMServerPlayers, this); refresh(); gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); }
void TPlayerList::setConnection(void* nextConnection) { connection = nextConnection; }
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
    GtkWidget* privateMessage = gtk_menu_item_new_with_label("Private Message");
    GtkWidget* history = gtk_menu_item_new_with_label("History");
    GtkWidget* profile = gtk_menu_item_new_with_label("Profile");
    GtkWidget* disconnect = gtk_menu_item_new_with_label("Disconnect");
    GtkWidget* reset = gtk_menu_item_new_with_label("Reset");
    GtkWidget* editAccount = gtk_menu_item_new_with_label("Edit Account");
    GtkWidget* warp = gtk_menu_item_new_with_label("Warp");
    GtkWidget* updateLevel = gtk_menu_item_new_with_label("Update Level");
    GtkWidget* adminMessage = gtk_menu_item_new_with_label("Admin Message");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), privateMessage);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), history);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), profile);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    GtkWidget* access = gtk_menu_item_new_with_label("Edit Access");
    GtkWidget* attributes = gtk_menu_item_new_with_label("Edit Attributes");
    GtkWidget* rights = gtk_menu_item_new_with_label("Edit Rights");
    GtkWidget* comments = gtk_menu_item_new_with_label("Edit Comments");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), attributes);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), rights);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), comments);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), access);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), disconnect);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), reset);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), editAccount);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), warp);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), updateLevel);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), adminMessage);
    g_signal_connect(privateMessage, "activate", G_CALLBACK(onPrivateMessageMenu), data);
    g_signal_connect(history, "activate", G_CALLBACK(onHistoryMenu), data);
    g_signal_connect(profile, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer userData) { static_cast<TPlayerList*>(userData)->editProfile(); }), data);
    g_signal_connect(access, "activate", G_CALLBACK(onEditAccess), data);
    g_signal_connect(attributes, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer userData) { static_cast<TPlayerList*>(userData)->editAttributes(); }), data);
    g_signal_connect(rights, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer userData) { static_cast<TPlayerList*>(userData)->editRights(); }), data);
    g_signal_connect(comments, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer userData) { static_cast<TPlayerList*>(userData)->editComments(); }), data);
    g_signal_connect(disconnect, "activate", G_CALLBACK(onDisconnectPlayer), data);
    g_signal_connect(reset, "activate", G_CALLBACK(onResetPlayer), data);
    g_signal_connect(editAccount, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer userData) { static_cast<TPlayerList*>(userData)->editAccount(); }), data);
    g_signal_connect(warp, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer userData) { static_cast<TPlayerList*>(userData)->warpSelectedPlayer(); }), data);
    g_signal_connect(updateLevel, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer userData) { static_cast<TPlayerList*>(userData)->updateSelectedPlayerLevel(); }), data);
    g_signal_connect(adminMessage, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer userData) { static_cast<TPlayerList*>(userData)->adminMessageSelectedPlayer(); }), data);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
    return true;
}
void TPlayerList::onEditAccess(GtkMenuItem*, gpointer data) { static_cast<TPlayerList*>(data)->editAccess(); }
void TPlayerList::onPrivateMessageMenu(GtkMenuItem*, gpointer data) { static_cast<TPlayerList*>(data)->openSelectedPrivateMessage(); }
void TPlayerList::onHistoryMenu(GtkMenuItem*, gpointer data) { static_cast<TPlayerList*>(data)->openSelectedHistory(); }
void TPlayerList::onDisconnectPlayer(GtkMenuItem*, gpointer data) { static_cast<TPlayerList*>(data)->disconnectSelectedPlayer(); }
void TPlayerList::onResetPlayer(GtkMenuItem*, gpointer data) { static_cast<TPlayerList*>(data)->resetSelectedPlayer(); }
void TPlayerList::handleBanData(const char* account, const char* computerId, const char* details) {
    if (account == nullptr || *account == '\0') return;
    if (localBanWindow == nullptr) localBanWindow = new TLocalBanWindow();
    localBanWindow->open(connection, account, computerId == nullptr ? "" : computerId, details == nullptr ? "" : details);
}
void TPlayerList::handleBanListData(const char* type, const char* account, const char* content) {
    if (type == nullptr) return;
    const std::string listType(type);
    if (listType == "bantypes") {
        if (localBanWindow == nullptr) localBanWindow = new TLocalBanWindow();
        localBanWindow->setBanTypes(content);
        return;
    }
    if (listType != "banhistory" && listType != "staffactivity") return;
    GtkWidget* dialog = gtk_dialog_new_with_buttons((std::string(listType == "banhistory" ? "Ban History of " : "Staff Activity of ") + (account == nullptr ? "" : account)).c_str(), GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, "Close", GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 440, 300);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkWidget* field = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(field), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(field), false);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(field)), content == nullptr || *content == '\0' ? "(none)" : content, -1);
    gtk_container_add(GTK_CONTAINER(scrolled), field);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scrolled, true, true, 0);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* responseDialog, gint, gpointer) { gtk_widget_destroy(GTK_WIDGET(responseDialog)); }), nullptr);
    gtk_widget_show_all(dialog);
}
void TPlayerList::handlePlayerRights(const char* account, int rights, const char* ipRange, const char* folderAccess) {
    if (account == nullptr || *account == '\0') return;
    struct RightsState { TPlayerList* list; std::string account; GtkWidget* ipRange; GtkWidget* folderAccess; GtkWidget* checks[20]{}; };
    GtkWidget* dialog = gtk_dialog_new_with_buttons(("Edit Rights of " + std::string(account)).c_str(), GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL, "Apply", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_widget_set_name(dialog, "EditRightsWindow");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 460, 420);
    GtkWidget* notebook = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(notebook), 5);
    GtkWidget* flags = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_container_set_border_width(GTK_CONTAINER(flags), 5);
    GtkWidget* accountRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget* accountField = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(accountField), account);
    gtk_editable_set_editable(GTK_EDITABLE(accountField), false);
    gtk_box_pack_start(GTK_BOX(accountRow), gtk_label_new("Account name:"), false, false, 0);
    gtk_box_pack_start(GTK_BOX(accountRow), accountField, true, true, 0);
    gtk_box_pack_start(GTK_BOX(flags), accountRow, false, false, 0);
    GtkWidget* ipRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget* ipField = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(ipField), ipRange == nullptr ? "" : ipRange);
    gtk_box_pack_start(GTK_BOX(ipRow), gtk_label_new("IP range(s):"), false, false, 0);
    gtk_box_pack_start(GTK_BOX(ipRow), ipField, true, true, 0);
    gtk_box_pack_start(GTK_BOX(flags), ipRow, false, false, 0);
    auto* state = new RightsState{this, account, ipField, nullptr};
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    const struct { const char* label; int bit; int column; int row; } rightsLayout[] = {
        {"Warpto XY", 0, 0, 0}, {"Set server flags", 15, 1, 0}, {"Warpto player", 1, 0, 1}, {"Change rights", 10, 1, 1},
        {"Warp players", 2, 0, 2}, {"Ban players", 11, 1, 2}, {"Update level", 3, 0, 3}, {"Change comments", 12, 1, 3},
        {"Disconnect players", 4, 0, 4}, {"Change staff accounts", 14, 1, 4}, {"View player attributes", 5, 0, 5}, {"Change server options", 16, 1, 5},
        {"Set player attributes", 6, 0, 6}, {"Edit folder configuration", 17, 1, 6}, {"Set the own attributes", 7, 0, 7}, {"Edit folder rights", 18, 1, 7},
        {"Reset attributes", 8, 0, 8}, {"NPC-Control", 19, 1, 8}, {"Admin message", 9, 0, 9}
    };
    for (const auto& entry : rightsLayout) {
        state->checks[entry.bit] = gtk_check_button_new_with_label(entry.label);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->checks[entry.bit]), (rights & (1 << entry.bit)) != 0);
        gtk_grid_attach(GTK_GRID(grid), state->checks[entry.bit], entry.column, entry.row, 1, 1);
    }
    GtkWidget* clearAll = gtk_button_new_with_label("Clear all");
    g_signal_connect(clearAll, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { auto* state = static_cast<RightsState*>(data); for (GtkWidget* check : state->checks) if (check != nullptr) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), false); }), state);
    gtk_grid_attach(GTK_GRID(grid), clearAll, 1, 9, 1, 1);
    gtk_box_pack_start(GTK_BOX(flags), grid, false, false, 0);
    GtkWidget* folderScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_set_border_width(GTK_CONTAINER(folderScroll), 5);
    GtkWidget* folderField = gtk_text_view_new();
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(folderField), 5);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(folderField), 5);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(folderField)), folderAccess == nullptr ? "" : folderAccess, -1);
    gtk_container_add(GTK_CONTAINER(folderScroll), folderField);
    state->folderAccess = folderField;
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), flags, gtk_label_new("IP Range and Right flags"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), folderScroll, gtk_label_new("Folder rights"));
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), notebook, true, true, 0);
    auto onAttributeResponse = +[](GtkDialog* responseDialog, gint response, gpointer userData) {
        auto* state = static_cast<RightsState*>(userData);
        if (response == GTK_RESPONSE_ACCEPT) {
            int value = 0;
            for (int bit = 0; bit < 20; ++bit) if (state->checks[bit] != nullptr && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->checks[bit]))) value |= 1 << bit;
            GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->folderAccess));
            GtkTextIter start; GtkTextIter end;
            gtk_text_buffer_get_bounds(buffer, &start, &end);
            gchar* folders = gtk_text_buffer_get_text(buffer, &start, &end, false);
            rc_set_player_rights(state->list->connection, state->account.c_str(), value, gtk_entry_get_text(GTK_ENTRY(state->ipRange)), folders == nullptr ? "" : folders);
            g_free(folders);
        }
        gtk_widget_destroy(GTK_WIDGET(responseDialog));
    };
    g_signal_connect(dialog, "response", G_CALLBACK(onAttributeResponse), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer userData) { delete static_cast<RightsState*>(userData); }), state);
    gtk_widget_show_all(dialog);
}
void TPlayerList::handlePlayerAttributes(const char* account, const char*, const char* editorText) {
    if (account == nullptr || *account == '\0') return;
    backupEditorText("attributes", account, editorText == nullptr ? "" : editorText, false);
    struct AttributeState { TPlayerList* list; std::string account; std::map<std::string, GtkWidget*> fields; GtkWidget* male; GtkWidget* weapons; GtkWidget* spin; GtkWidget* chests; GtkWidget* weaponList; GtkWidget* flags; };
    auto fieldValues = [](const char* source) {
        std::map<std::string, std::string> values;
        std::istringstream input(source == nullptr ? "" : source);
        for (std::string line; std::getline(input, line);) { const size_t colon = line.find(':'); if (colon != std::string::npos) values[line.substr(0, colon)] = line.substr(colon + 1); }
        return values;
    };
    const std::map<std::string, std::string> values = fieldValues(editorText);
    GtkWidget* dialog = gtk_dialog_new_with_buttons(("Edit Attributes of " + std::string(account)).c_str(), GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL, "Apply", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_widget_set_name(dialog, "EditAttributesWindow");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 360);
    GtkWidget* notebook = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(notebook), 5);
    auto* state = new AttributeState{this, account, {}, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    auto addFields = [&](const char* tab, const std::vector<std::pair<const char*, bool>>& labels) {
        GtkWidget* grid = gtk_grid_new();
        gtk_container_set_border_width(GTK_CONTAINER(grid), 5);
        gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
        for (int row = 0; row < static_cast<int>(labels.size()); ++row) {
            GtkWidget* entry = gtk_entry_new();
            const auto found = values.find(labels[row].first);
            gtk_entry_set_text(GTK_ENTRY(entry), found == values.end() ? "" : found->second.c_str() + (found->second.empty() ? 0 : 1));
            gtk_editable_set_editable(GTK_EDITABLE(entry), labels[row].second);
            gtk_grid_attach(GTK_GRID(grid), gtk_label_new((std::string(labels[row].first) + ":").c_str()), 0, row, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), entry, 1, row, 1, 1);
            state->fields[labels[row].first] = entry;
        }
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), grid, gtk_label_new(tab));
    };
    addFields("Stats", {{"Account", false}, {"Last IP", false}, {"Kills", false}, {"Deaths", false}, {"Online Seconds", false}, {"Rating", false}, {"Rating Deviation", false}});
    addFields("Look", {{"Head Image", true}, {"Body Image", true}, {"Animation", true}, {"Skin Color", true}, {"Coat Color", true}, {"Sleeves Color", true}, {"Shoes Color", true}, {"Belt Color", true}});
    addFields("Basic Attributes", {{"Level", true}, {"X", true}, {"Y", true}, {"Hearts", true}, {"Full Hearts", true}, {"AP", true}, {"MP", true}, {"Gralats", true}, {"Glove", true}, {"Bombs", true}, {"Arrows", true}, {"Sword Power", true}, {"Sword Image", true}, {"Shield Power", true}, {"Shield Image", true}});
    GtkWidget* basic = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), 2);
    state->male = gtk_check_button_new_with_label("male");
    state->weapons = gtk_check_button_new_with_label("weapons enabled");
    state->spin = gtk_check_button_new_with_label("spin attack");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->male), values.find("Male") != values.end() && values.at("Male").find("true") != std::string::npos);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->weapons), values.find("Weapons Enabled") != values.end() && values.at("Weapons Enabled").find("true") != std::string::npos);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->spin), values.find("Spin Attack") != values.end() && values.at("Spin Attack").find("true") != std::string::npos);
    gtk_grid_attach(GTK_GRID(basic), state->male, 0, 15, 2, 1);
    gtk_grid_attach(GTK_GRID(basic), state->weapons, 0, 16, 2, 1);
    gtk_grid_attach(GTK_GRID(basic), state->spin, 0, 17, 2, 1);
    auto addList = [&](const char* tab, GtkWidget** target) {
        GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_set_border_width(GTK_CONTAINER(scrolled), 5);
        *target = gtk_text_view_new();
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(*target), 5);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(*target), 5);
        gtk_container_add(GTK_CONTAINER(scrolled), *target);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new(tab));
    };
    addList("Chests", &state->chests);
    addList("Weapons", &state->weaponList);
    addList("Script Flags", &state->flags);
    std::string section;
    std::istringstream source(editorText == nullptr ? "" : editorText);
    for (std::string line; std::getline(source, line);) {
        if (line == "[Chests]") { section = "Chests"; continue; }
        if (line == "[Weapons]") { section = "Weapons"; continue; }
        if (line == "[Script Flags]") { section = "Script Flags"; continue; }
        if (!line.empty() && line.front() == '[') { section.clear(); continue; }
        if (line.empty() || line.find(':') != std::string::npos) continue;
        GtkWidget* target = section == "Chests" ? state->chests : section == "Weapons" ? state->weaponList : section == "Script Flags" ? state->flags : nullptr;
        if (target != nullptr) { GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(target)); GtkTextIter end; gtk_text_buffer_get_end_iter(buffer, &end); gtk_text_buffer_insert(buffer, &end, (line + "\n").c_str(), -1); }
    }
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), notebook, true, true, 0);
    auto onAttributeFormResponse = +[](GtkDialog* responseDialog, gint response, gpointer userData) {
        auto* state = static_cast<AttributeState*>(userData);
        if (response == GTK_RESPONSE_ACCEPT) {
            std::ostringstream text;
            const char* sections[] = {"Stats", "Look", "Basic Attributes"};
            const char* labels[][15] = {{"Account", "Last IP", "Kills", "Deaths", "Online Seconds", "Rating", "Rating Deviation"}, {"Head Image", "Body Image", "Animation", "Skin Color", "Coat Color", "Sleeves Color", "Shoes Color", "Belt Color"}, {"Level", "X", "Y", "Hearts", "Full Hearts", "AP", "MP", "Gralats", "Glove", "Bombs", "Arrows", "Sword Power", "Sword Image", "Shield Power", "Shield Image"}};
            const int counts[] = {7, 8, 15};
            for (int section = 0; section < 3; ++section) { text << '[' << sections[section] << "]\n"; for (int index = 0; index < counts[section]; ++index) text << labels[section][index] << ": " << gtk_entry_get_text(GTK_ENTRY(state->fields[labels[section][index]])) << "\n"; if (section == 2) text << "Male: " << (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->male)) ? "true" : "false") << "\nWeapons Enabled: " << (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->weapons)) ? "true" : "false") << "\nSpin Attack: " << (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->spin)) ? "true" : "false") << "\n"; text << '\n'; }
            const struct { const char* title; GtkWidget* field; } lists[] = {{"Chests", state->chests}, {"Weapons", state->weaponList}, {"Script Flags", state->flags}};
            for (const auto& list : lists) { GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(list.field)); GtkTextIter start; GtkTextIter end; gtk_text_buffer_get_bounds(buffer, &start, &end); gchar* content = gtk_text_buffer_get_text(buffer, &start, &end, false); text << '[' << list.title << "]\n" << (content == nullptr ? "" : content) << '\n'; g_free(content); }
            char* properties = rc_parse_player_attributes_text(text.str().c_str());
            if (properties != nullptr) {
                backupEditorText("attributes", state->account, text.str(), true);
                rc_set_player_attributes(state->list->connection, state->account.c_str(), properties);
                free(properties);
            }
        }
        gtk_widget_destroy(GTK_WIDGET(responseDialog));
    };
    g_signal_connect(dialog, "response", G_CALLBACK(onAttributeFormResponse), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer userData) { delete static_cast<AttributeState*>(userData); }), state);
    gtk_widget_show_all(dialog);
}
void TPlayerList::handlePlayerText(const char* type, const char* account, const char* content) {
    if (type == nullptr || account == nullptr) return;
    const std::string dataType(type);
    if (dataType != "comments" && dataType != "profile") return;
    backupEditorText(dataType, account, content == nullptr ? "" : content, false);
    if (dataType == "profile") {
        struct ProfileState { TPlayerList* list; std::string account; GtkWidget* fields[8]{}; GtkWidget* quote; };
        std::vector<std::string> values;
        std::istringstream input(content == nullptr ? "" : content);
        for (std::string value; std::getline(input, value);) values.push_back(value);
        while (values.size() < 11) values.emplace_back();
        GtkWidget* dialog = gtk_dialog_new_with_buttons(("Profile of " + std::string(account)).c_str(), GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, "Close", GTK_RESPONSE_CANCEL, "Apply", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_widget_set_name(dialog, "ProfileWindow");
        gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 400);
        GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), root);
        GtkWidget* nick = gtk_label_new((values[0] + ": " + (values.size() > 1 ? values[1] : "")).c_str());
        gtk_widget_set_halign(nick, GTK_ALIGN_START);
        gtk_widget_set_margin_start(nick, 10);
        gtk_widget_set_margin_top(nick, 6);
        gtk_box_pack_start(GTK_BOX(root), nick, false, false, 0);
        GtkWidget* split = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_box_pack_start(GTK_BOX(root), split, true, true, 0);
        GtkWidget* information = gtk_frame_new(" Player information ");
        gtk_container_set_border_width(GTK_CONTAINER(information), 5);
        GtkWidget* form = gtk_grid_new();
        gtk_container_set_border_width(GTK_CONTAINER(form), 5);
        gtk_grid_set_row_spacing(GTK_GRID(form), 3);
        gtk_grid_set_column_spacing(GTK_GRID(form), 6);
        gtk_container_add(GTK_CONTAINER(information), form);
        auto* state = new ProfileState{this, account};
        const char* labels[] = {"Real name", "Age", "Sex", "Country", "Messenger", "E-mail", "Homepage", "Fav. hangout"};
        for (int index = 0; index < 8; ++index) {
            GtkWidget* field = index == 2 ? gtk_combo_box_text_new_with_entry() : gtk_entry_new();
            if (index == 2) { gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(field), "unknown"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(field), "male"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(field), "female"); GtkWidget* entry = gtk_bin_get_child(GTK_BIN(field)); gtk_entry_set_text(GTK_ENTRY(entry), values[index + 1].c_str()); gtk_editable_set_editable(GTK_EDITABLE(entry), false); }
            else gtk_entry_set_text(GTK_ENTRY(field), values[index + 1].c_str());
            gtk_grid_attach(GTK_GRID(form), gtk_label_new((std::string(labels[index]) + ":").c_str()), 0, index, 1, 1);
            gtk_grid_attach(GTK_GRID(form), field, 1, index, 1, 1);
            state->fields[index] = field;
        }
        GtkWidget* quoteLabel = gtk_label_new("Favourite quote:");
        gtk_widget_set_halign(quoteLabel, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(form), quoteLabel, 0, 8, 2, 1);
        GtkWidget* quoteScroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(quoteScroll, -1, 80);
        state->quote = gtk_text_view_new();
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(state->quote), GTK_WRAP_WORD_CHAR);
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->quote)), values[9].c_str(), -1);
        gtk_container_add(GTK_CONTAINER(quoteScroll), state->quote);
        gtk_grid_attach(GTK_GRID(form), quoteScroll, 0, 9, 2, 1);
        gtk_box_pack_start(GTK_BOX(split), information, true, true, 0);
        GtkWidget* statsFrame = gtk_frame_new(" In-game stats ");
        gtk_container_set_border_width(GTK_CONTAINER(statsFrame), 5);
        GtkWidget* stats = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        gtk_container_set_border_width(GTK_CONTAINER(stats), 5);
        gtk_container_add(GTK_CONTAINER(statsFrame), stats);
        GtkWidget* level = gtk_entry_new();
        GtkWidget* online = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(level), values.size() > 11 ? values[11].c_str() : "");
        gtk_entry_set_text(GTK_ENTRY(online), values[10].c_str());
        gtk_editable_set_editable(GTK_EDITABLE(level), false);
        gtk_editable_set_editable(GTK_EDITABLE(online), false);
        gtk_box_pack_start(GTK_BOX(stats), gtk_label_new("Level:"), false, false, 0);
        gtk_box_pack_start(GTK_BOX(stats), level, false, false, 0);
        gtk_box_pack_start(GTK_BOX(stats), gtk_label_new("Online time:"), false, false, 0);
        gtk_box_pack_start(GTK_BOX(stats), online, false, false, 0);
        if (values.size() > 12) {
            GtkWidget* variables = gtk_frame_new(" Profile variables ");
            GtkWidget* variableScroll = gtk_scrolled_window_new(nullptr, nullptr);
            gtk_widget_set_size_request(variableScroll, 170, 110);
            GtkWidget* variableText = gtk_text_view_new();
            gtk_text_view_set_editable(GTK_TEXT_VIEW(variableText), false);
            gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(variableText), false);
            std::ostringstream profileVariables;
            for (std::size_t index = 12; index < values.size(); ++index) profileVariables << values[index] << '\n';
            gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(variableText)), profileVariables.str().c_str(), -1);
            gtk_container_add(GTK_CONTAINER(variableScroll), variableText);
            gtk_container_add(GTK_CONTAINER(variables), variableScroll);
            gtk_box_pack_start(GTK_BOX(stats), variables, false, false, 4);
        }
        gtk_box_pack_start(GTK_BOX(split), statsFrame, false, false, 0);
        auto onProfileResponse = +[](GtkDialog* responseDialog, gint response, gpointer userData) {
            auto* state = static_cast<ProfileState*>(userData);
            if (response == GTK_RESPONSE_ACCEPT) {
                std::ostringstream profile;
                const char* fields[] = {"Real Name", "Age", "Sex", "Country", "Messenger", "E-Mail", "Homepage", "Fav. Hangout"};
                for (int index = 0; index < 8; ++index) {
                    const char* value = index == 2 ? gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(state->fields[index])))) : gtk_entry_get_text(GTK_ENTRY(state->fields[index]));
                    profile << fields[index] << ": " << value << '\n';
                }
                GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->quote));
                GtkTextIter start; GtkTextIter end;
                gtk_text_buffer_get_bounds(buffer, &start, &end);
                gchar* quote = gtk_text_buffer_get_text(buffer, &start, &end, false);
                profile << "Favourite Quote: " << (quote == nullptr ? "" : quote);
                g_free(quote);
                backupEditorText("profile", state->account, profile.str(), true);
                rc_set_player_profile(state->list->connection, state->account.c_str(), profile.str().c_str());
            }
            gtk_widget_destroy(GTK_WIDGET(responseDialog));
        };
        g_signal_connect(dialog, "response", G_CALLBACK(onProfileResponse), state);
        g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer userData) { delete static_cast<ProfileState*>(userData); }), state);
        gtk_widget_show_all(dialog);
        return;
    }
    struct TextState { TPlayerList* list; std::string account; std::string type; GtkWidget* text; };
    const std::string title = (dataType == "profile" ? "Profile of " : "Edit Comments of ") + std::string(account);
    GtkWidget* dialog = gtk_dialog_new_with_buttons(title.c_str(), GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 280);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkWidget* text = gtk_text_view_new();
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text)), content == nullptr ? "" : content, -1);
    gtk_container_add(GTK_CONTAINER(scrolled), text);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scrolled, true, true, 0);
    auto* state = new TextState{this, account, dataType, text};
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* responseDialog, gint response, gpointer userData) { auto* state = static_cast<TextState*>(userData); if (response == GTK_RESPONSE_ACCEPT) { GtkTextIter start; GtkTextIter end; GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->text)); gtk_text_buffer_get_bounds(buffer, &start, &end); gchar* value = gtk_text_buffer_get_text(buffer, &start, &end, false); backupEditorText(state->type, state->account, value == nullptr ? "" : value, true); if (state->type == "profile") rc_set_player_profile(state->list->connection, state->account.c_str(), value == nullptr ? "" : value); else rc_set_player_comments(state->list->connection, state->account.c_str(), value == nullptr ? "" : value); g_free(value); } else gtk_widget_destroy(GTK_WIDGET(responseDialog)); }), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer userData) { delete static_cast<TextState*>(userData); }), state);
    gtk_widget_show_all(dialog);
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
                std::istringstream fields(player);
                std::string idText;
                std::string account;
                std::string nick;
                std::string level;
                std::getline(fields, idText, '\t');
                std::getline(fields, account, '\t');
                std::getline(fields, nick, '\t');
                std::getline(fields, level, '\t');
                int playerId = 0;
                try { playerId = std::stoi(idText); } catch (...) { nick = player; }
                if (nick.empty()) nick = account;
                gtk_tree_store_append(list->serverStore, &child, &row);
                gtk_tree_store_set(list->serverStore, &child, 0, list->onlineIcon, 1, nick.c_str(), 2, account.c_str(), 3, true, 5, playerId, -1);
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
    if (event->type != GDK_BUTTON_PRESS && event->type != GDK_2BUTTON_PRESS) return false;
    GtkTreePath* path = nullptr;
    if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), static_cast<gint>(event->x), static_cast<gint>(event->y), &path, nullptr, nullptr, nullptr)) return false;
    TPlayerList* list = static_cast<TPlayerList*>(data);
    const int depth = gtk_tree_path_get_depth(path);
    if (depth == 1 && event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_PRIMARY) onServerActivated(GTK_TREE_VIEW(widget), path, nullptr, data);
    else if (depth == 2 && event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_PRIMARY) {
        gtk_tree_path_free(path);
        return false;
    }
    else if (depth == 2 && event->type == GDK_2BUTTON_PRESS && event->button == GDK_BUTTON_PRIMARY) {
        GtkTreeIter row;
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(list->serverStore), &row, path)) {
            int playerId = 0;
            gchar* account = nullptr;
            gchar* nick = nullptr;
            gtk_tree_model_get(GTK_TREE_MODEL(list->serverStore), &row, 5, &playerId, 2, &account, 1, &nick, -1);
            list->openPrivateMessage(playerId, account == nullptr ? "" : account, nick == nullptr ? "" : nick);
            g_free(account);
            g_free(nick);
        }
    } else if (depth == 2 && event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_SECONDARY) {
        GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
        gtk_tree_selection_unselect_all(selection);
        gtk_tree_selection_select_path(selection, path);
        GtkWidget* menu = gtk_menu_new();
        GtkWidget* privateMessage = gtk_menu_item_new_with_label("Private Message");
        GtkWidget* history = gtk_menu_item_new_with_label("History");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), privateMessage);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), history);
        g_signal_connect(privateMessage, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer userData) {
            TPlayerList* remoteList = static_cast<TPlayerList*>(userData);
            GtkTreeModel* model = nullptr;
            GtkTreeIter row;
            if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(remoteList->serverTree)), &model, &row)) return;
            int playerId = 0;
            gchar* account = nullptr;
            gchar* nick = nullptr;
            gtk_tree_model_get(model, &row, 5, &playerId, 2, &account, 1, &nick, -1);
            remoteList->openPrivateMessage(playerId, account == nullptr ? "" : account, nick == nullptr ? "" : nick);
            g_free(account);
            g_free(nick);
        }), list);
        g_signal_connect(history, "activate", G_CALLBACK((+[](GtkMenuItem*, gpointer userData) {
            TPlayerList* remoteList = static_cast<TPlayerList*>(userData);
            GtkTreeModel* model = nullptr;
            GtkTreeIter row;
            if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(remoteList->serverTree)), &model, &row)) return;
            gchar* account = nullptr;
            gchar* nick = nullptr;
            gtk_tree_model_get(model, &row, 2, &account, 1, &nick, -1);
            if (account != nullptr && *account != '\0') {
                PMWindowData historyData{remoteList->connection, remoteList->applicationDirectory / "PMs", nullptr, nullptr, 0, account, nick == nullptr ? "" : nick};
                onPMHistory(nullptr, &historyData);
            }
            g_free(account);
            g_free(nick);
        })), list);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
    }
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
        rc_unmap_pm_server(list->connection, serverName);
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

void TPlayerList::notePrivateMessage(int playerId, const char* account, const char* nick, const char* message, const char* type) {
    if (playerId < 0) return;
    appendHistory(account, "Opposite", message);
    pmPlayers[playerId] = {account == nullptr ? "" : account, nick == nullptr ? "" : nick};
    latestPMPlayerId = playerId;
    pmTypes[playerId] = type == nullptr ? "normal" : type;
    pmIconsVisible = true;
    if (pmBlinkSource == 0) pmBlinkSource = g_timeout_add(500, onPMBlink, this);
    updatePMIcons();
}

bool TPlayerList::openLatestPrivateMessage() {
    const auto player = pmPlayers.find(latestPMPlayerId);
    if (player == pmPlayers.end() || player->second.first.empty()) return false;
    openPrivateMessage(latestPMPlayerId, player->second.first.c_str(), player->second.second.c_str());
    pmTypes.erase(latestPMPlayerId);
    pmPlayers.erase(player);
    latestPMPlayerId = pmPlayers.empty() ? 0 : pmPlayers.rbegin()->first;
    pmIconsVisible = true;
    updatePMIcons();
    return true;
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
    gtk_widget_set_name(data->window, "PrivateMessage");
    gtk_window_set_title(GTK_WINDOW(data->window), "PM");
    gtk_window_set_default_size(GTK_WINDOW(data->window), 380, 300);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(data->window), root);
    GtkWidget* label = gtk_label_new((data->account + ": " + data->nick).c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_box_pack_start(GTK_BOX(root), label, false, false, 5);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
    data->reply = gtk_text_view_new();
    gtk_widget_set_name(data->reply, "PrivateMessageText");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(data->reply), GTK_WRAP_WORD_CHAR);
    GtkCssProvider* pmProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(pmProvider, "#PrivateMessage, #PrivateMessage box, #PrivateMessage scrolledwindow, #PrivateMessage viewport { background-color: #454545; color: #d4d4d4; } #PrivateMessageText, #PrivateMessageText text { background-color: #252525; color: #f0f0f0; caret-color: #00ff00; }", -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(data->window), GTK_STYLE_PROVIDER(pmProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    gtk_style_context_add_provider(gtk_widget_get_style_context(data->reply), GTK_STYLE_PROVIDER(pmProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(pmProvider);
    gtk_container_add(GTK_CONTAINER(scrolled), data->reply);
    gtk_box_pack_start(GTK_BOX(root), scrolled, true, true, 0);
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
    gtk_widget_grab_focus(data->reply);
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
                std::istringstream fields(player);
                std::string idText;
                std::string account;
                std::string nick;
                std::string level;
                std::getline(fields, idText, '\t');
                std::getline(fields, account, '\t');
                std::getline(fields, nick, '\t');
                std::getline(fields, level, '\t');
                int playerId = 0;
                try { playerId = std::stoi(idText); } catch (...) { nick = player; }
                if (nick.empty()) nick = account;
                GtkTreeIter child;
                gtk_tree_store_append(serverStore, &child, &row);
                gtk_tree_store_set(serverStore, &child, 0, onlineIcon, 1, nick.c_str(), 2, account.c_str(), 3, true, 5, playerId, -1);
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
    if (account != nullptr && *account != '\0' && playerId != 0) { rc_request_ban_types(connection); rc_request_player_ban(connection, account, playerId); }
    g_free(account);
}
void TPlayerList::editRights() {
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) return;
    gchar* account = nullptr;
    gtk_tree_model_get(model, &row, PlayerAccountColumn, &account, -1);
    if (account != nullptr && *account != '\0') rc_request_player_rights(connection, account);
    g_free(account);
}
void TPlayerList::editAttributes() {
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) return;
    gchar* account = nullptr;
    gtk_tree_model_get(model, &row, PlayerAccountColumn, &account, -1);
    if (account != nullptr && *account != '\0') rc_request_player_attrs(connection, account);
    g_free(account);
}
void TPlayerList::editComments() {
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) return;
    gchar* account = nullptr;
    gtk_tree_model_get(model, &row, PlayerAccountColumn, &account, -1);
    if (account != nullptr && *account != '\0') rc_request_player_comments(connection, account);
    g_free(account);
}
void TPlayerList::editProfile() {
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) return;
    gchar* account = nullptr;
    gtk_tree_model_get(model, &row, PlayerAccountColumn, &account, -1);
    if (account != nullptr && *account != '\0') rc_request_player_profile(connection, account);
    g_free(account);
}
void TPlayerList::editAccount() {
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) return;
    gchar* account = nullptr;
    gtk_tree_model_get(model, &row, PlayerAccountColumn, &account, -1);
    if (account != nullptr && *account != '\0') rc_request_player_account(connection, account);
    g_free(account);
}

void TPlayerList::openSelectedPrivateMessage() {
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) return;
    int playerId = 0;
    gchar* account = nullptr;
    gchar* nick = nullptr;
    gtk_tree_model_get(model, &row, PlayerIdColumn, &playerId, PlayerAccountColumn, &account, PlayerNickColumn, &nick, -1);
    openPrivateMessage(playerId, account == nullptr ? "" : account, nick == nullptr ? "" : nick);
    g_free(account);
    g_free(nick);
}

void TPlayerList::openSelectedHistory() {
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) return;
    gchar* account = nullptr;
    gchar* nick = nullptr;
    gtk_tree_model_get(model, &row, PlayerAccountColumn, &account, PlayerNickColumn, &nick, -1);
    if (account != nullptr && *account != '\0') {
        PMWindowData data{connection, applicationDirectory / "PMs", nullptr, nullptr, 0, account, nick == nullptr ? "" : nick};
        onPMHistory(nullptr, &data);
    }
    g_free(account);
    g_free(nick);
}

void TPlayerList::disconnectSelectedPlayer() {
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) return;
    int playerId = 0;
    gtk_tree_model_get(model, &row, PlayerIdColumn, &playerId, -1);
    std::string reason;
    if (playerId != 0 && getMessage(GTK_WINDOW(window), "Disconnect Player", "Reason:", reason, "Disconnect")) rc_disconnect_player(connection, playerId, reason.c_str());
}

void TPlayerList::resetSelectedPlayer() {
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) return;
    gchar* account = nullptr;
    gtk_tree_model_get(model, &row, PlayerAccountColumn, &account, -1);
    if (account != nullptr && *account != '\0') rc_reset_player(connection, account);
    g_free(account);
}
void TPlayerList::updateSelectedPlayerLevel() {
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) return;
    gchar* level = nullptr;
    gtk_tree_model_get(model, &row, PlayerLevelColumn, &level, -1);
    if (level != nullptr && *level != '\0') rc_update_level(connection, level);
    g_free(level);
}
void TPlayerList::warpSelectedPlayer() {
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) return;
    int playerId = 0;
    gtk_tree_model_get(model, &row, PlayerIdColumn, &playerId, -1);
    if (playerId == 0) return;
    struct WarpState { TPlayerList* list; int playerId; GtkWidget* level; GtkWidget* x; GtkWidget* y; };
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Warp Player", GTK_WINDOW(window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Warp", GTK_RESPONSE_ACCEPT, nullptr);
    GtkWidget* grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    GtkWidget* level = gtk_entry_new();
    GtkWidget* x = gtk_entry_new();
    GtkWidget* y = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(x), "0");
    gtk_entry_set_text(GTK_ENTRY(y), "0");
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Level:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), level, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("X:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), x, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Y:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), y, 1, 2, 1, 1);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid);
    auto* state = new WarpState{this, playerId, level, x, y};
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* responseDialog, gint response, gpointer userData) {
        auto* state = static_cast<WarpState*>(userData);
        if (response == GTK_RESPONSE_ACCEPT) rc_warp_player(state->list->connection, state->playerId, gtk_entry_get_text(GTK_ENTRY(state->level)), std::strtof(gtk_entry_get_text(GTK_ENTRY(state->x)), nullptr), std::strtof(gtk_entry_get_text(GTK_ENTRY(state->y)), nullptr));
        gtk_widget_destroy(GTK_WIDGET(responseDialog));
    }), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer userData) { delete static_cast<WarpState*>(userData); }), state);
    gtk_widget_show_all(dialog);
}
void TPlayerList::adminMessageSelectedPlayer() {
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &row)) return;
    int playerId = 0;
    gtk_tree_model_get(model, &row, PlayerIdColumn, &playerId, -1);
    std::string message;
    if (playerId != 0 && getMessage(GTK_WINDOW(window), "Admin Message", "Message:", message)) rc_send_admin_message(connection, playerId, message.c_str());
}
