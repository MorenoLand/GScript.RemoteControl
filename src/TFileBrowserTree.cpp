#include "TFileBrowserTree.h"
#include "TEditorFind.h"
#include "TGScriptEditor.h"
#include "TTheme.h"

#include <grclib.h>
#include <gtksourceview/gtksource.h>
#include <glib/gstdio.h>

#ifdef _WIN32
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
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
#include <vector>

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
    struct FileMenuItem { TFileBrowserTree* browser; std::vector<std::string> paths; };
    void destroyFileMenuItem(gpointer data, GClosure*) { delete static_cast<FileMenuItem*>(data); }

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

    GdkPixbuf* loadImage(const char* name) { return gdk_pixbuf_new_from_file((std::string("images/") + name).c_str(), nullptr); }

    bool pathMatches(const std::string& expected, const std::string& received) {
        return expected == received || received.ends_with("/" + expected) || expected.ends_with("/" + received);
    }

    bool isPreviewImage(const std::string& path) {
        const std::size_t dot = path.find_last_of('.');
        if (dot == std::string::npos) return false;
        gchar* lower = g_ascii_strdown(path.c_str() + dot, -1);
        const std::string extension = lower == nullptr ? "" : lower;
        g_free(lower);
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".gif" || extension == ".webp" || extension == ".bmp" || extension == ".ico";
    }

#ifdef _WIN32
    struct NativeDropFiles { DWORD pFiles; POINT pt; BOOL fNC; BOOL fWide; };

    class NativeDropSource final : public IDropSource {
    public:
        explicit NativeDropSource(const std::shared_ptr<bool>& accepted) : dropAccepted(accepted) {}
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
        HRESULT __stdcall GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
    private:
        ULONG references = 1;
        std::shared_ptr<bool> dropAccepted;
    };

    class NativeFileDataObject final : public IDataObject {
    public:
        using ContentProvider = std::function<bool(size_t, std::vector<guint8>&)>;
        NativeFileDataObject(const std::vector<std::wstring>& values, ContentProvider provider) : names(values), contentProvider(std::move(provider)) {}
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
                    file.dwFlags = FD_ATTRIBUTES | FD_PROGRESSUI;
                    file.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
                    std::wcsncpy(file.cFileName, names[index].c_str(), ARRAYSIZE(file.cFileName) - 1);
                    file.cFileName[ARRAYSIZE(file.cFileName) - 1] = L'\0';
                }
                GlobalUnlock(global);
                medium->tymed = TYMED_HGLOBAL;
                medium->hGlobal = global;
                return S_OK;
            }
            if (format->cfFormat == contentsFormat() && (format->tymed & TYMED_ISTREAM) != 0) {
                if (format->lindex < 0 || static_cast<size_t>(format->lindex) >= names.size() || !contentProvider) return DV_E_LINDEX;
                std::vector<guint8> content;
                if (!contentProvider(static_cast<size_t>(format->lindex), content)) return E_FAIL;
                HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, content.size() == 0 ? 1 : content.size());
                if (global == nullptr) return E_OUTOFMEMORY;
                void* buffer = GlobalLock(global);
                if (buffer == nullptr) { GlobalFree(global); return E_OUTOFMEMORY; }
                if (!content.empty()) std::memcpy(buffer, content.data(), content.size());
                GlobalUnlock(global);
                IStream* stream = nullptr;
                HRESULT result = CreateStreamOnHGlobal(global, TRUE, &stream);
                if (FAILED(result)) { GlobalFree(global); return result; }
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
            if (format->cfFormat == contentsFormat() && (format->tymed & TYMED_ISTREAM) != 0 && format->lindex >= 0 && static_cast<size_t>(format->lindex) < names.size()) return S_OK;
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
                {static_cast<CLIPFORMAT>(contentsFormat()), nullptr, DVASPECT_CONTENT, -1, TYMED_ISTREAM}
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
        ContentProvider contentProvider;
    };

    void nativeFileDrag(const std::vector<std::wstring>& names, NativeFileDataObject::ContentProvider contentProvider, const std::shared_ptr<bool>& dropAccepted) {
        if (names.empty()) return;
        if (FAILED(OleInitialize(nullptr))) return;
        IDataObject* data = new NativeFileDataObject(names, std::move(contentProvider));
        IDropSource* source = new NativeDropSource(dropAccepted);
        DWORD effect = DROPEFFECT_NONE;
        DoDragDrop(data, source, DROPEFFECT_COPY, &effect);
        data->Release();
        source->Release();
        OleUninitialize();
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
    if (message == nullptr || pendingPreviewDownloads.empty()) return false;
    const std::string text(message);
    const std::size_t separator = text.find(" for ");
    const std::size_t colon = text.find(": ");
    const std::string path = separator == std::string::npos ? colon == std::string::npos ? std::string() : text.substr(colon + 2) : text.substr(separator + 5);
    if (path.empty()) return false;
    return std::any_of(pendingPreviewDownloads.begin(), pendingPreviewDownloads.end(), [&](const auto& pending) { return pathMatches(pending.first, path); });
}

TFileBrowserTree::TFileBrowserTree() {
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
    folderPath = gtk_label_new("Current Folder:");
    gtk_misc_set_alignment(GTK_MISC(folderPath), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(content), folderPath, false, false, 5);
    GtkWidget* panes = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_position(GTK_PANED(panes), 366);
    gtk_box_pack_start(GTK_BOX(content), panes, true, true, 0);
    GtkWidget* filePanes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(filePanes), 200);
    gtk_paned_pack1(GTK_PANED(panes), filePanes, false, true);
    GtkWidget* folderScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkWidget* fileScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(folderScrolled), GTK_SHADOW_IN);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(fileScrolled), GTK_SHADOW_IN);
    folders = gtk_tree_store_new(4, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(folders), FolderDisplayColumn, GTK_SORT_ASCENDING);
    files = gtk_list_store_new(7, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
    folderView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(folders));
    fileView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(files));
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(fileView)), GTK_SELECTION_MULTIPLE);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(folderView), false);
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
    gtk_paned_pack1(GTK_PANED(filePanes), folderScrolled, false, true);
    gtk_paned_pack2(GTK_PANED(filePanes), fileScrolled, true, true);
    GtkWidget* logScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(logScrolled), GTK_SHADOW_IN);
    log = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log), false);
    gtk_container_add(GTK_CONTAINER(logScrolled), log);
    gtk_paned_pack2(GTK_PANED(panes), logScrolled, true, true);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    gtk_container_set_border_width(GTK_CONTAINER(buttons), 5);
    GtkWidget* closeButton = gtk_button_new_with_label("Close");
    gtk_container_add(GTK_CONTAINER(buttons), closeButton);
    gtk_box_pack_start(GTK_BOX(root), buttons, false, true, 0);
    closedFolderIcon = loadImage("rcfiles_folderclosed.png");
    openFolderIcon = loadImage("rcfiles_folderopen.png");
    textFileIcon = loadImage("rcfiles_text2.png");
    nwFileIcon = loadImage("rcfiles_nw.png");
    scriptFileIcon = loadImage("rcfiles_graal.png");
    gmapFileIcon = loadImage("rcfiles_gmap.png");
    binaryFileIcon = loadImage("rcfiles_binary.png");
    fontFileIcon = loadImage("rcfiles_ttf.png");
    archiveFileIcon = loadImage("rcfiles_archive.png");
    configFileIcon = loadImage("rcfiles_conf.png");
    unknownFileIcon = loadImage("rcfiles_unknown.png");
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(folderView)), "changed", G_CALLBACK(onFolderSelected), this);
    g_signal_connect(folderView, "button-press-event", G_CALLBACK(onFolderButtonPress), this);
    g_signal_connect(fileView, "button-press-event", G_CALLBACK(onFileButtonPress), this);
    GtkTargetEntry dropTargets[] = {{const_cast<gchar*>("application/x-remote-control-file-path"), GTK_TARGET_SAME_APP, 1}, {const_cast<gchar*>("text/uri-list"), 0, 2}};
#ifndef _WIN32
    GtkTargetEntry fileTarget[] = {{const_cast<gchar*>("application/x-remote-control-file-path"), GTK_TARGET_SAME_APP, 1}, {const_cast<gchar*>("text/uri-list"), 0, 2}};
    gtk_drag_source_set(fileView, GDK_BUTTON1_MASK, fileTarget, G_N_ELEMENTS(fileTarget), static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
#else
    gtk_widget_add_events(fileView, GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(fileView, "button-release-event", G_CALLBACK(onFileButtonRelease), this);
#endif
    gtk_widget_add_events(fileView, static_cast<GdkEventMask>(GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK));
    g_signal_connect(fileView, "motion-notify-event", G_CALLBACK(onFileMotion), this);
    g_signal_connect(fileView, "leave-notify-event", G_CALLBACK(onFileLeave), this);
    gtk_drag_dest_set(folderView, static_cast<GtkDestDefaults>(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP), dropTargets, G_N_ELEMENTS(dropTargets), static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    gtk_drag_dest_set(fileView, static_cast<GtkDestDefaults>(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP), dropTargets, G_N_ELEMENTS(dropTargets), static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    g_signal_connect(fileView, "drag-begin", G_CALLBACK(onFileDragBegin), this);
    g_signal_connect(fileView, "drag-end", G_CALLBACK(onFileDragEnd), this);
    g_signal_connect(fileView, "drag-data-get", G_CALLBACK(onFileDragDataGet), this);
    g_signal_connect(folderView, "drag-data-received", G_CALLBACK(onDropDataReceived), this);
    g_signal_connect(fileView, "drag-data-received", G_CALLBACK(onDropDataReceived), this);
    g_signal_connect(closeButton, "clicked", G_CALLBACK(onRefresh), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
    previewWindow = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_transient_for(GTK_WINDOW(previewWindow), GTK_WINDOW(window));
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
}

TFileBrowserTree::~TFileBrowserTree() {
    if (externalWatchId != 0) g_source_remove(externalWatchId);
    if (inlineRenameId != 0) g_source_remove(inlineRenameId);
    clearPreviewCache();
    for (const std::string& path : pendingDragLocalPaths) g_remove(path.c_str());
    for (const std::string& path : completedDragDownloads) g_remove(path.c_str());
    if (!dragStagingFolder.empty()) g_rmdir(dragStagingFolder.c_str());
    if (closedFolderIcon != nullptr) g_object_unref(closedFolderIcon);
    if (openFolderIcon != nullptr) g_object_unref(openFolderIcon);
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
    if (window != nullptr) gtk_widget_destroy(window);
    if (folders != nullptr) g_object_unref(folders);
    if (files != nullptr) g_object_unref(files);
}

void TFileBrowserTree::open(void* nextConnection) {
    connection = nextConnection;
    rc_on_filebrowser_folders(connection, onFolders, this);
    rc_on_filebrowser_files(connection, onFiles, this);
    rc_on_filebrowser_message(connection, onMessage, this);
    rc_on_file_received(connection, onFileReceived, this);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    refresh();
}

void TFileBrowserTree::openFolder(void* nextConnection, const std::string& folder) {
    open(nextConnection);
    currentFolder = folder;
    if (!currentFolder.empty() && currentFolder.back() != '/') currentFolder += '/';
    gtk_list_store_clear(files);
    hidePreview();
    gtk_label_set_text(GTK_LABEL(folderPath), (std::string("Current Folder: ") + currentFolder).c_str());
    if (!rc_filebrowser_cd(connection, currentFolder.c_str())) appendLog(rc_last_error(connection));
}

void TFileBrowserTree::setDownloadFolder(const std::string& folder) { downloadFolder = folder; }
void TFileBrowserTree::setDownloadServer(const std::string& server) { downloadServer = server; }
void TFileBrowserTree::setServerName(const std::string& server) { gtk_window_set_title(GTK_WINDOW(window), server.empty() ? "File Browser" : ("File Browser - " + server).c_str()); }
std::string TFileBrowserTree::downloadDestinationDirectory() const {
    if (downloadFolder.empty()) return {};
    std::string destination = downloadFolder;
    destination = (std::filesystem::path(destination) / safeDownloadComponent(downloadServer, "Server")).string();
    for (const std::string& component : remotePathComponents(currentFolder)) destination = (std::filesystem::path(destination) / component).string();
    return destination;
}

void TFileBrowserTree::onRefresh(GtkButton*, gpointer data) { TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data); browser->hidePreview(); gtk_widget_hide(browser->window); }
void TFileBrowserTree::onFolders(int, void* data) { static_cast<TFileBrowserTree*>(data)->refreshFolders(); }
void TFileBrowserTree::onFiles(const char* folder, int count, void* data) { static_cast<TFileBrowserTree*>(data)->refreshFiles(folder, count); }
void TFileBrowserTree::onMessage(const char* message, void* data) { TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data); if (browser->isPreviewTransferMessage(message)) return; browser->appendLog(message == nullptr ? "" : message); }
gboolean TFileBrowserTree::onDelete(GtkWidget*, GdkEvent*, gpointer data) { TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data); browser->hidePreview(); gtk_widget_hide(browser->window); return true; }

void TFileBrowserTree::onFolderSelected(GtkTreeSelection* selection, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(selection, nullptr, &row)) return;
    gchar* folder = nullptr;
    gtk_tree_model_get(GTK_TREE_MODEL(browser->folders), &row, FolderPathColumn, &folder, -1);
    if (folder != nullptr) {
        std::string folderPath(folder);
        if (!folderPath.empty() && folderPath.back() != '/') folderPath += '/';
        browser->currentFolder = folderPath;
        gtk_list_store_clear(browser->files);
        browser->hidePreview();
        gtk_label_set_text(GTK_LABEL(browser->folderPath), (std::string("Current Folder: ") + folderPath).c_str());
        if (!rc_filebrowser_cd(browser->connection, folderPath.c_str())) browser->appendLog(rc_last_error(browser->connection));
        g_free(folder);
    }
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

#ifdef _WIN32
gboolean TFileBrowserTree::onFileButtonRelease(GtkWidget*, GdkEventButton* event, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    if (browser->nativeDragButton == event->button) browser->nativeDragButton = 0;
    return false;
}
#endif

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

gboolean TFileBrowserTree::onFileLeave(GtkWidget*, GdkEventCrossing*, gpointer data) {
    static_cast<TFileBrowserTree*>(data)->hidePreview();
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

void TFileBrowserTree::cachePreview(const std::string& path, const void* content, int length) {
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    g_signal_connect(loader, "size-prepared", G_CALLBACK(+[](GdkPixbufLoader* value, int width, int height, gpointer) {
        const double scale = std::min(1.0, std::min(360.0 / std::max(1, width), 260.0 / std::max(1, height)));
        gdk_pixbuf_loader_set_size(value, std::max(1, static_cast<int>(width * scale)), std::max(1, static_cast<int>(height * scale)));
    }), nullptr);
    GError* error = nullptr;
    const bool loaded = gdk_pixbuf_loader_write(loader, static_cast<const guchar*>(content), length, &error) && gdk_pixbuf_loader_close(loader, &error);
    GdkPixbuf* pixbuf = loaded ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr;
    if (pixbuf != nullptr) {
        g_object_ref(pixbuf);
        const auto existing = previewCache.find(path);
        if (existing != previewCache.end()) g_object_unref(existing->second);
        else previewCacheOrder.push_back(path);
        previewCache[path] = pixbuf;
        while (previewCacheOrder.size() > 50) { const std::string oldest = previewCacheOrder.front(); previewCacheOrder.erase(previewCacheOrder.begin()); g_object_unref(previewCache[oldest]); previewCache.erase(oldest); }
        if (hoveredPreviewPath == path) showPreview(path, previewRootX, previewRootY);
    }
    if (error != nullptr) g_error_free(error);
    g_object_unref(loader);
}

#ifdef _WIN32
void TFileBrowserTree::startNativeDrag(GtkWidget* widget) {
    for (const std::string& path : pendingDragLocalPaths) g_remove(path.c_str());
    for (const std::string& path : completedDragDownloads) g_remove(path.c_str());
    if (!dragStagingFolder.empty()) { g_rmdir(dragStagingFolder.c_str()); dragStagingFolder.clear(); }
    pendingDragDownloads.clear();
    completedDragDownloads.clear();
    pendingDragLocalPaths.clear();
    pendingExternalPath.clear();
    GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
    GList* rows = nullptr;
    for (const std::string& pathText : pendingDragSelectionPaths) {
        GtkTreePath* path = gtk_tree_path_new_from_string(pathText.c_str());
        if (path != nullptr) rows = g_list_append(rows, path);
    }
    pendingDragSelectionPaths.clear();
    if (rows == nullptr) rows = gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)), nullptr);
    if (rows == nullptr) return;
    gchar* temp = g_get_tmp_dir() == nullptr ? nullptr : g_build_filename(g_get_tmp_dir(), "RemoteControl-filebrowser-drag", nullptr);
    dragStagingFolder = temp == nullptr ? "" : temp;
    g_free(temp);
    if (!dragStagingFolder.empty()) g_mkdir_with_parents(dragStagingFolder.c_str(), 0755);
    std::vector<std::string> remotePaths;
    std::vector<std::wstring> names;
    for (GList* node = rows; node != nullptr; node = node->next) {
        GtkTreeIter row;
        gchar* remotePath = nullptr;
        if (!gtk_tree_model_get_iter(model, &row, static_cast<GtkTreePath*>(node->data))) continue;
        gtk_tree_model_get(model, &row, FilePathColumn, &remotePath, -1);
        if (remotePath == nullptr || *remotePath == '\0' || dragStagingFolder.empty()) { g_free(remotePath); continue; }
        gchar* basename = g_path_get_basename(remotePath);
        const std::string fileName = basename == nullptr ? "download" : basename;
        std::string localPath = dragStagingFolder + G_DIR_SEPARATOR_S + fileName;
        g_free(basename);
        int suffix = 2;
        while (g_file_test(localPath.c_str(), G_FILE_TEST_EXISTS)) localPath = dragStagingFolder + G_DIR_SEPARATOR_S + std::to_string(suffix++) + "-" + fileName;
        gunichar2* wideName = g_utf8_to_utf16(fileName.c_str(), -1, nullptr, nullptr, nullptr);
        if (wideName != nullptr) names.emplace_back(reinterpret_cast<wchar_t*>(wideName));
        g_free(wideName);
        remotePaths.emplace_back(remotePath);
        pendingDragLocalPaths.push_back(localPath);
        g_free(remotePath);
    }
    const auto dropAccepted = std::make_shared<bool>(false);
    nativeFileDrag(names, [this, remotePaths, dropAccepted](size_t index, std::vector<guint8>& content) {
        if (!*dropAccepted) return false;
        if (index >= remotePaths.size() || index >= pendingDragLocalPaths.size()) return false;
        const std::string& remotePath = remotePaths[index];
        const std::string& localPath = pendingDragLocalPaths[index];
        if (!g_file_test(localPath.c_str(), G_FILE_TEST_EXISTS)) {
            pendingDragDownloads[remotePath] = localPath;
            if (!rc_filebrowser_download(connection, remotePath.c_str())) { pendingDragDownloads.erase(remotePath); return false; }
            const gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
            while (pendingDragDownloads.contains(remotePath) && g_get_monotonic_time() < deadline) {
                while (gtk_events_pending()) gtk_main_iteration();
                g_usleep(10000);
            }
        }
        if (!g_file_test(localPath.c_str(), G_FILE_TEST_EXISTS)) return false;
        gchar* bytes = nullptr;
        gsize length = 0;
        GError* error = nullptr;
        if (!g_file_get_contents(localPath.c_str(), &bytes, &length, &error)) { if (error != nullptr) g_error_free(error); return false; }
        content.assign(reinterpret_cast<guint8*>(bytes), reinterpret_cast<guint8*>(bytes) + length);
        g_free(bytes);
        return true;
    }, dropAccepted);
    for (const std::string& path : pendingDragLocalPaths) g_remove(path.c_str());
    if (!dragStagingFolder.empty()) g_rmdir(dragStagingFolder.c_str());
    pendingDragDownloads.clear();
    completedDragDownloads.clear();
    pendingDragLocalPaths.clear();
    dragStagingFolder.clear();
    for (GList* node = rows; node != nullptr; node = node->next) gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
    g_list_free(rows);
}
#endif

void TFileBrowserTree::onFileDragBegin(GtkWidget* widget, GdkDragContext* context, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    if (!browser->pendingDragSelectionPaths.empty()) {
        GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
        gtk_tree_selection_unselect_all(selection);
        for (const std::string& pathText : browser->pendingDragSelectionPaths) {
            GtkTreePath* path = gtk_tree_path_new_from_string(pathText.c_str());
            if (path != nullptr) { gtk_tree_selection_select_path(selection, path); gtk_tree_path_free(path); }
        }
        browser->pendingDragSelectionPaths.clear();
    }
    for (const std::string& path : browser->pendingDragLocalPaths) g_remove(path.c_str());
    for (const std::string& path : browser->completedDragDownloads) g_remove(path.c_str());
    if (!browser->dragStagingFolder.empty()) { g_rmdir(browser->dragStagingFolder.c_str()); browser->dragStagingFolder.clear(); }
    browser->pendingDragDownloads.clear();
    browser->completedDragDownloads.clear();
    browser->pendingDragLocalPaths.clear();
    browser->pendingExternalPath.clear();
    GtkTreeSelection* selected = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
    GtkTreeModel* model = nullptr;
    GList* rows = gtk_tree_selection_get_selected_rows(selected, &model);
    if (rows == nullptr) return;
    gchar* staging = g_canonicalize_filename("cache/filebrowser-drag", nullptr);
    browser->dragStagingFolder = staging == nullptr ? "" : staging;
    g_free(staging);
    if (!browser->dragStagingFolder.empty()) g_mkdir_with_parents(browser->dragStagingFolder.c_str(), 0755);
    for (GList* node = rows; node != nullptr; node = node->next) {
        GtkTreeIter selectedRow;
        gchar* remotePath = nullptr;
        if (!gtk_tree_model_get_iter(model, &selectedRow, static_cast<GtkTreePath*>(node->data))) continue;
        gtk_tree_model_get(model, &selectedRow, FilePathColumn, &remotePath, -1);
        if (remotePath == nullptr || *remotePath == '\0' || browser->dragStagingFolder.empty()) { g_free(remotePath); continue; }
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
        gtk_tree_model_get(model, &row, FileIconColumn, &icon, FilePathColumn, &name, -1);
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
        GtkWidget* label = gtk_label_new(name == nullptr ? "" : name);
        gtk_label_set_max_width_chars(GTK_LABEL(label), 42);
        gtk_box_pack_start(GTK_BOX(preview), label, false, false, 0);
        gtk_widget_show_all(preview);
        gtk_drag_set_icon_widget(context, preview, 8, 8);
        g_free(name);
    }
    for (GList* node = rows; node != nullptr; node = node->next) gtk_tree_path_free(static_cast<GtkTreePath*>(node->data));
    g_list_free(rows);
}

void TFileBrowserTree::onFileDragEnd(GtkWidget*, GdkDragContext*, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    browser->pendingDragSelectionPaths.clear();
    browser->pendingDragDownloads.clear();
    browser->completedDragDownloads.clear();
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
    GtkTreeSelection* selected = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
    GtkTreeModel* model = nullptr;
    GList* rows = gtk_tree_selection_get_selected_rows(selected, &model);
    std::string paths;
    for (GList* node = rows; node != nullptr; node = node->next) {
        GtkTreeIter row;
        gchar* path = nullptr;
        if (gtk_tree_model_get_iter(model, &row, static_cast<GtkTreePath*>(node->data))) gtk_tree_model_get(model, &row, FilePathColumn, &path, -1);
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
    }
    bool success = false;
    if (info == 1) {
        const guchar* source = gtk_selection_data_get_data(selection);
        const gint length = gtk_selection_data_get_length(selection);
        if (source != nullptr && length > 0) {
            success = true;
            std::istringstream files(std::string(reinterpret_cast<const char*>(source), length));
            for (std::string filePath; std::getline(files, filePath);) if (!filePath.empty() && !rc_filebrowser_move(browser->connection, destination.c_str(), filePath.c_str())) { browser->appendLog(rc_last_error(browser->connection)); success = false; }
        }
    } else if (info == 2) {
        gchar** uris = g_uri_list_extract_uris(reinterpret_cast<const gchar*>(gtk_selection_data_get_data(selection)));
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
    gtk_drag_finish(context, success, false, time);
}

void TFileBrowserTree::showItemMenu(GtkWidget* view, GdkEventButton* event, bool folder) {
    GtkTreePath* path = nullptr;
    if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view), static_cast<gint>(event->x), static_cast<gint>(event->y), &path, nullptr, nullptr, nullptr)) return;
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    if (!gtk_tree_selection_path_is_selected(selection, path)) {
        gtk_tree_selection_unselect_all(selection);
        gtk_tree_selection_select_path(selection, path);
    }
    GtkTreeIter row;
    GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
    gtk_tree_model_get_iter(model, &row, path);
    std::vector<std::string> itemPaths;
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
    for (const std::string& path : item->paths) {
        if (!rc_filebrowser_delete(item->browser->connection, path.c_str())) item->browser->appendLog(rc_last_error(item->browser->connection));
    }
    if (!item->browser->currentFolder.empty() && !rc_filebrowser_cd(item->browser->connection, item->browser->currentFolder.c_str())) item->browser->appendLog(rc_last_error(item->browser->connection));
}

void TFileBrowserTree::onRename(GtkMenuItem*, gpointer data) {
    FileMenuItem* item = static_cast<FileMenuItem*>(data);
    if (item->paths.size() != 1) return;
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
    else if (!browser->currentFolder.empty() && !rc_filebrowser_cd(browser->connection, browser->currentFolder.c_str())) browser->appendLog(rc_last_error(browser->connection));
    g_free(oldPath);
}

void TFileBrowserTree::onMove(GtkMenuItem*, gpointer data) {
    FileMenuItem* item = static_cast<FileMenuItem*>(data);
    GtkWidget* dialog = gtk_dialog_new_with_buttons((item->paths.size() == 1 ? "Move " + item->paths.front() : "Move selected files").c_str(), GTK_WINDOW(item->browser->window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "OK", GTK_RESPONSE_ACCEPT, nullptr);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), item->browser->currentFolder.c_str());
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, false, false, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) for (const std::string& path : item->paths) if (!rc_filebrowser_move(item->browser->connection, gtk_entry_get_text(GTK_ENTRY(entry)), path.c_str())) item->browser->appendLog(rc_last_error(item->browser->connection));
    gtk_widget_destroy(dialog);
}

void TFileBrowserTree::onUpload(GtkMenuItem*, gpointer data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Upload file(s)", GTK_WINDOW(browser->window), GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Upload", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), true);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GSList* filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
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
                if (!rc_upload_file(browser->connection, remotePath.c_str(), contents, static_cast<int>(length))) browser->appendLog(rc_last_error(browser->connection));
                g_free(basename);
                g_free(contents);
            }
            g_free(node->data);
        }
        g_slist_free(filenames);
    }
    gtk_widget_destroy(dialog);
}

void TFileBrowserTree::onFileReceived(const char* path, const void* content, int length, void* data) {
    TFileBrowserTree* browser = static_cast<TFileBrowserTree*>(data);
    if (path == nullptr || content == nullptr || length < 0) return;
    const std::string receivedPath(path);
    bool previewResponse = false;
    for (auto iterator = browser->pendingPreviewDownloads.begin(); iterator != browser->pendingPreviewDownloads.end(); ++iterator) {
        if (!pathMatches(iterator->first, receivedPath)) continue;
        if (iterator->second == browser->previewFolder) browser->cachePreview(iterator->first, content, length);
        browser->pendingPreviewDownloads.erase(iterator);
        previewResponse = true;
        break;
    }
    for (auto iterator = browser->pendingDragDownloads.begin(); iterator != browser->pendingDragDownloads.end(); ++iterator) {
        if (!pathMatches(iterator->first, receivedPath)) continue;
        GError* error = nullptr;
        if (g_file_set_contents(iterator->second.c_str(), static_cast<const gchar*>(content), length, &error)) {
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
        if (!g_file_set_contents(destination, static_cast<const gchar*>(content), length, nullptr)) { g_free(destination); browser->appendLog("Could not save downloaded file."); return; }
        gchar* absoluteDestination = g_canonicalize_filename(destination, nullptr);
        g_free(destination);
        const std::string localPath(absoluteDestination);
        g_free(absoluteDestination);
        const std::string extension = localPath.substr(localPath.find_last_of('.') == std::string::npos ? localPath.size() : localPath.find_last_of('.'));
        if (g_ascii_strcasecmp(extension.c_str(), ".exe") == 0 || g_ascii_strcasecmp(extension.c_str(), ".bat") == 0 || g_ascii_strcasecmp(extension.c_str(), ".sh") == 0) { browser->showTextEditor(path, content, length); return; }
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
        browser->showTextEditor(path, content, length);
        return;
    }
    bool userDownload = false;
    for (auto iterator = browser->pendingUserDownloads.begin(); iterator != browser->pendingUserDownloads.end(); ++iterator) {
        if (!pathMatches(iterator->first, receivedPath)) continue;
        if (--iterator->second == 0) browser->pendingUserDownloads.erase(iterator);
        userDownload = true;
        break;
    }
    if (previewResponse && !userDownload) return;
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
    if (!g_file_set_contents(destination, static_cast<const gchar*>(content), length, nullptr)) {
        g_free(destination);
        browser->appendLog("Could not save downloaded file.");
        return;
    }
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
    GtkWidget* dialog = gtk_dialog_new_with_buttons(editorTitle.c_str(), GTK_WINDOW(window), static_cast<GtkDialogFlags>(0), "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), false);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), nullptr);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_NORMAL);
    gtk_window_set_resizable(GTK_WINDOW(dialog), true);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 520);
    GtkSourceLanguage* language = gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(), "graal");
    GtkSourceBuffer* sourceBuffer = language != nullptr ? gtk_source_buffer_new_with_language(language) : gtk_source_buffer_new(nullptr);
    applyRemoteControlSourceStyle(sourceBuffer);
    GtkWidget* editor = gtk_source_view_new_with_buffer(sourceBuffer);
    configureGScriptEditor(editor);
    addEditorFindButton(dialog, editor);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(editor), true);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(sourceBuffer), static_cast<const char*>(content), length);
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
        rc_upload_file(editorState->connection, editorState->path.c_str(), value, static_cast<int>(strlen(value)));
        g_free(value);
    }), state);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer userData) { delete static_cast<EditorState*>(userData); }), state);
    gtk_widget_show_all(dialog);
}

void TFileBrowserTree::refresh() {
    if (connection != nullptr && !rc_filebrowser_start(connection)) appendLog(rc_last_error(connection));
}

void TFileBrowserTree::refreshFolders() {
    RCFileBrowserFolder* entries = nullptr;
    const int count = rc_copy_filebrowser_folders(connection, &entries);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(folders), GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
    gtk_tree_store_clear(folders);
    for (int index = 0; index < count; ++index) addFolder(entries[index].pattern == nullptr ? "" : entries[index].pattern, entries[index].rights == nullptr ? "" : entries[index].rights);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(folders), FolderDisplayColumn, GTK_SORT_ASCENDING);
    rc_free_filebrowser_folders(entries, count);
}

void TFileBrowserTree::refreshFiles(const char* folder, int count) {
    const std::string responseFolder = folder == nullptr ? "" : folder;
    if (!currentFolder.empty() && responseFolder != currentFolder) return;
    RCFileBrowserEntry* entries = nullptr;
    const int entryCount = count > 0 ? rc_copy_filebrowser_files(connection, &entries) : 0;
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(files), GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
    gtk_list_store_clear(files);
    currentFolder = responseFolder;
    previewFolder = responseFolder;
    gtk_label_set_text(GTK_LABEL(folderPath), (std::string("Current Folder: ") + responseFolder).c_str());
    for (int index = 0; index < entryCount; ++index) {
        GtkTreeIter row;
        gtk_list_store_append(files, &row);
        const std::string modified = formatModified(entries[index].modified);
        const std::string size = entries[index].size == 0 ? "" : std::to_string(entries[index].size);
        gtk_list_store_set(files, &row, FileIconColumn, fileIcon(entries[index], textFileIcon, nwFileIcon, scriptFileIcon, gmapFileIcon, binaryFileIcon, fontFileIcon, archiveFileIcon, configFileIcon, unknownFileIcon), FilePathColumn, entries[index].path == nullptr ? "" : entries[index].path, FileRightsColumn, entries[index].rights == nullptr ? "" : entries[index].rights, FileSizeColumn, size.c_str(), FileModifiedColumn, modified.c_str(), FileSizeSortColumn, entries[index].size, FileModifiedSortColumn, entries[index].modified, -1);
        const std::string path = entries[index].path == nullptr ? "" : entries[index].path;
        if (isPreviewImage(path) && previewCache.size() + pendingPreviewDownloads.size() < 50 && previewCache.find(path) == previewCache.end() && pendingPreviewDownloads.find(path) == pendingPreviewDownloads.end()) {
            pendingPreviewDownloads[path] = responseFolder;
            if (!rc_filebrowser_download(connection, path.c_str())) pendingPreviewDownloads.erase(path);
        }
    }
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(files), FilePathColumn, GTK_SORT_ASCENDING);
    rc_free_filebrowser_files(entries, entryCount);
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
            gtk_tree_store_set(folders, &row, FolderIconColumn, index + 1 == parts.size() ? closedFolderIcon : openFolderIcon, FolderPathColumn, fullPath.c_str(), FolderRightsColumn, index + 1 == parts.size() && rights != nullptr ? rights : "", FolderDisplayColumn, label.c_str(), -1);
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
    gtk_text_buffer_insert(buffer, &end, (std::string(message) + "\n").c_str(), -1);
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(log), &end, 0.0, false, 0.0, 1.0);
}
