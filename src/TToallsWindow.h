#pragma once

#include <gtk/gtk.h>
#include <string>

class TToallsWindow {
public:
    TToallsWindow();
    ~TToallsWindow();
    void open(void* connection);
    void append(const char* message);
private:
    static void onSend(GtkEntry*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    GtkWidget* window = nullptr;
    GtkWidget* chat = nullptr;
    GtkWidget* entry = nullptr;
    void* connection = nullptr;
};
