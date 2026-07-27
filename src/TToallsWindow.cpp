#include "TToallsWindow.h"
#include "TDebug.h"

#include <grclib.h>

#include <algorithm>

TToallsWindow::TToallsWindow() {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "ToallsWindow");
    gtk_window_set_title(GTK_WINDOW(window), "Toalls");
    gtk_window_set_default_size(GTK_WINDOW(window), 360, 240);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
    chat = gtk_text_view_new();
    gtk_widget_set_name(chat, "ToallsChat");
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(chat), 5);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(chat), 5);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(chat), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(chat), false);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chat), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(scrolled), chat);
    gtk_box_pack_start(GTK_BOX(root), scrolled, true, true, 0);
    entry = gtk_entry_new();
    gtk_widget_set_name(entry, "EditField");
    gtk_box_pack_start(GTK_BOX(root), entry, false, false, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_set_border_width(GTK_CONTAINER(buttons), 5);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* close = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
    gtk_button_set_always_show_image(GTK_BUTTON(close), true);
    gtk_widget_set_size_request(close, 80, 24);
    gtk_container_add(GTK_CONTAINER(buttons), close);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 0);
    g_signal_connect(entry, "activate", G_CALLBACK(onSend), this);
    g_signal_connect(close, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TToallsWindow::~TToallsWindow() { if (window != nullptr) gtk_widget_destroy(window); }
void TToallsWindow::setServerName(const std::string& server) { serverName = server; gtk_window_set_title(GTK_WINDOW(window), serverName.empty() ? "Toalls" : ("Toalls - " + serverName).c_str()); }
void TToallsWindow::open(void* nextConnection, const std::string& nextSender) { connection = nextConnection; sender = nextSender; gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); gtk_widget_grab_focus(entry); }
void TToallsWindow::append(const char* message) {
    const std::string line = message == nullptr ? "" : message;
    const auto pending = std::find_if(pendingMessages.begin(), pendingMessages.end(), [&](const std::string& value) { return line == value || (!sender.empty() && line == sender + ": " + value); });
    if (pending != pendingMessages.end()) { pendingMessages.erase(pending); return; }
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, (line + "\n").c_str(), -1);
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(chat), &end, 0.0, false, 0.0, 1.0);
}
void TToallsWindow::onSend(GtkEntry*, gpointer data) {
    TToallsWindow* window = static_cast<TToallsWindow*>(data);
    const char* message = gtk_entry_get_text(GTK_ENTRY(window->entry));
    if (message == nullptr || *message == '\0') return;
    if (!rc_send_toall_message(window->connection, message)) {
        remoteControlDebugLog("toall send failed (authenticated=%d): %s", rc_is_authenticated(window->connection), rc_last_error(window->connection));
        return;
    }
    const std::string line = (window->sender.empty() ? std::string("You: ") : window->sender + ": ") + message;
    window->append(line.c_str());
    window->pendingMessages.emplace_back(message);
    gtk_entry_set_text(GTK_ENTRY(window->entry), "");
}
void TToallsWindow::onClose(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TToallsWindow*>(data)->window); }
gboolean TToallsWindow::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TToallsWindow*>(data)->window); return true; }
