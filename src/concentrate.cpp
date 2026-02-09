#include "concentrate.hpp"

#include <string>
#include <limits.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <algorithm>
#include <sys/wait.h>

// ─────────────────────────────────────
Concentrate::Concentrate(const unsigned port, const unsigned ping, LogLevel log_level)
    : m_Port(port), m_Ping(ping) {

    if (log_level == LOG_DEBUG) {
        spdlog::set_level(spdlog::level::debug);
    } else if (log_level == LOG_INFO) {
        spdlog::set_level(spdlog::level::info);
    } else if (log_level == LOG_OFF) {
        spdlog::set_level(spdlog::level::off);
    }

    // Server
    m_Root = GetBinaryPath();
    if (!std::filesystem::exists(m_Root)) {
        spdlog::error("Root does not exists: {}", m_Root.string());
        exit(1);
    }

    std::filesystem::path dbpath = GetDBPath();

    spdlog::info("Static WebSite Root, {}!", m_Root.string());
    spdlog::info("DataBase path: {}!", dbpath.string());
    spdlog::info("Serving on: http://localhost:{}", m_Port);

    // Secrets
    m_Secrets = std::make_unique<Secrets>();
    spdlog::info("Secrets manager initialized");

    // Server
    InitServer();

    // SQlite
    m_SQLite = std::make_unique<SQLite>(dbpath);
    spdlog::info("SQLite database initialized");

    // Anytype
    m_Anytype = std::make_unique<Anytype>();
    spdlog::info("Anytype client initialized");

    // Windows API (get AppID, Title)
    m_Window = std::make_unique<Window>();
    spdlog::info("Window API initialized");

    // Prefer event-driven focus updates via Niri IPC EventStream; fall back to polling when not
    // available.
    if (m_Window && m_Window->StartEventStream([this]() {
            // Avoid wake storms: if we're already dirty, the main loop will refresh soon anyway.
            const bool wasDirty = m_FocusDirty.exchange(true, std::memory_order_relaxed);
            if (!wasDirty) {
                WakeScheduler();
            }
        })) {
        m_EventDriven.store(true);
        spdlog::info("Niri IPC event stream enabled (push mode)");
    } else {
        m_EventDriven.store(false);
        spdlog::warn("Niri IPC event stream unavailable; falling back to polling mode");
    }

    // Notifications
    m_Notification = std::make_unique<Notification>();
    spdlog::info("Notification system initialized");

    // Tray icon (DBus StatusNotifierItem)
    m_Tray = std::make_unique<TrayIcon>();
    if (m_Tray->Start("Concentrate")) {
        m_Tray->SetTrayIcon(IDLE);
        spdlog::info("Tray icon initialized");
    } else {
        spdlog::warn("Tray icon not available (no DBus watcher or session bus)");
    }

    // HydrationService
    m_Hydration = std::make_unique<HydrationService>();

    // Time
    std::string monitoring_str = m_Secrets->LoadSecret("monitoring_enabled");
    m_MonitoringEnabled.store(monitoring_str.empty() ? true : (monitoring_str == "true"));

    // monitoring
    if (!m_MonitoringEnabled.load()) {
        m_Notification->SendNotification("concentrate-off", "Concentrate",
                                         "Apps monitoring is off");
    }

    UpdateAllowedApps();
    RefreshDailyActivities();

    InitLoopState();
    RunMainLoop();
}

// ─────────────────────────────────────
Concentrate::~Concentrate() {
    FocusState lastStateSnapshot = IDLE;
    std::chrono::steady_clock::time_point lastRecordSnapshot;
    std::string lastAppIdSnapshot;
    std::string lastTitleSnapshot;
    std::string lastCategorySnapshot;
    bool hasLastRecordSnapshot = false;

    {
        std::lock_guard<std::mutex> lock(m_GlobalMutex);
        lastStateSnapshot = m_LastState.load();
        lastRecordSnapshot = m_LastRecord;
        lastAppIdSnapshot = m_LastAppId;
        lastTitleSnapshot = m_LastTitle;
        lastCategorySnapshot = m_LastCategory;
        hasLastRecordSnapshot = m_HasLastRecord;
    }

    if (m_Window) {
        m_Window->StopEventStream();
    }

    if (hasLastRecordSnapshot && lastStateSnapshot != IDLE) {
        auto now = std::chrono::steady_clock::now();
        const double startUnix = ToUnixTime(lastRecordSnapshot);
        const double endUnix = ToUnixTime(now);
        const double duration = endUnix - startUnix;
        const std::string category =
            lastCategorySnapshot.empty() ? "Uncategorized" : lastCategorySnapshot;

        if (duration > 0.0) {
            // Prefer closing the already-inserted open interval.
            if (!m_SQLite->UpdateEventNew(lastAppIdSnapshot, lastTitleSnapshot, category, endUnix,
                                          duration, lastStateSnapshot)) {
                // Fallback: insert a final record so the session isn't lost.
                m_SQLite->InsertEventNew(lastAppIdSnapshot, lastTitleSnapshot, category, startUnix,
                                         endUnix, duration, lastStateSnapshot);
            }
            spdlog::info("Final focus event saved: state={}, app_id='{}', title='{}', "
                         "category='{}', duration={}",
                         static_cast<int>(lastStateSnapshot), lastAppIdSnapshot, lastTitleSnapshot,
                         category, duration);
        }
    }

    m_Server.stop();
    if (m_Thread.joinable()) {
        m_Thread.join();
    }
}

// ─────────────────────────────────────
void Concentrate::InitLoopState() {
    const auto now = std::chrono::steady_clock::now();

    // Hydration
    m_HydrationIntervalMinutes = 10.0;
    const double dailyLiters = m_Hydration ? m_Hydration->GetLiters() : 0.0;
    if (dailyLiters > 0.0) {
        m_LitersPerReminder = dailyLiters / (10.0 * 60.0 / m_HydrationIntervalMinutes);
    } else {
        m_LitersPerReminder = 0.0;
    }
    m_LastHydrationNotification =
        now - std::chrono::minutes(static_cast<int>(m_HydrationIntervalMinutes));
    m_LastClimateUpdate = now;

    // Monitoring disabled reminder
    m_LastMonitoringNotification = now - std::chrono::minutes(10);

    // Tracking model state
    m_HasOpenInterval = false;
    m_OpenState = IDLE;
    m_IntervalStart = now;
    m_LastDbFlush = now;
    m_OpenAppId.clear();
    m_OpenTitle.clear();
    m_OpenCategory.clear();

    m_HasOpenMonitoringInterval = false;
    m_OpenMonitoringState = MONITORING_ENABLE;
    m_MonitoringIntervalStart = now;
    m_LastMonitoringDbFlush = now;

    // Unfocused warning
    m_InUnfocusedStreak = false;
    m_UnfocusedSince = now;
    m_LastUnfocusedWarningAt = now;

    // Focus refresh timing
    m_LastFocusQueryAt = now - std::chrono::seconds(m_Ping);

    // Tray polling
    m_NextTrayPollAt = now;

    // Last tracked interval snapshot
    {
        std::lock_guard<std::mutex> lock(m_GlobalMutex);
        m_LastState.store(IDLE);
        m_LastAppId.clear();
        m_LastTitle.clear();
        m_LastCategory.clear();
        m_HasLastRecord = false;
    }
}

// ─────────────────────────────────────
void Concentrate::RefreshFocusSnapshotIfNeeded(std::chrono::steady_clock::time_point now,
                                               bool eventDriven) {
    bool shouldRefreshFocus = false;
    if (eventDriven) {
        shouldRefreshFocus = m_FocusDirty.exchange(false);
        if (!shouldRefreshFocus && (now - m_LastFocusQueryAt) >= kSafetyPollEvery) {
            shouldRefreshFocus = true;
        }
    } else {
        // In polling mode, m_FocusDirty must not force the scheduler to wake continuously.
        m_FocusDirty.store(false);
        if ((now - m_LastFocusQueryAt) >= std::chrono::seconds(m_Ping)) {
            shouldRefreshFocus = true;
        }
    }

    if (!shouldRefreshFocus) {
        return;
    }

    m_LastFocusQueryAt = now;
    FocusedWindow fresh = m_Window ? m_Window->GetFocusedWindow() : FocusedWindow{};
    {
        std::lock_guard<std::mutex> lock(m_GlobalMutex);
        m_Fw = fresh;
    }
}

// ─────────────────────────────────────
FocusedWindow Concentrate::LoadFocusedWindowSnapshot() {
    std::lock_guard<std::mutex> lock(m_GlobalMutex);
    return m_Fw;
}

// ─────────────────────────────────────
FocusState Concentrate::ComputeFocusStateAndPersist(FocusedWindow &fw_local) {
    if (m_SpecialProjectFocused) {
        fw_local.app_id = m_SpecialAppId;
        fw_local.title = m_SpecialProjectTitle;
        spdlog::debug("Special project focus override: app_id='{}', title='{}'", fw_local.app_id,
                      fw_local.title);
    }

    FocusState currentState = AmIFocused(fw_local);

    // Persist category/state adjustments back to the shared snapshot used by the web API.
    {
        std::lock_guard<std::mutex> lock(m_GlobalMutex);
        m_Fw = fw_local;
    }

    return currentState;
}

// ─────────────────────────────────────
void Concentrate::HandleMonitoringToggleSplit(std::chrono::steady_clock::time_point now) {
    const bool monitoringToggled = m_MonitoringTogglePending.exchange(false);
    if (!monitoringToggled || !m_HasOpenMonitoringInterval) {
        return;
    }

    const double endUnix = ToUnixTime(now);
    const double startUnix = ToUnixTime(m_MonitoringIntervalStart);
    const double duration = endUnix - startUnix;
    if (duration > 0.0) {
        if (!m_SQLite->UpdateMonitoringSession(endUnix, duration, m_OpenMonitoringState)) {
            m_SQLite->InsertMonitoringSession(startUnix, endUnix, duration, m_OpenMonitoringState);
        }
    }
    m_HasOpenMonitoringInterval = false;
}

// ─────────────────────────────────────
void Concentrate::MaybeNotifyMonitoringDisabled(std::chrono::steady_clock::time_point now,
                                                bool monitoringEnabledNow) {
    if (monitoringEnabledNow) {
        return;
    }
    if (now - m_LastMonitoringNotification >= std::chrono::minutes(1)) {
        if (m_Notification) {
            m_Notification->SendNotification("concentrate-off", "Concentrate",
                                             "Application monitoring is currently disabled.");
        }
        m_LastMonitoringNotification = now;
    }
}

// ─────────────────────────────────────
void Concentrate::CloseOpenMonitoringInterval(std::chrono::steady_clock::time_point now) {
    if (!m_HasOpenMonitoringInterval) {
        return;
    }

    const double endUnix = ToUnixTime(now);
    const double startUnix = ToUnixTime(m_MonitoringIntervalStart);
    const double duration = endUnix - startUnix;
    if (duration > 0.0) {
        if (!m_SQLite->UpdateMonitoringSession(endUnix, duration, m_OpenMonitoringState)) {
            m_SQLite->InsertMonitoringSession(startUnix, endUnix, duration, m_OpenMonitoringState);
        }
    }
    m_HasOpenMonitoringInterval = false;
}

// ─────────────────────────────────────
void Concentrate::UpdateMonitoringSession(std::chrono::steady_clock::time_point now,
                                          bool monitoringEnabledNow) {
    const MonitoringState desired = monitoringEnabledNow ? MONITORING_ENABLE : MONITORING_DISABLE;

    if (!m_HasOpenMonitoringInterval) {
        m_HasOpenMonitoringInterval = true;
        m_OpenMonitoringState = desired;
        m_MonitoringIntervalStart = now;
        m_LastMonitoringDbFlush = now;

        const double startUnix = ToUnixTime(m_MonitoringIntervalStart);
        m_SQLite->InsertMonitoringSession(startUnix, startUnix, 0.0, m_OpenMonitoringState);
        return;
    }

    if (desired != m_OpenMonitoringState) {
        CloseOpenMonitoringInterval(now);

        m_HasOpenMonitoringInterval = true;
        m_OpenMonitoringState = desired;
        m_MonitoringIntervalStart = now;
        m_LastMonitoringDbFlush = now;

        const double startUnix2 = ToUnixTime(m_MonitoringIntervalStart);
        m_SQLite->InsertMonitoringSession(startUnix2, startUnix2, 0.0, m_OpenMonitoringState);
        return;
    }

    if (now - m_LastMonitoringDbFlush >= kDbFlushEvery) {
        const double endUnix = ToUnixTime(now);
        const double startUnix = ToUnixTime(m_MonitoringIntervalStart);
        const double duration = endUnix - startUnix;
        if (duration > 0.0) {
            m_SQLite->UpdateMonitoringSession(endUnix, duration, m_OpenMonitoringState);
        }
        m_LastMonitoringDbFlush = now;
    }
}

// ─────────────────────────────────────
void Concentrate::CloseOpenFocusInterval(std::chrono::steady_clock::time_point now,
                                         const char *reasonForLog) {
    if (!m_HasOpenInterval || m_OpenState == IDLE) {
        return;
    }

    const double endUnix = ToUnixTime(now);
    const double startUnix = ToUnixTime(m_IntervalStart);
    const double duration = endUnix - startUnix;

    if (duration > 0.0) {
        if (!m_SQLite->UpdateEventNew(m_OpenAppId, m_OpenTitle, m_OpenCategory, endUnix, duration,
                                      m_OpenState)) {
            m_SQLite->InsertEventNew(m_OpenAppId, m_OpenTitle, m_OpenCategory, startUnix, endUnix,
                                     duration, m_OpenState);
        }
        spdlog::info("Focus event closed ({}): state={}, app_id='{}', title='{}', duration={}",
                     reasonForLog ? reasonForLog : "closed", static_cast<int>(m_OpenState),
                     m_OpenAppId, m_OpenTitle, duration);
    }
}

// ─────────────────────────────────────
void Concentrate::ResetOpenFocusIntervalToIdle() {
    m_HasOpenInterval = false;
    m_OpenState = IDLE;
    m_OpenAppId.clear();
    m_OpenTitle.clear();
    m_OpenCategory.clear();
}

// ─────────────────────────────────────
void Concentrate::ResetOpenMonitoringInterval() {
    m_HasOpenMonitoringInterval = false;
}

// ─────────────────────────────────────
void Concentrate::ResetLastTrackedSnapshot(FocusState state) {
    std::lock_guard<std::mutex> lock(m_GlobalMutex);
    m_LastState.store(state);
    m_LastAppId.clear();
    m_LastTitle.clear();
    m_LastCategory.clear();
    m_HasLastRecord = false;
}

// ─────────────────────────────────────
void Concentrate::UpdateUnfocusedWarning(std::chrono::steady_clock::time_point now,
                                         FocusState currentState) {
    if (currentState == UNFOCUSED) {
        if (!m_InUnfocusedStreak) {
            m_InUnfocusedStreak = true;
            m_UnfocusedSince = now;
            m_LastUnfocusedWarningAt = now;
            return;
        }

        if ((now - m_UnfocusedSince) >= kUnfocusedWarnEvery &&
            (now - m_LastUnfocusedWarningAt) >= kUnfocusedWarnEvery) {
            if (m_Notification) {
                m_Notification->SendNotification(
                    "concentrate-unfocused", "Concentrate",
                    "Focus: you've been unfocused for more than 15 seconds.");
            }
            m_LastUnfocusedWarningAt = now;
        }
        return;
    }

    // Reset streak when focused or idle.
    m_InUnfocusedStreak = false;
    m_UnfocusedSince = now;
    m_LastUnfocusedWarningAt = now;
}

// ─────────────────────────────────────
void Concentrate::EnsureTaskCategory() {
    if (!m_CurrentTaskCategory.empty()) {
        return;
    }
    m_CurrentTaskCategory = "Uncategorized";
    spdlog::debug("Current task category defaulted to '{}'", m_CurrentTaskCategory);
}

// ─────────────────────────────────────
void Concentrate::UpdateClimateIfDue(std::chrono::steady_clock::time_point now) {
    if (!m_Hydration) {
        return;
    }
    if (now - m_LastClimateUpdate < std::chrono::hours(3)) {
        return;
    }
    spdlog::info("Updating location and weather info...");
    try {
        m_Hydration->GetLocation();
        m_Hydration->GetHydrationRecommendation();
    } catch (const std::exception &e) {
        spdlog::warn("Failed to update location/climate: {}", e.what());
    }
    m_LastClimateUpdate = now;
}

// ─────────────────────────────────────
void Concentrate::UpdateHydrationIfDue(std::chrono::steady_clock::time_point now) {
    if (!m_Notification) {
        return;
    }
    if (now - m_LastHydrationNotification <
        std::chrono::minutes(static_cast<int>(m_HydrationIntervalMinutes))) {
        return;
    }

    m_Notification->SendNotification(
        "dialog-info", "Concentrate",
        fmt::format("Time to drink water! ~{:.2f} L since last reminder.", m_LitersPerReminder));
    m_LastHydrationNotification = now;
}

// ─────────────────────────────────────
void Concentrate::UpdateFocusInterval(std::chrono::steady_clock::time_point now,
                                      FocusState currentState, const FocusedWindow &fw_local) {
    const std::string currAppId = fw_local.app_id;
    const std::string currTitle = fw_local.title;
    const std::string currCategory = fw_local.category;

    if (!m_HasOpenInterval) {
        m_HasOpenInterval = true;
        m_OpenState = currentState;
        m_OpenAppId = currAppId;
        m_OpenTitle = currTitle;
        m_OpenCategory = currCategory;
        m_IntervalStart = now;
        m_LastDbFlush = now;

        const double startUnix = ToUnixTime(m_IntervalStart);
        m_SQLite->InsertEventNew(m_OpenAppId, m_OpenTitle, m_OpenCategory, startUnix, startUnix,
                                 0.0, m_OpenState);
        return;
    }

    const bool changed = (currentState != m_OpenState) || (currAppId != m_OpenAppId) ||
                         (currTitle != m_OpenTitle) || (currCategory != m_OpenCategory);

    if (changed) {
        // Close previous interval with a final UPDATE.
        CloseOpenFocusInterval(now, "changed");

        // Start next interval and INSERT.
        m_OpenState = currentState;
        m_OpenAppId = currAppId;
        m_OpenTitle = currTitle;
        m_OpenCategory = currCategory;
        m_IntervalStart = now;
        m_LastDbFlush = now;

        const double startUnix2 = ToUnixTime(m_IntervalStart);
        m_SQLite->InsertEventNew(m_OpenAppId, m_OpenTitle, m_OpenCategory, startUnix2, startUnix2,
                                 0.0, m_OpenState);
        return;
    }

    if (now - m_LastDbFlush >= kDbFlushEvery) {
        const double endUnix = ToUnixTime(now);
        const double startUnix = ToUnixTime(m_IntervalStart);
        const double duration = endUnix - startUnix;
        if (duration > 0.0) {
            m_SQLite->UpdateEventNew(m_OpenAppId, m_OpenTitle, m_OpenCategory, endUnix, duration,
                                     m_OpenState);
        }
        m_LastDbFlush = now;
    }
}

// ─────────────────────────────────────
void Concentrate::PublishLastTrackedIntervalSnapshot() {
    std::lock_guard<std::mutex> lock(m_GlobalMutex);
    if (m_HasOpenInterval && m_OpenState != IDLE) {
        m_LastRecord = m_IntervalStart;
        m_LastState.store(m_OpenState);
        m_LastAppId = m_OpenAppId;
        m_LastTitle = m_OpenTitle;
        m_LastCategory = m_OpenCategory;
        m_HasLastRecord = true;
        return;
    }

    m_LastState.store(IDLE);
    m_LastAppId.clear();
    m_LastTitle.clear();
    m_LastCategory.clear();
    m_HasLastRecord = false;
}

// ─────────────────────────────────────
bool Concentrate::PumpTrayIfDue(std::chrono::steady_clock::time_point now, bool eventDriven) {
    if (!m_Tray) {
        return false;
    }

    const auto trayPollEvery = eventDriven ? std::chrono::seconds(1) : std::chrono::seconds(m_Ping);
    if (now < m_NextTrayPollAt) {
        return false;
    }

    m_Tray->Poll();
    m_NextTrayPollAt = now + trayPollEvery;

    if (m_Tray->TakeOpenUiRequested()) {
        int result =
            std::system(("xdg-open http://127.0.0.1:" + std::to_string(m_Port) + "/ &").c_str());
        if (result != 0) {
            spdlog::error("Failed to open browser");
        }
    }

    if (m_Tray->TakeExitRequested()) {
        spdlog::info("Exit requested from tray");
        m_ShutdownRequested.store(true);
        if (m_Window) {
            m_Window->StopEventStream();
        }
        return true;
    }

    return false;
}

// ─────────────────────────────────────
bool Concentrate::UpdateTray(std::chrono::steady_clock::time_point now, FocusState iconState,
                             bool eventDriven) {
    if (!m_Tray) {
        return false;
    }

    m_Tray->SetTrayIcon(iconState);
    return PumpTrayIfDue(now, eventDriven);
}

// ─────────────────────────────────────
void Concentrate::WaitUntilNextDeadline(FocusState currentState, bool monitoringEnabledNow,
                                        bool eventDriven) {
    const auto now2 = std::chrono::steady_clock::now();
    auto deadline = now2 + std::chrono::hours(24);

    // Tray polling
    if (m_Tray) {
        deadline = std::min(deadline, m_NextTrayPollAt);
    }

    // Focus refresh cadence
    if (!eventDriven) {
        deadline = std::min(deadline, m_LastFocusQueryAt + std::chrono::seconds(m_Ping));
    } else {
        deadline = std::min(deadline, m_LastFocusQueryAt + kSafetyPollEvery);
    }

    // Hydration/climate
    // These tasks are only processed when monitoring is enabled and we're not idle.
    // In IDLE, their timestamps are not advanced, so including them here can create an
    // always-expired deadline and cause a tight loop.
    if (monitoringEnabledNow && currentState != IDLE) {
        deadline = std::min(
            deadline, m_LastHydrationNotification +
                          std::chrono::minutes(static_cast<int>(m_HydrationIntervalMinutes)));
        deadline = std::min(deadline, m_LastClimateUpdate + std::chrono::hours(3));
    }

    // Monitoring disabled reminder
    if (!monitoringEnabledNow) {
        deadline = std::min(deadline, m_LastMonitoringNotification + std::chrono::minutes(1));
    }

    // Periodic DB flushes
    if (m_HasOpenInterval && m_OpenState != IDLE) {
        deadline = std::min(deadline, m_LastDbFlush + kDbFlushEvery);
    }
    if (m_HasOpenMonitoringInterval) {
        deadline = std::min(deadline, m_LastMonitoringDbFlush + kDbFlushEvery);
    }

    // Unfocused warning timing
    if (currentState == UNFOCUSED) {
        const auto nextWarn = !m_InUnfocusedStreak
                                  ? (now2 + kUnfocusedWarnEvery)
                                  : std::max(m_UnfocusedSince + kUnfocusedWarnEvery,
                                             m_LastUnfocusedWarningAt + kUnfocusedWarnEvery);
        deadline = std::min(deadline, nextWarn);
    }

    const auto seq = m_WakeupSeq.load(std::memory_order_relaxed);
    std::unique_lock<std::mutex> lk(m_SchedulerMutex);
    m_SchedulerCv.wait_until(lk, deadline, [&] {
        if (m_ShutdownRequested.load()) {
            return true;
        }
        if (m_WakeupSeq.load(std::memory_order_relaxed) != seq) {
            return true;
        }

        // Only check focus dirty ONCE per wakeup sequence
        // Don't keep checking it on spurious wakeups
        return false;
    });
}

// ─────────────────────────────────────
void Concentrate::RunMainLoop() {
    bool running = true;
    while (running) {
        const auto now = std::chrono::steady_clock::now();
        const bool eventDriven = m_EventDriven.load();
        RefreshFocusSnapshotIfNeeded(now, eventDriven);

        HandleMonitoringToggleSplit(now);
        const bool monitoringEnabledNow = m_MonitoringEnabled.load();

        FocusedWindow fw_local = LoadFocusedWindowSnapshot();
        FocusState currentState = ComputeFocusStateAndPersist(fw_local);

        MaybeNotifyMonitoringDisabled(now, monitoringEnabledNow);

        // IDLE: close any open focus interval AND any open monitoring interval. Idle is never
        // recorded.
        if (currentState == IDLE) {
            CloseOpenFocusInterval(now, "idle");
            CloseOpenMonitoringInterval(now);
            ResetOpenFocusIntervalToIdle();
            ResetOpenMonitoringInterval();
            ResetLastTrackedSnapshot(IDLE);
            if (UpdateTray(now, IDLE, eventDriven)) {
                break;
            }
            WaitUntilNextDeadline(currentState, monitoringEnabledNow, eventDriven);
            continue;
        }

        // Monitoring sessions are recorded only when NOT idle.
        UpdateMonitoringSession(now, monitoringEnabledNow);
        if (!monitoringEnabledNow) {
            // Reset unfocused streak tracking while monitoring is disabled.
            m_InUnfocusedStreak = false;
            m_UnfocusedSince = now;
            m_LastUnfocusedWarningAt = now;

            // Close any open interval before entering the disabled/idle state.
            CloseOpenFocusInterval(now, "disabled");
            ResetOpenFocusIntervalToIdle();
            m_OpenState = DISABLE;
            ResetLastTrackedSnapshot(DISABLE);

            if (UpdateTray(now, DISABLE, eventDriven)) {
                break;
            }

            WaitUntilNextDeadline(currentState, monitoringEnabledNow, eventDriven);
            continue;
        }

        UpdateUnfocusedWarning(now, currentState);
        EnsureTaskCategory();
        UpdateClimateIfDue(now);
        UpdateHydrationIfDue(now);

        UpdateFocusInterval(now, currentState, fw_local);
        PublishLastTrackedIntervalSnapshot();

        if (UpdateTray(now, currentState, eventDriven)) {
            break;
        }

        WaitUntilNextDeadline(currentState, monitoringEnabledNow, eventDriven);
    }
}

// ─────────────────────────────────────
void Concentrate::WakeScheduler() {
    m_WakeupSeq.fetch_add(1, std::memory_order_relaxed);
    m_SchedulerCv.notify_one();
}

// ─────────────────────────────────────
double Concentrate::ToUnixTime(std::chrono::steady_clock::time_point steady_tp) {
    auto now_steady = std::chrono::steady_clock::now();
    auto now_system = std::chrono::system_clock::now();

    auto delta = steady_tp - now_steady;
    auto system_tp = now_system + delta;

    return std::chrono::duration<double>(system_tp.time_since_epoch()).count();
}

// ─────────────────────────────────────
FocusState Concentrate::AmIFocused(FocusedWindow &Fw) {
    // If the focused window is invalid (no app_id and no title), treat as IDLE
    if (Fw.app_id.empty() && Fw.title.empty()) {
        spdlog::debug("FOCUSED: IDLE (no app_id or title)");
        Fw.category.clear();
        m_CurrentLiveTaskCategory.clear();
        spdlog::debug("Live category cleared (idle)");
        return IDLE;
    }

    bool isFocusedWindow = true;

    if (!m_AllowedApps.empty() || !m_AllowedWindowTitles.empty()) {
        isFocusedWindow = false;

        // Check allowed apps
        for (const auto &allowedApp : m_AllowedApps) {
            if (Fw.app_id.find(allowedApp) != std::string::npos) {
                isFocusedWindow = true;
                spdlog::trace("Window matches allowed app: {}", allowedApp);
                break;
            }
        }

        // Check allowed titles
        if (!isFocusedWindow && !m_AllowedWindowTitles.empty()) {
            for (const auto &allowedTitle : m_AllowedWindowTitles) {
                if (Fw.title.find(allowedTitle) != std::string::npos) {
                    isFocusedWindow = true;
                    spdlog::trace("Window matches allowed title: {}", allowedTitle);
                    break;
                }
            }
        }
    }

    if (!isFocusedWindow) {
        if (AmIDoingDailyActivities(Fw)) {
            spdlog::debug("FOCUSED: DAILY ACTIVITY");
            Fw.category = m_CurrentDailyTaskCategory;
            m_CurrentLiveTaskCategory = Fw.category;
            spdlog::debug("Daily activity category set: '{}'", Fw.category);
            return FOCUSED;
        }
    }

    const std::string taskCategory =
        m_CurrentTaskCategory.empty() ? "Uncategorized" : m_CurrentTaskCategory;
    m_CurrentDailyTaskCategory.clear();
    Fw.category = taskCategory;
    m_CurrentLiveTaskCategory = Fw.category;

    return isFocusedWindow ? FOCUSED : UNFOCUSED;
}

// ─────────────────────────────────────
bool Concentrate::AmIDoingDailyActivities(FocusedWindow &Fw) {
    std::vector<DailyActivity> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_GlobalMutex);
        snapshot = m_DailyActivities;
    }

    if (snapshot.empty()) {
        return false;
    }

    for (const auto &activity : snapshot) {
        bool matches = false;
        for (const auto &appId : activity.appIds) {
            if (!appId.empty() && Fw.app_id.find(appId) != std::string::npos) {
                matches = true;
                break;
            }
        }

        if (!matches) {
            for (const auto &title : activity.appTitles) {
                if (!title.empty() && Fw.title.find(title) != std::string::npos) {
                    matches = true;
                    break;
                }
            }
        }

        if (matches) {
            if (!activity.name.empty()) {
                m_CurrentDailyTaskCategory = activity.name;
                spdlog::debug("Matched daily activity category: '{}'", m_CurrentDailyTaskCategory);
            }
            return true;
        }
    }

    return false;
}

// ─────────────────────────────────────
void Concentrate::RefreshDailyActivities() {
    nlohmann::json tasks;
    try {
        tasks = m_SQLite->FetchRecurringTasks();
    } catch (const std::exception &e) {
        spdlog::warn("Failed to load daily activities: {}", e.what());
        return;
    }

    std::vector<DailyActivity> updated;
    if (tasks.is_array()) {
        for (const auto &t : tasks) {
            DailyActivity activity;
            activity.name = t.value("name", "");

            if (t.contains("app_ids") && t["app_ids"].is_array()) {
                for (const auto &id : t["app_ids"]) {
                    if (id.is_string()) {
                        activity.appIds.push_back(id.get<std::string>());
                    }
                }
            }

            if (t.contains("app_titles") && t["app_titles"].is_array()) {
                for (const auto &title : t["app_titles"]) {
                    if (title.is_string()) {
                        activity.appTitles.push_back(title.get<std::string>());
                    }
                }
            }

            if (!activity.name.empty()) {
                updated.push_back(std::move(activity));
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_GlobalMutex);
        m_DailyActivities = std::move(updated);
    }
}

// ─────────────────────────────────────
void Concentrate::UpdateAllowedApps() {
    // Update the current task from page id
    std::string id = m_Secrets->LoadSecret("current_task_id");
    spdlog::info("Anytype: Updating allowed apps for task ID: {}", id);

    if (id.empty()) {
        spdlog::warn("Anytype: No current task ID set; skipping allowed apps update");
        std::lock_guard<std::mutex> lock(m_GlobalMutex);
        m_TaskTitle.clear();
        m_AllowedApps.clear();
        m_AllowedWindowTitles.clear();
        return;
    }

    nlohmann::json currentTaskPage = m_Anytype->GetPage(id);
    std::vector<std::string> allowedApps;
    std::vector<std::string> allowedWindowTitles;

    if (!currentTaskPage.contains("object") || !currentTaskPage["object"].is_object()) {
        spdlog::warn("Anytype: Task page is missing object data; skipping allowed apps update");
        std::lock_guard<std::mutex> lock(m_GlobalMutex);
        m_TaskTitle.clear();
        m_AllowedApps.clear();
        m_AllowedWindowTitles.clear();
        return;
    }

    if (currentTaskPage["object"].contains("properties") &&
        currentTaskPage["object"]["properties"].is_array()) {
        const auto &props = currentTaskPage["object"]["properties"];
        for (const auto &prop : props) {
            if (!prop.contains("key") || !prop["key"].is_string()) {
                continue;
            }

            std::string key = prop["key"].get<std::string>();
            if (key == "apps_allowed" && prop.contains("multi_select") &&
                prop["multi_select"].is_array()) {
                for (const auto &tag : prop["multi_select"]) {
                    if (tag.contains("name") && tag["name"].is_string()) {
                        allowedApps.push_back(tag["name"].get<std::string>());
                    }
                }
            }

            if (key == "app_title" && prop.contains("multi_select") &&
                prop["multi_select"].is_array()) {
                for (const auto &tag : prop["multi_select"]) {
                    if (tag.contains("name") && tag["name"].is_string()) {
                        allowedWindowTitles.push_back(tag["name"].get<std::string>());
                    }
                }
            }

            if (key == "category") {
                if (prop.contains("select") && prop["select"].is_object() &&
                    prop["select"].contains("name") && prop["select"]["name"].is_string()) {
                    m_CurrentTaskCategory = prop["select"]["name"].get<std::string>();
                    spdlog::info("Current category is {}", m_CurrentTaskCategory);
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_GlobalMutex);
        if (currentTaskPage["object"].contains("name") &&
            currentTaskPage["object"]["name"].is_string()) {
            m_TaskTitle = currentTaskPage["object"]["name"].get<std::string>();
        } else {
            m_TaskTitle.clear();
        }
        m_AllowedApps = allowedApps;
        m_AllowedWindowTitles = allowedWindowTitles;

        spdlog::info("Anytype: Task '{}' allows {} apps and {} window titles", m_TaskTitle,
                     m_AllowedApps.size(), m_AllowedWindowTitles.size());
    }
}

// ─────────────────────────────────────
bool Concentrate::InitServer() {
    m_Server.set_keep_alive_max_count(1);
    m_Server.set_keep_alive_timeout(1);         // segundos
    m_Server.set_payload_max_length(64 * 1024); // 64 KB

    m_Server.set_read_timeout(5, 0);
    m_Server.set_write_timeout(5, 0);
    m_Server.set_idle_interval(1, 0);

    // Cross-Origin Isolation (COOP/COEP)
    // Enables features like SharedArrayBuffer for the Web UI.
    m_Server.set_default_headers({
        {"Cross-Origin-Opener-Policy", "same-origin"},
        {"Cross-Origin-Embedder-Policy", "require-corp"},
    });

    // static files
    {
        // index.html
        m_Server.Get("/", [this](const httplib::Request &, httplib::Response &res) {
            std::ifstream file(m_Root / "index.html", std::ios::binary);
            if (!file) {
                res.status = 404;
                res.set_content("index.html not found", "text/plain");
                return;
            }
            std::string body((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
            res.set_content(body, "text/html");
        });

        // favicon.svg
        m_Server.Get("/favicon.svg", [this](const httplib::Request &, httplib::Response &res) {
            std::ifstream file(m_Root / "favicon.svg", std::ios::binary);
            if (!file) {
                res.status = 404;
                res.set_content("favicon.svg not found", "text/plain");
                return;
            }

            std::string body((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
            res.set_content(body, "image/svg+xml");
        });

        // style.css
        m_Server.Get("/style.css", [this](const httplib::Request &, httplib::Response &res) {
            std::ifstream file(m_Root / "style.css", std::ios::binary);
            if (!file) {
                res.status = 404;
                res.set_content("style.css not found", "text/plain");
                return;
            }
            std::string body((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
            res.set_content(body, "text/css"); // Correct content type
        });

        // app.js
        m_Server.Get("/app.js", [this](const httplib::Request &, httplib::Response &res) {
            std::ifstream file(m_Root / "app.js", std::ios::binary);
            if (!file) {
                res.status = 404;
                res.set_content("index.html not found", "text/plain");
                return;
            }
            std::string body((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
            res.set_content(body, "application/javascript");
        });

        // main.js
        m_Server.Get("/main.js", [this](const httplib::Request &, httplib::Response &res) {
            std::ifstream file(m_Root / "main.js", std::ios::binary);
            if (!file) {
                res.status = 404;
                res.set_content("main.js not found", "text/plain");
                return;
            }
            std::string body((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
            res.set_content(body, "application/javascript");
        });

        // ES module directories
        m_Server.Get(R"(/(core|modules|views|utils|api)/.*\.js)",
                     [this](const httplib::Request &req, httplib::Response &res) {
                         const std::string path = req.path;
                         if (path.find("..") != std::string::npos ||
                             path.find('\\') != std::string::npos) {
                             res.status = 400;
                             res.set_content("invalid path", "text/plain");
                             return;
                         }

                         std::string rel = path;
                         if (!rel.empty() && rel.front() == '/') {
                             rel.erase(rel.begin());
                         }

                         const std::filesystem::path fsPath = m_Root / rel;
                         std::ifstream file(fsPath, std::ios::binary);
                         if (!file) {
                             res.status = 404;
                             res.set_content("file not found", "text/plain");
                             return;
                         }

                         std::string body((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());

                         const auto ext = fsPath.extension().string();
                         if (ext == ".js") {
                             res.set_content(body, "application/javascript");
                         } else if (ext == ".css") {
                             res.set_content(body, "text/css");
                         } else if (ext == ".svg") {
                             res.set_content(body, "image/svg+xml");
                         } else if (ext == ".html") {
                             res.set_content(body, "text/html");
                         } else {
                             res.set_content(body, "application/octet-stream");
                         }
                     });
    }

    // Anytype API
    {
        m_Server.Post("/api/v1/anytype/auth/challenges",
                      [this](const httplib::Request &, httplib::Response &res) {
                          try {
                              std::string challenge_id = m_Anytype->LoginChallengeId();
                              nlohmann::json resp = {{"challenge_id", challenge_id}};
                              res.status = 200;
                              res.set_content(resp.dump(), "application/json");
                          } catch (const std::exception &e) {
                              res.status = 502;
                              res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                              "application/json");
                          }
                      });

        m_Server.Post("/api/v1/anytype/auth/api_keys", [this](const httplib::Request &req,
                                                              httplib::Response &res) {
            try {
                auto j = nlohmann::json::parse(req.body);
                const std::string challenge_id = j.at("challenge_id").get<std::string>();
                const std::string code = j.at("code").get<std::string>();
                std::string api_key = m_Anytype->CreateApiKey(challenge_id, code);
                nlohmann::json resp = {{"api_key", api_key}};
                res.status = 200;
                res.set_content(resp.dump(), "application/json");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                "application/json");
            }
        });

        m_Server.Get("/api/v1/anytype/spaces",
                     [this](const httplib::Request &, httplib::Response &res) {
                         try {
                             auto spaces_json = m_Anytype->GetSpaces();
                             res.status = 200;
                             res.set_content(spaces_json.dump(), "application/json");
                         } catch (const std::exception &e) {
                             res.status = 502;
                             res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                             "application/json");
                         }
                     });

        m_Server.Get("/api/v1/anytype/tasks_categories",
                     [this](const httplib::Request &, httplib::Response &res) {
                         try {
                             auto spaces_json = m_Anytype->GetCategoriesOfTasks();
                             res.status = 200;
                             res.set_content(spaces_json.dump(), "application/json");
                         } catch (const std::exception &e) {
                             res.status = 502;
                             res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                             "application/json");
                         }
                     });

        m_Server.Post(
            "/api/v1/anytype/space", [this](const httplib::Request &req, httplib::Response &res) {
                auto j = nlohmann::json::parse(req.body);
                const std::string space_id = j.at("space_id").get<std::string>();
                if (space_id.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"space_id cannot be empty"})", "application/json");
                    return;
                }
                m_Anytype->SetDefaultSpace(space_id);
                res.status = 200;
                res.set_content(R"({"status":"ok"})", "application/json");
            });

        m_Server.Get(
            "/api/v1/anytype/tasks", [this](const httplib::Request &, httplib::Response &res) {
                try {
                    nlohmann::json tasks;
                    {
                        std::lock_guard<std::mutex> lock(m_GlobalMutex);
                        tasks = m_Anytype->GetTasks();
                    }
                    res.status = 200;
                    res.set_content(tasks.dump(), "application/json");
                } catch (const std::exception &e) {
                    res.status = 502;
                    res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                    "application/json");
                } catch (...) {
                    res.status = 500;
                    res.set_content(R"({"error":"unknown server error"})", "application/json");
                }
            });
    }

    // Version
    {
        m_Server.Get("/api/v1/version", [](const httplib::Request &, httplib::Response &res) {
            nlohmann::json j = {{"version", CONCENTRATE_VERSION}};
            res.status = 200;
            res.set_content(j.dump(), "application/json");
        });
    }

    // Current State
    {
        m_Server.Get("/api/v1/current", [this](const httplib::Request &, httplib::Response &res) {
            FocusedWindow current;
            {
                std::lock_guard<std::mutex> lock(m_GlobalMutex);
                current = m_Fw;
            }

            nlohmann::json j;
            if (current.valid) {
                j["window_id"] = current.window_id;
                j["title"] = current.title;
                j["app_id"] = current.app_id;
                j["category"] = current.category;
            }

            res.status = 200;
            res.set_content(j.dump(), "application/json");
        });
    }

    // DataBase
    {
        m_Server.Get("/api/v1/history", [&](const httplib::Request &, httplib::Response &res) {
            nlohmann::json history = m_SQLite->FetchHistory();
            res.status = 200;
            res.set_content(history.dump(), "application/json");
        });

        // m_Server.Get("/api/v1/categories", [&](const httplib::Request &, httplib::Response &res)
        // {
        //     nlohmann::json categories = m_SQLite->FetchCategories();
        //     res.status = 200;
        //     res.set_content({}, "application/json");
        // });

        m_Server.Get("/api/v1/events", [&](const httplib::Request &, httplib::Response &res) {
            nlohmann::json events = m_SQLite->FetchEvents();
            res.status = 200;
            res.set_content(events.dump(), "application/json");
        });
    }

    // Update Server
    {
        m_Server.Post(
            "/api/v1/task/set_current", [&](const httplib::Request &req, httplib::Response &res) {
                try {
                    auto json_body = nlohmann::json::parse(req.body);
                    if (!json_body.contains("id") || !json_body["id"].is_string()) {
                        spdlog::warn("[SERVER] Invalid request body, 'id' missing or not a string");
                        res.status = 400;
                        res.set_content("Invalid JSON: 'id' missing or not a string", "text/plain");
                        return;
                    }
                    std::string id = json_body["id"].get<std::string>();

                    // Avoid expensive Anytype calls when clients re-send the same task id.
                    // This also prevents log spam if the UI posts repeatedly.
                    const std::string prev = m_Secrets->LoadSecret("current_task_id");
                    if (prev == id) {
                        res.status = 200;
                        res.set_content("Task unchanged", "text/plain");
                        return;
                    }

                    m_Secrets->SaveSecret("current_task_id", id);
                    UpdateAllowedApps();
                    const bool wasDirty = m_FocusDirty.exchange(true, std::memory_order_relaxed);
                    if (!wasDirty) {
                        WakeScheduler();
                    }
                    res.status = 200;
                    res.set_content("Task updated successfully", "text/plain");

                } catch (const nlohmann::json::parse_error &e) {
                    res.status = 400;
                    res.set_content("Invalid JSON format", "text/plain");
                } catch (const std::exception &e) {
                    res.status = 500;
                    res.set_content("Internal server error", "text/plain");
                }
            });
    }

    // Update focus
    {
        m_Server.Post("/api/v1/focus/rules", [&](const httplib::Request &req,
                                                 httplib::Response &res) {
            auto json_body = nlohmann::json::parse(req.body);
            std::vector<std::string> allowed_app_ids;
            std::vector<std::string> allowed_titles;
            std::string task_title;
            if (json_body.contains("allowed_app_ids") && json_body["allowed_app_ids"].is_array()) {
                for (const auto &app_id : json_body["allowed_app_ids"]) {
                    if (app_id.is_string()) {
                        allowed_app_ids.push_back(app_id.get<std::string>());
                    }
                }
            }
            if (json_body.contains("allowed_titles") && json_body["allowed_titles"].is_array()) {
                for (const auto &title : json_body["allowed_titles"]) {
                    if (title.is_string()) {
                        allowed_titles.push_back(title.get<std::string>());
                    }
                }
            }

            if (json_body.contains("task_title") && json_body["task_title"].is_string()) {
                task_title = json_body["task_title"].get<std::string>();
            }

            {
                std::lock_guard<std::mutex> lock(m_GlobalMutex);
                m_AllowedApps = allowed_app_ids;
                m_AllowedWindowTitles = allowed_titles;
                m_TaskTitle = task_title;
            }

            const bool wasDirty = m_FocusDirty.exchange(true, std::memory_order_relaxed);
            if (!wasDirty) {
                WakeScheduler();
            }
            res.status = 200;
            res.set_content(R"({"status":"ok"})", "application/json");
        });

        m_Server.Get("/api/v1/focus/today", [&](const httplib::Request &req,
                                                httplib::Response &res) {
            try {
                int days = 1;
                if (req.has_param("days")) {
                    const auto &raw = req.get_param_value("days");
                    if (!raw.empty()) {
                        days = std::stoi(raw);
                        if (days < 1) {
                            days = 1;
                        }
                    }
                }

                // Cache results briefly to avoid expensive DB scans when the UI polls frequently.
                const auto now = std::chrono::steady_clock::now();
                {
                    std::lock_guard<std::mutex> lock(m_ApiCacheMutex);
                    auto it = m_FocusSummaryCache.find(days);
                    if (it != m_FocusSummaryCache.end()) {
                        const auto age = now - it->second.first;
                        if (age < std::chrono::seconds(60)) {
                            res.status = 200;
                            res.set_content(it->second.second.dump(), "application/json");
                            return;
                        }
                    }
                }

                nlohmann::json summary = m_SQLite->GetFocusSummary(days);
                nlohmann::json result = {{"focused_seconds", summary.value("focused", 0.0)},
                                         {"unfocused_seconds", summary.value("unfocused", 0.0)}};

                {
                    std::lock_guard<std::mutex> lock(m_ApiCacheMutex);
                    m_FocusSummaryCache[days] = {now, result};
                }

                res.status = 200;
                res.set_content(result.dump(), "application/json");
            } catch (const std::exception &e) {
                res.status = 500;
                res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                "application/json");
            }
        });
    }

    // Recurring
    {
        m_Server.Post("/api/v1/task/recurring_tasks", [&](const httplib::Request &req,
                                                          httplib::Response &res) {
            try {
                if (req.body.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"empty request body"})", "application/json");
                    return;
                }

                auto body = nlohmann::json::parse(req.body);

                std::string name = body.value("name", "");
                std::vector<std::string> appIds = body.value("appIds", std::vector<std::string>{});
                std::vector<std::string> appTitles =
                    body.value("appTitles", std::vector<std::string>{});
                std::string icon = body.value("icon", "");
                std::string color = body.value("color", "");

                if (appIds.size() == 0 && appTitles.size() == 0) {
                    res.status = 400;
                    res.set_content(R"({"error":"appIds and appTitles size is 0"})",
                                    "application/json");
                    return;
                }

                if (name.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"name is required"})", "application/json");
                    return;
                }

                // Try to fetch existing task
                auto tasks = m_SQLite->FetchRecurringTasks();
                auto it = std::find_if(tasks.begin(), tasks.end(), [&](const nlohmann::json &t) {
                    return t.value("name", "") == name;
                });

                if (it != tasks.end()) {
                    m_SQLite->UpdateRecurringTask(name, appIds, appTitles, icon, color);
                } else {
                    // Add new
                    m_SQLite->AddRecurringTask(name, appIds, appTitles, icon, color);
                }

                RefreshDailyActivities();

                res.status = 200;
                res.set_content(R"({"success":true})", "application/json");

            } catch (const nlohmann::json::parse_error &e) {
                res.status = 400;
                res.set_content(std::string(R"({"error":"invalid JSON: ")") + e.what() + R"("})",
                                "application/json");
            } catch (const std::exception &e) {
                res.status = 500;
                res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                "application/json");
            }
        });

        m_Server.Get(
            "/api/v1/task/recurring_tasks", [&](const httplib::Request &, httplib::Response &res) {
                try {
                    const auto now = std::chrono::steady_clock::now();
                    {
                        std::lock_guard<std::mutex> lock(m_ApiCacheMutex);
                        if (!m_RecurringTasksCache.is_null() &&
                            (now - m_RecurringTasksCacheAt) < std::chrono::seconds(60)) {
                            res.status = 200;
                            res.set_content(m_RecurringTasksCache.dump(), "application/json");
                            return;
                        }
                    }

                    nlohmann::json tasks = m_SQLite->FetchRecurringTasks();
                    {
                        std::lock_guard<std::mutex> lock(m_ApiCacheMutex);
                        m_RecurringTasksCache = tasks;
                        m_RecurringTasksCacheAt = now;
                    }

                    res.status = 200;
                    res.set_content(tasks.dump(), "application/json");

                } catch (const std::exception &e) {
                    res.status = 500;
                    res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                    "application/json");
                }
            });

        m_Server.Delete("/api/v1/task/recurring_tasks",
                        [&](const httplib::Request &req, httplib::Response &res) {
                            try {
                                auto nameIt = req.params.find("name");
                                if (nameIt == req.params.end() || nameIt->second.empty()) {
                                    res.status = 400;
                                    res.set_content(R"({"error":"name parameter is required"})",
                                                    "application/json");
                                    return;
                                }

                                std::string name = nameIt->second;

                                // Remove from DB
                                m_SQLite->ExcludeRecurringTask(name);

                                RefreshDailyActivities();

                                res.status = 200;
                                res.set_content(R"({"success":true})", "application/json");

                            } catch (const std::exception &e) {
                                res.status = 500;
                                res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                                "application/json");
                            }
                        });
    }

    // Monitoring
    {
        m_Server.Get("/api/v1/monitoring",
                     [this](const httplib::Request &, httplib::Response &res) {
                         nlohmann::json j = {{"enabled", m_MonitoringEnabled.load()}};
                         res.status = 200;
                         res.set_content(j.dump(), "application/json");
                     });

        m_Server.Post(
            "/api/v1/monitoring", [this](const httplib::Request &req, httplib::Response &res) {
                try {
                    auto j = nlohmann::json::parse(req.body);
                    bool enabled = j.at("enabled").get<bool>();
                    m_MonitoringEnabled.store(enabled);
                    const bool wasPending =
                        m_MonitoringTogglePending.exchange(true, std::memory_order_relaxed);
                    if (!wasPending) {
                        WakeScheduler();
                    }
                    if (m_MonitoringEnabled.load()) {
                        m_Notification->SendNotification("dialog-ok", "Monitoring Enable",
                                                         "Monitoring your apps use");
                    } else {
                        m_Notification->SendNotification("dialog-warning", "Monitoring Disable",
                                                         "Not monitoring your apps use");
                    }
                    m_Secrets->SaveSecret("monitoring_enabled", enabled ? "true" : "false");
                    res.status = 200;
                    res.set_content(R"({"status":"ok"})", "application/json");
                } catch (const std::exception &e) {
                    res.status = 400;
                    res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                    "application/json");
                }
            });

        m_Server.Get(
            "/api/v1/monitoring/summary", [this](const httplib::Request &, httplib::Response &res) {
                try {
                    if (!m_SQLite) {
                        res.status = 503;
                        res.set_content(R"({"error":"database not ready"})", "application/json");
                        return;
                    }
                    nlohmann::json j = m_SQLite->GetTodayMonitoringTimeSummary();
                    res.status = 200;
                    res.set_content(j.dump(), "application/json");
                } catch (const std::exception &e) {
                    res.status = 500;
                    res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                    "application/json");
                }
            });
    }

    // Settings
    {
        m_Server.Get("/api/v1/settings", [this](const httplib::Request &, httplib::Response &res) {
            spdlog::info("[SERVER] Get Settings");
            std::string current_task_id = m_Secrets->LoadSecret("current_task_id");
            nlohmann::json j = {{"monitoring_enabled", m_MonitoringEnabled.load()},
                                {"current_task_id", current_task_id}};
            res.status = 200;
            res.set_content(j.dump(), "application/json");
        });
    }

    // Pomodoro
    {
        m_Server.Get(
            "/api/v1/pomodoro/state", [this](const httplib::Request &, httplib::Response &res) {
                try {
                    if (!m_SQLite) {
                        res.status = 503;
                        res.set_content(R"({"error":"database not ready"})", "application/json");
                        return;
                    }
                    nlohmann::json state = m_SQLite->GetPomodoroState();
                    res.status = 200;
                    res.set_content(state.dump(), "application/json");
                } catch (const std::exception &e) {
                    res.status = 500;
                    res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                    "application/json");
                }
            });

        m_Server.Post("/api/v1/pomodoro/state", [this](const httplib::Request &req,
                                                       httplib::Response &res) {
            try {
                if (!m_SQLite) {
                    res.status = 503;
                    res.set_content(R"({"error":"database not ready"})", "application/json");
                    return;
                }
                if (req.body.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"empty request body"})", "application/json");
                    return;
                }
                auto body = nlohmann::json::parse(req.body);
                std::string error;
                if (!m_SQLite->SavePomodoroState(body, error)) {
                    res.status = 400;
                    res.set_content(
                        nlohmann::json({{"error", error.empty() ? "save failed" : error}}).dump(),
                        "application/json");
                    return;
                }
                res.status = 200;
                res.set_content(R"({"success":true})", "application/json");
            } catch (const nlohmann::json::parse_error &e) {
                res.status = 400;
                res.set_content(std::string(R"({"error":"invalid JSON: ")") + e.what() + R"("})",
                                "application/json");
            } catch (const std::exception &e) {
                res.status = 500;
                res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                "application/json");
            }
        });

        m_Server.Get(
            "/api/v1/pomodoro/today", [this](const httplib::Request &, httplib::Response &res) {
                try {
                    if (!m_SQLite) {
                        res.status = 503;
                        res.set_content(R"({"error":"database not ready"})", "application/json");
                        return;
                    }
                    nlohmann::json stats = m_SQLite->GetPomodoroTodayStats();
                    // Provide a convenient minutes field for the UI
                    const int focusSeconds = stats.value("focus_seconds", 0);
                    stats["focus_minutes"] = static_cast<int>(std::round(focusSeconds / 60.0));
                    res.status = 200;
                    res.set_content(stats.dump(), "application/json");
                } catch (const std::exception &e) {
                    res.status = 500;
                    res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                    "application/json");
                }
            });

        // Call this when a focus block finishes (increments daily count and total focus seconds)
        m_Server.Post("/api/v1/pomodoro/focus/complete", [this](const httplib::Request &req,
                                                                httplib::Response &res) {
            try {
                if (!m_SQLite) {
                    res.status = 503;
                    res.set_content(R"({"error":"database not ready"})", "application/json");
                    return;
                }
                if (req.body.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"empty request body"})", "application/json");
                    return;
                }
                auto body = nlohmann::json::parse(req.body);
                const int focusSeconds = body.value("focus_seconds", 0);
                std::string error;
                if (!m_SQLite->IncrementPomodoroFocusToday(focusSeconds, error)) {
                    res.status = 400;
                    res.set_content(
                        nlohmann::json({{"error", error.empty() ? "increment failed" : error}})
                            .dump(),
                        "application/json");
                    return;
                }
                nlohmann::json stats = m_SQLite->GetPomodoroTodayStats();
                const int focusSecondsOut = stats.value("focus_seconds", 0);
                stats["focus_minutes"] = static_cast<int>(std::round(focusSecondsOut / 60.0));
                res.status = 200;
                res.set_content(stats.dump(), "application/json");
            } catch (const nlohmann::json::parse_error &e) {
                res.status = 400;
                res.set_content(std::string(R"({"error":"invalid JSON: ")") + e.what() + R"("})",
                                "application/json");
            } catch (const std::exception &e) {
                res.status = 500;
                res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                "application/json");
            }
        });
    }

    // Update focus
    {
        m_Server.Post("/api/v1/focus/rules", [&](const httplib::Request &req,
                                                 httplib::Response &res) {
            auto json_body = nlohmann::json::parse(req.body);
            std::vector<std::string> allowed_app_ids;
            std::vector<std::string> allowed_titles;
            std::string task_title;
            if (json_body.contains("allowed_app_ids") && json_body["allowed_app_ids"].is_array()) {
                for (const auto &app_id : json_body["allowed_app_ids"]) {
                    if (app_id.is_string()) {
                        allowed_app_ids.push_back(app_id.get<std::string>());
                    }
                }
            }
            if (json_body.contains("allowed_titles") && json_body["allowed_titles"].is_array()) {
                for (const auto &title : json_body["allowed_titles"]) {
                    if (title.is_string()) {
                        allowed_titles.push_back(title.get<std::string>());
                    }
                }
            }

            if (json_body.contains("task_title") && json_body["task_title"].is_string()) {
                task_title = json_body["task_title"].get<std::string>();
            }

            {
                std::lock_guard<std::mutex> lock(m_GlobalMutex);
                m_AllowedApps = allowed_app_ids;
                m_AllowedWindowTitles = allowed_titles;
                m_TaskTitle = task_title;
            }

            const bool wasDirty = m_FocusDirty.exchange(true, std::memory_order_relaxed);
            if (!wasDirty) {
                WakeScheduler();
            }
            res.status = 200;
            res.set_content(R"({"status":"ok"})", "application/json");
        });

        m_Server.Get("/api/v1/focus/today", [&](const httplib::Request &req,
                                                httplib::Response &res) {
            try {
                int days = 1;
                if (req.has_param("days")) {
                    const auto &raw = req.get_param_value("days");
                    if (!raw.empty()) {
                        days = std::stoi(raw);
                        if (days < 1) {
                            days = 1;
                        }
                    }
                }

                nlohmann::json summary = m_SQLite->GetFocusSummary(days);
                nlohmann::json result = {{"focused_seconds", summary.value("focused", 0.0)},
                                         {"unfocused_seconds", summary.value("unfocused", 0.0)}};

                res.status = 200;
                res.set_content(result.dump(), "application/json");
            } catch (const std::exception &e) {
                res.status = 500;
                res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                "application/json");
            }
        });
    }

    // Get History
    {
        m_Server.Get("/api/v1/history/category-time",
                     [&](const httplib::Request &req, httplib::Response &res) {
                         try {
                             int days = 30;
                             if (req.has_param("days")) {
                                 const auto &raw = req.get_param_value("days");
                                 if (!raw.empty()) {
                                     days = std::stoi(raw);
                                     if (days < 1) {
                                         days = 1;
                                     }
                                 }
                             }

                             nlohmann::json summary = m_SQLite->GetCategoryTimeSummary(days);
                             res.status = 200;
                             res.set_content(summary.dump(), "application/json");
                         } catch (const std::exception &e) {
                             res.status = 500;
                             res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                             "application/json");
                         }
                     });

        m_Server.Get("/api/v1/history/category-focus",
                     [&](const httplib::Request &req, httplib::Response &res) {
                         try {
                             int days = 30;
                             if (req.has_param("days")) {
                                 const auto &raw = req.get_param_value("days");
                                 if (!raw.empty()) {
                                     days = std::stoi(raw);
                                     if (days < 1) {
                                         days = 1;
                                     }
                                 }
                             }

                             nlohmann::json summary = m_SQLite->GetCategoryFocusSplit(days);
                             res.status = 200;
                             res.set_content(summary.dump(), "application/json");
                         } catch (const std::exception &e) {
                             res.status = 500;
                             res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                             "application/json");
                         }
                     });

        m_Server.Get("/api/v1/focus/category-percentages",
                     [&](const httplib::Request &req, httplib::Response &res) {
                         try {
                             int days = 1;
                             if (req.has_param("days")) {
                                 days = std::stoi(req.get_param_value("days"));
                             }

                             nlohmann::json summary = m_SQLite->GetFocusPercentageByCategory(days);

                             res.status = 200;
                             res.set_content(summary.dump(), "application/json");

                         } catch (const std::exception &e) {
                             res.status = 500;
                             res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                             "application/json");
                         }
                     });

        // Today Category Summary
        m_Server.Get("/api/v1/focus/today/categories",
                     [&](const httplib::Request &, httplib::Response &res) {
                         try {
                             nlohmann::json summary = m_SQLite->GetTodayFocusTimeSummary();
                             res.status = 200;
                             res.set_content(summary.dump(), "application/json");
                         } catch (const std::exception &e) {
                             res.status = 500;
                             res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                             "application/json");
                         }
                     });

        m_Server.Get("/api/v1/focus/app-usage", [&](const httplib::Request &req,
                                                    httplib::Response &res) {
            try {
                int days = 1;

                if (req.has_param("days")) {
                    const auto &raw = req.get_param_value("days");
                    if (!raw.empty()) {
                        days = std::stoi(raw);
                        if (days < 1) {
                            days = 1;
                        }
                    }
                }
                nlohmann::json data = m_SQLite->FetchDailyAppUsageByAppId(days);
                res.status = 200;
                res.set_content(data.dump(), "application/json");

            } catch (const std::exception &e) {
                spdlog::error("app-usage endpoint failed: {}", e.what());
                res.status = 500;
                res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
            }
        });

        m_Server.Get("/api/v1/daily_activities/today",
                     [&](const httplib::Request &, httplib::Response &res) {
                         try {
                             nlohmann::json summary = m_SQLite->GetTodayDailyActivitiesSummary();
                             res.status = 200;
                             res.set_content(summary.dump(), "application/json");
                         } catch (const std::exception &e) {
                             res.status = 500;
                             res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                             "application/json");
                         }
                     });
    }

    // Special End Points
    {
        // CORS preflight for browser-based clients (including extensions).
        m_Server.Options("/api/v1/special_project",
                         [](const httplib::Request &, httplib::Response &res) {
                             res.set_header("Access-Control-Allow-Origin", "*");
                             res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
                             res.set_header("Access-Control-Allow-Headers", "Content-Type");
                             res.set_header("Access-Control-Max-Age", "86400");
                             res.status = 204;
                         });

        m_Server.Post("/api/v1/special_project", [this](const httplib::Request &req,
                                                        httplib::Response &res) {
            // CORS for actual request (also set on error responses).
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");

            try {
                auto j = nlohmann::json::parse(req.body);

                // Extract project name
                if (!j.contains("title") || !j["title"].is_string() || !j.contains("focus") ||
                    !j["focus"].is_boolean() || !j.contains("app_id") || !j["app_id"].is_string()

                ) {
                    res.status = 400; // Bad request
                    res.set_content(R"({"error":"Missing or invalid title, status,  app_id"})",
                                    "application/json");
                    return;
                }

                std::string appId = j["app_id"];
                std::string projectName = j["title"];
                bool focus = j["focus"];

                if (focus) {
                    m_SpecialProjectFocused = true;
                } else {
                    m_SpecialProjectFocused = false;
                }
                m_SpecialProjectTitle = projectName;
                m_SpecialAppId = appId;
                const bool wasDirty = m_FocusDirty.exchange(true, std::memory_order_relaxed);
                if (!wasDirty) {
                    WakeScheduler();
                }
                nlohmann::json response = {{"status", "ok"}, {"project_name", projectName}};
                res.status = 200;
                res.set_content(response.dump(), "application/json");

            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
            }
        });
    }

    // clang-format on
    const std::string host = "127.0.0.1";
    int port = static_cast<int>(m_Port);
    m_Thread = std::thread([this, host, port] { m_Server.listen(host, port); });
    return true;
}

// ─────────────────────────────────────
std::filesystem::path Concentrate::GetBinaryPath() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) {
        exit(1);
    }
    buf[len] = '\0';

    std::filesystem::path binDir(buf);
    binDir = binDir.parent_path();

    if (binDir.filename() == "bin") {
        binDir = binDir.parent_path() / "share" / "concentrate";
    }

    // Fallbacks if assets are not in the computed path
    const std::vector<std::filesystem::path> candidates = {binDir, "/usr/local/share/concentrate",
                                                           "/usr/share/concentrate"};

    for (const auto &p : candidates) {
        if (std::filesystem::exists(p / "index.html")) {
            return p;
        }
    }

    return binDir;
}

// ─────────────────────────────────────
std::filesystem::path Concentrate::GetDBPath() {
    const char *xdgDataHome = std::getenv("XDG_DATA_HOME");
    std::filesystem::path baseDir;
    if (xdgDataHome && *xdgDataHome) {
        baseDir = xdgDataHome;
    } else {
        const char *home = std::getenv("HOME");
        if (!home || !*home) {
            std::cerr << "Error: HOME environment variable not set\n";
            exit(1);
        }
        baseDir = std::filesystem::path(home) / ".local" / "share";
    }

    std::filesystem::path dbPath = baseDir / "concentrate" / "data.sqlite";
    std::error_code ec;
    std::filesystem::create_directories(dbPath.parent_path(), ec);
    if (ec) {
        std::cerr << "Error creating directories: " << ec.message() << "\n";
        exit(1);
    }

    return dbPath;
}
