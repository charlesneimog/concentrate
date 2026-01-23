#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <strings.h>
#include <string>
#include <thread>
#include <vector>

using steady_clock = std::chrono::steady_clock;
using json = nlohmann::json;

struct FocusedWindow {
    int window_id = -1;
    std::string title;
    std::string app_id;
    bool valid = false;
};

namespace {
constexpr const char *kDbPath = "/home/neimog/.local/niriuse/data.sqlite";

std::string get_exe_dir() {
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return exe.parent_path().string();
    }
    return std::filesystem::current_path().string();
}

std::string ensure_db_path() {
    std::filesystem::path path(kDbPath);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return path.string();
}

void exec_ignoring_errors(sqlite3 *db, const std::string &sql) {
    char *errmsg = nullptr;
    sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errmsg);
    if (errmsg) {
        sqlite3_free(errmsg);
    }
}

void init_db(const std::string &db_path) {
    sqlite3 *db = nullptr;
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        if (db) {
            sqlite3_close(db);
        }
        throw std::runtime_error("unable to open database");
    }

    exec_ignoring_errors(db,
        "CREATE TABLE IF NOT EXISTS focus_events ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "app_id TEXT,"
        "window_id INTEGER,"
        "title TEXT,"
        "start_time REAL,"
        "end_time REAL,"
        "duration REAL"
        ")");

    exec_ignoring_errors(db,
        "CREATE TABLE IF NOT EXISTS focus_tasks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "category TEXT,"
        "task TEXT,"
        "start_time REAL,"
        "end_time REAL,"
        "allowed_app_ids TEXT,"
        "allowed_titles TEXT,"
        "excluded INTEGER DEFAULT 0,"
        "done INTEGER DEFAULT 0"
        ")");

    exec_ignoring_errors(db,
        "CREATE TABLE IF NOT EXISTS focus_categories ("
        "category TEXT PRIMARY KEY,"
        "allowed_app_ids TEXT,"
        "allowed_titles TEXT,"
        "updated_at REAL"
        ")");

    exec_ignoring_errors(db,
        "CREATE TABLE IF NOT EXISTS focus_activity_categories ("
        "app_id TEXT,"
        "title TEXT,"
        "category TEXT,"
        "updated_at REAL,"
        "PRIMARY KEY(app_id, title)"
        ")");

    exec_ignoring_errors(db, "ALTER TABLE focus_tasks ADD COLUMN excluded INTEGER DEFAULT 0");
    exec_ignoring_errors(db, "ALTER TABLE focus_tasks ADD COLUMN done INTEGER DEFAULT 0");

    sqlite3_close(db);
}

json parse_json_or_throw(const std::string &body) {
    try {
        return json::parse(body);
    } catch (const std::exception &e) {
        throw std::runtime_error(e.what());
    }
}

int get_int(const json &j, const std::string &key, int fallback) {
    if (!j.contains(key)) return fallback;
    if (j.at(key).is_number_integer()) return j.at(key).get<int>();
    if (j.at(key).is_number()) return static_cast<int>(j.at(key).get<double>());
    return fallback;
}

double get_double(const json &j, const std::string &key, double fallback) {
    if (!j.contains(key)) return fallback;
    if (j.at(key).is_number()) return j.at(key).get<double>();
    return fallback;
}

std::string get_string(const json &j, const std::string &key, const std::string &fallback) {
    if (!j.contains(key)) return fallback;
    if (j.at(key).is_string()) return j.at(key).get<std::string>();
    return fallback;
}

json parse_json_array_or_empty(const std::string &s) {
    try {
        auto v = json::parse(s);
        if (v.is_array()) return v;
    } catch (...) {
    }
    return json::array();
}

int task_count(sqlite3 *db) {
    sqlite3_stmt *stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM focus_tasks", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    return count;
}

std::vector<std::string> json_array_to_strings(const json &arr) {
    std::vector<std::string> out;
    if (!arr.is_array()) return out;
    for (const auto &v : arr) {
        if (v.is_string()) out.push_back(v.get<std::string>());
    }
    return out;
}

json merge_unique(const json &a, const json &b) {
    std::vector<std::string> merged;
    auto push_unique = [&](const std::string &s) {
        for (const auto &e : merged) {
            if (strcasecmp(e.c_str(), s.c_str()) == 0) return;
        }
        merged.push_back(s);
    };
    for (const auto &s : json_array_to_strings(a)) push_unique(s);
    for (const auto &s : json_array_to_strings(b)) push_unique(s);
    return json(merged);
}

void upsert_category(sqlite3 *db,
                     const std::string &category,
                     const json &allowed_app_ids,
                     const json &allowed_titles) {
    if (category.empty()) return;

    sqlite3_stmt *select_stmt = nullptr;
    const char *select_sql = "SELECT allowed_app_ids, allowed_titles FROM focus_categories WHERE category = ?";
    if (sqlite3_prepare_v2(db, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(select_stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);

    json merged_apps = allowed_app_ids.is_array() ? allowed_app_ids : json::array();
    json merged_titles = allowed_titles.is_array() ? allowed_titles : json::array();

    if (sqlite3_step(select_stmt) == SQLITE_ROW) {
        const char *apps = reinterpret_cast<const char *>(sqlite3_column_text(select_stmt, 0));
        const char *titles = reinterpret_cast<const char *>(sqlite3_column_text(select_stmt, 1));
        merged_apps = merge_unique(parse_json_array_or_empty(apps ? apps : "[]"), merged_apps);
        merged_titles = merge_unique(parse_json_array_or_empty(titles ? titles : "[]"), merged_titles);
    }
    sqlite3_finalize(select_stmt);

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO focus_categories (category, allowed_app_ids, allowed_titles, updated_at) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(category) DO UPDATE SET allowed_app_ids=excluded.allowed_app_ids, allowed_titles=excluded.allowed_titles, updated_at=excluded.updated_at";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    std::string apps_str = merged_apps.dump();
    std::string titles_str = merged_titles.dump();
    double now = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_bind_text(stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, apps_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, titles_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, now);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void upsert_activity_category(sqlite3 *db,
                              const std::string &app_id,
                              const std::string &title,
                              const std::string &category) {
    if (app_id.empty() && title.empty()) return;
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO focus_activity_categories (app_id, title, category, updated_at) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(app_id, title) DO UPDATE SET category=excluded.category, updated_at=excluded.updated_at";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    double now = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    sqlite3_bind_text(stmt, 1, app_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, now);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void insert_event(const std::string &db_path,
                  const std::string &app_id,
                  int window_id,
                  const std::string &title,
                  double start_time,
                  double end_time,
                  double duration) {
    sqlite3 *db = nullptr;
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        std::cerr << "unable to open database\n";
        return;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO focus_events "
        "(app_id, window_id, title, start_time, end_time, duration) "
        "VALUES (?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        std::cerr << "db prepare failed\n";
        return;
    }

    sqlite3_bind_text(stmt, 1, app_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, window_id);
    sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, start_time);
    sqlite3_bind_double(stmt, 5, end_time);
    sqlite3_bind_double(stmt, 6, duration);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "db insert failed\n";
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

FocusedWindow get_focused_window() {
    FILE *pipe = popen("niri msg windows", "r");
    if (!pipe) {
        return {};
    }

    char buf[4096];
    FocusedWindow fw;
    bool focused = false;

    while (fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);

        if (line.rfind("Window ID ", 0) == 0) {
            if (focused && fw.window_id != -1) {
                fw.valid = true;
                break;
            }
            focused = (line.find("(focused)") != std::string::npos);
            if (focused) {
                size_t id_start = 10;
                size_t id_end = line.find(':', id_start);
                if (id_end != std::string::npos) {
                    fw.window_id = std::stoi(line.substr(id_start, id_end - id_start));
                }
            }
            continue;
        }

        if (!focused) {
            continue;
        }

        if (line.find("Title:") != std::string::npos) {
            size_t pos = line.find("Title:") + 6;
            fw.title = line.substr(pos);
            fw.title.erase(0, fw.title.find_first_not_of(" \t\""));
            fw.title.erase(fw.title.find_last_not_of("\"\r\n") + 1);
        } else if (line.find("App ID:") != std::string::npos) {
            size_t pos = line.find("App ID:") + 7;
            fw.app_id = line.substr(pos);
            fw.app_id.erase(0, fw.app_id.find_first_not_of(" \t\""));
            fw.app_id.erase(fw.app_id.find_last_not_of("\"\r\n") + 1);
            if (!fw.title.empty()) {
                fw.valid = true;
                break;
            }
        }
    }

    if (focused && fw.window_id != -1 && !fw.title.empty() && !fw.app_id.empty()) {
        fw.valid = true;
    }

    pclose(pipe);
    return fw;
}

void serve_index(const std::string &base_dir, httplib::Response &res) {
    const char *env_path = std::getenv("NIRI_INDEX_PATH");
    std::vector<std::filesystem::path> candidates;
    if (env_path && *env_path) {
        candidates.emplace_back(env_path);
    }
    candidates.emplace_back(std::filesystem::path(base_dir) / "index.html");
    candidates.emplace_back(std::filesystem::current_path() / "index.html");
#ifdef NIRI_SOURCE_DIR
    candidates.emplace_back(std::filesystem::path(NIRI_SOURCE_DIR) / "index.html");
#endif

    std::filesystem::path index_path;
    for (const auto &candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            index_path = candidate;
            break;
        }
    }

    if (index_path.empty()) {
        res.status = 404;
        res.set_content("not found", "text/plain");
        return;
    }

    std::ifstream file(index_path, std::ios::binary);
    if (!file.is_open()) {
        res.status = 404;
        res.set_content("not found", "text/plain");
        return;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    res.status = 200;
    res.set_content(content, "text/html");
}

} // namespace

int main() {
    const std::string db_path = ensure_db_path();
    try {
        init_db(db_path);
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    std::mutex current_mutex;
    json current_focus = json::object();

    const std::string base_dir = get_exe_dir();

    std::thread([&]() {
        httplib::Server server;

        server.Post("/tasks", [&](const httplib::Request &req, httplib::Response &res) {
            try {
                json data = parse_json_or_throw(req.body);
                json allowed_app_ids = data.contains("allowed_app_ids") ? data["allowed_app_ids"] : json::array();
                json allowed_titles = data.contains("allowed_titles") ? data["allowed_titles"] : json::array();
                int excluded = data.value("exclude", false) ? 1 : 0;
                int done = data.value("done", false) ? 1 : 0;

                sqlite3 *db = nullptr;
                if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
                    throw std::runtime_error("unable to open database");
                }

                if (task_count(db) >= 1) {
                    sqlite3_close(db);
                    res.status = 409;
                    res.set_content("only one task allowed", "text/plain");
                    return;
                }

                exec_ignoring_errors(db, "DELETE FROM focus_events");

                sqlite3_stmt *stmt = nullptr;
                const char *sql =
                    "INSERT INTO focus_tasks "
                    "(category, task, start_time, end_time, allowed_app_ids, allowed_titles, excluded, done) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
                    sqlite3_close(db);
                    throw std::runtime_error("db prepare failed");
                }

                std::string category = get_string(data, "category", "");
                std::string task = get_string(data, "task", "");
                double start_time = get_double(data, "start_time", 0.0);
                double end_time = get_double(data, "end_time", 0.0);

                std::string allowed_app_ids_str = allowed_app_ids.dump();
                std::string allowed_titles_str = allowed_titles.dump();

                sqlite3_bind_text(stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, task.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(stmt, 3, start_time);
                sqlite3_bind_double(stmt, 4, end_time);
                sqlite3_bind_text(stmt, 5, allowed_app_ids_str.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 6, allowed_titles_str.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 7, excluded);
                sqlite3_bind_int(stmt, 8, done);

                if (sqlite3_step(stmt) != SQLITE_DONE) {
                    sqlite3_finalize(stmt);
                    sqlite3_close(db);
                    throw std::runtime_error("db insert failed");
                }

                upsert_category(db, category, allowed_app_ids, allowed_titles);

                sqlite3_finalize(stmt);
                sqlite3_close(db);

                res.status = 200;
                res.set_content("ok", "text/plain");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server.Post("/tasks/update", [&](const httplib::Request &req, httplib::Response &res) {
            try {
                json data = parse_json_or_throw(req.body);
                int task_id = get_int(data, "id", 0);
                if (task_id <= 0) {
                    res.status = 400;
                    res.set_content("missing id", "text/plain");
                    return;
                }

                sqlite3 *db = nullptr;
                if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
                    throw std::runtime_error("unable to open database");
                }

                sqlite3_stmt *select_stmt = nullptr;
                const char *select_sql = "SELECT * FROM focus_tasks WHERE id = ?";
                if (sqlite3_prepare_v2(db, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK) {
                    sqlite3_close(db);
                    throw std::runtime_error("db prepare failed");
                }
                sqlite3_bind_int(select_stmt, 1, task_id);

                if (sqlite3_step(select_stmt) != SQLITE_ROW) {
                    sqlite3_finalize(select_stmt);
                    sqlite3_close(db);
                    res.status = 404;
                    res.set_content("task not found", "text/plain");
                    return;
                }

                auto column_text = [&](int idx) -> std::string {
                    const unsigned char *txt = sqlite3_column_text(select_stmt, idx);
                    return txt ? reinterpret_cast<const char *>(txt) : "";
                };

                auto column_double = [&](int idx) -> double {
                    return sqlite3_column_double(select_stmt, idx);
                };

                auto column_int = [&](int idx) -> int {
                    return sqlite3_column_int(select_stmt, idx);
                };

                std::string category = data.contains("category") ? get_string(data, "category", "") : column_text(1);
                std::string task = data.contains("task") ? get_string(data, "task", "") : column_text(2);
                double start_time = data.contains("start_time") ? get_double(data, "start_time", 0.0) : column_double(3);
                double end_time = data.contains("end_time") ? get_double(data, "end_time", 0.0) : column_double(4);

                json allowed_app_ids = data.contains("allowed_app_ids")
                    ? data["allowed_app_ids"]
                    : parse_json_array_or_empty(column_text(5));

                json allowed_titles = data.contains("allowed_titles")
                    ? data["allowed_titles"]
                    : parse_json_array_or_empty(column_text(6));

                int excluded = data.contains("exclude") ? (data.value("exclude", false) ? 1 : 0) : column_int(7);
                int done = data.contains("done") ? (data.value("done", false) ? 1 : 0) : column_int(8);

                sqlite3_finalize(select_stmt);

                sqlite3_stmt *update_stmt = nullptr;
                const char *update_sql =
                    "UPDATE focus_tasks "
                    "SET category = ?, task = ?, start_time = ?, end_time = ?, allowed_app_ids = ?, allowed_titles = ?, excluded = ?, done = ? "
                    "WHERE id = ?";

                if (sqlite3_prepare_v2(db, update_sql, -1, &update_stmt, nullptr) != SQLITE_OK) {
                    sqlite3_close(db);
                    throw std::runtime_error("db prepare failed");
                }

                std::string allowed_app_ids_str = allowed_app_ids.dump();
                std::string allowed_titles_str = allowed_titles.dump();

                sqlite3_bind_text(update_stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(update_stmt, 2, task.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(update_stmt, 3, start_time);
                sqlite3_bind_double(update_stmt, 4, end_time);
                sqlite3_bind_text(update_stmt, 5, allowed_app_ids_str.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(update_stmt, 6, allowed_titles_str.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(update_stmt, 7, excluded);
                sqlite3_bind_int(update_stmt, 8, done);
                sqlite3_bind_int(update_stmt, 9, task_id);

                if (sqlite3_step(update_stmt) != SQLITE_DONE) {
                    sqlite3_finalize(update_stmt);
                    sqlite3_close(db);
                    throw std::runtime_error("db update failed");
                }

                upsert_category(db, category, allowed_app_ids, allowed_titles);

                sqlite3_finalize(update_stmt);
                sqlite3_close(db);

                res.status = 200;
                res.set_content("ok", "text/plain");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server.Get("/", [&](const httplib::Request &, httplib::Response &res) {
            serve_index(base_dir, res);
        });

        server.Get("/index.html", [&](const httplib::Request &, httplib::Response &res) {
            serve_index(base_dir, res);
        });

        server.Get("/events", [&](const httplib::Request &, httplib::Response &res) {
            try {
                sqlite3 *db = nullptr;
                if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
                    throw std::runtime_error("unable to open database");
                }

                sqlite3_stmt *stmt = nullptr;
                const char *sql = "SELECT * FROM focus_events ORDER BY start_time";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
                    sqlite3_close(db);
                    throw std::runtime_error("db prepare failed");
                }

                json rows = json::array();
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    json row;
                    const char *app_id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                    const char *title = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
                    row["id"] = sqlite3_column_int(stmt, 0);
                    row["app_id"] = app_id ? app_id : "";
                    row["window_id"] = sqlite3_column_int(stmt, 2);
                    row["title"] = title ? title : "";
                    row["start_time"] = sqlite3_column_double(stmt, 4);
                    row["end_time"] = sqlite3_column_double(stmt, 5);
                    row["duration"] = sqlite3_column_double(stmt, 6);
                    rows.push_back(row);
                }

                sqlite3_finalize(stmt);
                sqlite3_close(db);

                res.status = 200;
                res.set_content(rows.dump(), "application/json");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server.Get("/tasks", [&](const httplib::Request &, httplib::Response &res) {
            try {
                sqlite3 *db = nullptr;
                if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
                    throw std::runtime_error("unable to open database");
                }

                sqlite3_stmt *stmt = nullptr;
                const char *sql = "SELECT * FROM focus_tasks ORDER BY start_time";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
                    sqlite3_close(db);
                    throw std::runtime_error("db prepare failed");
                }

                json rows = json::array();
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    json row;
                    const char *category = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                    const char *task = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                    row["id"] = sqlite3_column_int(stmt, 0);
                    row["category"] = category ? category : "";
                    row["task"] = task ? task : "";
                    row["start_time"] = sqlite3_column_double(stmt, 3);
                    row["end_time"] = sqlite3_column_double(stmt, 4);

                    const char *allowed_app_ids = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
                    const char *allowed_titles = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));

                    row["allowed_app_ids"] = parse_json_array_or_empty(allowed_app_ids ? allowed_app_ids : "[]");
                    row["allowed_titles"] = parse_json_array_or_empty(allowed_titles ? allowed_titles : "[]");
                    row["excluded"] = sqlite3_column_int(stmt, 7) != 0;
                    row["done"] = sqlite3_column_int(stmt, 8) != 0;

                    rows.push_back(row);
                }

                sqlite3_finalize(stmt);
                sqlite3_close(db);

                res.status = 200;
                res.set_content(rows.dump(), "application/json");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server.Get("/categories", [&](const httplib::Request &, httplib::Response &res) {
            try {
                sqlite3 *db = nullptr;
                if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
                    throw std::runtime_error("unable to open database");
                }

                sqlite3_stmt *stmt = nullptr;
                const char *sql = "SELECT category, allowed_app_ids, allowed_titles, updated_at FROM focus_categories ORDER BY updated_at DESC";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
                    sqlite3_close(db);
                    throw std::runtime_error("db prepare failed");
                }

                json rows = json::array();
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    json row;
                    const char *category = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
                    const char *apps = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                    const char *titles = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                    row["category"] = category ? category : "";
                    row["allowed_app_ids"] = parse_json_array_or_empty(apps ? apps : "[]");
                    row["allowed_titles"] = parse_json_array_or_empty(titles ? titles : "[]");
                    row["updated_at"] = sqlite3_column_double(stmt, 3);
                    rows.push_back(row);
                }

                sqlite3_finalize(stmt);
                sqlite3_close(db);

                res.status = 200;
                res.set_content(rows.dump(), "application/json");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server.Get("/history", [&](const httplib::Request &, httplib::Response &res) {
            try {
                sqlite3 *db = nullptr;
                if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
                    throw std::runtime_error("unable to open database");
                }

                sqlite3_stmt *stmt = nullptr;
                const char *sql =
                    "SELECT e.app_id, e.title, SUM(e.duration) AS total_duration, "
                    "MAX(e.end_time) AS last_end, c.category "
                    "FROM focus_events e "
                    "LEFT JOIN focus_activity_categories c "
                    "ON e.app_id = c.app_id AND e.title = c.title "
                    "GROUP BY e.app_id, e.title "
                    "ORDER BY total_duration DESC";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
                    sqlite3_close(db);
                    throw std::runtime_error("db prepare failed");
                }

                json rows = json::array();
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    json row;
                    const char *app_id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
                    const char *title = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                    const char *category = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
                    row["app_id"] = app_id ? app_id : "";
                    row["title"] = title ? title : "";
                    row["total_duration"] = sqlite3_column_double(stmt, 2);
                    row["last_end"] = sqlite3_column_double(stmt, 3);
                    row["category"] = category ? category : "";
                    rows.push_back(row);
                }

                sqlite3_finalize(stmt);
                sqlite3_close(db);

                res.status = 200;
                res.set_content(rows.dump(), "application/json");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server.Post("/history/category", [&](const httplib::Request &req, httplib::Response &res) {
            try {
                json data = parse_json_or_throw(req.body);
                std::string app_id = get_string(data, "app_id", "");
                std::string title = get_string(data, "title", "");
                std::string category = get_string(data, "category", "");
                if (app_id.empty() && title.empty()) {
                    res.status = 400;
                    res.set_content("missing app_id/title", "text/plain");
                    return;
                }

                sqlite3 *db = nullptr;
                if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
                    throw std::runtime_error("unable to open database");
                }

                upsert_activity_category(db, app_id, title, category);
                if (!category.empty()) {
                    json apps = json::array();
                    json titles = json::array();
                    if (!app_id.empty()) apps.push_back(app_id);
                    if (!title.empty()) titles.push_back(title);
                    upsert_category(db, category, apps, titles);
                }
                sqlite3_close(db);

                res.status = 200;
                res.set_content("ok", "text/plain");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        server.Get("/current", [&](const httplib::Request &, httplib::Response &res) {
            json snapshot;
            {
                std::lock_guard<std::mutex> lock(current_mutex);
                snapshot = current_focus;
            }
            res.status = 200;
            res.set_content(snapshot.dump(), "application/json");
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

    int last_window_id = -1;
    std::string last_title;
    std::string last_app_id;

    steady_clock::time_point focus_start_steady;
    std::chrono::system_clock::time_point focus_start_system;

    while (true) {
        FocusedWindow fw = get_focused_window();
        if (!fw.valid) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        auto now_steady = steady_clock::now();
        auto now_system = std::chrono::system_clock::now();
        if (last_window_id == -1) {
            last_window_id = fw.window_id;
            last_title = fw.title;
            last_app_id = fw.app_id;
            focus_start_steady = now_steady;
            focus_start_system = now_system;
            double start_time = std::chrono::duration<double>(now_system.time_since_epoch()).count();
            json updated = json::object();
            updated["app_id"] = fw.app_id;
            updated["window_id"] = fw.window_id;
            updated["title"] = fw.title;
            updated["start_time"] = start_time;
            updated["updated_at"] = std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            {
                std::lock_guard<std::mutex> lock(current_mutex);
                current_focus = updated;
            }
        } else if (fw.window_id != last_window_id) {
            double elapsed = std::chrono::duration<double>(now_steady - focus_start_steady).count();
            double start_time = std::chrono::duration<double>(focus_start_system.time_since_epoch()).count();
            double end_time = std::chrono::duration<double>(now_system.time_since_epoch()).count();
            insert_event(db_path, last_app_id, last_window_id, last_title, start_time, end_time, elapsed);

            // troca de foco
            last_window_id = fw.window_id;
            last_title = fw.title;
            last_app_id = fw.app_id;
            focus_start_steady = now_steady;
            focus_start_system = now_system;

            json updated = json::object();
            updated["app_id"] = fw.app_id;
            updated["window_id"] = fw.window_id;
            updated["title"] = fw.title;
            updated["start_time"] = end_time;
            updated["updated_at"] = std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            {
                std::lock_guard<std::mutex> lock(current_mutex);
                current_focus = updated;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
