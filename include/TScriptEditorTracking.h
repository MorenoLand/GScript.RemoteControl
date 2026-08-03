#pragma once

#include <gtk/gtk.h>
#include <string>
#include <vector>

struct ScriptEditorSnapshot { std::string name; std::string text; std::string selection; bool active = false; bool modified = false; };

void trackScriptEditor(GtkWidget* dialog, GtkTextBuffer* buffer, const std::string& name, const std::string& original, void* connection = nullptr);
void detachScriptEditorConnection(void* connection);
void rebindScriptEditorConnection(void* disconnectedConnection, void* connection);
void* scriptEditorConnection(GtkTextBuffer* buffer);
void markScriptEditorSaved(GtkTextBuffer* buffer);
std::vector<std::string> unsavedScriptEditors();
std::vector<ScriptEditorSnapshot> scriptEditorSnapshots();
bool writeScriptEditor(const std::string& name, const std::string& text, std::string& error);
bool replaceScriptEditorText(const std::string& name, const std::string& expected, const std::string& replacement, std::string& error);
bool saveScriptEditor(const std::string& name, std::string& error);
