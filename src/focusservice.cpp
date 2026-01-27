#include "focusservice.hpp"
#include <string>
#include <limits.h>
#include <unistd.h>
#include <thread>
#include <chrono>

#include <httplib.h>

// ─────────────────────────────────────
FocusService::FocusService(const unsigned port, const unsigned ping) : m_Port(port), m_Ping(ping) {
    spdlog::set_level(spdlog::level::debug);
    // Server
    m_Root = GetBinaryPath();
    if (!std::filesystem::exists(m_Root)) {
        std::cerr << "Root does not exists: " << m_Root << std::endl;
        exit(1);
    }

    std::filesystem::path dbpath = GetDBPath();
    InitServer();

    spdlog::info("Static WebSite Root, {}!", m_Root.string());
    spdlog::info("DataBase path: {}!", dbpath.string());
    spdlog::info("Serving on: http://localhost:{}", m_Port);

    // Secrets
    m_Secrets = std::make_unique<Secrets>();

    // SQlite
    m_SQLite = std::make_unique<SQLite>(dbpath);

    // Anytype
    m_Anytype = std::make_unique<Anytype>();
    UpdateAllowedApps();

    // Windows API (get AppID, Title)
    m_Window = std::make_unique<Window>();

    // Notifications
    m_Notification = std::make_unique<Notification>();

    FocusedWindow previousWindow;
    auto currentTimePoint = std::chrono::system_clock::now();
    auto timeSinceEpoch = currentTimePoint.time_since_epoch();
    std::chrono::duration<double> durationInSeconds = timeSinceEpoch;
    double lastStartTime = durationInSeconds.count();
    double unfocusedStartTime = 0;

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(m_Ping * 1000));
        m_Fw = m_Window->GetFocusedWindow();
        bool isFocusedWindow = true;
        if (!m_AllowedApps.empty() || !m_AllowedWindowTitles.empty()) {
            isFocusedWindow = false;
            for (const auto &allowedApp : m_AllowedApps) {
                if (m_Fw.app_id.find(allowedApp) != std::string::npos) {
                    isFocusedWindow = true;
                    break;
                }
            }

            if (!isFocusedWindow && !m_AllowedWindowTitles.empty()) {
                for (const auto &allowedTitle : m_AllowedWindowTitles) {
                    if (m_Fw.title.find(allowedTitle) != std::string::npos) {
                        isFocusedWindow = true;
                        break;
                    }
                }
            }
        }

        m_IsFocused = isFocusedWindow;
        spdlog::debug("Focused: {}", m_IsFocused);

        auto currentTime = std::chrono::system_clock::now();
        auto timeSinceEpoch = currentTime.time_since_epoch();
        std::chrono::duration<double> secondsSinceEpoch = timeSinceEpoch;
        double now = secondsSinceEpoch.count();

        if (!m_IsFocused) {
            if (unfocusedStartTime == 0) {
                unfocusedStartTime = now;
            } else {
                double elapsed = now - unfocusedStartTime;
                if (elapsed >= ONFOCUSWARNINGAFTER) {
                    int intervals = static_cast<int>(elapsed / ONFOCUSWARNINGAFTER);
                    for (int i = 0; i < intervals; ++i) {
                        m_Notification->SendNotification(
                            "Focus Ended", "You are unfocused for " +
                                               std::to_string(ONFOCUSWARNINGAFTER) + " seconds");
                    }
                    unfocusedStartTime = now;
                }
            }
        } else {
            unfocusedStartTime = 0;
        }

        if (m_Fw.app_id != previousWindow.app_id || m_Fw.title != previousWindow.title) {
            double duration = now - lastStartTime;
            if (!previousWindow.app_id.empty()) {
                m_SQLite->InsertEvent(previousWindow.app_id, previousWindow.window_id,
                                      previousWindow.title, lastStartTime, now, duration);
            }
            previousWindow = m_Fw;
            lastStartTime = now;
        }
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
void FocusService::UpdateAllowedApps() {
    // Update the current task from page id
    std::string id = m_Secrets->LoadSecret("current_task_id");
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

        spdlog::info("Allowed Apps Count: {}", m_AllowedApps.size());
        spdlog::info("Allowed Window Titles Count: {}", m_AllowedWindowTitles.size());
    }
}

// ─────────────────────────────────────
bool FocusService::InitServer() {
    // static files
    {
        // index.html
        m_Server.Get("/", [this](const httplib::Request &, httplib::Response &res) {
            spdlog::info("Reading index.html");
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
            spdlog::info("Reading favicon.svg");
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
            spdlog::info("Reading style.css");
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
            spdlog::info("Reading app.js");
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
        m_Server.Post("/anytype/auth/challenges",
                      [this](const httplib::Request &, httplib::Response &res) {
                          spdlog::info("Anytype Challenges");
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

        m_Server.Post(
            "/anytype/auth/api_keys", [this](const httplib::Request &req, httplib::Response &res) {
                try {
                    spdlog::info("Anytype API Keys");
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

        m_Server.Get("/anytype/spaces", [this](const httplib::Request &, httplib::Response &res) {
            try {
                spdlog::info("Get Anytype Spaces");
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
            "/anytype/space", [this](const httplib::Request &req, httplib::Response &res) {
                spdlog::info("Set Anytype Default Space");
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

        m_Server.Get("/anytype/tasks", [this](const httplib::Request &, httplib::Response &res) {
            try {
                spdlog::info("Get Anytype Tasks");
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
        m_Server.Get("/current", [this](const httplib::Request &, httplib::Response &res) {
            spdlog::info("Get Current Window");
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
        m_Server.Get("/history", [&](const httplib::Request &, httplib::Response &res) {
            spdlog::info("Get Apps History Use");
            nlohmann::json history = m_SQLite->FetchHistory();
            res.status = 200;
            res.set_content(history.dump(), "application/json");
        });

        m_Server.Get("/categories", [&](const httplib::Request &, httplib::Response &res) {
            spdlog::info("Get Apps Categories");
            nlohmann::json categories = m_SQLite->FetchCategories();
            res.status = 200;
            res.set_content(categories.dump(), "application/json");
        });

        m_Server.Get("/events", [&](const httplib::Request &, httplib::Response &res) {
            spdlog::info("Get Events");
            nlohmann::json events = m_SQLite->FetchEvents();
            res.status = 200;
            res.set_content(events.dump(), "application/json");
        });
    }

    // Update Server
    {
        m_Server.Post(
            "/task/set_current", [&](const httplib::Request &req, httplib::Response &res) {
                spdlog::info("Received request to set current task");

                try {
                    auto json_body = nlohmann::json::parse(req.body);
                    if (!json_body.contains("id") || !json_body["id"].is_string()) {
                        spdlog::warn("Invalid request body, 'id' missing or not a string");
                        res.status = 400;
                        res.set_content("Invalid JSON: 'id' missing or not a string", "text/plain");
                        return;
                    }
                    std::string id = json_body["id"].get<std::string>();
                    m_Secrets->SaveSecret("current_task_id", id);
                    res.status = 200;
                    res.set_content("Task updated successfully", "text/plain");

                } catch (const nlohmann::json::parse_error &e) {
                    spdlog::error("Failed to parse JSON: {}", e.what());
                    res.status = 400;
                    res.set_content("Invalid JSON format", "text/plain");
                } catch (const std::exception &e) {
                    spdlog::error("Unexpected error: {}", e.what());
                    res.status = 500;
                    res.set_content("Internal server error", "text/plain");
                }
            });
    }

    // Update focus
    {
        m_Server.Post("/focus/rules", [&](const httplib::Request &req, httplib::Response &res) {
            spdlog::info("Get Focus Rules");
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
