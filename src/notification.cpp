#include "notification.hpp"
#include <iostream>
#include <spdlog/spdlog.h>

Notification::Notification() {
    dbus_error_init(&m_Err);
    m_Conn = dbus_bus_get(DBUS_BUS_SESSION, &m_Err);
    if (dbus_error_is_set(&m_Err) || !m_Conn) {
        spdlog::error("Failed to connect to session bus: {}", m_Err.message);
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
void Notification::SendNotification(const std::string icon, const std::string summary,
                                    const std::string msg) {
    int32_t timeout = 3000; // ms

    std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();

    if (now - m_LastNotification < std::chrono::milliseconds(timeout)) {
        spdlog::debug("Notification skipped: rate limit exceeded");
        return;
    }

    DBusMessage *msg_dbus = dbus_message_new_method_call("org.freedesktop.Notifications",
                                                         "/org/freedesktop/Notifications",
                                                         "org.freedesktop.Notifications", "Notify");

    if (!msg_dbus) {
        spdlog::error("Failed to create DBus message");
        return;
    }

    DBusMessageIter args;
    dbus_message_iter_init_append(msg_dbus, &args);

    const char *app_name = "FocusService";
    uint32_t replaces_id = 0;
    const char *body = msg.c_str();

    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &app_name);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &replaces_id);
    const char *icon_cstr = icon.c_str();
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &icon_cstr);

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
        spdlog::error("Failed to send DBus message");
        dbus_message_unref(msg_dbus);
        return;
    }

    dbus_message_unref(msg_dbus);
    m_LastNotification = now;
}

// ─────────────────────────────────────
uint32_t Notification::SendYesNoNotification(const std::string &icon, const std::string &summary,
                                             const std::string &msg,
                                             std::function<void(bool)> callback) {
    DBusMessage *m = dbus_message_new_method_call("org.freedesktop.Notifications",
                                                  "/org/freedesktop/Notifications",
                                                  "org.freedesktop.Notifications", "Notify");

    DBusMessageIter args;
    dbus_message_iter_init_append(m, &args);

    const char *app_name = "FocusService";
    uint32_t replaces_id = 0;
    int32_t timeout = 0; // persistent

    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &app_name);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &replaces_id);

    const char *icon_c = icon.c_str();
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &icon_c);

    const char *summary_c = summary.c_str();
    const char *body_c = msg.c_str();
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &summary_c);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &body_c);

    // ── Actions ──
    DBusMessageIter actions;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &actions);

    const char *yes_id = "yes";
    const char *yes_lbl = "Yes";
    const char *no_id = "no";
    const char *no_lbl = "No";

    dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &yes_id);
    dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &yes_lbl);
    dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &no_id);
    dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &no_lbl);

    dbus_message_iter_close_container(&args, &actions);

    // hints
    DBusMessageIter hints;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &hints);
    dbus_message_iter_close_container(&args, &hints);

    dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &timeout);

    // ── Send + wait reply to get notification id ──
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(m_Conn, m, -1, nullptr);

    dbus_message_unref(m);

    uint32_t notif_id = 0;
    if (reply) {
        dbus_message_get_args(reply, nullptr, DBUS_TYPE_UINT32, &notif_id, DBUS_TYPE_INVALID);

        dbus_message_unref(reply);
    }

    m_ActionCallbacks[notif_id] = std::move(callback);
    return notif_id;
}

// ─────────────────────────────────────
void Notification::Poll() {
    dbus_connection_read_write(m_Conn, 0);
    DBusMessage *msg;

    while ((msg = dbus_connection_pop_message(m_Conn)) != nullptr) {
        if (dbus_message_is_signal(msg, "org.freedesktop.Notifications", "ActionInvoked")) {

            uint32_t id;
            const char *action;

            dbus_message_get_args(msg, nullptr, DBUS_TYPE_UINT32, &id, DBUS_TYPE_STRING, &action,
                                  DBUS_TYPE_INVALID);

            auto it = m_ActionCallbacks.find(id);
            if (it != m_ActionCallbacks.end()) {
                it->second(strcmp(action, "yes") == 0);
                m_ActionCallbacks.erase(it);
            }
        }

        dbus_message_unref(msg);
    }
}
