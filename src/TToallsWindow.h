#pragma once

#include <gtk/gtk.h>
#include <string>
#include <vector>

class TToallsWindow {
public:
    TToallsWindow();
    ~TToallsWindow();
    void open(void* connection, const std::string& sender);
    void append(const char* message);
    void setServerName(const std::string& server);
private:
    static void onSend(GtkEntry*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    GtkWidget* window = nullptr;
    GtkWidget* chat = nullptr;
    GtkWidget* entry = nullptr;
    void* connection = nullptr;
    std::string sender;
    std::string serverName;
    std::vector<std::string> pendingMessages;
};
