#pragma once

#include "TRCOptions.h"

#include <functional>
#include <filesystem>
#include <string>
#include <vector>
#include <gtk/gtk.h>

void configureGScriptEditor(GtkWidget* editor, bool script = true);
void setGScriptEditorContent(GtkTextBuffer* buffer, const char* content, gint length = -1);
GtkWidget* wrapGScriptEditor(GtkWidget* editor, GtkWidget* scrolled);
bool consumeEditorCtrlS(GtkWidget* editor, GdkEventKey* event);
gboolean releaseEditorCtrlS(GtkWidget* editor, GdkEventKey* event);
GtkWidget* createGScriptEditorLineStatus(GtkWidget* editor);
void addGScriptEditorLineStatus(GtkDialog* dialog, GtkWidget* editor);
void setGScriptEditorOptions(const RC::RCOptions& options);
void refreshGScriptEditorTheme();
void setGScriptEditorCacheDirectory(const std::filesystem::path& directory);
void setGScriptEditorConnection(GtkWidget* editor, void* connection);
void updateGScriptEditorPlayerProperty(void* connection, int playerId, const char* property, const char* value);
void requestGScriptHelp(const std::string& query, std::function<void(std::vector<std::string>)> callback);
