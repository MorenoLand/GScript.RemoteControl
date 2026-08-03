#pragma once

#include <gtk/gtk.h>
#include "TRCOptions.h"
#include "TExternalEditor.h"
#include <memory>
#include <string>

class TServerTextEditor {
public:
    enum class Kind { ServerOptions, ServerFlags, FolderConfig };
    TServerTextEditor(Kind kind, const char* title, RC::RCOptions* options);
    ~TServerTextEditor();
    void open(void* connection);
    void setConnection(void* connection);
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
    std::string serverName;
    RC::RCOptions* options = nullptr;
    std::unique_ptr<TExternalEditor> externalEditor;
    std::string externalWorkspace;
    std::string externalCommand;
};
