#include <filesystem>
#include <memory>
#include <mutex>
#include <fstream>
#include <unordered_map>
#include <atomic>
#include <condition_variable>
#include <cstdint>

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
    void WakeScheduler();

    // Main loop (split into small, testable-ish pieces)
    void InitLoopState();
    void RunMainLoop();
    void RefreshFocusSnapshotIfNeeded(std::chrono::steady_clock::time_point now, bool eventDriven);
    FocusedWindow LoadFocusedWindowSnapshot();
    FocusState ComputeFocusStateAndPersist(FocusedWindow &fw_local);
    void HandleMonitoringToggleSplit(std::chrono::steady_clock::time_point now);
    void MaybeNotifyMonitoringDisabled(std::chrono::steady_clock::time_point now,
                                       bool monitoringEnabledNow);
    void UpdateMonitoringSession(std::chrono::steady_clock::time_point now,
                                 bool monitoringEnabledNow);
    void CloseOpenMonitoringInterval(std::chrono::steady_clock::time_point now);
    void CloseOpenFocusInterval(std::chrono::steady_clock::time_point now,
                                const char *reasonForLog);
    void ResetOpenFocusIntervalToIdle();
    void ResetOpenMonitoringInterval();
    void ResetLastTrackedSnapshot(FocusState state);
    void UpdateUnfocusedWarning(std::chrono::steady_clock::time_point now, FocusState currentState);
    void EnsureTaskCategory();
    void UpdateClimateIfDue(std::chrono::steady_clock::time_point now);
    void UpdateHydrationIfDue(std::chrono::steady_clock::time_point now);
    void UpdateFocusInterval(std::chrono::steady_clock::time_point now, FocusState currentState,
                             const FocusedWindow &fw_local);
    void PublishLastTrackedIntervalSnapshot();
    bool UpdateTray(std::chrono::steady_clock::time_point now, FocusState iconState,
                    bool eventDriven);
    bool PumpTrayIfDue(std::chrono::steady_clock::time_point now, bool eventDriven);
    void WaitUntilNextDeadline(FocusState currentState, bool monitoringEnabledNow, bool eventDriven);

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

    // Event-driven focus tracking (Niri IPC stream)
    std::atomic<bool> m_FocusDirty{true};
    std::atomic<bool> m_EventDriven{false};

    // Scheduler: wait-until-next-deadline with reliable wakeups
    std::mutex m_SchedulerMutex;
    std::condition_variable m_SchedulerCv;
    std::atomic<std::uint64_t> m_WakeupSeq{0};
    std::atomic<bool> m_ShutdownRequested{false};

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
    FocusState m_CurrentState{IDLE};

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

    // ---------------------------
    // Main-loop state (was previously locals in the constructor)
    // ---------------------------

    // Hydration
    double m_HydrationIntervalMinutes{10.0};
    double m_LitersPerReminder{0.0};
    std::chrono::steady_clock::time_point m_LastHydrationNotification{};
    std::chrono::steady_clock::time_point m_LastClimateUpdate{};

    // Monitoring disabled reminder
    std::chrono::steady_clock::time_point m_LastMonitoringNotification{};

    // Monitoring auto-enable after long disable
    bool m_MonitoringDisabledStreak{false};
    std::chrono::steady_clock::time_point m_MonitoringDisabledSince{};

    // Tracking: open focus interval
    bool m_HasOpenInterval{false};
    FocusState m_OpenState{IDLE};
    std::chrono::steady_clock::time_point m_IntervalStart{};
    std::chrono::steady_clock::time_point m_LastDbFlush{};
    std::string m_OpenAppId;
    std::string m_OpenTitle;
    std::string m_OpenCategory;

    // Tracking: open monitoring interval
    bool m_HasOpenMonitoringInterval{false};
    MonitoringState m_OpenMonitoringState{MONITORING_ENABLE};
    std::chrono::steady_clock::time_point m_MonitoringIntervalStart{};
    std::chrono::steady_clock::time_point m_LastMonitoringDbFlush{};

    // Unfocused warning
    bool m_InUnfocusedStreak{false};
    std::chrono::steady_clock::time_point m_UnfocusedSince{};
    std::chrono::steady_clock::time_point m_LastUnfocusedWarningAt{};

    // Focus refresh timing
    std::chrono::steady_clock::time_point m_LastFocusQueryAt{};

    // Tray polling schedule
    std::chrono::steady_clock::time_point m_NextTrayPollAt{};

    static constexpr std::chrono::seconds kSafetyPollEvery{30};
    static constexpr std::chrono::seconds kUnfocusedWarnEvery{15};
    static constexpr std::chrono::seconds kDbFlushEvery{15};

    // Last tracked interval (for graceful shutdown)
    std::chrono::steady_clock::time_point m_LastRecord;
    std::atomic<FocusState> m_LastState{IDLE};
    std::string m_LastAppId;
    std::string m_LastTitle;
    std::string m_LastCategory;
    bool m_HasLastRecord = false;
};
