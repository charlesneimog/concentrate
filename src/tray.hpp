#pragma once

#include <dbus/dbus.h>
#include <atomic>
#include <mutex>
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

    // Global filter for signals not delivered to object-path vtables (e.g. NameOwnerChanged).
    // We use this to detect when Waybar's StatusNotifierWatcher restarts after suspend/resume.
    static DBusHandlerResult FilterHandler(DBusConnection *conn, DBusMessage *msg, void *user_data);
    DBusHandlerResult HandleFilter(DBusConnection *conn, DBusMessage *msg);

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

    // Connection lifecycle helpers.
    // These are required because suspend/resume can leave the DBus connection or the watcher
    // in a restarted state, and Waybar forgets previous registrations.
    bool SetupConnection();
    void TeardownConnection(DBusConnection *conn);
    bool Reconnect(const char *reason);

    // Install the NameOwnerChanged match rule for org.kde.StatusNotifierWatcher.
    void AddWatcherOwnerChangedMatch(DBusConnection *conn);

    // Acquire a ref-counted connection pointer for thread-safe use outside the lock.
    DBusConnection *AcquireConnRef();

    void ReplyIntrospect(DBusConnection *conn, DBusMessage *msg);
    void ReplyGetProperty(DBusConnection *conn, DBusMessage *msg);
    void ReplyGetAllProperties(DBusConnection *conn, DBusMessage *msg);
    void ReplyEmptyMethodReturn(DBusConnection *conn, DBusMessage *msg);

    const char *GetPropString(const char *prop) const;

  private:
    mutable std::mutex m_ConnMutex;
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
