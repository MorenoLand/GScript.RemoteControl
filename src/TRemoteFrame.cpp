#include "TRemoteFrame.h"
#include "RCOptions.h"
#include "TFileBrowserTree.h"
#include "TPlayerList.h"
#include "TScriptList.h"
#include "TServerTextEditor.h"
#include "TToallsWindow.h"
#include "TAccountsWindow.h"
#include "TOptionsWindow.h"
#include "TNPCList.h"

#include <grclib.h>

#include <ctime>
#include <iomanip>
#include <sstream>

extern void rc3_begin_pm_tray_alert();

namespace {
    std::string chatTimestamp(const RC3::RCOptions& options) {
        if (!options.rctimestamps) return "";
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        std::ostringstream text;
        text << std::put_time(&local, options.timestampformat.c_str());
        return text.str();
    }
}

TRemoteFrame::TRemoteFrame(const RC3::RCOptions& nextOptions, const std::filesystem::path& nextApplicationDirectory, std::function<void()> onClose) : onCloseCallback(std::move(onClose)), options(nextOptions), applicationDirectory(nextApplicationDirectory) {
    kappaEmote = gdk_pixbuf_new_from_file((applicationDirectory / "images" / "emote_kappa.png").string().c_str(), nullptr);
    pmNormalEmote = gdk_pixbuf_new_from_file((applicationDirectory / "images" / "pmicon_normal.png").string().c_str(), nullptr);
    pacmanEmote = gdk_pixbuf_new_from_file((applicationDirectory / "images" / "emote_pacman.png").string().c_str(), nullptr);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "RemoteFrame");
    gtk_window_set_title(GTK_WINDOW(window), (std::string("Remote Control ") + RC3_BUILD_DATE).c_str());
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 350);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);

    if (!options.graphicalmenu) {
        GtkWidget* menuBar = gtk_menu_bar_new();
        const char* menuNames[] = {"Players", "Files", "Configuration", "Scripts", "Misc"};
        for (const char* menuName : menuNames) {
        GtkWidget* item = gtk_menu_item_new_with_label(menuName);
        GtkWidget* menu = gtk_menu_new();
        if (std::string(menuName) == "Players") {
            addMenuItem(menu, "Playerlist", G_CALLBACK(onPlayerList));
            addMenuItem(menu, "Accounts", G_CALLBACK(onAccounts));
            addMenuItem(menu, "Toalls", G_CALLBACK(onToalls));
        } else if (std::string(menuName) == "Files") {
            addMenuItem(menu, "File Browser", G_CALLBACK(onFileBrowser));
        } else if (std::string(menuName) == "Configuration") {
            addMenuItem(menu, "RC Options", G_CALLBACK(onRCOptions));
            addMenuItem(menu, "Server Options", G_CALLBACK(onServerOptions));
            addMenuItem(menu, "Folder Config", G_CALLBACK(onFolderConfig));
        } else if (std::string(menuName) == "Scripts") {
            addMenuItem(menu, "NPCs", G_CALLBACK(onNPCs));
            addMenuItem(menu, "Classes", G_CALLBACK(onClasses));
            addMenuItem(menu, "Weapons (GUI)", G_CALLBACK(onWeapons));
        } else {
            addMenuItem(menu, "Server Flags", G_CALLBACK(onServerFlags));
            addMenuItem(menu, "Level-NPC dump", G_CALLBACK(onLocalNPCDump));
        }
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
        gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), item);
        }
        gtk_box_pack_start(GTK_BOX(root), menuBar, false, false, 0);
    } else {
        GtkWidget* fixed = gtk_fixed_new();
        graphicalFixed = fixed;
        const std::filesystem::path background = applicationDirectory / "images" / options.background;
        GError* imageError = nullptr;
        backgroundPixbuf = gdk_pixbuf_new_from_file(background.string().c_str(), &imageError);
        backgroundImage = gtk_image_new_from_pixbuf(backgroundPixbuf);
        if (imageError != nullptr) g_error_free(imageError);
        gtk_widget_set_size_request(backgroundImage, 500, 160);
        gtk_fixed_put(GTK_FIXED(fixed), backgroundImage, 0, 0);
        const int positions[12][2] = {{5, 15}, {5, 48}, {38, 15}, {71, 15}, {394, 15}, {427, 15}, {460, 15}, {460, 48}, {460, 81}, {460, 114}, {427, 114}, {394, 114}};
        for (int index = 0; index < 12; ++index) {
            GtkWidget* button = gtk_event_box_new();
            gtk_event_box_set_visible_window(GTK_EVENT_BOX(button), false);
            GtkWidget* buttonImage = gtk_image_new_from_file((applicationDirectory / "images" / options.buttonimagefiles[index]).string().c_str());
            gtk_container_add(GTK_CONTAINER(button), buttonImage);
            g_object_set_data(G_OBJECT(button), "button-index", GINT_TO_POINTER(index));
            g_signal_connect(button, "button-press-event", G_CALLBACK(onGraphicalButton), this);
            g_signal_connect(button, "button-release-event", G_CALLBACK(onGraphicalButton), this);
            gtk_fixed_put(GTK_FIXED(fixed), button, positions[index][0], positions[index][1]);
            graphicalButtons[index] = button;
            if (index >= 8) gtk_widget_hide(button);
        }
        GdkColor labelColor;
        GdkColor labelBackgroundColor;
        gdk_color_parse(options.colorlabel.c_str(), &labelColor);
        gdk_color_parse(options.colorlabelback.c_str(), &labelBackgroundColor);
        PangoFontDescription* labelFont = pango_font_description_from_string("Sans Bold 12");
        const auto addLabel = [&](const std::string& text, int x, int y, GtkWidget** front) {
            const int offsets[][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {1, 1}};
            for (const auto& offset : offsets) {
                GtkWidget* shadow = gtk_label_new(text.c_str());
                gtk_widget_modify_fg(shadow, GTK_STATE_NORMAL, &labelBackgroundColor);
                gtk_widget_modify_font(shadow, labelFont);
                gtk_fixed_put(GTK_FIXED(fixed), shadow, x + offset[0], y + offset[1]);
            }
            *front = gtk_label_new(text.c_str());
            gtk_widget_modify_fg(*front, GTK_STATE_NORMAL, &labelColor);
            gtk_widget_modify_font(*front, labelFont);
            gtk_fixed_put(GTK_FIXED(fixed), *front, x, y);
        };
        addLabel(options.labelservers, 10, 90, &serverLabel);
        addLabel(options.labelplayers, 10, 110, &playersLabel);
        pango_font_description_free(labelFont);
        gtk_box_pack_start(GTK_BOX(root), fixed, true, true, 0);
        g_signal_connect(fixed, "size-allocate", G_CALLBACK(onGraphicalAllocate), this);
    }

    notebook = gtk_notebook_new();
    gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), false);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), true);
    chatScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chatScrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(chatScrolled), GTK_SHADOW_NONE);
    chatField = gtk_text_view_new();
    gtk_widget_set_name(chatField, "ChatField");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(chatField), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(chatField), false);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(chatField), 5);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(chatField), 5);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chatField), GTK_WRAP_WORD_CHAR);
    configureChatField(chatField);
    gtk_container_add(GTK_CONTAINER(chatScrolled), chatField);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), chatScrolled, gtk_label_new("RC Chat "));
    gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(notebook), chatScrolled, true);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), chatScrolled, true);
    GtkCssProvider* tabProvider = gtk_css_provider_new();
    const std::string notebookCss = "#RemoteFrame notebook, #RemoteFrame notebook > header, #RemoteFrame notebook > header.top, #RemoteFrame notebook > header.top > tabs, #RemoteFrame notebook > header.top > tabs > tab { margin: 0; padding: 0; border: 0; background-color: transparent; background-image: none; box-shadow: none; } #RemoteFrame notebook > stack, #RemoteFrame notebook > stack > scrolledwindow, #RemoteFrame notebook > stack > scrolledwindow > viewport { margin: 0; padding: 0; border: 0; background-color: " + options.colorchatback + "; } #RemoteFrame notebook > header.top > tabs > tab { min-height: 12px; padding: 0 8px; } #RemoteFrame notebook > header.top > tabs > tab label { margin: 0; padding: 0; font-size: 10px; }";
    gtk_css_provider_load_from_data(tabProvider, notebookCss.c_str(), -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(window), GTK_STYLE_PROVIDER(tabProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(tabProvider);
    if (graphicalFixed != nullptr) {
        gtk_widget_set_size_request(notebook, 500, 194);
        gtk_fixed_put(GTK_FIXED(graphicalFixed), notebook, 0, 136);
    } else gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);

    editField = gtk_entry_new();
    gtk_widget_set_name(editField, "EditField");
    if (options.graphicalmenu) {
        GdkColor editBackgroundColor;
        GdkColor editColor;
        gdk_color_parse(options.coloreditback.c_str(), &editBackgroundColor);
        gdk_color_parse(options.coloredit.c_str(), &editColor);
        gtk_widget_modify_base(editField, GTK_STATE_NORMAL, &editBackgroundColor);
        gtk_widget_modify_text(editField, GTK_STATE_NORMAL, &editColor);
    }
    gtk_box_pack_start(GTK_BOX(root), editField, false, false, 0);

    g_signal_connect(editField, "key-press-event", G_CALLBACK(onEditKey), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TRemoteFrame::~TRemoteFrame() {
    if (eventSource != 0) g_source_remove(eventSource);
    if (window != nullptr) gtk_widget_destroy(window);
    if (backgroundPixbuf != nullptr) g_object_unref(backgroundPixbuf);
    if (kappaEmote != nullptr) g_object_unref(kappaEmote);
    if (pmNormalEmote != nullptr) g_object_unref(pmNormalEmote);
    if (pacmanEmote != nullptr) g_object_unref(pacmanEmote);
    delete playerList;
    delete fileBrowser;
    delete classList;
    delete weaponList;
    delete serverOptionsEditor;
    delete serverFlagsEditor;
    delete folderConfigEditor;
    delete toallsWindow;
    delete accountsWindow;
    delete optionsWindow;
    delete npcList;
}

void TRemoteFrame::open(void* nextConnection, const std::string& serverName) {
    connection = nextConnection;
    ncConnectionAttempted = false;
    rc_on_connected(connection, onConnected, this);
    rc_on_disconnected(connection, onDisconnected, this);
    rc_on_message(connection, onMessage, this);
    rc_on_irc_message(connection, onIrcMessage, this);
    rc_on_private_message_ex(connection, onPrivateMessage, this);
    rc_on_server_data(connection, onServerData, this);
    rc_on_account_list(connection, onAccountList, this);
    rc_on_player_text_data(connection, onPlayerText, this);
    rc_on_player_rights(connection, onPlayerRights, this);
    rc_on_player_attributes(connection, onPlayerAttributes, this);
    rc_on_ban_data(connection, onBanData, this);
    rc_on_ban_list_data(connection, onBanListData, this);
    if (serverLabel != nullptr) gtk_label_set_text(GTK_LABEL(serverLabel), (options.labelservers + " " + serverName).c_str());
    if (eventSource == 0) eventSource = g_timeout_add(50, processEvents, this);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(editField);
}

void TRemoteFrame::show() {
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

bool TRemoteFrame::openLatestPrivateMessage() {
    return playerList != nullptr && playerList->openLatestPrivateMessage();
}

void TRemoteFrame::onSend(GtkButton*, gpointer data) { static_cast<TRemoteFrame*>(data)->send(); }

void TRemoteFrame::onPlayerList(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory);
    frame->playerList->open(frame->connection);
}

void TRemoteFrame::onToalls(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->toallsWindow == nullptr) frame->toallsWindow = new TToallsWindow();
    frame->toallsWindow->open(frame->connection);
}

void TRemoteFrame::onAccounts(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->accountsWindow == nullptr) frame->accountsWindow = new TAccountsWindow();
    frame->accountsWindow->open(frame->connection);
}

void TRemoteFrame::onRCOptions(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->optionsWindow == nullptr) frame->optionsWindow = new TOptionsWindow(const_cast<RC3::RCOptions&>(frame->options), frame->applicationDirectory);
    frame->optionsWindow->open();
}

void TRemoteFrame::onAccountList(const char* accounts, void* data) { TRemoteFrame* frame = static_cast<TRemoteFrame*>(data); if (frame->accountsWindow != nullptr) frame->accountsWindow->setAccounts(accounts); }
void TRemoteFrame::onPlayerText(const char* type, const char* account, const char* content, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (type == nullptr || account == nullptr) return;
    if (std::string(type) == "account") {
        if (frame->accountsWindow == nullptr) frame->accountsWindow = new TAccountsWindow();
        frame->accountsWindow->showEditor(frame->connection, account, content);
        return;
    }
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handlePlayerText(type, account, content);
}
void TRemoteFrame::onPlayerRights(const char* account, int rights, const char* ipRange, const char* folderAccess, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handlePlayerRights(account, rights, ipRange, folderAccess);
}
void TRemoteFrame::onPlayerAttributes(const char* account, const char* properties, const char* editorText, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handlePlayerAttributes(account, properties, editorText);
}
void TRemoteFrame::onBanData(const char* account, const char* computerId, const char* details, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handleBanData(account, computerId, details);
}
void TRemoteFrame::onBanListData(const char* type, const char* account, const char* content, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handleBanListData(type, account, content);
}

void TRemoteFrame::onFileBrowser(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->fileBrowser == nullptr) frame->fileBrowser = new TFileBrowserTree();
    frame->fileBrowser->open(frame->connection);
}

void TRemoteFrame::onClasses(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr || rc_is_nc_authenticated(frame->connection) == 0) return;
    if (frame->classList == nullptr) frame->classList = new TScriptList("classes");
    frame->classList->open(frame->connection);
}

void TRemoteFrame::onWeapons(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr || rc_is_nc_authenticated(frame->connection) == 0) return;
    if (frame->weaponList == nullptr) frame->weaponList = new TScriptList("weapons");
    frame->weaponList->open(frame->connection);
}

void TRemoteFrame::onNPCs(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr || rc_is_nc_authenticated(frame->connection) == 0) return;
    if (frame->npcList == nullptr) frame->npcList = new TNPCList();
    frame->npcList->open(frame->connection);
}

void TRemoteFrame::onLocalNPCDump(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr || rc_is_nc_authenticated(frame->connection) == 0) return;
    rc_on_local_npcs(frame->connection, onLocalNPCData, frame);
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Local NPCs", GTK_WINDOW(frame->window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "OK", GTK_RESPONSE_OK, nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_add(GTK_CONTAINER(content), grid);
    GtkWidget* label = gtk_label_new("Level:");
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), true);
    gtk_widget_set_hexpand(entry, true);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry, 1, 0, 1, 1);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    g_object_set_data(G_OBJECT(dialog), "level-entry", entry);
    g_signal_connect(dialog, "response", G_CALLBACK(onLocalNPCSubmit), frame);
    gtk_widget_show_all(dialog);
}

void TRemoteFrame::onLocalNPCSubmit(GtkDialog* dialog, gint response, gpointer data) {
    if (response != GTK_RESPONSE_OK) {
        gtk_widget_destroy(GTK_WIDGET(dialog));
        return;
    }
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    GtkWidget* entry = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(dialog), "level-entry"));
    const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
    gchar* level = g_ascii_strdown(text, -1);
    if (level[0] != '\0') rc_request_local_npcs(frame->connection, level);
    g_free(level);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void TRemoteFrame::onLocalNPCData(const char*, const char* content, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (content != nullptr && content[0] != '\0') frame->appendChat(content);
}

void TRemoteFrame::onServerOptions(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->serverOptionsEditor == nullptr) frame->serverOptionsEditor = new TServerTextEditor(TServerTextEditor::Kind::ServerOptions, "Server Options");
    frame->serverOptionsEditor->open(frame->connection);
}

void TRemoteFrame::onServerFlags(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->serverFlagsEditor == nullptr) frame->serverFlagsEditor = new TServerTextEditor(TServerTextEditor::Kind::ServerFlags, "Server Flags");
    frame->serverFlagsEditor->open(frame->connection);
}

void TRemoteFrame::onFolderConfig(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->folderConfigEditor == nullptr) frame->folderConfigEditor = new TServerTextEditor(TServerTextEditor::Kind::FolderConfig, "Folder Config");
    frame->folderConfigEditor->open(frame->connection);
}

gboolean TRemoteFrame::onGraphicalButton(GtkWidget* button, GdkEventButton* event, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    const int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "button-index"));
    std::string imageName = event->type == GDK_BUTTON_PRESS ? frame->options.buttonimagefilespressed[index] : frame->options.buttonimagefiles[index];
    if (event->type == GDK_BUTTON_PRESS && imageName == frame->options.buttonimagefiles[index]) {
        const std::size_t suffix = imageName.rfind("_normal");
        if (suffix != std::string::npos) {
            const std::string pressedName = imageName.substr(0, suffix) + "_pressed" + imageName.substr(suffix + 7);
            if (std::filesystem::exists(frame->applicationDirectory / "images" / pressedName)) imageName = pressedName;
        }
    }
    const std::filesystem::path imagePath = frame->applicationDirectory / "images" / imageName;
    GtkWidget* image = gtk_bin_get_child(GTK_BIN(button));
    gtk_image_set_from_file(GTK_IMAGE(image), imagePath.string().c_str());
    if (event->type == GDK_BUTTON_RELEASE) frame->graphicalAction(index);
    return true;
}

gboolean TRemoteFrame::onEditKey(GtkWidget*, GdkEventKey* event, gpointer data) {
    if (event->keyval != GDK_KEY_Return && event->keyval != GDK_KEY_KP_Enter) return false;
    static_cast<TRemoteFrame*>(data)->send();
    return true;
}

gboolean TRemoteFrame::onDelete(GtkWidget*, GdkEvent*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    gtk_widget_hide(frame->window);
    return true;
}

void TRemoteFrame::onGraphicalAllocate(GtkWidget*, GdkRectangle* allocation, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (allocation->width <= 0 || allocation->height <= 0) return;
    if (frame->backgroundPixbuf != nullptr) {
        GdkPixbuf* scaled = gdk_pixbuf_scale_simple(frame->backgroundPixbuf, allocation->width, 160, GDK_INTERP_BILINEAR);
        gtk_image_set_from_pixbuf(GTK_IMAGE(frame->backgroundImage), scaled);
        if (scaled != nullptr) g_object_unref(scaled);
    }
    gtk_widget_set_size_request(frame->backgroundImage, allocation->width, 160);
    const int positions[12][2] = {{5, 15}, {5, 48}, {38, 15}, {71, 15}, {394, 15}, {427, 15}, {460, 15}, {460, 48}, {460, 81}, {460, 114}, {427, 114}, {394, 114}};
    for (int index = 4; index < 12; ++index) gtk_fixed_move(GTK_FIXED(frame->graphicalFixed), frame->graphicalButtons[index], allocation->width - (500 - positions[index][0]), positions[index][1]);
    if (allocation->height > 128) gtk_widget_set_size_request(frame->notebook, allocation->width, allocation->height - 128);
}

gboolean TRemoteFrame::processEvents(gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection != nullptr) {
        rc_process_events(frame->connection);
        if (!frame->ncConnectionAttempted && rc_has_nc_server(frame->connection) != 0 && rc_is_nc_connected(frame->connection) == 0) {
            frame->ncConnectionAttempted = true;
            rc_connect_to_nc_server(frame->connection);
        }
        const bool npcServerConnected = rc_is_nc_authenticated(frame->connection) != 0;
        for (int index = 8; index < 12; ++index) {
            if (frame->graphicalButtons[index] == nullptr) continue;
            if (npcServerConnected) gtk_widget_show(frame->graphicalButtons[index]);
            else gtk_widget_hide(frame->graphicalButtons[index]);
        }
        if (frame->playersLabel != nullptr) {
            RCPlayer* players = nullptr;
            const int count = rc_get_players(frame->connection, &players);
            gtk_label_set_text(GTK_LABEL(frame->playersLabel), (frame->options.labelplayers + " " + std::to_string(count)).c_str());
        }
    }
    return G_SOURCE_CONTINUE;
}

void TRemoteFrame::onConnected(void*) {}

void TRemoteFrame::onDisconnected(const char* reason, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Connection Error", GTK_WINDOW(frame->window), GTK_DIALOG_MODAL, "OK", GTK_RESPONSE_OK, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 150);
    GtkWidget* message = gtk_label_new(reason == nullptr ? "Disconnected." : reason);
    gtk_label_set_line_wrap(GTK_LABEL(message), true);
    gtk_label_set_xalign(GTK_LABEL(message), 0.5F);
    gtk_label_set_yalign(GTK_LABEL(message), 0.5F);
    gtk_widget_set_size_request(message, 360, -1);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), message, true, true, 12);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    gtk_widget_hide(frame->window);
    frame->onCloseCallback();
}

void TRemoteFrame::onMessage(const char* message, void* data) { static_cast<TRemoteFrame*>(data)->appendChat(message == nullptr ? "" : message); }

void TRemoteFrame::onIrcMessage(const char* channel, const char* line, void* data) {
    static_cast<TRemoteFrame*>(data)->appendChannelMessage(channel == nullptr ? "" : channel, line == nullptr ? "" : line);
}

void TRemoteFrame::onPrivateMessage(int playerId, const char* account, const char* nick, const char* message, const char* type, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory);
    frame->playerList->notePrivateMessage(playerId, account, nick, message, type);
    rc3_begin_pm_tray_alert();
}

void TRemoteFrame::onServerData(const char* type, const char* content, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    const std::string value = content == nullptr ? "" : content;
    if (type != nullptr && std::string(type) == "toall" && frame->toallsWindow != nullptr) frame->toallsWindow->append(value.c_str());
    else if (type != nullptr && std::string(type) == "options" && frame->serverOptionsEditor != nullptr) frame->serverOptionsEditor->setContent(value.c_str());
    else if (type != nullptr && std::string(type) == "flags" && frame->serverFlagsEditor != nullptr) frame->serverFlagsEditor->setContent(value.c_str());
    else if (type != nullptr && std::string(type) == "folder_config" && frame->folderConfigEditor != nullptr) frame->folderConfigEditor->setContent(value.c_str());
}

gboolean TRemoteFrame::scrollChatToBottom(gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(frame->chatScrolled));
    gtk_adjustment_set_value(adjustment, gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment));
    return G_SOURCE_REMOVE;
}

void TRemoteFrame::addMenuItem(GtkWidget* menu, const char* label, GCallback callback) {
    GtkWidget* item = gtk_menu_item_new_with_label(label);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    if (callback != nullptr) g_signal_connect(item, "activate", callback, this);
}

void TRemoteFrame::graphicalAction(int index) {
    if (index == 0) onPlayerList(nullptr, this);
    else if (index == 1) onFileBrowser(nullptr, this);
    else if (index == 2) onAccounts(nullptr, this);
    else if (index == 3) onToalls(nullptr, this);
    else if (index == 4) onRCOptions(nullptr, this);
    else if (index == 9) onClasses(nullptr, this);
    else if (index == 10) onWeapons(nullptr, this);
    else if (index == 11) onNPCs(nullptr, this);
    else if (index == 8) onLocalNPCDump(nullptr, this);
    else if (index == 5) onServerFlags(nullptr, this);
    else if (index == 6) onFolderConfig(nullptr, this);
    else if (index == 7) onServerOptions(nullptr, this);
}

void TRemoteFrame::appendChat(const std::string& message) {
    const bool colorAlert = message.rfind("#ALERT", 0) == 0;
    std::string display = message;
    const bool alert = applyAlertTag(display);
    display = chatTimestamp(options) + display;
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chatField));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    const gint startOffset = gtk_text_iter_get_offset(&end);
    if (colorAlert) {
        GtkTextTag* tag = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.coloralert.c_str(), "weight", PANGO_WEIGHT_BOLD, nullptr);
        gtk_text_buffer_insert_with_tags(buffer, &end, (display + "\n").c_str(), -1, tag, nullptr);
    } else {
        std::size_t separator = display.find(':');
        if (!display.empty() && display.front() == '[') {
            const std::size_t timestampEnd = display.find(']');
            separator = timestampEnd == std::string::npos ? separator : display.find(':', timestampEnd + 1);
        }
        if (separator != std::string::npos && separator > 0) {
            const std::string prefix = display.substr(0, separator + 1);
            GtkTextTag* tag = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.colorchatbold.c_str(), "weight", PANGO_WEIGHT_BOLD, nullptr);
            gtk_text_buffer_insert_with_tags(buffer, &end, prefix.c_str(), -1, tag, nullptr);
            GtkTextIter textStart;
            gtk_text_buffer_get_end_iter(buffer, &textStart);
            const gint textStartOffset = gtk_text_iter_get_offset(&textStart);
            gtk_text_buffer_get_end_iter(buffer, &end);
            gtk_text_buffer_insert(buffer, &end, (display.substr(separator + 1) + "\n").c_str(), -1);
            applyEmotes(buffer, textStartOffset, display.substr(separator + 1));
        } else {
            gtk_text_buffer_insert(buffer, &end, (display + "\n").c_str(), -1);
            applyEmotes(buffer, startOffset, display);
        }
    }
    if (alert) {
        gtk_window_set_urgency_hint(GTK_WINDOW(window), true);
        gdk_beep();
    }
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(chatField), &end, 0.0, false, 0.0, 1.0);
    g_idle_add(scrollChatToBottom, this);
}

void TRemoteFrame::configureChatField(GtkWidget* field) {
    GdkColor chatBackgroundColor;
    GdkColor chatColor;
    gdk_color_parse(options.colorchatback.c_str(), &chatBackgroundColor);
    gdk_color_parse(options.colorchat.c_str(), &chatColor);
    gtk_widget_modify_base(field, GTK_STATE_NORMAL, &chatBackgroundColor);
    gtk_widget_modify_text(field, GTK_STATE_NORMAL, &chatColor);
    PangoFontDescription* chatFont = pango_font_description_from_string(("Sans " + std::to_string(options.chatfontsize)).c_str());
    gtk_widget_modify_font(field, chatFont);
    pango_font_description_free(chatFont);
    GtkCssProvider* provider = gtk_css_provider_new();
    const std::string css = "textview, textview text { background-color: " + options.colorchatback + "; color: " + options.colorchat + "; }";
    gtk_css_provider_load_from_data(provider, css.c_str(), -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(field), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);
}

void TRemoteFrame::applyEmotes(GtkTextBuffer* buffer, gint startOffset, const std::string& message) {
    const struct { const char* text; GdkPixbuf* pixbuf; } emotes[] = {{"Kappa", kappaEmote}, {"PMNormal", pmNormalEmote}, {":v", pacmanEmote}};
    for (const auto& emote : emotes) {
        if (emote.pixbuf == nullptr) continue;
        std::size_t position = message.find(emote.text);
        gint inserted = 0;
        while (position != std::string::npos) {
            GtkTextIter start;
            GtkTextIter end;
            gtk_text_buffer_get_iter_at_offset(buffer, &start, startOffset + static_cast<gint>(position) + inserted);
            gtk_text_buffer_get_iter_at_offset(buffer, &end, startOffset + static_cast<gint>(position + std::char_traits<char>::length(emote.text)) + inserted);
            GtkTextTag* hidden = gtk_text_buffer_create_tag(buffer, nullptr, "invisible", true, nullptr);
            gtk_text_buffer_apply_tag(buffer, hidden, &start, &end);
            gtk_text_buffer_insert_pixbuf(buffer, &start, emote.pixbuf);
            ++inserted;
            position = message.find(emote.text, position + std::char_traits<char>::length(emote.text));
        }
    }
}

void TRemoteFrame::appendChannelMessage(const std::string& channel, const std::string& message) {
    if (channel.empty()) {
        appendChat(message);
        return;
    }
    const bool colorAlert = message.rfind("#ALERT", 0) == 0;
    std::string display = message;
    const bool alert = applyAlertTag(display);
    display = chatTimestamp(options) + display;
    GtkWidget*& field = channelFields[channel];
    if (field == nullptr) {
        GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
        field = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(field), false);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(field), false);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(field), GTK_WRAP_WORD_CHAR);
        configureChatField(field);
        gtk_container_add(GTK_CONTAINER(scrolled), field);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new(channel.c_str()));
        gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(notebook), scrolled, true);
        gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), scrolled, true);
        gtk_widget_show_all(scrolled);
    }
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(field));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    const gint startOffset = gtk_text_iter_get_offset(&end);
    if (colorAlert) {
        GtkTextTag* tag = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.coloralert.c_str(), "weight", PANGO_WEIGHT_BOLD, nullptr);
        gtk_text_buffer_insert_with_tags(buffer, &end, (display + "\n").c_str(), -1, tag, nullptr);
    } else {
        std::size_t separator = display.find(':');
        if (!display.empty() && display.front() == '[') {
            const std::size_t timestampEnd = display.find(']');
            separator = timestampEnd == std::string::npos ? separator : display.find(':', timestampEnd + 1);
        }
        if (separator != std::string::npos && separator > 0) {
            const std::string prefix = display.substr(0, separator + 1);
            GtkTextTag* tag = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.colorchatbold.c_str(), "weight", PANGO_WEIGHT_BOLD, nullptr);
            gtk_text_buffer_insert_with_tags(buffer, &end, prefix.c_str(), -1, tag, nullptr);
            GtkTextIter textStart;
            gtk_text_buffer_get_end_iter(buffer, &textStart);
            const gint textStartOffset = gtk_text_iter_get_offset(&textStart);
            gtk_text_buffer_get_end_iter(buffer, &end);
            gtk_text_buffer_insert(buffer, &end, (display.substr(separator + 1) + "\n").c_str(), -1);
            applyEmotes(buffer, textStartOffset, display.substr(separator + 1));
        } else {
            gtk_text_buffer_insert(buffer, &end, (display + "\n").c_str(), -1);
            applyEmotes(buffer, startOffset, display);
        }
    }
    if (alert) {
        gtk_window_set_urgency_hint(GTK_WINDOW(window), true);
        gdk_beep();
    }
    GtkTextIter scrollEnd;
    gtk_text_buffer_get_end_iter(buffer, &scrollEnd);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(field), &scrollEnd, 0.0, false, 0.0, 1.0);
    GtkWidget* scrolled = gtk_widget_get_parent(field);
    if (GTK_IS_SCROLLED_WINDOW(scrolled)) {
        GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
        gtk_adjustment_set_value(adjustment, gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment));
    }
}

bool TRemoteFrame::applyAlertTag(std::string& message) {
    static const std::pair<const char*, bool> tags[] = {{"#ALERTSF", false}, {"#ALERTFS", false}, {"#ALERTSP", true}, {"#ALERTPS", true}, {"#ALERTF", true}, {"#ALERTP", true}, {"#ALERT", false}};
    for (const auto& [tag, sound] : tags) {
        const std::size_t length = std::char_traits<char>::length(tag);
        if (message.rfind(tag, 0) != 0) continue;
        message.erase(0, length);
        if (sound) return true;
        gtk_window_set_urgency_hint(GTK_WINDOW(window), true);
        return false;
    }
    return false;
}

void TRemoteFrame::send() {
    if (connection == nullptr) return;
    const std::string message = gtk_entry_get_text(GTK_ENTRY(editField));
    if (message.empty()) return;
    if (!rc_execute(connection, message.c_str())) appendChat(rc_last_error(connection));
    gtk_entry_set_text(GTK_ENTRY(editField), "");
}
