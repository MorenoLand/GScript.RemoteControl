#include "TRemoteFrame.h"
#include "RCOptions.h"
#include "TFileBrowserTree.h"
#include "TPlayerList.h"

#include <grclib.h>

TRemoteFrame::TRemoteFrame(const RC3::RCOptions& nextOptions, const std::filesystem::path& nextApplicationDirectory, std::function<void()> onClose) : onCloseCallback(std::move(onClose)), options(nextOptions), applicationDirectory(nextApplicationDirectory) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "RemoteFrame");
    gtk_window_set_title(GTK_WINDOW(window), "Remote Control");
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
            addMenuItem(menu, "Classes");
            addMenuItem(menu, "Weapons (GUI)");
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
        gtk_widget_set_size_request(fixed, 500, 136);
        const std::filesystem::path background = applicationDirectory / "images" / options.background;
        GtkWidget* image = gtk_image_new_from_file(background.string().c_str());
        gtk_widget_set_size_request(image, 500, 160);
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
        }
        serverLabel = gtk_label_new(options.labelservers.c_str());
        playersLabel = gtk_label_new(options.labelplayers.c_str());
        gtk_fixed_put(GTK_FIXED(fixed), serverLabel, 10, 90);
        gtk_fixed_put(GTK_FIXED(fixed), playersLabel, 10, 110);
        gtk_box_pack_start(GTK_BOX(root), fixed, false, false, 0);
    }

    notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), true);
    GtkWidget* chatScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chatScrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    chatField = gtk_text_view_new();
    gtk_widget_set_name(chatField, "ChatField");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(chatField), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(chatField), false);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chatField), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(chatScrolled), chatField);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), chatScrolled, gtk_label_new("RC Chat"));
    gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);

    editField = gtk_entry_new();
    gtk_widget_set_name(editField, "EditField");
    gtk_box_pack_start(GTK_BOX(root), editField, false, false, 0);

    g_signal_connect(editField, "key-press-event", G_CALLBACK(onEditKey), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TRemoteFrame::~TRemoteFrame() {
    if (eventSource != 0) g_source_remove(eventSource);
    if (window != nullptr) gtk_widget_destroy(window);
    delete playerList;
    delete fileBrowser;
}

void TRemoteFrame::open(void* nextConnection) {
    connection = nextConnection;
    rc_on_connected(connection, onConnected, this);
    rc_on_disconnected(connection, onDisconnected, this);
    rc_on_message(connection, onMessage, this);
    rc_on_irc_message(connection, onIrcMessage, this);
    if (eventSource == 0) eventSource = g_timeout_add(50, processEvents, this);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(editField);
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

gboolean TRemoteFrame::onGraphicalButton(GtkWidget* button, GdkEventButton* event, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    const int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "button-index"));
    const std::filesystem::path imagePath = frame->applicationDirectory / "images" / (event->type == GDK_BUTTON_PRESS ? frame->options.buttonimagefilespressed[index] : frame->options.buttonimagefiles[index]);
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
    if (frame->connection != nullptr) rc_process_events(frame->connection);
    return G_SOURCE_CONTINUE;
}

void TRemoteFrame::onConnected(void*) {}

void TRemoteFrame::onDisconnected(const char* reason, void* data) { static_cast<TRemoteFrame*>(data)->appendChat(reason == nullptr ? "Disconnected." : reason); }

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
}

void TRemoteFrame::appendChat(const std::string& message) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chatField));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, (message + "\n").c_str(), -1);
}

void TRemoteFrame::appendChannelMessage(const std::string& channel, const std::string& message) {
    if (channel.empty()) {
        appendChat(message);
        return;
    }
    GtkWidget*& field = channelFields[channel];
    if (field == nullptr) {
        GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
        field = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(field), false);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(field), false);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(field), GTK_WRAP_WORD_CHAR);
        gtk_container_add(GTK_CONTAINER(scrolled), field);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new(channel.c_str()));
        gtk_widget_show_all(scrolled);
    }
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(field));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, (message + "\n").c_str(), -1);
}

void TRemoteFrame::send() {
    if (connection == nullptr) return;
    const std::string message = gtk_entry_get_text(GTK_ENTRY(editField));
    if (message.empty()) return;
    if (!rc_execute(connection, message.c_str())) appendChat(rc_last_error(connection));
    gtk_entry_set_text(GTK_ENTRY(editField), "");
}
