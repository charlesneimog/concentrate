#include <nlohmann/json.hpp>
#include <dbus/dbus.h>

class Notification {
  public:
    Notification();
    ~Notification();
    void SendNotification(const std::string icon, const std::string summary, const std::string msg);
    uint32_t SendYesNoNotification(const std::string &icon, const std::string &summary,
                                   const std::string &msg, std::function<void(bool)> callback);
    void Poll();

  private:
    DBusError m_Err;
    DBusConnection *m_Conn;
    DBusMessage *m_Msg;
    DBusMessageIter m_Args, m_Dict;

    std::chrono::time_point<std::chrono::system_clock> m_LastNotification;
    std::unordered_map<uint32_t, std::function<void(bool)>> m_ActionCallbacks;
};
