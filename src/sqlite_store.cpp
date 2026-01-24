#include "sqlite_store.h"

#include "common.h"

#include <sqlite3.h>

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {

// ─────────────────────────────────────
struct SqliteDb {
    sqlite3 *db = nullptr;
    ~SqliteDb() {
        if (db) {
            sqlite3_close(db);
        }
    }
    SqliteDb(const SqliteDb &) = delete;
    SqliteDb &operator=(const SqliteDb &) = delete;
    SqliteDb() = default;
};

struct ThreadDb {
    sqlite3 *db = nullptr;
    std::string path;
    ~ThreadDb() {
        if (db) {
            sqlite3_close(db);
        }
    }
};

// ─────────────────────────────────────
struct SqliteStmt {
    sqlite3_stmt *stmt = nullptr;
    ~SqliteStmt() {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
    }
    sqlite3_stmt **out() { return &stmt; }
    sqlite3_stmt *get() const { return stmt; }
    SqliteStmt(const SqliteStmt &) = delete;
    SqliteStmt &operator=(const SqliteStmt &) = delete;
    SqliteStmt() = default;
};

// ─────────────────────────────────────
void exec_ignoring_errors(sqlite3 *db, const std::string &sql) {
    char *errmsg = nullptr;
    sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errmsg);
    if (errmsg) {
        sqlite3_free(errmsg);
    }
}

sqlite3 *get_thread_db(const std::string &db_path) {
    thread_local ThreadDb holder;
    if (!holder.db || holder.path != db_path) {
        if (holder.db) {
            sqlite3_close(holder.db);
            holder.db = nullptr;
        }
        holder.path = db_path;
        if (sqlite3_open(db_path.c_str(), &holder.db) != SQLITE_OK) {
            holder.db = nullptr;
            return nullptr;
        }
        sqlite3_busy_timeout(holder.db, 2000);
        exec_ignoring_errors(holder.db, "PRAGMA journal_mode=WAL");
        exec_ignoring_errors(holder.db, "PRAGMA synchronous=NORMAL");
        exec_ignoring_errors(holder.db, "PRAGMA temp_store=MEMORY");
    }
    return holder.db;
}

// ─────────────────────────────────────
void upsert_category(sqlite3 *db,
                     const std::string &category,
                     const json &allowed_app_ids,
                     const json &allowed_titles) {
    if (category.empty()) return;

    SqliteStmt select_stmt;
    const char *select_sql = "SELECT allowed_app_ids, allowed_titles FROM focus_categories WHERE category = ?";
    if (sqlite3_prepare_v2(db, select_sql, -1, select_stmt.out(), nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(select_stmt.get(), 1, category.c_str(), -1, SQLITE_TRANSIENT);

    json merged_apps = allowed_app_ids.is_array() ? allowed_app_ids : json::array();
    json merged_titles = allowed_titles.is_array() ? allowed_titles : json::array();

    if (sqlite3_step(select_stmt.get()) == SQLITE_ROW) {
        const char *apps = reinterpret_cast<const char *>(sqlite3_column_text(select_stmt.get(), 0));
        const char *titles = reinterpret_cast<const char *>(sqlite3_column_text(select_stmt.get(), 1));
        merged_apps = merge_unique(parse_json_array_or_empty(apps ? apps : "[]"), merged_apps);
        merged_titles = merge_unique(parse_json_array_or_empty(titles ? titles : "[]"), merged_titles);
    }

    SqliteStmt stmt;
    const char *sql =
        "INSERT INTO focus_categories (category, allowed_app_ids, allowed_titles, updated_at) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(category) DO UPDATE SET allowed_app_ids=excluded.allowed_app_ids, allowed_titles=excluded.allowed_titles, updated_at=excluded.updated_at";
    if (sqlite3_prepare_v2(db, sql, -1, stmt.out(), nullptr) != SQLITE_OK) {
        return;
    }
    std::string apps_str = merged_apps.dump();
    std::string titles_str = merged_titles.dump();
    double now = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_bind_text(stmt.get(), 1, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, apps_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, titles_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt.get(), 4, now);
    sqlite3_step(stmt.get());
}
} // namespace

// ─────────────────────────────────────
void init_db(const std::string &db_path) {
    SqliteDb db;
    if (sqlite3_open(db_path.c_str(), &db.db) != SQLITE_OK) {
        throw std::runtime_error("unable to open database");
    }

    exec_ignoring_errors(db.db,
        "CREATE TABLE IF NOT EXISTS focus_events ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "app_id TEXT,"
        "window_id INTEGER,"
        "title TEXT,"
        "start_time REAL,"
        "end_time REAL,"
        "duration REAL"
        ")");

    exec_ignoring_errors(db.db,
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

    exec_ignoring_errors(db.db,
        "CREATE TABLE IF NOT EXISTS focus_categories ("
        "category TEXT PRIMARY KEY,"
        "allowed_app_ids TEXT,"
        "allowed_titles TEXT,"
        "updated_at REAL"
        ")");

    exec_ignoring_errors(db.db,
        "CREATE TABLE IF NOT EXISTS focus_activity_categories ("
        "app_id TEXT,"
        "title TEXT,"
        "category TEXT,"
        "updated_at REAL,"
        "PRIMARY KEY(app_id, title)"
        ")");

    exec_ignoring_errors(db.db, "ALTER TABLE focus_tasks ADD COLUMN excluded INTEGER DEFAULT 0");
    exec_ignoring_errors(db.db, "ALTER TABLE focus_tasks ADD COLUMN done INTEGER DEFAULT 0");
}

// ─────────────────────────────────────
void insert_event(const std::string &db_path,
                  const std::string &app_id,
                  int window_id,
                  const std::string &title,
                  double start_time,
                  double end_time,
                  double duration) {
    sqlite3 *db = get_thread_db(db_path);
    if (!db) {
        return;
    }

    SqliteStmt stmt;
    const char *sql =
        "INSERT INTO focus_events "
        "(app_id, window_id, title, start_time, end_time, duration) "
        "VALUES (?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, stmt.out(), nullptr) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_text(stmt.get(), 1, app_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 2, window_id);
    sqlite3_bind_text(stmt.get(), 3, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt.get(), 4, start_time);
    sqlite3_bind_double(stmt.get(), 5, end_time);
    sqlite3_bind_double(stmt.get(), 6, duration);

    sqlite3_step(stmt.get());
}

// ─────────────────────────────────────
bool create_task(const std::string &db_path, const json &data, std::string &error) {
    json allowed_app_ids = data.contains("allowed_app_ids") ? data["allowed_app_ids"] : json::array();
    json allowed_titles = data.contains("allowed_titles") ? data["allowed_titles"] : json::array();
    int excluded = data.value("exclude", false) ? 1 : 0;
    int done = data.value("done", false) ? 1 : 0;

    sqlite3 *db = get_thread_db(db_path);
    if (!db) {
        error = "unable to open database";
        return false;
    }

    SqliteStmt stmt;
    const char *sql =
        "INSERT INTO focus_tasks "
        "(category, task, start_time, end_time, allowed_app_ids, allowed_titles, excluded, done) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, stmt.out(), nullptr) != SQLITE_OK) {
        error = "db prepare failed";
        return false;
    }

    std::string category = get_string(data, "category", "");
    std::string task = get_string(data, "task", "");
    double start_time = get_double(data, "start_time", 0.0);
    double end_time = get_double(data, "end_time", 0.0);

    std::string allowed_app_ids_str = allowed_app_ids.dump();
    std::string allowed_titles_str = allowed_titles.dump();

    sqlite3_bind_text(stmt.get(), 1, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, task.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt.get(), 3, start_time);
    sqlite3_bind_double(stmt.get(), 4, end_time);
    sqlite3_bind_text(stmt.get(), 5, allowed_app_ids_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 6, allowed_titles_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 7, excluded);
    sqlite3_bind_int(stmt.get(), 8, done);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        error = "db insert failed";
        return false;
    }

    upsert_category(db, category, allowed_app_ids, allowed_titles);
    return true;
}

// ─────────────────────────────────────
bool update_task(const std::string &db_path, const json &data, std::string &error) {
    int task_id = get_int(data, "id", 0);
    if (task_id <= 0) {
        error = "missing id";
        return false;
    }

    sqlite3 *db = get_thread_db(db_path);
    if (!db) {
        error = "unable to open database";
        return false;
    }

    SqliteStmt select_stmt;
    const char *select_sql = "SELECT * FROM focus_tasks WHERE id = ?";
    if (sqlite3_prepare_v2(db, select_sql, -1, select_stmt.out(), nullptr) != SQLITE_OK) {
        error = "db prepare failed";
        return false;
    }
    sqlite3_bind_int(select_stmt.get(), 1, task_id);

    if (sqlite3_step(select_stmt.get()) != SQLITE_ROW) {
        error = "task not found";
        return false;
    }

    auto column_text = [&](int idx) -> std::string {
        const unsigned char *txt = sqlite3_column_text(select_stmt.get(), idx);
        return txt ? reinterpret_cast<const char *>(txt) : "";
    };

    auto column_double = [&](int idx) -> double {
        return sqlite3_column_double(select_stmt.get(), idx);
    };

    auto column_int = [&](int idx) -> int {
        return sqlite3_column_int(select_stmt.get(), idx);
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

    SqliteStmt update_stmt;
    const char *update_sql =
        "UPDATE focus_tasks "
        "SET category = ?, task = ?, start_time = ?, end_time = ?, allowed_app_ids = ?, allowed_titles = ?, excluded = ?, done = ? "
        "WHERE id = ?";

    if (sqlite3_prepare_v2(db, update_sql, -1, update_stmt.out(), nullptr) != SQLITE_OK) {
        error = "db prepare failed";
        return false;
    }

    std::string allowed_app_ids_str = allowed_app_ids.dump();
    std::string allowed_titles_str = allowed_titles.dump();

    sqlite3_bind_text(update_stmt.get(), 1, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(update_stmt.get(), 2, task.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(update_stmt.get(), 3, start_time);
    sqlite3_bind_double(update_stmt.get(), 4, end_time);
    sqlite3_bind_text(update_stmt.get(), 5, allowed_app_ids_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(update_stmt.get(), 6, allowed_titles_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(update_stmt.get(), 7, excluded);
    sqlite3_bind_int(update_stmt.get(), 8, done);
    sqlite3_bind_int(update_stmt.get(), 9, task_id);

    if (sqlite3_step(update_stmt.get()) != SQLITE_DONE) {
        error = "db update failed";
        return false;
    }

    upsert_category(db, category, allowed_app_ids, allowed_titles);
    return true;
}

// ─────────────────────────────────────
bool upsert_activity_category(const std::string &db_path,
                              const std::string &app_id,
                              const std::string &title,
                              const std::string &category,
                              std::string &error) {
    if (app_id.empty() && title.empty()) {
        error = "missing app_id/title";
        return false;
    }

        sqlite3 *db = get_thread_db(db_path);
        if (!db) {
        error = "unable to open database";
        return false;
    }

    SqliteStmt stmt;
    const char *sql =
        "INSERT INTO focus_activity_categories (app_id, title, category, updated_at) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(app_id, title) DO UPDATE SET category=excluded.category, updated_at=excluded.updated_at";
        if (sqlite3_prepare_v2(db, sql, -1, stmt.out(), nullptr) != SQLITE_OK) {
        error = "db prepare failed";
        return false;
    }
    double now = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    sqlite3_bind_text(stmt.get(), 1, app_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt.get(), 4, now);
    sqlite3_step(stmt.get());

    if (!category.empty()) {
        json apps = json::array();
        json titles = json::array();
        if (!app_id.empty()) apps.push_back(app_id);
        if (!title.empty()) titles.push_back(title);
            upsert_category(db, category, apps, titles);
    }

    return true;
}

// ─────────────────────────────────────
json fetch_events(const std::string &db_path) {
    sqlite3 *db = get_thread_db(db_path);
    if (!db) {
        throw std::runtime_error("unable to open database");
    }

    SqliteStmt stmt;
    const char *sql = "SELECT * FROM focus_events ORDER BY start_time";
    if (sqlite3_prepare_v2(db, sql, -1, stmt.out(), nullptr) != SQLITE_OK) {
        throw std::runtime_error("db prepare failed");
    }

    json rows = json::array();
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        json row;
        const char *app_id = reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 1));
        const char *title = reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 3));
        row["id"] = sqlite3_column_int(stmt.get(), 0);
        row["app_id"] = app_id ? app_id : "";
        row["window_id"] = sqlite3_column_int(stmt.get(), 2);
        row["title"] = title ? title : "";
        row["start_time"] = sqlite3_column_double(stmt.get(), 4);
        row["end_time"] = sqlite3_column_double(stmt.get(), 5);
        row["duration"] = sqlite3_column_double(stmt.get(), 6);
        rows.push_back(row);
    }

    return rows;
}

// ─────────────────────────────────────
json fetch_tasks(const std::string &db_path) {
    sqlite3 *db = get_thread_db(db_path);
    if (!db) {
        throw std::runtime_error("unable to open database");
    }

    SqliteStmt stmt;
    const char *sql = "SELECT * FROM focus_tasks ORDER BY start_time";
    if (sqlite3_prepare_v2(db, sql, -1, stmt.out(), nullptr) != SQLITE_OK) {
        throw std::runtime_error("db prepare failed");
    }

    json rows = json::array();
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        json row;
        const char *category = reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 1));
        const char *task = reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 2));
        row["id"] = sqlite3_column_int(stmt.get(), 0);
        row["category"] = category ? category : "";
        row["task"] = task ? task : "";
        row["start_time"] = sqlite3_column_double(stmt.get(), 3);
        row["end_time"] = sqlite3_column_double(stmt.get(), 4);

        const char *allowed_app_ids = reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 5));
        const char *allowed_titles = reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 6));

        row["allowed_app_ids"] = parse_json_array_or_empty(allowed_app_ids ? allowed_app_ids : "[]");
        row["allowed_titles"] = parse_json_array_or_empty(allowed_titles ? allowed_titles : "[]");
        row["excluded"] = sqlite3_column_int(stmt.get(), 7) != 0;
        row["done"] = sqlite3_column_int(stmt.get(), 8) != 0;

        rows.push_back(row);
    }

    return rows;
}

// ─────────────────────────────────────
json fetch_categories(const std::string &db_path) {
    sqlite3 *db = get_thread_db(db_path);
    if (!db) {
        throw std::runtime_error("unable to open database");
    }

    SqliteStmt stmt;
    const char *sql = "SELECT category, allowed_app_ids, allowed_titles, updated_at FROM focus_categories ORDER BY updated_at DESC";
    if (sqlite3_prepare_v2(db, sql, -1, stmt.out(), nullptr) != SQLITE_OK) {
        throw std::runtime_error("db prepare failed");
    }

    json rows = json::array();
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        json row;
        const char *category = reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 0));
        const char *apps = reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 1));
        const char *titles = reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 2));
        row["category"] = category ? category : "";
        row["allowed_app_ids"] = parse_json_array_or_empty(apps ? apps : "[]");
        row["allowed_titles"] = parse_json_array_or_empty(titles ? titles : "[]");
        row["updated_at"] = sqlite3_column_double(stmt.get(), 3);
        rows.push_back(row);
    }

    return rows;
}

// ─────────────────────────────────────
json fetch_history(const std::string &db_path) {
    sqlite3 *db = get_thread_db(db_path);
    if (!db) {
        throw std::runtime_error("unable to open database");
    }

    SqliteStmt stmt;
    const char *sql =
        "SELECT e.app_id, e.title, SUM(e.duration) AS total_duration, "
        "MAX(e.end_time) AS last_end, c.category "
        "FROM focus_events e "
        "LEFT JOIN focus_activity_categories c "
        "ON e.app_id = c.app_id AND e.title = c.title "
        "GROUP BY e.app_id, e.title "
        "ORDER BY total_duration DESC";
    if (sqlite3_prepare_v2(db, sql, -1, stmt.out(), nullptr) != SQLITE_OK) {
        throw std::runtime_error("db prepare failed");
    }

    json rows = json::array();
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        json row;
        const char *app_id = reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 0));
        const char *title = reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 1));
        const char *category = reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 4));
        row["app_id"] = app_id ? app_id : "";
        row["title"] = title ? title : "";
        row["total_duration"] = sqlite3_column_double(stmt.get(), 2);
        row["last_end"] = sqlite3_column_double(stmt.get(), 3);
        row["category"] = category ? category : "";
        rows.push_back(row);
    }

    return rows;
}
