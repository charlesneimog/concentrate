#include "http_server.h"

#include "anytype_api.h"
#include "common.h"
#include "focus_rules.h"
#include "notification.h"
#include "secret_store.h"
#include "sqlite_store.h"

#include <httplib.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
struct StaticFile {
    std::string body;
    std::string content_type;
};

using StaticFileMap = std::unordered_map<std::string, StaticFile>;

static std::filesystem::path resolve_static_path(const std::string &base_dir,
                                                 const std::string &filename) {
    const char *env_path = std::getenv("NIRI_INDEX_PATH");
    std::vector<std::filesystem::path> candidates;
    if (env_path && *env_path) {
        candidates.emplace_back(std::filesystem::path(env_path) / filename);
    }
    candidates.emplace_back(std::filesystem::path(base_dir) / filename);
    candidates.emplace_back(std::filesystem::current_path() / filename);
#ifdef NIRI_SOURCE_DIR
    candidates.emplace_back(std::filesystem::path(NIRI_SOURCE_DIR) / filename);
#endif
#ifdef NIRI_SHARE_DIR
    candidates.emplace_back(std::filesystem::path(NIRI_SHARE_DIR) / filename);
#endif

    std::filesystem::path target;
    for (const auto &candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            target = candidate;
            break;
        }
    }

    return target;
}

static bool load_static_file(StaticFileMap &files, const std::string &base_dir,
                             const std::string &filename, const std::string &content_type) {
    std::filesystem::path target = resolve_static_path(base_dir, filename);
    if (target.empty()) {
        return false;
    }

    std::ifstream file(target, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    files[filename] = StaticFile{std::move(content), content_type};
    return true;
}

static void serve_file(const StaticFileMap &files, const std::string &filename,
                       const char *content_type, httplib::Response &res) {
    auto it = files.find(filename);
    if (it == files.end()) {
        res.status = 404;
        res.set_content("not found", "text/plain");
        return;
    }

    res.status = 200;
    const auto &body = it->second.body;
    const char *ctype = content_type ? content_type : it->second.content_type.c_str();
    res.set_content(body.data(), body.size(), ctype);
}

static std::string &json_buffer() {
    thread_local std::string buffer;
    return buffer;
}

static void set_json_response(httplib::Response &res, const json &payload, int status = 200) {
    auto &buffer = json_buffer();
    buffer = payload.dump();
    res.status = status;
    res.set_content_provider(
        buffer.size(),
        "application/json",
        [](size_t offset, size_t length, httplib::DataSink &sink) {
            const auto &data = json_buffer();
            if (offset >= data.size()) {
                return false;
            }
            const size_t remaining = data.size() - offset;
            const size_t to_write = std::min(length, remaining);
            sink.write(data.data() + offset, to_write);
            return true;
        });
}

class ServerState {
  public:
    ServerState(std::string base_dir,
                std::string db_path,
                std::mutex &current_mutex,
                json &current_focus)
        : base_dir_(std::move(base_dir)),
          db_path_(std::move(db_path)),
          current_mutex_(current_mutex),
          current_focus_(current_focus) {
        load_static_file(static_files_, base_dir_, "index.html", "text/html");
        load_static_file(static_files_, base_dir_, "app.js", "application/javascript");
        thread_ = std::thread([this]() { run(); });
    }

    ~ServerState() {
        server_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

  private:
    void run() {
        server_.Post("/tasks", [&](const httplib::Request &req, httplib::Response &res) {
            printf("[POST] Getting tasks\n");
            try {
                json data = parse_json_or_throw(req.body);
                std::string error;
                if (!create_task(db_path_, data, error)) {
                    res.status = 400;
                    res.set_content(error, "text/plain");
                    return;
                }
                res.status = 200;
                res.set_content("ok", "text/plain");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server_.Post("/tasks/update", [&](const httplib::Request &req, httplib::Response &res) {
            printf("[POST] Updating tasks\n");
            try {
                json data = parse_json_or_throw(req.body);
                std::string error;
                if (!update_task(db_path_, data, error)) {
                    res.status = error == "task not found" ? 404 : 400;
                    res.set_content(error, "text/plain");
                    return;
                }
                res.status = 200;
                res.set_content("ok", "text/plain");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server_.Get("/", [&](const httplib::Request &, httplib::Response &res) {
            printf("[GET] Get index.html\n");
            serve_file(static_files_, "index.html", "text/html", res);
        });

        server_.Get("/index.html", [&](const httplib::Request &, httplib::Response &res) {
            printf("[GET] Get index.html\n");
            serve_file(static_files_, "index.html", "text/html", res);
        });

        server_.Get("/app.js", [&](const httplib::Request &, httplib::Response &res) {
            printf("[GET] Get app.js\n");
            serve_file(static_files_, "app.js", "application/javascript", res);
        });

        server_.Get("/events", [&](const httplib::Request &, httplib::Response &res) {
            printf("[GET] Get Events\n");
            try {
                json rows = fetch_events(db_path_);
                set_json_response(res, rows, 200);
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server_.Get("/tasks", [&](const httplib::Request &, httplib::Response &res) {
            printf("[GET] Get tasks\n");
            try {
                json rows = fetch_tasks(db_path_);
                set_json_response(res, rows, 200);
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server_.Get("/anytype/tasks", [&](const httplib::Request &, httplib::Response &res) {
            printf("[GET] Get anytype tasks\n");
            json tasks;
            std::string error;
            if (!fetch_anytype_tasks_live(tasks, error)) {
                res.status = 500;
                res.set_content(error.empty() ? "anytype request failed" : error, "text/plain");
                return;
            }
            set_json_response(res, tasks, 200);
        });

        server_.Post("/anytype/config", [&](const httplib::Request &req, httplib::Response &res) {
            printf("[POST] Updating anytype config\n");
            try {
                json data = parse_json_or_throw(req.body);
                std::string api_key = get_string(data, "api_key", "");
                std::string space_id = get_string(data, "space_id", "");
                if (api_key.empty() || space_id.empty()) {
                    res.status = 400;
                    res.set_content("missing api_key or space_id", "text/plain");
                    return;
                }
                if (!save_secret_string("anytype_api_key", api_key)) {
                    res.status = 500;
                    res.set_content("failed to store api_key", "text/plain");
                    return;
                }
                if (!save_secret_string("anytype_space_id", space_id)) {
                    res.status = 500;
                    res.set_content("failed to store space_id", "text/plain");
                    return;
                }
                res.status = 200;
                res.set_content("ok", "text/plain");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server_.Post("/anytype/refresh", [&](const httplib::Request &, httplib::Response &res) {
            printf("[POST] Refresh anytype tasks\n");
            json tasks;
            std::string error;
            if (!fetch_anytype_tasks_live(tasks, error)) {
                res.status = 500;
                res.set_content(error.empty() ? "anytype request failed" : error, "text/plain");
                return;
            }
            set_json_response(res, tasks, 200);
        });

        server_.Get("/categories", [&](const httplib::Request &, httplib::Response &res) {
            printf("[GET] Getting categories\n");
            try {
                json rows = fetch_categories(db_path_);
                set_json_response(res, rows, 200);
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server_.Get("/history", [&](const httplib::Request &, httplib::Response &res) {
            printf("[GET] Getting history\n");
            try {
                json rows = fetch_history(db_path_);
                set_json_response(res, rows, 200);
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server_.Post("/history/category", [&](const httplib::Request &req, httplib::Response &res) {
            printf("[GET] Getting history categories\n");
            try {
                json data = parse_json_or_throw(req.body);
                std::string app_id = get_string(data, "app_id", "");
                std::string title = get_string(data, "title", "");
                std::string category = get_string(data, "category", "");
                std::string error;
                if (!upsert_activity_category(db_path_, app_id, title, category, error)) {
                    res.status = 400;
                    res.set_content(error, "text/plain");
                    return;
                }
                res.status = 200;
                res.set_content("ok", "text/plain");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server_.Post("/focus/rules", [&](const httplib::Request &req, httplib::Response &res) {
            printf("[POST] Update focus rules\n");
            try {
                json data = parse_json_or_throw(req.body);
                FocusRules rules;
                if (data.contains("allowed_app_ids") && data["allowed_app_ids"].is_array()) {
                    for (const auto &entry : data["allowed_app_ids"]) {
                        if (entry.is_string()) {
                            rules.allowed_app_ids.push_back(entry.get<std::string>());
                        }
                    }
                }
                if (data.contains("allowed_titles") && data["allowed_titles"].is_array()) {
                    for (const auto &entry : data["allowed_titles"]) {
                        if (entry.is_string()) {
                            rules.allowed_titles.push_back(entry.get<std::string>());
                        }
                    }
                }
                rules.task_title = get_string(data, "task_title", "");
                set_focus_rules(std::move(rules));
                res.status = 200;
                res.set_content("ok", "text/plain");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server_.Post("/notify", [&](const httplib::Request &req, httplib::Response &res) {
            printf("[POST] Notify\n");
            try {
                json data = parse_json_or_throw(req.body);
                std::string summary = get_string(data, "summary", "FocusService");
                std::string body = get_string(data, "body", "");
                if (body.empty()) {
                    res.status = 400;
                    res.set_content("missing body", "text/plain");
                    return;
                }
                std::string error;
                if (!send_focus_notification(summary, body, error)) {
                    res.status = 500;
                    res.set_content(error.empty() ? "notify failed" : error, "text/plain");
                    return;
                }
                res.status = 200;
                res.set_content("ok", "text/plain");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server_.Get("/current", [&](const httplib::Request &, httplib::Response &res) {
            printf("[GET] Getting current\n");
            json snapshot;
            {
                std::lock_guard<std::mutex> lock(current_mutex_);
                snapshot = current_focus_;
            }
            set_json_response(res, snapshot, 200);
        });

        server_.set_error_handler([&](const httplib::Request &, httplib::Response &res) {
            res.status = 404;
            res.set_content("not found", "text/plain");
        });

        const std::string host = "127.0.0.1";
        const int port = 8079;
        std::cout << "Listening on http://" << host << ":" << port << "\n";
        server_.listen(host, port);
    }

    std::string base_dir_;
    std::string db_path_;
    std::mutex &current_mutex_;
    json &current_focus_;
    StaticFileMap static_files_;
    httplib::Server server_;
    std::thread thread_;
};

static std::unique_ptr<ServerState> g_server;
static std::once_flag g_server_once;
} // namespace

void start_http_server(const std::string &base_dir, const std::string &db_path,
                       std::mutex &current_mutex, json &current_focus) {
    std::call_once(g_server_once, [&]() {
        g_server = std::make_unique<ServerState>(base_dir, db_path, current_mutex, current_focus);
    });
}