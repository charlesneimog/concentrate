#include "http_server.h"

#include "anytype_api.h"
#include "common.h"
#include "secret_store.h"
#include "sqlite_store.h"

#include <httplib.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
struct StaticFile {
    std::string body;
    std::string content_type;
};

using StaticFileMap = std::unordered_map<std::string, StaticFile>;

struct HttpResult {
    int status = 200;
    std::string body;
    std::string content_type = "text/plain";
    std::vector<std::pair<std::string, std::string>> headers;
};

class ThreadPool {
  public:
    explicit ThreadPool(size_t thread_count) : stop_(false) {
        if (thread_count == 0) {
            thread_count = 2;
        }
                max_queue_ = thread_count * 64;
        workers_.reserve(thread_count);
        for (size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this]() {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        cv_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) {
                            return;
                        }
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto &worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    template <class F>
    auto submit(F &&f) -> std::future<decltype(f())> {
        using R = decltype(f());
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> result = task->get_future();
        bool run_inline = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (tasks_.size() >= max_queue_) {
                run_inline = true;
            } else {
                tasks_.emplace([task]() { (*task)(); });
            }
        }
        if (run_inline) {
            (*task)();
        } else {
            cv_.notify_one();
        }
        return result;
    }

  private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_;
    size_t max_queue_ = 0;
};

class AnytypeRefreshWorker {
  public:
    explicit AnytypeRefreshWorker(std::chrono::seconds interval)
        : interval_(interval), pending_(false), stop_(false), worker_([this]() { run(); }) {}

    ~AnytypeRefreshWorker() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void request_refresh() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_ = true;
        }
        cv_.notify_all();
    }

  private:
    void run() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stop_) {
            cv_.wait_for(lock, interval_, [this]() { return stop_ || pending_; });
            if (stop_) {
                break;
            }
            pending_ = false;
            lock.unlock();
            std::string refresh_error;
            refresh_anytype_cache(&refresh_error);
            lock.lock();
        }
    }

    std::chrono::seconds interval_;
    bool pending_;
    bool stop_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
};

static std::filesystem::path resolve_static_path(const std::string &base_dir, const std::string &filename) {
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

static bool load_static_file(StaticFileMap &files,
                             const std::string &base_dir,
                             const std::string &filename,
                             const std::string &content_type) {
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

static void serve_file(const StaticFileMap &files,
                       const std::string &filename,
                       const char *content_type,
                       httplib::Response &res) {
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

static void apply_result(httplib::Response &res, const HttpResult &result) {
    for (const auto &header : result.headers) {
        res.set_header(header.first.c_str(), header.second.c_str());
    }
    res.status = result.status;
    res.set_content(result.body, result.content_type);
}
} // namespace

void start_http_server(const std::string &base_dir,
                       const std::string &db_path,
                       std::mutex &current_mutex,
                       json &current_focus) {
    std::thread([&, base_dir, db_path]() {
        httplib::Server server;
        const size_t hw_threads = std::thread::hardware_concurrency();
        const size_t pool_size = hw_threads > 2 ? hw_threads - 1 : 2;
        ThreadPool pool(pool_size);
        AnytypeRefreshWorker anytype_refresher(std::chrono::minutes(5));

        StaticFileMap static_files;
        load_static_file(static_files, base_dir, "index.html", "text/html");
        load_static_file(static_files, base_dir, "app.js", "application/javascript");

        server.Post("/tasks", [&](const httplib::Request &req, httplib::Response &res) {
            std::string body = req.body;
            auto fut = pool.submit([body = std::move(body), db_path]() {
                HttpResult out;
                try {
                    json data = parse_json_or_throw(body);
                    std::string error;
                    if (!create_task(db_path, data, error)) {
                        out.status = 400;
                        out.body = error;
                        return out;
                    }
                    out.status = 200;
                    out.body = "ok";
                } catch (const std::exception &e) {
                    out.status = 400;
                    out.body = e.what();
                }
                return out;
            });
            apply_result(res, fut.get());
        });

        server.Post("/tasks/update", [&](const httplib::Request &req, httplib::Response &res) {
            std::string body = req.body;
            auto fut = pool.submit([body = std::move(body), db_path]() {
                HttpResult out;
                try {
                    json data = parse_json_or_throw(body);
                    std::string error;
                    if (!update_task(db_path, data, error)) {
                        out.status = error == "task not found" ? 404 : 400;
                        out.body = error;
                        return out;
                    }
                    out.status = 200;
                    out.body = "ok";
                } catch (const std::exception &e) {
                    out.status = 400;
                    out.body = e.what();
                }
                return out;
            });
            apply_result(res, fut.get());
        });

        server.Get("/", [&](const httplib::Request &, httplib::Response &res) {
            serve_file(static_files, "index.html", "text/html", res);
        });

        server.Get("/index.html", [&](const httplib::Request &, httplib::Response &res) {
            serve_file(static_files, "index.html", "text/html", res);
        });

        server.Get("/app.js", [&](const httplib::Request &, httplib::Response &res) {
            serve_file(static_files, "app.js", "application/javascript", res);
        });

        server.Get("/events", [&](const httplib::Request &, httplib::Response &res) {
            auto fut = pool.submit([db_path]() {
                HttpResult out;
                out.content_type = "application/json";
                try {
                    json rows = fetch_events(db_path);
                    out.status = 200;
                    out.body = std::move(rows.dump());
                } catch (const std::exception &e) {
                    out.status = 400;
                    out.content_type = "text/plain";
                    out.body = e.what();
                }
                return out;
            });
            apply_result(res, fut.get());
        });

        server.Get("/tasks", [&](const httplib::Request &, httplib::Response &res) {
            auto fut = pool.submit([db_path]() {
                HttpResult out;
                out.content_type = "application/json";
                try {
                    json rows = fetch_tasks(db_path);
                    out.status = 200;
                    out.body = std::move(rows.dump());
                } catch (const std::exception &e) {
                    out.status = 400;
                    out.content_type = "text/plain";
                    out.body = e.what();
                }
                return out;
            });
            apply_result(res, fut.get());
        });

        server.Get("/anytype/tasks", [&](const httplib::Request &, httplib::Response &res) {
            try {
                AnytypeCache cache = get_anytype_cache();
                if ((!cache.tasks.is_array() || cache.tasks.empty()) && !cache.error.empty()) {
                    res.status = 500;
                    res.set_content(cache.error, "text/plain");
                    return;
                }
                res.status = 200;
                res.set_header("X-Anytype-Updated", std::to_string(cache.updated_at));
                std::string payload = cache.tasks.dump();
                res.set_content(std::move(payload), "application/json");
            } catch (const std::exception &e) {
                res.status = 500;
                res.set_content(e.what(), "text/plain");
            }
        });

        server.Post("/anytype/config", [&](const httplib::Request &req, httplib::Response &res) {
            std::string body = req.body;
            auto fut = pool.submit([body = std::move(body), &anytype_refresher]() {
                HttpResult out;
                try {
                    json data = parse_json_or_throw(body);
                    std::string api_key = get_string(data, "api_key", "");
                    std::string space_id = get_string(data, "space_id", "");
                    if (api_key.empty() || space_id.empty()) {
                        out.status = 400;
                        out.body = "missing api_key or space_id";
                        return out;
                    }
                    if (!save_secret_string("anytype_api_key", api_key)) {
                        out.status = 500;
                        out.body = "failed to store api_key";
                        return out;
                    }
                    if (!save_secret_string("anytype_space_id", space_id)) {
                        out.status = 500;
                        out.body = "failed to store space_id";
                        return out;
                    }
                    anytype_refresher.request_refresh();
                    out.status = 200;
                    out.body = "ok";
                } catch (const std::exception &e) {
                    out.status = 400;
                    out.body = e.what();
                }
                return out;
            });
            apply_result(res, fut.get());
        });

        server.Post("/anytype/refresh", [&](const httplib::Request &, httplib::Response &res) {
            anytype_refresher.request_refresh();
            res.status = 200;
            res.set_content("ok", "text/plain");
        });

        server.Get("/categories", [&](const httplib::Request &, httplib::Response &res) {
            auto fut = pool.submit([db_path]() {
                HttpResult out;
                out.content_type = "application/json";
                try {
                    json rows = fetch_categories(db_path);
                    out.status = 200;
                    out.body = std::move(rows.dump());
                } catch (const std::exception &e) {
                    out.status = 400;
                    out.content_type = "text/plain";
                    out.body = e.what();
                }
                return out;
            });
            apply_result(res, fut.get());
        });

        server.Get("/history", [&](const httplib::Request &, httplib::Response &res) {
            auto fut = pool.submit([db_path]() {
                HttpResult out;
                out.content_type = "application/json";
                try {
                    json rows = fetch_history(db_path);
                    out.status = 200;
                    out.body = std::move(rows.dump());
                } catch (const std::exception &e) {
                    out.status = 400;
                    out.content_type = "text/plain";
                    out.body = e.what();
                }
                return out;
            });
            apply_result(res, fut.get());
        });

        server.Post("/history/category", [&](const httplib::Request &req, httplib::Response &res) {
            std::string body = req.body;
            auto fut = pool.submit([body = std::move(body), db_path]() {
                HttpResult out;
                try {
                    json data = parse_json_or_throw(body);
                    std::string app_id = get_string(data, "app_id", "");
                    std::string title = get_string(data, "title", "");
                    std::string category = get_string(data, "category", "");
                    std::string error;
                    if (!upsert_activity_category(db_path, app_id, title, category, error)) {
                        out.status = 400;
                        out.body = error;
                        return out;
                    }
                    out.status = 200;
                    out.body = "ok";
                } catch (const std::exception &e) {
                    out.status = 400;
                    out.body = e.what();
                }
                return out;
            });
            apply_result(res, fut.get());
        });

        server.Get("/current", [&](const httplib::Request &, httplib::Response &res) {
            json snapshot;
            {
                std::lock_guard<std::mutex> lock(current_mutex);
                snapshot = current_focus;
            }
            res.status = 200;
            std::string payload = snapshot.dump();
            res.set_content(std::move(payload), "application/json");
        });

        server.set_error_handler([&](const httplib::Request &, httplib::Response &res) {
            res.status = 404;
            res.set_content("not found", "text/plain");
        });

        const std::string host = "127.0.0.1";
        const int port = 8079;
        std::cout << "Listening on http://" << host << ":" << port << "\n";
        server.listen(host, port);
    }).detach();
}
