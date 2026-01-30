#include "sqlite.hpp"
#include <spdlog/spdlog.h>

// ─────────────────────────────────────
SQLite::SQLite(const std::string &db_path) : m_Db(nullptr), m_DbPath(db_path) {
    if (sqlite3_open(m_DbPath.c_str(), &m_Db) != SQLITE_OK) {
        spdlog::error("unable to open database: {}", m_DbPath);
        throw std::runtime_error("unable to open database");
    }

    spdlog::info("SQLite database opened: {}", m_DbPath);
    sqlite3_busy_timeout(m_Db, 2000);
    ExecIgnoringErrors("PRAGMA journal_mode=WAL");
    ExecIgnoringErrors("PRAGMA synchronous=NORMAL");
    ExecIgnoringErrors("PRAGMA temp_store=MEMORY");
    ExecIgnoringErrors("PRAGMA cache_size = -1000;");

    Init();
}

// ─────────────────────────────────────
SQLite::~SQLite() {
    if (m_Db) {
        sqlite3_close(m_Db);
        m_Db = nullptr;
    }
}

// ─────────────────────────────────────
void SQLite::Init() {
    spdlog::debug("Initializing SQLite database tables");
    // Focused window events
    ExecIgnoringErrors("CREATE TABLE IF NOT EXISTS focus_events ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "app_id TEXT,"
                       "window_id INTEGER,"
                       "title TEXT,"
                       "start_time REAL,"
                       "end_time REAL,"
                       "duration REAL)");

    // Tasks
    ExecIgnoringErrors("CREATE TABLE IF NOT EXISTS focus_tasks ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "category TEXT,"
                       "task TEXT,"
                       "start_time REAL,"
                       "end_time REAL,"
                       "allowed_app_ids TEXT,"
                       "allowed_titles TEXT,"
                       "excluded INTEGER DEFAULT 0,"
                       "done INTEGER DEFAULT 0)");

    // Categories
    ExecIgnoringErrors("CREATE TABLE IF NOT EXISTS focus_categories ("
                       "category TEXT PRIMARY KEY,"
                       "allowed_app_ids TEXT,"
                       "allowed_titles TEXT,"
                       "updated_at REAL)");

    // App/title → category mapping
    ExecIgnoringErrors("CREATE TABLE IF NOT EXISTS focus_activity_categories ("
                       "app_id TEXT,"
                       "title TEXT,"
                       "category TEXT,"
                       "updated_at REAL,"
                       "PRIMARY KEY(app_id, title))");

    // Focus / Unfocus state timeline
    ExecIgnoringErrors("CREATE TABLE IF NOT EXISTS focus_states ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "state INTEGER NOT NULL,"
                       "start_time REAL NOT NULL,"
                       "end_time REAL,"
                       "duration REAL)");
    spdlog::info("SQLite database tables initialized");
}

// ─────────────────────────────────────
void SQLite::InsertEvent(const std::string &app_id, int window_id, const std::string &title,
                         double start_time, double end_time, double duration) {
    sqlite3_stmt *stmt = nullptr;

    const char *sql = "INSERT INTO focus_events "
                      "(app_id, window_id, title, start_time, end_time, duration) "
                      "VALUES (?, ?, ?, ?, ?, ?)";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_text(stmt, 1, app_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, window_id);
    sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, start_time);
    sqlite3_bind_double(stmt, 5, end_time);
    sqlite3_bind_double(stmt, 6, duration);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    spdlog::debug("Inserted focus event: app_id={}, title={}, duration={}", app_id, title,
                  duration);
}

// ─────────────────────────────────────
void SQLite::InsertFocusState(int state, double start, double end, double duration) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "INSERT INTO focus_states (state, start_time, end_time, duration) "
                      "VALUES (?, ?, ?, ?)";

    int rc = sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("db prepare failed in InsertFocusState: {}", sqlite3_errmsg(m_Db));
        return;
    }

    sqlite3_bind_int(stmt, 1, state);
    sqlite3_bind_double(stmt, 2, start);
    sqlite3_bind_double(stmt, 3, end);
    sqlite3_bind_double(stmt, 4, duration);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        spdlog::error("db step failed in InsertFocusState: rc={}, err={}", rc,
                      sqlite3_errmsg(m_Db));
    } else {
        spdlog::info("Inserted focus state: state={}, duration={}", state, duration);
    }

    sqlite3_finalize(stmt);
}

// ─────────────────────────────────────
nlohmann::json SQLite::FetchTodayFocusSummary() {
    sqlite3_stmt *stmt = nullptr;

    const char *sql = R"(
        SELECT
            state,
            SUM(duration) AS total_duration
        FROM focus_states
        WHERE date(start_time, 'unixepoch', 'localtime') =
              date('now', 'localtime')
        GROUP BY state
    )";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in FetchTodayFocusSummary");
        throw std::runtime_error("db prepare failed");
    }

    double focused = 0.0;
    double unfocused = 0.0;
    double idle = 0.0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int state = sqlite3_column_int(stmt, 0);
        double total = sqlite3_column_double(stmt, 1);

        if (state == 1) {
            focused = total;
        } else if (state == 2) {
            unfocused = total;
        } else if (state == 3) {
            idle = total;
        }
    }

    sqlite3_finalize(stmt);

    spdlog::debug("Fetched today focus summary: focused={}, unfocused={}, idle={}", focused,
                  unfocused, idle);
    return {{"focused_seconds", focused}, {"unfocused_seconds", unfocused}, {"idle_seconds", idle}};
}

// ─────────────────────────────────────
void SQLite::UpsertCategory(const std::string &category, const nlohmann::json &allowed_app_ids,
                            const nlohmann::json &allowed_titles) {
    if (category.empty()) {
        return;
    }

    sqlite3_stmt *select_stmt = nullptr;
    const char *select_sql = "SELECT allowed_app_ids, allowed_titles "
                             "FROM focus_categories WHERE category = ?";

    if (sqlite3_prepare_v2(m_Db, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_text(select_stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);

    nlohmann::json merged_apps =
        allowed_app_ids.is_array() ? allowed_app_ids : nlohmann::json::array();

    nlohmann::json merged_titles =
        allowed_titles.is_array() ? allowed_titles : nlohmann::json::array();

    if (sqlite3_step(select_stmt) == SQLITE_ROW) {
        const char *apps = reinterpret_cast<const char *>(sqlite3_column_text(select_stmt, 0));
        const char *titles = reinterpret_cast<const char *>(sqlite3_column_text(select_stmt, 1));
        merged_apps =
            m_JsonParse.MergeUnique(nlohmann::json::parse(apps ? apps : "[]"), merged_apps);
        merged_titles =
            m_JsonParse.MergeUnique(nlohmann::json::parse(titles ? titles : "[]"), merged_titles);
    }

    sqlite3_finalize(select_stmt);
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "INSERT INTO focus_categories "
                      "(category, allowed_app_ids, allowed_titles, updated_at) "
                      "VALUES (?, ?, ?, ?) "
                      "ON CONFLICT(category) DO UPDATE SET "
                      "allowed_app_ids=excluded.allowed_app_ids, "
                      "allowed_titles=excluded.allowed_titles, "
                      "updated_at=excluded.updated_at";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }

    std::string apps_str = merged_apps.dump();
    std::string titles_str = merged_titles.dump();
    double now =
        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_bind_text(stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, apps_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, titles_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, now);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    spdlog::debug("Upserted category: {}", category);
}

// ─────────────────────────────────────
bool SQLite::CreateTask(const nlohmann::json &data, std::string &error) {
    nlohmann::json allowed_app_ids =
        data.contains("allowed_app_ids") ? data["allowed_app_ids"] : nlohmann::json::array();

    nlohmann::json allowed_titles =
        data.contains("allowed_titles") ? data["allowed_titles"] : nlohmann::json::array();

    int excluded = data.value("exclude", false) ? 1 : 0;
    int done = data.value("done", false) ? 1 : 0;

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO focus_tasks "
        "(category, task, start_time, end_time, allowed_app_ids, allowed_titles, excluded, done) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in CreateTask");
        error = "db prepare failed";
        return false;
    }

    std::string category = m_JsonParse.GetString(data, "category", "");
    std::string task = m_JsonParse.GetString(data, "task", "");
    double start_time = m_JsonParse.GetDouble(data, "start_time", 0.0);
    double end_time = m_JsonParse.GetDouble(data, "end_time", 0.0);

    sqlite3_bind_text(stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, task.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, start_time);
    sqlite3_bind_double(stmt, 4, end_time);
    sqlite3_bind_text(stmt, 5, allowed_app_ids.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, allowed_titles.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, excluded);
    sqlite3_bind_int(stmt, 8, done);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        spdlog::error("db insert failed in CreateTask");
        sqlite3_finalize(stmt);
        error = "db insert failed";
        return false;
    }

    sqlite3_finalize(stmt);
    UpsertCategory(category, allowed_app_ids, allowed_titles);
    spdlog::info("Created task: {}", task);
    return true;
}

// ─────────────────────────────────────
bool SQLite::UpdateTask(const nlohmann::json &data, std::string &error) {
    int task_id = m_JsonParse.GetInt(data, "id", 0);
    if (task_id <= 0) {
        error = "missing id";
        return false;
    }

    sqlite3_stmt *select_stmt = nullptr;
    const char *select_sql = "SELECT * FROM focus_tasks WHERE id = ?";

    if (sqlite3_prepare_v2(m_Db, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in UpdateTask select");
        error = "db prepare failed";
        return false;
    }

    sqlite3_bind_int(select_stmt, 1, task_id);

    if (sqlite3_step(select_stmt) != SQLITE_ROW) {
        sqlite3_finalize(select_stmt);
        error = "task not found";
        return false;
    }

    auto column_text = [&](int idx) -> std::string {
        const unsigned char *txt = sqlite3_column_text(select_stmt, idx);
        return txt ? reinterpret_cast<const char *>(txt) : "";
    };

    auto column_double = [&](int idx) -> double { return sqlite3_column_double(select_stmt, idx); };

    auto column_int = [&](int idx) -> int { return sqlite3_column_int(select_stmt, idx); };

    std::string category =
        data.contains("category") ? m_JsonParse.GetString(data, "category", "Any") : column_text(1);
    std::string task =
        data.contains("task") ? m_JsonParse.GetString(data, "task", "Any") : column_text(2);
    double start_time = data.contains("start_time") ? m_JsonParse.GetDouble(data, "start_time", 0.0)
                                                    : column_double(3);
    double end_time =
        data.contains("end_time") ? m_JsonParse.GetDouble(data, "end_time", 0.0) : column_double(4);

    nlohmann::json allowed_app_ids = data.contains("allowed_app_ids")
                                         ? data["allowed_app_ids"]
                                         : nlohmann::json::parse(column_text(5));

    nlohmann::json allowed_titles = data.contains("allowed_titles")
                                        ? data["allowed_titles"]
                                        : nlohmann::json::parse(column_text(6));

    int excluded =
        data.contains("exclude") ? (data.value("exclude", false) ? 1 : 0) : column_int(7);
    int done = data.contains("done") ? (data.value("done", false) ? 1 : 0) : column_int(8);

    sqlite3_finalize(select_stmt);

    sqlite3_stmt *update_stmt = nullptr;
    const char *update_sql = "UPDATE focus_tasks "
                             "SET category = ?, task = ?, start_time = ?, end_time = ?, "
                             "allowed_app_ids = ?, allowed_titles = ?, excluded = ?, done = ? "
                             "WHERE id = ?";

    if (sqlite3_prepare_v2(m_Db, update_sql, -1, &update_stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in UpdateTask update");
        error = "db prepare failed";
        return false;
    }

    sqlite3_bind_text(update_stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(update_stmt, 2, task.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(update_stmt, 3, start_time);
    sqlite3_bind_double(update_stmt, 4, end_time);
    sqlite3_bind_text(update_stmt, 5, allowed_app_ids.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(update_stmt, 6, allowed_titles.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(update_stmt, 7, excluded);
    sqlite3_bind_int(update_stmt, 8, done);
    sqlite3_bind_int(update_stmt, 9, task_id);

    if (sqlite3_step(update_stmt) != SQLITE_DONE) {
        spdlog::error("db update failed in UpdateTask");
        sqlite3_finalize(update_stmt);
        error = "db update failed";
        return false;
    }

    sqlite3_finalize(update_stmt);
    UpsertCategory(category, allowed_app_ids, allowed_titles);
    spdlog::info("Updated task ID: {}", task_id);
    return true;
}

// ─────────────────────────────────────
bool SQLite::UpsertActivityCategory(const std::string &app_id, const std::string &title,
                                    const std::string &category, std::string &error) {
    if (app_id.empty() && title.empty()) {
        error = "missing app_id/title";
        return false;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "INSERT INTO focus_activity_categories "
                      "(app_id, title, category, updated_at) "
                      "VALUES (?, ?, ?, ?) "
                      "ON CONFLICT(app_id, title) DO UPDATE SET "
                      "category=excluded.category, updated_at=excluded.updated_at";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in UpsertActivityCategory");
        error = "db prepare failed";
        return false;
    }

    double now =
        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_bind_text(stmt, 1, app_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, now);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (!category.empty()) {
        nlohmann::json apps = nlohmann::json::array();
        nlohmann::json titles = nlohmann::json::array();
        if (!app_id.empty()) {
            apps.push_back(app_id);
        }
        if (!title.empty()) {
            titles.push_back(title);
        }
        UpsertCategory(category, apps, titles);
    }

    spdlog::debug("Upserted activity category: app_id={}, title={}, category={}", app_id, title,
                  category);
    return true;
}

// ─────────────────────────────────────
nlohmann::json SQLite::FetchEvents() {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT * FROM focus_events ORDER BY start_time";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in FetchEvents");
        throw std::runtime_error("db prepare failed");
    }

    nlohmann::json rows = nlohmann::json::array();

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json row;
        row["id"] = sqlite3_column_int(stmt, 0);

        const unsigned char *app_id_txt = sqlite3_column_text(stmt, 1);
        row["app_id"] = app_id_txt ? reinterpret_cast<const char *>(app_id_txt) : "";

        row["window_id"] = sqlite3_column_int(stmt, 2);

        const unsigned char *title_txt = sqlite3_column_text(stmt, 3);
        row["title"] = title_txt ? reinterpret_cast<const char *>(title_txt) : "";

        row["start_time"] = sqlite3_column_double(stmt, 4);
        row["end_time"] = sqlite3_column_double(stmt, 5);
        row["duration"] = sqlite3_column_double(stmt, 6);

        rows.push_back(row);
    }

    sqlite3_finalize(stmt);
    spdlog::debug("Fetched {} events", rows.size());
    return rows;
}

// ─────────────────────────────────────
nlohmann::json SQLite::FetchHistory() {
    sqlite3_stmt *stmt = nullptr;

    const char *sql = R"(
        SELECT 
            e.app_id, 
            e.title, 
            SUM(e.duration) AS total_duration, 
            MIN(e.start_time) AS first_start, 
            MAX(e.end_time) AS last_end, 
            c.category
        FROM focus_events e
        LEFT JOIN focus_activity_categories c
            ON e.app_id = c.app_id AND e.title = c.title
        GROUP BY e.app_id, e.title
        ORDER BY total_duration DESC
    )";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in FetchHistory");
        throw std::runtime_error("db prepare failed");
    }

    nlohmann::json rows = nlohmann::json::array();

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json row;

        const char *app_id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        const char *title = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        const char *category = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));

        row["app_id"] = app_id ? app_id : "";
        row["title"] = title ? title : "";
        row["total_duration"] = sqlite3_column_double(stmt, 2);
        row["start"] = sqlite3_column_double(stmt, 3); // real first start
        row["end"] = sqlite3_column_double(stmt, 4);   // last end
        row["category"] = category ? category : "";

        rows.push_back(row);
    }

    sqlite3_finalize(stmt);
    spdlog::debug("Fetched {} history entries", rows.size());
    return rows;
}

// ─────────────────────────────────────
nlohmann::json SQLite::FetchCategories() {
    sqlite3_stmt *stmt = nullptr;

    const char *sql = "SELECT category, allowed_app_ids, allowed_titles, updated_at "
                      "FROM focus_categories "
                      "ORDER BY updated_at DESC";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in FetchCategories");
        throw std::runtime_error("db prepare failed");
    }

    nlohmann::json rows = nlohmann::json::array();

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json row;

        const char *category = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        const char *apps = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        const char *titles = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));

        row["category"] = category ? category : "";

        try {
            row["allowed_app_ids"] = apps ? nlohmann::json::parse(apps) : nlohmann::json::array();
        } catch (...) {
            row["allowed_app_ids"] = nlohmann::json::array();
        }

        try {
            row["allowed_titles"] =
                titles ? nlohmann::json::parse(titles) : nlohmann::json::array();
        } catch (...) {
            row["allowed_titles"] = nlohmann::json::array();
        }

        row["updated_at"] = sqlite3_column_double(stmt, 3);

        rows.push_back(row);
    }

    sqlite3_finalize(stmt);
    spdlog::debug("Fetched {} categories", rows.size());
    return rows;
}

// ─────────────────────────────────────
void SQLite::ExecIgnoringErrors(const std::string &sql) {
    char *errmsg = nullptr;
    sqlite3_exec(m_Db, sql.c_str(), nullptr, nullptr, &errmsg);
    if (errmsg) {
        spdlog::warn("sqlite exec error: {}", errmsg);
        sqlite3_free(errmsg);
    }
}
