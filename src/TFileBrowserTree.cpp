#include "TFileBrowserTree.h"
#include "TAssetPaths.h"
#include "TEditorFind.h"
#include "TGScriptEditor.h"
#include "TMng.h"
#include "TTheme.h"
#include "TScriptEditorTracking.h"

#include <grclib.h>
#include <gtksourceview/gtksource.h>
#include <glib/gstdio.h>

#ifdef _WIN32
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <gdk/gdkwin32.h>
#endif

#include <string>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <functional>
#include <memory>
#include <ctime>
#include <cwchar>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <vector>

struct PreviewAsyncState {
    TFileBrowserTree* browser = nullptr;
    guint generation = 0;
};

struct PreviewDecodeRequest {
    std::shared_ptr<PreviewAsyncState> state;
    std::string path;
    std::string folder;
    std::vector<std::uint8_t> content;
    guint generation = 0;
    GdkPixbuf* preview = nullptr;
    GdkPixbuf* thumbnail = nullptr;
    ~PreviewDecodeRequest() {
        if (preview != nullptr) g_object_unref(preview);
        if (thumbnail != nullptr) g_object_unref(thumbnail);
    }
};

namespace {

    std::string safeDownloadComponent(const std::string& value, const std::string& fallback) {
        std::string result;
        for (const char character : value) {
            if (character == '/' || character == '\\' || character == ':' || character == '*' || character == '?' || character == '"' || character == '<' || character == '>' || character == '|') result += '_';
            else if (static_cast<unsigned char>(character) >= 32) result += character;
        }
        while (!result.empty() && (result.back() == ' ' || result.back() == '.')) result.pop_back();
        return result.empty() || result == "." || result == ".." ? fallback : result;
    }

    std::vector<std::string> remotePathComponents(const std::string& path) {
        std::vector<std::string> components;
        std::string component;
        for (const char character : path + "/") {
            if (character == '/' || character == '\\') {
                if (!component.empty() && component != "." && component != "..") components.push_back(safeDownloadComponent(component, "folder"));
                component.clear();
            } else component += character;
        }
        return components;
    }
    constexpr int FolderIconColumn = 0;
    constexpr int FolderPathColumn = 1;
    constexpr int FolderRightsColumn = 2;
    constexpr int FolderDisplayColumn = 3;
    constexpr int FileIconColumn = 0;
    constexpr int FilePathColumn = 1;
    constexpr int FileRightsColumn = 2;
    constexpr int FileSizeColumn = 3;
    constexpr int FileModifiedColumn = 4;
    constexpr int FileSizeSortColumn = 5;
    constexpr int FileModifiedSortColumn = 6;
    constexpr int ModernIconColumn = 0;
    constexpr int ModernNameColumn = 1;
    constexpr int ModernPathColumn = 2;
    constexpr int ModernFolderColumn = 3;
    constexpr int ModernRightsColumn = 4;
    struct FileMenuItem { TFileBrowserTree* browser; std::vector<std::string> paths; };
    void destroyFileMenuItem(gpointer data, GClosure*) { delete static_cast<FileMenuItem*>(data); }
    GList* selectedRows(GtkWidget* widget, GtkTreeModel** model) {
        if (GTK_IS_ICON_VIEW(widget)) {
            if (model != nullptr) *model = gtk_icon_view_get_model(GTK_ICON_VIEW(widget));
            return gtk_icon_view_get_selected_items(GTK_ICON_VIEW(widget));
        }
        return gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)), model);
    }

    std::string formatModified(int timestamp) {
        if (timestamp <= 0) return "";
        const std::time_t value = timestamp;
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &value);
#else
        localtime_r(&value, &local);
#endif
        std::ostringstream stream;
        stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
        return stream.str();
    }
    void applyModifiedTime(const std::filesystem::path& path, int timestamp) {
        if (timestamp <= 0) return;
        const auto systemTime = std::chrono::system_clock::from_time_t(timestamp);
        const auto fileTime = std::filesystem::file_time_type::clock::now() + (systemTime - std::chrono::system_clock::now());
        std::error_code error;
        std::filesystem::last_write_time(path, fileTime, error);
    }

    GdkPixbuf* loadImage(const std::filesystem::path& applicationDirectory, const char* name) { return gdk_pixbuf_new_from_file(resolveRuntimeImage(applicationDirectory, name).string().c_str(), nullptr); }
    GdkPixbuf* loadThemeIcon(const char* name, int size) {
        GError* error = nullptr;
        GdkPixbuf* icon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), name, size, GTK_ICON_LOOKUP_FORCE_SIZE, &error);
        if (error != nullptr) g_error_free(error);
        return icon;
    }
    GdkPixbuf* scaledIcon(GdkPixbuf* pixbuf, int maximumWidth, int maximumHeight) {
        if (pixbuf == nullptr) return nullptr;
        const int width = gdk_pixbuf_get_width(pixbuf);
        const int height = gdk_pixbuf_get_height(pixbuf);
        const double scale = std::min(static_cast<double>(maximumWidth) / std::max(1, width), static_cast<double>(maximumHeight) / std::max(1, height));
        return gdk_pixbuf_scale_simple(pixbuf, std::max(1, static_cast<int>(width * scale)), std::max(1, static_cast<int>(height * scale)), GDK_INTERP_BILINEAR);
    }

    std::string createDragStagingFolder() {
        GError* error = nullptr;
        gchar* path = g_dir_make_tmp("RemoteControl-filebrowser-drag-XXXXXX", &error);
        if (error != nullptr) g_error_free(error);
        if (path == nullptr) return {};
        std::string result(path);
        g_free(path);
        return result;
    }

    bool pathMatches(const std::string& expected, const std::string& received) {
        return expected == received || received.ends_with("/" + expected) || expected.ends_with("/" + received);
    }

    bool isPreviewImage(const std::string& path) {
        const std::size_t dot = path.find_last_of('.');
        if (dot == std::string::npos) return false;
        gchar* lower = g_ascii_strdown(path.c_str() + dot, -1);
        const std::string extension = lower == nullptr ? "" : lower;
        g_free(lower);
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".gif" || extension == ".webp" || extension == ".bmp" || extension == ".ico" || extension == ".mng";
    }

#ifdef _WIN32
    struct NativeDropFiles { DWORD pFiles; POINT pt; BOOL fNC; BOOL fWide; };

    class NativeDropSource final : public IDropSource {
    public:
        NativeDropSource(const std::shared_ptr<bool>& accepted, HWND preview) : dropAccepted(accepted), previewWindow(preview) {}
        HRESULT __stdcall QueryInterface(REFIID iid, void** result) override {
            if (result == nullptr) return E_POINTER;
            *result = nullptr;
            if (iid == IID_IUnknown || iid == IID_IDropSource) *result = static_cast<IDropSource*>(this);
            if (*result == nullptr) return E_NOINTERFACE;
            AddRef();
            return S_OK;
        }
        ULONG __stdcall AddRef() override { return ++references; }
        ULONG __stdcall Release() override { ULONG value = --references; if (value == 0) delete this; return value; }
        HRESULT __stdcall QueryContinueDrag(BOOL escapePressed, DWORD keyState) override {
            if (escapePressed) return DRAGDROP_S_CANCEL;
            if ((keyState & MK_LBUTTON) == 0) { if (dropAccepted) *dropAccepted = true; return DRAGDROP_S_DROP; }
            return S_OK;
        }
        HRESULT __stdcall GiveFeedback(DWORD) override {
            if (previewWindow != nullptr) {
                POINT cursor{};
                if (GetCursorPos(&cursor)) SetWindowPos(previewWindow, HWND_TOPMOST, cursor.x + 14, cursor.y + 14, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
            return DRAGDROP_S_USEDEFAULTCURSORS;
        }
    private:
        ULONG references = 1;
        std::shared_ptr<bool> dropAccepted;
        HWND previewWindow = nullptr;
    };

    class NativeFileDataObject final : public IDataObject {
    public:
        using ContentProvider = std::function<bool(size_t, std::vector<guint8>&)>;
        NativeFileDataObject(const std::vector<std::wstring>& values, const std::vector<ULONGLONG>& fileSizes, ContentProvider provider) : names(values), sizes(fileSizes), contentProvider(std::move(provider)) {}
        HRESULT __stdcall QueryInterface(REFIID iid, void** result) override {
            if (result == nullptr) return E_POINTER;
            *result = nullptr;
            if (iid == IID_IUnknown || iid == IID_IDataObject) *result = static_cast<IDataObject*>(this);
            if (*result == nullptr) return E_NOINTERFACE;
            AddRef();
            return S_OK;
        }
        ULONG __stdcall AddRef() override { return ++references; }
        ULONG __stdcall Release() override { ULONG value = --references; if (value == 0) delete this; return value; }
        HRESULT __stdcall GetData(FORMATETC* format, STGMEDIUM* medium) override {
            if (format == nullptr || medium == nullptr) return E_POINTER;
            std::memset(medium, 0, sizeof(STGMEDIUM));
            if (format->cfFormat == descriptorFormat() && (format->tymed & TYMED_HGLOBAL) != 0) {
                const SIZE_T bytes = sizeof(FILEGROUPDESCRIPTORW) + (names.size() - 1) * sizeof(FILEDESCRIPTORW);
                HGLOBAL global = GlobalAlloc(GHND | GMEM_SHARE, bytes);
                if (global == nullptr) return E_OUTOFMEMORY;
                FILEGROUPDESCRIPTORW* group = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(global));
                if (group == nullptr) { GlobalFree(global); return E_OUTOFMEMORY; }
                group->cItems = static_cast<UINT>(names.size());
                for (size_t index = 0; index < names.size(); ++index) {
                    FILEDESCRIPTORW& file = group->fgd[index];
                    const ULONGLONG size = index < sizes.size() ? sizes[index] : 0;
                    file.dwFlags = FD_ATTRIBUTES | FD_FILESIZE | FD_PROGRESSUI;
                    file.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
                    file.nFileSizeHigh = static_cast<DWORD>(size >> 32);
                    file.nFileSizeLow = static_cast<DWORD>(size & 0xFFFFFFFFULL);
                    std::wcsncpy(file.cFileName, names[index].c_str(), ARRAYSIZE(file.cFileName) - 1);
                    file.cFileName[ARRAYSIZE(file.cFileName) - 1] = L'\0';
                }
                GlobalUnlock(global);
                medium->tymed = TYMED_HGLOBAL;
                medium->hGlobal = global;
                return S_OK;
            }
            if (format->cfFormat == contentsFormat() && (format->tymed & (TYMED_ISTREAM | TYMED_HGLOBAL)) != 0) {
                const LONG index = format->lindex == -1 && names.size() == 1 ? 0 : format->lindex;
                if (index < 0 || static_cast<size_t>(index) >= names.size() || !contentProvider) return DV_E_LINDEX;
                std::vector<guint8> content;
                if (!contentProvider(static_cast<size_t>(index), content)) return E_FAIL;
                HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, std::max<std::size_t>(content.size(), 1));
                if (global == nullptr) return E_OUTOFMEMORY;
                void* buffer = GlobalLock(global);
                if (buffer == nullptr) { GlobalFree(global); return E_OUTOFMEMORY; }
                if (!content.empty()) std::memcpy(buffer, content.data(), content.size());
                GlobalUnlock(global);
                if ((format->tymed & TYMED_ISTREAM) == 0) {
                    medium->tymed = TYMED_HGLOBAL;
                    medium->hGlobal = global;
                    return S_OK;
                }
                IStream* stream = nullptr;
                HRESULT result = CreateStreamOnHGlobal(global, TRUE, &stream);
                if (FAILED(result)) { GlobalFree(global); return result; }
                ULARGE_INTEGER streamSize{};
                streamSize.QuadPart = content.size();
                result = stream->SetSize(streamSize);
                if (FAILED(result)) { stream->Release(); return result; }
                medium->tymed = TYMED_ISTREAM;
                medium->pstm = stream;
                return S_OK;
            }
            return DV_E_FORMATETC;
        }
        HRESULT __stdcall GetDataHere(FORMATETC*, STGMEDIUM*) override { return DATA_E_FORMATETC; }
        HRESULT __stdcall QueryGetData(FORMATETC* format) override {
            if (format == nullptr) return E_POINTER;
            if (format->cfFormat == descriptorFormat() && (format->tymed & TYMED_HGLOBAL) != 0) return S_OK;
            if (format->cfFormat == contentsFormat() && (format->tymed & (TYMED_ISTREAM | TYMED_HGLOBAL)) != 0 && ((format->lindex >= 0 && static_cast<size_t>(format->lindex) < names.size()) || (format->lindex == -1 && names.size() == 1))) return S_OK;
            return DV_E_FORMATETC;
        }
        HRESULT __stdcall GetCanonicalFormatEtc(FORMATETC* format, FORMATETC* result) override { if (format == nullptr || result == nullptr) return E_POINTER; *result = *format; result->ptd = nullptr; return DATA_S_SAMEFORMATETC; }
        HRESULT __stdcall SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
        HRESULT __stdcall EnumFormatEtc(DWORD direction, IEnumFORMATETC** result) override {
            if (result == nullptr) return E_POINTER;
            *result = nullptr;
            if (direction != DATADIR_GET) return E_NOTIMPL;
            FORMATETC formats[2] = {
                {static_cast<CLIPFORMAT>(descriptorFormat()), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
                {static_cast<CLIPFORMAT>(contentsFormat()), nullptr, DVASPECT_CONTENT, -1, static_cast<DWORD>(TYMED_ISTREAM | TYMED_HGLOBAL)}
            };
            IEnumFORMATETC* first = nullptr;
            HRESULT status = SHCreateStdEnumFmtEtc(2, formats, &first);
            if (FAILED(status)) return status;
            *result = first;
            return S_OK;
        }
        HRESULT __stdcall DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT __stdcall DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT __stdcall EnumDAdvise(IEnumSTATDATA** result) override { if (result != nullptr) *result = nullptr; return OLE_E_ADVISENOTSUPPORTED; }
    private:
        static UINT descriptorFormat() { static UINT value = RegisterClipboardFormatW(L"FileGroupDescriptorW"); return value; }
        static UINT contentsFormat() { static UINT value = RegisterClipboardFormatW(L"FileContents"); return value; }
        ULONG references = 1;
        std::vector<std::wstring> names;
        std::vector<ULONGLONG> sizes;
        ContentProvider contentProvider;
    };

    HRESULT nativeFileDrag(const std::vector<std::wstring>& names, const std::vector<ULONGLONG>& sizes, NativeFileDataObject::ContentProvider contentProvider, const std::shared_ptr<bool>& dropAccepted, HWND previewWindow) {
        if (names.empty()) return E_INVALIDARG;
        const HRESULT initialized = OleInitialize(nullptr);
        if (FAILED(initialized)) return initialized;
        IDataObject* data = new NativeFileDataObject(names, sizes, std::move(contentProvider));
        IDropSource* source = new NativeDropSource(dropAccepted, previewWindow);
        DWORD effect = DROPEFFECT_NONE;
        ReleaseCapture();
        const HRESULT result = DoDragDrop(data, source, DROPEFFECT_COPY, &effect);
        data->Release();
        source->Release();
        OleUninitialize();
        return result;
    }
#endif

    GdkPixbuf* fileIcon(const RCFileBrowserEntry& entry, GdkPixbuf* text, GdkPixbuf* nw, GdkPixbuf* script, GdkPixbuf* gmap, GdkPixbuf* binary, GdkPixbuf* font, GdkPixbuf* archive, GdkPixbuf* config, GdkPixbuf* unknown) {
        std::string path = entry.path == nullptr ? "" : entry.path;
        std::transform(path.begin(), path.end(), path.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (path.find("config") != std::string::npos || path.find("settings") != std::string::npos || path.find("options") != std::string::npos) return config;
        const std::size_t extension = path.rfind('.');
        const std::string suffix = extension == std::string::npos ? "" : path.substr(extension);
        if (suffix == ".bin" || suffix == ".gs2bc") return binary;
        if (suffix == ".txt") return text;
        if (suffix == ".ttf" || suffix == ".otf") return font;
        if (suffix == ".tar" || suffix == ".zip" || suffix == ".rar" || suffix == ".7z") return archive;
        if (suffix == ".conf") return config;
        if (suffix == ".gmap") return gmap;
        if (suffix == ".graal") return script;
        if (suffix == ".nw") return nw;
        return unknown;
    }
}

bool TFileBrowserTree::isPreviewTransferMessage(const char* message) const {
    if (message == nullptr) return false;
    const std::string text(message);
    return std::any_of(previewTransferPaths.begin(), previewTransferPaths.end(), [&](const std::string& path) {
        if (text.find(path) != std::string::npos) return true;
        gchar* basename = g_path_get_basename(path.c_str());
        const bool matches = basename != nullptr && *basename != '\0' && text.find(basename) != std::string::npos;
        g_free(basename);
        return matches;
    });
}

TFileBrowserTree::TFileBrowserTree(const std::filesystem::path& nextApplicationDirectory) : applicationDirectory(nextApplicationDirectory) {
    previewAsyncState = std::make_shared<PreviewAsyncState>();
    previewAsyncState->browser = this;
    previewWorkerThread = std::thread(&TFileBrowserTree::previewWorkerLoop, this);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "File Browser");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(window, "FileBrowser");
    gtk_container_set_border_width(GTK_CONTAINER(root), 5);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* frame = gtk_frame_new(" Files ");
    gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
    gtk_box_pack_start(GTK_BOX(root), frame, true, true, 0);
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(content), 5);
    gtk_container_add(GTK_CONTAINER(frame), content);
    pathStack = gtk_stack_new();
    folderPath = gtk_label_new("Current Folder:");
    gtk_misc_set_alignment(GTK_MISC(folderPath), 0.0f, 0.5f);
    gtk_stack_add_named(GTK_STACK(pathStack), folderPath, "classic");
    GtkWidget* addressRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* addressLabel = gtk_label_new("Address:");
    addressEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(addressEntry), "Remote folder path");
    GtkWidget* upButton = gtk_button_new_from_icon_name("go-up", GTK_ICON_SIZE_BUTTON);
    GtkWidget* refreshButton = gtk_button_new_from_icon_name("view-refresh", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(upButton, "Parent folder");
    gtk_widget_set_tooltip_text(refreshButton, "Refresh folder");
    gtk_box_pack_start(GTK_BOX(addressRow), addressLabel, false, false, 0);
    gtk_box_pack_start(GTK_BOX(addressRow), addressEntry, true, true, 0);
    gtk_box_pack_start(GTK_BOX(addressRow), upButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(addressRow), refreshButton, false, false, 0);
    gtk_stack_add_named(GTK_STACK(pathStack), addressRow, "modern");
    gtk_box_pack_start(GTK_BOX(content), pathStack, false, false, 5);
    GtkWidget* panes = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_position(GTK_PANED(panes), 366);
    gtk_box_pack_start(GTK_BOX(content), panes, true, true, 0);
    GtkWidget* filePanes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(filePanes), 200);
    gtk_paned_pack1(GTK_PANED(panes), filePanes, false, true);
    GtkWidget* folderScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkWidget* fileScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkWidget* modernScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(folderScrolled), GTK_SHADOW_IN);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(fileScrolled), GTK_SHADOW_IN);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(modernScrolled), GTK_SHADOW_IN);
    folders = gtk_tree_store_new(4, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(folders), FolderDisplayColumn, GTK_SORT_ASCENDING);
    files = gtk_list_store_new(7, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
    modernItems = gtk_list_store_new(5, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING);
    folderView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(folders));
    fileView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(files));
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(fileView)), GTK_SELECTION_MULTIPLE);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(folderView), false);
    gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(folderView), true);
    GtkTreeViewColumn* folderColumn = gtk_tree_view_column_new();
    GtkCellRenderer* image = gtk_cell_renderer_pixbuf_new();
    GtkCellRenderer* text = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(folderColumn, image, false);
    gtk_tree_view_column_add_attribute(folderColumn, image, "pixbuf", FolderIconColumn);
    gtk_tree_view_column_pack_start(folderColumn, text, true);
    gtk_tree_view_column_add_attribute(folderColumn, text, "text", FolderDisplayColumn);
    gtk_tree_view_append_column(GTK_TREE_VIEW(folderView), folderColumn);
    image = gtk_cell_renderer_pixbuf_new();
    text = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* nameColumn = gtk_tree_view_column_new_with_attributes("Name", image, "pixbuf", FileIconColumn, nullptr);
    gtk_tree_view_column_pack_start(nameColumn, text, true);
    gtk_tree_view_column_add_attribute(nameColumn, text, "text", FilePathColumn);
    fileNameRenderer = text;
    g_signal_connect(fileNameRenderer, "edited", G_CALLBACK(onFileNameEdited), this);
    g_signal_connect(fileNameRenderer, "editing-canceled", G_CALLBACK(+[](GtkCellRenderer*, gpointer data) { g_object_set(static_cast<TFileBrowserTree*>(data)->fileNameRenderer, "editable", FALSE, nullptr); }), this);
    gtk_tree_view_column_set_resizable(nameColumn, true);
    gtk_tree_view_column_set_fixed_width(nameColumn, 290);
    gtk_tree_view_column_set_sort_column_id(nameColumn, FilePathColumn);
    gtk_tree_view_append_column(GTK_TREE_VIEW(fileView), nameColumn);
    const struct { const char* name; int column; int sortColumn; int width; } columns[] = {{"Rights", FileRightsColumn, FileRightsColumn, 60}, {"Size", FileSizeColumn, FileSizeSortColumn, 60}, {"Modified", FileModifiedColumn, FileModifiedSortColumn, 145}};
    for (const auto& column : columns) {
        text = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* viewColumn = gtk_tree_view_column_new_with_attributes(column.name, text, "text", column.column, nullptr);
        gtk_tree_view_column_set_resizable(viewColumn, true);
        gtk_tree_view_column_set_fixed_width(viewColumn, column.width);
        gtk_tree_view_column_set_sort_column_id(viewColumn, column.sortColumn);
        gtk_tree_view_append_column(GTK_TREE_VIEW(fileView), viewColumn);
    }
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(files), FilePathColumn, GTK_SORT_ASCENDING);
    gtk_container_add(GTK_CONTAINER(folderScrolled), folderView);
    gtk_container_add(GTK_CONTAINER(fileScrolled), fileView);
    modernView = gtk_icon_view_new_with_model(GTK_TREE_MODEL(modernItems));
    gtk_widget_set_name(modernView, "remote-file-icon-view");
    gtk_icon_view_set_item_orientation(GTK_ICON_VIEW(modernView), GTK_ORIENTATION_VERTICAL);
    GtkCellRenderer* modernIconRenderer = gtk_cell_renderer_pixbuf_new();
    GtkCellRenderer* modernTextRenderer = gtk_cell_renderer_text_new();
    g_object_set(modernIconRenderer, "xalign", 0.5f, "yalign", 0.5f, nullptr);
    g_object_set(modernTextRenderer, "xalign", 0.5f, "alignment", PANGO_ALIGN_CENTER, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, "width-chars", 15, "max-width-chars", 15, nullptr);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(modernView), modernIconRenderer, false);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(modernView), modernIconRenderer, "pixbuf", ModernIconColumn);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(modernView), modernTextRenderer, false);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(modernView), modernTextRenderer, "text", ModernNameColumn);
    gtk_icon_view_set_selection_mode(GTK_ICON_VIEW(modernView), GTK_SELECTION_MULTIPLE);
    gtk_icon_view_set_item_width(GTK_ICON_VIEW(modernView), 112);
    modernSearchPopover = gtk_popover_new(modernView);
    modernSearchEntry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(modernSearchEntry), "Search files");
    gtk_container_set_border_width(GTK_CONTAINER(modernSearchPopover), 6);
    gtk_container_add(GTK_CONTAINER(modernSearchPopover), modernSearchEntry);
    gtk_widget_show(modernSearchEntry);
    gtk_icon_view_set_item_padding(GTK_ICON_VIEW(modernView), 5);
    gtk_icon_view_set_row_spacing(GTK_ICON_VIEW(modernView), 6);
    gtk_icon_view_set_column_spacing(GTK_ICON_VIEW(modernView), 6);
    gtk_icon_view_set_margin(GTK_ICON_VIEW(modernView), 8);
    gtk_container_add(GTK_CONTAINER(modernScrolled), modernView);
    gtk_paned_pack1(GTK_PANED(filePanes), folderScrolled, false, true);
    fileViewStack = gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(fileViewStack), fileScrolled, "classic");
    gtk_stack_add_named(GTK_STACK(fileViewStack), modernScrolled, "modern");
    gtk_paned_pack2(GTK_PANED(filePanes), fileViewStack, true, true);
    GtkWidget* logScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(logScrolled), GTK_SHADOW_IN);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(logScrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    log = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log), false);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(log), 10);
    gtk_container_add(GTK_CONTAINER(logScrolled), log);
    gtk_paned_pack2(GTK_PANED(panes), logScrolled, true, true);
    statusLabel = gtk_label_new("0 files");
    gtk_misc_set_alignment(GTK_MISC(statusLabel), 0.0f, 0.5f);
    GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(footer), 5);
    gtk_box_pack_start(GTK_BOX(footer), statusLabel, true, true, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget* closeButton = gtk_button_new_with_label("Close");
    gtk_button_set_image(GTK_BUTTON(closeButton), gtk_image_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(closeButton), true);
    gtk_container_add(GTK_CONTAINER(buttons), closeButton);
    gtk_box_pack_end(GTK_BOX(footer), buttons, false, false, 0);
    gtk_box_pack_start(GTK_BOX(root), footer, false, false, 0);
    closedFolderIcon = loadThemeIcon("folder", 16);
    openFolderIcon = loadThemeIcon("folder-open", 16);
    closedFolderLargeIcon = loadThemeIcon("folder", 48);
    openFolderLargeIcon = loadThemeIcon("folder-open", 48);
    textFileIcon = loadImage(applicationDirectory, "rcfiles_text2.png");
    nwFileIcon = loadImage(applicationDirectory, "rcfiles_nw.png");
    scriptFileIcon = loadImage(applicationDirectory, "rcfiles_graal.png");
    gmapFileIcon = loadImage(applicationDirectory, "rcfiles_gmap.png");
    binaryFileIcon = loadImage(applicationDirectory, "rcfiles_binary.png");
    fontFileIcon = loadImage(applicationDirectory, "rcfiles_ttf.png");
    archiveFileIcon = loadImage(applicationDirectory, "rcfiles_archive.png");
    configFileIcon = loadImage(applicationDirectory, "rcfiles_conf.png");
    unknownFileIcon = loadImage(applicationDirectory, "rcfiles_unknown.png");
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(folderView)), "changed", G_CALLBACK(onFolderSelected), this);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(fileView)), "changed", G_CALLBACK(onFileSelectionChanged), this);
    g_signal_connect(folderView, "row-expanded", G_CALLBACK(onFolderStateChanged), this);
    g_signal_connect(folderView, "row-collapsed", G_CALLBACK(onFolderStateChanged), this);
    g_signal_connect(folderView, "button-press-event", G_CALLBACK(onFolderButtonPress), this);
    g_signal_connect(fileView, "button-press-event", G_CALLBACK(onFileButtonPress), this);
    g_signal_connect(modernView, "button-press-event", G_CALLBACK(onModernButtonPress), this);
    g_signal_connect(modernView, "selection-changed", G_CALLBACK(onModernSelectionChanged), this);
    g_signal_connect(modernView, "key-press-event", G_CALLBACK(onModernKeyPress), this);
    g_signal_connect(modernSearchEntry, "search-changed", G_CALLBACK(onModernSearchChanged), this);
    g_signal_connect(addressEntry, "activate", G_CALLBACK(onAddressActivate), this);
    g_signal_connect(upButton, "clicked", G_CALLBACK(onAddressUp), this);
    g_signal_connect(refreshButton, "clicked", G_CALLBACK(onAddressRefresh), this);
    GtkTargetEntry dropTargets[] = {{const_cast<gchar*>("application/x-remote-control-file-path"), GTK_TARGET_SAME_APP, 1}, {const_cast<gchar*>("text/uri-list"), 0, 2}};
#ifndef _WIN32
    GtkTargetEntry fileTarget[] = {{const_cast<gchar*>("application/x-remote-control-file-path"), GTK_TARGET_SAME_APP, 1}, {const_cast<gchar*>("text/uri-list"), 0, 2}};
    gtk_drag_source_set(fileView, GDK_BUTTON1_MASK, fileTarget, G_N_ELEMENTS(fileTarget), GDK_ACTION_COPY);
    gtk_drag_source_set(modernView, GDK_BUTTON1_MASK, fileTarget, G_N_ELEMENTS(fileTarget), GDK_ACTION_COPY);
#else
    gtk_widget_add_events(fileView, GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(fileView, "button-release-event", G_CALLBACK(onFileButtonRelease), this);
    gtk_widget_add_events(modernView, GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(modernView, "button-release-event", G_CALLBACK(onFileButtonRelease), this);
    gtk_widget_add_events(folderView, GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(folderView, "button-release-event", G_CALLBACK(onFileButtonRelease), this);
#endif
    gtk_widget_add_events(fileView, static_cast<GdkEventMask>(GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK));
    g_signal_connect(fileView, "motion-notify-event", G_CALLBACK(onFileMotion), this);
    g_signal_connect(fileView, "leave-notify-event", G_CALLBACK(onFileLeave), this);
    gtk_widget_add_events(modernView, static_cast<GdkEventMask>(GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK));
    g_signal_connect(modernView, "motion-notify-event", G_CALLBACK(onModernMotion), this);
    g_signal_connect(modernView, "leave-notify-event", G_CALLBACK(onFileLeave), this);
    gtk_widget_add_events(folderView, GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(folderView, "leave-notify-event", G_CALLBACK(onFileLeave), this);
    g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(modernScrolled)), "value-changed", G_CALLBACK(+[](GtkAdjustment*, gpointer data) { static_cast<TFileBrowserTree*>(data)->queueVisibleThumbnails(); }), this);
    g_signal_connect(modernView, "size-allocate", G_CALLBACK(+[](GtkWidget* widget, GtkAllocation* allocation, gpointer data) {
        gtk_icon_view_set_columns(GTK_ICON_VIEW(widget), std::max(1, allocation->width / 124));
        static_cast<TFileBrowserTree*>(data)->queueVisibleThumbnails();
    }), this);
    gtk_drag_dest_set(folderView, static_cast<GtkDestDefaults>(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP), dropTargets, G_N_ELEMENTS(dropTargets), static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    gtk_drag_dest_set(fileView, static_cast<GtkDestDefaults>(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP), dropTargets, G_N_ELEMENTS(dropTargets), static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    gtk_drag_dest_set(modernView, static_cast<GtkDestDefaults>(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP), dropTargets, G_N_ELEMENTS(dropTargets), GDK_ACTION_COPY);
    g_signal_connect(fileView, "drag-begin", G_CALLBACK(onFileDragBegin), this);
    g_signal_connect(fileView, "drag-end", G_CALLBACK(onFileDragEnd), this);
    g_signal_connect(fileView, "drag-data-get", G_CALLBACK(onFileDragDataGet), this);
    g_signal_connect(modernView, "drag-begin", G_CALLBACK(onFileDragBegin), this);
    g_signal_connect(modernView, "drag-end", G_CALLBACK(onFileDragEnd), this);
    g_signal_connect(modernView, "drag-data-get", G_CALLBACK(onFileDragDataGet), this);
    g_signal_connect(folderView, "drag-data-received", G_CALLBACK(onDropDataReceived), this);
    g_signal_connect(fileView, "drag-data-received", G_CALLBACK(onDropDataReceived), this);
    g_signal_connect(modernView, "drag-data-received", G_CALLBACK(onDropDataReceived), this);
    g_signal_connect(closeButton, "clicked", G_CALLBACK(onRefresh), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
    previewWindow = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_type_hint(GTK_WINDOW(previewWindow), GDK_WINDOW_TYPE_HINT_TOOLTIP);
    GtkWidget* previewFrame = gtk_frame_new(nullptr);
    gtk_frame_set_shadow_type(GTK_FRAME(previewFrame), GTK_SHADOW_OUT);
    gtk_container_add(GTK_CONTAINER(previewWindow), previewFrame);
    GtkWidget* previewBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(previewBox), 8);
    gtk_container_add(GTK_CONTAINER(previewFrame), previewBox);
    previewImage = gtk_image_new();
    previewLabel = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(previewLabel), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(previewLabel), 48);
    gtk_box_pack_start(GTK_BOX(previewBox), previewImage, true, true, 0);
    gtk_box_pack_start(GTK_BOX(previewBox), previewLabel, false, false, 0);
    externalWatchId = g_timeout_add(2500, watchExternalFile, this);
    setModernFileBrowser(false);
}

void TFileBrowserTree::cleanupDragState() {
    for (const std::string& path : pendingDragLocalPaths) g_remove(path.c_str());
    for (const std::string& path : completedDragDownloads) g_remove(path.c_str());
    if (!dragStagingFolder.empty()) { g_rmdir(dragStagingFolder.c_str()); dragStagingFolder.clear(); }
    pendingDragDownloads.clear();
    pendingNativeDragContents.clear();
    completedDragDownloads.clear();
    pendingDragLocalPaths.clear();
}

TFileBrowserTree::~TFileBrowserTree() {
    previewAsyncState->browser = nullptr;
    ++previewAsyncState->generation;
    {
        std::lock_guard<std::mutex> lock(previewWorkerMutex);
        previewWorkerStop = true;
        previewWorkerJobs.clear();
    }
    previewWorkerCondition.notify_one();
    if (previewWorkerThread.joinable()) previewWorkerThread.join();
    if (externalWatchId != 0) g_source_remove(externalWatchId);
    if (inlineRenameId != 0) g_source_remove(inlineRenameId);
    if (mutationRefreshId != 0) g_source_remove(mutationRefreshId);
    if (thumbnailLoadId != 0) g_source_remove(thumbnailLoadId);
    clearModernBuild();
    clearPreviewCache();
    cleanupDragState();
    if (closedFolderIcon != nullptr) g_object_unref(closedFolderIcon);
    if (openFolderIcon != nullptr) g_object_unref(openFolderIcon);
    if (closedFolderLargeIcon != nullptr) g_object_unref(closedFolderLargeIcon);
    if (openFolderLargeIcon != nullptr) g_object_unref(openFolderLargeIcon);
    if (textFileIcon != nullptr) g_object_unref(textFileIcon);
    if (nwFileIcon != nullptr) g_object_unref(nwFileIcon);
    if (scriptFileIcon != nullptr) g_object_unref(scriptFileIcon);
    if (gmapFileIcon != nullptr) g_object_unref(gmapFileIcon);
    if (binaryFileIcon != nullptr) g_object_unref(binaryFileIcon);
    if (fontFileIcon != nullptr) g_object_unref(fontFileIcon);
    if (archiveFileIcon != nullptr) g_object_unref(archiveFileIcon);
    if (configFileIcon != nullptr) g_object_unref(configFileIcon);
    if (unknownFileIcon != nullptr) g_object_unref(unknownFileIcon);
    if (previewWindow != nullptr) gtk_widget_destroy(previewWindow);
    if (dragPreviewWindow != nullptr) gtk_widget_destroy(dragPreviewWindow);
    if (window != nullptr) gtk_widget_destroy(window);
    if (folders != nullptr) g_object_unref(folders);
    if (files != nullptr) g_object_unref(files);
    if (modernItems != nullptr) g_object_unref(modernItems);
}

void TFileBrowserTree::open(void* nextConnection) {
    connection = nextConnection;
    rc_on_filebrowser_folders(connection, onFolders, this);
    rc_on_filebrowser_files(connection, onFiles, this);
    rc_on_filebrowser_message(connection, onMessage, this);
    rc_on_file_received(connection, onFileReceived, this);
    gtk_widget_show_all(window);
    setModernFileBrowser(modernFileBrowser);
    gtk_window_present(GTK_WINDOW(window));
    refresh();
}

void TFileBrowserTree::openFolder(void* nextConnection, const std::string& folder) {
    open(nextConnection);
    navigateTo(folder);
}
void TFileBrowserTree::hide() { resetState(); gtk_widget_hide(window); }

void TFileBrowserTree::setDownloadFolder(const std::string& folder) { downloadFolder = folder; }
void TFileBrowserTree::setDownloadServer(const std::string& server) { downloadServer = server; }
void TFileBrowserTree::setServerName(const std::string& server) { gtk_window_set_title(GTK_WINDOW(window), server.empty() ? "File Browser" : ("File Browser - " + server).c_str()); }
void TFileBrowserTree::setModernFileBrowser(bool enabled) {
    modernFileBrowser = enabled;
    ++previewAsyncState->generation;
    clearModernBuild();
    {
        std::lock_guard<std::mutex> lock(previewWorkerMutex);
        previewWorkerJobs.clear();
    }
    queuedPreviewDownloads.clear();
    visiblePreviewPaths.clear();
    if (thumbnailLoadId != 0) { g_source_remove(thumbnailLoadId); thumbnailLoadId = 0; }
    gtk_stack_set_visible_child_name(GTK_STACK(pathStack), enabled ? "modern" : "classic");
    gtk_stack_set_visible_child_name(GTK_STACK(fileViewStack), enabled ? "modern" : "classic");
    gtk_entry_set_text(GTK_ENTRY(addressEntry), currentFolder.c_str());
    if (enabled) rebuildModernItems();
    updateFileStatus();
}
void TFileBrowserTree::setHoverPreviews(bool enabled) {
    hoverPreviews = enabled;
    if (!enabled) {
        hidePreview();
        hoveredPreviewPath.clear();
        if (!modernThumbnails) queuedPreviewDownloads.clear();
    }
    else if (modernFileBrowser) queueVisibleThumbnails();
    else if (connection != nullptr && !currentFolder.empty()) rc_filebrowser_cd(connection, currentFolder.c_str());
}
void TFileBrowserTree::setModernThumbnails(bool enabled) {
    modernThumbnails = enabled;
    if (!enabled && !hoverPreviews) queuedPreviewDownloads.clear();
    if (modernFileBrowser) rebuildModernItems();
}
std::string TFileBrowserTree::downloadDestinationDirectory() const {
    if (downloadFolder.empty()) return {};
    std::string destination = downloadFolder;
    destination = (std::filesystem::path(destination) / safeDownloadComponent(downloadServer, "Server")).string();
    for (const std::string& component : remotePathComponents(currentFolder)) destination = (std::filesystem::path(destination) / component).string();
    return destination;
}

void TFileBrowserTree::onRefresh(GtkButton*, gpointer data) { static_cast<TFileBrowserTree*>(data)->hide(); }
void TFileBrowserTree::onFolders(int, void* data) { static_cast<TFileBrowserTree*>(data)->refreshFolders(); }
void TFileBrowserTree::onFiles(const char* folder, int count, void* data) { static_cast<TFileBrowserTree*>(data)->refreshFiles(folder, count); }
void TFileBrowserTree::onMessage(const char* message, void* data) { TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data); if (browser->isPreviewTransferMessage(message)) return; browser->appendLog(message == nullptr ? "" : message); }
gboolean TFileBrowserTree::onDelete(GtkWidget*, GdkEvent*, gpointer data) { static_cast<TFileBrowserTree*>(data)->hide(); return true; }
void TFileBrowserTree::onFileSelectionChanged(GtkTreeSelection*, gpointer data) { static_cast<TFileBrowserTree*>(data)->updateFileStatus(); }
void TFileBrowserTree::onModernSelectionChanged(GtkIconView*, gpointer data) { static_cast<TFileBrowserTree*>(data)->updateFileStatus(); }

void TFileBrowserTree::updateFileStatus() {
    if (statusLabel == nullptr) return;
    const int fileCount = files == nullptr ? 0 : gtk_tree_model_iter_n_children(GTK_TREE_MODEL(files), nullptr);
    int selectedCount = 0;
    if (modernFileBrowser && modernView != nullptr) {
        GtkTreeModel* model = gtk_icon_view_get_model(GTK_ICON_VIEW(modernView));
        GList* selected = gtk_icon_view_get_selected_items(GTK_ICON_VIEW(modernView));
        for (GList* node = selected; node != nullptr; node = node->next) {
            GtkTreeIter row;
            gboolean folder = false;
            if (model != nullptr && gtk_tree_model_get_iter(model, &row, static_cast<GtkTreePath*>(node->data))) gtk_tree_model_get(model, &row, ModernFolderColumn, &folder, -1);
            if (!folder) ++selectedCount;
            gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
        }
        g_list_free(selected);
    } else if (fileView != nullptr) {
        GList* selected = gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(fileView)), nullptr);
        selectedCount = g_list_length(selected);
        for (GList* node = selected; node != nullptr; node = node->next) gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
        g_list_free(selected);
    }
    std::string text = std::to_string(fileCount) + (fileCount == 1 ? " file" : " files");
    if (selectedCount > 0) text += "  |  " + std::to_string(selectedCount) + " selected";
    gtk_label_set_text(GTK_LABEL(statusLabel), text.c_str());
}

void TFileBrowserTree::resetState() {
    if (inlineRenameId != 0) { g_source_remove(inlineRenameId); inlineRenameId = 0; }
    pendingInlineRenamePath.clear();
    if (mutationRefreshId != 0) { g_source_remove(mutationRefreshId); mutationRefreshId = 0; }
    ++previewAsyncState->generation;
    clearModernBuild();
    {
        std::lock_guard<std::mutex> lock(previewWorkerMutex);
        previewWorkerJobs.clear();
    }
    if (thumbnailLoadId != 0) { g_source_remove(thumbnailLoadId); thumbnailLoadId = 0; }
    queuedPreviewDownloads.clear();
    visiblePreviewPaths.clear();
    pendingPreviewDownloads.clear();
    previewTransferPaths.clear();
    pendingDragSelectionPaths.clear();
    cleanupDragState();
    pendingUserDownloads.clear();
    pendingEditPath.clear();
    pendingExternalPath.clear();
    watchExternalPath.clear();
    watchExternalFolder.clear();
    watchExternalRemotePath.clear();
    watchExternalModified = 0;
    watchExternalWritable = false;
    currentFolder.clear();
    previewFolder.clear();
    folderPaths.clear();
    remoteModifiedTimes.clear();
    previewFileSizes.clear();
    clearPreviewCache();
    hideDragPreview();
    gtk_list_store_clear(files);
    gtk_list_store_clear(modernItems);
    gtk_tree_store_clear(folders);
    gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(fileView)));
    gtk_icon_view_unselect_all(GTK_ICON_VIEW(modernView));
    gtk_entry_set_text(GTK_ENTRY(addressEntry), "");
    gtk_entry_set_text(GTK_ENTRY(modernSearchEntry), "");
    gtk_label_set_text(GTK_LABEL(folderPath), "Current Folder:");
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(log)), "", -1);
    updateFolderIcons();
    updateFileStatus();
}

void TFileBrowserTree::onFolderSelected(GtkTreeSelection* selection, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(selection, nullptr, &row)) return;
    gchar* folder = nullptr;
    gtk_tree_model_get(GTK_TREE_MODEL(browser->folders), &row, FolderPathColumn, &folder, -1);
    if (folder != nullptr) {
        std::string folderPath(folder);
        if (!folderPath.empty() && folderPath.back() != '/') folderPath += '/';
        browser->navigateTo(folderPath);
        g_free(folder);
    }
}

void TFileBrowserTree::onFolderStateChanged(GtkTreeView*, GtkTreeIter*, GtkTreePath*, gpointer data) { static_cast<TFileBrowserTree*>(data)->updateFolderIcons(); }

void TFileBrowserTree::navigateTo(const std::string& folder) {
    std::string normalized = folder;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    while (!normalized.empty() && normalized.front() == '/') normalized.erase(normalized.begin());
    while (normalized.find("//") != std::string::npos) normalized.replace(normalized.find("//"), 2, "/");
    if (normalized == ".") normalized.clear();
    if (!normalized.empty() && normalized.back() != '/') normalized += '/';
    ++previewAsyncState->generation;
    clearModernBuild();
    {
        std::lock_guard<std::mutex> lock(previewWorkerMutex);
        previewWorkerJobs.clear();
    }
    currentFolder = normalized;
    gtk_list_store_clear(files);
    gtk_list_store_clear(modernItems);
    updateFileStatus();
    queuedPreviewDownloads.clear();
    visiblePreviewPaths.clear();
    if (thumbnailLoadId != 0) { g_source_remove(thumbnailLoadId); thumbnailLoadId = 0; }
    if (previewFolder != normalized) clearPreviewCache();
    previewFolder = normalized;
    hidePreview();
    gtk_label_set_text(GTK_LABEL(folderPath), (std::string("Current Folder: ") + normalized).c_str());
    gtk_entry_set_text(GTK_ENTRY(addressEntry), normalized.c_str());
    updateFolderIcons();
    if (!rc_filebrowser_cd(connection, normalized.c_str())) appendLog(rc_last_error(connection));
}

void TFileBrowserTree::updateFolderIcons() {
    if (folders == nullptr || folderView == nullptr) return;
    std::string active = currentFolder;
    while (!active.empty() && active.back() == '/') active.pop_back();
    struct State { TFileBrowserTree* browser; const std::string* active; } state{this, &active};
    gtk_tree_model_foreach(GTK_TREE_MODEL(folders), +[](GtkTreeModel* model, GtkTreePath* treePath, GtkTreeIter* row, gpointer data) {
        State* state = static_cast<State*>(data);
        gchar* value = nullptr;
        gtk_tree_model_get(model, row, FolderPathColumn, &value, -1);
        const std::string folder = value == nullptr ? "" : value;
        g_free(value);
        const bool active = !folder.empty() && (*state->active == folder || state->active->starts_with(folder + "/"));
        const bool expanded = gtk_tree_view_row_expanded(GTK_TREE_VIEW(state->browser->folderView), treePath);
        gtk_tree_store_set(state->browser->folders, row, FolderIconColumn, active || expanded ? state->browser->openFolderIcon : state->browser->closedFolderIcon, -1);
        return FALSE;
    }, &state);
}

void TFileBrowserTree::onAddressActivate(GtkEntry* entry, gpointer data) { static_cast<TFileBrowserTree*>(data)->navigateTo(gtk_entry_get_text(entry)); }
void TFileBrowserTree::onAddressUp(GtkButton*, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    std::string parent = browser->currentFolder;
    while (!parent.empty() && parent.back() == '/') parent.pop_back();
    const std::size_t slash = parent.find_last_of('/');
    browser->navigateTo(slash == std::string::npos ? "" : parent.substr(0, slash + 1));
}
void TFileBrowserTree::onAddressRefresh(GtkButton*, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    if (!rc_filebrowser_cd(browser->connection, browser->currentFolder.c_str())) browser->appendLog(rc_last_error(browser->connection));
}

gboolean TFileBrowserTree::onMutationRefresh(gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    browser->mutationRefreshId = 0;
    if (browser->connection != nullptr && !rc_filebrowser_cd(browser->connection, browser->currentFolder.c_str())) browser->appendLog(rc_last_error(browser->connection));
    return G_SOURCE_REMOVE;
}

void TFileBrowserTree::queueMutationRefresh() {
    if (mutationRefreshId != 0) g_source_remove(mutationRefreshId);
    mutationRefreshId = g_timeout_add(500, onMutationRefresh, this);
}

gboolean TFileBrowserTree::onFolderButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY) return false;
    static_cast<TFileBrowserTree*>(data)->showItemMenu(widget, event, true);
    return true;
}

gboolean TFileBrowserTree::onFileButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    if (event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_PRIMARY) {
        GtkTreePath* path = nullptr;
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), static_cast<gint>(event->x), static_cast<gint>(event->y), &path, nullptr, nullptr, nullptr)) {
            GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
            if (gtk_tree_selection_path_is_selected(selection, path)) {
                GList* selectedRows = gtk_tree_selection_get_selected_rows(selection, nullptr);
                const bool multiple = selectedRows != nullptr && selectedRows->next != nullptr;
#ifdef _WIN32
                browser->nativeDragButton = event->button;
                browser->nativeDragX = static_cast<gint>(event->x);
                browser->nativeDragY = static_cast<gint>(event->y);
#endif
                browser->pendingDragSelectionPaths.clear();
                if (multiple) for (GList* node = selectedRows; node != nullptr; node = node->next) {
                    gchar* selectedPath = gtk_tree_path_to_string(static_cast<GtkTreePath*>(node->data));
                    if (selectedPath != nullptr) browser->pendingDragSelectionPaths.emplace_back(selectedPath);
                    g_free(selectedPath);
                }
                for (GList* node = selectedRows; node != nullptr; node = node->next) gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
                g_list_free(selectedRows);
                if (multiple) { gtk_tree_path_free(path); return true; }
                gchar* pathText = gtk_tree_path_to_string(path);
                browser->pendingInlineRenamePath = pathText == nullptr ? "" : pathText;
                g_free(pathText);
                gtk_tree_path_free(path);
                if (browser->inlineRenameId != 0) g_source_remove(browser->inlineRenameId);
                browser->inlineRenameId = g_timeout_add(300, beginInlineRename, browser);
                return false;
            }
            gtk_tree_path_free(path);
        }
    }
    if (event->type == GDK_2BUTTON_PRESS && event->button == GDK_BUTTON_PRIMARY) {
        if (browser->inlineRenameId != 0) { g_source_remove(browser->inlineRenameId); browser->inlineRenameId = 0; browser->pendingInlineRenamePath.clear(); }
        GtkTreePath* path = nullptr;
        if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), static_cast<gint>(event->x), static_cast<gint>(event->y), &path, nullptr, nullptr, nullptr)) return false;
        GtkTreeIter row;
        GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
        gchar* itemPath = nullptr;
        gchar* rights = nullptr;
        if (gtk_tree_model_get_iter(model, &row, path)) gtk_tree_model_get(model, &row, FilePathColumn, &itemPath, FileRightsColumn, &rights, -1);
        gtk_tree_path_free(path);
        if (itemPath != nullptr && *itemPath != '\0') {
            browser->pendingExternalPath = itemPath;
            browser->watchExternalWritable = rights != nullptr && std::string(rights).find('w') != std::string::npos;
            if (!rc_filebrowser_download(browser->connection, itemPath)) browser->appendLog(rc_last_error(browser->connection));
        }
        g_free(itemPath);
        g_free(rights);
        return true;
    }
    if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY) return false;
    browser->showItemMenu(widget, event, false);
    return true;
}

gboolean TFileBrowserTree::onModernButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    GtkTreePath* path = gtk_icon_view_get_path_at_pos(GTK_ICON_VIEW(widget), static_cast<gint>(event->x), static_cast<gint>(event->y));
    if (path == nullptr) return false;
    if (event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_SECONDARY) {
        if (!gtk_icon_view_path_is_selected(GTK_ICON_VIEW(widget), path)) { gtk_icon_view_unselect_all(GTK_ICON_VIEW(widget)); gtk_icon_view_select_path(GTK_ICON_VIEW(widget), path); }
        GtkTreeIter row;
        gboolean folder = false;
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->modernItems), &row, path)) gtk_tree_model_get(GTK_TREE_MODEL(browser->modernItems), &row, ModernFolderColumn, &folder, -1);
        gtk_tree_path_free(path);
        browser->showItemMenu(widget, event, folder);
        return true;
    }
    if (event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_PRIMARY) {
        GtkTreeIter row;
        gboolean folder = false;
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->modernItems), &row, path)) gtk_tree_model_get(GTK_TREE_MODEL(browser->modernItems), &row, ModernFolderColumn, &folder, -1);
        browser->pendingDragSelectionPaths.clear();
        const bool manualDragPress = !folder && (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == 0;
        if (!folder) {
            GList* selected = gtk_icon_view_get_selected_items(GTK_ICON_VIEW(widget));
            if (gtk_icon_view_path_is_selected(GTK_ICON_VIEW(widget), path)) {
                for (GList* node = selected; node != nullptr; node = node->next) {
                    gchar* selectedPath = gtk_tree_path_to_string(static_cast<GtkTreePath*>(node->data));
                    if (selectedPath != nullptr) browser->pendingDragSelectionPaths.emplace_back(selectedPath);
                    g_free(selectedPath);
                }
            }
            for (GList* node = selected; node != nullptr; node = node->next) gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
            g_list_free(selected);
            if (browser->pendingDragSelectionPaths.empty()) {
                if (manualDragPress) {
                    gtk_icon_view_unselect_all(GTK_ICON_VIEW(widget));
                    gtk_icon_view_select_path(GTK_ICON_VIEW(widget), path);
                }
                gchar* selectedPath = gtk_tree_path_to_string(path);
                if (selectedPath != nullptr) browser->pendingDragSelectionPaths.emplace_back(selectedPath);
                g_free(selectedPath);
            }
        }
#ifdef _WIN32
        if (manualDragPress) {
            browser->nativeDragButton = event->button;
            browser->nativeDragX = static_cast<gint>(event->x);
            browser->nativeDragY = static_cast<gint>(event->y);
            GdkWindow* surface = gtk_widget_get_window(widget);
            if (surface != nullptr) SetCapture(reinterpret_cast<HWND>(GDK_WINDOW_HWND(surface)));
        }
#endif
        gtk_tree_path_free(path);
        if (manualDragPress) gtk_widget_grab_focus(widget);
        return manualDragPress;
    }
    if (event->type != GDK_2BUTTON_PRESS || event->button != GDK_BUTTON_PRIMARY) { gtk_tree_path_free(path); return false; }
    GtkTreeIter row;
    gchar* itemPath = nullptr;
    gchar* rights = nullptr;
    gboolean folder = false;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->modernItems), &row, path)) gtk_tree_model_get(GTK_TREE_MODEL(browser->modernItems), &row, ModernPathColumn, &itemPath, ModernFolderColumn, &folder, ModernRightsColumn, &rights, -1);
    gtk_tree_path_free(path);
    if (itemPath != nullptr && *itemPath != '\0') {
        if (folder) browser->navigateTo(itemPath);
        else {
            browser->pendingExternalPath = itemPath;
            browser->watchExternalWritable = rights != nullptr && std::string(rights).find('w') != std::string::npos;
            if (!rc_filebrowser_download(browser->connection, itemPath)) browser->appendLog(rc_last_error(browser->connection));
        }
    }
    g_free(itemPath);
    g_free(rights);
    return true;
}

gboolean TFileBrowserTree::onModernKeyPress(GtkWidget*, GdkEventKey* event, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    if (event->keyval == GDK_KEY_Escape && gtk_widget_get_visible(browser->modernSearchPopover)) {
        gtk_widget_hide(browser->modernSearchPopover);
        gtk_entry_set_text(GTK_ENTRY(browser->modernSearchEntry), "");
        return true;
    }
    const bool explicitSearch = (event->state & GDK_CONTROL_MASK) != 0 && (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F);
    const gunichar character = gdk_keyval_to_unicode(event->keyval);
    const bool typedSearch = character != 0 && !g_unichar_iscntrl(character) && (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) == 0;
    if (!explicitSearch && !typedSearch) return false;
    gtk_widget_show_all(browser->modernSearchPopover);
    gtk_popover_popup(GTK_POPOVER(browser->modernSearchPopover));
    gtk_widget_grab_focus(browser->modernSearchEntry);
    if (typedSearch) {
        gchar encoded[7] = {};
        g_unichar_to_utf8(character, encoded);
        gtk_entry_set_text(GTK_ENTRY(browser->modernSearchEntry), encoded);
        gtk_editable_set_position(GTK_EDITABLE(browser->modernSearchEntry), -1);
    }
    return true;
}

void TFileBrowserTree::onModernSearchChanged(GtkSearchEntry* entry, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    const gchar* query = gtk_entry_get_text(GTK_ENTRY(entry));
    if (query == nullptr || *query == '\0') return;
    gchar* foldedQuery = g_utf8_strdown(query, -1);
    GtkTreeIter row;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(browser->modernItems), &row);
    int index = 0;
    while (valid) {
        gchar* name = nullptr;
        gtk_tree_model_get(GTK_TREE_MODEL(browser->modernItems), &row, ModernNameColumn, &name, -1);
        gchar* foldedName = g_utf8_strdown(name == nullptr ? "" : name, -1);
        const bool match = std::strstr(foldedName, foldedQuery) != nullptr;
        g_free(foldedName);
        g_free(name);
        if (match) {
            GtkTreePath* path = gtk_tree_path_new_from_indices(index, -1);
            gtk_icon_view_unselect_all(GTK_ICON_VIEW(browser->modernView));
            gtk_icon_view_select_path(GTK_ICON_VIEW(browser->modernView), path);
            gtk_icon_view_scroll_to_path(GTK_ICON_VIEW(browser->modernView), path, true, 0.5f, 0.5f);
            gtk_tree_path_free(path);
            break;
        }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(browser->modernItems), &row);
        ++index;
    }
    g_free(foldedQuery);
}

gboolean TFileBrowserTree::onModernMotion(GtkWidget* widget, GdkEventMotion* event, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
#ifdef _WIN32
    if (browser->nativeDragButton != 0 && gtk_drag_check_threshold(widget, browser->nativeDragX, browser->nativeDragY, static_cast<gint>(event->x), static_cast<gint>(event->y))) {
        browser->hidePreview();
        browser->showDragPreview(static_cast<int>(event->x_root), static_cast<int>(event->y_root));
        GdkWindow* browserWindow = gtk_widget_get_window(browser->window);
        gint browserX = 0;
        gint browserY = 0;
        if (browserWindow != nullptr) gdk_window_get_origin(browserWindow, &browserX, &browserY);
        const bool insideBrowser = browserWindow != nullptr && event->x_root >= browserX && event->x_root < browserX + gtk_widget_get_allocated_width(browser->window) && event->y_root >= browserY && event->y_root < browserY + gtk_widget_get_allocated_height(browser->window);
        if (!insideBrowser) {
            browser->startNativeDrag(browser->modernView);
            browser->nativeDragButton = 0;
            return true;
        }
        GdkWindow* folderWindow = gtk_widget_get_window(browser->folderView);
        gint folderX = 0;
        gint folderY = 0;
        if (folderWindow != nullptr) gdk_window_get_origin(folderWindow, &folderX, &folderY);
        const bool overFolderTree = folderWindow != nullptr && event->x_root >= folderX && event->x_root < folderX + gtk_widget_get_allocated_width(browser->folderView) && event->y_root >= folderY && event->y_root < folderY + gtk_widget_get_allocated_height(browser->folderView);
        GtkTreePath* folderPath = nullptr;
        if (overFolderTree) gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(browser->folderView), static_cast<gint>(event->x_root) - folderX, static_cast<gint>(event->y_root) - folderY, &folderPath, nullptr, nullptr, nullptr);
        gtk_tree_view_set_drag_dest_row(GTK_TREE_VIEW(browser->folderView), folderPath, GTK_TREE_VIEW_DROP_INTO_OR_BEFORE);
        if (folderPath != nullptr) gtk_tree_path_free(folderPath);
        GtkTreePath* modernPath = overFolderTree ? nullptr : gtk_icon_view_get_path_at_pos(GTK_ICON_VIEW(widget), static_cast<gint>(event->x), static_cast<gint>(event->y));
        gboolean modernFolder = false;
        if (modernPath != nullptr) {
            GtkTreeIter row;
            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->modernItems), &row, modernPath)) gtk_tree_model_get(GTK_TREE_MODEL(browser->modernItems), &row, ModernFolderColumn, &modernFolder, -1);
        }
        gtk_icon_view_set_drag_dest_item(GTK_ICON_VIEW(widget), modernFolder ? modernPath : nullptr, GTK_ICON_VIEW_DROP_INTO);
        if (modernPath != nullptr) gtk_tree_path_free(modernPath);
        return true;
    }
#endif
    if (!browser->hoverPreviews) { browser->hidePreview(); return false; }
    GtkTreePath* path = gtk_icon_view_get_path_at_pos(GTK_ICON_VIEW(widget), static_cast<gint>(event->x), static_cast<gint>(event->y));
    if (path == nullptr) { browser->hidePreview(); return false; }
    GtkTreeIter row;
    gchar* itemPath = nullptr;
    gboolean folder = false;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->modernItems), &row, path)) gtk_tree_model_get(GTK_TREE_MODEL(browser->modernItems), &row, ModernPathColumn, &itemPath, ModernFolderColumn, &folder, -1);
    gtk_tree_path_free(path);
    browser->hoveredPreviewPath = !folder && itemPath != nullptr ? itemPath : "";
    g_free(itemPath);
    browser->previewRootX = static_cast<int>(event->x_root);
    browser->previewRootY = static_cast<int>(event->y_root);
    if (browser->hoveredPreviewPath.empty()) browser->hidePreview();
    else browser->showPreview(browser->hoveredPreviewPath, browser->previewRootX, browser->previewRootY);
    return false;
}

#ifdef _WIN32
gboolean TFileBrowserTree::onFileButtonRelease(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    if (browser->nativeDragButton != event->button) return false;
    ReleaseCapture();
    browser->nativeDragButton = 0;
    browser->hideDragPreview();
    GtkTreeModel* model = GTK_TREE_MODEL(browser->modernItems);
    GList* rows = nullptr;
    for (const std::string& pathText : browser->pendingDragSelectionPaths) {
        GtkTreePath* sourcePath = gtk_tree_path_new_from_string(pathText.c_str());
        if (sourcePath != nullptr) rows = g_list_append(rows, sourcePath);
    }
    browser->pendingDragSelectionPaths.clear();
    if (rows == nullptr) rows = selectedRows(browser->modernView, &model);
    GtkTreePath* targetPath = nullptr;
    bool targetFolderTree = widget == browser->folderView;
    if (widget == browser->modernView) {
        GdkWindow* folderWindow = gtk_widget_get_window(browser->folderView);
        gint folderX = 0;
        gint folderY = 0;
        if (folderWindow != nullptr) gdk_window_get_origin(folderWindow, &folderX, &folderY);
        targetFolderTree = folderWindow != nullptr && event->x_root >= folderX && event->x_root < folderX + gtk_widget_get_allocated_width(browser->folderView) && event->y_root >= folderY && event->y_root < folderY + gtk_widget_get_allocated_height(browser->folderView);
        if (targetFolderTree) gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(browser->folderView), static_cast<gint>(event->x_root) - folderX, static_cast<gint>(event->y_root) - folderY, &targetPath, nullptr, nullptr, nullptr);
        else targetPath = gtk_icon_view_get_path_at_pos(GTK_ICON_VIEW(widget), static_cast<gint>(event->x), static_cast<gint>(event->y));
    }
    else if (widget == browser->folderView) gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), static_cast<gint>(event->x), static_cast<gint>(event->y), &targetPath, nullptr, nullptr, nullptr);
    else return false;
    gtk_tree_view_set_drag_dest_row(GTK_TREE_VIEW(browser->folderView), nullptr, GTK_TREE_VIEW_DROP_BEFORE);
    gtk_icon_view_set_drag_dest_item(GTK_ICON_VIEW(browser->modernView), nullptr, GTK_ICON_VIEW_DROP_INTO);
    if (targetPath == nullptr) {
        for (GList* node = rows; node != nullptr; node = node->next) gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
        g_list_free(rows);
        return false;
    }
    GtkTreeIter target;
    gboolean folder = false;
    gchar* destination = nullptr;
    if (!targetFolderTree) {
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->modernItems), &target, targetPath)) gtk_tree_model_get(GTK_TREE_MODEL(browser->modernItems), &target, ModernPathColumn, &destination, ModernFolderColumn, &folder, -1);
    } else if (gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->folders), &target, targetPath)) {
        gtk_tree_model_get(GTK_TREE_MODEL(browser->folders), &target, FolderPathColumn, &destination, -1);
        folder = destination != nullptr;
    }
    gtk_tree_path_free(targetPath);
    if (!folder || destination == nullptr) {
        for (GList* node = rows; node != nullptr; node = node->next) gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
        g_list_free(rows);
        g_free(destination);
        return false;
    }
    bool moved = false;
    for (GList* node = rows; node != nullptr; node = node->next) {
        GtkTreeIter row;
        gchar* source = nullptr;
        gboolean sourceFolder = false;
        if (gtk_tree_model_get_iter(model, &row, static_cast<GtkTreePath*>(node->data))) gtk_tree_model_get(model, &row, ModernPathColumn, &source, ModernFolderColumn, &sourceFolder, -1);
        if (!sourceFolder && source != nullptr && *source != '\0') {
            if (rc_filebrowser_move(browser->connection, destination, source)) {
                browser->appendLog((std::string("Moved file ") + source + " to " + destination).c_str());
                moved = true;
            }
            else browser->appendLog(rc_last_error(browser->connection));
        }
        g_free(source);
        gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
    }
    g_list_free(rows);
    g_free(destination);
    if (moved) browser->queueMutationRefresh();
    return moved;
}
#endif

gboolean TFileBrowserTree::onFileLeave(GtkWidget* widget, GdkEventCrossing* event, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
#ifdef _WIN32
    if ((widget == browser->modernView || widget == browser->folderView) && browser->nativeDragButton != 0) {
        GdkWindow* browserWindow = gtk_widget_get_window(browser->window);
        gint browserX = 0;
        gint browserY = 0;
        if (browserWindow != nullptr) gdk_window_get_origin(browserWindow, &browserX, &browserY);
        const bool insideBrowser = browserWindow != nullptr && event->x_root >= browserX && event->x_root < browserX + gtk_widget_get_allocated_width(browser->window) && event->y_root >= browserY && event->y_root < browserY + gtk_widget_get_allocated_height(browser->window);
        if (insideBrowser) return false;
        gtk_tree_view_set_drag_dest_row(GTK_TREE_VIEW(browser->folderView), nullptr, GTK_TREE_VIEW_DROP_BEFORE);
        gtk_icon_view_set_drag_dest_item(GTK_ICON_VIEW(browser->modernView), nullptr, GTK_ICON_VIEW_DROP_INTO);
        browser->hidePreview();
        browser->startNativeDrag(browser->modernView);
        browser->nativeDragButton = 0;
        return true;
    }
#endif
    browser->hidePreview();
    browser->hideDragPreview();
    return false;
}

gboolean TFileBrowserTree::onFileMotion(GtkWidget* widget, GdkEventMotion* event, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
#ifdef _WIN32
    if (browser->nativeDragButton != 0) {
        if (!gtk_drag_check_threshold(widget, browser->nativeDragX, browser->nativeDragY, static_cast<gint>(event->x), static_cast<gint>(event->y))) return true;
        if (browser->inlineRenameId != 0) { g_source_remove(browser->inlineRenameId); browser->inlineRenameId = 0; browser->pendingInlineRenamePath.clear(); }
        browser->hidePreview();
        browser->startNativeDrag(widget);
        browser->nativeDragButton = 0;
        return true;
    }
#endif
    if (!browser->hoverPreviews) { browser->hidePreview(); return false; }
    GtkTreePath* path = nullptr;
    if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), static_cast<gint>(event->x), static_cast<gint>(event->y), &path, nullptr, nullptr, nullptr)) { browser->hidePreview(); return false; }
    GtkTreeIter row;
    gchar* remotePath = nullptr;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->files), &row, path)) gtk_tree_model_get(GTK_TREE_MODEL(browser->files), &row, FilePathColumn, &remotePath, -1);
    gtk_tree_path_free(path);
    browser->hoveredPreviewPath = remotePath == nullptr ? "" : remotePath;
    g_free(remotePath);
    browser->previewRootX = static_cast<int>(event->x_root);
    browser->previewRootY = static_cast<int>(event->y_root);
    browser->showPreview(browser->hoveredPreviewPath, browser->previewRootX, browser->previewRootY);
    return false;
}

void TFileBrowserTree::clearPreviewCache() {
    hidePreview();
    for (const auto& item : previewCache) g_object_unref(item.second);
    previewCache.clear();
    previewCacheOrder.clear();
}

void TFileBrowserTree::hidePreview() {
    hoveredPreviewPath.clear();
    if (previewWindow != nullptr) gtk_widget_hide(previewWindow);
}

void TFileBrowserTree::hideDragPreview() {
    if (dragPreviewWindow != nullptr) gtk_widget_hide(dragPreviewWindow);
}

void TFileBrowserTree::showDragPreview(int rootX, int rootY) {
    GtkTreeModel* model = GTK_TREE_MODEL(modernItems);
    GList* rows = nullptr;
    for (const std::string& pathText : pendingDragSelectionPaths) {
        GtkTreePath* path = gtk_tree_path_new_from_string(pathText.c_str());
        if (path != nullptr) rows = g_list_append(rows, path);
    }
    if (rows == nullptr) rows = selectedRows(modernView, &model);
    GdkPixbuf* icon = nullptr;
    gchar* name = nullptr;
    int count = 0;
    for (GList* node = rows; node != nullptr; node = node->next) {
        GtkTreeIter row;
        gboolean folder = false;
        gchar* path = nullptr;
        GdkPixbuf* rowIcon = nullptr;
        if (gtk_tree_model_get_iter(model, &row, static_cast<GtkTreePath*>(node->data))) gtk_tree_model_get(model, &row, ModernIconColumn, &rowIcon, ModernPathColumn, &path, ModernFolderColumn, &folder, -1);
        if (!folder && path != nullptr) {
            if (count == 0) { name = g_path_get_basename(path); icon = rowIcon; rowIcon = nullptr; }
            ++count;
        }
        if (rowIcon != nullptr) g_object_unref(rowIcon);
        g_free(path);
        gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
    }
    g_list_free(rows);
    if (count == 0) { if (icon != nullptr) g_object_unref(icon); g_free(name); hideDragPreview(); return; }
    if (dragPreviewWindow == nullptr) {
        dragPreviewWindow = gtk_window_new(GTK_WINDOW_POPUP);
        gtk_window_set_type_hint(GTK_WINDOW(dragPreviewWindow), GDK_WINDOW_TYPE_HINT_TOOLTIP);
        GtkWidget* frame = gtk_frame_new(nullptr);
        gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
        gtk_container_add(GTK_CONTAINER(dragPreviewWindow), frame);
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_container_set_border_width(GTK_CONTAINER(box), 6);
        gtk_container_add(GTK_CONTAINER(frame), box);
        dragPreviewImage = gtk_image_new();
        dragPreviewLabel = gtk_label_new("");
        gtk_box_pack_start(GTK_BOX(box), dragPreviewImage, false, false, 0);
        gtk_box_pack_start(GTK_BOX(box), dragPreviewLabel, false, false, 0);
    }
    gtk_image_set_from_pixbuf(GTK_IMAGE(dragPreviewImage), icon);
    std::string label = name == nullptr ? "" : name;
    if (count > 1) label += " +" + std::to_string(count - 1) + " more";
    gtk_label_set_text(GTK_LABEL(dragPreviewLabel), label.c_str());
    gtk_widget_show_all(dragPreviewWindow);
    gtk_window_move(GTK_WINDOW(dragPreviewWindow), rootX + 14, rootY + 14);
    if (icon != nullptr) g_object_unref(icon);
    g_free(name);
}

void TFileBrowserTree::showPreview(const std::string& path, int rootX, int rootY) {
    const auto iterator = previewCache.find(path);
    if (iterator == previewCache.end()) { if (previewWindow != nullptr) gtk_widget_hide(previewWindow); return; }
    gtk_image_set_from_pixbuf(GTK_IMAGE(previewImage), iterator->second);
    gchar* basename = g_path_get_basename(path.c_str());
    gtk_label_set_text(GTK_LABEL(previewLabel), basename == nullptr ? path.c_str() : basename);
    g_free(basename);
    gtk_widget_show_all(previewWindow);
    int width = 0, height = 0;
    gtk_window_get_size(GTK_WINDOW(previewWindow), &width, &height);
    GdkScreen* screen = gtk_widget_get_screen(fileView);
    const int x = std::max(0, std::min(rootX + 18, gdk_screen_get_width(screen) - width - 8));
    const int y = std::max(0, std::min(rootY + 18, gdk_screen_get_height(screen) - height - 8));
    gtk_window_move(GTK_WINDOW(previewWindow), x, y);
}

bool TFileBrowserTree::canAutoPreview(const std::string& path) const {
    if (!isPreviewImage(path)) return false;
    const auto sizeEntry = previewFileSizes.find(path);
    if (sizeEntry == previewFileSizes.end() || sizeEntry->second == 0) return false;
    gchar* loweredText = g_ascii_strdown(path.c_str(), -1);
    const std::string lowered = loweredText == nullptr ? path : loweredText;
    g_free(loweredText);
    const bool animated = lowered.ends_with(".gif") || lowered.ends_with(".mng");
    if (animated) return false;
    const bool tileset = lowered.find("tileset") != std::string::npos || lowered.find("_tiles") != std::string::npos || lowered.find("-tiles") != std::string::npos;
    const std::uint64_t limit = tileset ? 128 * 1024 : 512 * 1024;
    return sizeEntry->second <= limit;
}

void TFileBrowserTree::cachePreview(const std::string& path, const void* content, int length) {
    auto request = std::make_shared<PreviewDecodeRequest>();
    request->state = previewAsyncState;
    request->path = path;
    request->folder = previewFolder;
    request->generation = previewAsyncState->generation;
    request->content.assign(static_cast<const std::uint8_t*>(content), static_cast<const std::uint8_t*>(content) + length);
    {
        std::lock_guard<std::mutex> lock(previewWorkerMutex);
        previewWorkerJobs.clear();
        previewWorkerJobs.push_back(std::move(request));
    }
    previewWorkerCondition.notify_one();
}

void TFileBrowserTree::previewWorkerLoop() {
    while (true) {
        std::shared_ptr<PreviewDecodeRequest> request;
        {
            std::unique_lock<std::mutex> lock(previewWorkerMutex);
            previewWorkerCondition.wait(lock, [&] { return previewWorkerStop || !previewWorkerJobs.empty(); });
            if (previewWorkerStop) return;
            request = std::move(previewWorkerJobs.front());
            previewWorkerJobs.pop_front();
        }
        const std::size_t dot = request->path.find_last_of('.');
        gchar* lower = dot == std::string::npos ? nullptr : g_ascii_strdown(request->path.c_str() + dot, -1);
        const bool isMng = lower != nullptr && std::string(lower) == ".mng";
        g_free(lower);
        std::vector<std::uint8_t> mngFrame;
        if (isMng) mngFrame = TMng::firstPngFrame(request->content.data(), request->content.size());
        if (!isMng || !mngFrame.empty()) {
            const void* imageContent = isMng ? mngFrame.data() : request->content.data();
            const int imageLength = static_cast<int>(isMng ? mngFrame.size() : request->content.size());
            GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
            g_signal_connect(loader, "size-prepared", G_CALLBACK(+[](GdkPixbufLoader* value, int width, int height, gpointer) {
                const double scale = std::min(1.0, std::min(360.0 / std::max(1, width), 260.0 / std::max(1, height)));
                gdk_pixbuf_loader_set_size(value, std::max(1, static_cast<int>(width * scale)), std::max(1, static_cast<int>(height * scale)));
            }), nullptr);
            GError* error = nullptr;
            const bool loaded = gdk_pixbuf_loader_write(loader, static_cast<const guchar*>(imageContent), imageLength, &error) && gdk_pixbuf_loader_close(loader, &error);
            GdkPixbuf* pixbuf = loaded ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr;
            if (pixbuf != nullptr) {
                request->preview = GDK_PIXBUF(g_object_ref(pixbuf));
                const int width = gdk_pixbuf_get_width(pixbuf);
                const int height = gdk_pixbuf_get_height(pixbuf);
                const double scale = std::min(88.0 / std::max(1, width), 88.0 / std::max(1, height));
                request->thumbnail = gdk_pixbuf_scale_simple(pixbuf, std::max(1, static_cast<int>(width * scale)), std::max(1, static_cast<int>(height * scale)), GDK_INTERP_BILINEAR);
            }
            if (error != nullptr) g_error_free(error);
            g_object_unref(loader);
        }
        auto* completion = new std::shared_ptr<PreviewDecodeRequest>(std::move(request));
        g_main_context_invoke_full(nullptr, G_PRIORITY_DEFAULT, onPreviewDecoded, completion, +[](gpointer data) { delete static_cast<std::shared_ptr<PreviewDecodeRequest>*>(data); });
    }
}

gboolean TFileBrowserTree::onPreviewDecoded(gpointer data) {
    std::shared_ptr<PreviewDecodeRequest> request = std::move(*static_cast<std::shared_ptr<PreviewDecodeRequest>*>(data));
    TFileBrowserTree* browser = request->state->browser;
    if (browser != nullptr && request->preview != nullptr && request->generation == request->state->generation && request->folder == browser->previewFolder && (!browser->modernFileBrowser || std::find(browser->visiblePreviewPaths.begin(), browser->visiblePreviewPaths.end(), request->path) != browser->visiblePreviewPaths.end())) {
        const auto existing = browser->previewCache.find(request->path);
        if (existing != browser->previewCache.end()) g_object_unref(existing->second);
        else browser->previewCacheOrder.push_back(request->path);
        browser->previewCache[request->path] = request->preview;
        request->preview = nullptr;
        while (browser->previewCacheOrder.size() > 50) { const std::string oldest = browser->previewCacheOrder.front(); browser->previewCacheOrder.erase(browser->previewCacheOrder.begin()); g_object_unref(browser->previewCache[oldest]); browser->previewCache.erase(oldest); }
        const auto row = browser->modernItemIndices.find(request->path);
        if (browser->modernThumbnails && request->thumbnail != nullptr && row != browser->modernItemIndices.end()) {
            GtkTreeIter item;
            GtkTreePath* itemPath = gtk_tree_path_new_from_indices(row->second, -1);
            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->modernItems), &item, itemPath)) gtk_list_store_set(browser->modernItems, &item, ModernIconColumn, request->thumbnail, -1);
            gtk_tree_path_free(itemPath);
        }
        if (browser->hoverPreviews && browser->hoveredPreviewPath == request->path) browser->showPreview(request->path, browser->previewRootX, browser->previewRootY);
    }
    if (request->thumbnail != nullptr) { g_object_unref(request->thumbnail); request->thumbnail = nullptr; }
    if (request->preview != nullptr) { g_object_unref(request->preview); request->preview = nullptr; }
    if (browser != nullptr) browser->startNextPreviewDownload();
    return G_SOURCE_REMOVE;
}

#ifdef _WIN32
void TFileBrowserTree::startNativeDrag(GtkWidget* widget) {
    cleanupDragState();
    pendingExternalPath.clear();
    GtkTreeModel* model = GTK_IS_ICON_VIEW(widget) ? gtk_icon_view_get_model(GTK_ICON_VIEW(widget)) : gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
    GList* rows = nullptr;
    for (const std::string& pathText : pendingDragSelectionPaths) {
        GtkTreePath* path = gtk_tree_path_new_from_string(pathText.c_str());
        if (path != nullptr) rows = g_list_append(rows, path);
    }
    pendingDragSelectionPaths.clear();
    if (rows == nullptr) rows = selectedRows(widget, nullptr);
    if (rows == nullptr) return;
    std::vector<std::string> remotePaths;
    std::vector<std::wstring> names;
    std::vector<ULONGLONG> sizes;
    for (GList* node = rows; node != nullptr; node = node->next) {
        GtkTreeIter row;
        gchar* remotePath = nullptr;
        if (!gtk_tree_model_get_iter(model, &row, static_cast<GtkTreePath*>(node->data))) continue;
        gboolean folder = false;
        if (GTK_IS_ICON_VIEW(widget)) gtk_tree_model_get(model, &row, ModernPathColumn, &remotePath, ModernFolderColumn, &folder, -1);
        else gtk_tree_model_get(model, &row, FilePathColumn, &remotePath, -1);
        if (folder || remotePath == nullptr || *remotePath == '\0') { g_free(remotePath); continue; }
        gchar* basename = g_path_get_basename(remotePath);
        const std::string fileName = basename == nullptr ? "download" : basename;
        g_free(basename);
        gunichar2* wideName = g_utf8_to_utf16(fileName.c_str(), -1, nullptr, nullptr, nullptr);
        if (wideName == nullptr) { g_free(remotePath); continue; }
        names.emplace_back(reinterpret_cast<wchar_t*>(wideName));
        g_free(wideName);
        remotePaths.emplace_back(remotePath);
        const auto size = previewFileSizes.find(remotePath);
        sizes.push_back(size == previewFileSizes.end() ? 0 : size->second);
        g_free(remotePath);
    }
    pendingNativeDragContents.clear();
    const auto dropAccepted = std::make_shared<bool>(false);
    GdkWindow* previewSurface = dragPreviewWindow == nullptr ? nullptr : gtk_widget_get_window(dragPreviewWindow);
    HWND previewHandle = previewSurface == nullptr ? nullptr : reinterpret_cast<HWND>(GDK_WINDOW_HWND(previewSurface));
    const HRESULT dragResult = nativeFileDrag(names, sizes, [this, remotePaths](size_t index, std::vector<guint8>& content) {
        if (index >= remotePaths.size()) return false;
        const std::string& remotePath = remotePaths[index];
        if (!pendingNativeDragContents.contains(remotePath)) {
            pendingNativeDragContents.emplace(remotePath, std::vector<guint8>());
            if (!rc_filebrowser_download(connection, remotePath.c_str())) { pendingNativeDragContents.erase(remotePath); appendLog(rc_last_error(connection)); return false; }
            const gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
            while (!pendingNativeDragContents.contains(remotePath) && g_get_monotonic_time() < deadline) {
                while (gtk_events_pending()) gtk_main_iteration();
                g_usleep(10000);
            }
        }
        const auto received = pendingNativeDragContents.find(remotePath);
        if (received == pendingNativeDragContents.end()) { appendLog((std::string("Timed out downloading dragged file ") + remotePath).c_str()); return false; }
        content = received->second;
        return true;
    }, dropAccepted, previewHandle);
    if (FAILED(dragResult)) {
        std::ostringstream error;
        error << "Could not start Windows file drag (0x" << std::hex << std::uppercase << static_cast<unsigned long>(dragResult) << ").";
        appendLog(error.str().c_str());
    }
    hideDragPreview();
    cleanupDragState();
    for (GList* node = rows; node != nullptr; node = node->next) gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
    g_list_free(rows);
}
#endif

void TFileBrowserTree::onFileDragBegin(GtkWidget* widget, GdkDragContext* context, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    if (!browser->pendingDragSelectionPaths.empty()) {
        if (GTK_IS_ICON_VIEW(widget)) gtk_icon_view_unselect_all(GTK_ICON_VIEW(widget));
        GtkTreeSelection* selection = GTK_IS_TREE_VIEW(widget) ? gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)) : nullptr;
        if (selection != nullptr) gtk_tree_selection_unselect_all(selection);
        for (const std::string& pathText : browser->pendingDragSelectionPaths) {
            GtkTreePath* path = gtk_tree_path_new_from_string(pathText.c_str());
            if (path != nullptr) {
                if (GTK_IS_ICON_VIEW(widget)) gtk_icon_view_select_path(GTK_ICON_VIEW(widget), path);
                else gtk_tree_selection_select_path(selection, path);
                gtk_tree_path_free(path);
            }
        }
        browser->pendingDragSelectionPaths.clear();
    }
    browser->cleanupDragState();
    browser->pendingExternalPath.clear();
    GtkTreeModel* model = nullptr;
    GList* rows = selectedRows(widget, &model);
    if (rows == nullptr) return;
    browser->dragStagingFolder = createDragStagingFolder();
    for (GList* node = rows; node != nullptr; node = node->next) {
        GtkTreeIter selectedRow;
        gchar* remotePath = nullptr;
        if (!gtk_tree_model_get_iter(model, &selectedRow, static_cast<GtkTreePath*>(node->data))) continue;
        gboolean folder = false;
        if (GTK_IS_ICON_VIEW(widget)) gtk_tree_model_get(model, &selectedRow, ModernPathColumn, &remotePath, ModernFolderColumn, &folder, -1);
        else gtk_tree_model_get(model, &selectedRow, FilePathColumn, &remotePath, -1);
        if (folder || remotePath == nullptr || *remotePath == '\0' || browser->dragStagingFolder.empty()) { g_free(remotePath); continue; }
        gchar* basename = g_path_get_basename(remotePath);
        const std::string fileName = basename == nullptr ? "download" : basename;
        std::string localPath = browser->dragStagingFolder + G_DIR_SEPARATOR_S + fileName;
        g_free(basename);
        int suffix = 2;
        while (g_file_test(localPath.c_str(), G_FILE_TEST_EXISTS)) localPath = browser->dragStagingFolder + G_DIR_SEPARATOR_S + std::to_string(suffix++) + "-" + fileName;
        browser->pendingDragDownloads.emplace(remotePath, localPath);
        browser->pendingDragLocalPaths.push_back(localPath);
        if (!rc_filebrowser_download(browser->connection, remotePath)) browser->appendLog(rc_last_error(browser->connection));
        g_free(remotePath);
    }
    GtkTreeIter row;
    GtkTreePath* path = static_cast<GtkTreePath*>(rows->data);
    if (gtk_tree_model_get_iter(model, &row, path)) {
        GdkPixbuf* icon = nullptr;
        gchar* name = nullptr;
        gtk_tree_model_get(model, &row, GTK_IS_ICON_VIEW(widget) ? ModernIconColumn : FileIconColumn, &icon, GTK_IS_ICON_VIEW(widget) ? ModernPathColumn : FilePathColumn, &name, -1);
        GtkWidget* preview = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_widget_set_margin_start(preview, 6);
        gtk_widget_set_margin_end(preview, 6);
        gtk_widget_set_margin_top(preview, 4);
        gtk_widget_set_margin_bottom(preview, 4);
        if (icon != nullptr) {
            GtkWidget* image = gtk_image_new_from_pixbuf(icon);
            gtk_box_pack_start(GTK_BOX(preview), image, false, false, 0);
            g_object_unref(icon);
        }
        gchar* basename = name == nullptr ? nullptr : g_path_get_basename(name);
        std::string display = basename == nullptr ? "" : basename;
        const int count = g_list_length(rows);
        if (count > 1) display += " +" + std::to_string(count - 1) + " more";
        GtkWidget* label = gtk_label_new(display.c_str());
        gtk_label_set_max_width_chars(GTK_LABEL(label), 42);
        gtk_box_pack_start(GTK_BOX(preview), label, false, false, 0);
        gtk_widget_show_all(preview);
        gtk_drag_set_icon_widget(context, preview, 8, 8);
        g_free(basename);
        g_free(name);
    }
    for (GList* node = rows; node != nullptr; node = node->next) gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
    g_list_free(rows);
}

void TFileBrowserTree::onFileDragEnd(GtkWidget*, GdkDragContext*, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    browser->pendingDragSelectionPaths.clear();
    browser->cleanupDragState();
}

void TFileBrowserTree::onFileDragDataGet(GtkWidget* widget, GdkDragContext*, GtkSelectionData* selection, guint, guint targetInfo, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    if (targetInfo == 2) {
        const gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
        while (!browser->pendingDragDownloads.empty() && g_get_monotonic_time() < deadline) {
            while (gtk_events_pending()) gtk_main_iteration();
            g_usleep(10000);
        }
        std::vector<gchar*> uris;
        for (const std::string& localPath : browser->pendingDragLocalPaths) {
            if (!g_file_test(localPath.c_str(), G_FILE_TEST_EXISTS)) continue;
            gchar* uri = g_filename_to_uri(localPath.c_str(), nullptr, nullptr);
            if (uri != nullptr) uris.push_back(uri);
        }
        if (!uris.empty()) {
            uris.push_back(nullptr);
            gtk_selection_data_set_uris(selection, uris.data());
            for (gchar* uri : uris) g_free(uri);
        }
        return;
    }
    GtkTreeModel* model = nullptr;
    GList* rows = selectedRows(widget, &model);
    std::string paths;
    for (GList* node = rows; node != nullptr; node = node->next) {
        GtkTreeIter row;
        gchar* path = nullptr;
        gboolean folder = false;
        if (gtk_tree_model_get_iter(model, &row, static_cast<GtkTreePath*>(node->data))) {
            if (GTK_IS_ICON_VIEW(widget)) gtk_tree_model_get(model, &row, ModernPathColumn, &path, ModernFolderColumn, &folder, -1);
            else gtk_tree_model_get(model, &row, FilePathColumn, &path, -1);
        }
        if (folder) { g_free(path); path = nullptr; }
        if (path != nullptr && *path != '\0') { if (!paths.empty()) paths += '\n'; paths += path; }
        g_free(path);
        gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
    }
    g_list_free(rows);
    if (!paths.empty()) gtk_selection_data_set(selection, gtk_selection_data_get_target(selection), 8, reinterpret_cast<const guchar*>(paths.data()), static_cast<gint>(paths.size()));
}

void TFileBrowserTree::onDropDataReceived(GtkWidget* widget, GdkDragContext* context, gint x, gint y, GtkSelectionData* selection, guint info, guint time, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    std::string destination = browser->currentFolder;
    if (widget == browser->folderView) {
        GtkTreePath* rowPath = nullptr;
        if (gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(widget), x, y, &rowPath, nullptr)) {
            GtkTreeIter row;
            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->folders), &row, rowPath)) {
                gchar* value = nullptr;
                gtk_tree_model_get(GTK_TREE_MODEL(browser->folders), &row, FolderPathColumn, &value, -1);
                if (value != nullptr) destination = value;
                g_free(value);
            }
            gtk_tree_path_free(rowPath);
        }
    } else if (widget == browser->modernView) {
        GtkTreePath* rowPath = gtk_icon_view_get_path_at_pos(GTK_ICON_VIEW(widget), x, y);
        if (rowPath != nullptr) {
            GtkTreeIter row;
            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->modernItems), &row, rowPath)) {
                gchar* value = nullptr;
                gboolean folder = false;
                gtk_tree_model_get(GTK_TREE_MODEL(browser->modernItems), &row, ModernPathColumn, &value, ModernFolderColumn, &folder, -1);
                if (folder && value != nullptr) destination = value;
                g_free(value);
            }
            gtk_tree_path_free(rowPath);
        }
    }
    bool success = false;
    if (info == 1) {
        const guchar* source = gtk_selection_data_get_data(selection);
        const gint length = gtk_selection_data_get_length(selection);
        if (source != nullptr && length > 0) {
            success = true;
            std::istringstream files(std::string(reinterpret_cast<const char*>(source), length));
            for (std::string filePath; std::getline(files, filePath);) {
                if (filePath.empty()) continue;
                if (rc_filebrowser_move(browser->connection, destination.c_str(), filePath.c_str())) browser->appendLog((std::string("Moved file ") + filePath + " to " + destination).c_str());
                else { browser->appendLog(rc_last_error(browser->connection)); success = false; }
            }
        }
    } else if (info == 2) {
        gchar** uris = gtk_selection_data_get_uris(selection);
        if (uris != nullptr) {
            success = true;
            for (int index = 0; uris[index] != nullptr; ++index) {
                GError* error = nullptr;
                gchar* filename = g_filename_from_uri(uris[index], nullptr, &error);
                gchar* contents = nullptr;
                gsize length = 0;
                if (filename == nullptr || !g_file_get_contents(filename, &contents, &length, &error)) {
                    success = false;
                    if (error != nullptr) { browser->appendLog(error->message); g_error_free(error); }
                } else {
                    gchar* basename = g_path_get_basename(filename);
                    std::string remotePath = destination;
                    if (!remotePath.empty() && remotePath.back() != '/') remotePath += '/';
                    remotePath += basename;
                    if (!rc_upload_file(browser->connection, remotePath.c_str(), contents, static_cast<int>(length))) { browser->appendLog(rc_last_error(browser->connection)); success = false; }
                    g_free(basename);
                    g_free(contents);
                }
                g_free(filename);
            }
            g_strfreev(uris);
        }
    }
    if (!success) browser->appendLog(rc_last_error(browser->connection));
    else browser->queueMutationRefresh();
    gtk_drag_finish(context, success, false, time);
}

void TFileBrowserTree::showItemMenu(GtkWidget* view, GdkEventButton* event, bool folder) {
    std::vector<std::string> itemPaths;
    if (GTK_IS_ICON_VIEW(view)) {
        GList* selectedRows = gtk_icon_view_get_selected_items(GTK_ICON_VIEW(view));
        for (GList* node = selectedRows; node != nullptr; node = node->next) {
            GtkTreeIter row;
            gchar* itemPath = nullptr;
            gboolean itemFolder = false;
            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(modernItems), &row, static_cast<GtkTreePath*>(node->data))) gtk_tree_model_get(GTK_TREE_MODEL(modernItems), &row, ModernPathColumn, &itemPath, ModernFolderColumn, &itemFolder, -1);
            if (itemPath != nullptr && *itemPath != '\0' && itemFolder == folder) itemPaths.emplace_back(itemPath);
            g_free(itemPath);
            gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
        }
        g_list_free(selectedRows);
    } else {
        GtkTreePath* path = nullptr;
        if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view), static_cast<gint>(event->x), static_cast<gint>(event->y), &path, nullptr, nullptr, nullptr)) return;
        GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
        if (!gtk_tree_selection_path_is_selected(selection, path)) { gtk_tree_selection_unselect_all(selection); gtk_tree_selection_select_path(selection, path); }
        GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
        GList* selectedRows = gtk_tree_selection_get_selected_rows(selection, &model);
        for (GList* node = selectedRows; node != nullptr; node = node->next) {
            GtkTreeIter selectedRow;
            gchar* itemPath = nullptr;
            if (gtk_tree_model_get_iter(model, &selectedRow, static_cast<GtkTreePath*>(node->data))) gtk_tree_model_get(model, &selectedRow, folder ? FolderPathColumn : FilePathColumn, &itemPath, -1);
            if (itemPath != nullptr && *itemPath != '\0') itemPaths.emplace_back(itemPath);
            g_free(itemPath);
            gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
        }
        g_list_free(selectedRows);
        gtk_tree_path_free(path);
    }
    if (itemPaths.empty()) return;
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* upload = gtk_menu_item_new_with_label("Upload file(s)");
    g_signal_connect(upload, "activate", G_CALLBACK(onUpload), this);
    if (!folder) {
        GtkWidget* download = gtk_menu_item_new_with_label(itemPaths.size() == 1 ? "Download" : "Download selected");
        GtkWidget* move = gtk_menu_item_new_with_label(itemPaths.size() == 1 ? "Move" : "Move selected");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), download);
        FileMenuItem* item = new FileMenuItem{this, itemPaths};
        FileMenuItem* moveItem = new FileMenuItem{this, itemPaths};
        g_signal_connect_data(download, "activate", G_CALLBACK(onDownload), item, destroyFileMenuItem, static_cast<GConnectFlags>(0));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), upload);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), move);
        g_signal_connect_data(move, "activate", G_CALLBACK(onMove), moveItem, destroyFileMenuItem, static_cast<GConnectFlags>(0));
    } else {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), upload);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    }
    if (!folder && itemPaths.size() == 1) {
        GtkWidget* editAsText = gtk_menu_item_new_with_label("Edit");
        gtk_menu_shell_insert(GTK_MENU_SHELL(menu), editAsText, 1);
        FileMenuItem* editItem = new FileMenuItem{this, itemPaths};
        g_signal_connect_data(editAsText, "activate", G_CALLBACK(onEditAsText), editItem, destroyFileMenuItem, static_cast<GConnectFlags>(0));
    }
    GtkWidget* remove = gtk_menu_item_new_with_label("Delete");
    GtkWidget* rename = nullptr;
    if (itemPaths.size() == 1) {
        rename = gtk_menu_item_new_with_label("Rename");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), rename);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), remove);
    FileMenuItem* deleteItem = new FileMenuItem{this, itemPaths};
    if (rename != nullptr) {
        FileMenuItem* renameItem = new FileMenuItem{this, itemPaths};
        g_signal_connect_data(rename, "activate", G_CALLBACK(onRename), renameItem, destroyFileMenuItem, static_cast<GConnectFlags>(0));
    }
    g_signal_connect_data(remove, "activate", G_CALLBACK(onDeleteItem), deleteItem, destroyFileMenuItem, static_cast<GConnectFlags>(0));
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
}

void TFileBrowserTree::onDownload(GtkMenuItem*, gpointer data) {
    FileMenuItem* item = static_cast<FileMenuItem*>(data);
    for (const std::string& path : item->paths) {
        ++item->browser->pendingUserDownloads[path];
        if (!rc_filebrowser_download(item->browser->connection, path.c_str())) { if (--item->browser->pendingUserDownloads[path] == 0) item->browser->pendingUserDownloads.erase(path); item->browser->appendLog(rc_last_error(item->browser->connection)); }
    }
}

void TFileBrowserTree::onEditAsText(GtkMenuItem*, gpointer data) {
    FileMenuItem* item = static_cast<FileMenuItem*>(data);
    if (item->paths.size() != 1) return;
    item->browser->pendingEditPath = item->paths.front();
    if (!rc_filebrowser_download(item->browser->connection, item->paths.front().c_str())) item->browser->appendLog(rc_last_error(item->browser->connection));
}

void TFileBrowserTree::onDeleteItem(GtkMenuItem*, gpointer data) {
    FileMenuItem* item = static_cast<FileMenuItem*>(data);
    bool changed = false;
    for (const std::string& path : item->paths) {
        if (rc_filebrowser_delete(item->browser->connection, path.c_str())) changed = true;
        else item->browser->appendLog(rc_last_error(item->browser->connection));
    }
    if (changed) item->browser->queueMutationRefresh();
}

void TFileBrowserTree::onRename(GtkMenuItem*, gpointer data) {
    FileMenuItem* item = static_cast<FileMenuItem*>(data);
    if (item->paths.size() != 1) return;
    if (item->browser->modernFileBrowser) {
        GtkWidget* dialog = gtk_dialog_new_with_buttons("Rename", nullptr, GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Rename", GTK_RESPONSE_ACCEPT, nullptr);
        GtkWidget* entry = gtk_entry_new();
        gchar* basename = g_path_get_basename(item->paths.front().c_str());
        gtk_entry_set_text(GTK_ENTRY(entry), basename == nullptr ? item->paths.front().c_str() : basename);
        g_free(basename);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, false, false, 8);
        gtk_widget_show_all(dialog);
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            const char* value = gtk_entry_get_text(GTK_ENTRY(entry));
            if (value != nullptr && *value != '\0') {
                if (rc_filebrowser_rename(item->browser->connection, item->paths.front().c_str(), value)) item->browser->queueMutationRefresh();
                else item->browser->appendLog(rc_last_error(item->browser->connection));
            }
        }
        gtk_widget_destroy(dialog);
        return;
    }
    GtkTreeModel* model = GTK_TREE_MODEL(item->browser->files);
    GtkTreeIter row;
    gboolean found = gtk_tree_model_get_iter_first(model, &row);
    while (found) {
        gchar* path = nullptr;
        gtk_tree_model_get(model, &row, FilePathColumn, &path, -1);
        const bool matches = path != nullptr && item->paths.front() == path;
        g_free(path);
        if (matches) {
            GtkTreePath* treePath = gtk_tree_model_get_path(model, &row);
            g_object_set(item->browser->fileNameRenderer, "editable", TRUE, nullptr);
            gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(item->browser->fileView), treePath, gtk_tree_view_get_column(GTK_TREE_VIEW(item->browser->fileView), 0), item->browser->fileNameRenderer, true);
            gtk_tree_path_free(treePath);
            return;
        }
        found = gtk_tree_model_iter_next(model, &row);
    }
}

void TFileBrowserTree::onFileNameEdited(GtkCellRendererText*, gchar* path, gchar* value, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    g_object_set(browser->fileNameRenderer, "editable", FALSE, nullptr);
    if (path == nullptr || value == nullptr || *value == '\0') return;
    GtkTreePath* treePath = gtk_tree_path_new_from_string(path);
    GtkTreeIter row;
    if (treePath == nullptr || !gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->files), &row, treePath)) { if (treePath != nullptr) gtk_tree_path_free(treePath); return; }
    gchar* oldPath = nullptr;
    gtk_tree_model_get(GTK_TREE_MODEL(browser->files), &row, FilePathColumn, &oldPath, -1);
    gtk_tree_path_free(treePath);
    if (oldPath == nullptr || *oldPath == '\0' || std::string(oldPath) == value) { g_free(oldPath); return; }
    if (!rc_filebrowser_rename(browser->connection, oldPath, value)) browser->appendLog(rc_last_error(browser->connection));
    else browser->queueMutationRefresh();
    g_free(oldPath);
}

void TFileBrowserTree::onMove(GtkMenuItem*, gpointer data) {
    FileMenuItem* item = static_cast<FileMenuItem*>(data);
    GtkWidget* dialog = gtk_dialog_new_with_buttons((item->paths.size() == 1 ? "Move " + item->paths.front() : "Move selected files").c_str(), nullptr, GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "OK", GTK_RESPONSE_ACCEPT, nullptr);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), item->browser->currentFolder.c_str());
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, false, false, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        bool changed = false;
        for (const std::string& path : item->paths) {
            if (rc_filebrowser_move(item->browser->connection, gtk_entry_get_text(GTK_ENTRY(entry)), path.c_str())) {
                item->browser->appendLog((std::string("Moved file ") + path + " to " + gtk_entry_get_text(GTK_ENTRY(entry))).c_str());
                changed = true;
            }
            else item->browser->appendLog(rc_last_error(item->browser->connection));
        }
        if (changed) item->browser->queueMutationRefresh();
    }
    gtk_widget_destroy(dialog);
}

void TFileBrowserTree::onUpload(GtkMenuItem*, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Upload file(s)", nullptr, GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Upload", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), true);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GSList* filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
        bool changed = false;
        for (GSList* node = filenames; node != nullptr; node = node->next) {
            gchar* contents = nullptr;
            gsize length = 0;
            GError* error = nullptr;
            const gchar* filename = static_cast<const gchar*>(node->data);
            if (!g_file_get_contents(filename, &contents, &length, &error)) {
                browser->appendLog(error == nullptr ? "Unable to read selected file" : error->message);
                if (error != nullptr) g_error_free(error);
            } else {
                gchar* basename = g_path_get_basename(filename);
                std::string remotePath = browser->currentFolder;
                if (!remotePath.empty() && remotePath.back() != '/') remotePath += '/';
                remotePath += basename;
                if (rc_upload_file(browser->connection, remotePath.c_str(), contents, static_cast<int>(length))) changed = true;
                else browser->appendLog(rc_last_error(browser->connection));
                g_free(basename);
                g_free(contents);
            }
            g_free(node->data);
        }
        g_slist_free(filenames);
        if (changed) browser->queueMutationRefresh();
    }
    gtk_widget_destroy(dialog);
}

void TFileBrowserTree::onFileReceived(const char* path, const void* content, int length, void* data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    if (path == nullptr || length < 0 || (content == nullptr && length != 0)) return;
    const void* safeContent = content == nullptr ? static_cast<const void*>("") : content;
    const std::string receivedPath(path);
    for (auto iterator = browser->pendingNativeDragContents.begin(); iterator != browser->pendingNativeDragContents.end(); ++iterator) {
        if (!pathMatches(iterator->first, receivedPath)) continue;
        iterator->second.assign(static_cast<const guint8*>(safeContent), static_cast<const guint8*>(safeContent) + length);
        return;
    }
    bool previewResponse = false;
    for (auto iterator = browser->pendingPreviewDownloads.begin(); iterator != browser->pendingPreviewDownloads.end(); ++iterator) {
        if (!pathMatches(iterator->first, receivedPath)) continue;
        const bool wanted = iterator->second == browser->previewFolder && (!browser->modernFileBrowser || std::find(browser->visiblePreviewPaths.begin(), browser->visiblePreviewPaths.end(), iterator->first) != browser->visiblePreviewPaths.end());
        const std::string previewPath = iterator->first;
        browser->pendingPreviewDownloads.erase(iterator);
        previewResponse = true;
        if (wanted) browser->cachePreview(previewPath, safeContent, length);
        else browser->startNextPreviewDownload();
        break;
    }
    for (auto iterator = browser->pendingDragDownloads.begin(); iterator != browser->pendingDragDownloads.end(); ++iterator) {
        if (!pathMatches(iterator->first, receivedPath)) continue;
        GError* error = nullptr;
        if (g_file_set_contents(iterator->second.c_str(), static_cast<const gchar*>(safeContent), length, &error)) {
            browser->completedDragDownloads.push_back(iterator->second);
        } else {
            browser->appendLog(error == nullptr ? "Could not stage dragged file." : error->message);
        }
        if (error != nullptr) g_error_free(error);
        browser->pendingDragDownloads.erase(iterator);
        return;
    }
    const bool externalPathMatches = pathMatches(browser->pendingExternalPath, receivedPath);
    if (!browser->pendingExternalPath.empty() && externalPathMatches) {
        const std::string remotePath = browser->pendingExternalPath;
        browser->pendingExternalPath.clear();
        const std::string destinationFolder = browser->downloadDestinationDirectory();
        if (destinationFolder.empty()) { browser->appendLog("Specify a download folder in RC Options first."); return; }
        if (g_mkdir_with_parents(destinationFolder.c_str(), 0755) != 0) { browser->appendLog("Could not create the download folder."); return; }
        gchar* basename = g_path_get_basename(path);
        gchar* destination = g_build_filename(destinationFolder.c_str(), basename, nullptr);
        g_free(basename);
        if (!g_file_set_contents(destination, static_cast<const gchar*>(safeContent), length, nullptr)) { g_free(destination); browser->appendLog("Could not save downloaded file."); return; }
        gchar* absoluteDestination = g_canonicalize_filename(destination, nullptr);
        g_free(destination);
        const std::string localPath(absoluteDestination);
        g_free(absoluteDestination);
        for (const auto& entry : browser->remoteModifiedTimes) if (pathMatches(entry.first, receivedPath)) { applyModifiedTime(localPath, entry.second); break; }
        const std::string extension = localPath.substr(localPath.find_last_of('.') == std::string::npos ? localPath.size() : localPath.find_last_of('.'));
        if (g_ascii_strcasecmp(extension.c_str(), ".exe") == 0 || g_ascii_strcasecmp(extension.c_str(), ".bat") == 0 || g_ascii_strcasecmp(extension.c_str(), ".sh") == 0) { browser->showTextEditor(path, safeContent, length); return; }
#ifdef _WIN32
        gunichar2* widePath = g_utf8_to_utf16(localPath.c_str(), -1, nullptr, nullptr, nullptr);
        const HINSTANCE result = widePath == nullptr ? nullptr : ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(widePath), nullptr, nullptr, SW_SHOWNORMAL);
        g_free(widePath);
        if (reinterpret_cast<INT_PTR>(result) <= 32) browser->appendLog("Could not open downloaded file.");
#else
        GError* error = nullptr;
        gchar* uri = g_filename_to_uri(localPath.c_str(), nullptr, &error);
        if (uri == nullptr || !g_app_info_launch_default_for_uri(uri, nullptr, &error)) browser->appendLog(error == nullptr ? "Could not open downloaded file." : error->message);
        if (error != nullptr) g_error_free(error);
        g_free(uri);
#endif
        GStatBuf status{};
        if (g_stat(localPath.c_str(), &status) == 0) {
            browser->watchExternalPath = localPath;
            browser->watchExternalFolder = browser->currentFolder;
            browser->watchExternalRemotePath = remotePath;
            browser->watchExternalModified = static_cast<gint64>(status.st_mtime);
        }
        browser->appendLog((std::string("Downloaded file ") + localPath).c_str());
        return;
    }
    const bool editPathMatches = pathMatches(browser->pendingEditPath, receivedPath);
    if (!browser->pendingEditPath.empty() && editPathMatches) {
        browser->pendingEditPath.clear();
        browser->showTextEditor(path, safeContent, length);
        return;
    }
    bool userDownload = false;
    for (auto iterator = browser->pendingUserDownloads.begin(); iterator != browser->pendingUserDownloads.end(); ++iterator) {
        if (!pathMatches(iterator->first, receivedPath)) continue;
        if (--iterator->second == 0) browser->pendingUserDownloads.erase(iterator);
        userDownload = true;
        break;
    }
    if (!userDownload) return;
    const std::string destinationFolder = browser->downloadDestinationDirectory();
    if (destinationFolder.empty()) {
        browser->appendLog("Specify a download folder in RC Options first.");
        return;
    }
    if (g_mkdir_with_parents(destinationFolder.c_str(), 0755) != 0) {
        browser->appendLog("Could not create the download folder.");
        return;
    }
    gchar* basename = g_path_get_basename(path);
    gchar* destination = g_build_filename(destinationFolder.c_str(), basename, nullptr);
    g_free(basename);
    if (!g_file_set_contents(destination, static_cast<const gchar*>(safeContent), length, nullptr)) {
        g_free(destination);
        browser->appendLog("Could not save downloaded file.");
        return;
    }
    for (const auto& entry : browser->remoteModifiedTimes) if (pathMatches(entry.first, receivedPath)) { applyModifiedTime(destination, entry.second); break; }
    browser->appendLog((std::string("Downloaded file ") + destination).c_str());
    g_free(destination);
}

gboolean TFileBrowserTree::watchExternalFile(gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    if (browser->watchExternalPath.empty() || browser->watchExternalFolder != browser->currentFolder) return G_SOURCE_CONTINUE;
    GStatBuf status{};
    if (g_stat(browser->watchExternalPath.c_str(), &status) != 0) return G_SOURCE_CONTINUE;
    // The original client stopped watching files above 1 MiB; keep watching so externally edited large assets upload too.
    if (static_cast<gint64>(status.st_mtime) == browser->watchExternalModified) return G_SOURCE_CONTINUE;
    browser->watchExternalModified = static_cast<gint64>(status.st_mtime);
    if (!browser->watchExternalWritable) { browser->appendLog((std::string("No write rights for ") + browser->watchExternalRemotePath).c_str()); return G_SOURCE_CONTINUE; }
    gchar* contents = nullptr;
    gsize length = 0;
    GError* error = nullptr;
    if (!g_file_get_contents(browser->watchExternalPath.c_str(), &contents, &length, &error)) {
        browser->appendLog(error == nullptr ? "Could not read changed file." : error->message);
        if (error != nullptr) g_error_free(error);
        return G_SOURCE_CONTINUE;
    }
    if (!rc_upload_file(browser->connection, browser->watchExternalRemotePath.c_str(), contents, static_cast<int>(length))) browser->appendLog(rc_last_error(browser->connection));
    g_free(contents);
    return G_SOURCE_CONTINUE;
}

gboolean TFileBrowserTree::beginInlineRename(gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    browser->inlineRenameId = 0;
    GtkTreePath* path = gtk_tree_path_new_from_string(browser->pendingInlineRenamePath.c_str());
    browser->pendingInlineRenamePath.clear();
    GtkTreeIter row;
    if (path == nullptr || !gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->files), &row, path)) { if (path != nullptr) gtk_tree_path_free(path); return G_SOURCE_REMOVE; }
    g_object_set(browser->fileNameRenderer, "editable", TRUE, nullptr);
    gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(browser->fileView), path, gtk_tree_view_get_column(GTK_TREE_VIEW(browser->fileView), 0), browser->fileNameRenderer, true);
    gtk_tree_path_free(path);
    return G_SOURCE_REMOVE;
}

void TFileBrowserTree::showTextEditor(const char* path, const void* content, int length) {
    struct EditorState { void* connection; std::string path; GtkWidget* editor; };
    const std::string editorTitle = downloadServer.empty() ? std::string(path) : std::string(path) + " - " + downloadServer;
    GtkWidget* dialog = gtk_dialog_new_with_buttons(editorTitle.c_str(), nullptr, static_cast<GtkDialogFlags>(0), "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_NORMAL);
    gtk_window_set_resizable(GTK_WINDOW(dialog), true);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 520);
    GtkSourceLanguage* language = gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(), "graal");
    GtkSourceBuffer* sourceBuffer = language != nullptr ? gtk_source_buffer_new_with_language(language) : gtk_source_buffer_new(nullptr);
    applyRemoteControlSourceStyle(sourceBuffer);
    GtkWidget* editor = gtk_source_view_new_with_buffer(sourceBuffer);
    configureGScriptEditor(editor);
    setGScriptEditorConnection(editor, connection);
    addEditorFindButton(dialog, editor);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(editor), true);
    setGScriptEditorContent(GTK_TEXT_BUFFER(sourceBuffer), static_cast<const char*>(content), length);
    const std::string original = content == nullptr || length <= 0 ? std::string() : std::string(static_cast<const char*>(content), static_cast<std::size_t>(length));
    trackScriptEditor(dialog, GTK_TEXT_BUFFER(sourceBuffer), "File: " + std::string(path), original, connection);
    g_object_unref(sourceBuffer);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scrolled), editor);
    GtkWidget* contentArea = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(contentArea), 0);
    gtk_box_set_spacing(GTK_BOX(contentArea), 0);
    gtk_widget_set_margin_top(scrolled, 0);
    gtk_box_pack_start(GTK_BOX(contentArea), wrapGScriptEditor(editor, scrolled), true, true, 0);
    addGScriptEditorLineStatus(GTK_DIALOG(dialog), editor);
    auto* state = new EditorState{connection, path, editor};
    g_signal_connect(editor, "key-press-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventKey* event, gpointer responseDialog) {
        if (!consumeEditorCtrlS(widget, event)) return static_cast<gboolean>(FALSE);
        gtk_dialog_response(GTK_DIALOG(responseDialog), GTK_RESPONSE_ACCEPT);
        return static_cast<gboolean>(TRUE);
    }), dialog);
    g_signal_connect(editor, "key-release-event", G_CALLBACK(releaseEditorCtrlS), nullptr);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* responseDialog, gint response, gpointer userData) {
        auto* editorState = static_cast<EditorState*>(userData);
        if (response != GTK_RESPONSE_ACCEPT) { gtk_widget_destroy(GTK_WIDGET(responseDialog)); return; }
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editorState->editor));
        GtkTextIter start;
        GtkTextIter end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar* value = gtk_text_buffer_get_text(buffer, &start, &end, false);
        void* currentConnection = scriptEditorConnection(buffer);
        if (currentConnection != nullptr) rc_upload_file(currentConnection, editorState->path.c_str(), value, static_cast<int>(strlen(value)));
        g_free(value);
    }), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer userData) { delete static_cast<EditorState*>(userData); }), state);
    gtk_widget_show_all(dialog);
}

void TFileBrowserTree::refresh() {
    if (connection != nullptr && !rc_filebrowser_start(connection)) appendLog(rc_last_error(connection));
}

void TFileBrowserTree::setConnection(void* nextConnection) { connection = nextConnection; }

void TFileBrowserTree::rebuildModernItems() {
    if (modernItems == nullptr) return;
    clearModernBuild();
    gtk_list_store_clear(modernItems);
    modernItemIndices.clear();
    std::unordered_map<GdkPixbuf*, GdkPixbuf*> scaledIcons;
    std::string prefix = currentFolder;
    if (!prefix.empty() && prefix.back() != '/') prefix += '/';
    for (const std::string& storedPath : folderPaths) {
        std::string path = storedPath;
        while (!path.empty() && path.back() == '/') path.pop_back();
        if (!prefix.empty() && !path.starts_with(prefix)) continue;
        const std::string remainder = prefix.empty() ? path : path.substr(prefix.size());
        if (remainder.empty() || remainder.find('/') != std::string::npos) continue;
        if (closedFolderLargeIcon != nullptr) g_object_ref(closedFolderLargeIcon);
        pendingModernItems.push_back({closedFolderLargeIcon, remainder, path + "/", "", true});
    }
    GtkTreeIter source;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(files), &source);
    while (valid) {
        GdkPixbuf* icon = nullptr;
        gchar* path = nullptr;
        gchar* rights = nullptr;
        gtk_tree_model_get(GTK_TREE_MODEL(files), &source, FileIconColumn, &icon, FilePathColumn, &path, FileRightsColumn, &rights, -1);
        const std::string itemPath = path == nullptr ? "" : path;
        const auto cached = previewCache.find(itemPath);
        GdkPixbuf* displayed = nullptr;
        if (modernThumbnails && cached != previewCache.end() && canAutoPreview(itemPath)) {
            const int width = gdk_pixbuf_get_width(cached->second);
            const int height = gdk_pixbuf_get_height(cached->second);
            const double scale = std::min(88.0 / std::max(1, width), 88.0 / std::max(1, height));
            displayed = gdk_pixbuf_scale_simple(cached->second, std::max(1, static_cast<int>(width * scale)), std::max(1, static_cast<int>(height * scale)), GDK_INTERP_BILINEAR);
        } else {
            if (scaledIcons.find(icon) == scaledIcons.end()) scaledIcons[icon] = scaledIcon(icon, 44, 44);
            displayed = scaledIcons[icon];
            if (displayed != nullptr) g_object_ref(displayed);
        }
        pendingModernItems.push_back({displayed, itemPath, itemPath, rights == nullptr ? "" : rights, false});
        if (icon != nullptr) g_object_unref(icon);
        g_free(path);
        g_free(rights);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(files), &source);
    }
    for (const auto& icon : scaledIcons) if (icon.second != nullptr) g_object_unref(icon.second);
    modernBuildIndex = 0;
    if (pendingModernItems.empty()) queueVisibleThumbnails();
    else modernBuildId = g_idle_add_full(G_PRIORITY_LOW, appendModernItems, this, nullptr);
}

void TFileBrowserTree::clearModernBuild() {
    if (modernBuildId != 0) {
        g_source_remove(modernBuildId);
        modernBuildId = 0;
    }
    for (ModernPendingItem& item : pendingModernItems) {
        if (item.icon != nullptr) g_object_unref(item.icon);
    }
    pendingModernItems.clear();
    modernBuildIndex = 0;
}

gboolean TFileBrowserTree::appendModernItems(gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    constexpr std::size_t batchSize = 64;
    const std::size_t end = std::min(browser->modernBuildIndex + batchSize, browser->pendingModernItems.size());
    while (browser->modernBuildIndex < end) {
        ModernPendingItem& pending = browser->pendingModernItems[browser->modernBuildIndex];
        GtkTreeIter item;
        gtk_list_store_append(browser->modernItems, &item);
        gtk_list_store_set(browser->modernItems, &item, ModernIconColumn, pending.icon, ModernNameColumn, pending.name.c_str(), ModernPathColumn, pending.path.c_str(), ModernFolderColumn, pending.folder, ModernRightsColumn, pending.rights.c_str(), -1);
        browser->modernItemIndices[pending.path] = static_cast<int>(browser->modernBuildIndex);
        if (pending.icon != nullptr) {
            g_object_unref(pending.icon);
            pending.icon = nullptr;
        }
        ++browser->modernBuildIndex;
    }
    if (browser->modernBuildIndex < browser->pendingModernItems.size()) return G_SOURCE_CONTINUE;
    browser->modernBuildId = 0;
    browser->pendingModernItems.clear();
    browser->modernBuildIndex = 0;
    browser->queueVisibleThumbnails();
    return G_SOURCE_REMOVE;
}

void TFileBrowserTree::queueVisibleThumbnails() {
    if (!modernFileBrowser || (!modernThumbnails && !hoverPreviews)) return;
    if (modernBuildId != 0) return;
    queuedPreviewDownloads.clear();
    visiblePreviewPaths.clear();
    if (thumbnailLoadId != 0) g_source_remove(thumbnailLoadId);
    thumbnailLoadId = g_timeout_add(300, loadVisibleThumbnails, this);
}

void TFileBrowserTree::startNextPreviewDownload() {
    if (connection == nullptr || !pendingPreviewDownloads.empty()) return;
    while (!queuedPreviewDownloads.empty()) {
        const std::string path = queuedPreviewDownloads.front();
        queuedPreviewDownloads.erase(queuedPreviewDownloads.begin());
        if (!canAutoPreview(path)) continue;
        if (previewCache.find(path) != previewCache.end()) continue;
        pendingPreviewDownloads[path] = previewFolder;
        if (std::find(previewTransferPaths.begin(), previewTransferPaths.end(), path) == previewTransferPaths.end()) previewTransferPaths.push_back(path);
        if (previewTransferPaths.size() > 100) previewTransferPaths.erase(previewTransferPaths.begin());
        if (rc_filebrowser_download(connection, path.c_str())) return;
        pendingPreviewDownloads.erase(path);
    }
}

gboolean TFileBrowserTree::loadVisibleThumbnails(gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    browser->thumbnailLoadId = 0;
    if (!browser->modernFileBrowser || (!browser->modernThumbnails && !browser->hoverPreviews) || browser->connection == nullptr) return G_SOURCE_REMOVE;
    GtkTreePath* start = nullptr;
    GtkTreePath* end = nullptr;
    if (!gtk_icon_view_get_visible_range(GTK_ICON_VIEW(browser->modernView), &start, &end)) return G_SOURCE_REMOVE;
    const gint* startIndices = gtk_tree_path_get_indices(start);
    const gint* endIndices = gtk_tree_path_get_indices(end);
    if (startIndices == nullptr || endIndices == nullptr) { gtk_tree_path_free(start); gtk_tree_path_free(end); return G_SOURCE_REMOVE; }
    const int itemCount = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(browser->modernItems), nullptr);
    const int first = std::max(0, startIndices[0]);
    const int last = std::min(itemCount - 1, endIndices[0]);
    gtk_tree_path_free(start);
    gtk_tree_path_free(end);
    constexpr std::size_t maximumRequests = 12;
    for (int index = first; index <= last; ++index) {
        GtkTreePath* item = gtk_tree_path_new_from_indices(index, -1);
        GtkTreeIter row;
        gchar* path = nullptr;
        gboolean folder = false;
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->modernItems), &row, item)) gtk_tree_model_get(GTK_TREE_MODEL(browser->modernItems), &row, ModernPathColumn, &path, ModernFolderColumn, &folder, -1);
        const std::string itemPath = path == nullptr ? "" : path;
        if (!folder && browser->canAutoPreview(itemPath)) {
            browser->visiblePreviewPaths.push_back(itemPath);
            if (browser->queuedPreviewDownloads.size() < maximumRequests && browser->previewCache.find(itemPath) == browser->previewCache.end() && browser->pendingPreviewDownloads.find(itemPath) == browser->pendingPreviewDownloads.end()) browser->queuedPreviewDownloads.push_back(itemPath);
        }
        g_free(path);
        gtk_tree_path_free(item);
    }
    browser->startNextPreviewDownload();
    return G_SOURCE_REMOVE;
}

void TFileBrowserTree::updateModernThumbnail(const std::string& path, GdkPixbuf* pixbuf) {
    if (!modernThumbnails || modernItems == nullptr || pixbuf == nullptr) return;
    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const double scale = std::min(88.0 / std::max(1, width), 88.0 / std::max(1, height));
    GdkPixbuf* thumbnail = gdk_pixbuf_scale_simple(pixbuf, std::max(1, static_cast<int>(width * scale)), std::max(1, static_cast<int>(height * scale)), GDK_INTERP_BILINEAR);
    GtkTreeIter row;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(modernItems), &row);
    while (valid) {
        gchar* itemPath = nullptr;
        gtk_tree_model_get(GTK_TREE_MODEL(modernItems), &row, ModernPathColumn, &itemPath, -1);
        const bool matches = itemPath != nullptr && path == itemPath;
        g_free(itemPath);
        if (matches) { gtk_list_store_set(modernItems, &row, ModernIconColumn, thumbnail, -1); break; }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(modernItems), &row);
    }
    if (thumbnail != nullptr) g_object_unref(thumbnail);
}

void TFileBrowserTree::refreshFolders() {
    RCFileBrowserFolder* entries = nullptr;
    const int count = rc_copy_filebrowser_folders(connection, &entries);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(folders), GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
    gtk_tree_store_clear(folders);
    folderPaths.clear();
    for (int index = 0; index < count; ++index) {
        const char* pattern = entries[index].pattern == nullptr ? "" : entries[index].pattern;
        addFolder(pattern, entries[index].rights == nullptr ? "" : entries[index].rights);
        std::string path(pattern);
        const std::size_t wildcard = path.find('*');
        if (wildcard != std::string::npos) path.resize(wildcard);
        while (!path.empty() && path.back() == '/') path.pop_back();
        if (!path.empty() && std::find(folderPaths.begin(), folderPaths.end(), path) == folderPaths.end()) folderPaths.push_back(path);
    }
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(folders), FolderDisplayColumn, GTK_SORT_ASCENDING);
    rc_free_filebrowser_folders(entries, count);
    updateFolderIcons();
    if (modernFileBrowser) rebuildModernItems();
}

void TFileBrowserTree::refreshFiles(const char* folder, int count) {
    const std::string responseFolder = folder == nullptr ? "" : folder;
    if (!currentFolder.empty() && responseFolder != currentFolder) return;
    if (previewFolder != responseFolder) { clearPreviewCache(); queuedPreviewDownloads.clear(); }
    RCFileBrowserEntry* entries = nullptr;
    const int entryCount = count > 0 ? rc_copy_filebrowser_files(connection, &entries) : 0;
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(files), GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
    gtk_list_store_clear(files);
    remoteModifiedTimes.clear();
    previewFileSizes.clear();
    currentFolder = responseFolder;
    previewFolder = responseFolder;
    gtk_label_set_text(GTK_LABEL(folderPath), (std::string("Current Folder: ") + responseFolder).c_str());
    gtk_entry_set_text(GTK_ENTRY(addressEntry), responseFolder.c_str());
    updateFolderIcons();
    for (int index = 0; index < entryCount; ++index) {
        GtkTreeIter row;
        gtk_list_store_append(files, &row);
        const std::string modified = formatModified(entries[index].modified);
        const std::string size = entries[index].size == 0 ? "" : std::to_string(entries[index].size);
        gtk_list_store_set(files, &row, FileIconColumn, fileIcon(entries[index], textFileIcon, nwFileIcon, scriptFileIcon, gmapFileIcon, binaryFileIcon, fontFileIcon, archiveFileIcon, configFileIcon, unknownFileIcon), FilePathColumn, entries[index].path == nullptr ? "" : entries[index].path, FileRightsColumn, entries[index].rights == nullptr ? "" : entries[index].rights, FileSizeColumn, size.c_str(), FileModifiedColumn, modified.c_str(), FileSizeSortColumn, entries[index].size, FileModifiedSortColumn, entries[index].modified, -1);
        const std::string path = entries[index].path == nullptr ? "" : entries[index].path;
        previewFileSizes[path] = entries[index].size;
        remoteModifiedTimes[path] = entries[index].modified;
        remoteModifiedTimes[responseFolder + path] = entries[index].modified;
        if (!modernFileBrowser && hoverPreviews && canAutoPreview(path) && previewCache.size() + pendingPreviewDownloads.size() + queuedPreviewDownloads.size() < 50 && previewCache.find(path) == previewCache.end() && pendingPreviewDownloads.find(path) == pendingPreviewDownloads.end() && std::find(queuedPreviewDownloads.begin(), queuedPreviewDownloads.end(), path) == queuedPreviewDownloads.end()) queuedPreviewDownloads.push_back(path);
    }
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(files), FilePathColumn, GTK_SORT_ASCENDING);
    rc_free_filebrowser_files(entries, entryCount);
    if (!modernFileBrowser) startNextPreviewDownload();
    updateFileStatus();
    if (modernFileBrowser) rebuildModernItems();
}

void TFileBrowserTree::addFolder(const char* pattern, const char* rights) {
    if (pattern == nullptr || *pattern == '\0') return;
    std::string folderPath(pattern);
    const std::size_t wildcard = folderPath.find('*');
    if (wildcard != std::string::npos) folderPath.resize(wildcard);
    while (!folderPath.empty() && folderPath.back() == '/') folderPath.pop_back();
    if (folderPath.empty()) return;
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < folderPath.size()) {
        const std::size_t end = folderPath.find('/', start);
        const std::string part = folderPath.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) parts.push_back(part);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    GtkTreeIter parent;
    GtkTreeIter* parentPointer = nullptr;
    std::string fullPath;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        fullPath += parts[index];
        GtkTreeIter row;
        gboolean found = gtk_tree_model_iter_children(GTK_TREE_MODEL(folders), &row, parentPointer);
        while (found) {
            gchar* existing = nullptr;
            gtk_tree_model_get(GTK_TREE_MODEL(folders), &row, FolderPathColumn, &existing, -1);
            const bool matches = existing != nullptr && fullPath == existing;
            g_free(existing);
            if (matches) break;
            found = gtk_tree_model_iter_next(GTK_TREE_MODEL(folders), &row);
        }
        if (!found) {
            gtk_tree_store_append(folders, &row, parentPointer);
            const std::string label = parts[index] + "/";
            gtk_tree_store_set(folders, &row, FolderIconColumn, closedFolderIcon, FolderPathColumn, fullPath.c_str(), FolderRightsColumn, index + 1 == parts.size() && rights != nullptr ? rights : "", FolderDisplayColumn, label.c_str(), -1);
        }
        parent = row;
        parentPointer = &parent;
        fullPath += "/";
    }
}

void TFileBrowserTree::appendLog(const char* message) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, (std::string(message == nullptr ? "" : message) + "\n").c_str(), -1);
    while (gtk_text_buffer_get_line_count(buffer) > 5000 || gtk_text_buffer_get_char_count(buffer) > 2 * 1024 * 1024) {
        const gint lineCount = gtk_text_buffer_get_line_count(buffer);
        const gint characterCount = gtk_text_buffer_get_char_count(buffer);
        GtkTextIter start;
        gtk_text_buffer_get_start_iter(buffer, &start);
        if (lineCount > 5000) gtk_text_buffer_get_iter_at_line(buffer, &end, lineCount - 5000);
        else gtk_text_buffer_get_iter_at_offset(buffer, &end, characterCount - 2 * 1024 * 1024);
        gtk_text_buffer_delete(buffer, &start, &end);
    }
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(log), &end, 0.0, false, 0.0, 1.0);
}
