#include "notification.hpp"
#include <iostream>
#include <cstring>
#include <spdlog/spdlog.h>

Notification::Notification() {
    dbus_error_init(&m_Err);
    m_Conn = dbus_bus_get(DBUS_BUS_SESSION, &m_Err);
    if (dbus_error_is_set(&m_Err) || !m_Conn) {
        spdlog::error("Failed to connect to session bus: {}", m_Err.message);
        dbus_error_free(&m_Err);
        return;
    }

    dbus_bus_add_match(
        m_Conn,
        "type='signal',interface='org.freedesktop.Notifications',member='ActionInvoked'",
        &m_Err);
    dbus_bus_add_match(
        m_Conn,
        "type='signal',interface='org.freedesktop.Notifications',member='NotificationClosed'",
        &m_Err);

    if (dbus_error_is_set(&m_Err)) {
        spdlog::warn("Failed to subscribe to notification signals: {}", m_Err.message);
        dbus_error_free(&m_Err);
    } else {
        dbus_connection_add_filter(m_Conn, &Notification::DBusSignalFilter, this, nullptr);
        dbus_connection_flush(m_Conn);
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
    if (!m_Conn) {
        return;
    }

    int32_t timeout = 5000; // ms

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

    const char *app_name = "Concentrate";
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
    if (!m_Conn) {
        return 0;
    }

    DBusMessage *m = dbus_message_new_method_call("org.freedesktop.Notifications",
                                                  "/org/freedesktop/Notifications",
                                                  "org.freedesktop.Notifications", "Notify");

    if (!m) {
        return 0;
    }

    DBusMessageIter args;
    dbus_message_iter_init_append(m, &args);

    const char *app_name = "Concentrate";
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
uint32_t Notification::SendHydrationPrompt(const std::string &icon, const std::string &summary,
                                           const std::string &msg,
                                           HydrationResponseCallback callback) {
    if (!m_Conn) {
        return 0;
    }

    DBusMessage *m = dbus_message_new_method_call("org.freedesktop.Notifications",
                                                  "/org/freedesktop/Notifications",
                                                  "org.freedesktop.Notifications", "Notify");
    if (!m) {
        return 0;
    }

    DBusMessageIter args;
    dbus_message_iter_init_append(m, &args);

    const char *app_name = "Concentrate";
    uint32_t replaces_id = 0;
    const int32_t timeout = 120000; // 2 minutes

    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &app_name);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &replaces_id);

    const char *icon_c = icon.c_str();
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &icon_c);

    const char *summary_c = summary.c_str();
    const char *body_c = msg.c_str();
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &summary_c);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &body_c);

    DBusMessageIter actions;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &actions);

    const char *default_id = "default";
    const char *default_lbl = "Yes";
    const char *yes_id = "yes";
    const char *yes_lbl = "Yes";
    const char *no_id = "no";
    const char *no_lbl = "No";

    dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &default_id);
    dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &default_lbl);
    dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &yes_id);
    dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &yes_lbl);
    dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &no_id);
    dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &no_lbl);

    dbus_message_iter_close_container(&args, &actions);

    DBusMessageIter hints;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &hints);
    dbus_message_iter_close_container(&args, &hints);

    dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &timeout);

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(m_Conn, m, -1, nullptr);
    dbus_message_unref(m);

    uint32_t notif_id = 0;
    if (!reply) {
        return 0;
    }

    dbus_message_get_args(reply, nullptr, DBUS_TYPE_UINT32, &notif_id, DBUS_TYPE_INVALID);
    dbus_message_unref(reply);

    if (notif_id == 0) {
        return 0;
    }

    m_HydrationPrompts[notif_id] = {
        .prompted_at = std::chrono::system_clock::now(),
        .callback = std::move(callback),
    };

    spdlog::info("Hydration prompt sent with notification id={}", notif_id);

    return notif_id;
}

// ─────────────────────────────────────
void Notification::Poll() {
    if (!m_Conn) {
        return;
    }

    // Raw libdbus pattern: read + write + dispatch to drive filters/signals.
    while (dbus_connection_get_dispatch_status(m_Conn) == DBUS_DISPATCH_DATA_REMAINS) {
        dbus_connection_dispatch(m_Conn);
    }
    dbus_connection_read_write_dispatch(m_Conn, 0);
    while (dbus_connection_get_dispatch_status(m_Conn) == DBUS_DISPATCH_DATA_REMAINS) {
        dbus_connection_dispatch(m_Conn);
    }

    const auto now = std::chrono::system_clock::now();
    for (auto it = m_HydrationPrompts.begin(); it != m_HydrationPrompts.end();) {
        const bool timedOut = now - it->second.prompted_at >= std::chrono::minutes(2);

        if (timedOut) {
            const double prompted_at =
                std::chrono::duration<double>(it->second.prompted_at.time_since_epoch()).count();
            const double answered_at = std::chrono::duration<double>(now.time_since_epoch()).count();
            spdlog::warn("Hydration prompt fallback to unknown (timedOut=true)");
            it->second.callback("unknown", prompted_at, answered_at);
            it = m_HydrationPrompts.erase(it);
        } else {
            ++it;
        }
    }
}

// ─────────────────────────────────────
DBusHandlerResult Notification::DBusSignalFilter(DBusConnection *, DBusMessage *message,
                                                 void *user_data) {
    if (!user_data || !message) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    auto *self = static_cast<Notification *>(user_data);
    self->HandleSignalMessage(message);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

// ─────────────────────────────────────
void Notification::HandleSignalMessage(DBusMessage *message) {
    if (dbus_message_is_signal(message, "org.freedesktop.Notifications", "ActionInvoked")) {
        uint32_t id = 0;
        const char *action = nullptr;
        if (!dbus_message_get_args(message, nullptr, DBUS_TYPE_UINT32, &id, DBUS_TYPE_STRING,
                                   &action, DBUS_TYPE_INVALID)) {
            spdlog::warn("Failed to parse ActionInvoked args");
            return;
        }

        spdlog::info("DBus ActionInvoked: id={}, action='{}'", id, action ? action : "<null>");

        auto it = m_ActionCallbacks.find(id);
        if (it != m_ActionCallbacks.end()) {
            it->second(action && std::strcmp(action, "yes") == 0);
            m_ActionCallbacks.erase(it);
        }

        auto hydrationIt = m_HydrationPrompts.find(id);
        if (hydrationIt != m_HydrationPrompts.end()) {
            const auto answered_at_tp = std::chrono::system_clock::now();
            const double prompted_at =
                std::chrono::duration<double>(hydrationIt->second.prompted_at.time_since_epoch())
                    .count();
            const double answered_at =
                std::chrono::duration<double>(answered_at_tp.time_since_epoch()).count();
            const std::string action_str = action ? action : "";
            const std::string answer =
                (action_str == "yes" || action_str == "default" || action_str == "1")
                    ? "yes"
                    : "no";
            hydrationIt->second.callback(answer, prompted_at, answered_at);
            m_HydrationPrompts.erase(hydrationIt);
        }

        return;
    }

    if (dbus_message_is_signal(message, "org.freedesktop.Notifications", "NotificationClosed")) {
        uint32_t id = 0;
        uint32_t reason = 0;
        if (!dbus_message_get_args(message, nullptr, DBUS_TYPE_UINT32, &id, DBUS_TYPE_UINT32,
                                   &reason, DBUS_TYPE_INVALID)) {
            spdlog::warn("Failed to parse NotificationClosed args");
            return;
        }

        spdlog::info("DBus NotificationClosed: id={}, reason={}", id, reason);

        auto hydrationIt = m_HydrationPrompts.find(id);
        if (hydrationIt != m_HydrationPrompts.end()) {
            hydrationIt->second.closed_at = std::chrono::system_clock::now();
        }
    }
}
