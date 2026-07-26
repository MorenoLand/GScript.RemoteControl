#pragma once

#include <gtk/gtk.h>

#include <cctype>
#include <string>

inline std::string treeSearchLower(const char* value) {
    std::string result = value == nullptr ? "" : value;
    for (char& character : result) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return result;
}

inline gboolean treeSearchContains(GtkTreeModel* model, gint column, const gchar* key, GtkTreeIter* iter, gpointer) {
    gchar* value = nullptr;
    gtk_tree_model_get(model, iter, column, &value, -1);
    const std::string query = treeSearchLower(key);
    const std::string candidate = treeSearchLower(value);
    g_free(value);
    return candidate.find(query) == std::string::npos;
}
