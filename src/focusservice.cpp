#include "focusservice.hpp"
#include <string>
#include <limits.h>
#include <unistd.h>
#include <thread>
#include <chrono>

#include <httplib.h>

// ─────────────────────────────────────
FocusService::FocusService(const unsigned port, const unsigned ping) : m_Port(port), m_Ping(ping) {
    // Server
    m_Root = GetBinaryPath();
    if (!std::filesystem::exists(m_Root)) {
        std::cerr << "Root does not exists: " << m_Root << std::endl;
        exit(1);
    }
    std::filesystem::path dbpath = GetDBPath();
    InitServer();
    std::cout << "FocusService running on http://localhost:" << std::to_string(m_Port) << std::endl;

    // SQlite
    m_SQLite = std::make_unique<SQLite>(dbpath);

    // Anytype
    m_Anytype = std::make_unique<Anytype>();

    // Windows API (get AppID, Title)
    m_Window = std::make_unique<Window>();

    // Secrets
    m_Secrets = std::make_unique<Secrets>();

    // Notifications
    m_Notification = std::make_unique<Notification>();

    FocusedWindow previousWindow;
    double lastStartTime =
        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(m_Ping * 1000));
        m_Fw = m_Window->GetFocusedWindow();
        if (m_Fw.app_id != previousWindow.app_id || m_Fw.title != previousWindow.title) {
            double now =
                std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
                    .count();

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
bool FocusService::InitServer() {
    // static files
    {
        // index.html
        m_Server.Get("/", [this](const httplib::Request &, httplib::Response &res) {
            std::lock_guard<std::mutex> lock(m_GlobalMutex);
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

        // app.js
        m_Server.Get("/app.js", [this](const httplib::Request &, httplib::Response &res) {
            std::lock_guard<std::mutex> lock(m_GlobalMutex);
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
            nlohmann::json history = m_SQLite->FetchHistory();
            res.status = 200;
            res.set_content(history.dump(), "application/json");
        });

        m_Server.Get("/categories", [&](const httplib::Request &, httplib::Response &res) {
            nlohmann::json categories = m_SQLite->FetchCategories();
            res.status = 200;
            res.set_content(categories.dump(), "application/json");
        });

        m_Server.Get("/events", [&](const httplib::Request &, httplib::Response &res) {
            nlohmann::json events = m_SQLite->FetchEvents();
            res.status = 200;
            res.set_content(events.dump(), "application/json");
        });
    }

    // Update focus
    {
        m_Server.Post("/focus/rules", [&](const httplib::Request &req, httplib::Response &res) {
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
        });
    }

    // clang-format on
    const std::string host = "127.0.0.1";
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
    return std::filesystem::path(buf).parent_path();
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
