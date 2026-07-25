#pragma once

#include "RCOptions.h"

#include <functional>
#include <filesystem>
#include <string>
#include <vector>
#include <gtk/gtk.h>

void configureGScriptEditor(GtkWidget* editor);
bool consumeEditorCtrlS(GtkWidget* editor, GdkEventKey* event);
gboolean releaseEditorCtrlS(GtkWidget* editor, GdkEventKey* event);
GtkWidget* createGScriptEditorLineStatus(GtkWidget* editor);
void addGScriptEditorLineStatus(GtkDialog* dialog, GtkWidget* editor);
void setGScriptEditorOptions(const RC::RCOptions& options);
void refreshGScriptEditorTheme();
void setGScriptEditorCacheDirectory(const std::filesystem::path& directory);
void requestGScriptHelp(const std::string& query, std::function<void(std::vector<std::string>)> callback);
