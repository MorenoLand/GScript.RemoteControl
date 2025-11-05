#include "RCOptions.h"
#include "TRemoteFrame.h"
#include "TServerList.h"
#include "TStartFrame.h"

#include <array>
#include <filesystem>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

    void copySyntaxFiles(const std::filesystem::path& applicationDirectory) {
        const auto languageSpecsDirectory = (applicationDirectory / "language-specs").string();
        gchar* searchPaths[] = {const_cast<gchar*>(languageSpecsDirectory.c_str()), nullptr};
        gtk_source_language_manager_set_search_path(gtk_source_language_manager_get_default(), searchPaths);
        gtk_source_style_scheme_manager_append_search_path(gtk_source_style_scheme_manager_get_default(), languageSpecsDirectory.c_str());
    }

    std::filesystem::path getApplicationDirectory() {
#ifdef _WIN32
        std::array<wchar_t, 32768> executablePath;
        const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        if (length != 0 && length < executablePath.size()) return std::filesystem::path(executablePath.data()).parent_path();
#endif
        return std::filesystem::current_path();
    }

    void configureGtkRuntime(const std::filesystem::path& applicationDirectory) {
#ifdef _WIN32
        const std::string applicationPath = applicationDirectory.string();
        const std::string loaders = (applicationDirectory / "lib" / "gdk-pixbuf-2.0" / "2.10.0" / "loaders").string();
        g_setenv("GTK_DATA_PREFIX", applicationPath.c_str(), true);
        g_setenv("GDK_PIXBUF_MODULEDIR", loaders.c_str(), true);
        g_setenv("GDK_PIXBUF_MODULE_FILE", (applicationDirectory / "lib" / "gdk-pixbuf-2.0" / "2.10.0" / "loaders.cache").string().c_str(), true);
        g_setenv("GSETTINGS_SCHEMA_DIR", (applicationDirectory / "share" / "glib-2.0" / "schemas").string().c_str(), true);
#endif
    }

}

int main(int argc, char** argv) {
    const std::filesystem::path applicationDirectory = getApplicationDirectory();
    std::filesystem::current_path(applicationDirectory);
    configureGtkRuntime(applicationDirectory);
    gtk_init(&argc, &argv);
    RC3::RCOptions options;
    copySyntaxFiles(applicationDirectory);
    RC3::loadRCOptions(options, applicationDirectory);
    TStartFrame* startFrame = nullptr;
    TRemoteFrame remoteFrame([&] { startFrame->show(); });
    TServerList serverList([&] { startFrame->show(); }, [&](void* connection) { remoteFrame.open(connection); });
    TStartFrame frame(options, applicationDirectory, [&](const std::string& account, const std::string& password) { serverList.open(account, password); });
    startFrame = &frame;
    frame.show();
    gtk_main();
    return 0;
}
