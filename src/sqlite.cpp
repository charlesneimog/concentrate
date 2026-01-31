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
    ExecIgnoringErrors("ALTER TABLE recurring_tasks ADD COLUMN excluded INTEGER DEFAULT 0");

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
                       "windowId INTEGER,"
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
                       "allowedAppIds TEXT,"
                       "allowedTitles TEXT,"
                       "excluded INTEGER DEFAULT 0,"
                       "done INTEGER DEFAULT 0)");

    // Categories
    ExecIgnoringErrors("CREATE TABLE IF NOT EXISTS focus_categories ("
                       "category TEXT PRIMARY KEY,"
                       "allowedAppIds TEXT,"
                       "allowedTitles TEXT,"
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

    // Recurring daily tasks config
    ExecIgnoringErrors("CREATE TABLE IF NOT EXISTS recurring_tasks ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "name TEXT NOT NULL UNIQUE,"
                       "app_ids TEXT,"
                       "app_titles TEXT,"
                       "icon TEXT,"
                       "color TEXT,"
                       "updated_at REAL)");

    spdlog::info("SQLite database tables initialized");
}

// ─────────────────────────────────────
void SQLite::InsertEvent(const std::string &appId, int windowId, const std::string &title,
                         double start_time, double end_time, double duration) {
    sqlite3_stmt *stmt = nullptr;

    const char *sql = "INSERT INTO focus_events "
                      "(app_id, windowId, title, start_time, end_time, duration) "
                      "VALUES (?, ?, ?, ?, ?, ?)";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_text(stmt, 1, appId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, windowId);
    sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, start_time);
    sqlite3_bind_double(stmt, 5, end_time);
    sqlite3_bind_double(stmt, 6, duration);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    spdlog::debug("Inserted focus event: app_id={}, title={}, duration={}", appId, title, duration);
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
void SQLite::UpsertCategory(const std::string &category, const nlohmann::json &allowedAppIds,
                            const nlohmann::json &allowedTitles) {
    if (category.empty()) {
        return;
    }

    sqlite3_stmt *select_stmt = nullptr;
    const char *select_sql = "SELECT allowedAppIds, allowedTitles "
                             "FROM focus_categories WHERE category = ?";

    if (sqlite3_prepare_v2(m_Db, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_text(select_stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);

    nlohmann::json merged_apps = allowedAppIds.is_array() ? allowedAppIds : nlohmann::json::array();

    nlohmann::json merged_titles =
        allowedTitles.is_array() ? allowedTitles : nlohmann::json::array();

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
                      "(category, allowedAppIds, allowedTitles, updated_at) "
                      "VALUES (?, ?, ?, ?) "
                      "ON CONFLICT(category) DO UPDATE SET "
                      "allowedAppIds=excluded.allowedAppIds, "
                      "allowedTitles=excluded.allowedTitles, "
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
    nlohmann::json allowedAppIds =
        data.contains("allowedAppIds") ? data["allowedAppIds"] : nlohmann::json::array();

    nlohmann::json allowedTitles =
        data.contains("allowedTitles") ? data["allowedTitles"] : nlohmann::json::array();

    int excluded = data.value("exclude", false) ? 1 : 0;
    int done = data.value("done", false) ? 1 : 0;

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO focus_tasks "
        "(category, task, start_time, end_time, allowedAppIds, allowedTitles, excluded, done) "
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
    sqlite3_bind_text(stmt, 5, allowedAppIds.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, allowedTitles.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, excluded);
    sqlite3_bind_int(stmt, 8, done);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        spdlog::error("db insert failed in CreateTask");
        sqlite3_finalize(stmt);
        error = "db insert failed";
        return false;
    }

    sqlite3_finalize(stmt);
    UpsertCategory(category, allowedAppIds, allowedTitles);
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

    nlohmann::json allowedAppIds = data.contains("allowedAppIds")
                                       ? data["allowedAppIds"]
                                       : nlohmann::json::parse(column_text(5));

    nlohmann::json allowedTitles = data.contains("allowedTitles")
                                       ? data["allowedTitles"]
                                       : nlohmann::json::parse(column_text(6));

    int excluded =
        data.contains("exclude") ? (data.value("exclude", false) ? 1 : 0) : column_int(7);
    int done = data.contains("done") ? (data.value("done", false) ? 1 : 0) : column_int(8);

    sqlite3_finalize(select_stmt);

    sqlite3_stmt *update_stmt = nullptr;
    const char *update_sql = "UPDATE focus_tasks "
                             "SET category = ?, task = ?, start_time = ?, end_time = ?, "
                             "allowedAppIds = ?, allowedTitles = ?, excluded = ?, done = ? "
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
    sqlite3_bind_text(update_stmt, 5, allowedAppIds.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(update_stmt, 6, allowedTitles.dump().c_str(), -1, SQLITE_TRANSIENT);
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
    UpsertCategory(category, allowedAppIds, allowedTitles);
    spdlog::info("Updated task ID: {}", task_id);
    return true;
}

// ─────────────────────────────────────
bool SQLite::UpsertActivityCategory(const std::string &appId, const std::string &title,
                                    const std::string &category, std::string &error) {
    if (appId.empty() && title.empty()) {
        error = "missing appId/title";
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

    sqlite3_bind_text(stmt, 1, appId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, now);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (!category.empty()) {
        nlohmann::json apps = nlohmann::json::array();
        nlohmann::json titles = nlohmann::json::array();
        if (!appId.empty()) {
            apps.push_back(appId);
        }
        if (!title.empty()) {
            titles.push_back(title);
        }
        UpsertCategory(category, apps, titles);
    }

    spdlog::debug("Upserted activity category: app_id={}, title={}, category={}", appId, title,
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

        const unsigned char *appId_txt = sqlite3_column_text(stmt, 1);
        row["app_id"] = appId_txt ? reinterpret_cast<const char *>(appId_txt) : "";

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

        const char *appId = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        const char *title = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        const char *category = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));

        row["app_id"] = appId ? appId : "";
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

// ─────────────────────────────────────
bool SQLite::UpsertRecurringTask(const nlohmann::json &data, std::string &error) {
    std::string name = data.value("name", "");
    if (name.empty()) {
        error = "missing name";
        return false;
    }

    nlohmann::json appIds = data.contains("app_ids") ? data["app_ids"] : nlohmann::json::array();
    nlohmann::json app_titles =
        data.contains("app_titles") ? data["app_titles"] : nlohmann::json::array();

    std::string icon = data.value("icon", "");
    std::string color = data.value("color", "");

    const char *sql = R"(
        INSERT INTO recurring_tasks
            (name, app_ids, app_titles, icon, color, updated_at, excluded)
        VALUES (?, ?, ?, ?, ?, ?, 0)
        ON CONFLICT(name) DO UPDATE SET
            app_ids = excluded.app_ids,
            app_titles = excluded.app_titles,
            icon = excluded.icon,
            color = excluded.color,
            updated_at = excluded.updated_at,
            excluded = 0
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(m_Db);
        return false;
    }

    double now =
        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, appIds.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, app_titles.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, icon.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, color.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 6, now);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        error = sqlite3_errmsg(m_Db);
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    spdlog::info("Upserted recurring task: {}", name);
    return true;
}

// ─────────────────────────────────────
nlohmann::json SQLite::FetchRecurringTasks() {
    const char *sql = R"(
        SELECT name, app_ids, app_titles, icon, color, updated_at
        FROM recurring_tasks
        WHERE excluded = 0
        ORDER BY updated_at DESC
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("db prepare failed");
    }

    nlohmann::json rows = nlohmann::json::array();

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json row;
        row["name"] = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));

        const char *apps = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        const char *titles = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));

        row["app_ids"] = apps ? nlohmann::json::parse(apps) : nlohmann::json::array();
        row["app_titles"] = titles ? nlohmann::json::parse(titles) : nlohmann::json::array();

        row["icon"] = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        row["color"] = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        row["updated_at"] = sqlite3_column_double(stmt, 5);

        rows.push_back(row);
    }

    sqlite3_finalize(stmt);
    return rows;
}

// ──────────────────────────────────────────
nlohmann::json SQLite::FetchTodayCategorySummary() {
    nlohmann::json rows = nlohmann::json::array();

    const char *sql = R"(
        SELECT t.category,
               SUM(e.duration) AS total_seconds
        FROM focus_tasks t
        JOIN focus_activity_categories c
          ON c.category = t.category
        JOIN focus_events e
          ON e.app_id = c.app_id AND e.title = c.title
        WHERE t.excluded = 0
          AND date(e.start_time, 'unixepoch', 'localtime') = date('now', 'localtime')
        GROUP BY t.category
        ORDER BY total_seconds DESC
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in FetchTodayCategorySummary: {}", sqlite3_errmsg(m_Db));
        return rows;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *category = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        double total_seconds = sqlite3_column_double(stmt, 1);

        rows.push_back({{"category", category ? category : ""}, {"total_seconds", total_seconds}});
    }

    sqlite3_finalize(stmt);
    spdlog::debug("Fetched {} categories from recurring tasks for today", rows.size());
    return rows;
}

// ──────────────────────────────────────────
bool SQLite::ExcludeRecurringTask(const std::string &name, std::string &error) {
    if (name.empty()) {
        error = "missing task name";
        return false;
    }

    const char *sql = R"(
        UPDATE recurring_tasks
        SET excluded = 1,
            updated_at = ?
        WHERE name = ?
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(m_Db);
        return false;
    }

    // Current timestamp in seconds
    double now =
        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_bind_double(stmt, 1, now);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        error = sqlite3_errmsg(m_Db);
        return false;
    }

    // Check if a row was actually updated
    if (sqlite3_changes(m_Db) == 0) {
        error = "task not found";
        return false;
    }

    spdlog::info("Excluded recurring task: {}", name);
    return true;
}
