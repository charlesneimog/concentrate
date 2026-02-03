#pragma once

#include <dbus/dbus.h>

#include <string>

// Minimal StatusNotifierItem (system tray) implementation using raw libdbus.
// No Qt/GTK. Call Poll() regularly from your existing main loop.
class TrayIcon {
  public:
    TrayIcon();
    ~TrayIcon();

    TrayIcon(const TrayIcon &) = delete;
    TrayIcon &operator=(const TrayIcon &) = delete;

    // Initializes DBus connection, exports /StatusNotifierItem, and registers with the watcher.
    // Returns true if connected and exported; false if DBus is unavailable.
    bool Start(std::string title);

    // Non-blocking: dispatches pending DBus messages.
    void Poll();

    // Switch between focused/unfocused icons (concentrate-focused / concentrate-unfocused).
    void SetFocused(bool focused);

  private:
    static DBusHandlerResult MessageHandler(DBusConnection *conn, DBusMessage *msg, void *user_data);
    DBusHandlerResult HandleMessage(DBusConnection *conn, DBusMessage *msg);

    void RegisterWithWatcher();
    void EmitNewIcon();
    void EmitPropertiesChangedIconName();

    void ReplyIntrospect(DBusConnection *conn, DBusMessage *msg);
    void ReplyGetProperty(DBusConnection *conn, DBusMessage *msg);
    void ReplyGetAllProperties(DBusConnection *conn, DBusMessage *msg);
    void ReplyEmptyMethodReturn(DBusConnection *conn, DBusMessage *msg);

    const char *GetPropString(const char *prop) const;

  private:
    DBusConnection *m_Conn = nullptr;
    std::string m_BusName;
    std::string m_Title;
    std::string m_IconName = "concentrate-unfocused";
    bool m_Focused = false;
    bool m_Started = false;
};
