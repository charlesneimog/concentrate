#pragma once

#include <dbus/dbus.h>
#include <atomic>
#include <string>
#include <thread>

#include "common.hpp"

class TrayIcon {
  public:
    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon &) = delete;
    bool Start(std::string title);
    void Poll();
    void SetTrayIcon(FocusState state);

    // Non-blocking, edge-triggered requests coming from tray interactions.
    // Returns true once per request.
    bool TakeOpenUiRequested();
    bool TakeExitRequested();

  private:
    static DBusHandlerResult MessageHandler(DBusConnection *conn, DBusMessage *msg,
                                            void *user_data);
    DBusHandlerResult HandleMessage(DBusConnection *conn, DBusMessage *msg);

    // com.canonical.dbusmenu (minimal but libdbusmenu-compatible)
    void ReplyMenuIntrospect(DBusConnection *conn, DBusMessage *msg);
    void ReplyMenuGetLayout(DBusConnection *conn, DBusMessage *msg);
    void ReplyMenuGetGroupProperties(DBusConnection *conn, DBusMessage *msg);
    void ReplyMenuGetProperty(DBusConnection *conn, DBusMessage *msg);
    void ReplyMenuEvent(DBusConnection *conn, DBusMessage *msg);
    void ReplyMenuEventGroup(DBusConnection *conn, DBusMessage *msg);
    void ReplyMenuAboutToShow(DBusConnection *conn, DBusMessage *msg);
    void ReplyMenuAboutToShowGroup(DBusConnection *conn, DBusMessage *msg);

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

    std::atomic<bool> m_StopDispatch{false};
    std::thread m_DispatchThread;

    std::atomic<bool> m_OpenUiRequested{false};
    std::atomic<bool> m_ExitRequested{false};
};
