#include "TRemoteFrame.h"
#include "TErrorWindow.h"
#include "TBackup.h"
#include "TDebug.h"
#ifdef _WIN32
#include <gdk/gdkwin32.h>
#include <windows.h>
#endif
#include "TGScriptEditor.h"
#include "TEditorFind.h"
#include "TScriptEditorTracking.h"
#include "TRCOptions.h"
#include "TTheme.h"
#include "TFileBrowserTree.h"
#include "TPlayerList.h"
#include "TScriptList.h"
#include "TServerTextEditor.h"
#include "TToallsWindow.h"
#include "TAccountsWindow.h"
#include "TOptionsWindow.h"
#include "TNPCList.h"
#include "TLevelList.h"
#include "TSyncManager.h"
#include "TExtensions.h"
#include "TMcpServer.h"

#include <grclib.h>
#include <IEnums.h>
#include <gtksourceview/gtksource.h>
#include <webp/demux.h>

#include <cctype>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

extern void remote_control_begin_pm_tray_alert();
extern void remote_control_clear_pm_tray_alert();
extern void remote_control_set_tray_label(const char* serverName, int playerCount);

namespace {
struct ChannelScrollRequest { GtkWidget* field = nullptr; double previousValue = 0.0; };
std::string remoteControlTitle(const std::string& server = {}, const std::string& players = {}, int syncProgress = -1) {
    std::string title = server.empty() ? "Remote Control" : server;
    if (!players.empty()) title += " [" + players + "]";
    if (syncProgress >= 0) title += " [Sync: " + std::to_string(syncProgress) + "%]";
    return title + " - " + REMOTE_CONTROL_BUILD_DATE;
}

}

gboolean TRemoteFrame::scrollChannelToBottom(gpointer data) {
    auto* request = static_cast<ChannelScrollRequest*>(data);
    GtkWidget* field = request->field;
    GtkWidget* scrolled = gtk_widget_get_parent(field);
    if (GTK_IS_SCROLLED_WINDOW(scrolled)) {
        GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
        if (std::abs(gtk_adjustment_get_value(adjustment) - request->previousValue) <= 2.0) {
            GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(field));
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buffer, &end);
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(field), &end, 0.0, false, 0.0, 1.0);
            gtk_adjustment_set_value(adjustment, gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment));
        }
    }
    g_object_unref(field);
    delete request;
    return G_SOURCE_REMOVE;
}

struct WebPAnimation {
    std::vector<uint8_t> data;
    WebPData source{};
    WebPAnimDecoder* decoder = nullptr;
    WebPAnimInfo info{};
    GdkPixbuf* frame = nullptr;
    int previousTimestamp = 0;
    gint64 nextFrame = 0;

    ~WebPAnimation() {
        if (frame != nullptr) g_object_unref(frame);
        if (decoder != nullptr) WebPAnimDecoderDelete(decoder);
    }

    bool advance(gint64 now, bool force = false) {
        if (!force && now < nextFrame) return false;
        if (!WebPAnimDecoderHasMoreFrames(decoder)) {
            WebPAnimDecoderReset(decoder);
            previousTimestamp = 0;
        }
        uint8_t* pixels = nullptr;
        int timestamp = 0;
        if (!WebPAnimDecoderGetNext(decoder, &pixels, &timestamp)) return false;
        GdkPixbuf* sourceFrame = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, true, 8, info.canvas_width, info.canvas_height, info.canvas_width * 4, nullptr, nullptr);
        if (sourceFrame == nullptr) return false;
        GdkPixbuf* next = gdk_pixbuf_copy(sourceFrame);
        g_object_unref(sourceFrame);
        if (next == nullptr) return false;
        if (frame != nullptr) g_object_unref(frame);
        frame = next;
        const int duration = std::max(10, timestamp - previousTimestamp);
        previousTimestamp = timestamp;
        nextFrame = now + static_cast<gint64>(duration) * 1000;
        return true;
    }
};

namespace {
    bool hasActiveRemoteControlWindow() {
        GList* windows = gtk_window_list_toplevels();
        bool active = false;
        for (GList* current = windows; current != nullptr; current = current->next) {
            if (GTK_IS_WINDOW(current->data) && gtk_window_is_active(GTK_WINDOW(current->data))) { active = true; break; }
        }
        g_list_free(windows);
        return active;
    }

    void clearRemoteControlUrgency() {
        GList* windows = gtk_window_list_toplevels();
        for (GList* current = windows; current != nullptr; current = current->next) if (GTK_IS_WINDOW(current->data)) gtk_window_set_urgency_hint(GTK_WINDOW(current->data), false);
        g_list_free(windows);
    }

    std::string chatTimestamp(const RC::RCOptions& options) {
        if (!options.rctimestamps) return "";
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        std::ostringstream text;
        text << std::put_time(&local, options.timestampformat.c_str());
        return text.str();
    }

    std::unique_ptr<WebPAnimation> loadWebPAnimation(const std::filesystem::path& path) {
        gchar* contents = nullptr;
        gsize length = 0;
        if (!g_file_get_contents(path.string().c_str(), &contents, &length, nullptr)) return nullptr;
        auto animation = std::make_unique<WebPAnimation>();
        animation->data.assign(reinterpret_cast<const uint8_t*>(contents), reinterpret_cast<const uint8_t*>(contents) + length);
        g_free(contents);
        animation->source.bytes = animation->data.data();
        animation->source.size = animation->data.size();
        WebPAnimDecoderOptions options;
        WebPAnimDecoderOptionsInit(&options);
        animation->decoder = WebPAnimDecoderNew(&animation->source, &options);
        if (animation->decoder == nullptr || !WebPAnimDecoderGetInfo(animation->decoder, &animation->info)) return nullptr;
        if (!animation->advance(g_get_monotonic_time(), true)) return nullptr;
        return animation;
    }
}

TRemoteFrame::TRemoteFrame(const RC::RCOptions& nextOptions, const std::filesystem::path& nextApplicationDirectory, std::function<void()> onClose, std::function<void()> onListServer, std::function<void()> onListServerSettings) : onCloseCallback(std::move(onClose)), onListServerCallback(std::move(onListServer)), onListServerSettingsCallback(std::move(onListServerSettings)), options(nextOptions), applicationDirectory(nextApplicationDirectory) {
    kappaEmote = gdk_pixbuf_new_from_file((applicationDirectory / "images" / "emote_kappa.png").string().c_str(), nullptr);
    pmNormalEmote = gdk_pixbuf_new_from_file((applicationDirectory / "images" / "pmicon_normal.png").string().c_str(), nullptr);
    pacmanEmote = gdk_pixbuf_new_from_file((applicationDirectory / "images" / "emote_pacman.png").string().c_str(), nullptr);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    syncManager = std::make_unique<TSyncManager>(window, applicationDirectory, [this](int progress, bool active) { syncProgress = progress; syncInProgress = active; });
    gtk_widget_set_name(window, "RemoteFrame");
    gtk_window_set_title(GTK_WINDOW(window), remoteControlTitle().c_str());
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 350);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    graphicalContainer = nullptr;

    if (!options.graphicalmenu) {
        GtkWidget* menuBar = gtk_menu_bar_new();
        const char* menuNames[] = {"Players", "Files", "Configuration", "Scripts", "Misc"};
        for (const char* menuName : menuNames) {
        GtkWidget* item = gtk_menu_item_new_with_label(menuName);
        GtkWidget* menu = gtk_menu_new();
        if (std::string(menuName) == "Players") {
            addMenuItem(menu, "Playerlist", G_CALLBACK(onPlayerList));
            addMenuItem(menu, "Accounts", G_CALLBACK(onAccounts));
            addMenuItem(menu, "Toalls", G_CALLBACK(onToalls));
        } else if (std::string(menuName) == "Files") {
            addMenuItem(menu, "File Browser", G_CALLBACK(onFileBrowser));
        } else if (std::string(menuName) == "Configuration") {
            addMenuItem(menu, "RC Options", G_CALLBACK(onRCOptions));
            addMenuItem(menu, "Server Options", G_CALLBACK(onServerOptions));
            addMenuItem(menu, "Folder Config", G_CALLBACK(onFolderConfig));
        } else if (std::string(menuName) == "Scripts") {
            addMenuItem(menu, "NPCs", G_CALLBACK(onNPCs));
            addMenuItem(menu, "Classes", G_CALLBACK(onClasses));
            addMenuItem(menu, "Weapons (GUI)", G_CALLBACK(onWeapons));
        } else {
            addMenuItem(menu, "Server Flags", G_CALLBACK(onServerFlags));
            addMenuItem(menu, "Level-NPC dump", G_CALLBACK(onLocalNPCDump));
            addMenuItem(menu, "Extensions", G_CALLBACK(onExtensions));
        }
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
        gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), item);
        }
        gtk_box_pack_start(GTK_BOX(root), menuBar, false, false, 0);
    } else {
        graphicalContainer = gtk_overlay_new();
        GtkWidget* graphicalBase = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(graphicalContainer), graphicalBase);
        GtkWidget* header = gtk_overlay_new();
        gtk_widget_set_size_request(header, 1, 180);
        gtk_widget_set_hexpand(header, true);
        GtkWidget* fixed = gtk_fixed_new();
        graphicalFixed = fixed;
        gtk_widget_set_hexpand(fixed, true);
        gtk_widget_set_halign(fixed, GTK_ALIGN_FILL);
        gtk_widget_set_valign(fixed, GTK_ALIGN_FILL);
        const std::filesystem::path configuredBackground(options.background);
        const std::filesystem::path background = configuredBackground.is_absolute() ? configuredBackground : applicationDirectory / "images" / configuredBackground;
        std::string extension = background.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (extension == ".webp" && (backgroundWebPAnimation = loadWebPAnimation(background)) != nullptr) {
        } else {
            GError* imageError = nullptr;
            backgroundAnimation = gdk_pixbuf_animation_new_from_file(background.string().c_str(), &imageError);
            if (backgroundAnimation != nullptr && gdk_pixbuf_animation_is_static_image(backgroundAnimation)) {
                backgroundPixbuf = gdk_pixbuf_animation_get_static_image(backgroundAnimation);
                g_object_ref(backgroundPixbuf);
                g_object_unref(backgroundAnimation);
                backgroundAnimation = nullptr;
            } else if (backgroundAnimation != nullptr) {
                GTimeVal now;
                g_get_current_time(&now);
                backgroundAnimationIter = gdk_pixbuf_animation_get_iter(backgroundAnimation, &now);
            }
            if (imageError != nullptr) g_error_free(imageError);
        }
        backgroundImage = gtk_drawing_area_new();
        gtk_widget_set_hexpand(backgroundImage, true);
        gtk_widget_set_halign(backgroundImage, GTK_ALIGN_FILL);
        gtk_container_add(GTK_CONTAINER(header), backgroundImage);
        gtk_widget_set_hexpand(fixed, true);
        gtk_widget_set_halign(fixed, GTK_ALIGN_FILL);
        gtk_widget_set_valign(fixed, GTK_ALIGN_FILL);
        gtk_overlay_add_overlay(GTK_OVERLAY(header), fixed);
        g_signal_connect(backgroundImage, "draw", G_CALLBACK(onGraphicalDraw), this);
        if (backgroundAnimationIter != nullptr || backgroundWebPAnimation != nullptr) backgroundAnimationSource = g_timeout_add(16, +[](gpointer data) -> gboolean {
            TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
            if (frame->backgroundImage == nullptr) return G_SOURCE_REMOVE;
            if (frame->backgroundWebPAnimation != nullptr) {
                const gint64 now = g_get_monotonic_time();
                if (frame->backgroundWebPAnimation->advance(now)) gtk_widget_queue_draw(frame->backgroundImage);
                return G_SOURCE_CONTINUE;
            }
            if (frame->backgroundAnimationIter == nullptr) return G_SOURCE_REMOVE;
            GTimeVal now;
            g_get_current_time(&now);
            if (!gdk_pixbuf_animation_iter_advance(frame->backgroundAnimationIter, &now)) return G_SOURCE_CONTINUE;
            gtk_widget_queue_draw(frame->backgroundImage);
            return G_SOURCE_CONTINUE;
        }, this);
        const int positions[15][2] = {{5, 15}, {5, 48}, {38, 15}, {71, 15}, {394, 15}, {427, 15}, {460, 15}, {460, 48}, {460, 81}, {427, 114}, {394, 114}, {360, 114}, {104, 15}, {460, 114}, {361, 15}};
        const char* tooltips[15] = {"Player List", "File Browser", "Accounts", "Toalls", "Options", "Server Flags", "Folder Options", "Server Options", "Local NPCs", "Classes", "Weapons", "NPCs", "Extensions", "Level List", "Sync & Git"};
        for (int index = 0; index < 15; ++index) {
            GtkWidget* button = gtk_event_box_new();
            gtk_event_box_set_visible_window(GTK_EVENT_BOX(button), false);
            gtk_widget_add_events(button, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
            GtkWidget* buttonImage = gtk_image_new_from_file((applicationDirectory / "images" / options.buttonimagefiles[index]).string().c_str());
            gtk_container_add(GTK_CONTAINER(button), buttonImage);
            g_object_set_data(G_OBJECT(button), "button-index", GINT_TO_POINTER(index));
            g_signal_connect(button, "button-press-event", G_CALLBACK(onGraphicalButton), this);
            g_signal_connect(button, "button-release-event", G_CALLBACK(onGraphicalButton), this);
            gtk_widget_set_tooltip_text(button, tooltips[index]);
            gtk_fixed_put(GTK_FIXED(fixed), button, positions[index][0], positions[index][1]);
            graphicalButtons[index] = button;
            if (index >= 8 && index != 12) gtk_widget_hide(button);
        }
        GtkWidget* listServerSettings = gtk_button_new_with_label("⚙");
        gtk_widget_set_size_request(listServerSettings, 28, 28);
        gtk_widget_set_tooltip_text(listServerSettings, "List server settings");
        gtk_fixed_put(GTK_FIXED(fixed), listServerSettings, 360, 15);
        g_signal_connect(listServerSettings, "clicked", G_CALLBACK(TRemoteFrame::onListServerSettings), this);
        gtk_widget_set_no_show_all(listServerSettings, true);
        gtk_widget_hide(listServerSettings);
        GdkColor labelColor;
        GdkColor labelBackgroundColor;
        gdk_color_parse(options.colorlabel.c_str(), &labelColor);
        gdk_color_parse(options.colorlabelback.c_str(), &labelBackgroundColor);
        PangoFontDescription* labelFont = pango_font_description_from_string("Sans Bold 12");
        const auto addLabel = [&](const std::string& text, int x, int y, GtkWidget** front, std::array<GtkWidget*, 8>* shadows) {
            const int offsets[][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
            for (int index = 0; index < static_cast<int>(shadows->size()); ++index) {
                const auto& offset = offsets[index];
                GtkWidget* shadow = gtk_label_new(text.c_str());
                gtk_widget_set_size_request(shadow, 300, -1);
                gtk_label_set_xalign(GTK_LABEL(shadow), 0.0f);
                gtk_label_set_single_line_mode(GTK_LABEL(shadow), true);
                gtk_widget_modify_fg(shadow, GTK_STATE_NORMAL, &labelBackgroundColor);
                gtk_widget_modify_font(shadow, labelFont);
                gtk_fixed_put(GTK_FIXED(fixed), shadow, x + offset[0], y + offset[1]);
                (*shadows)[index] = shadow;
            }
            *front = gtk_label_new(text.c_str());
            gtk_widget_set_size_request(*front, 300, -1);
            gtk_label_set_xalign(GTK_LABEL(*front), 0.0f);
            gtk_label_set_single_line_mode(GTK_LABEL(*front), true);
            gtk_widget_modify_fg(*front, GTK_STATE_NORMAL, &labelColor);
            gtk_widget_modify_font(*front, labelFont);
            gtk_fixed_put(GTK_FIXED(fixed), *front, x, y);
        };
        addLabel(options.labelservers, 10, 90, &serverLabel, &serverLabelShadows);
        addLabel(options.labelplayers, 10, 110, &playersLabel, &playersLabelShadows);
        addLabel(options.labelnpcserver, 10, 130, &npcServerLabel, &npcServerLabelShadows);
        gtk_widget_set_size_request(npcServerLabel, 500, -1);
        gtk_label_set_xalign(GTK_LABEL(npcServerLabel), 0.5f);
        gtk_fixed_move(GTK_FIXED(fixed), npcServerLabel, 0, 130);
        const int npcShadowOffsets[][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
        for (int index = 0; index < static_cast<int>(npcServerLabelShadows.size()); ++index) {
            GtkWidget* shadow = npcServerLabelShadows[index];
            if (shadow == nullptr) continue;
            gtk_widget_set_size_request(shadow, 500, -1);
            gtk_label_set_xalign(GTK_LABEL(shadow), 0.5f);
            gtk_fixed_move(GTK_FIXED(fixed), shadow, npcShadowOffsets[index][0], 130 + npcShadowOffsets[index][1]);
        }
        pango_font_description_free(labelFont);
        gtk_box_pack_start(GTK_BOX(graphicalBase), header, false, false, 0);
        GtkWidget* filler = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_pack_start(GTK_BOX(graphicalBase), filler, true, true, 0);
        gtk_box_pack_start(GTK_BOX(root), graphicalContainer, true, true, 0);
        g_signal_connect(fixed, "size-allocate", G_CALLBACK(onGraphicalAllocate), this);
    }

    notebook = gtk_notebook_new();
    gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), false);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), true);
    chatScrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chatScrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(chatScrolled), GTK_SHADOW_NONE);
    chatField = gtk_text_view_new();
    gtk_widget_set_name(chatField, "ChatField");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(chatField), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(chatField), false);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chatField), GTK_WRAP_WORD_CHAR);
    configureChatField(chatField);
    gtk_container_add(GTK_CONTAINER(chatScrolled), chatField);
    GtkWidget* chatTab = gtk_label_new("RC Chat");
    gtk_widget_set_size_request(chatTab, -1, 16);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), chatScrolled, chatTab);
    gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(notebook), chatScrolled, false);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), chatScrolled, true);
    refreshNotebookTheme();
    if (graphicalContainer != nullptr) {
        gtk_widget_set_halign(notebook, GTK_ALIGN_FILL);
        gtk_widget_set_valign(notebook, GTK_ALIGN_FILL);
        gtk_widget_set_margin_top(notebook, 156);
        gtk_overlay_add_overlay(GTK_OVERLAY(graphicalContainer), notebook);
    } else gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);
    extensionsManager = std::make_unique<TExtensionsManager>(GTK_WINDOW(window), applicationDirectory, [this](const std::string& extension, const std::string& output) { appendChannelMessage(extension, output); }, [this](const std::string& extension) { removeChannel(extension); });

    editField = gtk_entry_new();
    gtk_widget_set_name(editField, "EditField");
    if (options.graphicalmenu) {
        GdkColor editBackgroundColor;
        GdkColor editColor;
        gdk_color_parse(options.coloreditback.c_str(), &editBackgroundColor);
        gdk_color_parse(options.coloredit.c_str(), &editColor);
        gtk_widget_modify_base(editField, GTK_STATE_NORMAL, &editBackgroundColor);
        gtk_widget_modify_text(editField, GTK_STATE_NORMAL, &editColor);
    }
    gtk_box_pack_start(GTK_BOX(root), editField, false, false, 0);

    mentionStore = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    mentionCompletion = gtk_entry_completion_new();
    gtk_entry_completion_set_model(mentionCompletion, GTK_TREE_MODEL(mentionStore));
    gtk_entry_completion_set_text_column(mentionCompletion, 0);
    gtk_entry_completion_set_minimum_key_length(mentionCompletion, 1);
    gtk_entry_completion_set_popup_single_match(mentionCompletion, true);
    gtk_entry_completion_set_match_func(mentionCompletion, onMentionMatch, this, nullptr);
    gtk_entry_set_completion(GTK_ENTRY(editField), mentionCompletion);
    g_signal_connect(mentionCompletion, "match-selected", G_CALLBACK(onMentionSelected), this);
    g_signal_connect(editField, "changed", G_CALLBACK(onMentionChanged), this);
    g_signal_connect(editField, "key-press-event", G_CALLBACK(onEditKey), this);
    gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK);
    gtk_widget_add_events(chatField, GDK_POINTER_MOTION_MASK);
    gtk_widget_add_events(notebook, GDK_POINTER_MOTION_MASK);
    g_signal_connect(window, "button-press-event", G_CALLBACK(onActivityEvent), this);
    g_signal_connect(window, "motion-notify-event", G_CALLBACK(onActivityEvent), this);
    g_signal_connect(editField, "key-press-event", G_CALLBACK(onActivityEvent), this);
    g_signal_connect(chatField, "motion-notify-event", G_CALLBACK(onActivityEvent), this);
    g_signal_connect(notebook, "motion-notify-event", G_CALLBACK(onActivityEvent), this);
    g_signal_connect(window, "key-press-event", G_CALLBACK(onWindowKey), this);
    g_signal_connect(window, "configure-event", G_CALLBACK(onConfigure), this);
    g_signal_connect(window, "window-state-event", G_CALLBACK(onWindowState), this);
    g_signal_connect(window, "focus-in-event", G_CALLBACK(+[](GtkWidget*, GdkEventFocus*, gpointer data) -> gboolean { TRemoteFrame* frame = static_cast<TRemoteFrame*>(data); clearRemoteControlUrgency(); remote_control_clear_pm_tray_alert(); if (frame->playerList != nullptr) frame->playerList->clearPrivateMessageAlert(); return false; }), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

gboolean TRemoteFrame::onConfigure(GtkWidget*, GdkEventConfigure* event, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    frame->repositionGraphicalButtons(event->width);
    GdkWindow* nativeWindow = gtk_widget_get_window(frame->window);
    const bool maximized = nativeWindow != nullptr && (gdk_window_get_state(nativeWindow) & GDK_WINDOW_STATE_MAXIMIZED) != 0;
    if (!frame->windowMaximized && !maximized && event->width > 0 && event->height > 0) {
        frame->normalWindowWidth = event->width;
        frame->normalWindowHeight = event->height;
    }
    return false;
}

gboolean TRemoteFrame::onWindowState(GtkWidget*, GdkEventWindowState* event, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if ((event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED) == 0) return false;
    const bool maximized = (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
#ifdef _WIN32
    if (!frame->windowMaximized && maximized) {
        GdkWindow* nativeWindow = gtk_widget_get_window(frame->window);
        HWND handle = nativeWindow == nullptr ? nullptr : reinterpret_cast<HWND>(GDK_WINDOW_HWND(nativeWindow));
        WINDOWPLACEMENT placement = {sizeof(WINDOWPLACEMENT)};
        if (handle != nullptr && GetWindowPlacement(handle, &placement)) {
            frame->normalWindowX = placement.rcNormalPosition.left;
            frame->normalWindowY = placement.rcNormalPosition.top;
            frame->normalWindowWidth = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
            frame->normalWindowHeight = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
            frame->hasNativeNormalWindowGeometry = frame->normalWindowWidth > 0 && frame->normalWindowHeight > 0;
        }
    }
#endif
    if (frame->windowMaximized && !maximized) {
        g_idle_add(+[](gpointer value) -> gboolean {
            TRemoteFrame* target = static_cast<TRemoteFrame*>(value);
            target->windowMaximized = false;
#ifdef _WIN32
            GdkWindow* nativeWindow = gtk_widget_get_window(target->window);
            HWND handle = nativeWindow == nullptr ? nullptr : reinterpret_cast<HWND>(GDK_WINDOW_HWND(nativeWindow));
            if (target->hasNativeNormalWindowGeometry && handle != nullptr) SetWindowPos(handle, nullptr, target->normalWindowX, target->normalWindowY, target->normalWindowWidth, target->normalWindowHeight, SWP_NOACTIVATE | SWP_NOZORDER);
            else gtk_window_resize(GTK_WINDOW(target->window), target->normalWindowWidth, target->normalWindowHeight);
#else
            gtk_window_resize(GTK_WINDOW(target->window), target->normalWindowWidth, target->normalWindowHeight);
#endif
            target->repositionGraphicalButtons(target->normalWindowWidth);
            return G_SOURCE_REMOVE;
        }, frame);
        return false;
    }
    frame->windowMaximized = maximized;
    g_idle_add(+[](gpointer value) -> gboolean { static_cast<TRemoteFrame*>(value)->repositionGraphicalButtons(); return G_SOURCE_REMOVE; }, frame);
    return false;
}

gboolean TRemoteFrame::onGraphicalDraw(GtkWidget* widget, cairo_t* context, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    GdkPixbuf* pixbuf = frame->backgroundWebPAnimation != nullptr ? frame->backgroundWebPAnimation->frame : (frame->backgroundAnimationIter != nullptr ? gdk_pixbuf_animation_iter_get_pixbuf(frame->backgroundAnimationIter) : frame->backgroundPixbuf);
    if (pixbuf == nullptr) return false;
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    cairo_save(context);
    cairo_scale(context, static_cast<double>(std::max(1, allocation.width)) / gdk_pixbuf_get_width(pixbuf), static_cast<double>(std::max(1, allocation.height)) / gdk_pixbuf_get_height(pixbuf));
    gdk_cairo_set_source_pixbuf(context, pixbuf, 0, 0);
    cairo_paint(context);
    GdkRGBA tint{};
    bool hasTint = gdk_rgba_parse(&tint, frame->options.backgroundtint.c_str());
    if (!hasTint && frame->options.backgroundtint.size() == 9 && frame->options.backgroundtint.front() == '#') {
        unsigned int red = 0, green = 0, blue = 0, alpha = 0;
        if (std::sscanf(frame->options.backgroundtint.c_str() + 1, "%02x%02x%02x%02x", &red, &green, &blue, &alpha) == 4) { tint.red = red / 255.0; tint.green = green / 255.0; tint.blue = blue / 255.0; tint.alpha = alpha / 255.0; hasTint = true; }
    }
    if (hasTint && (tint.alpha > 0.0 || frame->options.backgroundtintsolid)) {
        tint.alpha = frame->options.backgroundtintsolid ? 1.0 : 0.35;
        cairo_set_source_rgba(context, tint.red, tint.green, tint.blue, tint.alpha);
        cairo_paint(context);
    }
    cairo_restore(context);
    return false;
}

TRemoteFrame::~TRemoteFrame() {
    *callbackAlive = false;
    if (eventSource != 0) g_source_remove(eventSource);
    if (backgroundAnimationSource != 0) g_source_remove(backgroundAnimationSource);
    GList* toplevels = gtk_window_list_toplevels();
    for (GList* current = toplevels; current != nullptr; current = current->next) {
        GtkWidget* candidate = GTK_WIDGET(current->data);
        if (GTK_IS_WINDOW(candidate) && g_object_get_data(G_OBJECT(candidate), "rc-afk-frame") == this) { g_signal_handlers_disconnect_by_func(candidate, reinterpret_cast<gpointer>(G_CALLBACK(onActivityEvent)), this); g_object_set_data(G_OBJECT(candidate), "rc-afk-frame", nullptr); }
    }
    g_list_free(toplevels);
    extensionsManager.reset();
    if (window != nullptr) gtk_widget_destroy(window);
    if (notebookTabProvider != nullptr) { gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(notebookTabProvider)); g_object_unref(notebookTabProvider); }
    if (backgroundPixbuf != nullptr) g_object_unref(backgroundPixbuf);
    if (backgroundAnimationIter != nullptr) g_object_unref(backgroundAnimationIter);
    if (backgroundAnimation != nullptr) g_object_unref(backgroundAnimation);
    if (kappaEmote != nullptr) g_object_unref(kappaEmote);
    if (pmNormalEmote != nullptr) g_object_unref(pmNormalEmote);
    if (pacmanEmote != nullptr) g_object_unref(pacmanEmote);
    delete playerList;
    delete fileBrowser;
    delete classList;
    delete weaponList;
    delete serverOptionsEditor;
    delete serverFlagsEditor;
    delete folderConfigEditor;
    delete toallsWindow;
    delete accountsWindow;
    delete optionsWindow;
    delete npcList;
    delete levelList;
}

void TRemoteFrame::open(void* nextConnection, int serverIndex, const std::string& serverName, const std::string& nickname, const std::string& accountName) {
    connection = nextConnection;
    currentServerIndex = serverIndex;
    this->serverName = serverName;
    syncManager->setConnection(nextConnection, serverName);
    gtk_window_set_title(GTK_WINDOW(window), remoteControlTitle(serverName).c_str());
    trayPlayerCount = -1;
    setBackupServerName(serverName);
    disconnectHandled = false;
    this->nickname = nickname;
    baseNickname = nickname;
    lastActivity = g_get_monotonic_time();
    awayNicknameApplied = false;
    awayStatusApplied = false;
    this->accountName = accountName;
    playerCommunityNames.clear();
    ncConnectionAttempted = false;
    ncManuallyDisconnected = false;
    rc_on_connected(connection, onConnected, this);
    rc_on_disconnected(connection, onDisconnected, this);
    rc_on_message(connection, onMessage, this);
    rc_on_irc_message(connection, onIrcMessage, this);
    rc_on_private_message_ex(connection, onPrivateMessage, this);
    rc_on_player_properties_changed(connection, onPlayerPropertiesChanged, this);
    rc_on_player_prop_changed(connection, onPlayerPropChanged, this);
    rc_on_raw_packet(connection, onRawPacket, this);
    rc_on_server_data(connection, onServerData, this);
    rc_on_account_list(connection, onAccountList, this);
    rc_on_player_text_data(connection, onPlayerText, this);
    rc_on_player_rights(connection, onPlayerRights, this);
    rc_on_player_attributes(connection, onPlayerAttributes, this);
    rc_on_ban_data(connection, onBanData, this);
    rc_on_ban_list_data(connection, onBanListData, this);
    setNCChannelVisible(options.separatenc);
    const std::string serverText = options.labelservers.empty() ? serverName : options.labelservers + " " + serverName;
    if (serverLabel != nullptr) gtk_label_set_text(GTK_LABEL(serverLabel), serverText.c_str());
    for (GtkWidget* shadow : serverLabelShadows) if (shadow != nullptr) gtk_label_set_text(GTK_LABEL(shadow), serverText.c_str());
    if (eventSource == 0) eventSource = g_timeout_add(50, processEvents, this);
    refreshMentionCompletion();
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(editField);
}

void TRemoteFrame::disconnect() {
    if (eventSource != 0) {
        g_source_remove(eventSource);
        eventSource = 0;
    }
    if (connection == nullptr) return;
    rc_disconnect(connection);
    connection = nullptr;
}

void TRemoteFrame::signOut() {
    disconnectHandled = true;
    if (window != nullptr) gtk_widget_hide(window);
    disconnect();
}

bool TRemoteFrame::isNCAuthenticated() const { return connection != nullptr && rc_is_nc_authenticated(connection) != 0; }
const std::string& TRemoteFrame::currentServerName() const { return serverName; }
void TRemoteFrame::setDownloadServer(const std::string& server) { if (fileBrowser != nullptr) fileBrowser->setDownloadServer(server); }
bool TRemoteFrame::isConnected() const { return connection != nullptr; }

void TRemoteFrame::show() {
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

void TRemoteFrame::toggleVisibility() {
    if (gtk_widget_get_visible(window)) gtk_widget_hide(window);
    else show();
}

bool TRemoteFrame::isVisible() const { return window != nullptr && gtk_widget_get_visible(window); }

void TRemoteFrame::hideFromTray() {
    if (!isVisible()) return;
    gtk_window_get_position(GTK_WINDOW(window), &trayWindowX, &trayWindowY);
    trayWindowPositionValid = true;
    trayWindowMaximized = gtk_window_is_maximized(GTK_WINDOW(window));
    gtk_widget_hide(window);
}

void TRemoteFrame::showFromTray() {
    gtk_widget_show_all(window);
    if (trayWindowPositionValid && !trayWindowMaximized) gtk_window_move(GTK_WINDOW(window), trayWindowX, trayWindowY);
    if (trayWindowMaximized) gtk_window_maximize(GTK_WINDOW(window));
    gtk_window_present(GTK_WINDOW(window));
}

bool TRemoteFrame::openLatestPrivateMessage() {
    return playerList != nullptr && playerList->openLatestPrivateMessage();
}

void TRemoteFrame::onSend(GtkButton*, gpointer data) { static_cast<TRemoteFrame*>(data)->send(); }

void TRemoteFrame::onPlayerList(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    frame->playerList->setServerName(frame->serverName);
    frame->playerList->open(frame->connection);
}

void TRemoteFrame::onToalls(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->toallsWindow == nullptr) frame->toallsWindow = new TToallsWindow();
    frame->toallsWindow->setServerName(frame->serverName);
    frame->toallsWindow->open(frame->connection, frame->nickname);
}

void TRemoteFrame::onAccounts(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->accountsWindow == nullptr) frame->accountsWindow = new TAccountsWindow();
    frame->accountsWindow->setServerName(frame->serverName);
    frame->accountsWindow->open(frame->connection);
}

void TRemoteFrame::onRCOptions(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->optionsWindow == nullptr) frame->optionsWindow = new TOptionsWindow(const_cast<RC::RCOptions&>(frame->options), frame->applicationDirectory, [frame](const RC::RCOptions& previous) { frame->applyOptions(previous); });
    frame->optionsWindow->setServerName(frame->serverName);
    frame->optionsWindow->open();
}

void TRemoteFrame::onAccountList(const char* accounts, void* data) { TRemoteFrame* frame = static_cast<TRemoteFrame*>(data); if (frame->accountsWindow != nullptr) frame->accountsWindow->setAccounts(accounts); }
void TRemoteFrame::onPlayerText(const char* type, const char* account, const char* content, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (type == nullptr || account == nullptr) return;
    if (std::string(type) == "account") {
        if (frame->accountsWindow == nullptr) frame->accountsWindow = new TAccountsWindow();
        frame->accountsWindow->setServerName(frame->serverName);
        frame->accountsWindow->showEditor(frame->connection, account, content);
        return;
    }
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    frame->playerList->setServerName(frame->serverName);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handlePlayerText(type, account, content);
}
void TRemoteFrame::onPlayerRights(const char* account, int rights, const char* ipRange, const char* folderAccess, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    frame->playerList->setServerName(frame->serverName);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handlePlayerRights(account, rights, ipRange, folderAccess);
}
void TRemoteFrame::onPlayerAttributes(const char* account, const char* properties, const char* editorText, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    frame->playerList->setServerName(frame->serverName);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handlePlayerAttributes(account, properties, editorText);
}
void TRemoteFrame::onBanData(const char* account, const char* computerId, const char* details, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    frame->playerList->setServerName(frame->serverName);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handleBanData(account, computerId, details);
}
void TRemoteFrame::onBanListData(const char* type, const char* account, const char* content, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    frame->playerList->setServerName(frame->serverName);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handleBanListData(type, account, content);
}

void TRemoteFrame::onFileBrowser(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr || (frame->syncManager != nullptr && frame->syncManager->isFileSyncActive())) return;
    if (frame->fileBrowser == nullptr) frame->fileBrowser = new TFileBrowserTree();
    frame->fileBrowser->setServerName(frame->serverName);
    frame->fileBrowser->setDownloadFolder(frame->options.downloadfolder);
    frame->fileBrowser->setDownloadServer(frame->serverName);
    frame->fileBrowser->setModernFileBrowser(frame->options.modernfilebrowser);
    frame->fileBrowser->setHoverPreviews(frame->options.filebrowserhoverpreview);
    frame->fileBrowser->setModernThumbnails(frame->options.filebrowserthumbnails);
    frame->fileBrowser->open(frame->connection);
}

void TRemoteFrame::onClasses(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr || rc_is_nc_authenticated(frame->connection) == 0) return;
    if (frame->classList == nullptr) frame->classList = new TScriptList("classes", &frame->options);
    frame->classList->setServerName(frame->serverName);
    frame->classList->open(frame->connection);
}

void TRemoteFrame::onWeapons(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr || rc_is_nc_authenticated(frame->connection) == 0) return;
    if (frame->weaponList == nullptr) frame->weaponList = new TScriptList("weapons", &frame->options);
    frame->weaponList->setServerName(frame->serverName);
    frame->weaponList->open(frame->connection);
}

void TRemoteFrame::onNPCs(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr || rc_is_nc_authenticated(frame->connection) == 0) return;
    if (frame->npcList == nullptr) frame->npcList = new TNPCList(frame->accountName);
    frame->npcList->setServerName(frame->serverName);
    frame->npcList->open(frame->connection);
}

void TRemoteFrame::onLevels(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    const int authenticated = frame->connection == nullptr ? 0 : rc_is_nc_authenticated(frame->connection);
    remoteControlDebugLog("level list requested: connection=%p authenticated=%d", frame->connection, authenticated);
    if (frame->connection == nullptr || authenticated == 0) return;
    if (frame->levelList == nullptr) frame->levelList = new TLevelList(GTK_WINDOW(frame->window));
    frame->levelList->setServerName(frame->serverName);
    frame->levelList->open(frame->connection);
}

void TRemoteFrame::onLocalNPCDump(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr || rc_is_nc_authenticated(frame->connection) == 0) return;
    rc_on_local_npcs(frame->connection, onLocalNPCData, frame);
    const std::string title = frame->serverName.empty() ? "Local NPCs" : "Local NPCs - " + frame->serverName;
    GtkWidget* dialog = gtk_dialog_new_with_buttons(title.c_str(), GTK_WINDOW(frame->window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "OK", GTK_RESPONSE_OK, nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_add(GTK_CONTAINER(content), grid);
    GtkWidget* label = gtk_label_new("Level:");
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), true);
    gtk_widget_set_hexpand(entry, true);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry, 1, 0, 1, 1);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    g_object_set_data(G_OBJECT(dialog), "level-entry", entry);
    g_signal_connect(dialog, "response", G_CALLBACK(onLocalNPCSubmit), frame);
    gtk_widget_show_all(dialog);
}

void TRemoteFrame::onLocalNPCSubmit(GtkDialog* dialog, gint response, gpointer data) {
    if (response != GTK_RESPONSE_OK) {
        gtk_widget_destroy(GTK_WIDGET(dialog));
        return;
    }
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    GtkWidget* entry = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(dialog), "level-entry"));
    const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
    gchar* level = g_ascii_strdown(text, -1);
    if (level[0] != '\0') rc_request_local_npcs(frame->connection, level);
    g_free(level);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void TRemoteFrame::onLocalNPCData(const char*, const char* content, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (content == nullptr || content[0] == '\0') return;
    const std::string title = frame->serverName.empty() ? "Local NPCs" : "Local NPCs - " + frame->serverName;
    GtkWidget* dialog = gtk_dialog_new_with_buttons(title.c_str(), GTK_WINDOW(frame->window), GTK_DIALOG_DESTROY_WITH_PARENT, "Close", GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 380);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkSourceLanguage* language = gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(), "ini");
    GtkSourceBuffer* buffer = language != nullptr ? gtk_source_buffer_new_with_language(language) : gtk_source_buffer_new(nullptr);
    applyRemoteControlSourceStyle(buffer);
    GtkWidget* text = gtk_source_view_new_with_buffer(buffer);
    configureGScriptEditor(text, false);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text), false);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text), true);
    gchar* validContent = g_utf8_make_valid(content, -1);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(buffer), validContent, -1);
    g_free(validContent);
    g_object_unref(buffer);
    gtk_container_add(GTK_CONTAINER(scrolled), text);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scrolled, true, true, 0);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* responseDialog, gint, gpointer) { gtk_widget_destroy(GTK_WIDGET(responseDialog)); }), nullptr);
    gtk_widget_show_all(dialog);
}

void TRemoteFrame::onServerOptions(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->serverOptionsEditor == nullptr) frame->serverOptionsEditor = new TServerTextEditor(TServerTextEditor::Kind::ServerOptions, "Server Options", &frame->options);
    frame->serverOptionsEditor->setServerName(frame->serverName);
    frame->serverOptionsEditor->open(frame->connection);
}

void TRemoteFrame::onListServerSettings(GtkButton*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->onListServerSettingsCallback) frame->onListServerSettingsCallback();
}

void TRemoteFrame::onServerFlags(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->serverFlagsEditor == nullptr) frame->serverFlagsEditor = new TServerTextEditor(TServerTextEditor::Kind::ServerFlags, "Server Flags", &frame->options);
    frame->serverFlagsEditor->setServerName(frame->serverName);
    frame->serverFlagsEditor->open(frame->connection);
}

void TRemoteFrame::onFolderConfig(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->folderConfigEditor == nullptr) frame->folderConfigEditor = new TServerTextEditor(TServerTextEditor::Kind::FolderConfig, "Folder Config", &frame->options);
    frame->folderConfigEditor->setServerName(frame->serverName);
    frame->folderConfigEditor->open(frame->connection);
}

gboolean TRemoteFrame::onGraphicalButton(GtkWidget* button, GdkEventButton* event, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    const int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "button-index"));
    remoteControlDebugLog("graphical button event: index=%d type=%d button=%u", index, event == nullptr ? -1 : event->type, event == nullptr ? 0 : event->button);
    if (event == nullptr || event->button != GDK_BUTTON_PRIMARY || index < 0 || index >= static_cast<int>(frame->graphicalButtons.size())) return false;
    std::string imageName = event->type == GDK_BUTTON_PRESS ? frame->options.buttonimagefilespressed[index] : frame->options.buttonimagefiles[index];
    if (event->type == GDK_BUTTON_PRESS && imageName == frame->options.buttonimagefiles[index]) {
        const std::size_t suffix = imageName.rfind("_normal");
        if (suffix != std::string::npos) {
            const std::string pressedName = imageName.substr(0, suffix) + "_pressed" + imageName.substr(suffix + 7);
            if (std::filesystem::exists(frame->applicationDirectory / "images" / pressedName)) imageName = pressedName;
        }
    }
    const std::filesystem::path imagePath = frame->applicationDirectory / "images" / imageName;
    GtkWidget* image = gtk_bin_get_child(GTK_BIN(button));
    gtk_image_set_from_file(GTK_IMAGE(image), imagePath.string().c_str());
    if (event->type == GDK_BUTTON_RELEASE) {
        remoteControlDebugLog("graphical button release: index=%d", index);
        frame->graphicalAction(index);
    }
    return true;
}

gboolean TRemoteFrame::onEditKey(GtkWidget*, GdkEventKey* event, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (event->keyval == GDK_KEY_F8) {
        if (frame->onListServerCallback) frame->onListServerCallback();
        return true;
    }
    if (event->keyval == GDK_KEY_Up && !frame->chatHistory.empty()) {
        frame->chatHistoryIndex = std::min(frame->chatHistoryIndex + 1, static_cast<int>(frame->chatHistory.size()) - 1);
        gtk_entry_set_text(GTK_ENTRY(frame->editField), frame->chatHistory[frame->chatHistoryIndex].c_str());
        return true;
    }
    if (event->keyval == GDK_KEY_Down && frame->chatHistoryIndex >= 0) {
        --frame->chatHistoryIndex;
        gtk_entry_set_text(GTK_ENTRY(frame->editField), frame->chatHistoryIndex < 0 ? "" : frame->chatHistory[frame->chatHistoryIndex].c_str());
        return true;
    }
    if (event->keyval != GDK_KEY_Return && event->keyval != GDK_KEY_KP_Enter) return false;
    frame->send();
    return true;
}

void TRemoteFrame::onExtensions(GtkMenuItem*, gpointer data) { TRemoteFrame* frame = static_cast<TRemoteFrame*>(data); if (frame->extensionsManager != nullptr) frame->extensionsManager->showWindow(); }

void TRemoteFrame::onMentionChanged(GtkEditable*, gpointer data) {
    static_cast<TRemoteFrame*>(data)->refreshMentionCompletion();
}

gboolean TRemoteFrame::onMentionMatch(GtkEntryCompletion*, const gchar*, GtkTreeIter* row, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    const gchar* text = gtk_entry_get_text(GTK_ENTRY(frame->editField));
    const gint cursor = gtk_editable_get_position(GTK_EDITABLE(frame->editField));
    if (text == nullptr || cursor <= 0) return false;
    gint at = cursor - 1;
    while (at >= 0 && !g_ascii_isspace(text[at])) --at;
    ++at;
    if (at >= cursor) return false;
    const std::string token(text + at, text + cursor);
    const bool mention = token[0] == '@';
    const bool directCommand = token[0] == '/';
    gint commandEnd = at - 1;
    while (commandEnd >= 0 && g_ascii_isspace(text[commandEnd])) --commandEnd;
    gint commandStart = commandEnd;
    while (commandStart >= 0 && !g_ascii_isspace(text[commandStart])) --commandStart;
    ++commandStart;
    const bool commandArgument = !mention && !directCommand && commandStart <= commandEnd && text[commandStart] == '/';
    const bool command = directCommand || commandArgument;
    if (!mention && !command) return false;
    const std::size_t argumentStart = directCommand ? token.find_last_of(" \t") : std::string::npos;
    const std::string queryText = commandArgument ? token : (argumentStart == std::string::npos ? token.substr(mention ? 1 : 0) : token.substr(argumentStart + 1));
    gchar* foldedQueryText = g_utf8_casefold(queryText.c_str(), -1);
    gchar* account = nullptr;
    gchar* nick = nullptr;
    gchar* insertion = nullptr;
    gchar* community = nullptr;
    gtk_tree_model_get(GTK_TREE_MODEL(frame->mentionStore), row, 1, &account, 2, &nick, 3, &insertion, 4, &community, -1);
    const bool commandRow = insertion != nullptr && insertion[0] == '/';
    if ((mention && commandRow) || (command && !commandArgument && argumentStart == std::string::npos && !commandRow)) { g_free(account); g_free(nick); g_free(insertion); g_free(community); g_free(foldedQueryText); return false; }
    gchar* foldedAccount = g_utf8_casefold(account == nullptr ? "" : account, -1);
    gchar* foldedNick = g_utf8_casefold(nick == nullptr ? "" : nick, -1);
    gchar* foldedCommunity = g_utf8_casefold(community == nullptr ? "" : community, -1);
    const std::string query = foldedQueryText == nullptr ? "" : foldedQueryText;
    const bool matches = commandRow ? std::string(insertion).rfind(query, 0) == 0 : std::string(foldedAccount == nullptr ? "" : foldedAccount).rfind(query, 0) == 0 || std::string(foldedNick == nullptr ? "" : foldedNick).rfind(query, 0) == 0 || std::string(foldedCommunity == nullptr ? "" : foldedCommunity).rfind(query, 0) == 0;
    g_free(foldedQueryText);
    g_free(account);
    g_free(nick);
    g_free(insertion);
    g_free(community);
    g_free(foldedAccount);
    g_free(foldedNick);
    g_free(foldedCommunity);
    return matches;
}

gboolean TRemoteFrame::onMentionSelected(GtkEntryCompletion*, GtkTreeModel* model, GtkTreeIter* row, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    gchar* account = nullptr;
    gchar* insertion = nullptr;
    gtk_tree_model_get(model, row, 1, &account, 3, &insertion, -1);
    const std::string accountText = account == nullptr ? "" : account;
    const std::string insertionText = insertion == nullptr ? "" : insertion;
    g_free(account);
    g_free(insertion);
    if (accountText.empty() && insertionText.empty()) return true;
    const std::string text = gtk_entry_get_text(GTK_ENTRY(frame->editField));
    const gint cursor = gtk_editable_get_position(GTK_EDITABLE(frame->editField));
    gint at = cursor - 1;
    while (at >= 0 && !g_ascii_isspace(text[at])) --at;
    ++at;
    if (at < cursor && text[at] == '@') {
        const std::string replacement = text.substr(0, at) + "@" + accountText + " " + text.substr(cursor);
        gtk_entry_set_text(GTK_ENTRY(frame->editField), replacement.c_str());
        gtk_editable_set_position(GTK_EDITABLE(frame->editField), at + static_cast<gint>(accountText.size()) + 2);
    } else if (at < cursor && (text[at] == '/' || [&text, at]() { gint commandEnd = at - 1; while (commandEnd >= 0 && g_ascii_isspace(text[commandEnd])) --commandEnd; gint commandStart = commandEnd; while (commandStart >= 0 && !g_ascii_isspace(text[commandStart])) --commandStart; ++commandStart; return commandStart <= commandEnd && text[commandStart] == '/'; }())) {
        const std::size_t argument = text.find_last_of(" \t", cursor - 1);
        const std::size_t start = argument == std::string::npos ? static_cast<std::size_t>(at) : argument + 1;
        const std::string value = insertionText.rfind("/", 0) == 0 ? insertionText + " " : accountText;
        const std::string replacement = text.substr(0, start) + value + text.substr(cursor);
        gtk_entry_set_text(GTK_ENTRY(frame->editField), replacement.c_str());
        gtk_editable_set_position(GTK_EDITABLE(frame->editField), static_cast<gint>(start + value.size()));
    }
    return true;
}

gboolean TRemoteFrame::onWindowKey(GtkWidget*, GdkEventKey* event, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (event->keyval == GDK_KEY_F8) {
        if (frame->onListServerCallback) frame->onListServerCallback();
        return true;
    }
    if ((event->state & GDK_CONTROL_MASK) == 0 || (event->keyval != GDK_KEY_f && event->keyval != GDK_KEY_F)) return false;
    GtkWidget* page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(frame->notebook), gtk_notebook_get_current_page(GTK_NOTEBOOK(frame->notebook)));
    GtkWidget* field = page == nullptr ? nullptr : gtk_bin_get_child(GTK_BIN(page));
    if (field == nullptr || !GTK_IS_TEXT_VIEW(field)) return false;
    openEditorFind(field);
    return true;
}

gboolean TRemoteFrame::onDelete(GtkWidget*, GdkEvent*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    gtk_widget_hide(frame->window);
    return true;
}

void TRemoteFrame::onGraphicalAllocate(GtkWidget*, GdkRectangle* allocation, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (allocation->width <= 0 || allocation->height <= 0) return;
    frame->graphicalBackgroundWidth = allocation->width;
    gtk_widget_queue_draw(frame->backgroundImage);
    frame->repositionGraphicalButtons(allocation->width);
}

void TRemoteFrame::repositionGraphicalButtons(int requestedWidth) {
    if (graphicalFixed == nullptr) return;
    GtkAllocation allocation;
    gtk_widget_get_allocation(graphicalFixed, &allocation);
    int width = requestedWidth > 0 ? requestedWidth : (allocation.width > 0 ? allocation.width : graphicalBackgroundWidth);
    const int containerWidth = graphicalContainer == nullptr ? 0 : gtk_widget_get_allocated_width(graphicalContainer);
    const int windowWidth = window == nullptr ? 0 : gtk_widget_get_allocated_width(window);
    if (containerWidth > 0) width = std::min(width, containerWidth);
    if (windowWidth > 0) width = std::min(width, windowWidth);
    if (width <= 0) return;
    const int labelWidth = std::min(500, width);
    const int labelX = std::max(0, (width - labelWidth) / 2);
    if (npcServerLabel != nullptr) {
        gtk_widget_set_size_request(npcServerLabel, labelWidth, -1);
        gtk_fixed_move(GTK_FIXED(graphicalFixed), npcServerLabel, labelX, 130);
        const int offsets[][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
        for (int index = 0; index < static_cast<int>(npcServerLabelShadows.size()); ++index) {
            GtkWidget* shadow = npcServerLabelShadows[index];
            if (shadow == nullptr) continue;
            gtk_widget_set_size_request(shadow, labelWidth, -1);
            gtk_fixed_move(GTK_FIXED(graphicalFixed), shadow, labelX + offsets[index][0], 130 + offsets[index][1]);
        }
    }
    const int positions[15][2] = {{5, 15}, {5, 48}, {38, 15}, {71, 15}, {394, 15}, {427, 15}, {460, 15}, {460, 48}, {460, 81}, {427, 114}, {394, 114}, {360, 114}, {104, 15}, {460, 114}, {361, 15}};
    for (int index = 4; index < 15; ++index) if (index != 12 && graphicalButtons[index] != nullptr) gtk_fixed_move(GTK_FIXED(graphicalFixed), graphicalButtons[index], std::clamp(width - (500 - positions[index][0]), 0, std::max(0, width - 32)), positions[index][1]);
}

gboolean TRemoteFrame::processEvents(gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    const bool fileBrowserAvailable = frame->syncManager == nullptr || !frame->syncManager->isFileSyncActive();
    if (frame->graphicalButtons[1] != nullptr) gtk_widget_set_sensitive(frame->graphicalButtons[1], fileBrowserAvailable);
    if (!fileBrowserAvailable && frame->fileBrowser != nullptr) frame->fileBrowser->hide();
    if (frame->connection != nullptr) {
        rc_process_events(frame->connection);
        GList* toplevels = gtk_window_list_toplevels();
        for (GList* current = toplevels; current != nullptr; current = current->next) {
            GtkWidget* candidate = GTK_WIDGET(current->data);
            if (candidate == frame->window || !GTK_IS_WINDOW(candidate)) continue;
            if (g_object_get_data(G_OBJECT(candidate), "rc-afk-frame") != frame) {
                g_object_set_data(G_OBJECT(candidate), "rc-afk-frame", frame);
                gtk_widget_add_events(candidate, GDK_KEY_PRESS_MASK | GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK);
                g_signal_connect(candidate, "key-press-event", G_CALLBACK(onActivityEvent), frame);
                g_signal_connect(candidate, "button-press-event", G_CALLBACK(onActivityEvent), frame);
                g_signal_connect(candidate, "motion-notify-event", G_CALLBACK(onActivityEvent), frame);
            }
        }
        g_list_free(toplevels);
        std::function<void(GtkWidget*)> watchActivity = [&](GtkWidget* widget) {
            if (g_object_get_data(G_OBJECT(widget), "rc-afk-widget") == nullptr) {
                g_object_set_data(G_OBJECT(widget), "rc-afk-widget", frame);
                gtk_widget_add_events(widget, GDK_KEY_PRESS_MASK | GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK);
                g_signal_connect(widget, "key-press-event", G_CALLBACK(onActivityEvent), frame);
                g_signal_connect(widget, "button-press-event", G_CALLBACK(onActivityEvent), frame);
                g_signal_connect(widget, "motion-notify-event", G_CALLBACK(onActivityEvent), frame);
            }
            if (!GTK_IS_CONTAINER(widget)) return;
            GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
            for (GList* child = children; child != nullptr; child = child->next) watchActivity(GTK_WIDGET(child->data));
            g_list_free(children);
        };
        watchActivity(frame->window);
        if (frame->options.afkenabled && !frame->awayNicknameApplied && frame->lastActivity > 0 && g_get_monotonic_time() - frame->lastActivity >= static_cast<gint64>(std::max(1, frame->options.afktimeout)) * 60 * G_USEC_PER_SEC) {
            std::string lower = frame->baseNickname;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if (!frame->baseNickname.empty() && lower.find("away") == std::string::npos && lower.find("dnd") == std::string::npos && lower.find("do not disturb") == std::string::npos && lower.find("busy") == std::string::npos) {
                frame->nickname = frame->baseNickname + " - Away";
                rc_set_nickname(frame->connection, frame->nickname.c_str());
                frame->awayNicknameApplied = true;
                if (frame->playerList != nullptr) frame->playerList->setAwayStatus(true); else { const char payload[] = {'U', static_cast<char>(1 + 32)}; rc_send_raw_packet(frame->connection, PLI_PLAYERPROPS, payload, sizeof(payload)); }
                frame->awayStatusApplied = true;
            }
        }
        if (!frame->ncManuallyDisconnected && !frame->ncConnectionAttempted && rc_has_nc_server(frame->connection) != 0 && rc_is_nc_connected(frame->connection) == 0) {
            frame->ncConnectionAttempted = true;
            rc_connect_to_nc_server(frame->connection);
        }
        frame->updateNCUi(!frame->ncManuallyDisconnected && rc_is_nc_authenticated(frame->connection) != 0);
        if (frame->playersLabel != nullptr) {
            RCPlayer* players = nullptr;
            const int count = rc_get_players(frame->connection, &players);
            const std::string playerText = frame->options.labelplayers.empty() ? std::to_string(count) : frame->options.labelplayers + " " + std::to_string(count);
            gtk_label_set_text(GTK_LABEL(frame->playersLabel), playerText.c_str());
            for (GtkWidget* shadow : frame->playersLabelShadows) if (shadow != nullptr) gtk_label_set_text(GTK_LABEL(shadow), playerText.c_str());
            const std::string playerLabel = frame->options.labelplayers.empty() ? "Players:" : frame->options.labelplayers;
            gtk_window_set_title(GTK_WINDOW(frame->window), remoteControlTitle(frame->serverName, playerLabel + " " + std::to_string(count), frame->syncInProgress ? frame->syncProgress : -1).c_str());
            if (count != frame->trayPlayerCount) {
                frame->trayPlayerCount = count;
                remote_control_set_tray_label(frame->serverName.c_str(), count);
            }
        }
    }
    return G_SOURCE_CONTINUE;
}

void TRemoteFrame::onConnected(void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    remoteControlDebugLog("connected to %s", frame->accountName.c_str());
    if (!frame->nickname.empty()) rc_set_nickname(frame->connection, frame->nickname.c_str());
    frame->updateMassPMAcceptance();
    rc_execute(frame->connection, (std::string("/npc newrc,") + REMOTE_CONTROL_BUILD_DATE).c_str());
}

void TRemoteFrame::onDisconnected(const char* reason, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    remote_control_set_tray_label(nullptr, 0);
    if (frame->suppressReconnectDisconnect) {
        frame->suppressReconnectDisconnect = false;
        return;
    }
    if (frame->disconnectHandled) return;
    remoteControlDebugLog("connection disconnected: %s", reason == nullptr ? "You have been disconnected!" : reason);
    frame->disconnectHandled = true;
    const std::vector<std::string> unsaved = unsavedScriptEditors();
    if (!unsaved.empty()) {
        std::string message = "The following scripts have unsaved changes and remain open:\n";
        for (const std::string& name : unsaved) message += "\n" + name;
        GtkWidget* warning = gtk_message_dialog_new(GTK_WINDOW(frame->window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "%s", message.c_str());
        gtk_dialog_run(GTK_DIALOG(warning));
        gtk_widget_destroy(warning);
    }
    gtk_widget_hide(frame->window);
    frame->onCloseCallback();
        createErrorWindow("Connection Error", reason == nullptr ? "You have been disconnected!" : reason, GTK_WINDOW(frame->window));
}

void TRemoteFrame::onMessage(const char* message, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    const std::string value = message == nullptr ? "" : message;
    if (frame->options.separatefindresults && frame->appendFindResult(value)) return;
    frame->appendChat(value);
}

gboolean TRemoteFrame::onActivityEvent(GtkWidget*, GdkEvent*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    frame->lastActivity = g_get_monotonic_time();
    if (frame->awayNicknameApplied && frame->connection != nullptr) {
        frame->nickname = frame->baseNickname;
        rc_set_nickname(frame->connection, frame->nickname.c_str());
        frame->awayNicknameApplied = false;
        if (frame->awayStatusApplied) {
            if (frame->playerList != nullptr) frame->playerList->setAwayStatus(false); else { const char payload[] = {'U', static_cast<char>(32)}; rc_send_raw_packet(frame->connection, PLI_PLAYERPROPS, payload, sizeof(payload)); }
            frame->awayStatusApplied = false;
        }
    }
    return false;
}

bool TRemoteFrame::mcpOpenView(const std::string& view, std::string& error) {
    if (view == "player_list") onPlayerList(nullptr, this);
    else if (view == "file_browser") onFileBrowser(nullptr, this);
    else if (view == "classes") onClasses(nullptr, this);
    else if (view == "weapons") onWeapons(nullptr, this);
    else if (view == "npcs") onNPCs(nullptr, this);
    else if (view == "server_options") onServerOptions(nullptr, this);
    else if (view == "server_flags") onServerFlags(nullptr, this);
    else if (view == "folder_config") onFolderConfig(nullptr, this);
    else if (view == "toalls") onToalls(nullptr, this);
    else if (view == "accounts") onAccounts(nullptr, this);
    else if (view == "options") onRCOptions(nullptr, this);
    else { error = "Unsupported managed RC view"; return false; }
    return true;
}

bool TRemoteFrame::mcpSendChat(const std::string& text, std::string& error) {
    if (!isConnected()) { error = "RC is not connected"; return false; }
    if (text.empty()) { error = "Chat text is empty"; return false; }
    gtk_entry_set_text(GTK_ENTRY(editField), text.c_str());
    send();
    return true;
}

void TRemoteFrame::onIrcMessage(const char* channel, const char* line, void* data) {
    static_cast<TRemoteFrame*>(data)->appendChannelMessage(channel == nullptr ? "" : channel, line == nullptr ? "" : line);
}

void TRemoteFrame::onPrivateMessage(int playerId, const char* account, const char* nick, const char* message, const char* type, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    const std::string messageType = type == nullptr ? "normal" : type;
    if (messageType == "mass" && frame->options.nomassmessages) return;
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    frame->playerList->setServerName(frame->serverName);
    const std::string display = frame->playerList->notePrivateMessage(playerId, account, nick, message, type);
    if (frame->options.newpmalerts) {
        frame->appendChat("#ALERT New PM from " + display, true);
    }
    remote_control_begin_pm_tray_alert();
}

void TRemoteFrame::onPlayerPropertiesChanged(int playerId, const char* properties, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    frame->playerList->setServerName(frame->serverName);
    frame->playerList->setPlayerProperties(playerId, properties);
    frame->updateMassPMAcceptance();
    frame->refreshMentionCompletion();
}

void TRemoteFrame::onPlayerPropChanged(int playerId, const char* property, const char* value, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    updateGScriptEditorPlayerProperty(frame->connection, playerId, property, value);
    if (property != nullptr && g_ascii_strcasecmp(property, "community") == 0) {
        frame->playerCommunityNames[playerId] = value == nullptr ? "" : value;
        frame->refreshMentionCompletion();
    }
}

void TRemoteFrame::onRawPacket(int packetId, const char* data, int length, void*) {
    remoteControlPacketLogMessage(packetId, data, length);
}

void TRemoteFrame::onServerData(const char* type, const char* content, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    const std::string value = content == nullptr ? "" : content;
    if (type != nullptr && std::string(type) == "statuslist") {
        if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
        frame->playerList->setServerName(frame->serverName);
        frame->playerList->setConnection(frame->connection);
        frame->playerList->setStatusList(value.c_str());
    }
    else if (type != nullptr && std::string(type) == "toall" && frame->toallsWindow != nullptr) frame->toallsWindow->append(value.c_str());
    else if (type != nullptr && std::string(type) == "server_text") frame->appendChat(value);
    else if (type != nullptr && std::string(type) == "nc_message") {
        if (frame->options.separatenc) frame->appendChannelMessage("NC", value);
        else frame->appendChat(value);
    }
    else if (type != nullptr && std::string(type) == "options" && frame->serverOptionsEditor != nullptr) frame->serverOptionsEditor->setContent(value.c_str());
    else if (type != nullptr && std::string(type) == "flags" && frame->serverFlagsEditor != nullptr) frame->serverFlagsEditor->setContent(value.c_str());
    else if (type != nullptr && std::string(type) == "folder_config" && frame->folderConfigEditor != nullptr) frame->folderConfigEditor->setContent(value.c_str());
    else if (type != nullptr && std::string(type) == "nc_levellist" && frame->levelList != nullptr) frame->levelList->setContent(value.c_str());
}

gboolean TRemoteFrame::scrollChatToBottom(gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(frame->chatScrolled));
    gtk_adjustment_set_value(adjustment, gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment));
    return G_SOURCE_REMOVE;
}

void TRemoteFrame::addMenuItem(GtkWidget* menu, const char* label, GCallback callback) {
    GtkWidget* item = gtk_menu_item_new_with_label(label);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    if (callback != nullptr) g_signal_connect(item, "activate", callback, this);
}

void TRemoteFrame::graphicalAction(int index) {
    remoteControlDebugLog("graphical action: index=%d", index);
    if (index == 0) onPlayerList(nullptr, this);
    else if (index == 1) onFileBrowser(nullptr, this);
    else if (index == 2) onAccounts(nullptr, this);
    else if (index == 3) onToalls(nullptr, this);
    else if (index == 4) onRCOptions(nullptr, this);
    else if (index == 9) onClasses(nullptr, this);
    else if (index == 10) onWeapons(nullptr, this);
    else if (index == 11) onNPCs(nullptr, this);
    else if (index == 8) onLocalNPCDump(nullptr, this);
    else if (index == 5) onServerFlags(nullptr, this);
    else if (index == 6) onFolderConfig(nullptr, this);
    else if (index == 7) onServerOptions(nullptr, this);
    else if (index == 12) {
        if (extensionsManager != nullptr) extensionsManager->showWindow();
    }
    else if (index == 13) onLevels(nullptr, this);
    else if (index == 14) {
        remoteControlDebugLog("sync window requested: manager=%p", syncManager.get());
        if (syncManager != nullptr) syncManager->showWindow();
    }
}

void TRemoteFrame::appendChat(const std::string& message, bool suppressUrgency, bool suppressEmotes) {
    const bool colorAlert = message.rfind("#ALERT", 0) == 0;
    std::string display = message;
    const bool alert = applyAlertTag(display, !suppressUrgency);
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chatField));
    ChatTags& tags = chatTagsFor(buffer);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    const std::string timestamp = chatTimestamp(options);
    if (!timestamp.empty()) {
        gtk_text_buffer_insert_with_tags(buffer, &end, (timestamp + " ").c_str(), -1, tags.timestamp, nullptr);
        gtk_text_buffer_get_end_iter(buffer, &end);
    }
    const gint startOffset = gtk_text_iter_get_offset(&end);
    if (colorAlert) {
        gtk_text_buffer_insert_with_tags(buffer, &end, (display + "\n").c_str(), -1, tags.alert, nullptr);
    } else {
        const std::size_t separator = display.find(':');
        if (separator != std::string::npos && separator > 0) {
            const std::string prefix = display.substr(0, separator + 1);
            gtk_text_buffer_insert_with_tags(buffer, &end, prefix.c_str(), -1, tags.bold, nullptr);
            GtkTextIter textStart;
            gtk_text_buffer_get_end_iter(buffer, &textStart);
            const gint textStartOffset = gtk_text_iter_get_offset(&textStart);
            gtk_text_buffer_get_end_iter(buffer, &end);
            gtk_text_buffer_insert(buffer, &end, (display.substr(separator + 1) + "\n").c_str(), -1);
            if (!suppressEmotes) applyEmotes(buffer, textStartOffset, display.substr(separator + 1));
        } else {
            gtk_text_buffer_insert(buffer, &end, (display + "\n").c_str(), -1);
            if (!suppressEmotes) applyEmotes(buffer, startOffset, display);
        }
    }
    GtkTextIter linkEnd;
    gtk_text_buffer_get_end_iter(buffer, &linkEnd);
    applyChatUrls(buffer, startOffset, gtk_text_iter_get_offset(&linkEnd));
    if (alert && !hasActiveRemoteControlWindow()) {
        gtk_window_set_urgency_hint(GTK_WINDOW(window), true);
        if (options.notificationsounds) gdk_beep();
    }
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(chatField), &end, 0.0, false, 0.0, 1.0);
    g_idle_add(scrollChatToBottom, this);
    appendChatLog(message);
}

void TRemoteFrame::appendChatLog(const std::string& message) const {
    if (!options.logrcchat || options.chatlogfile.empty()) return;
    const std::filesystem::path path = options.chatlogfile;
    std::error_code error;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::app | std::ios::binary);
    if (!stream) return;
    const std::string timestamp = chatTimestamp(options);
    if (!timestamp.empty()) stream << timestamp << ' ';
    stream << message << '\n';
}

void TRemoteFrame::applyOptions(const RC::RCOptions& previous) {
    updateMcpGuiBridgeOptions(options);
    nickname = options.nickname;
    baseNickname = nickname;
    if (!options.afkenabled && awayNicknameApplied && connection != nullptr) { nickname = baseNickname; rc_set_nickname(connection, nickname.c_str()); awayNicknameApplied = false; if (awayStatusApplied) { if (playerList != nullptr) playerList->setAwayStatus(false); else { const char payload[] = {'U', static_cast<char>(32)}; rc_send_raw_packet(connection, PLI_PLAYERPROPS, payload, sizeof(payload)); } awayStatusApplied = false; } }
    if (connection != nullptr && nickname != previous.nickname) rc_set_nickname(connection, nickname.c_str());
    if (connection != nullptr && (options.nomassmessages != previous.nomassmessages || options.nomassifclienton != previous.nomassifclienton)) updateMassPMAcceptance();
    if (connection != nullptr && (options.globalpms != previous.globalpms || options.buddytracking != previous.buddytracking || options.showbuddies != previous.showbuddies)) sendServerListOptions();
    if (playerList != nullptr && options.attachaway != previous.attachaway) playerList->setAttachAway(options.attachaway);
    if (options.separatenc != previous.separatenc) setNCChannelVisible(options.separatenc);
    if (fileBrowser != nullptr && options.downloadfolder != previous.downloadfolder) fileBrowser->setDownloadFolder(options.downloadfolder);
    if (fileBrowser != nullptr && options.modernfilebrowser != previous.modernfilebrowser) fileBrowser->setModernFileBrowser(options.modernfilebrowser);
    if (fileBrowser != nullptr && options.filebrowserhoverpreview != previous.filebrowserhoverpreview) fileBrowser->setHoverPreviews(options.filebrowserhoverpreview);
    if (fileBrowser != nullptr && options.filebrowserthumbnails != previous.filebrowserthumbnails) fileBrowser->setModernThumbnails(options.filebrowserthumbnails);
    if (options.background != previous.background) reloadBackground();
    if (options.backgroundtint != previous.backgroundtint && backgroundImage != nullptr) gtk_widget_queue_draw(backgroundImage);
    refreshTheme();
    if (options.chatfontfamily != previous.chatfontfamily || options.chatfontsize != previous.chatfontsize) for (const auto& entry : channelFields) configureChatField(entry.second);
    for (const auto& entry : chatTags) {
        g_object_set(entry.second.alert, "foreground", options.coloralert.c_str(), nullptr);
        g_object_set(entry.second.bold, "foreground", options.colorchatbold.c_str(), nullptr);
    }
    if (serverLabel != nullptr && (options.labelservers != previous.labelservers || options.colorlabel != previous.colorlabel || options.colorlabelback != previous.colorlabelback)) {
        const std::string text = options.labelservers.empty() ? serverName : options.labelservers + " " + serverName;
        GdkColor foreground;
        GdkColor background;
        gdk_color_parse(options.colorlabel.c_str(), &foreground);
        gdk_color_parse(options.colorlabelback.c_str(), &background);
        gtk_label_set_text(GTK_LABEL(serverLabel), text.c_str());
        gtk_widget_modify_fg(serverLabel, GTK_STATE_NORMAL, &foreground);
        for (GtkWidget* shadow : serverLabelShadows) if (shadow != nullptr) { gtk_label_set_text(GTK_LABEL(shadow), text.c_str()); gtk_widget_modify_fg(shadow, GTK_STATE_NORMAL, &background); }
    }
    if (playersLabel != nullptr && (options.labelplayers != previous.labelplayers || options.colorlabel != previous.colorlabel || options.colorlabelback != previous.colorlabelback)) {
        RCPlayer* players = nullptr;
        const int count = connection == nullptr ? 0 : rc_get_players(connection, &players);
        const std::string text = options.labelplayers.empty() ? std::to_string(count) : options.labelplayers + " " + std::to_string(count);
        GdkColor foreground;
        GdkColor background;
        gdk_color_parse(options.colorlabel.c_str(), &foreground);
        gdk_color_parse(options.colorlabelback.c_str(), &background);
        gtk_label_set_text(GTK_LABEL(playersLabel), text.c_str());
        gtk_widget_modify_fg(playersLabel, GTK_STATE_NORMAL, &foreground);
        for (GtkWidget* shadow : playersLabelShadows) if (shadow != nullptr) { gtk_label_set_text(GTK_LABEL(shadow), text.c_str()); gtk_widget_modify_fg(shadow, GTK_STATE_NORMAL, &background); }
    }
    if (npcServerLabel != nullptr && (options.labelnpcserver != previous.labelnpcserver || options.colorlabel != previous.colorlabel || options.colorlabelback != previous.colorlabelback)) {
        GdkColor foreground;
        GdkColor background;
        gdk_color_parse(options.colorlabel.c_str(), &foreground);
        gdk_color_parse(options.colorlabelback.c_str(), &background);
        gtk_label_set_text(GTK_LABEL(npcServerLabel), options.labelnpcserver.c_str());
        gtk_widget_modify_fg(npcServerLabel, GTK_STATE_NORMAL, &foreground);
        for (GtkWidget* shadow : npcServerLabelShadows) if (shadow != nullptr) { gtk_label_set_text(GTK_LABEL(shadow), options.labelnpcserver.c_str()); gtk_widget_modify_fg(shadow, GTK_STATE_NORMAL, &background); }
    }
}

void TRemoteFrame::reloadBackground() {
    if (backgroundImage == nullptr) return;
    if (backgroundAnimationSource != 0) { g_source_remove(backgroundAnimationSource); backgroundAnimationSource = 0; }
    if (backgroundPixbuf != nullptr) { g_object_unref(backgroundPixbuf); backgroundPixbuf = nullptr; }
    if (backgroundAnimationIter != nullptr) { g_object_unref(backgroundAnimationIter); backgroundAnimationIter = nullptr; }
    if (backgroundAnimation != nullptr) { g_object_unref(backgroundAnimation); backgroundAnimation = nullptr; }
    backgroundWebPAnimation.reset();
    const std::filesystem::path configuredBackground(options.background);
    const std::filesystem::path background = configuredBackground.is_absolute() ? configuredBackground : applicationDirectory / "images" / configuredBackground;
    std::string extension = background.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension == ".webp") backgroundWebPAnimation = loadWebPAnimation(background);
    if (backgroundWebPAnimation == nullptr) {
        GError* imageError = nullptr;
        backgroundAnimation = gdk_pixbuf_animation_new_from_file(background.string().c_str(), &imageError);
        if (backgroundAnimation != nullptr && gdk_pixbuf_animation_is_static_image(backgroundAnimation)) {
            backgroundPixbuf = gdk_pixbuf_animation_get_static_image(backgroundAnimation);
            g_object_ref(backgroundPixbuf);
            g_object_unref(backgroundAnimation);
            backgroundAnimation = nullptr;
        } else if (backgroundAnimation != nullptr) {
            GTimeVal now;
            g_get_current_time(&now);
            backgroundAnimationIter = gdk_pixbuf_animation_get_iter(backgroundAnimation, &now);
        }
        if (imageError != nullptr) g_error_free(imageError);
    }
    if (backgroundAnimationIter != nullptr || backgroundWebPAnimation != nullptr) backgroundAnimationSource = g_timeout_add(16, +[](gpointer data) -> gboolean {
        TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
        if (frame->backgroundImage == nullptr) return G_SOURCE_REMOVE;
        if (frame->backgroundWebPAnimation != nullptr) {
            if (frame->backgroundWebPAnimation->advance(g_get_monotonic_time())) gtk_widget_queue_draw(frame->backgroundImage);
            return G_SOURCE_CONTINUE;
        }
        if (frame->backgroundAnimationIter == nullptr) return G_SOURCE_REMOVE;
        GTimeVal now;
        g_get_current_time(&now);
        if (gdk_pixbuf_animation_iter_advance(frame->backgroundAnimationIter, &now)) gtk_widget_queue_draw(frame->backgroundImage);
        return G_SOURCE_CONTINUE;
    }, this);
    gtk_widget_queue_draw(backgroundImage);
}

void TRemoteFrame::refreshTheme() {
    refreshNotebookTheme();
    if (chatField != nullptr) configureChatField(chatField);
    for (const auto& entry : channelFields) if (entry.second != nullptr) configureChatField(entry.second);
    refreshGScriptEditorTheme();
}
void TRemoteFrame::updateThemeOptions(const RC::RCOptions& nextOptions) {
    options = nextOptions;
    refreshTheme();
}

void TRemoteFrame::refreshNotebookTheme() {
    GdkScreen* screen = gdk_screen_get_default();
    if (notebookTabProvider != nullptr) { gtk_style_context_remove_provider_for_screen(screen, GTK_STYLE_PROVIDER(notebookTabProvider)); g_object_unref(notebookTabProvider); notebookTabProvider = nullptr; }
    if (screen == nullptr || notebook == nullptr) return;
    notebookTabProvider = gtk_css_provider_new();
    const std::string tabBackground = options.darkmode ? "#3d3d3d" : "#f5f5f5";
    const std::string tabBorder = options.darkmode ? "#707070" : "#c4c4c4";
    const std::string activeTabBackground = options.darkmode ? "#454545" : "#ffffff";
    const std::string activeTabBorder = options.darkmode ? "#909090" : "#9a9a9a";
    const std::string notebookCss = "#RemoteFrame notebook, #RemoteFrame notebook > header, #RemoteFrame notebook > header.top, #RemoteFrame notebook > header.top > tabs { margin: 0; padding: 0; border: 0; background-color: transparent; background-image: none; box-shadow: none; } #RemoteFrame notebook > header.top > tabs { padding-left: 6px; } #RemoteFrame notebook > header, #RemoteFrame notebook > header.top, #RemoteFrame notebook > header.top > tabs { min-height: 0; } #RemoteFrame notebook > stack, #RemoteFrame notebook > stack > scrolledwindow, #RemoteFrame notebook > stack > scrolledwindow > viewport { margin: 0; padding: 0; border: 0; background-color: " + options.colorchatback + "; } #RemoteFrame notebook > header.top > tabs > tab { min-height: 0; min-width: 0; margin: 0 1px 0 0; padding: 3px 7px; background-image: none; background-color: " + tabBackground + "; border: 1px solid " + tabBorder + "; border-bottom: none; border-radius: 3px 3px 0 0; } #RemoteFrame notebook > header.top > tabs > tab:checked { background-color: " + activeTabBackground + "; border-color: " + activeTabBorder + "; } #RemoteFrame notebook > header.top > tabs > tab label { min-width: 0; margin: 0; padding: 0; font-size: 12px; }";
    gtk_css_provider_load_from_data(notebookTabProvider, notebookCss.c_str(), -1, nullptr);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(notebookTabProvider), GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
}

void TRemoteFrame::sendServerListOptions() {
    const std::string payload = "GraalEngine,lister,options,globalpms=" + std::string(options.globalpms ? "true" : "false") + ",buddytracking=" + std::string(options.buddytracking ? "true" : "false") + ",showbuddies=" + std::string(options.showbuddies ? "true" : "false");
    rc_send_raw_packet(connection, PLI_SENDTEXT, payload.c_str(), static_cast<int>(payload.size()));
}

void TRemoteFrame::updateMassPMAcceptance() {
    if (connection == nullptr) return;
    const std::optional<bool> connected = playerList == nullptr ? std::nullopt : playerList->localAccountConnected();
    const bool reject = options.nomassmessages || (options.nomassifclienton && connected.has_value() && !*connected);
    const char state[] = {'A', reject ? '!' : ' '};
    rc_send_raw_packet(connection, PLI_PLAYERPROPS, state, sizeof(state));
}

void TRemoteFrame::setNCChannelVisible(bool visible) {
    const auto found = channelFields.find("NC");
    if (!visible) {
        if (found == channelFields.end()) return;
        GtkWidget* page = gtk_widget_get_parent(found->second);
        const int pageNumber = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), page);
        if (pageNumber >= 0) gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), pageNumber);
        chatTags.erase(gtk_text_view_get_buffer(GTK_TEXT_VIEW(found->second)));
        channelFields.erase(found);
        return;
    }
    if (found != channelFields.end()) return;
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkWidget* field = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(field), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(field), false);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(field), GTK_WRAP_WORD_CHAR);
    configureChatField(field);
    gtk_container_add(GTK_CONTAINER(scrolled), field);
    channelFields.emplace("NC", field);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new("NC"));
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), scrolled, true);
    gtk_widget_show_all(scrolled);
}

void TRemoteFrame::configureChatField(GtkWidget* field) {
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(field), 5);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(field), 5);
    GdkColor chatBackgroundColor;
    GdkColor chatColor;
    gdk_color_parse(options.colorchatback.c_str(), &chatBackgroundColor);
    gdk_color_parse(options.colorchat.c_str(), &chatColor);
    gtk_widget_modify_base(field, GTK_STATE_NORMAL, &chatBackgroundColor);
    gtk_widget_modify_text(field, GTK_STATE_NORMAL, &chatColor);
    PangoFontDescription* chatFont = pango_font_description_from_string(((options.chatfontfamily.empty() ? std::string("Sans") : options.chatfontfamily) + " " + std::to_string(options.chatfontsize)).c_str());
    gtk_widget_modify_font(field, chatFont);
    pango_font_description_free(chatFont);
    GtkStyleContext* context = gtk_widget_get_style_context(field);
    GtkCssProvider* oldProvider = static_cast<GtkCssProvider*>(g_object_get_data(G_OBJECT(field), "remote-chat-provider"));
    if (oldProvider != nullptr) gtk_style_context_remove_provider(context, GTK_STYLE_PROVIDER(oldProvider));
    GtkCssProvider* provider = gtk_css_provider_new();
    const std::string css = "textview, textview text { background-color: " + options.colorchatback + "; color: " + options.colorchat + "; }";
    gtk_css_provider_load_from_data(provider, css.c_str(), -1, nullptr);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_set_data_full(G_OBJECT(field), "remote-chat-provider", provider, g_object_unref);
    if (g_object_get_data(G_OBJECT(field), "remote-chat-links") == nullptr) {
        gtk_widget_add_events(field, GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);
        g_signal_connect(field, "button-press-event", G_CALLBACK(onChatLinkClick), this);
        g_signal_connect(field, "motion-notify-event", G_CALLBACK(onChatMotion), this);
        g_signal_connect(field, "leave-notify-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventCrossing*, gpointer) -> gboolean { GdkWindow* window = gtk_text_view_get_window(GTK_TEXT_VIEW(widget), GTK_TEXT_WINDOW_TEXT); if (window != nullptr) gdk_window_set_cursor(window, nullptr); return false; }), this);
        g_object_set_data(G_OBJECT(field), "remote-chat-links", GINT_TO_POINTER(1));
    }
}

void TRemoteFrame::refreshMentionCompletion() {
    if (mentionStore == nullptr) return;
    gtk_list_store_clear(mentionStore);
    static const char* commands[] = {"/open", "/openacc", "/opencomments", "/openrights", "/openaccess", "/openprofile", "/openban", "/reset", "/rchelp", "/nc", "/reconnect", "/rc", "/find", "/finddef", "/clear"};
    for (const char* command : commands) {
        GtkTreeIter row;
        gtk_list_store_append(mentionStore, &row);
        gtk_list_store_set(mentionStore, &row, 0, command, 1, "", 2, "", 3, command, 4, "", -1);
    }
    if (connection == nullptr) { gtk_entry_completion_complete(mentionCompletion); return; }
    RCPlayer* players = nullptr;
    const int count = rc_get_players(connection, &players);
    std::vector<std::string> accounts;
    for (int index = 0; index < count; ++index) {
        const RCPlayer& player = players[index];
        const std::string account = player.account == nullptr ? "" : player.account;
        if (account.empty() || g_ascii_strcasecmp(account.c_str(), accountName.c_str()) == 0) continue;
        if (std::any_of(accounts.begin(), accounts.end(), [&](const std::string& existing) { return g_ascii_strcasecmp(existing.c_str(), account.c_str()) == 0; })) continue;
        accounts.push_back(account);
        const std::string nick = player.nick == nullptr || *player.nick == '\0' ? account : player.nick;
        const std::string community = playerCommunityNames.count(player.id) == 0 ? "" : playerCommunityNames[player.id];
        const std::string display = account + (g_ascii_strcasecmp(account.c_str(), nick.c_str()) == 0 ? "" : " - " + nick);
        GtkTreeIter row;
        gtk_list_store_append(mentionStore, &row);
        gtk_list_store_set(mentionStore, &row, 0, display.c_str(), 1, account.c_str(), 2, nick.c_str(), 3, account.c_str(), 4, community.c_str(), -1);
    }
    gtk_entry_completion_complete(mentionCompletion);
}

void TRemoteFrame::updateNCUi(bool connected) {
    for (int index = 8; index < 15; ++index) {
        if (graphicalButtons[index] == nullptr) continue;
        if (connected) gtk_widget_show(graphicalButtons[index]); else gtk_widget_hide(graphicalButtons[index]);
    }
    if (npcServerLabel != nullptr) {
        if (connected) gtk_widget_show(npcServerLabel); else gtk_widget_hide(npcServerLabel);
        for (GtkWidget* shadow : npcServerLabelShadows) if (shadow != nullptr) { if (connected) gtk_widget_show(shadow); else gtk_widget_hide(shadow); }
    }
    setNCChannelVisible(connected && options.separatenc);
    if (!connected) {
        if (classList != nullptr) classList->hide();
        if (weaponList != nullptr) weaponList->hide();
        if (npcList != nullptr) npcList->hide();
        if (levelList != nullptr) levelList->hide();
        if (serverOptionsEditor != nullptr) serverOptionsEditor->hide();
        if (serverFlagsEditor != nullptr) serverFlagsEditor->hide();
        if (folderConfigEditor != nullptr) folderConfigEditor->hide();
    }
}

void TRemoteFrame::applyChatUrls(GtkTextBuffer* buffer, gint startOffset, gint endOffset) {
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_iter_at_offset(buffer, &start, startOffset);
    gtk_text_buffer_get_iter_at_offset(buffer, &end, endOffset);
    gchar* value = gtk_text_buffer_get_text(buffer, &start, &end, false);
    const std::string text = value == nullptr ? "" : value;
    g_free(value);
    for (std::size_t position = 0; position < text.size();) {
        const auto startsUrl = [&text](std::size_t index) {
            return text.compare(index, 7, "http://") == 0 || text.compare(index, 8, "https://") == 0 || text.compare(index, 4, "www.") == 0;
        };
        if (!startsUrl(position) || (position != 0 && (std::isalnum(static_cast<unsigned char>(text[position - 1])) != 0 || text[position - 1] == '_'))) { ++position; continue; }
        std::size_t finish = position;
        while (finish < text.size() && std::isspace(static_cast<unsigned char>(text[finish])) == 0 && text[finish] != '<' && text[finish] != '>' && text[finish] != '"' && text[finish] != '\'') ++finish;
        while (finish > position && (text[finish - 1] == '.' || text[finish - 1] == ',' || text[finish - 1] == ';' || text[finish - 1] == ':' || text[finish - 1] == '!' || text[finish - 1] == ')')) --finish;
        if (finish == position) { ++position; continue; }
        const std::string url = text.substr(position, finish - position);
        const std::string target = url.rfind("www.", 0) == 0 ? "https://" + url : url;
        const gint urlStart = startOffset + static_cast<gint>(g_utf8_strlen(text.c_str(), static_cast<gssize>(position)));
        const gint urlEnd = urlStart + static_cast<gint>(g_utf8_strlen(text.c_str() + position, static_cast<gssize>(finish - position)));
        GtkTextTag* tag = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.colorchatbold.c_str(), "underline", PANGO_UNDERLINE_SINGLE, nullptr);
        g_object_set_data_full(G_OBJECT(tag), "remote-chat-url", g_strdup(target.c_str()), g_free);
        gtk_text_buffer_get_iter_at_offset(buffer, &start, urlStart);
        gtk_text_buffer_get_iter_at_offset(buffer, &end, urlEnd);
        gtk_text_buffer_apply_tag(buffer, tag, &start, &end);
        position = finish;
    }
}

gboolean TRemoteFrame::onChatLinkClick(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    if (event->button != GDK_BUTTON_PRIMARY || event->type != GDK_BUTTON_PRESS) return false;
    gint x = 0;
    gint y = 0;
    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget), GTK_TEXT_WINDOW_WIDGET, static_cast<gint>(event->x), static_cast<gint>(event->y), &x, &y);
    GtkTextIter iter;
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, x, y);
    GSList* tags = gtk_text_iter_get_tags(&iter);
    const char* url = nullptr;
    for (GSList* item = tags; item != nullptr && url == nullptr; item = item->next) url = static_cast<const char*>(g_object_get_data(G_OBJECT(item->data), "remote-chat-url"));
    g_slist_free(tags);
    if (url == nullptr) return false;
    GError* error = nullptr;
    gtk_show_uri_on_window(GTK_WINDOW(static_cast<TRemoteFrame*>(data)->window), url, event->time, &error);
    if (error != nullptr) g_error_free(error);
    return true;
}

gboolean TRemoteFrame::onChatMotion(GtkWidget* widget, GdkEventMotion* event, gpointer) {
    gint x = 0;
    gint y = 0;
    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget), GTK_TEXT_WINDOW_WIDGET, static_cast<gint>(event->x), static_cast<gint>(event->y), &x, &y);
    GtkTextIter iter;
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, x, y);
    GSList* tags = gtk_text_iter_get_tags(&iter);
    bool link = false;
    for (GSList* item = tags; item != nullptr; item = item->next) if (g_object_get_data(G_OBJECT(item->data), "remote-chat-url") != nullptr) { link = true; break; }
    g_slist_free(tags);
    GdkWindow* textWindow = gtk_text_view_get_window(GTK_TEXT_VIEW(widget), GTK_TEXT_WINDOW_TEXT);
    if (textWindow != nullptr) {
        GdkDisplay* display = gdk_window_get_display(textWindow);
        GdkCursor* cursor = gdk_cursor_new_from_name(display, link ? "pointer" : "text");
        gdk_window_set_cursor(textWindow, cursor);
        if (cursor != nullptr) g_object_unref(cursor);
    }
    return false;
}

void TRemoteFrame::applyEmotes(GtkTextBuffer* buffer, gint startOffset, const std::string& message) {
    const struct { const char* text; GdkPixbuf* pixbuf; } emotes[] = {{"Kappa", kappaEmote}, {"PMNormal", pmNormalEmote}, {":v", pacmanEmote}};
    for (const auto& emote : emotes) {
        if (emote.pixbuf == nullptr) continue;
        std::size_t position = message.find(emote.text);
        gint inserted = 0;
        while (position != std::string::npos) {
            GtkTextIter start;
            GtkTextIter end;
            gtk_text_buffer_get_iter_at_offset(buffer, &start, startOffset + static_cast<gint>(position) + inserted);
            gtk_text_buffer_get_iter_at_offset(buffer, &end, startOffset + static_cast<gint>(position + std::char_traits<char>::length(emote.text)) + inserted);
            gtk_text_buffer_apply_tag(buffer, chatTagsFor(buffer).invisible, &start, &end);
            gtk_text_buffer_insert_pixbuf(buffer, &start, emote.pixbuf);
            ++inserted;
            position = message.find(emote.text, position + std::char_traits<char>::length(emote.text));
        }
    }
}

void TRemoteFrame::appendChannelMessage(const std::string& channel, const std::string& message) {
    if (channel.empty()) {
        appendChat(message);
        return;
    }
    if (message == "* Left " + channel) {
        removeChannel(channel);
        return;
    }
    GtkWidget* existingScrolled = channelFields.count(channel) == 0 ? nullptr : gtk_widget_get_parent(channelFields[channel]);
    GtkAdjustment* existingAdjustment = existingScrolled != nullptr && GTK_IS_SCROLLED_WINDOW(existingScrolled) ? gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(existingScrolled)) : nullptr;
    const double previousValue = existingAdjustment == nullptr ? 0.0 : gtk_adjustment_get_value(existingAdjustment);
    const bool followBottom = existingAdjustment == nullptr || gtk_adjustment_get_upper(existingAdjustment) - gtk_adjustment_get_page_size(existingAdjustment) - previousValue <= 2.0;
    const bool colorAlert = message.rfind("#ALERT", 0) == 0;
    std::string display = message;
    const bool alert = applyAlertTag(display, true);
    GtkWidget* selectedPage = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook)));
    if (selectedPage == nullptr) selectedPage = chatScrolled;
    GtkWidget*& field = channelFields[channel];
    if (field == nullptr) {
        GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
        field = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(field), false);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(field), false);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(field), GTK_WRAP_WORD_CHAR);
        configureChatField(field);
        gtk_container_add(GTK_CONTAINER(scrolled), field);
        GtkWidget* tab = gtk_event_box_new();
        gtk_container_add(GTK_CONTAINER(tab), gtk_label_new(channel.c_str()));
        gtk_widget_add_events(tab, GDK_BUTTON_PRESS_MASK);
        g_object_set_data(G_OBJECT(tab), "remote-frame", this);
        g_object_set_data_full(G_OBJECT(tab), "remote-channel", g_strdup(channel.c_str()), g_free);
        g_signal_connect(tab, "button-press-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventButton* event, gpointer) -> gboolean { if (event->button != 3) return false; GtkWidget* menu = gtk_menu_new(); GtkWidget* close = gtk_menu_item_new_with_label("Close"); g_object_set_data(G_OBJECT(close), "remote-frame", g_object_get_data(G_OBJECT(widget), "remote-frame")); g_object_set_data_full(G_OBJECT(close), "remote-channel", g_strdup(static_cast<const char*>(g_object_get_data(G_OBJECT(widget), "remote-channel"))), g_free); g_signal_connect(close, "activate", G_CALLBACK(+[](GtkMenuItem* item, gpointer) { auto* frame = static_cast<TRemoteFrame*>(g_object_get_data(G_OBJECT(item), "remote-frame")); const char* channel = static_cast<const char*>(g_object_get_data(G_OBJECT(item), "remote-channel")); if (frame != nullptr && channel != nullptr) frame->removeChannel(channel); }), nullptr); gtk_menu_shell_append(GTK_MENU_SHELL(menu), close); gtk_widget_show_all(menu); gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event)); return true; }), nullptr);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, tab);
        gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(notebook), scrolled, false);
        gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), scrolled, true);
        gtk_widget_show_all(tab);
        gtk_widget_show_all(scrolled);
        const int selectedIndex = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), selectedPage);
        if (selectedIndex >= 0) gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), selectedIndex);
        struct SelectionRestore { GtkWidget* notebook; GtkWidget* page; };
        auto* restore = new SelectionRestore{GTK_WIDGET(g_object_ref(notebook)), GTK_WIDGET(g_object_ref(selectedPage))};
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, +[](gpointer data) -> gboolean {
            auto* restore = static_cast<SelectionRestore*>(data);
            const int page = gtk_notebook_page_num(GTK_NOTEBOOK(restore->notebook), restore->page);
            if (page >= 0) gtk_notebook_set_current_page(GTK_NOTEBOOK(restore->notebook), page);
            g_object_unref(restore->page);
            g_object_unref(restore->notebook);
            delete restore;
            return G_SOURCE_REMOVE;
        }, restore, nullptr);
    }
    if (message.empty()) return;
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(field));
    ChatTags& tags = chatTagsFor(buffer);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    const std::string timestamp = chatTimestamp(options);
    if (!timestamp.empty()) {
        gtk_text_buffer_insert_with_tags(buffer, &end, (timestamp + " ").c_str(), -1, tags.timestamp, nullptr);
        gtk_text_buffer_get_end_iter(buffer, &end);
    }
    const gint startOffset = gtk_text_iter_get_offset(&end);
    if (colorAlert) {
        gtk_text_buffer_insert_with_tags(buffer, &end, (display + "\n").c_str(), -1, tags.alert, nullptr);
    } else {
        const std::size_t separator = display.find(':');
        if (separator != std::string::npos && separator > 0) {
            const std::string prefix = display.substr(0, separator + 1);
            gtk_text_buffer_insert_with_tags(buffer, &end, prefix.c_str(), -1, tags.bold, nullptr);
            GtkTextIter textStart;
            gtk_text_buffer_get_end_iter(buffer, &textStart);
            const gint textStartOffset = gtk_text_iter_get_offset(&textStart);
            gtk_text_buffer_get_end_iter(buffer, &end);
            gtk_text_buffer_insert(buffer, &end, (display.substr(separator + 1) + "\n").c_str(), -1);
            applyEmotes(buffer, textStartOffset, display.substr(separator + 1));
        } else {
            gtk_text_buffer_insert(buffer, &end, (display + "\n").c_str(), -1);
            applyEmotes(buffer, startOffset, display);
        }
    }
    GtkTextIter linkEnd;
    gtk_text_buffer_get_end_iter(buffer, &linkEnd);
    applyChatUrls(buffer, startOffset, gtk_text_iter_get_offset(&linkEnd));
    if (alert && !hasActiveRemoteControlWindow()) {
        if (!hasActiveRemoteControlWindow()) gtk_window_set_urgency_hint(GTK_WINDOW(window), true);
        if (options.notificationsounds) gdk_beep();
    }
    if (followBottom) g_idle_add(scrollChannelToBottom, new ChannelScrollRequest{GTK_WIDGET(g_object_ref(field)), previousValue});
}

void TRemoteFrame::removeChannel(const std::string& channel) {
    const auto found = channelFields.find(channel);
    if (found == channelFields.end()) return;
    GtkWidget* scrolled = gtk_widget_get_parent(found->second);
    const int page = scrolled == nullptr ? -1 : gtk_notebook_page_num(GTK_NOTEBOOK(notebook), scrolled);
    if (page != -1) gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), page);
    chatTags.erase(gtk_text_view_get_buffer(GTK_TEXT_VIEW(found->second)));
    channelFields.erase(found);
}

void TRemoteFrame::beginFindResults(const std::string& base) {
    findResultBase = base;
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    findResultsField = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(findResultsField), false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(findResultsField), false);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(findResultsField), GTK_WRAP_WORD_CHAR);
    configureChatField(findResultsField);
    gtk_container_add(GTK_CONTAINER(scrolled), findResultsField);
    g_signal_connect(findResultsField, "button-press-event", G_CALLBACK(onFindResultClick), this);
    const int page = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new("Find Results"));
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), scrolled, true);
    gtk_widget_show_all(scrolled);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), page);
}

bool TRemoteFrame::appendFindResult(const std::string& message) {
    static const std::string header = "Game files found (relative to ";
    const std::size_t headerStart = message.find(header);
    if (headerStart != std::string::npos) {
        const std::size_t baseStart = headerStart + header.size();
        const std::size_t baseEnd = message.find(", max", baseStart);
        if (findResultsField == nullptr) beginFindResults(baseEnd == std::string::npos ? "" : message.substr(baseStart, baseEnd - baseStart));
        else findResultBase = baseEnd == std::string::npos ? "" : message.substr(baseStart, baseEnd - baseStart);
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(findResultsField));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, (message + "\n").c_str(), -1);
        return true;
    }
    if (findResultsField == nullptr) return false;
    const std::size_t contentStart = message.starts_with('[') && message.find("] ") != std::string::npos ? message.find("] ") + 2 : 0;
    const std::size_t separator = message.find(':', contentStart);
    const std::size_t byteCount = separator == std::string::npos ? std::string::npos : message.find(" byte,", separator + 1);
    if (separator == std::string::npos || byteCount == std::string::npos) {
        if (message.find("Also found default files matching this") == std::string::npos) return false;
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(findResultsField));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, (message + "\n").c_str(), -1);
        return true;
    }
    const std::string filename = message.substr(contentStart, separator - contentStart);
    if (filename.find('/') == std::string::npos && filename.find('.') == std::string::npos) return false;
    std::string path = findResultBase;
    if (!path.empty() && path.back() != '/') path += '/';
    path += filename;
    const std::size_t folderSeparator = path.find_last_of('/');
    const std::string folder = folderSeparator == std::string::npos ? findResultBase : path.substr(0, folderSeparator + 1);
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(findResultsField));
    GtkTextTag* link = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.colorchatbold.c_str(), "underline", PANGO_UNDERLINE_SINGLE, nullptr);
    g_object_set_data_full(G_OBJECT(link), "remote-find-folder", g_strdup(folder.c_str()), g_free);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, message.substr(0, contentStart).c_str(), -1);
    gtk_text_buffer_insert_with_tags(buffer, &end, filename.c_str(), -1, link, nullptr);
    gtk_text_buffer_insert(buffer, &end, (message.substr(separator) + "\n").c_str(), -1);
    return true;
}

gboolean TRemoteFrame::onFindResultClick(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    if (event->button != GDK_BUTTON_PRIMARY || event->type != GDK_BUTTON_PRESS) return false;
    gint x = 0;
    gint y = 0;
    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget), GTK_TEXT_WINDOW_WIDGET, static_cast<gint>(event->x), static_cast<gint>(event->y), &x, &y);
    GtkTextIter iter;
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, x, y);
    GSList* tags = gtk_text_iter_get_tags(&iter);
    const char* folder = nullptr;
    for (GSList* item = tags; item != nullptr && folder == nullptr; item = item->next) folder = static_cast<const char*>(g_object_get_data(G_OBJECT(item->data), "remote-find-folder"));
    g_slist_free(tags);
    if (folder == nullptr) return false;
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return true;
    if (frame->fileBrowser == nullptr) frame->fileBrowser = new TFileBrowserTree();
    frame->fileBrowser->setServerName(frame->serverName);
    frame->fileBrowser->setDownloadFolder(frame->options.downloadfolder);
    frame->fileBrowser->setDownloadServer(frame->serverName);
    frame->fileBrowser->setModernFileBrowser(frame->options.modernfilebrowser);
    frame->fileBrowser->setHoverPreviews(frame->options.filebrowserhoverpreview);
    frame->fileBrowser->setModernThumbnails(frame->options.filebrowserthumbnails);
    frame->fileBrowser->openFolder(frame->connection, folder);
    return true;
}

TRemoteFrame::ChatTags& TRemoteFrame::chatTagsFor(GtkTextBuffer* buffer) {
    auto [entry, inserted] = chatTags.try_emplace(buffer);
    if (inserted) {
        entry->second.alert = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.coloralert.c_str(), "weight", PANGO_WEIGHT_BOLD, nullptr);
        entry->second.bold = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.colorchatbold.c_str(), "weight", PANGO_WEIGHT_BOLD, nullptr);
        entry->second.timestamp = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.colorchatbold.c_str(), nullptr);
        entry->second.invisible = gtk_text_buffer_create_tag(buffer, nullptr, "invisible", true, nullptr);
    }
    return entry->second;
}

bool TRemoteFrame::applyAlertTag(std::string& message, bool allowUrgency) {
    static const std::pair<const char*, bool> tags[] = {{"#ALERTSF", false}, {"#ALERTFS", false}, {"#ALERTSP", true}, {"#ALERTPS", true}, {"#ALERTF", true}, {"#ALERTP", true}, {"#ALERT", false}};
    for (const auto& [tag, sound] : tags) {
        const std::size_t length = std::char_traits<char>::length(tag);
        if (message.rfind(tag, 0) != 0) continue;
        message.erase(0, length);
        if (sound) return true;
        if (allowUrgency && !hasActiveRemoteControlWindow()) gtk_window_set_urgency_hint(GTK_WINDOW(window), true);
        return false;
    }
    return false;
}

void TRemoteFrame::send() {
    if (connection == nullptr) return;
    const std::string message = gtk_entry_get_text(GTK_ENTRY(editField));
    if (message.empty()) return;
    if (chatHistory.empty() || chatHistory.front() != message) chatHistory.insert(chatHistory.begin(), message);
    if (chatHistory.size() > 30) chatHistory.pop_back();
    chatHistoryIndex = -1;
    if (message == "/rchelp") {
        appendChat("RC commands:");
        appendChat("  /nc connect (or /nc c) - connect to the NPC server");
        appendChat("  /nc disconnect (or /nc dc) - disconnect the NPC server");
        appendChat("  /nc rc - reconnect the NPC server");
        appendChat("  /reconnect or /rc - reconnect the RC server");
        appendChat("  /scripthelp2 [query] - search script help");
        appendChat("  /find [text] or /finddef [text] - find in the current editor");
        appendChat("  /clear or /clear all - clear the current chat or all chats");
        appendChat("Emotes: type Kappa, PMNormal, or :v in chat", false, true);
        gtk_entry_set_text(GTK_ENTRY(editField), "");
        return;
    }
    if (message == "/help") {
        if (!rc_execute(connection, message.c_str())) appendChat(rc_last_error(connection));
        appendChat("RC: /rchelp - show Remote Control commands");
        appendChat("RC emotes: type Kappa, PMNormal, or :v in chat", false, true);
        gtk_entry_set_text(GTK_ENTRY(editField), "");
        return;
    }
    if (message == "/nc connect" || message == "/nc c" || message == "/nc rc") {
        reconnectNPCServer();
        gtk_entry_set_text(GTK_ENTRY(editField), "");
        return;
    }
    if (message == "/nc disconnect" || message == "/nc dc") {
        disconnectNPCServer();
        gtk_entry_set_text(GTK_ENTRY(editField), "");
        return;
    }
    if (message == "/reconnect" || message == "/rc") {
        reconnectServer();
        gtk_entry_set_text(GTK_ENTRY(editField), "");
        return;
    }
    if (message.rfind("/scripthelp2", 0) == 0 && (message.size() == 12 || std::isspace(static_cast<unsigned char>(message[12])) != 0)) {
        std::string query = message.substr(12);
        const std::size_t first = query.find_first_not_of(" \t");
        query = first == std::string::npos ? "" : query.substr(first);
        std::weak_ptr<bool> alive = callbackAlive;
        requestGScriptHelp(query, [this, alive](std::vector<std::string> lines) { const std::shared_ptr<bool> state = alive.lock(); if (!state || !*state) return; for (const std::string& line : lines) appendChat(line); });
        gtk_entry_set_text(GTK_ENTRY(editField), "");
        return;
    }
    if (options.separatefindresults && (message == "/find" || message.rfind("/find ", 0) == 0 || message == "/finddef" || message.rfind("/finddef ", 0) == 0)) beginFindResults("");
    if (message == "/clear") {
        GtkWidget* page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook)));
        GtkWidget* field = page == nullptr ? nullptr : gtk_bin_get_child(GTK_BIN(page));
        if (field != nullptr && GTK_IS_TEXT_VIEW(field)) gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(field)), "", -1);
        gtk_entry_set_text(GTK_ENTRY(editField), "");
        return;
    }
    if (message == "/clear all") {
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(chatField)), "", -1);
        for (const auto& entry : channelFields) gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(entry.second)), "", -1);
        gtk_entry_set_text(GTK_ENTRY(editField), "");
        return;
    }
    std::string serverCommand = message;
    static const char* accountCommands[] = {"/open", "/openacc", "/opencomments", "/openrights", "/openaccess", "/openprofile", "/openban", "/reset"};
    for (const char* command : accountCommands) if (serverCommand == command && !accountName.empty()) { serverCommand += " "; serverCommand += accountName; break; }
    if (!rc_execute(connection, serverCommand.c_str())) appendChat(rc_last_error(connection));
    gtk_entry_set_text(GTK_ENTRY(editField), "");
}

void TRemoteFrame::reconnectNPCServer() {
    syncManager->setConnection(nullptr, serverName);
    if (connection == nullptr) return;
    ncManuallyDisconnected = false;
    updateNCUi(false);
    if (rc_is_nc_connected(connection) != 0) rc_disconnect_nc(connection);
    ncConnectionAttempted = true;
    if (!rc_connect_to_nc_server(connection)) appendChat(rc_last_error(connection));
}

void TRemoteFrame::disconnectNPCServer() {
    if (connection == nullptr) return;
    ncManuallyDisconnected = true;
    if (!rc_disconnect_nc(connection)) appendChat(rc_last_error(connection));
    ncConnectionAttempted = true;
    updateNCUi(false);
}

void TRemoteFrame::reconnectServer() {
    if (connection == nullptr || currentServerIndex < 0) return;
    suppressReconnectDisconnect = rc_is_connected(connection) != 0;
    ncConnectionAttempted = false;
    if (!rc_connect_to_server(connection, currentServerIndex)) {
        suppressReconnectDisconnect = false;
        appendChat(rc_last_error(connection));
    }
}
