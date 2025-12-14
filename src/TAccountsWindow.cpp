#include "TAccountsWindow.h"
#include <grclib.h>
#include <sstream>

TAccountsWindow::TAccountsWindow() {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "GetAccountsWindow");
    gtk_window_set_title(GTK_WINDOW(window), "Get Accounts List");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* frame = gtk_frame_new(" Account specification ");
    gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
    GtkWidget* fields = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(fields), 3);
    gtk_container_add(GTK_CONTAINER(frame), fields);
    gtk_grid_attach(GTK_GRID(fields), gtk_label_new("Account name spec:"), 0, 0, 1, 1);
    accountField = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(fields), accountField, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(fields), gtk_label_new("Conditions:"), 0, 1, 1, 1);
    conditionsField = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(fields), conditionsField, 1, 1, 1, 1);
    GtkWidget* examples = gtk_label_new("example: Stef%\nVariables: email='xxx', adminlevel=1, adminworlds like '%all%', blocked=1");
    gtk_label_set_xalign(GTK_LABEL(examples), 0.0F);
    gtk_label_set_line_wrap(GTK_LABEL(examples), true);
    gtk_grid_attach(GTK_GRID(fields), examples, 0, 2, 2, 1);
    gtk_box_pack_start(GTK_BOX(root), frame, false, false, 0);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    store = gtk_list_store_new(1, G_TYPE_STRING);
    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column_new_with_attributes("Account", renderer, "text", 0, nullptr));
    gtk_container_add(GTK_CONTAINER(scrolled), tree);
    gtk_box_pack_start(GTK_BOX(root), scrolled, true, true, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_set_border_width(GTK_CONTAINER(buttons), 5);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* getList = gtk_button_new_with_label("Get List");
    GtkWidget* close = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), getList);
    gtk_container_add(GTK_CONTAINER(buttons), close);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, false, 0);
    g_signal_connect(getList, "clicked", G_CALLBACK(onGetList), this);
    g_signal_connect(close, "clicked", G_CALLBACK(onClose), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}
TAccountsWindow::~TAccountsWindow() { if (window != nullptr) gtk_widget_destroy(window); }
void TAccountsWindow::open(void* nextConnection) { connection = nextConnection; gtk_widget_show_all(window); gtk_window_present(GTK_WINDOW(window)); gtk_widget_grab_focus(accountField); }
void TAccountsWindow::setAccounts(const char* accounts) { gtk_list_store_clear(store); std::istringstream input(accounts == nullptr ? "" : accounts); for (std::string account; std::getline(input, account);) { GtkTreeIter row; gtk_list_store_append(store, &row); gtk_list_store_set(store, &row, 0, account.c_str(), -1); } }
void TAccountsWindow::onGetList(GtkButton*, gpointer data) { TAccountsWindow* window = static_cast<TAccountsWindow*>(data); rc_request_account_list(window->connection, gtk_entry_get_text(GTK_ENTRY(window->accountField)), gtk_entry_get_text(GTK_ENTRY(window->conditionsField))); }
void TAccountsWindow::onClose(GtkButton*, gpointer data) { gtk_widget_hide(static_cast<TAccountsWindow*>(data)->window); }
gboolean TAccountsWindow::onDelete(GtkWidget*, GdkEvent*, gpointer data) { gtk_widget_hide(static_cast<TAccountsWindow*>(data)->window); return true; }
