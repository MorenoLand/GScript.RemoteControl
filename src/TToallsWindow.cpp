#include "TToallsWindow.h"

#include <grclib.h>

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
    gtk_widget_set_name(chat, "ChatField");
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
    GtkWidget* close = gtk_button_new_with_label("Close");
    gtk_widget_set_size_request(close, 80, 24);
    gtk_container_add(GTK_CONTAINER(buttons), close);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 0);
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, "window#ToallsWindow, window#ToallsWindow box, window#ToallsWindow scrolledwindow, window#ToallsWindow viewport, window#ToallsWindow textview.view, window#ToallsWindow textview.view text { background-color: #1e1e1e; color: #d4d4d4; } window#ToallsWindow entry { background-color: #1e1e1e; color: #00ff00; }", -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);
    GdkColor chatBackground;
    GdkColor chatText;
    GdkColor editBackground;
    GdkColor editText;
    gdk_color_parse("#1e1e1e", &chatBackground);
    gdk_color_parse("#d4d4d4", &chatText);
    gdk_color_parse("#1e1e1e", &editBackground);
    gdk_color_parse("#00ff00", &editText);
    gtk_widget_modify_base(chat, GTK_STATE_NORMAL, &chatBackground);
    gtk_widget_modify_text(chat, GTK_STATE_NORMAL, &chatText);
    GdkRGBA chatBackgroundRgba;
    gdk_rgba_parse(&chatBackgroundRgba, "#1e1e1e");
    gtk_widget_override_background_color(chat, GTK_STATE_FLAG_NORMAL, &chatBackgroundRgba);
    gtk_widget_modify_base(entry, GTK_STATE_NORMAL, &editBackground);
    gtk_widget_modify_text(entry, GTK_STATE_NORMAL, &editText);
    g_signal_connect(entry, "activate", G_CALLBACK(onSend), this);
    g_signal_connect(close, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

TToallsWindow::~TToallsWindow() { if (window != nullptr) gtk_widget_destroy(window); }
void TToallsWindow::open(void* nextConnection) { connection = nextConnection; gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); gtk_widget_grab_focus(entry); }
void TToallsWindow::append(const char* message) { GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat)); GtkTextIter end; gtk_text_buffer_get_end_iter(buffer, &end); gtk_text_buffer_insert(buffer, &end, (std::string(message == nullptr ? "" : message) + "\n").c_str(), -1); gtk_text_buffer_get_end_iter(buffer, &end); gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(chat), &end, 0.0, false, 0.0, 1.0); }
void TToallsWindow::onSend(GtkEntry*, gpointer data) { TToallsWindow* window = static_cast<TToallsWindow*>(data); const char* message = gtk_entry_get_text(GTK_ENTRY(window->entry)); if (message == nullptr || *message == '\0') return; if (rc_send_toall_message(window->connection, message)) { window->append(message); gtk_entry_set_text(GTK_ENTRY(window->entry), ""); } }
void TToallsWindow::onClose(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TToallsWindow*>(data)->window); }
gboolean TToallsWindow::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TToallsWindow*>(data)->window); return true; }
