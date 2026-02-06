#include <filesystem>
#include <memory>
#include <mutex>
#include <fstream>
#include <unordered_map>
#include <atomic>

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
#include "tray.hpp"

#include "common.hpp"

#define ONFOCUSWARNINGAFTER 15 // seconds
// Register current focus state periodically if it persists this long
#define REGISTER_STATE_AFTER 30 // seconds

class Concentrate {
  public:
    Concentrate(const unsigned port, const unsigned ping, LogLevel log_level);
    ~Concentrate();

  private:
    std::filesystem::path GetBinaryPath();
    std::filesystem::path GetDBPath();
    void UpdateAllowedApps();
    void RefreshDailyActivities();
    bool InitServer();
    FocusState AmIFocused(FocusedWindow &Fw);
    bool AmIDoingDailyActivities(FocusedWindow &Fw);
    double ToUnixTime(std::chrono::steady_clock::time_point steady_tp);

  private:
    const unsigned m_Port;
    const unsigned m_Ping;
    std::filesystem::path m_Root;
    std::mutex m_GlobalMutex;

    // Lightweight API caches to avoid expensive work on high-frequency polling
    std::mutex m_ApiCacheMutex;
    std::chrono::steady_clock::time_point m_RecurringTasksCacheAt{};
    nlohmann::json m_RecurringTasksCache = nlohmann::json::array();
    std::unordered_map<int, std::pair<std::chrono::steady_clock::time_point, nlohmann::json>>
      m_FocusSummaryCache;

    // Parts
    std::unique_ptr<Anytype> m_Anytype;
    std::unique_ptr<Window> m_Window;
    std::unique_ptr<Secrets> m_Secrets;
    std::unique_ptr<Notification> m_Notification;
    std::unique_ptr<SQLite> m_SQLite;
    std::unique_ptr<HydrationService> m_Hydration;
    std::unique_ptr<TrayIcon> m_Tray;

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
    std::string m_CurrentDailyTaskCategory;
    std::string m_CurrentLiveTaskCategory;

    struct DailyActivity {
      std::string name;
      std::vector<std::string> appIds;
      std::vector<std::string> appTitles;
    };

    std::vector<DailyActivity> m_DailyActivities;

    // Special API (When wayland info is not enough)
    std::string m_SpecialProjectTitle;
    std::string m_SpecialAppId;
    bool m_SpecialProjectFocused = false;

    // Monitoring Notification
    std::chrono::system_clock m_LastMonitoringDisabledNotification;
    std::atomic<bool> m_MonitoringEnabled{true};

    std::atomic<bool> m_MonitoringTogglePending{false};

    // Last tracked interval (for graceful shutdown)
    std::chrono::steady_clock::time_point m_LastRecord;
    FocusState m_LastState = IDLE;
    std::string m_LastAppId;
    std::string m_LastTitle;
    std::string m_LastCategory;
    bool m_HasLastRecord = false;
};
