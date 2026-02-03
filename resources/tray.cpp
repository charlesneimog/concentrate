#include <dbus/dbus.h>
#include <glib.h>
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>

static const char* BUS_NAME   = "org.example.TrayDemo";
static const char* OBJ_PATH  = "/StatusNotifierItem";
static const char* IFACE     = "org.kde.StatusNotifierItem";

DBusHandlerResult message_handler(DBusConnection* conn,
                                  DBusMessage* msg,
                                  void* /*user_data*/) {
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        const char* xml =
            "<node>"
            " <interface name='org.kde.StatusNotifierItem'>"
            "  <property name='Category' type='s' access='read'/>"
            "  <property name='Id' type='s' access='read'/>"
            "  <property name='Title' type='s' access='read'/>"
            "  <property name='Status' type='s' access='read'/>"
            "  <property name='IconName' type='s' access='read'/>"
            " </interface>"
            "</node>";

        DBusMessage* reply = dbus_message_new_method_return(msg);
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
        dbus_connection_send(conn, reply, nullptr);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "Get")) {
        const char* iface;
        const char* prop;
        dbus_message_get_args(msg, nullptr,
                              DBUS_TYPE_STRING, &iface,
                              DBUS_TYPE_STRING, &prop,
                              DBUS_TYPE_INVALID);

        const char* value = "";

        if (!strcmp(prop, "Category")) value = "ApplicationStatus";
        else if (!strcmp(prop, "Id")) value = "tray-demo";
        else if (!strcmp(prop, "Title")) value = "Tray Demo";
        else if (!strcmp(prop, "Status")) value = "Active";
        else if (!strcmp(prop, "IconName")) value = "utilities-terminal";

        DBusMessage* reply = dbus_message_new_method_return(msg);
        DBusMessageIter iter, variant;
        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
        dbus_message_iter_close_container(&iter, &variant);

        dbus_connection_send(conn, reply, nullptr);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int main() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection* conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn) {
        std::cerr << "DBus connection failed\n";
        return 1;
    }

    dbus_bus_request_name(conn, BUS_NAME, DBUS_NAME_FLAG_REPLACE_EXISTING, &err);

    static DBusObjectPathVTable vtable = {};
    vtable.message_function = message_handler;

    dbus_connection_register_object_path(conn, OBJ_PATH, &vtable, nullptr);

    // Register with tray watcher
    DBusMessage* msg = dbus_message_new_method_call(
        "org.kde.StatusNotifierWatcher",
        "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher",
        "RegisterStatusNotifierItem"
    );
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &BUS_NAME, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, msg, nullptr);
    dbus_message_unref(msg);

    std::cout << "Tray icon running\n";

    while (true) {
       // sleep for 33 milliseconds
         std::this_thread::sleep_for(std::chrono::milliseconds(33)); 
    }
}

