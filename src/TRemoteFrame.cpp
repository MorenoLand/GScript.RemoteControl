#include "TRemoteFrame.h"
#include "ErrorWindow.h"
#include "Backup.h"
#include "Debug.h"
#ifdef _WIN32
#include <gdk/gdkwin32.h>
#include <windows.h>
#endif
#include "GScriptEditor.h"
#include "RCOptions.h"
#include "Theme.h"
#include "TFileBrowserTree.h"
#include "TPlayerList.h"
#include "TScriptList.h"
#include "TServerTextEditor.h"
#include "TToallsWindow.h"
#include "TAccountsWindow.h"
#include "TOptionsWindow.h"
#include "TNPCList.h"

#include <grclib.h>
#include <IEnums.h>
#include <gtksourceview/gtksource.h>
#include <webp/demux.h>

#include <cctype>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

extern void remote_control_begin_pm_tray_alert();
extern void remote_control_clear_pm_tray_alert();
extern void remote_control_set_tray_label(const char* serverName, int playerCount);

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

    bool loadWebPAnimation(const std::filesystem::path& path, std::vector<GdkPixbuf*>& frames, std::vector<int>& durations) {
        gchar* contents = nullptr;
        gsize length = 0;
        if (!g_file_get_contents(path.string().c_str(), &contents, &length, nullptr)) return false;
        WebPData data{reinterpret_cast<const uint8_t*>(contents), length};
        WebPAnimDecoderOptions options;
        WebPAnimDecoderOptionsInit(&options);
        WebPAnimDecoder* decoder = WebPAnimDecoderNew(&data, &options);
        if (decoder == nullptr) { g_free(contents); return false; }
        WebPAnimInfo info;
        if (!WebPAnimDecoderGetInfo(decoder, &info)) { WebPAnimDecoderDelete(decoder); g_free(contents); return false; }
        int previousTimestamp = 0;
        while (WebPAnimDecoderHasMoreFrames(decoder)) {
            uint8_t* pixels = nullptr;
            int timestamp = 0;
            if (!WebPAnimDecoderGetNext(decoder, &pixels, &timestamp)) break;
            GdkPixbuf* frame = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, true, 8, info.canvas_width, info.canvas_height, info.canvas_width * 4, nullptr, nullptr);
            if (frame != nullptr) {
                frames.push_back(gdk_pixbuf_copy(frame));
                g_object_unref(frame);
                durations.push_back(std::max(10, timestamp - previousTimestamp));
                previousTimestamp = timestamp;
            }
        }
        WebPAnimDecoderDelete(decoder);
        g_free(contents);
        if (frames.empty()) return false;
        if (durations.size() == 1) durations[0] = 100;
        return true;
    }
}

TRemoteFrame::TRemoteFrame(const RC::RCOptions& nextOptions, const std::filesystem::path& nextApplicationDirectory, std::function<void()> onClose, std::function<void()> onListServer, std::function<void()> onListServerSettings) : onCloseCallback(std::move(onClose)), onListServerCallback(std::move(onListServer)), onListServerSettingsCallback(std::move(onListServerSettings)), options(nextOptions), applicationDirectory(nextApplicationDirectory) {
    kappaEmote = gdk_pixbuf_new_from_file((applicationDirectory / "images" / "emote_kappa.png").string().c_str(), nullptr);
    pmNormalEmote = gdk_pixbuf_new_from_file((applicationDirectory / "images" / "pmicon_normal.png").string().c_str(), nullptr);
    pacmanEmote = gdk_pixbuf_new_from_file((applicationDirectory / "images" / "emote_pacman.png").string().c_str(), nullptr);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "RemoteFrame");
    gtk_window_set_title(GTK_WINDOW(window), (std::string("Remote Control ") + REMOTE_CONTROL_BUILD_DATE).c_str());
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 350);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);
    GtkWidget* graphicalContainer = nullptr;

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
        GtkWidget* fixed = gtk_fixed_new();
        graphicalFixed = fixed;
        const std::filesystem::path background = applicationDirectory / "images" / options.background;
        std::string extension = background.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (extension == ".webp" && loadWebPAnimation(background, backgroundWebPFrames, backgroundWebPFrameDurations)) {
            backgroundWebPNextFrame = g_get_monotonic_time() + static_cast<gint64>(backgroundWebPFrameDurations.front()) * 1000;
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
        gtk_container_add(GTK_CONTAINER(header), backgroundImage);
        gtk_overlay_add_overlay(GTK_OVERLAY(header), fixed);
        g_signal_connect(backgroundImage, "draw", G_CALLBACK(onGraphicalDraw), this);
        if (backgroundAnimationIter != nullptr || !backgroundWebPFrames.empty()) backgroundAnimationSource = g_timeout_add(16, +[](gpointer data) -> gboolean {
            TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
            if (frame->backgroundImage == nullptr) return G_SOURCE_REMOVE;
            if (!frame->backgroundWebPFrames.empty()) {
                const gint64 now = g_get_monotonic_time();
                if (now < frame->backgroundWebPNextFrame) return G_SOURCE_CONTINUE;
                frame->backgroundWebPFrame = (frame->backgroundWebPFrame + 1) % frame->backgroundWebPFrames.size();
                frame->backgroundWebPNextFrame = now + static_cast<gint64>(frame->backgroundWebPFrameDurations[frame->backgroundWebPFrame]) * 1000;
                gtk_widget_queue_draw(frame->backgroundImage);
                return G_SOURCE_CONTINUE;
            }
            if (frame->backgroundAnimationIter == nullptr) return G_SOURCE_REMOVE;
            GTimeVal now;
            g_get_current_time(&now);
            if (!gdk_pixbuf_animation_iter_advance(frame->backgroundAnimationIter, &now)) return G_SOURCE_CONTINUE;
            gtk_widget_queue_draw(frame->backgroundImage);
            return G_SOURCE_CONTINUE;
        }, this);
        const int positions[12][2] = {{5, 15}, {5, 48}, {38, 15}, {71, 15}, {394, 15}, {427, 15}, {460, 15}, {460, 48}, {460, 81}, {460, 114}, {427, 114}, {394, 114}};
        for (int index = 0; index < 12; ++index) {
            GtkWidget* button = gtk_event_box_new();
            gtk_event_box_set_visible_window(GTK_EVENT_BOX(button), false);
            GtkWidget* buttonImage = gtk_image_new_from_file((applicationDirectory / "images" / options.buttonimagefiles[index]).string().c_str());
            gtk_container_add(GTK_CONTAINER(button), buttonImage);
            g_object_set_data(G_OBJECT(button), "button-index", GINT_TO_POINTER(index));
            g_signal_connect(button, "button-press-event", G_CALLBACK(onGraphicalButton), this);
            g_signal_connect(button, "button-release-event", G_CALLBACK(onGraphicalButton), this);
            gtk_fixed_put(GTK_FIXED(fixed), button, positions[index][0], positions[index][1]);
            graphicalButtons[index] = button;
            if (index >= 8) gtk_widget_hide(button);
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
                gtk_widget_modify_fg(shadow, GTK_STATE_NORMAL, &labelBackgroundColor);
                gtk_widget_modify_font(shadow, labelFont);
                gtk_fixed_put(GTK_FIXED(fixed), shadow, x + offset[0], y + offset[1]);
                (*shadows)[index] = shadow;
            }
            *front = gtk_label_new(text.c_str());
            gtk_widget_modify_fg(*front, GTK_STATE_NORMAL, &labelColor);
            gtk_widget_modify_font(*front, labelFont);
            gtk_fixed_put(GTK_FIXED(fixed), *front, x, y);
        };
        addLabel(options.labelservers, 10, 90, &serverLabel, &serverLabelShadows);
        addLabel(options.labelplayers, 10, 110, &playersLabel, &playersLabelShadows);
        pango_font_description_free(labelFont);
        gtk_box_pack_start(GTK_BOX(graphicalBase), header, false, false, 0);
        GtkWidget* filler = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_pack_start(GTK_BOX(graphicalBase), filler, true, true, 0);
        gtk_box_pack_start(GTK_BOX(root), graphicalContainer, true, true, 0);
        g_signal_connect(header, "size-allocate", G_CALLBACK(onGraphicalAllocate), this);
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
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(chatField), 5);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(chatField), 5);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chatField), GTK_WRAP_WORD_CHAR);
    configureChatField(chatField);
    gtk_container_add(GTK_CONTAINER(chatScrolled), chatField);
    GtkWidget* chatTab = gtk_label_new("RC Chat");
    gtk_widget_set_size_request(chatTab, -1, 16);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), chatScrolled, chatTab);
    gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(notebook), chatScrolled, false);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), chatScrolled, true);
    GtkCssProvider* tabProvider = gtk_css_provider_new();
    const std::string tabBackground = options.darkmode ? "#3d3d3d" : "#f5f5f5";
    const std::string tabBorder = options.darkmode ? "#707070" : "#c4c4c4";
    const std::string activeTabBackground = options.darkmode ? "#454545" : "#ffffff";
    const std::string activeTabBorder = options.darkmode ? "#909090" : "#9a9a9a";
    const std::string notebookCss = "#RemoteFrame notebook, #RemoteFrame notebook > header, #RemoteFrame notebook > header.top, #RemoteFrame notebook > header.top > tabs { margin: 0; padding: 0; border: 0; background-color: transparent; background-image: none; box-shadow: none; } #RemoteFrame notebook > header.top > tabs { padding-left: 6px; } #RemoteFrame notebook > header, #RemoteFrame notebook > header.top, #RemoteFrame notebook > header.top > tabs { min-height: 0; } #RemoteFrame notebook > stack, #RemoteFrame notebook > stack > scrolledwindow, #RemoteFrame notebook > stack > scrolledwindow > viewport { margin: 0; padding: 0; border: 0; background-color: " + options.colorchatback + "; } #RemoteFrame notebook > header.top > tabs > tab { min-height: 0; min-width: 0; margin: 0 1px 0 0; padding: 3px 7px; background-image: none; background-color: " + tabBackground + "; border: 1px solid " + tabBorder + "; border-bottom: none; border-radius: 3px 3px 0 0; } #RemoteFrame notebook > header.top > tabs > tab:checked { background-color: " + activeTabBackground + "; border-color: " + activeTabBorder + "; } #RemoteFrame notebook > header.top > tabs > tab label { min-width: 0; margin: 0; padding: 0; font-size: 12px; }";
    gtk_css_provider_load_from_data(tabProvider, notebookCss.c_str(), -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(tabProvider), GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
    g_object_unref(tabProvider);
    if (graphicalContainer != nullptr) {
        gtk_widget_set_halign(notebook, GTK_ALIGN_FILL);
        gtk_widget_set_valign(notebook, GTK_ALIGN_FILL);
        gtk_widget_set_margin_top(notebook, 156);
        gtk_overlay_add_overlay(GTK_OVERLAY(graphicalContainer), notebook);
    } else gtk_box_pack_start(GTK_BOX(root), notebook, true, true, 0);

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

    g_signal_connect(editField, "key-press-event", G_CALLBACK(onEditKey), this);
    g_signal_connect(window, "key-press-event", G_CALLBACK(onWindowKey), this);
    g_signal_connect(window, "configure-event", G_CALLBACK(onConfigure), this);
    g_signal_connect(window, "window-state-event", G_CALLBACK(onWindowState), this);
    g_signal_connect(window, "focus-in-event", G_CALLBACK(+[](GtkWidget*, GdkEventFocus*, gpointer data) -> gboolean { TRemoteFrame* frame = static_cast<TRemoteFrame*>(data); clearRemoteControlUrgency(); remote_control_clear_pm_tray_alert(); if (frame->playerList != nullptr) frame->playerList->clearPrivateMessageAlert(); return false; }), this);
    g_signal_connect(window, "delete-event", G_CALLBACK(onDelete), this);
}

gboolean TRemoteFrame::onConfigure(GtkWidget*, GdkEventConfigure* event, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
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
            return G_SOURCE_REMOVE;
        }, frame);
        return false;
    }
    frame->windowMaximized = maximized;
    return false;
}

gboolean TRemoteFrame::onGraphicalDraw(GtkWidget* widget, cairo_t* context, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    GdkPixbuf* pixbuf = !frame->backgroundWebPFrames.empty() ? frame->backgroundWebPFrames[frame->backgroundWebPFrame] : (frame->backgroundAnimationIter != nullptr ? gdk_pixbuf_animation_iter_get_pixbuf(frame->backgroundAnimationIter) : frame->backgroundPixbuf);
    if (pixbuf == nullptr) return false;
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    cairo_save(context);
    cairo_scale(context, static_cast<double>(std::max(1, allocation.width)) / gdk_pixbuf_get_width(pixbuf), static_cast<double>(std::max(1, allocation.height)) / gdk_pixbuf_get_height(pixbuf));
    gdk_cairo_set_source_pixbuf(context, pixbuf, 0, 0);
    cairo_paint(context);
    cairo_restore(context);
    return false;
}

TRemoteFrame::~TRemoteFrame() {
    *callbackAlive = false;
    if (eventSource != 0) g_source_remove(eventSource);
    if (backgroundAnimationSource != 0) g_source_remove(backgroundAnimationSource);
    if (window != nullptr) gtk_widget_destroy(window);
    if (backgroundPixbuf != nullptr) g_object_unref(backgroundPixbuf);
    if (backgroundAnimationIter != nullptr) g_object_unref(backgroundAnimationIter);
    if (backgroundAnimation != nullptr) g_object_unref(backgroundAnimation);
    for (GdkPixbuf* frame : backgroundWebPFrames) g_object_unref(frame);
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
}

void TRemoteFrame::open(void* nextConnection, int serverIndex, const std::string& serverName, const std::string& nickname, const std::string& accountName) {
    connection = nextConnection;
    currentServerIndex = serverIndex;
    this->serverName = serverName;
    trayPlayerCount = -1;
    setBackupServerName(serverName);
    disconnectHandled = false;
    this->nickname = nickname;
    this->accountName = accountName;
    ncConnectionAttempted = false;
    rc_on_connected(connection, onConnected, this);
    rc_on_disconnected(connection, onDisconnected, this);
    rc_on_message(connection, onMessage, this);
    rc_on_irc_message(connection, onIrcMessage, this);
    rc_on_private_message_ex(connection, onPrivateMessage, this);
    rc_on_server_data(connection, onServerData, this);
    rc_on_account_list(connection, onAccountList, this);
    rc_on_player_text_data(connection, onPlayerText, this);
    rc_on_player_rights(connection, onPlayerRights, this);
    rc_on_player_attributes(connection, onPlayerAttributes, this);
    rc_on_ban_data(connection, onBanData, this);
    rc_on_ban_list_data(connection, onBanListData, this);
    setNCChannelVisible(options.separatenc);
    const std::string serverText = options.labelservers + " " + serverName;
    if (serverLabel != nullptr) gtk_label_set_text(GTK_LABEL(serverLabel), serverText.c_str());
    for (GtkWidget* shadow : serverLabelShadows) if (shadow != nullptr) gtk_label_set_text(GTK_LABEL(shadow), serverText.c_str());
    if (eventSource == 0) eventSource = g_timeout_add(50, processEvents, this);
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

void TRemoteFrame::show() {
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

void TRemoteFrame::toggleVisibility() {
    if (gtk_widget_get_visible(window)) gtk_widget_hide(window);
    else show();
}

bool TRemoteFrame::openLatestPrivateMessage() {
    return playerList != nullptr && playerList->openLatestPrivateMessage();
}

void TRemoteFrame::onSend(GtkButton*, gpointer data) { static_cast<TRemoteFrame*>(data)->send(); }

void TRemoteFrame::onPlayerList(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    frame->playerList->open(frame->connection);
}

void TRemoteFrame::onToalls(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->toallsWindow == nullptr) frame->toallsWindow = new TToallsWindow();
    frame->toallsWindow->open(frame->connection, frame->nickname);
}

void TRemoteFrame::onAccounts(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->accountsWindow == nullptr) frame->accountsWindow = new TAccountsWindow();
    frame->accountsWindow->open(frame->connection);
}

void TRemoteFrame::onRCOptions(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->optionsWindow == nullptr) frame->optionsWindow = new TOptionsWindow(const_cast<RC::RCOptions&>(frame->options), frame->applicationDirectory, [frame](const RC::RCOptions& previous) { frame->applyOptions(previous); });
    frame->optionsWindow->open();
}

void TRemoteFrame::onAccountList(const char* accounts, void* data) { TRemoteFrame* frame = static_cast<TRemoteFrame*>(data); if (frame->accountsWindow != nullptr) frame->accountsWindow->setAccounts(accounts); }
void TRemoteFrame::onPlayerText(const char* type, const char* account, const char* content, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (type == nullptr || account == nullptr) return;
    if (std::string(type) == "account") {
        if (frame->accountsWindow == nullptr) frame->accountsWindow = new TAccountsWindow();
        frame->accountsWindow->showEditor(frame->connection, account, content);
        return;
    }
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handlePlayerText(type, account, content);
}
void TRemoteFrame::onPlayerRights(const char* account, int rights, const char* ipRange, const char* folderAccess, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handlePlayerRights(account, rights, ipRange, folderAccess);
}
void TRemoteFrame::onPlayerAttributes(const char* account, const char* properties, const char* editorText, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handlePlayerAttributes(account, properties, editorText);
}
void TRemoteFrame::onBanData(const char* account, const char* computerId, const char* details, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handleBanData(account, computerId, details);
}
void TRemoteFrame::onBanListData(const char* type, const char* account, const char* content, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    frame->playerList->setConnection(frame->connection);
    frame->playerList->handleBanListData(type, account, content);
}

void TRemoteFrame::onFileBrowser(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->fileBrowser == nullptr) frame->fileBrowser = new TFileBrowserTree();
    frame->fileBrowser->setDownloadFolder(frame->options.downloadfolder);
    frame->fileBrowser->open(frame->connection);
}

void TRemoteFrame::onClasses(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr || rc_is_nc_authenticated(frame->connection) == 0) return;
    if (frame->classList == nullptr) frame->classList = new TScriptList("classes");
    frame->classList->open(frame->connection);
}

void TRemoteFrame::onWeapons(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr || rc_is_nc_authenticated(frame->connection) == 0) return;
    if (frame->weaponList == nullptr) frame->weaponList = new TScriptList("weapons");
    frame->weaponList->open(frame->connection);
}

void TRemoteFrame::onNPCs(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr || rc_is_nc_authenticated(frame->connection) == 0) return;
    if (frame->npcList == nullptr) frame->npcList = new TNPCList(frame->accountName);
    frame->npcList->open(frame->connection);
}

void TRemoteFrame::onLocalNPCDump(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr || rc_is_nc_authenticated(frame->connection) == 0) return;
    rc_on_local_npcs(frame->connection, onLocalNPCData, frame);
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Local NPCs", GTK_WINDOW(frame->window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "OK", GTK_RESPONSE_OK, nullptr);
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
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Local NPCs", GTK_WINDOW(frame->window), GTK_DIALOG_DESTROY_WITH_PARENT, "Close", GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 380);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkSourceLanguage* language = gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(), "ini");
    GtkSourceBuffer* buffer = language != nullptr ? gtk_source_buffer_new_with_language(language) : gtk_source_buffer_new(nullptr);
    applyRemoteControlSourceStyle(buffer);
    GtkWidget* text = gtk_source_view_new_with_buffer(buffer);
    configureGScriptEditor(text);
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
    if (frame->serverOptionsEditor == nullptr) frame->serverOptionsEditor = new TServerTextEditor(TServerTextEditor::Kind::ServerOptions, "Server Options");
    frame->serverOptionsEditor->open(frame->connection);
}

void TRemoteFrame::onListServerSettings(GtkButton*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->onListServerSettingsCallback) frame->onListServerSettingsCallback();
}

void TRemoteFrame::onServerFlags(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->serverFlagsEditor == nullptr) frame->serverFlagsEditor = new TServerTextEditor(TServerTextEditor::Kind::ServerFlags, "Server Flags");
    frame->serverFlagsEditor->open(frame->connection);
}

void TRemoteFrame::onFolderConfig(GtkMenuItem*, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection == nullptr) return;
    if (frame->folderConfigEditor == nullptr) frame->folderConfigEditor = new TServerTextEditor(TServerTextEditor::Kind::FolderConfig, "Folder Config");
    frame->folderConfigEditor->open(frame->connection);
}

gboolean TRemoteFrame::onGraphicalButton(GtkWidget* button, GdkEventButton* event, gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    const int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "button-index"));
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
    if (event->type == GDK_BUTTON_RELEASE) frame->graphicalAction(index);
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

gboolean TRemoteFrame::onWindowKey(GtkWidget*, GdkEventKey* event, gpointer data) {
    if (event->keyval != GDK_KEY_F8) return false;
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->onListServerCallback) frame->onListServerCallback();
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
    const int positions[12][2] = {{5, 15}, {5, 48}, {38, 15}, {71, 15}, {394, 15}, {427, 15}, {460, 15}, {460, 48}, {460, 81}, {460, 114}, {427, 114}, {394, 114}};
    for (int index = 4; index < 12; ++index) gtk_fixed_move(GTK_FIXED(frame->graphicalFixed), frame->graphicalButtons[index], allocation->width - (500 - positions[index][0]), positions[index][1]);
}

gboolean TRemoteFrame::processEvents(gpointer data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    if (frame->connection != nullptr) {
        rc_process_events(frame->connection);
        if (!frame->ncConnectionAttempted && rc_has_nc_server(frame->connection) != 0 && rc_is_nc_connected(frame->connection) == 0) {
            frame->ncConnectionAttempted = true;
            rc_connect_to_nc_server(frame->connection);
        }
        const bool npcServerConnected = rc_is_nc_authenticated(frame->connection) != 0;
        for (int index = 8; index < 12; ++index) {
            if (frame->graphicalButtons[index] == nullptr) continue;
            if (npcServerConnected) gtk_widget_show(frame->graphicalButtons[index]);
            else gtk_widget_hide(frame->graphicalButtons[index]);
        }
        if (frame->playersLabel != nullptr) {
            RCPlayer* players = nullptr;
            const int count = rc_get_players(frame->connection, &players);
            const std::string playerText = frame->options.labelplayers + " " + std::to_string(count);
            gtk_label_set_text(GTK_LABEL(frame->playersLabel), playerText.c_str());
            for (GtkWidget* shadow : frame->playersLabelShadows) if (shadow != nullptr) gtk_label_set_text(GTK_LABEL(shadow), playerText.c_str());
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
    gtk_widget_hide(frame->window);
    frame->onCloseCallback();
    createErrorWindow("Connection Error", reason == nullptr ? "You have been disconnected!" : reason);
}

void TRemoteFrame::onMessage(const char* message, void* data) { static_cast<TRemoteFrame*>(data)->appendChat(message == nullptr ? "" : message); }

void TRemoteFrame::onIrcMessage(const char* channel, const char* line, void* data) {
    static_cast<TRemoteFrame*>(data)->appendChannelMessage(channel == nullptr ? "" : channel, line == nullptr ? "" : line);
}

void TRemoteFrame::onPrivateMessage(int playerId, const char* account, const char* nick, const char* message, const char* type, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    const std::string messageType = type == nullptr ? "normal" : type;
    if (messageType == "mass" && frame->options.nomassmessages) return;
    if (frame->playerList == nullptr) frame->playerList = new TPlayerList(frame->applicationDirectory, frame->accountName);
    const std::string display = frame->playerList->notePrivateMessage(playerId, account, nick, message, type);
    if (frame->options.newpmalerts) {
        frame->appendChat("#ALERT New PM from " + display, true);
    }
    remote_control_begin_pm_tray_alert();
}

void TRemoteFrame::onServerData(const char* type, const char* content, void* data) {
    TRemoteFrame* frame = static_cast<TRemoteFrame*>(data);
    const std::string value = content == nullptr ? "" : content;
    if (type != nullptr && std::string(type) == "toall" && frame->toallsWindow != nullptr) frame->toallsWindow->append(value.c_str());
    else if (type != nullptr && std::string(type) == "nc_message") {
        if (frame->options.separatenc) frame->appendChannelMessage("NC", value);
        else frame->appendChat(value);
    }
    else if (type != nullptr && std::string(type) == "options" && frame->serverOptionsEditor != nullptr) frame->serverOptionsEditor->setContent(value.c_str());
    else if (type != nullptr && std::string(type) == "flags" && frame->serverFlagsEditor != nullptr) frame->serverFlagsEditor->setContent(value.c_str());
    else if (type != nullptr && std::string(type) == "folder_config" && frame->folderConfigEditor != nullptr) frame->folderConfigEditor->setContent(value.c_str());
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
}

void TRemoteFrame::appendChat(const std::string& message, bool suppressUrgency) {
    const bool colorAlert = message.rfind("#ALERT", 0) == 0;
    std::string display = message;
    const bool alert = applyAlertTag(display, !suppressUrgency);
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chatField));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    const std::string timestamp = chatTimestamp(options);
    if (!timestamp.empty()) {
        gtk_text_buffer_insert(buffer, &end, (timestamp + " ").c_str(), -1);
        gtk_text_buffer_get_end_iter(buffer, &end);
    }
    const gint startOffset = gtk_text_iter_get_offset(&end);
    if (colorAlert) {
        GtkTextTag* tag = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.coloralert.c_str(), "weight", PANGO_WEIGHT_BOLD, nullptr);
        gtk_text_buffer_insert_with_tags(buffer, &end, (display + "\n").c_str(), -1, tag, nullptr);
    } else {
        const std::size_t separator = display.find(':');
        if (separator != std::string::npos && separator > 0) {
            const std::string prefix = display.substr(0, separator + 1);
            GtkTextTag* tag = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.colorchatbold.c_str(), "weight", PANGO_WEIGHT_BOLD, nullptr);
            gtk_text_buffer_insert_with_tags(buffer, &end, prefix.c_str(), -1, tag, nullptr);
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
    if (alert && !hasActiveRemoteControlWindow()) {
        gtk_window_set_urgency_hint(GTK_WINDOW(window), true);
        gdk_beep();
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
    nickname = options.nickname;
    if (connection != nullptr && nickname != previous.nickname) rc_set_nickname(connection, nickname.c_str());
    if (connection != nullptr && (options.nomassmessages != previous.nomassmessages || options.nomassifclienton != previous.nomassifclienton)) updateMassPMAcceptance();
    if (connection != nullptr && (options.globalpms != previous.globalpms || options.buddytracking != previous.buddytracking || options.showbuddies != previous.showbuddies)) sendServerListOptions();
    if (playerList != nullptr && options.attachaway != previous.attachaway) playerList->setAttachAway(options.attachaway);
    if (options.separatenc != previous.separatenc) setNCChannelVisible(options.separatenc);
    if (fileBrowser != nullptr && options.downloadfolder != previous.downloadfolder) fileBrowser->setDownloadFolder(options.downloadfolder);
    if (chatField != nullptr) configureChatField(chatField);
    for (const auto& entry : channelFields) if (entry.second != nullptr) configureChatField(entry.second);
}

void TRemoteFrame::sendServerListOptions() {
    const std::string payload = "GraalEngine,lister,options,globalpms=" + std::string(options.globalpms ? "true" : "false") + ",buddytracking=" + std::string(options.buddytracking ? "true" : "false") + ",showbuddies=" + std::string(options.showbuddies ? "true" : "false");
    rc_send_raw_packet(connection, PLI_SENDTEXT, payload.c_str(), static_cast<int>(payload.size()));
}

void TRemoteFrame::updateMassPMAcceptance() {
    if (connection == nullptr) return;
    const char state[] = {'A', options.nomassmessages ? '!' : ' '};
    rc_send_raw_packet(connection, PLI_PLAYERPROPS, state, sizeof(state));
}

void TRemoteFrame::setNCChannelVisible(bool visible) {
    const auto found = channelFields.find("NC");
    if (!visible) {
        if (found == channelFields.end()) return;
        GtkWidget* page = gtk_widget_get_parent(found->second);
        const int pageNumber = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), page);
        if (pageNumber >= 0) gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), pageNumber);
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
    GdkColor chatBackgroundColor;
    GdkColor chatColor;
    gdk_color_parse(options.colorchatback.c_str(), &chatBackgroundColor);
    gdk_color_parse(options.colorchat.c_str(), &chatColor);
    gtk_widget_modify_base(field, GTK_STATE_NORMAL, &chatBackgroundColor);
    gtk_widget_modify_text(field, GTK_STATE_NORMAL, &chatColor);
    PangoFontDescription* chatFont = pango_font_description_from_string(("Sans " + std::to_string(options.chatfontsize)).c_str());
    gtk_widget_modify_font(field, chatFont);
    pango_font_description_free(chatFont);
    GtkCssProvider* provider = gtk_css_provider_new();
    const std::string css = "textview, textview text { background-color: " + options.colorchatback + "; color: " + options.colorchat + "; }";
    gtk_css_provider_load_from_data(provider, css.c_str(), -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(field), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);
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
            GtkTextTag* hidden = gtk_text_buffer_create_tag(buffer, nullptr, "invisible", true, nullptr);
            gtk_text_buffer_apply_tag(buffer, hidden, &start, &end);
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
    const bool colorAlert = message.rfind("#ALERT", 0) == 0;
    std::string display = message;
    const bool alert = applyAlertTag(display, true);
    GtkWidget*& field = channelFields[channel];
    if (field == nullptr) {
        GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
        field = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(field), false);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(field), false);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(field), GTK_WRAP_WORD_CHAR);
        configureChatField(field);
        gtk_container_add(GTK_CONTAINER(scrolled), field);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new(channel.c_str()));
        gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(notebook), scrolled, false);
        gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), scrolled, true);
        gtk_widget_show_all(scrolled);
    }
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(field));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    const std::string timestamp = chatTimestamp(options);
    if (!timestamp.empty()) {
        gtk_text_buffer_insert(buffer, &end, (timestamp + " ").c_str(), -1);
        gtk_text_buffer_get_end_iter(buffer, &end);
    }
    const gint startOffset = gtk_text_iter_get_offset(&end);
    if (colorAlert) {
        GtkTextTag* tag = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.coloralert.c_str(), "weight", PANGO_WEIGHT_BOLD, nullptr);
        gtk_text_buffer_insert_with_tags(buffer, &end, (display + "\n").c_str(), -1, tag, nullptr);
    } else {
        const std::size_t separator = display.find(':');
        if (separator != std::string::npos && separator > 0) {
            const std::string prefix = display.substr(0, separator + 1);
            GtkTextTag* tag = gtk_text_buffer_create_tag(buffer, nullptr, "foreground", options.colorchatbold.c_str(), "weight", PANGO_WEIGHT_BOLD, nullptr);
            gtk_text_buffer_insert_with_tags(buffer, &end, prefix.c_str(), -1, tag, nullptr);
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
    if (alert && !hasActiveRemoteControlWindow()) {
        if (!hasActiveRemoteControlWindow()) gtk_window_set_urgency_hint(GTK_WINDOW(window), true);
        gdk_beep();
    }
    GtkTextIter scrollEnd;
    gtk_text_buffer_get_end_iter(buffer, &scrollEnd);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(field), &scrollEnd, 0.0, false, 0.0, 1.0);
    GtkWidget* scrolled = gtk_widget_get_parent(field);
    if (GTK_IS_SCROLLED_WINDOW(scrolled)) {
        GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
        gtk_adjustment_set_value(adjustment, gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment));
    }
}

void TRemoteFrame::removeChannel(const std::string& channel) {
    const auto found = channelFields.find(channel);
    if (found == channelFields.end()) return;
    GtkWidget* scrolled = gtk_widget_get_parent(found->second);
    const int page = scrolled == nullptr ? -1 : gtk_notebook_page_num(GTK_NOTEBOOK(notebook), scrolled);
    if (page != -1) gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), page);
    channelFields.erase(found);
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
    if (message == "/rnc") {
        reconnectNPCServer();
        gtk_entry_set_text(GTK_ENTRY(editField), "");
        return;
    }
    if (message == "/dnc") {
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
    if (!rc_execute(connection, message.c_str())) appendChat(rc_last_error(connection));
    gtk_entry_set_text(GTK_ENTRY(editField), "");
}

void TRemoteFrame::reconnectNPCServer() {
    if (connection == nullptr) return;
    if (rc_is_nc_connected(connection) != 0) rc_disconnect_nc(connection);
    ncConnectionAttempted = true;
    if (!rc_connect_to_nc_server(connection)) appendChat(rc_last_error(connection));
}

void TRemoteFrame::disconnectNPCServer() {
    if (connection == nullptr) return;
    if (!rc_disconnect_nc(connection)) appendChat(rc_last_error(connection));
    ncConnectionAttempted = true;
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
