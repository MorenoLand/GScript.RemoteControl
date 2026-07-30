#pragma once

#include <gtk/gtk.h>
#include <string>

class TLevelList {
public:
    explicit TLevelList(GtkWindow* parent);
    ~TLevelList();
    void open(void* connection);
    void hide();
    void setServerName(const std::string& server);
    void setContent(const char* content);
private:
    static void onApply(GtkButton*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void apply();
    GtkWidget* window = nullptr;
    GtkTextBuffer* buffer = nullptr;
    void* connection = nullptr;
    std::string serverName;
};
