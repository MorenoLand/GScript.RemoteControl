#pragma once

#include <gtk/gtk.h>
#include <string>

class TServerTextEditor {
public:
    enum class Kind { ServerOptions, ServerFlags, FolderConfig };
    TServerTextEditor(Kind kind, const char* title);
    ~TServerTextEditor();
    void open(void* connection);
    void hide();
    void setContent(const char* content);
    void setServerName(const std::string& server);
private:
    static void onSave(GtkButton*, gpointer data);
    static void onClose(GtkButton*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    void save();
    Kind kind;
    GtkWidget* window = nullptr;
    GtkTextBuffer* buffer = nullptr;
    void* connection = nullptr;
    std::string title;
};
