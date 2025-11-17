#include "RCOptions.h"
#include "TServerList.h"
#include "TStartFrame.h"

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
    TStartFrame* startFrame = nullptr;
    TServerList serverList([&] { startFrame->show(); });
    TStartFrame frame(options, applicationDirectory, [&](const std::string& account, const std::string& password) { serverList.open(account, password); });
    startFrame = &frame;
    frame.show();
    gtk_main();
    return 0;
}
