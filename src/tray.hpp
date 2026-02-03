#pragma once

#include <dbus/dbus.h>
#include <string>

#include "common.hpp"

class TrayIcon {
  public:
    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon &) = delete;
    bool Start(std::string title);
    void Poll();
    void SetFocused(FocusState state);

  private:
    static DBusHandlerResult MessageHandler(DBusConnection *conn, DBusMessage *msg,
                                            void *user_data);
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
    FocusState m_FocusState = IDLE;
    bool m_Started = false;
};
