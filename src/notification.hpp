#include <nlohmann/json.hpp>
#include <dbus/dbus.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

class Notification {
  public:
    using HydrationResponseCallback =
        std::function<void(const std::string &answer, double prompted_at, double answered_at)>;

    Notification();
    ~Notification();
    void SendNotification(const std::string icon, const std::string summary, const std::string msg);
    uint32_t SendYesNoNotification(const std::string &icon, const std::string &summary,
                                   const std::string &msg, std::function<void(bool)> callback);
    uint32_t SendHydrationPrompt(const std::string &icon, const std::string &summary,
                                 const std::string &msg, HydrationResponseCallback callback);
    void Poll();

  private:
    static DBusHandlerResult DBusSignalFilter(DBusConnection *connection, DBusMessage *message,
                                              void *user_data);
    void HandleSignalMessage(DBusMessage *message);

    DBusError m_Err;
    DBusConnection *m_Conn;
    DBusMessage *m_Msg;
    DBusMessageIter m_Args, m_Dict;

    std::chrono::time_point<std::chrono::system_clock> m_LastNotification;
    std::unordered_map<uint32_t, std::function<void(bool)>> m_ActionCallbacks;

    struct PendingHydrationPrompt {
      std::chrono::time_point<std::chrono::system_clock> prompted_at;
      std::optional<std::chrono::time_point<std::chrono::system_clock>> closed_at;
      HydrationResponseCallback callback;
    };

    std::unordered_map<uint32_t, PendingHydrationPrompt> m_HydrationPrompts;
};
