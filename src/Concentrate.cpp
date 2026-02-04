#include "Concentrate.hpp"

#include <string>
#include <limits.h>
#include <unistd.h>
#include <thread>
#include <chrono>
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
    double hydrationIntervalMinutes = 10.0;
    double dailyLiters = m_Hydration->GetLiters();
    double litersPerReminder = dailyLiters / (10.0 * 60.0 / hydrationIntervalMinutes);
    auto lastHydrationNotification =
        std::chrono::steady_clock::now() -
        std::chrono::minutes(static_cast<int>(hydrationIntervalMinutes));
    auto lastClimateUpdate = std::chrono::steady_clock::now();

    // Time
    std::string monitoring_str = m_Secrets->LoadSecret("monitoring_enabled");
    m_MonitoringEnabled = monitoring_str.empty() ? true : (monitoring_str == "true");

    // monitoring
    if (!m_MonitoringEnabled) {
        m_Notification->SendNotification("dialog-warning", "Concentrate", "Apps monitoring is off");
    }
    auto lastMonitoringNotification = std::chrono::steady_clock::now() - std::chrono::minutes(10);

    // Tracking model:
    // - When an active interval starts, INSERT a row with start=end=now, duration=0.
    // - While it continues, UPDATE that same row with end=now and duration=(now-start).
    // - When it ends/changes, do a final UPDATE and then INSERT the next interval.
    bool hasOpenInterval = false;
    FocusState openState = IDLE;
    auto intervalStart = std::chrono::steady_clock::now();
    auto lastDbFlush = intervalStart;
    std::string openAppId;
    std::string openTitle;
    std::string openCategory;

    {
        std::lock_guard<std::mutex> lock(m_GlobalMutex);
        m_LastState = IDLE;
        m_LastAppId.clear();
        m_LastTitle.clear();
        m_LastCategory.clear();
        m_HasLastRecord = false;
    }

    UpdateAllowedApps();
    RefreshDailyActivities();

    // Unfocused warning: while UNFOCUSED, warn every 15 seconds (after an initial 15s grace).
    bool inUnfocusedStreak = false;
    auto unfocusedSince = std::chrono::steady_clock::now();
    auto lastUnfocusedWarningAt = unfocusedSince;
    const auto unfocusedWarnEvery = std::chrono::seconds(15);

    while (true) {
        auto now = std::chrono::steady_clock::now();

        // Notify if monitoring is disabled every 5 minutes
        if (!m_MonitoringEnabled && now - lastMonitoringNotification >= std::chrono::minutes(5)) {
            m_Notification->SendNotification("dialog-warning", "Concentrate",
                                             "Application monitoring is currently disabled.");
            lastMonitoringNotification = now;
        }

        if (!m_MonitoringEnabled) {
            // Reset unfocused streak tracking while monitoring is disabled.
            inUnfocusedStreak = false;
            unfocusedSince = now;
            lastUnfocusedWarningAt = now;

            // Close any open interval before entering the disabled/idle state.
            if (hasOpenInterval && openState != IDLE) {
                const double endUnix = ToUnixTime(now);
                const double startUnix = ToUnixTime(intervalStart);
                const double duration = endUnix - startUnix;
                if (duration > 0.0) {
                    if (!m_SQLite->UpdateEventNew(openAppId, openTitle, openCategory, endUnix,
                                                  duration, openState)) {
                        m_SQLite->InsertEventNew(openAppId, openTitle, openCategory, startUnix,
                                                 endUnix, duration, openState);
                    }
                }
            }

            m_Tray->SetTrayIcon(IDLE);

            hasOpenInterval = false;
            openState = IDLE;
            openAppId.clear();
            openTitle.clear();
            openCategory.clear();

            {
                std::lock_guard<std::mutex> lock(m_GlobalMutex);
                m_LastState = IDLE;
                m_LastAppId.clear();
                m_LastTitle.clear();
                m_LastCategory.clear();
                m_HasLastRecord = false;
            }

            // Pump tray DBus messages (non-blocking)
            if (m_Tray) {
                m_Tray->Poll();

                if (m_Tray->TakeOpenUiRequested()) {
                    std::system(
                        ("xdg-open http://127.0.0.1:" + std::to_string(m_Port) + "/ &").c_str());
                }
                if (m_Tray->TakeExitRequested()) {
                    spdlog::info("Exit requested from tray");
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(m_Ping));
            continue;
        }

        // Get focused window and current state
        m_Fw = m_Window->GetFocusedWindow();
        if (m_SpecialProjectFocused) {
            m_Fw.app_id = m_SpecialAppId;
            m_Fw.title = m_SpecialProjectTitle;
            spdlog::debug("Special project focus override: app_id='{}', title='{}'", m_Fw.app_id,
                          m_Fw.title);
        }

        FocusState currentState = AmIFocused(m_Fw);

        // Repeating unfocused warning.
        if (currentState == UNFOCUSED) {
            if (!inUnfocusedStreak) {
                inUnfocusedStreak = true;
                unfocusedSince = now;
                lastUnfocusedWarningAt = now;
            } else if ((now - unfocusedSince) >= unfocusedWarnEvery &&
                       (now - lastUnfocusedWarningAt) >= unfocusedWarnEvery) {
                if (m_Notification) {
                    m_Notification->SendNotification(
                        "concentrate-unfocused", "Concentrate",
                        "Focus: you've been unfocused for more than 15 seconds.");
                }
                lastUnfocusedWarningAt = now;
            }
        } else {
            // Reset streak when focused or idle.
            inUnfocusedStreak = false;
            unfocusedSince = now;
            lastUnfocusedWarningAt = now;
        }

        if (m_CurrentTaskCategory.empty()) {
            m_CurrentTaskCategory = "Uncategorized";
            spdlog::debug("Current task category defaulted to '{}'", m_CurrentTaskCategory);
        }

        // IDLE: close any open interval and reset tracking.
        if (currentState == IDLE) {
            if (hasOpenInterval && openState != IDLE) {
                const double endUnix = ToUnixTime(now);
                const double startUnix = ToUnixTime(intervalStart);
                const double duration = endUnix - startUnix;

                if (duration > 0.0) {
                    if (!m_SQLite->UpdateEventNew(openAppId, openTitle, openCategory, endUnix,
                                                  duration, openState)) {
                        m_SQLite->InsertEventNew(openAppId, openTitle, openCategory, startUnix,
                                                 endUnix, duration, openState);
                    }
                    spdlog::info(
                        "Focus event closed (idle): state={}, app_id='{}', title='{}', duration={}",
                        static_cast<int>(openState), openAppId, openTitle, duration);
                }
            }

            hasOpenInterval = false;
            openState = IDLE;
            openAppId.clear();
            openTitle.clear();
            openCategory.clear();

            {
                std::lock_guard<std::mutex> lock(m_GlobalMutex);
                m_LastState = IDLE;
                m_LastAppId.clear();
                m_LastTitle.clear();
                m_LastCategory.clear();
                m_HasLastRecord = false;
            }

            std::this_thread::sleep_for(std::chrono::seconds(m_Ping));
            continue;
        }

        // Update climate/hydration (every 3 hours)
        if (now - lastClimateUpdate >= std::chrono::hours(3)) {
            spdlog::info("Updating location and weather info...");
            try {
                m_Hydration->GetLocation();
                m_Hydration->GetHydrationRecommendation();
            } catch (const std::exception &e) {
                spdlog::warn("Failed to update location/climate: {}", e.what());
            }
            lastClimateUpdate = now;
        }

        // Hydration reminders
        if (now - lastHydrationNotification >=
            std::chrono::minutes(static_cast<int>(hydrationIntervalMinutes))) {
            m_Notification->SendNotification(
                "dialog-info", "Concentrate",
                fmt::format("Time to drink water! ~{:.2f} L since last reminder.",
                            litersPerReminder));
            lastHydrationNotification = now;
        }

        const std::string currAppId = m_Fw.app_id;
        const std::string currTitle = m_Fw.title;
        const std::string currCategory = m_Fw.category;

        if (!hasOpenInterval) {
            // Start a new active interval and INSERT immediately so subsequent UPDATEs have
            // something to target.
            hasOpenInterval = true;
            openState = currentState;
            openAppId = currAppId;
            openTitle = currTitle;
            openCategory = currCategory;
            intervalStart = now;
            lastDbFlush = now;

            const double startUnix = ToUnixTime(intervalStart);
            m_SQLite->InsertEventNew(openAppId, openTitle, openCategory, startUnix, startUnix, 0.0,
                                     openState);
        } else {
            const bool changed = (currentState != openState) || (currAppId != openAppId) ||
                                 (currTitle != openTitle) || (currCategory != openCategory);

            if (changed) {
                // Close previous interval with a final UPDATE.
                const double endUnix = ToUnixTime(now);
                const double startUnix = ToUnixTime(intervalStart);
                const double duration = endUnix - startUnix;
                if (duration > 0.0) {
                    if (!m_SQLite->UpdateEventNew(openAppId, openTitle, openCategory, endUnix,
                                                  duration, openState)) {
                        m_SQLite->InsertEventNew(openAppId, openTitle, openCategory, startUnix,
                                                 endUnix, duration, openState);
                    }
                }

                // Start next interval and INSERT.
                openState = currentState;
                openAppId = currAppId;
                openTitle = currTitle;
                openCategory = currCategory;
                intervalStart = now;
                lastDbFlush = now;

                const double startUnix2 = ToUnixTime(intervalStart);
                m_SQLite->InsertEventNew(openAppId, openTitle, openCategory, startUnix2, startUnix2,
                                         0.0, openState);
            } else if (now - lastDbFlush >= std::chrono::seconds(15)) {
                // Periodic progress update: duration must be TOTAL (end - intervalStart), not just
                // the last 15s slice.
                const double endUnix = ToUnixTime(now);
                const double startUnix = ToUnixTime(intervalStart);
                const double duration = endUnix - startUnix;
                if (duration > 0.0) {
                    m_SQLite->UpdateEventNew(openAppId, openTitle, openCategory, endUnix, duration,
                                             openState);
                }
                lastDbFlush = now;
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_GlobalMutex);
            if (hasOpenInterval && openState != IDLE) {
                m_LastRecord = intervalStart; // interval start, not last flush
                m_LastState = openState;
                m_LastAppId = openAppId;
                m_LastTitle = openTitle;
                m_LastCategory = openCategory;
                m_HasLastRecord = true;
            } else {
                m_LastState = IDLE;
                m_LastAppId.clear();
                m_LastTitle.clear();
                m_LastCategory.clear();
                m_HasLastRecord = false;
            }
        }

        // Update tray icon state (focused/unfocused)
        if (m_Tray) {
            m_Tray->SetTrayIcon(currentState);
            m_Tray->Poll();
            if (m_Tray->TakeOpenUiRequested()) {
                std::system(
                    ("xdg-open http://127.0.0.1:" + std::to_string(m_Port) + "/ &").c_str());
            }
            if (m_Tray->TakeExitRequested()) {
                spdlog::info("Exit requested from tray");
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(m_Ping));
    }
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
        lastStateSnapshot = m_LastState;
        lastRecordSnapshot = m_LastRecord;
        lastAppIdSnapshot = m_LastAppId;
        lastTitleSnapshot = m_LastTitle;
        lastCategorySnapshot = m_LastCategory;
        hasLastRecordSnapshot = m_HasLastRecord;
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
    spdlog::debug("Task category set: '{}'", Fw.category);

    spdlog::debug("FOCUSED: {}", isFocusedWindow ? "YES" : "NO");
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
                         nlohmann::json j = {{"enabled", m_MonitoringEnabled}};
                         res.status = 200;
                         res.set_content(j.dump(), "application/json");
                     });

        m_Server.Post(
            "/api/v1/monitoring", [this](const httplib::Request &req, httplib::Response &res) {
                try {
                    auto j = nlohmann::json::parse(req.body);
                    bool enabled = j.at("enabled").get<bool>();
                    m_MonitoringEnabled = enabled;
                    if (m_MonitoringEnabled) {
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
    }

    // Settings
    {
        m_Server.Get("/api/v1/settings", [this](const httplib::Request &, httplib::Response &res) {
            spdlog::info("[SERVER] Get Settings");
            std::string current_task_id = m_Secrets->LoadSecret("current_task_id");
            nlohmann::json j = {{"monitoring_enabled", m_MonitoringEnabled},
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
        m_Server.Post("/api/v1/special_project", [this](const httplib::Request &req,
                                                        httplib::Response &res) {
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
        binDir = binDir.parent_path() / "share" / "Concentrate";
    }

    // Fallbacks if assets are not in the computed path
    const std::vector<std::filesystem::path> candidates = {binDir, "/usr/local/share/Concentrate",
                                                           "/usr/share/Concentrate"};

    for (const auto &p : candidates) {
        if (std::filesystem::exists(p / "index.html")) {
            return p;
        }
    }

    return binDir;
}

// ─────────────────────────────────────
std::filesystem::path Concentrate::GetDBPath() {
    const char* xdgDataHome = std::getenv("XDG_DATA_HOME");
    std::filesystem::path baseDir;
    if (xdgDataHome && *xdgDataHome) {
        baseDir = xdgDataHome;
    } else {
        const char* home = std::getenv("HOME");
        if (!home || !*home) {
            std::cerr << "Error: HOME environment variable not set\n";
            exit(1);
        }
        baseDir = std::filesystem::path(home) / ".local" / "share";
    }

    std::filesystem::path dbPath = baseDir / "Concentrate" / "data.sqlite";
    std::error_code ec;
    std::filesystem::create_directories(dbPath.parent_path(), ec);
    if (ec) {
        std::cerr << "Error creating directories: " << ec.message() << "\n";
        exit(1);
    }

    return dbPath;
}

