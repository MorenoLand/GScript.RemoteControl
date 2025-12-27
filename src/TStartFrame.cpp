#include "TStartFrame.h"

#include <utility>

TStartFrame::TStartFrame(RC3::RCOptions& options, const std::filesystem::path& applicationDirectory, ConnectCallback onConnect)
    : options(options), applicationDirectory(applicationDirectory), onConnectCallback(std::move(onConnect)) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "StartFrame");
    gtk_window_set_title(GTK_WINDOW(window), "Graal RemoteControl");
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(window), 280, 220);
    gtk_window_set_resizable(GTK_WINDOW(window), true);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);

    GtkWidget* frame = gtk_frame_new(" Options ");
    gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
    gtk_box_pack_start(GTK_BOX(root), frame, true, true, 0);

    GtkWidget* optionsBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_container_set_border_width(GTK_CONTAINER(optionsBox), 5);
    gtk_container_add(GTK_CONTAINER(frame), optionsBox);

    auto addField = [optionsBox](const char* label, GtkWidget*& field, bool password) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        GtkWidget* caption = gtk_label_new(label);
        gtk_widget_set_size_request(caption, 80, -1);
        gtk_label_set_xalign(GTK_LABEL(caption), 0.0F);
        field = gtk_entry_new();
        if (password) gtk_entry_set_visibility(GTK_ENTRY(field), false);
        gtk_box_pack_start(GTK_BOX(row), caption, false, false, 0);
        gtk_box_pack_end(GTK_BOX(row), field, true, true, 0);
        gtk_box_pack_start(GTK_BOX(optionsBox), row, false, true, 0);
    };

    addField("Nickname:", nicknameField, false);

    GtkWidget* accountRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget* accountLabel = gtk_label_new("Account:");
    gtk_widget_set_size_request(accountLabel, 80, -1);
    gtk_label_set_xalign(GTK_LABEL(accountLabel), 0.0F);
    accountCombo = gtk_combo_box_text_new_with_entry();
    gtk_widget_set_name(accountCombo, "AccountCombo");
    accountField = gtk_bin_get_child(GTK_BIN(accountCombo));
    gtk_widget_set_name(accountField, "AccountField");
    gtk_box_pack_start(GTK_BOX(accountRow), accountLabel, false, false, 0);
    gtk_box_pack_end(GTK_BOX(accountRow), accountCombo, true, true, 0);
    gtk_box_pack_start(GTK_BOX(optionsBox), accountRow, false, true, 0);

    addField("Password:", passwordField, true);
    gtk_widget_set_name(nicknameField, "NicknameField");
    gtk_widget_set_name(passwordField, "PasswordField");
    gtk_entry_set_text(GTK_ENTRY(nicknameField), options.nickname.c_str());
    for (const std::string& accountName : accounts.names()) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(accountCombo), accountName.c_str());
    gtk_entry_set_text(GTK_ENTRY(accountField), accounts.accountName().c_str());
    gtk_entry_set_text(GTK_ENTRY(passwordField), accounts.password().c_str());

    passwordCheck = gtk_check_button_new_with_label("Don't save password");
    gtk_widget_set_name(passwordCheck, "PasswordCheck");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(passwordCheck), options.dontsavepassword);
    gtk_box_pack_start(GTK_BOX(optionsBox), passwordCheck, false, false, 0);

    graphicsCheck = gtk_check_button_new_with_label("Graphical Menu");
    gtk_widget_set_name(graphicsCheck, "GraphicsCheck");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(graphicsCheck), options.graphicalmenu);
    gtk_box_pack_start(GTK_BOX(optionsBox), graphicsCheck, false, false, 0);

    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_set_border_width(GTK_CONTAINER(buttons), 5);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* connectButton = gtk_button_new_from_stock(GTK_STOCK_OK);
    GtkWidget* cancelButton = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
    gtk_container_add(GTK_CONTAINER(buttons), connectButton);
    gtk_container_add(GTK_CONTAINER(buttons), cancelButton);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, true, 0);

    g_signal_connect(connectButton, "clicked", G_CALLBACK(TStartFrame::onConnect), this);
    g_signal_connect(accountCombo, "changed", G_CALLBACK(TStartFrame::onAccountChanged), this);
    g_signal_connect(cancelButton, "clicked", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
}

TStartFrame::~TStartFrame() {
    if (window != nullptr) gtk_widget_destroy(window);
}

void TStartFrame::show() {
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

void TStartFrame::onConnect(GtkButton*, gpointer data) { static_cast<TStartFrame*>(data)->connect(); }

void TStartFrame::onAccountChanged(GtkComboBox*, gpointer data) {
    TStartFrame* frame = static_cast<TStartFrame*>(data);
    gtk_entry_set_text(GTK_ENTRY(frame->passwordField), frame->accounts.passwordFor(frame->getText(frame->accountField)).c_str());
}

gboolean TStartFrame::onDelete(GtkWidget*, GdkEvent*, gpointer) { return false; }

void TStartFrame::connect() {
    options.nickname = getText(nicknameField);
    options.dontsavepassword = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(passwordCheck));
    options.graphicalmenu = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(graphicsCheck));
    RC3::saveRCOptions(options, applicationDirectory);
    accounts.save(getText(accountField), getText(passwordField), options.dontsavepassword);
    gtk_widget_hide(window);
    onConnectCallback(getText(accountField), getText(passwordField));
}

std::string TStartFrame::getText(GtkWidget* widget) const { return gtk_entry_get_text(GTK_ENTRY(widget)); }
