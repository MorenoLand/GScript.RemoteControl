#include "TRemoteFrame.h"
#include "TFileBrowser.h"
#include "TPlayerList.h"

#include <grclib.h>

TRemoteFrame::TRemoteFrame(std::function<void()> onClose) : onCloseCallback(std::move(onClose)) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "RemoteFrame");
    gtk_window_set_title(GTK_WINDOW(window), "Remote Control");
    gtk_window_set_default_size(GTK_WINDOW(window), 700, 500);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);

    GtkWidget* menuBar = gtk_menu_bar_new();
    const char* menuNames[] = {"Players", "Files", "Configuration", "Scripts", "Misc"};
    for (const char* menuName : menuNames) {
        GtkWidget* item = gtk_menu_item_new_with_label(menuName);
        GtkWidget* menu = gtk_menu_new();
        if (std::string(menuName) == "Players") {
            GtkWidget* playerListItem = gtk_menu_item_new_with_label("Playerlist");
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), playerListItem);
            g_signal_connect(playerListItem, "activate", G_CALLBACK(onPlayerList), this);
        } else if (std::string(menuName) == "Files") {
            GtkWidget* fileBrowserItem = gtk_menu_item_new_with_label("File Browser");
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), fileBrowserItem);
            g_signal_connect(fileBrowserItem, "activate", G_CALLBACK(onFileBrowser), this);
        }
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
        gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), item);
    }
    gtk_box_pack_start(GTK_BOX(root), menuBar, false, false, 0);

    GtkWidget* status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(status), 5);
    serverLabel = gtk_label_new("Server:");
    playersLabel = gtk_label_new("Players:");
    gtk_label_set_xalign(GTK_LABEL(serverLabel), 0.0F);
    gtk_label_set_xalign(GTK_LABEL(playersLabel), 0.0F);
    gtk_box_pack_start(GTK_BOX(status), serverLabel, true, true, 0);
    gtk_box_pack_start(GTK_BOX(status), playersLabel, true, true, 0);
    gtk_box_pack_start(GTK_BOX(root), status, false, false, 0);

    GtkWidget* chatScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chatScrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    chatField = gtk_text_view_new();
    gtk_widget_set_name(chatField, "ChatField");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(chatField), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(chatField), false);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chatField), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(chatScrolled), chatField);
    gtk_box_pack_start(GTK_BOX(root), chatScrolled, true, true, 0);

    GtkWidget* input = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(input), 5);
    editField = gtk_entry_new();
    gtk_widget_set_name(editField, "EditField");
    GtkWidget* sendButton = gtk_button_new_with_label("Send");
    gtk_box_pack_start(GTK_BOX(input), editField, true, true, 0);
    gtk_box_pack_start(GTK_BOX(input), sendButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(root), input, false, false, 0);

    g_signal_connect(sendButton, "clicked", G_CALLBACK(onSend), this);
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
    if (eventSource == 0) eventSource = g_timeout_add(50, processEvents, this);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(editField);
    appendChat("Connecting...");
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
    if (frame->fileBrowser == nullptr) frame->fileBrowser = new TFileBrowser();
    frame->fileBrowser->open(frame->connection);
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

void TRemoteFrame::onConnected(void* data) { static_cast<TRemoteFrame*>(data)->appendChat("Connected."); }

void TRemoteFrame::onDisconnected(const char* reason, void* data) { static_cast<TRemoteFrame*>(data)->appendChat(reason == nullptr ? "Disconnected." : reason); }

void TRemoteFrame::onMessage(const char* message, void* data) { static_cast<TRemoteFrame*>(data)->appendChat(message == nullptr ? "" : message); }

void TRemoteFrame::appendChat(const std::string& message) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chatField));
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
