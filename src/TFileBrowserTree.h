#pragma once

#include <gtk/gtk.h>

#include <string>
#include <unordered_map>
#include <vector>

class TFileBrowserTree {
public:
    TFileBrowserTree();
    ~TFileBrowserTree();
    void open(void* connection);
    void openFolder(void* connection, const std::string& folder);
    void setDownloadFolder(const std::string& folder);
private:
    static void onRefresh(GtkButton*, gpointer data);
    static void onFolderSelected(GtkTreeSelection*, gpointer data);
    static gboolean onFolderButtonPress(GtkWidget*, GdkEventButton*, gpointer data);
    static gboolean onFileButtonPress(GtkWidget*, GdkEventButton*, gpointer data);
#ifdef _WIN32
    static gboolean onFileButtonRelease(GtkWidget*, GdkEventButton*, gpointer data);
    void startNativeDrag(GtkWidget* widget);
#endif
    static gboolean onFileMotion(GtkWidget*, GdkEventMotion*, gpointer data);
    static gboolean onFileLeave(GtkWidget*, GdkEventCrossing*, gpointer data);
    static void onFileDragBegin(GtkWidget*, GdkDragContext*, gpointer data);
    static void onFileDragEnd(GtkWidget*, GdkDragContext*, gpointer data);
    static void onFileDragDataGet(GtkWidget*, GdkDragContext*, GtkSelectionData*, guint, guint, gpointer data);
    static void onDropDataReceived(GtkWidget*, GdkDragContext*, gint, gint, GtkSelectionData*, guint, guint, gpointer data);
    static void onDownload(GtkMenuItem*, gpointer data);
    static void onEditAsText(GtkMenuItem*, gpointer data);
    static void onDeleteItem(GtkMenuItem*, gpointer data);
    static void onRename(GtkMenuItem*, gpointer data);
    static void onFileNameEdited(GtkCellRendererText*, gchar* path, gchar* value, gpointer data);
    static void onMove(GtkMenuItem*, gpointer data);
    static void onUpload(GtkMenuItem*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    static void onFolders(int count, void* data);
    static void onFiles(const char* folder, int count, void* data);
    static void onMessage(const char* message, void* data);
    static void onFileReceived(const char* path, const void* content, int length, void* data);
    static gboolean watchExternalFile(gpointer data);
    static gboolean beginInlineRename(gpointer data);
    void refresh();
    void refreshFolders();
    void refreshFiles(const char* folder, int count);
    void addFolder(const char* pattern, const char* rights);
    void appendLog(const char* message);
    void showTextEditor(const char* path, const void* content, int length);
    void showItemMenu(GtkWidget* view, GdkEventButton* event, bool folder);
    void clearPreviewCache();
    void hidePreview();
    void showPreview(const std::string& path, int rootX, int rootY);
    void cachePreview(const std::string& path, const void* content, int length);
    GtkWidget* window = nullptr;
    GtkWidget* folderPath = nullptr;
    GtkTreeStore* folders = nullptr;
    GtkListStore* files = nullptr;
    GtkWidget* folderView = nullptr;
    GtkWidget* fileView = nullptr;
    GtkCellRenderer* fileNameRenderer = nullptr;
    GtkWidget* previewWindow = nullptr;
    GtkWidget* previewImage = nullptr;
    GtkWidget* previewLabel = nullptr;
    GtkWidget* log = nullptr;
    GdkPixbuf* closedFolderIcon = nullptr;
    GdkPixbuf* openFolderIcon = nullptr;
    GdkPixbuf* textFileIcon = nullptr;
    GdkPixbuf* nwFileIcon = nullptr;
    GdkPixbuf* scriptFileIcon = nullptr;
    GdkPixbuf* gmapFileIcon = nullptr;
    GdkPixbuf* binaryFileIcon = nullptr;
    GdkPixbuf* fontFileIcon = nullptr;
    GdkPixbuf* archiveFileIcon = nullptr;
    GdkPixbuf* configFileIcon = nullptr;
    GdkPixbuf* unknownFileIcon = nullptr;
    void* connection = nullptr;
    std::string currentFolder;
    std::string pendingEditPath;
    std::string pendingExternalPath;
    std::string watchExternalPath;
    std::string watchExternalFolder;
    std::string watchExternalRemotePath;
    gint64 watchExternalModified = 0;
    bool watchExternalWritable = false;
    guint externalWatchId = 0;
    std::string pendingInlineRenamePath;
    guint inlineRenameId = 0;
    std::string downloadFolder;
    std::vector<std::string> pendingDragSelectionPaths;
    std::unordered_map<std::string, std::string> pendingDragDownloads;
    std::unordered_map<std::string, std::string> pendingPreviewDownloads;
    std::unordered_map<std::string, int> pendingUserDownloads;
    std::unordered_map<std::string, GdkPixbuf*> previewCache;
    std::vector<std::string> previewCacheOrder;
    std::vector<std::string> completedDragDownloads;
    std::vector<std::string> pendingDragLocalPaths;
    std::string dragStagingFolder;
    std::string previewFolder;
    std::string hoveredPreviewPath;
    int previewRootX = 0;
    int previewRootY = 0;
#ifdef _WIN32
    guint nativeDragButton = 0;
    gint nativeDragX = 0;
    gint nativeDragY = 0;
#endif
};
