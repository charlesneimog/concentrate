#include "tray.hpp"

#include <spdlog/spdlog.h>

#include <cstring>
#include <string>
#include <unistd.h>

namespace {
static constexpr const char *kObjPath = "/StatusNotifierItem";
static constexpr const char *kIfaceSNI = "org.kde.StatusNotifierItem";
static constexpr const char *kIfaceProps = "org.freedesktop.DBus.Properties";
static constexpr const char *kIfaceIntro = "org.freedesktop.DBus.Introspectable";

static void append_variant_string(DBusMessageIter *iter, const char *value) {
    DBusMessageIter variant;
    const char *sig = "s";
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, sig, &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(iter, &variant);
}

static void dict_append_string(DBusMessageIter *dictIter, const char *key, const char *value) {
    DBusMessageIter entry;
    dbus_message_iter_open_container(dictIter, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    append_variant_string(&entry, value);
    dbus_message_iter_close_container(dictIter, &entry);
}
} // namespace

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() {
    if (m_Conn) {
        dbus_connection_unregister_object_path(m_Conn, kObjPath);
        // Drop our reference; DBus will close when refcount hits zero.
        dbus_connection_unref(m_Conn);
        m_Conn = nullptr;
    }
}

bool TrayIcon::Start(std::string title) {
    if (m_Started) {
        return true;
    }

    m_Title = std::move(title);

    DBusError err;
    dbus_error_init(&err);

    m_Conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!m_Conn) {
        if (dbus_error_is_set(&err)) {
            spdlog::warn("Tray: DBus session connection failed: {}", err.message);
            dbus_error_free(&err);
        } else {
            spdlog::warn("Tray: DBus session connection failed");
        }
        return false;
    }

    // Build a well-known name that won't collide (bus name elements can't start with a digit).
    const pid_t pid = getpid();
    m_BusName = "io.Concentrate.Tray.P" + std::to_string(static_cast<int>(pid));

    const int req = dbus_bus_request_name(m_Conn, m_BusName.c_str(), DBUS_NAME_FLAG_REPLACE_EXISTING,
                                         &err);
    if (req != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        if (dbus_error_is_set(&err)) {
            spdlog::warn("Tray: request_name failed: {}", err.message);
            dbus_error_free(&err);
        } else {
            spdlog::warn("Tray: request_name failed");
        }
        dbus_connection_unref(m_Conn);
        m_Conn = nullptr;
        return false;
    }

    static DBusObjectPathVTable vtable{};
    vtable.message_function = &TrayIcon::MessageHandler;

    if (!dbus_connection_register_object_path(m_Conn, kObjPath, &vtable, this)) {
        spdlog::warn("Tray: failed to register object path {}", kObjPath);
        dbus_connection_unref(m_Conn);
        m_Conn = nullptr;
        return false;
    }

    RegisterWithWatcher();
    m_Started = true;
    spdlog::info("Tray: StatusNotifierItem exported as {}{}", m_BusName, kObjPath);
    return true;
}

void TrayIcon::Poll() {
    if (!m_Conn) {
        return;
    }

    // Non-blocking dispatch; integrate into existing loop.
    dbus_connection_read_write_dispatch(m_Conn, 0);
}

void TrayIcon::SetFocused(bool focused) {
    if (!m_Started) {
        return;
    }
    if (focused == m_Focused) {
        return;
    }

    m_Focused = focused;
    m_IconName = m_Focused ? "concentrate-focused" : "concentrate-unfocused";

    EmitNewIcon();
    EmitPropertiesChangedIconName();
}

DBusHandlerResult TrayIcon::MessageHandler(DBusConnection *conn, DBusMessage *msg, void *user_data) {
    auto *self = static_cast<TrayIcon *>(user_data);
    if (!self) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    return self->HandleMessage(conn, msg);
}

DBusHandlerResult TrayIcon::HandleMessage(DBusConnection *conn, DBusMessage *msg) {
    // Introspection
    if (dbus_message_is_method_call(msg, kIfaceIntro, "Introspect")) {
        ReplyIntrospect(conn, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
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
        ReplyEmptyMethodReturn(conn, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void TrayIcon::RegisterWithWatcher() {
    if (!m_Conn) {
        return;
    }

    DBusMessage *msg = dbus_message_new_method_call("org.kde.StatusNotifierWatcher",
                                                   "/StatusNotifierWatcher",
                                                   "org.kde.StatusNotifierWatcher",
                                                   "RegisterStatusNotifierItem");
    if (!msg) {
        return;
    }

    const char *name = m_BusName.c_str();
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);

    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(m_Conn, msg, 1000, &err);
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
    dbus_connection_flush(m_Conn);
}

void TrayIcon::EmitNewIcon() {
    if (!m_Conn) {
        return;
    }
    DBusMessage *sig = dbus_message_new_signal(kObjPath, kIfaceSNI, "NewIcon");
    if (!sig) {
        return;
    }
    dbus_connection_send(m_Conn, sig, nullptr);
    dbus_message_unref(sig);
    dbus_connection_flush(m_Conn);
}

void TrayIcon::EmitPropertiesChangedIconName() {
    if (!m_Conn) {
        return;
    }

    // org.freedesktop.DBus.Properties.PropertiesChanged(s interface_name, a{sv} changed, as invalidated)
    DBusMessage *sig = dbus_message_new_signal(kObjPath, kIfaceProps, "PropertiesChanged");
    if (!sig) {
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

    dbus_connection_send(m_Conn, sig, nullptr);
    dbus_message_unref(sig);
    dbus_connection_flush(m_Conn);
}

void TrayIcon::ReplyIntrospect(DBusConnection *conn, DBusMessage *msg) {
    static const char *xml =
        "<node>"
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

    const char *value = GetPropString(prop ? prop : "");

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return;
    }

    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    append_variant_string(&iter, value);

    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

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
    dbus_message_iter_close_container(&iter, &dict);

    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

void TrayIcon::ReplyEmptyMethodReturn(DBusConnection *conn, DBusMessage *msg) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return;
    }
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}
