#include "notification.hpp"
#include <iostream>

Notification::Notification() {
    dbus_error_init(&m_Err);
    m_Conn = dbus_bus_get(DBUS_BUS_SESSION, &m_Err);
    if (dbus_error_is_set(&m_Err) || !m_Conn) {
        std::cerr << "Failed to connect to session bus: " << m_Err.message << "\n";
        dbus_error_free(&m_Err);
        return;
    }
}

// ─────────────────────────────────────
Notification::~Notification() {
    if (m_Conn) {
        dbus_connection_unref(m_Conn);
        m_Conn = nullptr;
    }

    if (dbus_error_is_set(&m_Err)) {
        dbus_error_free(&m_Err);
    }
}

// ─────────────────────────────────────
void Notification::SendNotification(const std::string summary, const std::string msg) {
    int32_t timeout = 3000; // ms

    std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();

    if (now - m_LastNotification < std::chrono::milliseconds(timeout)) {
        std::cerr << "Notification skipped: rate limit exceeded\n";
        return;
    }

    DBusMessage *msg_dbus = dbus_message_new_method_call("org.freedesktop.Notifications",
                                                         "/org/freedesktop/Notifications",
                                                         "org.freedesktop.Notifications", "Notify");

    if (!msg_dbus) {
        std::cerr << "Failed to create DBus message\n";
        return;
    }

    DBusMessageIter args;
    dbus_message_iter_init_append(msg_dbus, &args);

    const char *app_name = "FocusService";
    uint32_t replaces_id = 0;
    const char *icon = "";
    const char *body = msg.c_str();

    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &app_name);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &replaces_id);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &icon);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &summary);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &body);

    DBusMessageIter array;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &array);
    dbus_message_iter_close_container(&args, &array);

    DBusMessageIter dict;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);
    dbus_message_iter_close_container(&args, &dict);

    dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &timeout);

    if (!dbus_connection_send(m_Conn, msg_dbus, nullptr)) {
        std::cerr << "Failed to send DBus message\n";
        dbus_message_unref(msg_dbus);
        return;
    }

    dbus_message_unref(msg_dbus);
    m_LastNotification = now;
}
