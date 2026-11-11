#include "TStatusNotifier.h"

#if defined(__linux__)
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <unistd.h>
#include <utility>

// GtkStatusIcon is XEmbed only, which GNOME (Wayland and X11) no longer shows. This publishes the same icon as a
// StatusNotifierItem with a dbusmenu menu, which the AppIndicator extension and KDE render.
namespace RC::StatusNotifier {
namespace {
constexpr const char* itemPath = "/StatusNotifierItem";
constexpr const char* menuPath = "/MenuBar";
constexpr const char* itemInterface = "org.kde.StatusNotifierItem";
constexpr const char* menuInterface = "com.canonical.dbusmenu";
constexpr const char* watcherName = "org.kde.StatusNotifierWatcher";

constexpr const char* itemXml =
    "<node><interface name='org.kde.StatusNotifierItem'>"
    "<property name='Category' type='s' access='read'/><property name='Id' type='s' access='read'/>"
    "<property name='Title' type='s' access='read'/><property name='Status' type='s' access='read'/>"
    "<property name='WindowId' type='u' access='read'/><property name='IconName' type='s' access='read'/>"
    "<property name='IconPixmap' type='a(iiay)' access='read'/><property name='OverlayIconName' type='s' access='read'/>"
    "<property name='OverlayIconPixmap' type='a(iiay)' access='read'/><property name='AttentionIconName' type='s' access='read'/>"
    "<property name='AttentionIconPixmap' type='a(iiay)' access='read'/><property name='AttentionMovieName' type='s' access='read'/>"
    "<property name='ToolTip' type='(sa(iiay)ss)' access='read'/><property name='ItemIsMenu' type='b' access='read'/>"
    "<property name='Menu' type='o' access='read'/>"
    "<method name='ContextMenu'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
    "<method name='Activate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
    "<method name='SecondaryActivate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
    "<method name='Scroll'><arg type='i' direction='in'/><arg type='s' direction='in'/></method>"
    "<signal name='NewIcon'/><signal name='NewToolTip'/><signal name='NewStatus'><arg type='s'/></signal>"
    "</interface></node>";

constexpr const char* menuXml =
    "<node><interface name='com.canonical.dbusmenu'>"
    "<property name='Version' type='u' access='read'/><property name='TextDirection' type='s' access='read'/>"
    "<property name='Status' type='s' access='read'/><property name='IconThemePath' type='as' access='read'/>"
    "<method name='GetLayout'><arg type='i' direction='in'/><arg type='i' direction='in'/><arg type='as' direction='in'/>"
    "<arg type='u' direction='out'/><arg type='(ia{sv}av)' direction='out'/></method>"
    "<method name='GetGroupProperties'><arg type='ai' direction='in'/><arg type='as' direction='in'/><arg type='a(ia{sv})' direction='out'/></method>"
    "<method name='GetProperty'><arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='out'/></method>"
    "<method name='Event'><arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='in'/><arg type='u' direction='in'/></method>"
    "<method name='EventGroup'><arg type='a(isvu)' direction='in'/><arg type='ai' direction='out'/></method>"
    "<method name='AboutToShow'><arg type='i' direction='in'/><arg type='b' direction='out'/></method>"
    "<method name='AboutToShowGroup'><arg type='ai' direction='in'/><arg type='ai' direction='out'/><arg type='ai' direction='out'/></method>"
    "<signal name='ItemsPropertiesUpdated'><arg type='a(ia{sv})'/><arg type='a(ia{sv})'/></signal>"
    "<signal name='LayoutUpdated'><arg type='u'/><arg type='i'/></signal>"
    "<signal name='ItemActivationRequested'><arg type='i'/><arg type='u'/></signal>"
    "</interface></node>";

struct State {
    GDBusConnection* connection = nullptr;
    GDBusNodeInfo* itemInfo = nullptr;
    GDBusNodeInfo* menuInfo = nullptr;
    guint itemRegistration = 0;
    guint menuRegistration = 0;
    guint ownerId = 0;
    guint watchId = 0;
    std::string busName;
    std::string tooltip;
    GVariant* iconPixmap = nullptr;
    Handlers handlers;
    guint revision = 1;
    bool running = false;
};
State state;

GVariant* emptyPixmaps() { return g_variant_new_array(G_VARIANT_TYPE("(iiay)"), nullptr, 0); }

GVariant* buildPixmaps(const std::string& path) {
    GVariantBuilder sizes;
    g_variant_builder_init(&sizes, G_VARIANT_TYPE("a(iiay)"));
    GdkPixbuf* source = gdk_pixbuf_new_from_file(path.c_str(), nullptr);
    if (source != nullptr) {
        for (const int size : {22, 32, 48}) {
            GdkPixbuf* scaled = gdk_pixbuf_scale_simple(source, size, size, GDK_INTERP_BILINEAR);
            if (scaled == nullptr) continue;
            GdkPixbuf* withAlpha = gdk_pixbuf_get_has_alpha(scaled) ? static_cast<GdkPixbuf*>(g_object_ref(scaled)) : gdk_pixbuf_add_alpha(scaled, FALSE, 0, 0, 0);
            const int rowstride = gdk_pixbuf_get_rowstride(withAlpha);
            const guchar* pixels = gdk_pixbuf_read_pixels(withAlpha);
            std::vector<guchar> argb(static_cast<std::size_t>(size) * size * 4);
            for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
                const guchar* in = pixels + y * rowstride + x * 4;
                guchar* out = argb.data() + (y * size + x) * 4;
                out[0] = in[3]; out[1] = in[0]; out[2] = in[1]; out[3] = in[2];
            }
            GVariant* bytes = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, argb.data(), argb.size(), 1);
            g_variant_builder_add(&sizes, "(ii@ay)", size, size, bytes);
            g_object_unref(withAlpha);
            g_object_unref(scaled);
        }
        g_object_unref(source);
    }
    return g_variant_ref_sink(g_variant_builder_end(&sizes));
}

GVariant* itemProperty(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* name, GError** error, gpointer) {
    const std::string property = name;
    if (property == "Category") return g_variant_new_string("ApplicationStatus");
    if (property == "Id") return g_variant_new_string("remote-control");
    if (property == "Title") return g_variant_new_string("Remote Control");
    if (property == "Status") return g_variant_new_string("Active");
    if (property == "WindowId") return g_variant_new_uint32(0);
    if (property == "IconName" || property == "OverlayIconName" || property == "AttentionIconName" || property == "AttentionMovieName") return g_variant_new_string("");
    if (property == "IconPixmap") return state.iconPixmap != nullptr ? g_variant_ref(state.iconPixmap) : emptyPixmaps();
    if (property == "OverlayIconPixmap" || property == "AttentionIconPixmap") return emptyPixmaps();
    if (property == "ToolTip") return g_variant_new("(s@a(iiay)ss)", "", emptyPixmaps(), state.tooltip.c_str(), "");
    if (property == "ItemIsMenu") return g_variant_new_boolean(FALSE);
    if (property == "Menu") return g_variant_new_object_path(menuPath);
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", name);
    return nullptr;
}

void itemMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method, GVariant*, GDBusMethodInvocation* invocation, gpointer) {
    g_dbus_method_invocation_return_value(invocation, nullptr);
    if (std::string(method) == "Activate" && state.handlers.activate) state.handlers.activate();
}

GVariant* entryProperties(const MenuEntry& entry) {
    GVariantBuilder properties;
    g_variant_builder_init(&properties, G_VARIANT_TYPE("a{sv}"));
    if (entry.separator) g_variant_builder_add(&properties, "{sv}", "type", g_variant_new_string("separator"));
    else {
        g_variant_builder_add(&properties, "{sv}", "label", g_variant_new_string(entry.label.c_str()));
        g_variant_builder_add(&properties, "{sv}", "enabled", g_variant_new_boolean(entry.enabled));
    }
    return g_variant_builder_end(&properties);
}

std::vector<MenuEntry> currentMenu() { return state.handlers.menu ? state.handlers.menu() : std::vector<MenuEntry>(); }

GVariant* menuLayout() {
    GVariantBuilder children;
    g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
    for (const MenuEntry& entry : currentMenu()) {
        GVariant* node = g_variant_new("(i@a{sv}@av)", entry.id, entryProperties(entry), g_variant_new_array(G_VARIANT_TYPE("v"), nullptr, 0));
        g_variant_builder_add(&children, "v", node);
    }
    GVariantBuilder rootProperties;
    g_variant_builder_init(&rootProperties, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&rootProperties, "{sv}", "children-display", g_variant_new_string("submenu"));
    return g_variant_new("(i@a{sv}@av)", 0, g_variant_builder_end(&rootProperties), g_variant_builder_end(&children));
}

void menuMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method, GVariant* parameters, GDBusMethodInvocation* invocation, gpointer) {
    const std::string name = method;
    if (name == "GetLayout") {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u@(ia{sv}av))", state.revision++, menuLayout()));
    } else if (name == "GetGroupProperties") {
        GVariantBuilder list;
        g_variant_builder_init(&list, G_VARIANT_TYPE("a(ia{sv})"));
        for (const MenuEntry& entry : currentMenu()) g_variant_builder_add(&list, "(i@a{sv})", entry.id, entryProperties(entry));
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&list)));
    } else if (name == "GetProperty") {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(v)", g_variant_new_string("")));
    } else if (name == "Event") {
        gint32 id = 0;
        const gchar* eventId = nullptr;
        GVariant* data = nullptr;
        guint32 timestamp = 0;
        g_variant_get(parameters, "(i&svu)", &id, &eventId, &data, &timestamp);
        const bool clicked = eventId != nullptr && std::string(eventId) == "clicked";
        g_variant_unref(data);
        g_dbus_method_invocation_return_value(invocation, nullptr);
        if (clicked && state.handlers.menuActivated) state.handlers.menuActivated(id);
    } else if (name == "EventGroup") {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai)", g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0)));
    } else if (name == "AboutToShow") {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    } else if (name == "AboutToShowGroup") {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai@ai)", g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0), g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0)));
    } else g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.UnknownMethod", "Unknown method");
}

GVariant* menuProperty(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* name, GError** error, gpointer) {
    const std::string property = name;
    if (property == "Version") return g_variant_new_uint32(3);
    if (property == "TextDirection") return g_variant_new_string("ltr");
    if (property == "Status") return g_variant_new_string("normal");
    if (property == "IconThemePath") return g_variant_new_strv(nullptr, 0);
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", name);
    return nullptr;
}

void registerWithWatcher(GDBusConnection* connection, const gchar*, const gchar*, gpointer) {
    g_dbus_connection_call(connection, watcherName, "/StatusNotifierWatcher", watcherName, "RegisterStatusNotifierItem", g_variant_new("(s)", state.busName.c_str()), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
}

void nameAcquired(GDBusConnection* connection, const gchar*, gpointer) {
    if (state.watchId == 0) state.watchId = g_bus_watch_name_on_connection(connection, watcherName, G_BUS_NAME_WATCHER_FLAGS_NONE, registerWithWatcher, nullptr, nullptr, nullptr);
}

void emitSignal(const char* name) {
    if (state.connection != nullptr) g_dbus_connection_emit_signal(state.connection, nullptr, itemPath, itemInterface, name, nullptr, nullptr);
}
}

bool start(const std::string& iconPath, const std::string& tooltip, Handlers handlers) {
    if (state.running) return true;
    GError* error = nullptr;
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (connection == nullptr) { if (error != nullptr) g_error_free(error); return false; }
    GVariant* owned = g_dbus_connection_call_sync(connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner", g_variant_new("(s)", watcherName), G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
    gboolean hasWatcher = FALSE;
    if (owned != nullptr) { g_variant_get(owned, "(b)", &hasWatcher); g_variant_unref(owned); }
    if (!hasWatcher) { g_object_unref(connection); return false; }
    state.itemInfo = g_dbus_node_info_new_for_xml(itemXml, nullptr);
    state.menuInfo = g_dbus_node_info_new_for_xml(menuXml, nullptr);
    static const GDBusInterfaceVTable itemTable = {itemMethod, itemProperty, nullptr, {}};
    static const GDBusInterfaceVTable menuTable = {menuMethod, menuProperty, nullptr, {}};
    state.itemRegistration = g_dbus_connection_register_object(connection, itemPath, state.itemInfo->interfaces[0], &itemTable, nullptr, nullptr, nullptr);
    state.menuRegistration = g_dbus_connection_register_object(connection, menuPath, state.menuInfo->interfaces[0], &menuTable, nullptr, nullptr, nullptr);
    if (state.itemRegistration == 0 || state.menuRegistration == 0) {
        if (state.itemRegistration != 0) g_dbus_connection_unregister_object(connection, state.itemRegistration);
        if (state.menuRegistration != 0) g_dbus_connection_unregister_object(connection, state.menuRegistration);
        g_dbus_node_info_unref(state.itemInfo);
        g_dbus_node_info_unref(state.menuInfo);
        state.itemRegistration = state.menuRegistration = 0;
        g_object_unref(connection);
        return false;
    }
    state.connection = connection;
    state.handlers = std::move(handlers);
    state.tooltip = tooltip;
    state.iconPixmap = buildPixmaps(iconPath);
    state.busName = "org.kde.StatusNotifierItem-" + std::to_string(getpid()) + "-1";
    state.ownerId = g_bus_own_name_on_connection(connection, state.busName.c_str(), G_BUS_NAME_OWNER_FLAGS_NONE, nameAcquired, nullptr, nullptr, nullptr);
    state.running = true;
    return true;
}

bool active() { return state.running; }

void setIcon(const std::string& iconPath) {
    if (!state.running) return;
    GVariant* previous = state.iconPixmap;
    state.iconPixmap = buildPixmaps(iconPath);
    if (previous != nullptr) g_variant_unref(previous);
    emitSignal("NewIcon");
}

void setTooltip(const std::string& tooltip) {
    if (!state.running) return;
    state.tooltip = tooltip;
    emitSignal("NewToolTip");
}

void stop() {
    if (!state.running) return;
    if (state.watchId != 0) g_bus_unwatch_name(state.watchId);
    if (state.ownerId != 0) g_bus_unown_name(state.ownerId);
    g_dbus_connection_unregister_object(state.connection, state.itemRegistration);
    g_dbus_connection_unregister_object(state.connection, state.menuRegistration);
    g_dbus_node_info_unref(state.itemInfo);
    g_dbus_node_info_unref(state.menuInfo);
    if (state.iconPixmap != nullptr) g_variant_unref(state.iconPixmap);
    g_object_unref(state.connection);
    state = State();
}
}

#else

namespace RC::StatusNotifier {
bool start(const std::string&, const std::string&, Handlers) { return false; }
bool active() { return false; }
void setIcon(const std::string&) {}
void setTooltip(const std::string&) {}
void stop() {}
}

#endif
