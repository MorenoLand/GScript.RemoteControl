#include "TRemoteFrame.h"
#include "RCOptions.h"
#include "TFileBrowserTree.h"
#include "TPlayerList.h"
#include "TScriptList.h"

#include <grclib.h>

TRemoteFrame::TRemoteFrame(const RC3::RCOptions& nextOptions, const std::filesystem::path& nextApplicationDirectory, std::function<void()> onClose) : onCloseCallback(std::move(onClose)), options(nextOptions), applicationDirectory(nextApplicationDirectory) {
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
            addMenuItem(menu, "Accounts");
            addMenuItem(menu, "Toalls");
        } else if (std::string(menuName) == "Files") {
            addMenuItem(menu, "File Browser", G_CALLBACK(onFileBrowser));
        } else if (std::string(menuName) == "Configuration") {
            addMenuItem(menu, "RC Options");
            addMenuItem(menu, "Server Options");
            addMenuItem(menu, "Folder Config");
        } else if (std::string(menuName) == "Scripts") {
            addMenuItem(menu, "NPCs");
            addMenuItem(menu, "Classes", G_CALLBACK(onClasses));
            addMenuItem(menu, "Weapons (GUI)", G_CALLBACK(onWeapons));
        } else {
            addMenuItem(menu, "Server Flags");
            addMenuItem(menu, "Level-NPC dump");
        }
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
        gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), item);
        }
        gtk_box_pack_start(GTK_BOX(root), menuBar, false, false, 0);
    } else {
        GtkWidget* fixed = gtk_fixed_new();
        graphicalFixed = fixed;
        gtk_widget_set_size_request(fixed, 500, 330);
        const std::filesystem::path background = applicationDirectory / "images" / options.background;
        GError* imageError = nullptr;
        GdkPixbuf* backgroundPixbuf = gdk_pixbuf_new_from_file_at_scale(background.string().c_str(), 500, 165, false, &imageError);
        GtkWidget* image = gtk_image_new_from_pixbuf(backgroundPixbuf);
        if (backgroundPixbuf != nullptr) g_object_unref(backgroundPixbuf);
        if (imageError != nullptr) g_error_free(imageError);
        gtk_widget_set_size_request(image, 500, 165);
        gtk_fixed_put(GTK_FIXED(fixed), image, 0, 0);
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
            GtkWidget* shadow = gtk_label_new(text.c_str());
            gtk_widget_modify_fg(shadow, GTK_STATE_NORMAL, &labelBackgroundColor);
            gtk_widget_modify_font(shadow, labelFont);
            gtk_fixed_put(GTK_FIXED(fixed), shadow, x + 1, y + 1);
            *front = gtk_label_new(text.c_str());
            gtk_widget_modify_fg(*front, GTK_STATE_NORMAL, &labelColor);
            gtk_widget_modify_font(*front, labelFont);
            gtk_fixed_put(GTK_FIXED(fixed), *front, x, y);
        };
        addLabel(options.labelservers, 10, 90, &serverLabel);
        addLabel(options.labelplayers, 10, 110, &playersLabel);
        pango_font_description_free(labelFont);
        gtk_box_pack_start(GTK_BOX(root), fixed, true, true, 0);
    }

    notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), true);
    GtkWidget* chatScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chatScrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(chatScrolled), GTK_SHADOW_NONE);
    chatField = gtk_text_view_new();
    gtk_widget_set_name(chatField, "ChatField");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(chatField), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(chatField), false);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chatField), GTK_WRAP_WORD_CHAR);
    configureChatField(chatField);
    gtk_container_add(GTK_CONTAINER(chatScrolled), chatField);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), chatScrolled, gtk_label_new("RC Chat"));
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
    delete playerList;
    delete fileBrowser;
    delete classList;
    delete weaponList;
}

void TRemoteFrame::open(void* nextConnection, const std::string& serverName) {
    connection = nextConnection;
    rc_on_connected(connection, onConnected, this);
    rc_on_disconnected(connection, onDisconnected, this);
    rc_on_message(connection, onMessage, this);
    rc_on_irc_message(connection, onIrcMessage, this);
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

void TRemoteFrame::onSend(GtkButton*, gpointer data) { static_cast<TRemoteFrame*>(data)->send(); }

void TRemoteFrame::onPlayerList(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList();
    frame->playerList->open(frame->connection);
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
    frame->onCloseCallback();
    return true;
}

gboolean TRemoteFrame::processEvents(gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection != nullptr) {
        rc_process_events(frame->connection);
        const gint64 now = g_get_monotonic_time();
        if (rc_is_nc_connected(frame->connection) == 0 && now >= frame->nextNcConnectAttempt) {
            rc_connect_to_nc_server(frame->connection);
            frame->nextNcConnectAttempt = now + G_TIME_SPAN_SECOND;
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
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(frame->window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", reason == nullptr ? "Disconnected." : reason);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void TRemoteFrame::onMessage(const char* message, void* data) { static_cast<TRemoteFrame*>(data)->appendChat(message == nullptr ? "" : message); }

void TRemoteFrame::onIrcMessage(const char* channel, const char* line, void* data) {
    static_cast<TRemoteFrame*>(data)->appendChannelMessage(channel == nullptr ? "" : channel, line == nullptr ? "" : line);
}

void TRemoteFrame::addMenuItem(GtkWidget* menu, const char* label, GCallback callback) {
    GtkWidget* item = gtk_menu_item_new_with_label(label);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    if (callback != nullptr) g_signal_connect(item, "activate", callback, this);
}

void TRemoteFrame::graphicalAction(int index) {
    if (index == 0) onPlayerList(nullptr, this);
    else if (index == 1) onFileBrowser(nullptr, this);
    else if (index == 9) onClasses(nullptr, this);
    else if (index == 10) onWeapons(nullptr, this);
}

void TRemoteFrame::appendChat(const std::string& message) {
    const bool colorAlert = message.rfind("#ALERT", 0) == 0;
    std::string display = message;
    const bool alert = applyAlertTag(display);
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chatField));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    if (colorAlert) {
        GtkTextTag* tag = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.coloralert.c_str(), "weight", PANGO_WEIGHT_BOLD, nullptr);
        gtk_text_buffer_insert_with_tags(buffer, &end, (display + "\n").c_str(), -1, tag, nullptr);
    } else gtk_text_buffer_insert(buffer, &end, (display + "\n").c_str(), -1);
    if (alert) {
        gtk_window_set_urgency_hint(GTK_WINDOW(window), true);
        gdk_beep();
    }
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

void TRemoteFrame::appendChannelMessage(const std::string& channel, const std::string& message) {
    if (channel.empty()) {
        appendChat(message);
        return;
    }
    const bool colorAlert = message.rfind("#ALERT", 0) == 0;
    std::string display = message;
    const bool alert = applyAlertTag(display);
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
        gtk_widget_show_all(scrolled);
    }
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(field));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    if (colorAlert) {
        GtkTextTag* tag = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.coloralert.c_str(), "weight", PANGO_WEIGHT_BOLD, nullptr);
        gtk_text_buffer_insert_with_tags(buffer, &end, (display + "\n").c_str(), -1, tag, nullptr);
    } else gtk_text_buffer_insert(buffer, &end, (display + "\n").c_str(), -1);
    if (alert) {
        gtk_window_set_urgency_hint(GTK_WINDOW(window), true);
        gdk_beep();
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
