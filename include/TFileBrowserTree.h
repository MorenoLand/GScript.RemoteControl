#pragma once

#include <gtk/gtk.h>

#include <filesystem>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

struct PreviewAsyncState;
struct PreviewDecodeRequest;
struct ModernPendingItem {
    GdkPixbuf* icon = nullptr;
    std::string name;
    std::string path;
    std::string rights;
    bool folder = false;
};

class TFileBrowserTree {
public:
    explicit TFileBrowserTree(const std::filesystem::path& applicationDirectory);
    ~TFileBrowserTree();
    void open(void* connection);
    void setConnection(void* connection);
    void openFolder(void* connection, const std::string& folder);
    void hide();
    void setDownloadFolder(const std::string& folder);
    void setDownloadServer(const std::string& server);
    void setServerName(const std::string& server);
    void setModernFileBrowser(bool enabled);
    void setHoverPreviews(bool enabled);
    void setModernThumbnails(bool enabled);
private:
    static void onRefresh(GtkButton*, gpointer data);
    static void onFolderSelected(GtkTreeSelection*, gpointer data);
    static void onFileSelectionChanged(GtkTreeSelection*, gpointer data);
    static void onModernSelectionChanged(GtkIconView*, gpointer data);
    static void onFolderStateChanged(GtkTreeView*, GtkTreeIter*, GtkTreePath*, gpointer data);
    static gboolean onFolderButtonPress(GtkWidget*, GdkEventButton*, gpointer data);
    static gboolean onFileButtonPress(GtkWidget*, GdkEventButton*, gpointer data);
#ifdef _WIN32
    static gboolean onFileButtonRelease(GtkWidget*, GdkEventButton*, gpointer data);
    void startNativeDrag(GtkWidget* widget);
#endif
    static gboolean onFileMotion(GtkWidget*, GdkEventMotion*, gpointer data);
    static gboolean onFileLeave(GtkWidget*, GdkEventCrossing*, gpointer data);
    static gboolean onModernButtonPress(GtkWidget*, GdkEventButton*, gpointer data);
    static gboolean onModernMotion(GtkWidget*, GdkEventMotion*, gpointer data);
    static gboolean onModernKeyPress(GtkWidget*, GdkEventKey*, gpointer data);
    static void onModernSearchChanged(GtkSearchEntry*, gpointer data);
    static void onAddressActivate(GtkEntry*, gpointer data);
    static void onAddressUp(GtkButton*, gpointer data);
    static void onAddressRefresh(GtkButton*, gpointer data);
    static gboolean loadVisibleThumbnails(gpointer data);
    static gboolean appendModernItems(gpointer data);
    static void onFileDragBegin(GtkWidget*, GdkDragContext*, gpointer data);
    static void onFileDragEnd(GtkWidget*, GdkDragContext*, gpointer data);
    static void onFileDragDataGet(GtkWidget*, GdkDragContext*, GtkSelectionData*, guint, guint, gpointer data);
    static void onDropDataReceived(GtkWidget*, GdkDragContext*, gint, gint, GtkSelectionData*, guint, guint, gpointer data);
    static void onDownload(GtkMenuItem*, gpointer data);
    static void onEditAsText(GtkMenuItem*, gpointer data);
    static void onDeleteItem(GtkMenuItem*, gpointer data);
    static void onRename(GtkMenuItem*, gpointer data);
    static void onMove(GtkMenuItem*, gpointer data);
    static void onUpload(GtkMenuItem*, gpointer data);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer data);
    static gboolean onMutationRefresh(gpointer data);
    static void onFolders(int count, void* data);
    static void onFiles(const char* folder, int count, void* data);
    static void onMessage(const char* message, void* data);
    static void onFileReceived(const char* path, const void* content, int length, void* data);
    static gboolean watchExternalFile(gpointer data);
    void refresh();
    void resetState();
    void refreshFolders();
    void refreshFiles(const char* folder, int count);
    void addFolder(const char* pattern, const char* rights);
    void appendLog(const char* message);
    void updateFileStatus();
    bool isPreviewTransferMessage(const char* message) const;
    void showTextEditor(const char* path, const void* content, int length);
    void showItemMenu(GtkWidget* view, GdkEventButton* event, bool folder);
    void navigateTo(const std::string& folder);
    void updateFolderIcons();
    void queueMutationRefresh();
    void rebuildModernItems();
    void queueVisibleThumbnails();
    void startNextPreviewDownload();
    void updateModernThumbnail(const std::string& path, GdkPixbuf* pixbuf);
    void clearModernBuild();
    void clearPreviewCache();
    void hidePreview();
    void showPreview(const std::string& path, int rootX, int rootY);
    bool canAutoPreview(const std::string& path) const;
    void cleanupDragState();
    void hideDragPreview();
    void showDragPreview(int rootX, int rootY);
    void cachePreview(const std::string& path, const void* content, int length);
    void previewWorkerLoop();
    static gboolean onPreviewDecoded(gpointer);
    GtkWidget* window = nullptr;
    std::filesystem::path applicationDirectory;
    GtkWidget* folderPath = nullptr;
    GtkWidget* pathStack = nullptr;
    GtkWidget* addressRow = nullptr;
    GtkWidget* addressEntry = nullptr;
    GtkTreeStore* folders = nullptr;
    GtkListStore* files = nullptr;
    GtkListStore* modernItems = nullptr;
    GtkWidget* folderView = nullptr;
    GtkWidget* folderScrolled = nullptr;
    GtkWidget* fileView = nullptr;
    GtkWidget* fileScrolled = nullptr;
    GtkWidget* fileViewStack = nullptr;
    GtkWidget* modernView = nullptr;
    GtkWidget* modernScrolled = nullptr;
    GtkWidget* modernSearchPopover = nullptr;
    GtkWidget* modernSearchEntry = nullptr;
    GtkCellRenderer* fileNameRenderer = nullptr;
    GtkWidget* previewWindow = nullptr;
    GtkWidget* previewImage = nullptr;
    GtkWidget* previewLabel = nullptr;
    GtkWidget* dragPreviewWindow = nullptr;
    GtkWidget* dragPreviewImage = nullptr;
    GtkWidget* dragPreviewLabel = nullptr;
    GtkWidget* log = nullptr;
    GtkWidget* statusLabel = nullptr;
    GdkPixbuf* closedFolderIcon = nullptr;
    GdkPixbuf* openFolderIcon = nullptr;
    GdkPixbuf* closedFolderLargeIcon = nullptr;
    GdkPixbuf* openFolderLargeIcon = nullptr;
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
    guint mutationRefreshId = 0;
    std::string downloadFolder;
    std::string downloadServer;
    std::vector<std::string> pendingDragSelectionPaths;
    std::unordered_map<std::string, std::string> pendingDragDownloads;
    std::unordered_map<std::string, std::vector<guint8>> pendingNativeDragContents;
    std::unordered_set<std::string> pendingNativeDragDownloadsReady;
    std::unordered_map<std::string, std::size_t> chunkLogSegments;
    std::unordered_map<std::string, std::string> pendingPreviewDownloads;
    std::vector<std::string> queuedPreviewDownloads;
    std::vector<std::string> visiblePreviewPaths;
    std::vector<std::string> previewTransferPaths;
    std::unordered_map<std::string, int> pendingUserDownloads;
    std::unordered_map<std::string, int> remoteModifiedTimes;
    std::unordered_map<std::string, std::uint64_t> previewFileSizes;
    std::unordered_map<std::string, GdkPixbuf*> previewCache;
    std::unordered_map<std::string, int> modernItemIndices;
    std::vector<ModernPendingItem> pendingModernItems;
    std::vector<std::string> previewCacheOrder;
    std::vector<std::string> completedDragDownloads;
    std::vector<std::string> pendingDragLocalPaths;
    std::string dragStagingFolder;
    std::string previewFolder;
    std::string hoveredPreviewPath;
    std::vector<std::string> folderPaths;
    bool modernFileBrowser = false;
    bool hoverPreviews = true;
    bool modernThumbnails = true;
    guint thumbnailLoadId = 0;
    guint modernBuildId = 0;
    std::size_t modernBuildIndex = 0;
    std::shared_ptr<PreviewAsyncState> previewAsyncState;
    std::thread previewWorkerThread;
    std::mutex previewWorkerMutex;
    std::condition_variable previewWorkerCondition;
    std::deque<std::shared_ptr<PreviewDecodeRequest>> previewWorkerJobs;
    bool previewWorkerStop = false;
    int previewRootX = 0;
    int previewRootY = 0;
    std::string downloadDestinationDirectory() const;
#ifdef _WIN32
    guint nativeDragButton = 0;
    gint nativeDragX = 0;
    gint nativeDragY = 0;
#endif
};
