#include <nlohmann/json.hpp>
#include <dbus/dbus.h>

class Notification {
  public:
    Notification();
    ~Notification();
    void SendNotification(const std::string summary, const std::string msg);

  private:
    DBusError m_Err;
    DBusConnection *m_Conn;
    DBusMessage *m_Msg;
    DBusMessageIter m_Args, m_Dict;

    std::chrono::time_point<std::chrono::system_clock> m_LastNotification;
};
