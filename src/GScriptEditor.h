#pragma once

#include "RCOptions.h"

#include <gtk/gtk.h>

void configureGScriptEditor(GtkWidget* editor);
GtkWidget* createGScriptEditorLineStatus(GtkWidget* editor);
void setGScriptEditorOptions(const RC::RCOptions& options);
