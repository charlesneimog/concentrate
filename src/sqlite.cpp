#include "sqlite.hpp"
#include <spdlog/spdlog.h>

// ─────────────────────────────────────
SQLite::SQLite(const std::string &db_path) : m_Db(nullptr), m_DbPath(db_path) {
    if (sqlite3_open(m_DbPath.c_str(), &m_Db) != SQLITE_OK) {
        spdlog::error("unable to open database: {}", m_DbPath);
        throw std::runtime_error("unable to open database");
    }

    spdlog::debug("SQLite database opened: {}", m_DbPath);
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

    // Save all inside this
    ExecIgnoringErrors("CREATE TABLE IF NOT EXISTS focus_log ("
                       "app_id TEXT,"
                       "title TEXT,"
                       "task_category TEXT DEFAULT '',"
                       "state INTEGER,"
                       "start_time REAL NOT NULL,"
                       "end_time REAL NOT NULL,"
                       "duration REAL NOT NULL"
                       ")");

    // Recurring daily tasks config
    ExecIgnoringErrors("CREATE TABLE IF NOT EXISTS recurring_tasks ("
                       "name TEXT NOT NULL UNIQUE,"
                       "app_ids TEXT,"
                       "app_titles TEXT,"
                       "icon TEXT,"
                       "color TEXT,"
                       "updated_at REAL)");

    spdlog::debug("SQLite database tables initialized");
}

// ─────────────────────────────────────
void SQLite::InsertEventNew(const std::string &appId, const std::string &title,
                            const std::string &taskCategory, double start_time, double end_time,
                            double duration, int state) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = R"(
        INSERT INTO focus_log
        (app_id, title, task_category, state, start_time, end_time, duration)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in InsertEvent: {}", sqlite3_errmsg(m_Db));
        return;
    }

    sqlite3_bind_text(stmt, 1, appId.empty() ? nullptr : appId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, title.empty() ? nullptr : title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, taskCategory.empty() ? nullptr : taskCategory.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, state);
    sqlite3_bind_double(stmt, 5, start_time);
    sqlite3_bind_double(stmt, 6, end_time);
    sqlite3_bind_double(stmt, 7, duration);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        spdlog::error("InsertEvent failed: {}", sqlite3_errmsg(m_Db));
    }
    sqlite3_finalize(stmt);

    spdlog::debug("Inserted log: app_id={}, title={}, category={}, state={}, duration={}", appId,
                  title, taskCategory, state, duration);
}

// ─────────────────────────────────────
bool SQLite::UpdateEventNew(const std::string &appId, const std::string &title,
                            const std::string &taskCategory, double end_time, double duration,
                            int state) {
    sqlite3_stmt *stmt = nullptr;

    // Update the most recent row matching either app event or focus state
    const char *sql = "UPDATE focus_log SET "
                      "end_time = ?, "
                      "duration = ?, "
                      "task_category = ?, "
                      "state = ? "
                      "WHERE rowid = ("
                      "  SELECT MAX(rowid) FROM focus_log "
                      "  WHERE (app_id = ? AND title = ?) OR state = ?"
                      ")";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in UpdateEventNew: {}", sqlite3_errmsg(m_Db));
        return false;
    }

    // Bind new values
    sqlite3_bind_double(stmt, 1, end_time);
    sqlite3_bind_double(stmt, 2, duration);
    sqlite3_bind_text(stmt, 3, taskCategory.empty() ? nullptr : taskCategory.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, state);

    // Bind WHERE subquery values
    sqlite3_bind_text(stmt, 5, appId.empty() ? nullptr : appId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, title.empty() ? nullptr : title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, state);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("UpdateEventNew failed: {}", sqlite3_errmsg(m_Db));
        return false;
    }

    spdlog::debug("Updated log: app_id={}, title={}, category={}, state={}, duration={}", appId,
                  title, taskCategory, state, duration);
    return true;
}

// ─────────────────────────────────────
nlohmann::json SQLite::FetchTodayCategorySummary() {
    nlohmann::json rows = nlohmann::json::array();

    // Sum durations grouped by app_id category for today
    const char *sql = "SELECT task_category AS app_category, SUM(duration) AS total_seconds "
                      "FROM focus_log "
                      "WHERE task_category != '' "
                      "  AND date(start_time, 'unixepoch', 'localtime') = date('now', 'localtime') "
                      "GROUP BY task_category "
                      "ORDER BY total_seconds DESC";

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

    spdlog::debug("Fetched {} app categories for today from focus_log", rows.size());
    return rows;
}

// ─────────────────────────────────────
nlohmann::json SQLite::GetTodayFocusSummary() {
    sqlite3_stmt *stmt = nullptr;

    const char *sql = "SELECT state, SUM(duration) AS total_duration "
                      "FROM focus_log "
                      "WHERE state IS NOT NULL "
                      "  AND date(start_time, 'unixepoch', 'localtime') = date('now', 'localtime') "
                      "GROUP BY state";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in GetTodayFocusSummary: {}", sqlite3_errmsg(m_Db));
        throw std::runtime_error("db prepare failed");
    }

    double focused = 0.0;
    double unfocused = 0.0;
    double idle = 0.0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int state = sqlite3_column_int(stmt, 0);
        double total = sqlite3_column_double(stmt, 1);

        switch (state) {
        case 1:
            focused = total;
            break;
        case 2:
            unfocused = total;
            break;
        case 3:
            idle = total;
            break;
        }
    }

    sqlite3_finalize(stmt);

    spdlog::debug("Today's focus summary: focused={}, unfocused={}, idle={}", focused, unfocused,
                  idle);

    return {{"focused", focused}, {"unfocused", unfocused}, {"idle", idle}};
}

// ─────────────────────────────────────
nlohmann::json SQLite::GetTodayFocusTimeSummary() {
    sqlite3_stmt *stmt = nullptr;

    // Sum durations grouped by state for today
    const char *sql = "SELECT state, SUM(duration) AS total_duration "
                      "FROM focus_log "
                      "WHERE state IS NOT NULL "
                      "  AND date(start_time, 'unixepoch', 'localtime') = date('now', 'localtime') "
                      "GROUP BY state";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in GetTodayFocusTimeSummary: {}", sqlite3_errmsg(m_Db));
        throw std::runtime_error("db prepare failed");
    }

    double focused = 0.0;
    double unfocused = 0.0;
    double idle = 0.0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int state = sqlite3_column_int(stmt, 0);
        double total = sqlite3_column_double(stmt, 1);

        switch (state) {
        case 1:
            focused = total;
            break; // FOCUSED
        case 2:
            unfocused = total;
            break; // UNFOCUSED
        case 3:
            idle = total;
            break; // IDLE
        }
    }

    sqlite3_finalize(stmt);

    spdlog::debug("Today's focus time summary: focused={}, unfocused={}, idle={}", focused,
                  unfocused, idle);

    return {{"focused_seconds", focused}, {"unfocused_seconds", unfocused}, {"idle_seconds", idle}};
}

// ─────────────────────────────────────
nlohmann::json SQLite::GetFocusPercentageByCategory(int days) {
    sqlite3_stmt *stmt = nullptr;

    // SQL: sum duration by category in the last `days` days
    const char *sql = "SELECT task_category, SUM(duration) AS total_seconds "
                      "FROM focus_log "
                      "WHERE task_category != '' "
                      "  AND start_time >= strftime('%s', 'now', 'localtime', ? || ' days') "
                      "GROUP BY task_category";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in GetFocusPercentageByCategory: {}",
                      sqlite3_errmsg(m_Db));
        throw std::runtime_error("db prepare failed");
    }

    // Pass the negative number of days as string, e.g., "-1 days"
    std::string daysArg = "-" + std::to_string(days);
    sqlite3_bind_text(stmt, 1, daysArg.c_str(), -1, SQLITE_TRANSIENT);

    nlohmann::json rows = nlohmann::json::array();
    double totalDuration = 0.0;
    std::vector<std::pair<std::string, double>> temp;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *category_txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        double duration = sqlite3_column_double(stmt, 1);

        if (category_txt && duration > 0) {
            temp.emplace_back(category_txt, duration);
            totalDuration += duration;
        }
    }

    sqlite3_finalize(stmt);

    // Convert to percentage
    for (const auto &[category, duration] : temp) {
        double pct = totalDuration > 0 ? (duration / totalDuration) * 100.0 : 0.0;
        rows.push_back({{"category", category}, {"percentage", pct}});
    }

    return rows;
}

// ─────────────────────────────────────
nlohmann::json SQLite::FetchDailyAppUsageByAppId(int days) {
    sqlite3_stmt *stmt = nullptr;

    // Start of today (00:00 local time)
    std::time_t now = std::time(nullptr);
    std::tm tm = *std::localtime(&now);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;

    std::time_t dayStart = std::mktime(&tm) - (days - 1) * 86400;

    const char *sql = "SELECT "
                      "  strftime('%Y-%m-%d', start_time, 'unixepoch', 'localtime') AS day, "
                      "  app_id, "
                      "  COALESCE(title, '') AS title, "
                      "  SUM(duration) AS total_seconds "
                      "FROM focus_log "
                      "WHERE start_time >= ? "
                      "  AND app_id IS NOT NULL "
                      "  AND app_id <> '' "
                      "GROUP BY day, app_id, title "
                      "ORDER BY day ASC";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("prepare failed in FetchDailyAppUsageByAppId: {}", sqlite3_errmsg(m_Db));
        return {};
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(dayStart));

    nlohmann::json result = nlohmann::json::object();

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *dayTxt = sqlite3_column_text(stmt, 0);
        const unsigned char *appIdTxt = sqlite3_column_text(stmt, 1);
        const unsigned char *titleTxt = sqlite3_column_text(stmt, 2);

        const std::string day = dayTxt ? reinterpret_cast<const char *>(dayTxt) : "";
        const std::string appId = appIdTxt ? reinterpret_cast<const char *>(appIdTxt) : "";
        const std::string title = titleTxt ? reinterpret_cast<const char *>(titleTxt) : "";

        const double seconds = sqlite3_column_double(stmt, 3);

        result[day][appId][title] = seconds;
    }

    sqlite3_finalize(stmt);
    return result;
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

// ╭─────────────────────────────────────╮
// │           Recurring Tasks           │
// ╰─────────────────────────────────────╯
void SQLite::AddRecurringTask(const std::string &name, const std::vector<std::string> &appIds,
                              const std::vector<std::string> &appTitles, const std::string &icon,
                              const std::string &color) {
    sqlite3_stmt *stmt = nullptr;

    const char *sql = "INSERT INTO recurring_tasks "
                      "(name, app_ids, app_titles, icon, color, updated_at) "
                      "VALUES (?, ?, ?, ?, ?, ?)";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in AddRecurringTask: {}", sqlite3_errmsg(m_Db));
        return;
    }

    // Convert vectors to JSON strings
    std::string appIdsJson = nlohmann::json(appIds).dump();
    std::string appTitlesJson = nlohmann::json(appTitles).dump();
    spdlog::error(" \n\n {} {} \n\n", appIdsJson, appTitlesJson);

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, appIdsJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, appTitlesJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, icon.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, color.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(std::time(nullptr)));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        spdlog::error("AddRecurringTask failed: {}", sqlite3_errmsg(m_Db));
    }

    sqlite3_finalize(stmt);
}

// ─────────────────────────────────────
void SQLite::UpdateRecurringTask(const std::string &name, const std::vector<std::string> &appIds,
                                 const std::vector<std::string> &appTitles, const std::string &icon,
                                 const std::string &color) {
    sqlite3_stmt *stmt = nullptr;

    const char *sql = "UPDATE recurring_tasks SET "
                      "app_ids = ?, app_titles = ?, icon = ?, color = ?, updated_at = ? "
                      "WHERE name = ?";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in UpdateRecurringTask: {}", sqlite3_errmsg(m_Db));
        return;
    }

    std::string appIdsJson = nlohmann::json(appIds).dump();
    std::string appTitlesJson = nlohmann::json(appTitles).dump();

    sqlite3_bind_text(stmt, 1, appIdsJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, appTitlesJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, icon.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, color.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, static_cast<double>(std::time(nullptr)));
    sqlite3_bind_text(stmt, 6, name.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        spdlog::error("UpdateRecurringTask failed: {}", sqlite3_errmsg(m_Db));
    }

    sqlite3_finalize(stmt);
}

// ─────────────────────────────────────
void SQLite::ExcludeRecurringTask(const std::string &name) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "DELETE FROM recurring_tasks WHERE name = ?";
    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in ExcludeRecurringTask: {}", sqlite3_errmsg(m_Db));
        return;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        spdlog::error("ExcludeRecurringTask failed: {}", sqlite3_errmsg(m_Db));
    }
    sqlite3_finalize(stmt);
}

// ─────────────────────────────────────
nlohmann::json SQLite::FetchRecurringTasks() {
    sqlite3_stmt *stmt = nullptr;

    const char *sql = "SELECT name, app_ids, app_titles, icon, color, updated_at "
                      "FROM recurring_tasks ORDER BY updated_at DESC";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in FetchRecurringTasks: {}", sqlite3_errmsg(m_Db));
        throw std::runtime_error("db prepare failed");
    }

    nlohmann::json rows = nlohmann::json::array(); // always an array

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json row;

        const char *name_txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        const char *apps_txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        const char *titles_txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        const char *icon_txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        const char *color_txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        double updated_at = sqlite3_column_double(stmt, 5);

        row["name"] = name_txt ? name_txt : "";

        try {
            row["app_ids"] = apps_txt ? nlohmann::json::parse(apps_txt) : nlohmann::json::array();
        } catch (...) {
            row["app_ids"] = nlohmann::json::array();
        }

        try {
            row["app_titles"] =
                titles_txt ? nlohmann::json::parse(titles_txt) : nlohmann::json::array();
        } catch (...) {
            row["app_titles"] = nlohmann::json::array();
        }

        row["icon"] = icon_txt ? icon_txt : "";
        row["color"] = color_txt ? color_txt : "";
        row["updated_at"] = updated_at;

        rows.push_back(row);
    }

    sqlite3_finalize(stmt);

    spdlog::debug("Fetched {} recurring tasks", rows.size());

    // always return an array, even if empty
    return rows;
}

// ╭─────────────────────────────────────╮
// │             Historical              │
// ╰─────────────────────────────────────╯
nlohmann::json SQLite::FetchEvents() {
    sqlite3_stmt *stmt = nullptr;

    const char *sql = "SELECT app_id, title, task_category, state, duration "
                      "FROM focus_log "
                      "WHERE start_time >= strftime('%s','now','-7 days') "
                      "ORDER BY start_time";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in FetchLast7Days: {}", sqlite3_errmsg(m_Db));
        return {};
    }

    nlohmann::json rows = nlohmann::json::array();

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json row;

        const unsigned char *appId_txt = sqlite3_column_text(stmt, 0);
        row["app_id"] = appId_txt ? reinterpret_cast<const char *>(appId_txt) : "";

        const unsigned char *title_txt = sqlite3_column_text(stmt, 1);
        row["title"] = title_txt ? reinterpret_cast<const char *>(title_txt) : "";

        const unsigned char *category_txt = sqlite3_column_text(stmt, 2);
        row["task_category"] = category_txt ? reinterpret_cast<const char *>(category_txt) : "";

        // state can be NULL for app events
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
            row["state"] = sqlite3_column_int(stmt, 3);
        } else {
            row["state"] = nullptr;
        }

        row["duration"] = sqlite3_column_double(stmt, 4);

        rows.push_back(row);
    }

    sqlite3_finalize(stmt);
    spdlog::debug("Fetched {} rows from last 7 days", rows.size());
    return rows;
}

// ─────────────────────────────────────
nlohmann::json SQLite::FetchHistory() {
    sqlite3_stmt *stmt = nullptr;

    const char *sql = R"(
        SELECT 
            app_id,
            title,
            COALESCE(
                NULLIF(MAX(task_category), ''), 
                'uncategorized'
            ) AS category,
            SUM(duration) AS total_duration,
            MIN(start_time) AS first_start,
            MAX(end_time) AS last_end
        FROM focus_events
        GROUP BY app_id, title
        ORDER BY total_duration DESC
    )";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in FetchHistory: {}", sqlite3_errmsg(m_Db));
        return nlohmann::json::array();
    }

    nlohmann::json rows = nlohmann::json::array();

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json row;

        const char *appId = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        const char *title = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        const char *category = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));

        row["app_id"] = appId ? appId : "";
        row["title"] = title ? title : "";
        row["category"] = category ? category : "uncategorized";
        row["total_duration"] = sqlite3_column_double(stmt, 3);
        row["start"] = sqlite3_column_double(stmt, 4);
        row["end"] = sqlite3_column_double(stmt, 5);

        rows.push_back(row);
    }

    sqlite3_finalize(stmt);
    spdlog::debug("Fetched {} history entries", rows.size());
    return rows;
}

// ─────────────────────────────────────
void SQLite::ExecIgnoringErrors(const std::string &sql) {
    char *errmsg = nullptr;
    sqlite3_exec(m_Db, sql.c_str(), nullptr, nullptr, &errmsg);
    if (errmsg) {
        spdlog::error("sqlite exec error: {}", errmsg);
        sqlite3_free(errmsg);
    }
}
