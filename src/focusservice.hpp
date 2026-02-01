#include <filesystem>
#include <memory>
#include <mutex>
#include <fstream>

// Libs
#include <httplib.h>
#include <spdlog/spdlog.h>

// parts
#include "anytype.hpp"
#include "window.hpp"
#include "secrets.hpp"
#include "notification.hpp"
#include "sqlite.hpp"
#include "hydration.hpp"

#include "common.hpp"

#define ONFOCUSWARNINGAFTER 15 // seconds
// Register current focus state periodically if it persists this long
#define REGISTER_STATE_AFTER 30 // seconds

class FocusService {
  public:
    FocusService(const unsigned port, const unsigned ping, LogLevel log_level);
    ~FocusService();

  private:
    std::filesystem::path GetBinaryPath();
    std::filesystem::path GetDBPath();
    void UpdateAllowedApps();
    bool InitServer();
    FocusState AmIFocused(FocusedWindow Fw);
    double ToUnixTime(std::chrono::steady_clock::time_point steady_tp);

  private:
    const unsigned m_Port;
    const unsigned m_Ping;
    std::filesystem::path m_Root;
    std::mutex m_GlobalMutex;

    // Parts
    std::unique_ptr<Anytype> m_Anytype;
    std::unique_ptr<Window> m_Window;
    std::unique_ptr<Secrets> m_Secrets;
    std::unique_ptr<Notification> m_Notification;
    std::unique_ptr<SQLite> m_SQLite;
    std::unique_ptr<HydrationService> m_Hydration;

    // Server
    std::thread m_Thread;
    httplib::Server m_Server;
    std::string m_IndexHtml;
    std::string m_AppJs;
    FocusedWindow m_Fw;

    // Window
    std::string m_TaskTitle;
    FocusState m_CurrentState;

    // Current Task
    std::vector<std::string> m_AllowedApps;
    std::vector<std::string> m_AllowedWindowTitles;
    std::string m_CurrentTaskCategory;

    // Special API (When wayland info is not enough)
    std::string m_SpecialProjectTitle;
    std::string m_SpecialAppId;
    bool m_SpecialProjectFocused;

    // Monitoring Notification
    std::chrono::system_clock m_LastMonitoringDisabledNotification;
    bool m_MonitoringEnabled = true;
};
