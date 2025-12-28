#include "TLocalBanWindow.h"

#include <grclib.h>

TLocalBanWindow::TLocalBanWindow() {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "LocalBanWindow");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 320);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    bannedCheck = gtk_check_button_new_with_label("Banned");
    gtk_widget_set_name(bannedCheck, "BannedCheck");
    gtk_container_set_border_width(GTK_CONTAINER(bannedCheck), 5);
    gtk_box_pack_start(GTK_BOX(root), bannedCheck, false, false, 0);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
    gtk_container_set_border_width(GTK_CONTAINER(scrolled), 5);
    text = gtk_text_view_new();
    gtk_widget_set_name(text, "TextField");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text), 2);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text), 2);
    gtk_container_add(GTK_CONTAINER(scrolled), text);
    gtk_box_pack_start(GTK_BOX(root), scrolled, true, true, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_set_border_width(GTK_CONTAINER(buttons), 5);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* apply = gtk_button_new_with_label("Apply");
    GtkWidget* cancel = gtk_button_new_with_label("Cancel");
    gtk_container_add(GTK_CONTAINER(buttons), apply);
    gtk_container_add(GTK_CONTAINER(buttons), cancel);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 0);
    g_signal_connect(apply, "clicked", G_CALLBACK(onApply), this);
    g_signal_connect(cancel, "clicked", G_CALLBACK(onCancel), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TLocalBanWindow::~TLocalBanWindow() { if (window != nullptr) gtk_widget_destroy(window); }

void TLocalBanWindow::open(void* nextConnection, const std::string& nextAccount, const std::string& details) {
    connection = nextConnection;
    account = nextAccount;
    gtk_window_set_title(GTK_WINDOW(window), ("Edit Account of " + account).c_str());
    std::string content = details;
    const size_t marker = content.find("banned=");
    if (marker != std::string::npos) {
        const size_t end = content.find_first_of(",\n", marker);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bannedCheck), content.substr(marker + 7, end - marker - 7) == "1" || content.substr(marker + 7, end - marker - 7) == "true");
        content.erase(marker, (end == std::string::npos ? content.size() : end) - marker);
        if (!content.empty() && (content.front() == ',' || content.front() == '\n')) content.erase(0, 1);
    }
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
    gtk_text_buffer_set_text(buffer, content.c_str(), -1);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

void TLocalBanWindow::onApply(GtkButton*, gpointer data) {
    TLocalBanWindow* editor = static_cast<TLocalBanWindow*>(data);
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor->text));
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar* details = gtk_text_buffer_get_text(buffer, &start, &end, false);
    rc_set_legacy_player_ban(editor->connection, editor->account.c_str(), gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(editor->bannedCheck)), details == nullptr ? "" : details);
    g_free(details);
    gtk_widget_hide(editor->window);
}

void TLocalBanWindow::onCancel(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TLocalBanWindow*>(data)->window); }
gboolean TLocalBanWindow::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TLocalBanWindow*>(data)->window); return true; }
