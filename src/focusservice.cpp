#include "focusservice.hpp"

#include <string>
#include <limits.h>
#include <unistd.h>
#include <thread>
#include <chrono>

// ─────────────────────────────────────
FocusService::FocusService(const unsigned port, const unsigned ping, LogLevel log_level)
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
    UpdateAllowedApps();

    // Windows API (get AppID, Title)
    m_Window = std::make_unique<Window>();
    spdlog::info("Window API initialized");

    // Notifications
    m_Notification = std::make_unique<Notification>();
    spdlog::info("Notification system initialized");

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
    auto now = std::chrono::system_clock::now();

    // monitoring
    if (!m_MonitoringEnabled) {
        m_Notification->SendNotification("dialog-warning", "FocusService",
                                         "Apps monitoring is off");
    }
    auto lastMonitoringNotification = std::chrono::steady_clock::now() - std::chrono::minutes(10);

    FocusState lastState{};
    auto stateSince = std::chrono::steady_clock::now();
    auto lastRecord = stateSince;
    std::string lastAppId;
    std::string lastTitle;

    while (true) {
        auto now = std::chrono::steady_clock::now();

        // Notify if monitoring is disabled every 10 minutes
        if (!m_MonitoringEnabled && now - lastMonitoringNotification >= std::chrono::minutes(5)) {
            m_Notification->SendNotification("dialog-warning", "FocusService",
                                             "Application monitoring is currently disabled.");
            lastMonitoringNotification = now;
        }

        if (!m_MonitoringEnabled) {
            lastState = IDLE;
            lastRecord = now;
            stateSince = now;
            lastAppId.clear();
            lastTitle.clear();
            std::this_thread::sleep_for(std::chrono::seconds(m_Ping));
            continue;
        }

        // Get focused window and current state
        m_Fw = m_Window->GetFocusedWindow();
        FocusState currentState = AmIFocused(m_Fw);
        if (m_SpecialProjectFocused) {
            m_Fw.app_id = m_SpecialAppId;
            m_Fw.title = m_SpecialProjectTitle;
        }

        // IDLE: close nothing, record nothing, reset clocks
        if (currentState == IDLE) {
            lastState = IDLE;
            lastRecord = now;
            stateSince = now;
            lastAppId.clear();
            lastTitle.clear();

            std::this_thread::sleep_for(std::chrono::seconds(m_Ping));
            continue;
        }
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

        if (now - lastHydrationNotification >=
            std::chrono::minutes(static_cast<int>(hydrationIntervalMinutes))) {
            m_Notification->SendNotification(
                "dialog-info", "FocusService",
                fmt::format("Time to drink water! ~{:.2f} L since last reminder.",
                            litersPerReminder));
            lastHydrationNotification = now;
        }


        // Compute slice: ALWAYS from lastRecord -> now
        double startUnix = ToUnixTime(lastRecord);
        double endUnix = ToUnixTime(now);
        double duration = endUnix - startUnix;

        if (duration <= 0.0) {
            std::this_thread::sleep_for(std::chrono::seconds(m_Ping));
            continue;
        }

        bool stateChanged = (currentState != lastState);
        bool appChanged = (m_Fw.app_id != lastAppId) || (m_Fw.title != lastTitle);

        // Close previous interval if needed
        if ((stateChanged || appChanged) && lastState != IDLE) {
            m_SQLite->InsertFocusState(static_cast<int>(lastState), startUnix, endUnix, duration);

            m_SQLite->InsertEvent(lastAppId, m_Fw.window_id, lastTitle, startUnix, endUnix,
                                  duration);

            spdlog::info("Focus event: state={}, app_id='{}', title='{}', duration={}",
                         static_cast<int>(lastState), lastAppId, lastTitle, duration);

            lastRecord = now;
        }

        // Periodic snapshot (same app + same state)
        if (!stateChanged && !appChanged && now - lastRecord >= std::chrono::seconds(15)) {

            startUnix = ToUnixTime(lastRecord);
            endUnix = ToUnixTime(now);
            duration = endUnix - startUnix;

            if (duration > 0.0) {
                m_SQLite->InsertFocusState(static_cast<int>(lastState), startUnix, endUnix,
                                           duration);

                m_SQLite->InsertEvent(m_Fw.app_id, m_Fw.window_id, m_Fw.title, startUnix, endUnix,
                                      duration);

                spdlog::info("Focus snapshot: state={}, app_id='{}', title='{}', start={}, end={}, "
                             "duration={}",
                             static_cast<int>(lastState), m_Fw.app_id, m_Fw.title, startUnix,
                             endUnix, duration);

                lastRecord = now;
            }
        }

        // Start new logical interval
        if (stateChanged) {
            stateSince = now;
            lastState = currentState;
        }

        if (appChanged) {
            lastAppId = m_Fw.app_id;
            lastTitle = m_Fw.title;
        }

        std::this_thread::sleep_for(std::chrono::seconds(m_Ping));
    }
}

// ─────────────────────────────────────
FocusService::~FocusService() {
    m_Server.stop();
    if (m_Thread.joinable()) {
        m_Thread.join();
    }
}

// ─────────────────────────────────────
double FocusService::ToUnixTime(std::chrono::steady_clock::time_point steady_tp) {
    auto now_steady = std::chrono::steady_clock::now();
    auto now_system = std::chrono::system_clock::now();

    auto delta = steady_tp - now_steady;
    auto system_tp = now_system + delta;

    return std::chrono::duration<double>(system_tp.time_since_epoch()).count();
}

// ─────────────────────────────────────
FocusState FocusService::AmIFocused(FocusedWindow Fw) {
    // If the focused window is invalid (no app_id and no title), treat as IDLE
    if (Fw.app_id.empty() && Fw.title.empty()) {
        spdlog::info("FOCUSED: IDLE (no app_id or title)");
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

    spdlog::debug("FOCUSED: {}", isFocusedWindow ? "YES" : "NO");
    return isFocusedWindow ? FOCUSED : UNFOCUSED;
}

// ─────────────────────────────────────
void FocusService::UpdateAllowedApps() {
    // Update the current task from page id
    std::string id = m_Secrets->LoadSecret("current_task_id");
    spdlog::info("Anytype: Updating allowed apps for task ID: {}", id);
    nlohmann::json currentTaskPage = m_Anytype->GetPage(id);
    std::vector<std::string> allowedApps;
    std::vector<std::string> allowedWindowTitles;

    if (currentTaskPage.contains("object") && currentTaskPage["object"].contains("properties")) {
        const auto &props = currentTaskPage["object"]["properties"];
        for (const auto &prop : props) {
            if (!prop.contains("key")) {
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
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_GlobalMutex);
        m_TaskTitle = currentTaskPage["object"]["name"].get<std::string>();
        m_AllowedApps = allowedApps;
        m_AllowedWindowTitles = allowedWindowTitles;

        spdlog::info("Anytype: Task '{}' allows {} apps and {} window titles", m_TaskTitle,
                     m_AllowedApps.size(), m_AllowedWindowTitles.size());
    }
}

// ─────────────────────────────────────
bool FocusService::InitServer() {
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

        m_Server.Get("/api/v1/categories", [&](const httplib::Request &, httplib::Response &res) {
            nlohmann::json categories = m_SQLite->FetchCategories();
            res.status = 200;
            res.set_content(categories.dump(), "application/json");
        });

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

        m_Server.Get("/api/v1/focus/today", [&](const httplib::Request &, httplib::Response &res) {
            try {
                nlohmann::json summary = m_SQLite->FetchTodayFocusSummary();
                nlohmann::json result = {
                    {"focused_seconds", summary.value("focused_seconds", 0.0)},
                    {"unfocused_seconds", summary.value("unfocused_seconds", 0.0)}};

                res.status = 200;
                res.set_content(result.dump(), "application/json");
            } catch (const std::exception &e) {
                res.status = 500;
                res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                "application/json");
            }
        });
    }

    {
        m_Server.Post("/api/v1/task/recurring_tasks", [&](const httplib::Request &req,
                                                          httplib::Response &res) {
            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body);
            } catch (const nlohmann::json::parse_error &) {
                res.status = 400;
                res.set_content("Invalid JSON", "text/plain");
                return;
            }

            // Validate presence and type
            if (!body.contains("app_ids") || !body.contains("app_titles") ||
                !body.contains("icon") || !body.contains("color") || !body.contains("name") ||
                !body["app_ids"].is_array() || !body["app_titles"].is_array()) {
                res.status = 400;
                res.set_content("Invalid JSON: 'appIds' and 'appTitles' must be arrays, and "
                                "all fields required",
                                "text/plain");
                return;
            }

            std::string error;
            bool ok = m_SQLite->UpsertRecurringTask(body, error);
            if (!ok) {
                res.status = 500;
                res.set_content("Failed to save recurring task: " + error, "text/plain");
                return;
            }

            res.status = 200;
            res.set_content("Recurring task configured", "text/plain");
        });

        m_Server.Get(
            "/api/v1/task/recurring_tasks", [&](const httplib::Request &, httplib::Response &res) {
                try {
                    nlohmann::json dailyTasks = m_SQLite->FetchRecurringTasks();
                    res.set_content(dailyTasks.dump(), "application/json");
                    res.status = 200;
                } catch (const std::exception &e) {
                    res.status = 500;
                    res.set_content("Failed to fetch recurring tasks: " + std::string(e.what()),
                                    "text/plain");
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

        m_Server.Get("/api/v1/focus/today", [&](const httplib::Request &, httplib::Response &res) {
            try {
                nlohmann::json summary = m_SQLite->FetchTodayFocusSummary();
                nlohmann::json result = {
                    {"focused_seconds", summary.value("focused_seconds", 0.0)},
                    {"unfocused_seconds", summary.value("unfocused_seconds", 0.0)}};

                res.status = 200;
                res.set_content(result.dump(), "application/json");
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
                             nlohmann::json summary = m_SQLite->FetchTodayCategorySummary();
                             res.status = 200;
                             res.set_content(summary.dump(), "application/json");
                         } catch (const std::exception &e) {
                             res.status = 500;
                             res.set_content(std::string(R"({"error":")") + e.what() + R"("})",
                                             "application/json");
                         }
                     });

        // Register route before server starts
        m_Server.Post("/api/v1/focus/exclude_task", [&](const httplib::Request &req,
                                                        httplib::Response &res) {
            try {
                auto body = nlohmann::json::parse(req.body);
                std::string taskName = body.value("name", "");

                if (taskName.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"missing task name"})", "application/json");
                    return;
                }

                std::string error;
                bool ok = m_SQLite->ExcludeRecurringTask(taskName, error);
                if (!ok) {
                    res.status = 500;
                    res.set_content(nlohmann::json({{"error", error}}).dump(), "application/json");
                    return;
                }

                res.status = 200;
                res.set_content(nlohmann::json({{"status", "excluded"}, {"name", taskName}}).dump(),
                                "application/json");

            } catch (const std::exception &e) {
                res.status = 500;
                res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
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
    const std::string host = "0.0.0.0";
    int port = static_cast<int>(m_Port);
    m_Thread = std::thread([this, host, port] { m_Server.listen(host, port); });
    return true;
}

// ─────────────────────────────────────
std::filesystem::path FocusService::GetBinaryPath() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) {
        exit(1);
    }
    buf[len] = '\0';

    std::filesystem::path binDir(buf);
    binDir = binDir.parent_path();

    if (binDir.filename() == "bin") {
        binDir = binDir.parent_path() / "share" / "focusservice";
    }

    // Fallbacks if assets are not in the computed path
    const std::vector<std::filesystem::path> candidates = {binDir, "/usr/local/share/focusservice",
                                                           "/usr/share/focusservice"};

    for (const auto &p : candidates) {
        if (std::filesystem::exists(p / "index.html")) {
            return p;
        }
    }

    return binDir;
}

// ─────────────────────────────────────
std::filesystem::path FocusService::GetDBPath() {
    std::string home_dir;
    const char *home_env = std::getenv("HOME");
    if (home_env && *home_env) {
        home_dir = home_env;
    } else {
        std::cerr << "Error: HOME environment variable not set\n";
        exit(1);
    }
    std::filesystem::path path(home_dir + "/.local/focusservice/data.sqlite");
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return path;
}
