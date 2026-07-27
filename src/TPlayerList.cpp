#include "TPlayerList.h"
#include "TButtonIcons.h"
#include "TBackup.h"
#include "TEditorFind.h"
#include "TGScriptEditor.h"
#include "TLocalBanWindow.h"
#include "TTheme.h"

#include <grclib.h>
#include <IEnums.h>

#include <algorithm>
#include <string>
#include <cstdlib>
#include <vector>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <map>
#include <cctype>

#include <gtksourceview/gtksource.h>

extern void remote_control_clear_pm_tray_alert();

namespace {
    constexpr int PlayerIconColumn = 0;
    constexpr int PlayerNickColumn = 1;
    constexpr int PlayerAccountColumn = 2;
    constexpr int PlayerLevelColumn = 3;
    constexpr int PlayerIdColumn = 4;
    constexpr int PlayerOrderColumn = 5;

    gint comparePlayerIconColumn(GtkTreeModel* model, GtkTreeIter* left, GtkTreeIter* right, gpointer) {
        gchar* leftNick = nullptr;
        gchar* rightNick = nullptr;
        gint leftOrder = 0;
        gint rightOrder = 0;
        gtk_tree_model_get(model, left, PlayerNickColumn, &leftNick, PlayerOrderColumn, &leftOrder, -1);
        gtk_tree_model_get(model, right, PlayerNickColumn, &rightNick, PlayerOrderColumn, &rightOrder, -1);
        const std::string leftValue = leftNick == nullptr ? "" : leftNick;
        const std::string rightValue = rightNick == nullptr ? "" : rightNick;
        g_free(leftNick);
        g_free(rightNick);
        if (leftValue == "Admins" || rightValue == "Admins") return leftValue == rightValue ? 0 : (leftValue == "Admins" ? -1 : 1);
        if (leftValue == "Players" || rightValue == "Players") return leftValue == rightValue ? 0 : (leftValue == "Players" ? 1 : -1);
        return leftOrder == rightOrder ? 0 : (leftOrder < rightOrder ? -1 : 1);
    }

    struct PMWindowData {
        void* connection;
        std::filesystem::path historyDirectory;
        GtkWidget* window;
        GtkWidget* received;
        GtkWidget* reply;
        int playerId;
        std::string account;
        std::string nick;
        std::string localAccount;
        std::string message;
    };

    struct PMPlayerIdentity { int id; std::string account; std::string nick; };

    gboolean findServerPlayerById(GtkTreeModel* model, GtkTreePath*, GtkTreeIter* row, gpointer data) {
        PMPlayerIdentity* identity = static_cast<PMPlayerIdentity*>(data);
        int playerId = 0;
        gchar* account = nullptr;
        gchar* nick = nullptr;
        gtk_tree_model_get(model, row, 5, &playerId, 2, &account, 1, &nick, -1);
        if (playerId == identity->id) {
            identity->account = account == nullptr ? "" : account;
            identity->nick = nick == nullptr ? "" : nick;
            g_free(account);
            g_free(nick);
            return true;
        }
        g_free(account);
        g_free(nick);
        return false;
    }

    gboolean onPMWindowDelete(GtkWidget*, GdkEvent*, gpointer) { return false; }
    void onPMWindowDestroy(GtkWidget*, gpointer data) { delete static_cast<PMWindowData*>(data); }

    std::string formatPMCommaText(const std::string& value) {
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            std::string unquoted = value.substr(1, value.size() - 2);
            for (size_t index = 0; (index = unquoted.find("\"\"", index)) != std::string::npos; ++index) unquoted.replace(index, 2, "\"");
            return unquoted;
        }
        if (value.find(',') == std::string::npos) return value;
        std::vector<std::string> fields;
        std::string field;
        bool quoted = false;
        for (size_t index = 0; index < value.size(); ++index) {
            const char character = value[index];
            if (character == '"') {
                if (quoted && index + 1 < value.size() && value[index + 1] == '"') { field += character; ++index; }
                else quoted = !quoted;
            } else if (character == ',' && !quoted) {
                while (!field.empty() && field.front() == ' ') field.erase(field.begin());
                while (!field.empty() && field.back() == ' ') field.pop_back();
                fields.push_back(field);
                field.clear();
            } else field += character;
        }
        while (!field.empty() && field.front() == ' ') field.erase(field.begin());
        while (!field.empty() && field.back() == ' ') field.pop_back();
        fields.push_back(field);
        if (fields.size() < 2) return value;
        std::ostringstream output;
        for (size_t index = 0; index < fields.size(); ++index) {
            if (index != 0) output << '\n';
            output << fields[index];
        }
        return output.str();
    }

    std::string pmHistoryTimestamp() {
        const std::time_t now = std::time(nullptr);
        std::tm localTime{};
#ifdef _WIN32
        localtime_s(&localTime, &now);
#else
        localtime_r(&now, &localTime);
#endif
        std::ostringstream output;
        output << std::put_time(&localTime, "%a %b %d %H:%M:%S %Y");
        return output.str();
    }

    void writePMHistory(const std::filesystem::path& directory, const std::string& account, const std::string& sender, const char* message) {
        if (account.empty() || sender.empty() || message == nullptr || *message == '\0') return;
        std::filesystem::create_directories(directory);
        std::ofstream output(directory / (account + ".txt"), std::ios::app | std::ios::binary);
        output << sender << " (" << pmHistoryTimestamp() << "):\n" << formatPMCommaText(message) << "\n\n";
    }

    void onPMSend(GtkButton*, gpointer data) {
        PMWindowData* windowData = static_cast<PMWindowData*>(data);
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(windowData->reply));
        GtkTextIter start;
        GtkTextIter end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, false);
        if (text == nullptr || *text == '\0') { g_free(text); return; }
        if (rc_send_private_message(windowData->connection, windowData->playerId, text) == 0) { g_free(text); return; }
        writePMHistory(windowData->historyDirectory, windowData->account, windowData->localAccount.empty() ? "You" : windowData->localAccount, text);
        g_free(text);
        gtk_widget_destroy(windowData->window);
    }

    void onPMHistory(GtkButton*, gpointer data) {
        PMWindowData* windowData = static_cast<PMWindowData*>(data);
        GtkWidget* history = gtk_dialog_new_with_buttons(("History: " + windowData->account + " - " + windowData->nick).c_str(), GTK_WINDOW(windowData->window), GTK_DIALOG_DESTROY_WITH_PARENT, "Close", GTK_RESPONSE_CLOSE, nullptr);
        gtk_widget_set_name(history, "PrivateMessageHistory");
        gtk_window_set_default_size(GTK_WINDOW(history), 540, 475);
        GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
        GtkSourceBuffer* sourceBuffer = gtk_source_buffer_new(nullptr);
        applyRemoteControlSourceStyle(sourceBuffer);
        GtkWidget* field = gtk_source_view_new_with_buffer(sourceBuffer);
        configureGScriptEditor(field, false);
        gtk_widget_set_name(field, "PrivateMessageHistoryText");
        gtk_text_view_set_editable(GTK_TEXT_VIEW(field), false);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(field), false);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(field), true);
        gtk_container_add(GTK_CONTAINER(scrolled), field);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(history))), scrolled, true, true, 0);
        GtkWidget* find = gtk_button_new_with_label("Find");
        applyGtkButtonIcon(find, GTK_STOCK_FIND);
        gtk_widget_set_tooltip_text(find, "Find (Ctrl+F)");
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_action_area(GTK_DIALOG(history))), find);
        g_signal_connect(find, "clicked", G_CALLBACK(editorFind), field);
        addEditorFindShortcut(field);
        addGScriptEditorLineStatus(GTK_DIALOG(history), field);
        const std::filesystem::path path = windowData->historyDirectory / (windowData->account + ".txt");
        std::ifstream input(path, std::ios::binary);
        std::ostringstream content;
        content << input.rdbuf();
        gchar* validContent = g_utf8_make_valid(content.str().c_str(), -1);
        gtk_text_buffer_set_text(GTK_TEXT_BUFFER(sourceBuffer), validContent, -1);
        GtkTextTag* headerTag = gtk_text_buffer_create_tag(GTK_TEXT_BUFFER(sourceBuffer), "pm-history-header", "foreground", remoteControlDarkMode() ? "#ff00ff" : "#a000a0", "weight", PANGO_WEIGHT_BOLD, nullptr);
        GtkTextIter lineStart;
        gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(sourceBuffer), &lineStart);
        do {
            GtkTextIter lineEnd = lineStart;
            gtk_text_iter_forward_to_line_end(&lineEnd);
            gchar* line = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(sourceBuffer), &lineStart, &lineEnd, false);
            const bool modernHeader = line != nullptr && std::string(line).find(" (") != std::string::npos && g_str_has_suffix(line, "):");
            const bool legacyHeader = line != nullptr && (g_strcmp0(line, "You:") == 0 || g_strcmp0(line, "Opposite:") == 0);
            if (modernHeader || legacyHeader) gtk_text_buffer_apply_tag(GTK_TEXT_BUFFER(sourceBuffer), headerTag, &lineStart, &lineEnd);
            g_free(line);
        } while (gtk_text_iter_forward_line(&lineStart));
        g_free(validContent);
        g_object_unref(sourceBuffer);
        g_signal_connect(history, "response", G_CALLBACK(+[](GtkDialog* dialog, gint, gpointer) { gtk_widget_destroy(GTK_WIDGET(dialog)); }), nullptr);
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

TPlayerList::TPlayerList(const std::filesystem::path& nextApplicationDirectory, std::string nextAccountName) : applicationDirectory(nextApplicationDirectory), accountName(std::move(nextAccountName)) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "PlayerList");
    gtk_window_set_title(GTK_WINDOW(window), "Players");
    gtk_window_set_default_size(GTK_WINDOW(window), 580, 420);
    g_signal_connect(window, "focus-in-event", G_CALLBACK(+[](GtkWidget*, GdkEventFocus*, gpointer data) -> gboolean { static_cast<TPlayerList*>(data)->clearPrivateMessageAlert(); return false; }), this);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* notebook = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(notebook), 5);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_set_border_width(GTK_CONTAINER(scrolled), 5);
    store = gtk_tree_store_new(6, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
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
    loadStatusIcons();
    GtkCellRenderer* imageRenderer = gtk_cell_renderer_pixbuf_new();
    GtkTreeViewColumn* imageColumn = gtk_tree_view_column_new_with_attributes("", imageRenderer, "pixbuf", PlayerIconColumn, nullptr);
    gtk_tree_view_column_set_sizing(imageColumn, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(imageColumn, 24);
    gtk_tree_view_column_set_alignment(imageColumn, 0.0F);
    gtk_tree_view_column_set_sort_column_id(imageColumn, PlayerIconColumn);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), imageColumn);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), PlayerIconColumn, comparePlayerIconColumn, nullptr, nullptr);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), PlayerIconColumn, GTK_SORT_ASCENDING);
    g_signal_connect(store, "sort-column-changed", G_CALLBACK(+[](GtkTreeSortable* sortable, gpointer) {
        gint column = GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID;
        GtkSortType order = GTK_SORT_ASCENDING;
        if (gtk_tree_sortable_get_sort_column_id(sortable, &column, &order) && column == PlayerIconColumn && order != GTK_SORT_ASCENDING)
            gtk_tree_sortable_set_sort_column_id(sortable, PlayerIconColumn, GTK_SORT_ASCENDING);
    }), nullptr);
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
    statusCombo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(statusCombo), "Online");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(statusCombo), "Away");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(statusCombo), "Offline");
    gtk_combo_box_set_active(GTK_COMBO_BOX(statusCombo), 0);
    gtk_box_pack_start(GTK_BOX(bottom), statusCombo, false, false, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* refreshButton = gtk_button_new_with_label("Refresh");
    GtkWidget* massPMButton = gtk_button_new_with_label("Mass PM");
    GtkWidget* adminMessageButton = gtk_button_new_with_label("Admin Message");
        GtkWidget* closeButton = gtk_button_new_with_label("Close");
        applyGtkButtonIcon(refreshButton, GTK_STOCK_REFRESH);
        applyGtkButtonIcon(massPMButton, GTK_STOCK_JUMP_TO);
        applyGtkButtonIcon(adminMessageButton, GTK_STOCK_DIALOG_WARNING);
        applyGtkButtonIcon(closeButton, GTK_STOCK_CLOSE);
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
    g_signal_connect(statusCombo, "changed", G_CALLBACK(onStatusChanged), this);
    g_signal_connect(closeButton, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TPlayerList::~TPlayerList() { if (pmBlinkSource != 0) g_source_remove(pmBlinkSource); delete localBanWindow; for (GdkPixbuf* icon : statusIcons) if (icon != nullptr) g_object_unref(icon); if (onlineIcon != nullptr) g_object_unref(onlineIcon); if (channelIcon != nullptr) g_object_unref(channelIcon); if (channelClosedIcon != nullptr) g_object_unref(channelClosedIcon); if (pmNormalIcon != nullptr) g_object_unref(pmNormalIcon); if (pmGuildIcon != nullptr) g_object_unref(pmGuildIcon); if (pmAdminIcon != nullptr) g_object_unref(pmAdminIcon); if (pmMassIcon != nullptr) g_object_unref(pmMassIcon); if (window != nullptr) gtk_widget_destroy(window); if (store != nullptr) g_object_unref(store); if (guildStore != nullptr) g_object_unref(guildStore); if (serverStore != nullptr) g_object_unref(serverStore); if (channelStore != nullptr) g_object_unref(channelStore); }
void TPlayerList::open(void* nextConnection) { setConnection(nextConnection); rc_on_pm_servers_updated(connection, onPMServers, this); rc_on_pm_guilds_updated(connection, onPMGuilds, this); rc_on_pm_server_players(connection, onPMServerPlayers, this); refresh(); gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); }
void TPlayerList::setConnection(void* nextConnection) { connection = nextConnection; }
void TPlayerList::setStatusList(const char* statuses) {
    statusNames.clear();
    std::stringstream stream(statuses == nullptr ? "" : statuses);
    std::string status;
    while (std::getline(stream, status, ',')) {
        while (!status.empty() && std::isspace(static_cast<unsigned char>(status.front()))) status.erase(status.begin());
        while (!status.empty() && std::isspace(static_cast<unsigned char>(status.back()))) status.pop_back();
        if (!status.empty()) statusNames.push_back(status);
    }
    loadStatusIcons();
    if (window != nullptr && store != nullptr) refresh();
}
void TPlayerList::setAttachAway(bool enabled) { if (statusCombo != nullptr) gtk_combo_box_set_active(GTK_COMBO_BOX(statusCombo), enabled && !gtk_widget_get_visible(window) ? 1 : 0); }
void TPlayerList::setAwayStatus(bool away) { if (statusCombo == nullptr) return; const int active = away ? 1 : 0; if (gtk_combo_box_get_active(GTK_COMBO_BOX(statusCombo)) != active) gtk_combo_box_set_active(GTK_COMBO_BOX(statusCombo), active); else sendAttachAway(); }
void TPlayerList::onRefresh(GtkButton*, gpointer data) { static_cast<TPlayerList*>(data)->refresh(); }
void TPlayerList::onMassPM(GtkButton*, gpointer data) { static_cast<TPlayerList*>(data)->sendMassPM(); }
void TPlayerList::onAdminMessage(GtkButton*, gpointer data) { static_cast<TPlayerList*>(data)->sendAdminMessage(); }
void TPlayerList::onStatusChanged(GtkComboBox*, gpointer data) { static_cast<TPlayerList*>(data)->sendAttachAway(); }
void TPlayerList::onClose(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TPlayerList*>(data)->window); }
void TPlayerList::sendAttachAway() {
    if (connection == nullptr || statusCombo == nullptr) return;
    const int status = std::clamp(gtk_combo_box_get_active(GTK_COMBO_BOX(statusCombo)), 0, 223);
    const char payload[] = {'U', static_cast<char>(status + 32)};
    rc_send_raw_packet(connection, PLI_PLAYERPROPS, payload, sizeof(payload));
}
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
    g_signal_connect(menu, "deactivate", G_CALLBACK(+[](GtkWidget* menuWidget, gpointer) {
        g_object_ref(menuWidget);
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, +[](gpointer menuData) {
            gtk_widget_destroy(GTK_WIDGET(menuData));
            g_object_unref(menuData);
            return G_SOURCE_REMOVE;
        }, menuWidget, nullptr);
    }), nullptr);
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
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(field));
    const std::string history = content == nullptr ? "" : content;
    if (history.empty()) {
        gtk_text_buffer_set_text(buffer, "(none)", -1);
    } else if (listType != "banhistory") {
        gtk_text_buffer_set_text(buffer, history.c_str(), -1);
    } else {
        GtkTextTag* headerTag = gtk_text_buffer_create_tag(buffer, "ban-history-header", "foreground", remoteControlDarkMode() ? "#ff00ff" : "#a000a0", "weight", PANGO_WEIGHT_BOLD, nullptr);
        const auto isDigit = [](char value) { return value >= '0' && value <= '9'; };
        bool hasEntry = false;
        size_t lineStart = 0;
        while (lineStart <= history.size()) {
            const size_t lineEnd = history.find('\n', lineStart);
            std::string line = history.substr(lineStart, lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const bool header = line.size() >= 20 && isDigit(line[0]) && isDigit(line[1]) && isDigit(line[2]) && isDigit(line[3]) && line[4] == '-' && isDigit(line[5]) && isDigit(line[6]) && line[7] == '-' && isDigit(line[8]) && isDigit(line[9]) && line[10] == ' ' && isDigit(line[11]) && isDigit(line[12]) && line[13] == ':' && isDigit(line[14]) && isDigit(line[15]) && line[16] == ':' && isDigit(line[17]) && isDigit(line[18]) && line.back() == ':';
            if (!line.empty()) {
                if (header && hasEntry) gtk_text_buffer_insert_at_cursor(buffer, "\n", -1);
                if (header) {
                    GtkTextIter end;
                    gtk_text_buffer_get_end_iter(buffer, &end);
                    gtk_text_buffer_insert_with_tags(buffer, &end, line.c_str(), -1, headerTag, nullptr);
                    hasEntry = true;
                } else {
                    gtk_text_buffer_insert_at_cursor(buffer, line.c_str(), -1);
                }
                gtk_text_buffer_insert_at_cursor(buffer, "\n", -1);
            }
            if (lineEnd == std::string::npos) break;
            lineStart = lineEnd + 1;
        }
    }
    gtk_container_add(GTK_CONTAINER(scrolled), field);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scrolled, true, true, 0);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* responseDialog, gint, gpointer) { gtk_widget_destroy(GTK_WIDGET(responseDialog)); }), nullptr);
    gtk_widget_show_all(dialog);
}
void TPlayerList::handlePlayerRights(const char* account, int rights, const char* ipRange, const char* folderAccess) {
    if (account == nullptr || *account == '\0') return;
    struct RightsState { TPlayerList* list; std::string account; GtkWidget* ipRange; GtkWidget* folderAccess; GtkWidget* checks[20]{}; };
    GtkWidget* dialog = gtk_dialog_new_with_buttons(("Edit Rights of " + std::string(account)).c_str(), GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, "Close", GTK_RESPONSE_CANCEL, "Apply", GTK_RESPONSE_ACCEPT, nullptr);
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
    gtk_grid_set_column_spacing(GTK_GRID(grid), 26);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
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
    gtk_box_pack_start(GTK_BOX(flags), grid, false, false, 0);
    GtkWidget* presets = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget* clearAll = gtk_button_new_with_label("Clear all");
    g_signal_connect(clearAll, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { auto* state = static_cast<RightsState*>(data); for (GtkWidget* check : state->checks) if (check != nullptr) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), false); }), state);
    gtk_box_pack_start(GTK_BOX(presets), clearAll, false, false, 0);
    const int presetRights[] = {
        (1 << 0) | (1 << 1) | (1 << 2),
        (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7),
        (1 << 15) - 1,
        (1 << 20) - 1
    };
    for (int index = 0; index < 4; ++index) {
        GtkWidget* preset = gtk_button_new_with_label(std::to_string(index + 1).c_str());
        g_signal_connect(preset, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
            auto* state = static_cast<RightsState*>(data);
            const int value = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "rights-preset"));
            for (int bit = 0; bit < 20; ++bit) if (state->checks[bit] != nullptr) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->checks[bit]), (value & (1 << bit)) != 0);
        }), state);
        g_object_set_data(G_OBJECT(preset), "rights-preset", GINT_TO_POINTER(presetRights[index]));
        gtk_box_pack_start(GTK_BOX(presets), preset, false, false, 0);
    }
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
    GtkWidget* actionArea = gtk_dialog_get_action_area(GTK_DIALOG(dialog));
    gtk_widget_set_hexpand(presets, true);
    gtk_box_pack_start(GTK_BOX(actionArea), presets, true, true, 0);
    gtk_box_reorder_child(GTK_BOX(actionArea), presets, 0);
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
    struct AttributeState { TPlayerList* list; std::string account; std::map<std::string, GtkWidget*> fields; GtkWidget* male; GtkWidget* weapons; GtkWidget* spin; GtkWidget* chests; GtkListStore* chestStore; GtkWidget* weaponList; GtkWidget* flags; };
    auto fieldValues = [](const char* source) {
        std::map<std::string, std::string> values;
        std::istringstream input(source == nullptr ? "" : source);
        for (std::string line; std::getline(input, line);) { const size_t colon = line.find(':'); if (colon != std::string::npos) values[line.substr(0, colon)] = line.substr(colon + 1); }
        return values;
    };
    const std::map<std::string, std::string> values = fieldValues(editorText);
    GtkWidget* dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(dialog), ("Edit Attributes of " + std::string(account)).c_str());
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), true);
    gtk_widget_set_name(dialog, "EditAttributesWindow");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 360);
    GdkGeometry attributeGeometry{};
    attributeGeometry.min_width = 400;
    attributeGeometry.max_width = 400;
    gtk_window_set_geometry_hints(GTK_WINDOW(dialog), nullptr, &attributeGeometry, static_cast<GdkWindowHints>(GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE));
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(dialog), root);
    GtkWidget* notebook = gtk_notebook_new();
    gtk_widget_set_name(notebook, "AttributeNotebook");
    gtk_container_set_border_width(GTK_CONTAINER(notebook), 5);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), true);
    GtkCssProvider* tabProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(tabProvider, "#EditAttributesWindow notebook > header > tabs > tab { min-height: 0; margin: 0; padding: 3px 5px; } #EditAttributesWindow notebook > header > tabs > tab label { margin: 0; padding: 0; font-size: 12px; }", -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(tabProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(tabProvider);
    auto* state = new AttributeState{this, account, {}, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    auto addFields = [&](const char* tab, const std::vector<std::pair<const char*, bool>>& labels) {
        GtkWidget* grid = gtk_grid_new();
        gtk_container_set_border_width(GTK_CONTAINER(grid), 5);
        gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
        for (int row = 0; row < static_cast<int>(labels.size()); ++row) {
            const bool color = std::string(labels[row].first).find("Color") != std::string::npos;
            GtkWidget* editor = color ? gtk_combo_box_text_new_with_entry() : gtk_entry_new();
            GtkWidget* entry = color ? gtk_bin_get_child(GTK_BIN(editor)) : editor;
            if (color) {
                const char* colors[] = {"white", "yellow", "orange", "red", "darkred", "lightgreen", "green", "darkgreen", "lightblue", "blue", "darkblue", "brown", "cynober", "purple", "darkpurple", "lightgray", "gray", "black", "transparent"};
                for (const char* colorName : colors) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(editor), colorName);
            }
            const auto found = values.find(labels[row].first);
            gtk_entry_set_text(GTK_ENTRY(entry), found == values.end() ? "" : found->second.c_str() + (found->second.empty() ? 0 : 1));
            gtk_editable_set_editable(GTK_EDITABLE(entry), labels[row].second);
            gtk_widget_set_size_request(editor, 190, -1);
            std::string label(labels[row].first);
            if (label == "Account") label = "Account name";
            else if (label == "Online Seconds") label = "Online seconds";
            else label[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(label[0])));
            GtkWidget* caption = gtk_label_new((label + ":").c_str());
            gtk_widget_set_halign(caption, GTK_ALIGN_START);
            gtk_widget_set_hexpand(caption, true);
            gtk_grid_attach(GTK_GRID(grid), caption, 0, row, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), editor, 1, row, 1, 1);
            state->fields[labels[row].first] = entry;
        }
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), grid, gtk_label_new(tab));
    };
    addFields("Stats", {{"Account", false}, {"Last IP", false}, {"Kills", false}, {"Deaths", false}, {"Online Seconds", false}, {"Rating", false}, {"Rating Deviation", false}});
    addFields("Look", {{"Head Image", true}, {"Body Image", true}, {"Animation", true}, {"Skin Color", true}, {"Coat Color", true}, {"Sleeves Color", true}, {"Shoes Color", true}, {"Belt Color", true}});
    GtkWidget* basic = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(basic), 5);
    gtk_grid_set_row_spacing(GTK_GRID(basic), 3);
    gtk_grid_set_column_spacing(GTK_GRID(basic), 6);
    auto addBasicField = [&](const char* key, const char* label, int column, int row, int width, int span = 1) {
        GtkWidget* entry = gtk_entry_new();
        const auto found = values.find(key);
        gtk_entry_set_text(GTK_ENTRY(entry), found == values.end() ? "" : found->second.c_str() + (found->second.empty() ? 0 : 1));
        gtk_widget_set_size_request(entry, width, -1);
        GtkWidget* caption = gtk_label_new(label);
        gtk_widget_set_halign(caption, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(basic), caption, column, row, 1, 1);
        gtk_grid_attach(GTK_GRID(basic), entry, column + 1, row, span, 1);
        state->fields[key] = entry;
    };
    addBasicField("Level", "Level:", 0, 0, 280, 3);
    const struct { const char* leftKey; const char* leftLabel; const char* rightKey; const char* rightLabel; } basicRows[] = {
        {"X", "x:", "Y", "y:"}, {"Hearts", "Hearts:", "Full Hearts", "Fullhearts:"}, {"AP", "AP:", "MP", "MP:"}, {"Gralats", "Gralats:", "Glove", "Glove:"}, {"Bombs", "Bombs:", "Arrows", "Arrows:"}, {"Sword Power", "Sword:", "Sword Image", "-Image:"}, {"Shield Power", "Shield:", "Shield Image", "-Image:"}
    };
    for (int row = 0; row < static_cast<int>(std::size(basicRows)); ++row) {
        addBasicField(basicRows[row].leftKey, basicRows[row].leftLabel, 0, row + 1, 92);
        addBasicField(basicRows[row].rightKey, basicRows[row].rightLabel, 2, row + 1, 92);
    }
    state->male = gtk_check_button_new_with_label("male");
    state->weapons = gtk_check_button_new_with_label("weapons enabled");
    state->spin = gtk_check_button_new_with_label("spin attack");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->male), values.find("Male") != values.end() && values.at("Male").find("true") != std::string::npos);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->weapons), values.find("Weapons Enabled") != values.end() && values.at("Weapons Enabled").find("true") != std::string::npos);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->spin), values.find("Spin Attack") != values.end() && values.at("Spin Attack").find("true") != std::string::npos);
    GtkWidget* options = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(options), state->male, false, false, 0);
    gtk_box_pack_start(GTK_BOX(options), state->weapons, false, false, 0);
    gtk_box_pack_start(GTK_BOX(options), state->spin, false, false, 0);
    gtk_grid_attach(GTK_GRID(basic), options, 0, 8, 4, 1);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), basic, gtk_label_new("Basic Attributes"));
    GtkWidget* chestsPage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(chestsPage), 5);
    GtkWidget* chestHeader = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget* chestLabel = gtk_label_new("Open chests:");
    gtk_widget_set_halign(chestLabel, GTK_ALIGN_START);
    gtk_widget_set_margin_start(chestLabel, 5);
    GtkWidget* fillChests = gtk_button_new_with_label("'Fill' selected chests");
    gtk_widget_set_size_request(fillChests, 128, 24);
    gtk_box_pack_start(GTK_BOX(chestHeader), chestLabel, false, false, 0);
    gtk_box_pack_end(GTK_BOX(chestHeader), fillChests, false, false, 0);
    gtk_box_pack_start(GTK_BOX(chestsPage), chestHeader, false, false, 0);
    GtkWidget* chestScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chestScroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    state->chestStore = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    state->chests = gtk_tree_view_new_with_model(GTK_TREE_MODEL(state->chestStore));
    GtkCellRenderer* chestRenderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(state->chests), gtk_tree_view_column_new_with_attributes("", chestRenderer, "text", 0, nullptr));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(state->chests), false);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(state->chests)), GTK_SELECTION_MULTIPLE);
    gtk_container_add(GTK_CONTAINER(chestScroll), state->chests);
    gtk_box_pack_start(GTK_BOX(chestsPage), chestScroll, true, true, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), chestsPage, gtk_label_new("Chests"));
    auto addList = [&](const char* tab, GtkWidget** target) {
        GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_set_border_width(GTK_CONTAINER(scrolled), 5);
        *target = gtk_text_view_new();
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(*target), 5);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(*target), 5);
        gtk_container_add(GTK_CONTAINER(scrolled), *target);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new(tab));
    };
    addList("Weapons", &state->weaponList);
    addList("Script Flags", &state->flags);
    std::string section;
    std::istringstream source(editorText == nullptr ? "" : editorText);
    for (std::string line; std::getline(source, line);) {
        if (line == "[Chests]") { section = "Chests"; continue; }
        if (line == "[Weapons]") { section = "Weapons"; continue; }
        if (line == "[Script Flags]") { section = "Script Flags"; continue; }
        if (!line.empty() && line.front() == '[') { section.clear(); continue; }
        if (line.empty()) continue;
        if (section == "Chests") { const size_t first = line.find(':'); const size_t second = first == std::string::npos ? std::string::npos : line.find(':', first + 1); const std::string filename = second == std::string::npos ? line : line.substr(second + 1); GtkTreeIter row; gtk_list_store_append(state->chestStore, &row); gtk_list_store_set(state->chestStore, &row, 0, filename.c_str(), 1, line.c_str(), -1); continue; }
        if (line.find(':') != std::string::npos) continue;
        GtkWidget* target = section == "Weapons" ? state->weaponList : section == "Script Flags" ? state->flags : nullptr;
        if (target != nullptr) { GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(target)); GtkTextIter end; gtk_text_buffer_get_end_iter(buffer, &end); gtk_text_buffer_insert(buffer, &end, (line + "\n").c_str(), -1); }
    }
    gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);
    GtkWidget* footer = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(footer), GTK_BUTTONBOX_END);
    gtk_container_set_border_width(GTK_CONTAINER(footer), 5);
    gtk_box_set_spacing(GTK_BOX(footer), 5);
    GtkWidget* apply = gtk_button_new_with_label("Apply");
    GtkWidget* cancel = gtk_button_new_with_label("Close");
    gtk_widget_set_size_request(apply, 80, 24);
    gtk_widget_set_size_request(cancel, 80, 24);
    gtk_container_add(GTK_CONTAINER(footer), apply);
    gtk_container_add(GTK_CONTAINER(footer), cancel);
    gtk_box_pack_end(GTK_BOX(root), footer, false, false, 0);
    auto onAttributeFormApply = +[](GtkButton* button, gpointer userData) {
        auto* state = static_cast<AttributeState*>(userData);
        GtkWidget* responseDialog = GTK_WIDGET(button);
        while (!GTK_IS_WINDOW(responseDialog)) responseDialog = gtk_widget_get_parent(responseDialog);
        if (responseDialog == nullptr) return;
            std::ostringstream text;
            const char* sections[] = {"Stats", "Look", "Basic Attributes"};
            const char* labels[][15] = {{"Account", "Last IP", "Kills", "Deaths", "Online Seconds", "Rating", "Rating Deviation"}, {"Head Image", "Body Image", "Animation", "Skin Color", "Coat Color", "Sleeves Color", "Shoes Color", "Belt Color"}, {"Level", "X", "Y", "Hearts", "Full Hearts", "AP", "MP", "Gralats", "Glove", "Bombs", "Arrows", "Sword Power", "Sword Image", "Shield Power", "Shield Image"}};
            const int counts[] = {7, 8, 15};
            for (int section = 0; section < 3; ++section) { text << '[' << sections[section] << "]\n"; for (int index = 0; index < counts[section]; ++index) text << labels[section][index] << ": " << gtk_entry_get_text(GTK_ENTRY(state->fields[labels[section][index]])) << "\n"; if (section == 2) text << "Male: " << (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->male)) ? "true" : "false") << "\nWeapons Enabled: " << (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->weapons)) ? "true" : "false") << "\nSpin Attack: " << (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->spin)) ? "true" : "false") << "\n"; text << '\n'; }
            text << "[Chests]\n";
            GtkTreeIter chest;
            gboolean hasChest = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(state->chestStore), &chest);
            while (hasChest) { gchar* chestName = nullptr; gtk_tree_model_get(GTK_TREE_MODEL(state->chestStore), &chest, 1, &chestName, -1); text << (chestName == nullptr ? "" : chestName) << '\n'; g_free(chestName); hasChest = gtk_tree_model_iter_next(GTK_TREE_MODEL(state->chestStore), &chest); }
            text << '\n';
            const struct { const char* title; GtkWidget* field; } lists[] = {{"Weapons", state->weaponList}, {"Script Flags", state->flags}};
            for (const auto& list : lists) { GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(list.field)); GtkTextIter start; GtkTextIter end; gtk_text_buffer_get_bounds(buffer, &start, &end); gchar* content = gtk_text_buffer_get_text(buffer, &start, &end, false); text << '[' << list.title << "]\n" << (content == nullptr ? "" : content) << '\n'; g_free(content); }
            char* properties = rc_parse_player_attributes_text(text.str().c_str());
            if (properties != nullptr) {
                backupEditorText("attributes", state->account, text.str(), true);
                rc_set_player_attributes(state->list->connection, state->account.c_str(), properties);
                free(properties);
            }
        gtk_widget_destroy(GTK_WIDGET(responseDialog));
    };
    g_signal_connect(apply, "clicked", G_CALLBACK(onAttributeFormApply), state);
    g_signal_connect(cancel, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer) { GtkWidget* dialog = GTK_WIDGET(button); while (!GTK_IS_WINDOW(dialog)) dialog = gtk_widget_get_parent(dialog); if (dialog != nullptr) gtk_widget_destroy(dialog); }), nullptr);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer userData) { delete static_cast<AttributeState*>(userData); }), state);
    gtk_widget_show_all(dialog);
    gtk_window_resize(GTK_WINDOW(dialog), 400, 360);
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, +[](gpointer userData) -> gboolean { if (GTK_IS_WINDOW(userData)) gtk_window_resize(GTK_WINDOW(userData), 400, 360); return G_SOURCE_REMOVE; }, dialog, nullptr);
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
        GtkWidget* dialog = gtk_dialog_new_with_buttons(("Profile of " + std::string(account)).c_str(), GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, "Apply", GTK_RESPONSE_ACCEPT, "Close", GTK_RESPONSE_CANCEL, nullptr);
        gtk_widget_set_name(dialog, "ProfileWindow");
        gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 400);
        GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), root);
        GtkWidget* nick = gtk_label_new((values[0] + ": " + (values.size() > 1 ? values[1] : "")).c_str());
        gtk_widget_set_halign(nick, GTK_ALIGN_START);
        gtk_widget_set_margin_start(nick, 10);
        gtk_widget_set_margin_top(nick, 6);
        gtk_box_pack_start(GTK_BOX(root), nick, false, false, 0);
        GtkWidget* split = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
        gtk_container_set_border_width(GTK_CONTAINER(split), 10);
        gtk_box_pack_start(GTK_BOX(root), split, true, true, 0);
        GtkWidget* information = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        GtkWidget* informationLabel = gtk_label_new("Player information");
        gtk_widget_set_halign(informationLabel, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(information), informationLabel, false, false, 0);
        GtkWidget* form = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(form), 3);
        gtk_grid_set_column_spacing(GTK_GRID(form), 6);
        gtk_box_pack_start(GTK_BOX(information), form, false, false, 0);
        auto* state = new ProfileState{this, account};
        const char* labels[] = {"Real name", "Age", "Sex", "Country", "Messenger", "E-mail", "Homepage", "Fav. hangout"};
        for (int index = 0; index < 8; ++index) {
            GtkWidget* field = index == 2 ? gtk_combo_box_text_new_with_entry() : gtk_entry_new();
            if (index == 2) { gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(field), "unknown"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(field), "male"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(field), "female"); GtkWidget* entry = gtk_bin_get_child(GTK_BIN(field)); gtk_entry_set_text(GTK_ENTRY(entry), values[index + 1].c_str()); gtk_editable_set_editable(GTK_EDITABLE(entry), false); }
            else gtk_entry_set_text(GTK_ENTRY(field), values[index + 1].c_str());
            gtk_widget_set_size_request(field, 160, 24);
            GtkWidget* caption = gtk_label_new((std::string(labels[index]) + ":").c_str());
            gtk_widget_set_size_request(caption, 94, -1);
            gtk_widget_set_halign(caption, GTK_ALIGN_START);
            gtk_grid_attach(GTK_GRID(form), caption, 0, index, 1, 1);
            gtk_grid_attach(GTK_GRID(form), field, 1, index, 1, 1);
            state->fields[index] = field;
        }
        GtkWidget* quoteLabel = gtk_label_new("Favourite quote:");
        gtk_widget_set_halign(quoteLabel, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(form), quoteLabel, 0, 8, 2, 1);
        GtkWidget* quoteScroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(quoteScroll, 260, 58);
        state->quote = gtk_text_view_new();
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(state->quote), GTK_WRAP_WORD_CHAR);
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->quote)), values[9].c_str(), -1);
        gtk_container_add(GTK_CONTAINER(quoteScroll), state->quote);
        gtk_grid_attach(GTK_GRID(form), quoteScroll, 0, 9, 2, 1);
        gtk_box_pack_start(GTK_BOX(split), information, false, false, 0);
        GtkWidget* statsFrame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        GtkWidget* statsLabel = gtk_label_new("In-game stats");
        gtk_widget_set_halign(statsLabel, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(statsFrame), statsLabel, false, false, 0);
        GtkWidget* stats = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        gtk_box_pack_start(GTK_BOX(statsFrame), stats, true, true, 0);
        GtkWidget* level = gtk_entry_new();
        GtkWidget* online = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(level), values.size() > 11 ? values[11].c_str() : "");
        gtk_entry_set_text(GTK_ENTRY(online), values[10].c_str());
        gtk_editable_set_editable(GTK_EDITABLE(level), false);
        gtk_editable_set_editable(GTK_EDITABLE(online), false);
        gtk_widget_set_size_request(level, 134, 24);
        gtk_widget_set_size_request(online, 134, 24);
        GtkWidget* statForm = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(statForm), 3);
        gtk_grid_set_column_spacing(GTK_GRID(statForm), 5);
        GtkWidget* levelLabel = gtk_label_new("Level:");
        GtkWidget* onlineLabel = gtk_label_new("Online time:");
        gtk_widget_set_halign(levelLabel, GTK_ALIGN_START);
        gtk_widget_set_halign(onlineLabel, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(statForm), levelLabel, 0, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(statForm), level, 1, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(statForm), onlineLabel, 0, 1, 1, 1);
        gtk_grid_attach(GTK_GRID(statForm), online, 1, 1, 1, 1);
        gtk_box_pack_start(GTK_BOX(stats), statForm, false, false, 0);
        GtkWidget* variableScroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(variableScroll, 200, 240);
        GtkListStore* variableStore = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
        GtkWidget* variableText = gtk_tree_view_new_with_model(GTK_TREE_MODEL(variableStore));
        g_object_unref(variableStore);
        GtkCellRenderer* nameRenderer = gtk_cell_renderer_text_new();
        GtkCellRenderer* valueRenderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* nameColumn = gtk_tree_view_column_new_with_attributes("", nameRenderer, "text", 0, nullptr);
        gtk_tree_view_column_set_fixed_width(nameColumn, 100);
        gtk_tree_view_column_set_sizing(nameColumn, GTK_TREE_VIEW_COLUMN_FIXED);
        gtk_tree_view_append_column(GTK_TREE_VIEW(variableText), nameColumn);
        gtk_tree_view_append_column(GTK_TREE_VIEW(variableText), gtk_tree_view_column_new_with_attributes("", valueRenderer, "text", 1, nullptr));
        gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(variableText), false);
        const char* statNames[] = {"Kills", "Deaths", "Maxpower", "Rating", "Alignment", "Gralat", "Swordpower", "Spin"};
        for (std::size_t index = 12; index < values.size() && index - 12 < std::size(statNames); ++index) { GtkTreeIter stat; gtk_list_store_append(variableStore, &stat); gtk_list_store_set(variableStore, &stat, 0, (std::string(statNames[index - 12]) + ":").c_str(), 1, values[index].c_str(), -1); }
        gtk_container_add(GTK_CONTAINER(variableScroll), variableText);
        gtk_box_pack_start(GTK_BOX(stats), variableScroll, true, true, 4);
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
    GtkWidget* dialog = gtk_dialog_new_with_buttons(title.c_str(), GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, "Close", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, nullptr);
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
                PMWindowData historyData{remoteList->connection, remoteList->applicationDirectory / "PMs", nullptr, nullptr, nullptr, 0, account, nick == nullptr ? "" : nick, remoteList->accountName, ""};
                onPMHistory(nullptr, &historyData);
            }
            g_free(account);
            g_free(nick);
        })), list);
        g_signal_connect(menu, "deactivate", G_CALLBACK(+[](GtkWidget* menuWidget, gpointer) {
            g_object_ref(menuWidget);
            g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, +[](gpointer menuData) {
                gtk_widget_destroy(GTK_WIDGET(menuData));
                g_object_unref(menuData);
                return G_SOURCE_REMOVE;
            }, menuWidget, nullptr);
        }), nullptr);
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
    std::vector<int> activePlayerIds;
    GtkTreeIter admins;
    GtkTreeIter playersGroup;
    gtk_tree_store_append(store, &admins, nullptr);
    gtk_tree_store_set(store, &admins, PlayerIconColumn, channelIcon, PlayerNickColumn, "Admins", PlayerIdColumn, 0, PlayerOrderColumn, 0, -1);
    gtk_tree_store_append(store, &playersGroup, nullptr);
    gtk_tree_store_set(store, &playersGroup, PlayerIconColumn, channelIcon, PlayerNickColumn, "Players", PlayerIdColumn, 0, PlayerOrderColumn, 1, -1);
    for (int index = 0; index < count; ++index) {
        activePlayerIds.push_back(players[index].id);
        auto [player, inserted] = serverPlayersById.try_emplace(players[index].id, players[index].id);
        player->second.setIdentity(players[index].account, players[index].nick, players[index].level);
        GtkTreeIter row;
        const bool admin = players[index].level == nullptr || *players[index].level == '\0';
        gtk_tree_store_append(store, &row, admin ? &admins : &playersGroup);
        const auto pm = pmTypes.find(players[index].id);
        gtk_tree_store_set(store, &row, PlayerIconColumn, pm != pmTypes.end() && pmIconsVisible ? pmIconFor(pm->second) : statusIconFor(player->second), PlayerNickColumn, players[index].nick == nullptr ? "" : players[index].nick, PlayerAccountColumn, players[index].account == nullptr ? "" : players[index].account, PlayerLevelColumn, players[index].level == nullptr ? "" : players[index].level, PlayerIdColumn, players[index].id, PlayerOrderColumn, static_cast<int>(index) + 2, -1);
    }
    int retainedOrder = count + 2;
    for (const auto& [playerId, identity] : pmPlayers) {
        if (std::find(activePlayerIds.begin(), activePlayerIds.end(), playerId) != activePlayerIds.end()) continue;
        const std::string& account = identity.first;
        const std::string& nick = identity.second.empty() ? identity.first : identity.second;
        if (account.empty() && nick.empty()) continue;
        GtkTreeIter row;
        gtk_tree_store_append(store, &row, &playersGroup);
        const auto pm = pmTypes.find(playerId);
        const auto player = serverPlayersById.find(playerId);
        gtk_tree_store_set(store, &row, PlayerIconColumn, pm != pmTypes.end() && pmIconsVisible ? pmIconFor(pm->second) : (player == serverPlayersById.end() ? onlineIcon : statusIconFor(player->second)), PlayerNickColumn, nick.c_str(), PlayerAccountColumn, account.c_str(), PlayerLevelColumn, "Offline", PlayerIdColumn, playerId, PlayerOrderColumn, retainedOrder++, -1);
    }
    rc_request_pm_server_list(connection);
    rc_request_pm_guild_list(connection);
    refreshRemoteLists();
    gtk_tree_view_expand_all(GTK_TREE_VIEW(tree));
}

void TPlayerList::setPlayerProperties(int playerId, const char* properties) {
    auto [player, inserted] = serverPlayersById.try_emplace(playerId, playerId);
    RCPlayer* players = nullptr;
    const int count = connection == nullptr ? 0 : rc_get_players(connection, &players);
    for (int index = 0; index < count; ++index) if (players[index].id == playerId) {
        player->second.setIdentity(players[index].account, players[index].nick, players[index].level);
        break;
    }
    player->second.setProperties(properties == nullptr ? "" : properties);
    if (store != nullptr) refresh();
}

std::optional<bool> TPlayerList::localAccountConnected() const {
    for (const auto& [id, player] : serverPlayersById) if (g_ascii_strcasecmp(player.account().c_str(), accountName.c_str()) == 0) return player.connected();
    return std::nullopt;
}

void TPlayerList::loadStatusIcons() {
    for (GdkPixbuf* icon : statusIcons) if (icon != nullptr) g_object_unref(icon);
    statusIcons.clear();
    for (const std::string& status : statusNames) {
        std::string fileName = status;
        std::transform(fileName.begin(), fileName.end(), fileName.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (fileName == "rping") fileName = "role-playing";
        else if (fileName == "eating") fileName = "eating";
        else if (fileName == "no pms") fileName = "no pms";
        const std::filesystem::path path = std::filesystem::path("images") / ("plisticon" + fileName + ".png");
        statusIcons.push_back(std::filesystem::exists(path) ? gdk_pixbuf_new_from_file(path.string().c_str(), nullptr) : nullptr);
    }
}

GdkPixbuf* TPlayerList::statusIconFor(const TServerPlayer& player) const {
    const int index = player.status();
    if (index >= 0 && index < static_cast<int>(statusIcons.size()) && statusIcons[index] != nullptr) return statusIcons[index];
    return onlineIcon;
}

GdkPixbuf* TPlayerList::pmIconFor(const std::string& type) const {
    if (type == "mass") return pmMassIcon;
    if (type == "guild") return pmGuildIcon;
    if (type == "admin") return pmAdminIcon;
    return pmNormalIcon;
}

std::string TPlayerList::notePrivateMessage(int playerId, const char* account, const char* nick, const char* message, const char* type) {
    if (playerId < 0) return "";
    std::string accountText = account == nullptr ? "" : account;
    std::string nickText = nick == nullptr ? "" : nick;
    if ((accountText.empty() || nickText.empty()) && serverStore != nullptr) {
        PMPlayerIdentity identity{playerId, accountText, nickText};
        gtk_tree_model_foreach(GTK_TREE_MODEL(serverStore), findServerPlayerById, &identity);
        if (!identity.account.empty()) accountText = identity.account;
        if (!identity.nick.empty()) nickText = identity.nick;
    }
    appendHistory(accountText.c_str(), nickText.empty() ? accountText.c_str() : nickText.c_str(), message);
    pmPlayers[playerId] = {accountText, nickText};
    if (!pmMessages[playerId].empty()) pmMessages[playerId] += '\n';
    pmMessages[playerId] += formatPMCommaText(message == nullptr ? "" : message);
    latestPMPlayerId = playerId;
    pmTypes[playerId] = type == nullptr ? "normal" : type;
    pmIconsVisible = true;
    if (pmBlinkSource == 0) pmBlinkSource = g_timeout_add(500, onPMBlink, this);
    refresh();
    updatePMIcons();
    return nickText.empty() || nickText == accountText ? accountText : nickText + " (" + accountText + ")";
}

bool TPlayerList::openLatestPrivateMessage() {
    const auto player = pmPlayers.find(latestPMPlayerId);
    if (player == pmPlayers.end() || player->second.first.empty()) return false;
    openPrivateMessage(latestPMPlayerId, player->second.first.c_str(), player->second.second.c_str());
    return true;
}

void TPlayerList::appendHistory(const char* account, const char* sender, const char* message) const {
    writePMHistory(applicationDirectory / "PMs", account == nullptr ? "" : account, sender == nullptr ? "" : sender, message);
}

void TPlayerList::openPrivateMessage(int playerId, const char* account, const char* nick) {
    if (playerId == 0 || account == nullptr || *account == '\0') return;
    const auto unread = pmMessages.find(playerId);
    PMWindowData* data = new PMWindowData{connection, applicationDirectory / "PMs", nullptr, nullptr, nullptr, playerId, account, nick == nullptr ? "" : nick, accountName, unread == pmMessages.end() ? "" : unread->second};
    data->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(data->window, "PrivateMessage");
    gtk_window_set_title(GTK_WINDOW(data->window), "PM");
    gtk_window_set_default_size(GTK_WINDOW(data->window), 380, 300);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(data->window), root);
    GtkWidget* label = gtk_label_new((data->account + ": " + data->nick).c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_widget_set_margin_start(label, 5);
    gtk_widget_set_margin_end(label, 5);
    gtk_box_pack_start(GTK_BOX(root), label, false, false, 5);
    GtkWidget* panes = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_position(GTK_PANED(panes), 120);
    gtk_box_pack_start(GTK_BOX(root), panes, true, true, 0);
    GtkWidget* receivedScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(receivedScrolled), GTK_SHADOW_IN);
    data->received = gtk_text_view_new();
    gtk_widget_set_name(data->received, "PrivateMessageReceived");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(data->received), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(data->received), false);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(data->received), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(data->received), 5);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(data->received), 5);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(data->received)), data->message.c_str(), -1);
    gtk_container_add(GTK_CONTAINER(receivedScrolled), data->received);
    gtk_paned_pack1(GTK_PANED(panes), receivedScrolled, false, true);
    GtkWidget* replyScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(replyScrolled), GTK_SHADOW_IN);
    data->reply = gtk_text_view_new();
    gtk_widget_set_name(data->reply, "PrivateMessageText");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(data->reply), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(data->reply), 5);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(data->reply), 5);
    gtk_container_add(GTK_CONTAINER(replyScrolled), data->reply);
    gtk_paned_pack2(GTK_PANED(panes), replyScrolled, true, true);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* history = gtk_button_new_with_label("History");
    GtkWidget* send = gtk_button_new_with_label("Send");
    applyGtkButtonIcon(history, GTK_STOCK_OPEN);
    applyGtkButtonIcon(send, GTK_STOCK_EXECUTE);
    gtk_widget_set_size_request(history, 76, 28);
    gtk_widget_set_size_request(send, 76, 28);
    gtk_container_add(GTK_CONTAINER(buttons), history);
    gtk_container_add(GTK_CONTAINER(buttons), send);
    gtk_box_pack_end(GTK_BOX(root), buttons, false, false, 0);
    g_signal_connect(send, "clicked", G_CALLBACK(onPMSend), data);
    g_signal_connect(history, "clicked", G_CALLBACK(onPMHistory), data);
    g_signal_connect(data->window, "delete-event", G_CALLBACK(onPMWindowDelete), data);
    g_signal_connect(data->window, "destroy", G_CALLBACK(onPMWindowDestroy), data);
    if (unread != pmMessages.end()) markPrivateMessageRead(playerId);
    gtk_widget_show_all(data->window);
    gtk_window_present(GTK_WINDOW(data->window));
    gtk_widget_grab_focus(data->reply);
}

void TPlayerList::openPrivateMessageHistory(const char* account, const char* nick) {
    PMWindowData data{connection, applicationDirectory / "PMs", nullptr, nullptr, nullptr, 0, account == nullptr ? "" : account, nick == nullptr ? "" : nick, accountName, ""};
    onPMHistory(nullptr, &data);
}

void TPlayerList::markPrivateMessageRead(int playerId) {
    pmTypes.erase(playerId);
    pmPlayers.erase(playerId);
    pmMessages.erase(playerId);
    if (latestPMPlayerId == playerId) latestPMPlayerId = pmPlayers.empty() ? 0 : pmPlayers.rbegin()->first;
    if (pmPlayers.empty() && pmBlinkSource != 0) {
        g_source_remove(pmBlinkSource);
        pmBlinkSource = 0;
        pmIconsVisible = true;
    }
    clearPrivateMessageAlert();
    refresh();
}

void TPlayerList::clearPrivateMessageAlert() {
    remote_control_clear_pm_tray_alert();
    GList* windows = gtk_window_list_toplevels();
    for (GList* current = windows; current != nullptr; current = current->next) if (GTK_IS_WINDOW(current->data)) gtk_window_set_urgency_hint(GTK_WINDOW(current->data), false);
    g_list_free(windows);
    updatePMIcons();
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
            const auto player = serverPlayersById.find(playerId);
            gtk_tree_store_set(store, &row, PlayerIconColumn, pm != pmTypes.end() && pmIconsVisible ? pmIconFor(pm->second) : (player == serverPlayersById.end() ? onlineIcon : statusIconFor(player->second)), -1);
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
        PMWindowData data{connection, applicationDirectory / "PMs", nullptr, nullptr, nullptr, 0, account, nick == nullptr ? "" : nick, accountName, ""};
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
    if (account != nullptr && *account != '\0') {
        const std::string prompt = "Do you really want to reset the attributes of " + std::string(account) + " ?";
        GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_CANCEL, "%s", prompt.c_str());
        gtk_window_set_title(GTK_WINDOW(dialog), "Question");
        gtk_dialog_add_button(GTK_DIALOG(dialog), "OK", GTK_RESPONSE_ACCEPT);
        g_object_set_data_full(G_OBJECT(dialog), "player-account", g_strdup(account), g_free);
        g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* responseDialog, gint response, gpointer userData) {
            auto* list = static_cast<TPlayerList*>(userData);
            if (response == GTK_RESPONSE_ACCEPT) rc_reset_player(list->connection, static_cast<const char*>(g_object_get_data(G_OBJECT(responseDialog), "player-account")));
            gtk_widget_destroy(GTK_WIDGET(responseDialog));
        }), this);
        gtk_widget_show_all(dialog);
    }
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
