#include "tray.hpp"

#include <spdlog/spdlog.h>

#include <cstring>
#include <string>
#include <chrono>
#include <unistd.h>

static constexpr const char *kWatcherBusName = "org.kde.StatusNotifierWatcher";
static constexpr const char *kIfaceDBus = "org.freedesktop.DBus";
static constexpr const char *kPathDBus = "/org/freedesktop/DBus";

static constexpr const char *kObjPath = "/StatusNotifierItem";
static constexpr const char *kMenuPath = "/StatusNotifierItem/Menu";
static constexpr const char *kIfaceSNI = "org.kde.StatusNotifierItem";
static constexpr const char *kIfaceProps = "org.freedesktop.DBus.Properties";
static constexpr const char *kIfaceIntro = "org.freedesktop.DBus.Introspectable";
static constexpr const char *kIfaceMenu = "com.canonical.dbusmenu";

// ─────────────────────────────────────
static constexpr dbus_int32_t kMenuRootId = 0;
static constexpr dbus_int32_t kMenuOpenUiId = 1;
static constexpr dbus_int32_t kMenuExitId = 2;

// ─────────────────────────────────────
static void append_variant_string(DBusMessageIter *iter, const char *value) {
    DBusMessageIter variant;
    const char *sig = "s";
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, sig, &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(iter, &variant);
}

// ─────────────────────────────────────
static void append_variant_bool(DBusMessageIter *iter, dbus_bool_t value) {
    DBusMessageIter variant;
    const char *sig = "b";
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, sig, &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
    dbus_message_iter_close_container(iter, &variant);
}

// ─────────────────────────────────────
static void append_variant_object_path(DBusMessageIter *iter, const char *value) {
    DBusMessageIter variant;
    const char *sig = "o";
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, sig, &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_OBJECT_PATH, &value);
    dbus_message_iter_close_container(iter, &variant);
}

// ─────────────────────────────────────
static void dict_append_string(DBusMessageIter *dictIter, const char *key, const char *value) {
    DBusMessageIter entry;
    dbus_message_iter_open_container(dictIter, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    append_variant_string(&entry, value);
    dbus_message_iter_close_container(dictIter, &entry);
}

// ─────────────────────────────────────
static void dict_append_bool(DBusMessageIter *dictIter, const char *key, dbus_bool_t value) {
    DBusMessageIter entry;
    dbus_message_iter_open_container(dictIter, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    append_variant_bool(&entry, value);
    dbus_message_iter_close_container(dictIter, &entry);
}

// ─────────────────────────────────────
static void dict_append_object_path(DBusMessageIter *dictIter, const char *key, const char *value) {
    DBusMessageIter entry;
    dbus_message_iter_open_container(dictIter, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    append_variant_object_path(&entry, value);
    dbus_message_iter_close_container(dictIter, &entry);
}

// ─────────────────────────────────────
static void menu_append_properties(DBusMessageIter *props, dbus_int32_t id) {
    dict_append_bool(props, "visible", true);
    dict_append_bool(props, "enabled", true);
    if (id == kMenuRootId) {
        dict_append_string(props, "children-display", "submenu");
        return;
    }
    if (id == kMenuOpenUiId) {
        dict_append_string(props, "label", "Open Web UI");
        dict_append_string(props, "type", "standard");
        return;
    }
    if (id == kMenuExitId) {
        dict_append_string(props, "label", "Exit");
        dict_append_string(props, "type", "standard");
        return;
    }
}

// ─────────────────────────────────────
static void menu_append_layout_node(DBusMessageIter *out, dbus_int32_t id,
                                    bool includeChildrenForNode) {
    DBusMessageIter nodeStruct;
    dbus_message_iter_open_container(out, DBUS_TYPE_STRUCT, nullptr, &nodeStruct);

    dbus_message_iter_append_basic(&nodeStruct, DBUS_TYPE_INT32, &id);

    DBusMessageIter props;
    dbus_message_iter_open_container(&nodeStruct, DBUS_TYPE_ARRAY, "{sv}", &props);
    menu_append_properties(&props, id);
    dbus_message_iter_close_container(&nodeStruct, &props);

    DBusMessageIter children;
    dbus_message_iter_open_container(&nodeStruct, DBUS_TYPE_ARRAY, "v", &children);

    if (includeChildrenForNode && id == kMenuRootId) {
        auto append_child_variant = [&](dbus_int32_t childId) {
            DBusMessageIter childVar;
            const char *variantSig = "(ia{sv}av)";
            dbus_message_iter_open_container(&children, DBUS_TYPE_VARIANT, variantSig, &childVar);
            menu_append_layout_node(&childVar, childId, false);
            dbus_message_iter_close_container(&children, &childVar);
        };

        append_child_variant(kMenuOpenUiId);
        append_child_variant(kMenuExitId);
    }

    dbus_message_iter_close_container(&nodeStruct, &children);
    dbus_message_iter_close_container(out, &nodeStruct);
}

// ─────────────────────────────────────
TrayIcon::~TrayIcon() {
    m_StopDispatch.store(true);
    if (m_DispatchThread.joinable()) {
        m_DispatchThread.join();
    }

    DBusConnection *conn = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_ConnMutex);
        conn = m_Conn;
        m_Conn = nullptr;
    }
    TeardownConnection(conn);
}

// ─────────────────────────────────────
bool TrayIcon::Start(std::string title) {
    if (m_Started) {
        return true;
    }

    m_Title = std::move(title);

    // We may send DBus messages from the main thread while a dispatcher thread is running.
    // Enable libdbus internal locking.
    if (!dbus_threads_init_default()) {
        spdlog::warn("Tray: dbus_threads_init_default failed; tray may be unstable");
    }

    // Build a well-known name that won't collide (bus name elements can't start with a digit).
    // Keep it stable across reconnects so the watcher can re-associate us.
    const pid_t pid = getpid();
    m_BusName = "io.Concentrate.Tray.P" + std::to_string(static_cast<int>(pid));

    if (!SetupConnection()) {
        // If we cannot connect at all, behave as before: fail Start().
        return false;
    }

    // Initial registration.
    // Note: Waybar may restart its StatusNotifierWatcher later (e.g. after resume); we handle
    // that via NameOwnerChanged + reconnect logic in the dispatcher thread.
    RegisterWithWatcher();
    m_Started = true;

    // Dispatch DBus continuously so tray method calls don't time out while the app sleeps.
    m_StopDispatch.store(false);
    m_DispatchThread = std::thread([this]() {
        // Reconnect backoff: keep the thread alive across suspend/resume.
        // We don't rely on Waybar polling; we re-export and re-register when needed.
        auto nextAttempt = std::chrono::steady_clock::now();
        while (!m_StopDispatch.load()) {
            DBusConnection *conn = AcquireConnRef();
            if (!conn) {
                // Connection missing (e.g. after teardown). Try to reconnect periodically.
                const auto now = std::chrono::steady_clock::now();
                if (now >= nextAttempt) {
                    (void)Reconnect("no session connection");
                    nextAttempt = now + std::chrono::seconds(2);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            if (!dbus_connection_get_is_connected(conn)) {
                dbus_connection_unref(conn);
                const auto now = std::chrono::steady_clock::now();
                if (now >= nextAttempt) {
                    (void)Reconnect("dbus disconnected");
                    nextAttempt = now + std::chrono::seconds(2);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            // Wait a little for IO, then dispatch any pending messages.
            // This dispatch also delivers NameOwnerChanged to our filter.
            dbus_connection_read_write_dispatch(conn, 250);
            dbus_connection_unref(conn);

            // If nothing is happening, avoid a hot loop.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    spdlog::info("Tray: StatusNotifierItem exported as {}{}", m_BusName, kObjPath);
    return true;
}

// ─────────────────────────────────────
void TrayIcon::Poll() {
    DBusConnection *conn = AcquireConnRef();
    if (!conn) {
        return;
    }

    // Kept for API compatibility: do a quick non-blocking pump.
    // The dedicated dispatcher thread is the primary mechanism.
    dbus_connection_read_write_dispatch(conn, 0);
    dbus_connection_unref(conn);
}

// ─────────────────────────────────────
bool TrayIcon::TakeOpenUiRequested() {
    return m_OpenUiRequested.exchange(false);
}

// ─────────────────────────────────────
bool TrayIcon::TakeExitRequested() {
    return m_ExitRequested.exchange(false);
}

// ─────────────────────────────────────
void TrayIcon::SetTrayIcon(FocusState state) {
    if (!m_Started) {
        return;
    }
    if (state == m_FocusState) {
        return;
    }

    m_FocusState = state;
    switch (state) {
    case IDLE: {
        m_IconName = "concentrate";
        break;
    }
    case DISABLE: {
        m_IconName = "concentrate-off";
        break;
    }
    case FOCUSED: {
        m_IconName = "concentrate-focused";
        break;
    }
    case UNFOCUSED: {
        m_IconName = "concentrate-unfocused";
        break;
    }
    }

    EmitNewIcon();
    EmitPropertiesChangedIconName();
}

// ─────────────────────────────────────
DBusHandlerResult TrayIcon::FilterHandler(DBusConnection *conn, DBusMessage *msg, void *user_data) {
    auto *self = static_cast<TrayIcon *>(user_data);
    if (!self) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    return self->HandleFilter(conn, msg);
}

// ─────────────────────────────────────
DBusHandlerResult TrayIcon::HandleFilter(DBusConnection *, DBusMessage *msg) {
    // Watcher lifecycle handling:
    // Waybar often restarts its StatusNotifierWatcher on resume. When that happens, it forgets
    // previously registered items. We subscribe to NameOwnerChanged for the watcher name so we
    // can re-register ourselves immediately.
    if (!dbus_message_is_signal(msg, kIfaceDBus, "NameOwnerChanged")) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    const char *name = nullptr;
    const char *old_owner = nullptr;
    const char *new_owner = nullptr;

    DBusError err;
    dbus_error_init(&err);
    if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old_owner,
                               DBUS_TYPE_STRING, &new_owner, DBUS_TYPE_INVALID)) {
        if (dbus_error_is_set(&err)) {
            dbus_error_free(&err);
        }
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (!name || std::strcmp(name, kWatcherBusName) != 0) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // Watcher appears when new_owner becomes non-empty.
    if (new_owner && *new_owner) {
        spdlog::info("Tray: StatusNotifierWatcher appeared (re-registering SNI)");
        RegisterWithWatcher();

        // Some hosts only refresh if NewIcon / PropertiesChanged are emitted after registration.
        EmitNewIcon();
        EmitPropertiesChangedIconName();
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

// ─────────────────────────────────────
DBusHandlerResult TrayIcon::MessageHandler(DBusConnection *conn, DBusMessage *msg,
                                           void *user_data) {
    auto *self = static_cast<TrayIcon *>(user_data);
    if (!self) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    return self->HandleMessage(conn, msg);
}

// ─────────────────────────────────────
DBusHandlerResult TrayIcon::HandleMessage(DBusConnection *conn, DBusMessage *msg) {
    const char *path = dbus_message_get_path(msg);
    const bool isMenu = (path && std::strcmp(path, kMenuPath) == 0);

    // Introspection
    if (dbus_message_is_method_call(msg, kIfaceIntro, "Introspect")) {
        if (isMenu) {
            ReplyMenuIntrospect(conn, msg);
        } else {
            ReplyIntrospect(conn, msg);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    // com.canonical.dbusmenu
    if (isMenu) {
        if (dbus_message_is_method_call(msg, kIfaceMenu, "GetLayout")) {
            ReplyMenuGetLayout(conn, msg);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_is_method_call(msg, kIfaceMenu, "GetGroupProperties")) {
            ReplyMenuGetGroupProperties(conn, msg);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_is_method_call(msg, kIfaceMenu, "GetProperty")) {
            ReplyMenuGetProperty(conn, msg);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_is_method_call(msg, kIfaceMenu, "Event")) {
            ReplyMenuEvent(conn, msg);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_is_method_call(msg, kIfaceMenu, "EventGroup")) {
            ReplyMenuEventGroup(conn, msg);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_is_method_call(msg, kIfaceMenu, "AboutToShow")) {
            ReplyMenuAboutToShow(conn, msg);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_is_method_call(msg, kIfaceMenu, "AboutToShowGroup")) {
            ReplyMenuAboutToShowGroup(conn, msg);
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // Properties
    if (dbus_message_is_method_call(msg, kIfaceProps, "Get")) {
        ReplyGetProperty(conn, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_method_call(msg, kIfaceProps, "GetAll")) {
        ReplyGetAllProperties(conn, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    // Click handling (optional). Reply so the caller doesn't see an error.
    if (dbus_message_is_method_call(msg, kIfaceSNI, "Activate") ||
        dbus_message_is_method_call(msg, kIfaceSNI, "SecondaryActivate") ||
        dbus_message_is_method_call(msg, kIfaceSNI, "ContextMenu") ||
        dbus_message_is_method_call(msg, kIfaceSNI, "Scroll")) {
        if (dbus_message_is_method_call(msg, kIfaceSNI, "Activate")) {
            m_OpenUiRequested.store(true);
        }
        // ContextMenu / SecondaryActivate should be handled by the host by showing our
        // DBusMenu from the Menu property. We intentionally do not trigger actions here.
        ReplyEmptyMethodReturn(conn, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

// ─────────────────────────────────────
void TrayIcon::RegisterWithWatcher() {
    DBusConnection *conn = AcquireConnRef();
    if (!conn) {
        return;
    }

    DBusMessage *msg =
        dbus_message_new_method_call("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                                     "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem");
    if (!msg) {
        return;
    }

    const char *name = m_BusName.c_str();
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);

    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(conn, msg, 1000, &err);
    if (!reply) {
        // Watcher may not exist; that's fine.
        if (dbus_error_is_set(&err)) {
            spdlog::debug("Tray: watcher registration failed: {}", err.message);
            dbus_error_free(&err);
        }
    } else {
        dbus_message_unref(reply);
    }

    dbus_message_unref(msg);
    dbus_connection_flush(conn);
    dbus_connection_unref(conn);
}

// ─────────────────────────────────────
void TrayIcon::EmitNewIcon() {
    DBusConnection *conn = AcquireConnRef();
    if (!conn) {
        return;
    }
    DBusMessage *sig = dbus_message_new_signal(kObjPath, kIfaceSNI, "NewIcon");
    if (!sig) {
        dbus_connection_unref(conn);
        return;
    }
    dbus_connection_send(conn, sig, nullptr);
    dbus_message_unref(sig);
    dbus_connection_flush(conn);
    dbus_connection_unref(conn);
}

// ─────────────────────────────────────
void TrayIcon::EmitPropertiesChangedIconName() {
    DBusConnection *conn = AcquireConnRef();
    if (!conn) {
        return;
    }

    // org.freedesktop.DBus.Properties.PropertiesChanged(s interface_name, a{sv} changed, as
    // invalidated)
    DBusMessage *sig = dbus_message_new_signal(kObjPath, kIfaceProps, "PropertiesChanged");
    if (!sig) {
        dbus_connection_unref(conn);
        return;
    }

    DBusMessageIter iter;
    dbus_message_iter_init_append(sig, &iter);

    const char *iface = kIfaceSNI;
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface);

    DBusMessageIter dict;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
    dict_append_string(&dict, "IconName", m_IconName.c_str());
    dbus_message_iter_close_container(&iter, &dict);

    DBusMessageIter invalidated;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &invalidated);
    dbus_message_iter_close_container(&iter, &invalidated);

    dbus_connection_send(conn, sig, nullptr);
    dbus_message_unref(sig);
    dbus_connection_flush(conn);
    dbus_connection_unref(conn);
}

// ─────────────────────────────────────
DBusConnection *TrayIcon::AcquireConnRef() {
    std::lock_guard<std::mutex> lk(m_ConnMutex);
    if (!m_Conn) {
        return nullptr;
    }
    dbus_connection_ref(m_Conn);
    return m_Conn;
}

// ─────────────────────────────────────
void TrayIcon::AddWatcherOwnerChangedMatch(DBusConnection *conn) {
    if (!conn) {
        return;
    }

    // Requirement: match org.freedesktop.DBus.NameOwnerChanged filtered to the watcher.
    // This is the canonical way to detect watcher restarts (e.g. Waybar on resume).
    const char *rule =
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='org.kde.StatusNotifierWatcher'";

    DBusError err;
    dbus_error_init(&err);
    dbus_bus_add_match(conn, rule, &err);
    if (dbus_error_is_set(&err)) {
        spdlog::warn("Tray: failed to add NameOwnerChanged match: {}", err.message);
        dbus_error_free(&err);
    }

    dbus_connection_flush(conn);
}

// ─────────────────────────────────────
bool TrayIcon::SetupConnection() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn) {
        if (dbus_error_is_set(&err)) {
            spdlog::warn("Tray: DBus session connection failed: {}", err.message);
            dbus_error_free(&err);
        } else {
            spdlog::warn("Tray: DBus session connection failed");
        }
        return false;
    }

    // Don't let libdbus terminate the whole process if the bus disconnects.
    dbus_connection_set_exit_on_disconnect(conn, FALSE);

    const int req =
        dbus_bus_request_name(conn, m_BusName.c_str(), DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    if (req != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        if (dbus_error_is_set(&err)) {
            spdlog::warn("Tray: request_name failed: {}", err.message);
            dbus_error_free(&err);
        } else {
            spdlog::warn("Tray: request_name failed");
        }
        dbus_connection_unref(conn);
        return false;
    }

    // Attach a filter before registering so we don't miss NameOwnerChanged during resume storms.
    if (!dbus_connection_add_filter(conn, &TrayIcon::FilterHandler, this, nullptr)) {
        spdlog::warn("Tray: failed to add DBus filter; watcher restart handling may be limited");
    }
    AddWatcherOwnerChangedMatch(conn);

    static DBusObjectPathVTable vtable{};
    vtable.message_function = &TrayIcon::MessageHandler;

    if (!dbus_connection_register_object_path(conn, kObjPath, &vtable, this)) {
        spdlog::warn("Tray: failed to register object path {}", kObjPath);
        dbus_connection_unref(conn);
        return false;
    }

    if (!dbus_connection_register_object_path(conn, kMenuPath, &vtable, this)) {
        spdlog::warn("Tray: failed to register menu object path {}", kMenuPath);
        dbus_connection_unregister_object_path(conn, kObjPath);
        dbus_connection_unref(conn);
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(m_ConnMutex);
        m_Conn = conn;
    }

    return true;
}

// ─────────────────────────────────────
void TrayIcon::TeardownConnection(DBusConnection *conn) {
    if (!conn) {
        return;
    }

    // Clean teardown required for reconnect robustness.
    // Suspend/resume may leave the old connection in a half-dead state; explicitly unexport.
    dbus_connection_unregister_object_path(conn, kMenuPath);
    dbus_connection_unregister_object_path(conn, kObjPath);
    dbus_connection_unref(conn);
}

// ─────────────────────────────────────
bool TrayIcon::Reconnect(const char *reason) {
    // Requirement: detect disconnect and recreate the DBus connection, re-request the name,
    // re-register object paths, and re-register with the watcher.
    spdlog::warn("Tray: reconnecting DBus session ({})", reason ? reason : "unknown");

    DBusConnection *oldConn = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_ConnMutex);
        oldConn = m_Conn;
        m_Conn = nullptr;
    }
    TeardownConnection(oldConn);

    if (!SetupConnection()) {
        spdlog::warn("Tray: reconnect failed; will retry");
        return false;
    }

    // After reconnect, force a re-register. This is also safe if the watcher is already up.
    RegisterWithWatcher();
    EmitNewIcon();
    EmitPropertiesChangedIconName();

    spdlog::info("Tray: reconnect complete");
    return true;
}

// ─────────────────────────────────────
void TrayIcon::ReplyIntrospect(DBusConnection *conn, DBusMessage *msg) {
    static const char *xml = "<node>"
                             " <interface name='org.freedesktop.DBus.Introspectable'>"
                             "  <method name='Introspect'>"
                             "   <arg name='xml_data' type='s' direction='out'/>"
                             "  </method>"
                             " </interface>"
                             " <interface name='org.freedesktop.DBus.Properties'>"
                             "  <method name='Get'>"
                             "   <arg name='interface' type='s' direction='in'/>"
                             "   <arg name='prop' type='s' direction='in'/>"
                             "   <arg name='value' type='v' direction='out'/>"
                             "  </method>"
                             "  <method name='GetAll'>"
                             "   <arg name='interface' type='s' direction='in'/>"
                             "   <arg name='props' type='a{sv}' direction='out'/>"
                             "  </method>"
                             "  <signal name='PropertiesChanged'>"
                             "   <arg name='interface' type='s'/>"
                             "   <arg name='changed' type='a{sv}'/>"
                             "   <arg name='invalidated' type='as'/>"
                             "  </signal>"
                             " </interface>"
                             " <interface name='org.kde.StatusNotifierItem'>"
                             "  <property name='Category' type='s' access='read'/>"
                             "  <property name='Id' type='s' access='read'/>"
                             "  <property name='Title' type='s' access='read'/>"
                             "  <property name='Status' type='s' access='read'/>"
                             "  <property name='IconName' type='s' access='read'/>"
                             "  <property name='Menu' type='o' access='read'/>"
                             "  <property name='ItemIsMenu' type='b' access='read'/>"
                             "  <method name='Activate'>"
                             "   <arg name='x' type='i' direction='in'/>"
                             "   <arg name='y' type='i' direction='in'/>"
                             "  </method>"
                             "  <method name='SecondaryActivate'>"
                             "   <arg name='x' type='i' direction='in'/>"
                             "   <arg name='y' type='i' direction='in'/>"
                             "  </method>"
                             "  <method name='ContextMenu'>"
                             "   <arg name='x' type='i' direction='in'/>"
                             "   <arg name='y' type='i' direction='in'/>"
                             "  </method>"
                             "  <signal name='NewIcon'/>"
                             " </interface>"
                             "</node>";

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return;
    }

    dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

// ─────────────────────────────────────
void TrayIcon::ReplyMenuIntrospect(DBusConnection *conn, DBusMessage *msg) {
    static const char *xml = "<node>"
                             " <interface name='org.freedesktop.DBus.Introspectable'>"
                             "  <method name='Introspect'>"
                             "   <arg name='xml_data' type='s' direction='out'/>"
                             "  </method>"
                             " </interface>"
                             " <interface name='com.canonical.dbusmenu'>"
                             "  <method name='GetLayout'>"
                             "   <arg name='parentId' type='i' direction='in'/>"
                             "   <arg name='recursionDepth' type='i' direction='in'/>"
                             "   <arg name='propertyNames' type='as' direction='in'/>"
                             "   <arg name='revision' type='u' direction='out'/>"
                             "   <arg name='layout' type='(ia{sv}av)' direction='out'/>"
                             "  </method>"
                             "  <method name='GetGroupProperties'>"
                             "   <arg name='ids' type='ai' direction='in'/>"
                             "   <arg name='propertyNames' type='as' direction='in'/>"
                             "   <arg name='properties' type='a(ia{sv})' direction='out'/>"
                             "  </method>"
                             "  <method name='GetProperty'>"
                             "   <arg name='id' type='i' direction='in'/>"
                             "   <arg name='name' type='s' direction='in'/>"
                             "   <arg name='value' type='v' direction='out'/>"
                             "  </method>"
                             "  <method name='Event'>"
                             "   <arg name='id' type='i' direction='in'/>"
                             "   <arg name='eventId' type='s' direction='in'/>"
                             "   <arg name='data' type='v' direction='in'/>"
                             "   <arg name='timestamp' type='u' direction='in'/>"
                             "  </method>"
                             "  <method name='EventGroup'>"
                             "   <arg name='events' type='a(isvu)' direction='in'/>"
                             "  </method>"
                             "  <method name='AboutToShow'>"
                             "   <arg name='id' type='i' direction='in'/>"
                             "   <arg name='needUpdate' type='b' direction='out'/>"
                             "  </method>"
                             "  <method name='AboutToShowGroup'>"
                             "   <arg name='ids' type='ai' direction='in'/>"
                             "   <arg name='updatesNeeded' type='ai' direction='out'/>"
                             "   <arg name='idErrors' type='ai' direction='out'/>"
                             "  </method>"
                             "  <signal name='LayoutUpdated'>"
                             "   <arg name='revision' type='u'/>"
                             "   <arg name='parent' type='i'/>"
                             "  </signal>"
                             "  <signal name='ItemsPropertiesUpdated'>"
                             "   <arg name='updatedProps' type='a(ia{sv})'/>"
                             "   <arg name='removedProps' type='a(ias)'/>"
                             "  </signal>"
                             " </interface>"
                             "</node>";

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return;
    }

    dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

// ─────────────────────────────────────
const char *TrayIcon::GetPropString(const char *prop) const {
    if (std::strcmp(prop, "Category") == 0) {
        return "ApplicationStatus";
    }
    if (std::strcmp(prop, "Id") == 0) {
        return "concentrate";
    }
    if (std::strcmp(prop, "Title") == 0) {
        return m_Title.c_str();
    }
    if (std::strcmp(prop, "Status") == 0) {
        return "Active";
    }
    if (std::strcmp(prop, "IconName") == 0) {
        return m_IconName.c_str();
    }

    return "";
}

// ─────────────────────────────────────
void TrayIcon::ReplyGetProperty(DBusConnection *conn, DBusMessage *msg) {
    const char *iface = nullptr;
    const char *prop = nullptr;

    DBusError err;
    dbus_error_init(&err);

    if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop,
                               DBUS_TYPE_INVALID)) {
        if (dbus_error_is_set(&err)) {
            spdlog::debug("Tray: Properties.Get args error: {}", err.message);
            dbus_error_free(&err);
        }
        return;
    }

    // Only serve our SNI interface.
    if (!iface || std::strcmp(iface, kIfaceSNI) != 0) {
        return;
    }

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return;
    }

    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);

    const char *p = prop ? prop : "";
    if (std::strcmp(p, "Menu") == 0) {
        append_variant_object_path(&iter, kMenuPath);
    } else if (std::strcmp(p, "ItemIsMenu") == 0) {
        append_variant_bool(&iter, false);
    } else {
        const char *value = GetPropString(p);
        append_variant_string(&iter, value);
    }

    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

// ─────────────────────────────────────
void TrayIcon::ReplyGetAllProperties(DBusConnection *conn, DBusMessage *msg) {
    const char *iface = nullptr;

    DBusError err;
    dbus_error_init(&err);
    if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID)) {
        if (dbus_error_is_set(&err)) {
            dbus_error_free(&err);
        }
        return;
    }

    if (!iface || std::strcmp(iface, kIfaceSNI) != 0) {
        return;
    }

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return;
    }

    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);

    DBusMessageIter dict;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
    dict_append_string(&dict, "Category", GetPropString("Category"));
    dict_append_string(&dict, "Id", GetPropString("Id"));
    dict_append_string(&dict, "Title", GetPropString("Title"));
    dict_append_string(&dict, "Status", GetPropString("Status"));
    dict_append_string(&dict, "IconName", GetPropString("IconName"));
    dict_append_object_path(&dict, "Menu", kMenuPath);
    dict_append_bool(&dict, "ItemIsMenu", false);
    dbus_message_iter_close_container(&iter, &dict);

    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

// ─────────────────────────────────────
void TrayIcon::ReplyMenuGetLayout(DBusConnection *conn, DBusMessage *msg) {
    // GetLayout(i parentId, i recursionDepth, as propertyNames) -> (u (ia{sv}av))
    dbus_int32_t parentId = kMenuRootId;
    dbus_int32_t recursionDepth = 1;

    DBusError err;
    dbus_error_init(&err);
    dbus_message_get_args(msg, &err, DBUS_TYPE_INT32, &parentId, DBUS_TYPE_INT32, &recursionDepth,
                          DBUS_TYPE_INVALID);
    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
    }

    const bool includeChildren = (recursionDepth != 0);

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return;
    }

    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);

    const dbus_uint32_t revision = 1;
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &revision);

    menu_append_layout_node(&iter, parentId, includeChildren);

    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

// ─────────────────────────────────────
void TrayIcon::ReplyMenuGetGroupProperties(DBusConnection *conn, DBusMessage *msg) {
    // GetGroupProperties(ai ids, as propertyNames) -> a(ia{sv})
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return;
    }

    DBusMessageIter out;
    dbus_message_iter_init_append(reply, &out);

    DBusMessageIter array;
    dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "(ia{sv})", &array);

    DBusMessageIter args;
    if (dbus_message_iter_init(msg, &args) &&
        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY) {
        DBusMessageIter ids;
        dbus_message_iter_recurse(&args, &ids);
        while (dbus_message_iter_get_arg_type(&ids) == DBUS_TYPE_INT32) {
            dbus_int32_t id = 0;
            dbus_message_iter_get_basic(&ids, &id);

            DBusMessageIter entry;
            dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, nullptr, &entry);
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_INT32, &id);

            DBusMessageIter props;
            dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY, "{sv}", &props);
            menu_append_properties(&props, id);
            dbus_message_iter_close_container(&entry, &props);

            dbus_message_iter_close_container(&array, &entry);

            dbus_message_iter_next(&ids);
        }
    }

    dbus_message_iter_close_container(&out, &array);

    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

// ─────────────────────────────────────
void TrayIcon::ReplyMenuGetProperty(DBusConnection *conn, DBusMessage *msg) {
    // GetProperty(i id, s name) -> v
    dbus_int32_t id = 0;
    const char *name = nullptr;

    DBusError err;
    dbus_error_init(&err);
    if (!dbus_message_get_args(msg, &err, DBUS_TYPE_INT32, &id, DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_INVALID)) {
        if (dbus_error_is_set(&err)) {
            dbus_error_free(&err);
        }
        return;
    }

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return;
    }

    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);

    if (name && std::strcmp(name, "visible") == 0) {
        append_variant_bool(&iter, true);
    } else if (name && std::strcmp(name, "enabled") == 0) {
        append_variant_bool(&iter, true);
    } else if (name && std::strcmp(name, "label") == 0) {
        if (id == kMenuOpenUiId) {
            append_variant_string(&iter, "Open Web UI");
        } else if (id == kMenuExitId) {
            append_variant_string(&iter, "Exit");
        } else {
            append_variant_string(&iter, "");
        }
    } else if (name && std::strcmp(name, "children-display") == 0) {
        if (id == kMenuRootId) {
            append_variant_string(&iter, "submenu");
        } else {
            append_variant_string(&iter, "");
        }
    } else if (name && std::strcmp(name, "type") == 0) {
        append_variant_string(&iter, "standard");
    } else {
        append_variant_string(&iter, "");
    }

    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

// ─────────────────────────────────────
void TrayIcon::ReplyMenuEvent(DBusConnection *conn, DBusMessage *msg) {
    // Event(i id, s eventId, v data, u timestamp)
    dbus_int32_t id = 0;
    const char *eventId = nullptr;
    DBusMessageIter args;

    if (!dbus_message_iter_init(msg, &args)) {
        ReplyEmptyMethodReturn(conn, msg);
        return;
    }

    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_INT32) {
        dbus_message_iter_get_basic(&args, &id);
    }
    dbus_message_iter_next(&args);
    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING) {
        dbus_message_iter_get_basic(&args, &eventId);
    }

    if (eventId &&
        (std::strcmp(eventId, "clicked") == 0 || std::strcmp(eventId, "activated") == 0)) {
        if (id == kMenuOpenUiId) {
            m_OpenUiRequested.store(true);
        } else if (id == kMenuExitId) {
            m_ExitRequested.store(true);
        }
    }

    ReplyEmptyMethodReturn(conn, msg);
}

// ─────────────────────────────────────
void TrayIcon::ReplyMenuEventGroup(DBusConnection *conn, DBusMessage *msg) {
    // EventGroup(a(isvu) events) - no return data, but must return a method reply.
    ReplyEmptyMethodReturn(conn, msg);
}

// ─────────────────────────────────────
void TrayIcon::ReplyMenuAboutToShow(DBusConnection *conn, DBusMessage *msg) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return;
    }
    dbus_bool_t needUpdate = false;
    dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &needUpdate, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

// ─────────────────────────────────────
void TrayIcon::ReplyMenuAboutToShowGroup(DBusConnection *conn, DBusMessage *msg) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return;
    }

    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);

    // updatesNeeded: empty ai
    DBusMessageIter updates;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "i", &updates);
    dbus_message_iter_close_container(&iter, &updates);

    // idErrors: empty ai
    DBusMessageIter errors;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "i", &errors);
    dbus_message_iter_close_container(&iter, &errors);

    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

// ─────────────────────────────────────
void TrayIcon::ReplyEmptyMethodReturn(DBusConnection *conn, DBusMessage *msg) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return;
    }
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}
