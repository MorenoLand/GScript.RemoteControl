#include "RCOptions.h"

#include <filesystem>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

namespace {

    void copySyntaxFiles(const std::filesystem::path& applicationDirectory) {
        const auto languageSpecsDirectory = (applicationDirectory / "language-specs").string();
        const char* searchPaths[] = {languageSpecsDirectory.c_str(), nullptr};
        gtk_source_language_manager_set_search_path(gtk_source_language_manager_get_default(), searchPaths);
        gtk_source_style_scheme_manager_append_search_path(gtk_source_style_scheme_manager_get_default(), languageSpecsDirectory.c_str());
    }

}

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    RC3::RCOptions options;
    const std::filesystem::path applicationDirectory = std::filesystem::current_path();
    copySyntaxFiles(applicationDirectory);
    RC3::loadRCOptions(options, applicationDirectory);
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "RemoteControl3");
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    gtk_widget_show_all(window);
    gtk_main();
    RC3::saveRCOptions(options, applicationDirectory);
    return 0;
}
