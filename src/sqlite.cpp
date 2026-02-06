#include "sqlite.hpp"
#include <chrono>
#include <spdlog/spdlog.h>
#include <unordered_map>

// ─────────────────────────────────────
SQLite::SQLite(const std::string &db_path) : m_Db(nullptr), m_DbPath(db_path) {
    if (sqlite3_open(m_DbPath.c_str(), &m_Db) != SQLITE_OK) {
        spdlog::error("unable to open database: {}", m_DbPath);
        throw std::runtime_error("unable to open database");
    }

    spdlog::debug("SQLite database opened: {}", m_DbPath);

    // Reduce heap churn by enabling a small lookaside buffer.
    // This gives deterministic per-connection memory and keeps small allocations off malloc.
    sqlite3_db_config(m_Db, SQLITE_DBCONFIG_LOOKASIDE, m_Lookaside.data(), kLookasideSlotSize,
                      kLookasideSlotCount);

    sqlite3_busy_timeout(m_Db, 2000);
    ExecIgnoringErrors("PRAGMA journal_mode=WAL");
    ExecIgnoringErrors("PRAGMA wal_autocheckpoint=1000");
    ExecIgnoringErrors("PRAGMA journal_size_limit=10485760");
    ExecIgnoringErrors("PRAGMA synchronous=NORMAL");
    // For low RSS: avoid holding temp btrees in memory.
    ExecIgnoringErrors("PRAGMA temp_store=FILE");
    ExecIgnoringErrors("PRAGMA cache_size = -1000;");
    ExecIgnoringErrors("PRAGMA mmap_size=0;");
    ExecIgnoringErrors("PRAGMA secure_delete=ON");
    ExecIgnoringErrors("PRAGMA auto_vacuum=INCREMENTAL");
    ExecIgnoringErrors("PRAGMA optimize");

    Init();
    PrepareStatements();
}

// ─────────────────────────────────────
SQLite::~SQLite() {
    if (m_InsertEventStmt) {
        sqlite3_finalize(m_InsertEventStmt);
        m_InsertEventStmt = nullptr;
    }
    if (m_UpdateEventStmt) {
        sqlite3_finalize(m_UpdateEventStmt);
        m_UpdateEventStmt = nullptr;
    }
    if (m_InsertMonitoringStmt) {
        sqlite3_finalize(m_InsertMonitoringStmt);
        m_InsertMonitoringStmt = nullptr;
    }
    if (m_UpdateMonitoringStmt) {
        sqlite3_finalize(m_UpdateMonitoringStmt);
        m_UpdateMonitoringStmt = nullptr;
    }
    if (m_Db) {
        sqlite3_close(m_Db);
        m_Db = nullptr;
    }
}

// ─────────────────────────────────────
double SQLite::GetLocalDayStartEpoch(int days) {
    using namespace std::chrono;

    if (days < 0) {
        days = 0;
    }

    const auto now = system_clock::now();
    const auto *tz = current_zone();
    const zoned_time zt{tz, now};

    const auto local_now = zt.get_local_time();
    const auto local_midnight = floor<std::chrono::days>(local_now) - std::chrono::days{days};
    const auto sys_midnight = tz->to_sys(local_midnight, choose::earliest);

    return duration<double>(sys_midnight.time_since_epoch()).count();
}

// ─────────────────────────────────────
void SQLite::PrepareStatements() {
    {
        const char *sql = R"(
            INSERT INTO focus_log
            (app_id, title, task_category, state, start_time, end_time, duration)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        )";
        if (sqlite3_prepare_v2(m_Db, sql, -1, &m_InsertEventStmt, nullptr) != SQLITE_OK) {
            spdlog::error("db prepare failed for InsertEvent stmt: {}", sqlite3_errmsg(m_Db));
            m_InsertEventStmt = nullptr;
        }
    }

    {
                // Update the most recent record for the exact (app_id, title, state).
                // Use SQLite's NULL-safe `IS` operator so NULL app_id/title can still match.
                const char *sql = R"(
                        UPDATE focus_log SET
                            end_time = ?,
                            duration = ?,
                            task_category = ?,
                            state = ?
                        WHERE rowid = (
                            SELECT MAX(rowid) FROM focus_log
                            WHERE app_id IS ? AND title IS ? AND state = ?
                        )
                )";
        if (sqlite3_prepare_v2(m_Db, sql, -1, &m_UpdateEventStmt, nullptr) != SQLITE_OK) {
            spdlog::error("db prepare failed for UpdateEvent stmt: {}", sqlite3_errmsg(m_Db));
            m_UpdateEventStmt = nullptr;
        }
    }

    {
        const char *sql = R"(
            INSERT INTO monitoring_log
            (state, start_time, end_time, duration)
            VALUES (?, ?, ?, ?)
        )";
        if (sqlite3_prepare_v2(m_Db, sql, -1, &m_InsertMonitoringStmt, nullptr) != SQLITE_OK) {
            spdlog::error("db prepare failed for InsertMonitoring stmt: {}", sqlite3_errmsg(m_Db));
            m_InsertMonitoringStmt = nullptr;
        }
    }

    {
        const char *sql = R"(
            UPDATE monitoring_log SET
                end_time = ?,
                duration = ?
            WHERE rowid = (
                SELECT MAX(rowid) FROM monitoring_log
                WHERE state = ?
            )
        )";
        if (sqlite3_prepare_v2(m_Db, sql, -1, &m_UpdateMonitoringStmt, nullptr) != SQLITE_OK) {
            spdlog::error("db prepare failed for UpdateMonitoring stmt: {}", sqlite3_errmsg(m_Db));
            m_UpdateMonitoringStmt = nullptr;
        }
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

    ExecIgnoringErrors("CREATE TABLE IF NOT EXISTS monitoring_log ("
                       "state INTEGER NOT NULL,"
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

    // Pomodoro: last known state (single-row)
    ExecIgnoringErrors(
        "CREATE TABLE IF NOT EXISTS pomodoro_state ("
        "id INTEGER PRIMARY KEY CHECK (id = 1),"
        "phase TEXT NOT NULL,"
        "cycle_step INTEGER NOT NULL,"
        "is_running INTEGER NOT NULL,"
        "is_paused INTEGER NOT NULL,"
        "time_left INTEGER NOT NULL,"
        "focus_duration INTEGER NOT NULL,"
        "short_break_duration INTEGER NOT NULL,"
        "long_break_duration INTEGER NOT NULL,"
        "auto_start_breaks INTEGER NOT NULL,"
        "updated_at REAL NOT NULL"
        ")");

    // Pomodoro: per-day focus counts/totals
    ExecIgnoringErrors(
        "CREATE TABLE IF NOT EXISTS pomodoro_daily ("
        "day TEXT PRIMARY KEY,"
        "focus_sessions INTEGER NOT NULL DEFAULT 0,"
        "focus_seconds INTEGER NOT NULL DEFAULT 0,"
        "updated_at REAL NOT NULL"
        ")");

    spdlog::debug("SQLite database tables initialized");
}

// ─────────────────────────────────────
void SQLite::InsertMonitoringSession(double start_time, double end_time, double duration,
                                     int state) {
    if (!m_InsertMonitoringStmt) {
        spdlog::error("InsertMonitoring stmt not prepared");
        return;
    }

    sqlite3_reset(m_InsertMonitoringStmt);
    sqlite3_clear_bindings(m_InsertMonitoringStmt);

    sqlite3_bind_int(m_InsertMonitoringStmt, 1, state);
    sqlite3_bind_double(m_InsertMonitoringStmt, 2, start_time);
    sqlite3_bind_double(m_InsertMonitoringStmt, 3, end_time);
    sqlite3_bind_double(m_InsertMonitoringStmt, 4, duration);

    const int rc = sqlite3_step(m_InsertMonitoringStmt);
    if (rc != SQLITE_DONE) {
        spdlog::error("InsertMonitoring failed: {}", sqlite3_errmsg(m_Db));
    }
}

// ─────────────────────────────────────
bool SQLite::UpdateMonitoringSession(double end_time, double duration, int state) {
    if (!m_UpdateMonitoringStmt) {
        spdlog::error("UpdateMonitoring stmt not prepared");
        return false;
    }

    sqlite3_reset(m_UpdateMonitoringStmt);
    sqlite3_clear_bindings(m_UpdateMonitoringStmt);

    sqlite3_bind_double(m_UpdateMonitoringStmt, 1, end_time);
    sqlite3_bind_double(m_UpdateMonitoringStmt, 2, duration);
    sqlite3_bind_int(m_UpdateMonitoringStmt, 3, state);

    const int rc = sqlite3_step(m_UpdateMonitoringStmt);
    if (rc != SQLITE_DONE) {
        spdlog::error("UpdateMonitoring failed: {}", sqlite3_errmsg(m_Db));
        return false;
    }

    // If no row matched, SQLite still returns DONE, but changes() will be 0.
    return sqlite3_changes(m_Db) > 0;
}

// ─────────────────────────────────────
nlohmann::json SQLite::GetTodayMonitoringTimeSummary() {
    const double from_epoch = GetLocalDayStartEpoch(0);

    const char *sql = R"(
        SELECT state, COALESCE(SUM(duration), 0)
        FROM monitoring_log
        WHERE start_time >= ?
        GROUP BY state
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed for GetTodayMonitoringTimeSummary: {}",
                      sqlite3_errmsg(m_Db));
        return { {"monitoring_enabled_seconds", 0}, {"monitoring_disabled_seconds", 0} };
    }

    sqlite3_bind_double(stmt, 1, from_epoch);

    double enabled = 0.0;
    double disabled = 0.0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const int state = sqlite3_column_int(stmt, 0);
        const double total = sqlite3_column_double(stmt, 1);
        if (state == MONITORING_ENABLE) {
            enabled = total;
        } else if (state == MONITORING_DISABLE) {
            disabled = total;
        }
    }

    sqlite3_finalize(stmt);

    return { {"monitoring_enabled_seconds", enabled},
             {"monitoring_disabled_seconds", disabled},
             {"total_seconds", enabled + disabled} };
}

// ─────────────────────────────────────
void SQLite::InsertEventNew(const std::string &appId, const std::string &title,
                            const std::string &taskCategory, double start_time, double end_time,
                            double duration, int state) {
    if (!m_InsertEventStmt) {
        spdlog::error("InsertEvent stmt not prepared");
        return;
    }

    sqlite3_reset(m_InsertEventStmt);
    sqlite3_clear_bindings(m_InsertEventStmt);

    sqlite3_bind_text(m_InsertEventStmt, 1, appId.empty() ? nullptr : appId.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(m_InsertEventStmt, 2, title.empty() ? nullptr : title.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(m_InsertEventStmt, 3,
                      taskCategory.empty() ? nullptr : taskCategory.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(m_InsertEventStmt, 4, state);
    sqlite3_bind_double(m_InsertEventStmt, 5, start_time);
    sqlite3_bind_double(m_InsertEventStmt, 6, end_time);
    sqlite3_bind_double(m_InsertEventStmt, 7, duration);

    int rc = sqlite3_step(m_InsertEventStmt);
    if (rc != SQLITE_DONE) {
        spdlog::error("InsertEvent failed: {}", sqlite3_errmsg(m_Db));
    }

    spdlog::debug("Inserted log: app_id={}, title={}, category={}, state={}, duration={}", appId,
                  title, taskCategory, state, duration);
}

// ─────────────────────────────────────
bool SQLite::UpdateEventNew(const std::string &appId, const std::string &title,
                            const std::string &taskCategory, double end_time, double duration,
                            int state) {
    if (!m_UpdateEventStmt) {
        spdlog::error("UpdateEvent stmt not prepared");
        return false;
    }

    sqlite3_reset(m_UpdateEventStmt);
    sqlite3_clear_bindings(m_UpdateEventStmt);

    // Bind new values
    sqlite3_bind_double(m_UpdateEventStmt, 1, end_time);
    sqlite3_bind_double(m_UpdateEventStmt, 2, duration);
    sqlite3_bind_text(m_UpdateEventStmt, 3,
                      taskCategory.empty() ? nullptr : taskCategory.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(m_UpdateEventStmt, 4, state);

    // Bind WHERE subquery values
    sqlite3_bind_text(m_UpdateEventStmt, 5, appId.empty() ? nullptr : appId.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(m_UpdateEventStmt, 6, title.empty() ? nullptr : title.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(m_UpdateEventStmt, 7, state);

    int rc = sqlite3_step(m_UpdateEventStmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("UpdateEventNew failed: {}", sqlite3_errmsg(m_Db));
        return false;
    }

    if (sqlite3_changes(m_Db) == 0) {
        spdlog::warn(
            "UpdateEventNew affected 0 rows (app_id='{}', title='{}', state={}); record may not exist",
            appId, title, state);
        return false;
    }

    spdlog::debug("Updated log: app_id={}, title={}, category={}, state={}, duration={}", appId,
                  title, taskCategory, state, duration);
    return true;
}

// ─────────────────────────────────────
nlohmann::json SQLite::FetchTodayCategorySummary() {
    nlohmann::json rows = nlohmann::json::array();

    const double from_epoch = GetLocalDayStartEpoch(0);
    const double now_epoch = std::chrono::duration<double>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

    // Sum durations grouped by app_id category for today
    const char *sql = "SELECT task_category AS app_category, SUM(duration) AS total_seconds "
                      "FROM focus_log "
                      "WHERE task_category != '' "
                      "  AND start_time >= ? "
                      "  AND start_time < ? "
                      "GROUP BY task_category "
                      "ORDER BY total_seconds DESC";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in FetchTodayCategorySummary: {}", sqlite3_errmsg(m_Db));
        return rows;
    }

    sqlite3_bind_double(stmt, 1, from_epoch);
    sqlite3_bind_double(stmt, 2, now_epoch);

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
nlohmann::json SQLite::GetFocusSummary(int days) {
    sqlite3_stmt *stmt = nullptr;

    if (days < 1) {
        days = 1;
    }

    const double from_epoch = GetLocalDayStartEpoch(days - 1);
    const double now_epoch = std::chrono::duration<double>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

    const char *sql = "SELECT state, SUM(duration) AS total_duration "
                      "FROM focus_log "
                      "WHERE state IS NOT NULL "
                      "  AND start_time >= ? "
                      "  AND start_time < ? "
                      "GROUP BY state";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in GetFocusSummary: {}", sqlite3_errmsg(m_Db));
        throw std::runtime_error("db prepare failed");
    }

    sqlite3_bind_double(stmt, 1, from_epoch);
    sqlite3_bind_double(stmt, 2, now_epoch);

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

    spdlog::debug("Focus summary (last {} days): focused={}, unfocused={}, idle={}", days, focused,
                  unfocused, idle);

    return {{"focused", focused}, {"unfocused", unfocused}, {"idle", idle}};
}

// ─────────────────────────────────────
nlohmann::json SQLite::GetTodayFocusTimeSummary() {
    sqlite3_stmt *stmt = nullptr;

    const double from_epoch = GetLocalDayStartEpoch(0);
    const double now_epoch = std::chrono::duration<double>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

    // Sum durations grouped by state for today
    const char *sql = "SELECT state, SUM(duration) AS total_duration "
                      "FROM focus_log "
                      "WHERE state IS NOT NULL "
                      "  AND start_time >= ? "
                      "  AND start_time < ? "
                      "GROUP BY state";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in GetTodayFocusTimeSummary: {}", sqlite3_errmsg(m_Db));
        throw std::runtime_error("db prepare failed");
    }

    sqlite3_bind_double(stmt, 1, from_epoch);
    sqlite3_bind_double(stmt, 2, now_epoch);

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
nlohmann::json SQLite::GetTodayDailyActivitiesSummary() {
    sqlite3_stmt *stmt = nullptr;

    const double from_epoch = GetLocalDayStartEpoch(0);
    const double now_epoch = std::chrono::duration<double>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

    const char *sql =
        "SELECT task_category AS name, SUM(duration) AS total_seconds "
        "FROM focus_log "
        "WHERE state = 1 "
        "  AND start_time >= ? "
        "  AND start_time < ? "
        "  AND task_category IN (SELECT name FROM recurring_tasks) "
        "GROUP BY task_category "
        "ORDER BY total_seconds DESC";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in GetTodayDailyActivitiesSummary: {}",
                      sqlite3_errmsg(m_Db));
        throw std::runtime_error("db prepare failed");
    }

    sqlite3_bind_double(stmt, 1, from_epoch);
    sqlite3_bind_double(stmt, 2, now_epoch);

    nlohmann::json rows = nlohmann::json::array();

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name_txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        double total_seconds = sqlite3_column_double(stmt, 1);

        rows.push_back({{"name", name_txt ? name_txt : ""}, {"total_seconds", total_seconds}});
    }

    sqlite3_finalize(stmt);
    return rows;
}

// ─────────────────────────────────────
nlohmann::json SQLite::GetFocusPercentageByCategory(int days) {
    sqlite3_stmt *stmt = nullptr;

    if (days < 1) {
        days = 1;
    }

    const double from_epoch = GetLocalDayStartEpoch(days - 1);
    const double now_epoch = std::chrono::duration<double>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

    // SQL: sum duration by category in the last `days` days
    const char *sql = "SELECT task_category, SUM(duration) AS total_seconds "
                      "FROM focus_log "
                      "WHERE task_category != '' "
                      "  AND start_time >= ? "
                      "  AND start_time < ? "
                      "GROUP BY task_category";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in GetFocusPercentageByCategory: {}",
                      sqlite3_errmsg(m_Db));
        throw std::runtime_error("db prepare failed");
    }

    sqlite3_bind_double(stmt, 1, from_epoch);
    sqlite3_bind_double(stmt, 2, now_epoch);

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
nlohmann::json SQLite::GetCategoryTimeSummary(int days) {
    sqlite3_stmt *stmt = nullptr;

    if (days < 1) {
        days = 1;
    }

    const double from_epoch = GetLocalDayStartEpoch(days - 1);
    const double now_epoch = std::chrono::duration<double>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

    const char *sql =
        "SELECT COALESCE(NULLIF(task_category, ''), 'uncategorized') AS category, "
        "SUM(duration) AS total_seconds "
        "FROM focus_log "
        "WHERE state IN (1, 2) "
        "  AND start_time >= ? "
        "  AND start_time < ? "
        "GROUP BY category "
        "ORDER BY total_seconds DESC";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in GetCategoryTimeSummary: {}", sqlite3_errmsg(m_Db));
        return nlohmann::json::array();
    }

    sqlite3_bind_double(stmt, 1, from_epoch);
    sqlite3_bind_double(stmt, 2, now_epoch);

    nlohmann::json rows = nlohmann::json::array();

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *category_txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        double total_seconds = sqlite3_column_double(stmt, 1);

        rows.push_back({{"category", category_txt ? category_txt : "uncategorized"},
                        {"total_seconds", total_seconds}});
    }

    sqlite3_finalize(stmt);
    return rows;
}

// ─────────────────────────────────────
nlohmann::json SQLite::GetCategoryFocusSplit(int days) {
    sqlite3_stmt *stmt = nullptr;

    if (days < 1) {
        days = 1;
    }

    const double from_epoch = GetLocalDayStartEpoch(days - 1);
    const double now_epoch = std::chrono::duration<double>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

    const char *sql =
        "SELECT COALESCE(NULLIF(task_category, ''), 'uncategorized') AS category, "
        "state, SUM(duration) AS total_seconds "
        "FROM focus_log "
        "WHERE state IN (1, 2) "
        "  AND start_time >= ? "
        "  AND start_time < ? "
        "GROUP BY category, state";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in GetCategoryFocusSplit: {}", sqlite3_errmsg(m_Db));
        return nlohmann::json::array();
    }

    sqlite3_bind_double(stmt, 1, from_epoch);
    sqlite3_bind_double(stmt, 2, now_epoch);

    struct FocusSplit {
        double focused = 0.0;
        double unfocused = 0.0;
    };

    std::unordered_map<std::string, FocusSplit> splits;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *category_txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        const std::string category = category_txt ? category_txt : "uncategorized";
        int state = sqlite3_column_int(stmt, 1);
        double total_seconds = sqlite3_column_double(stmt, 2);

        auto &entry = splits[category];
        if (state == 1) {
            entry.focused += total_seconds;
        } else if (state == 2) {
            entry.unfocused += total_seconds;
        }
    }

    sqlite3_finalize(stmt);

    std::vector<std::pair<std::string, FocusSplit>> ordered(splits.begin(), splits.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
        const double totalA = a.second.focused + a.second.unfocused;
        const double totalB = b.second.focused + b.second.unfocused;
        return totalA > totalB;
    });

    nlohmann::json rows = nlohmann::json::array();
    for (const auto &entry : ordered) {
        const double total = entry.second.focused + entry.second.unfocused;
        const double focused_pct = total > 0.0 ? (entry.second.focused / total) * 100.0 : 0.0;
        const double unfocused_pct = total > 0.0 ? (entry.second.unfocused / total) * 100.0 : 0.0;

        rows.push_back({{"category", entry.first},
                        {"focused_seconds", entry.second.focused},
                        {"unfocused_seconds", entry.second.unfocused},
                        {"focused_pct", focused_pct},
                        {"unfocused_pct", unfocused_pct}});
    }

    return rows;
}

// ─────────────────────────────────────
nlohmann::json SQLite::FetchDailyAppUsageByAppId(int days) {
    sqlite3_stmt *stmt = nullptr;

    if (days < 1) {
        days = 1;
    }

    const double from_epoch = GetLocalDayStartEpoch(days - 1);
    const double now_epoch = std::chrono::duration<double>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

    const char *sql = "SELECT "
                      "  strftime('%Y-%m-%d', start_time, 'unixepoch', 'localtime') AS day, "
                      "  app_id, "
                      "  COALESCE(title, '') AS title, "
                      "  SUM(duration) AS total_seconds "
                      "FROM focus_log "
                      "WHERE start_time >= ? "
                      "  AND start_time < ? "
                      "  AND app_id IS NOT NULL "
                      "  AND app_id <> '' "
                      "GROUP BY day, app_id, title "
                      "ORDER BY day ASC";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("prepare failed in FetchDailyAppUsageByAppId: {}", sqlite3_errmsg(m_Db));
        return {};
    }

    sqlite3_bind_double(stmt, 1, from_epoch);
    sqlite3_bind_double(stmt, 2, now_epoch);

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
static nlohmann::json DefaultPomodoroState() {
    const int focus = 25 * 60;
    const int shortBreak = 5 * 60;
    const int longBreak = 20 * 60;
    return {
        {"phase", "focus-1"},
        {"cycle_step", 0},
        {"is_running", false},
        {"is_paused", false},
        {"time_left", focus},
        {"focus_duration", focus},
        {"short_break_duration", shortBreak},
        {"long_break_duration", longBreak},
        {"auto_start_breaks", true},
        {"updated_at", static_cast<double>(std::time(nullptr))},
    };
}

// ─────────────────────────────────────
nlohmann::json SQLite::GetPomodoroState() {
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT phase, cycle_step, is_running, is_paused, time_left, focus_duration, "
        "short_break_duration, long_break_duration, auto_start_breaks, updated_at "
        "FROM pomodoro_state WHERE id = 1";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in GetPomodoroState: {}", sqlite3_errmsg(m_Db));
        return DefaultPomodoroState();
    }

    nlohmann::json state = DefaultPomodoroState();
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *phaseTxt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        const int cycleStep = sqlite3_column_int(stmt, 1);
        const int isRunning = sqlite3_column_int(stmt, 2);
        const int isPaused = sqlite3_column_int(stmt, 3);
        const int timeLeft = sqlite3_column_int(stmt, 4);
        const int focusDuration = sqlite3_column_int(stmt, 5);
        const int shortBreakDuration = sqlite3_column_int(stmt, 6);
        const int longBreakDuration = sqlite3_column_int(stmt, 7);
        const int autoStartBreaks = sqlite3_column_int(stmt, 8);
        const double updatedAt = sqlite3_column_double(stmt, 9);

        state["phase"] = phaseTxt ? phaseTxt : "focus-1";
        state["cycle_step"] = cycleStep;
        state["is_running"] = (isRunning != 0);
        state["is_paused"] = (isPaused != 0);
        state["time_left"] = timeLeft;
        state["focus_duration"] = focusDuration;
        state["short_break_duration"] = shortBreakDuration;
        state["long_break_duration"] = longBreakDuration;
        state["auto_start_breaks"] = (autoStartBreaks != 0);
        state["updated_at"] = updatedAt;
    }

    sqlite3_finalize(stmt);
    return state;
}

// ─────────────────────────────────────
bool SQLite::SavePomodoroState(const nlohmann::json &state, std::string &error) {
    error.clear();
    sqlite3_stmt *stmt = nullptr;

    const char *sql =
        "INSERT INTO pomodoro_state (id, phase, cycle_step, is_running, is_paused, time_left, "
        "focus_duration, short_break_duration, long_break_duration, auto_start_breaks, updated_at) "
        "VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "phase=excluded.phase, "
        "cycle_step=excluded.cycle_step, "
        "is_running=excluded.is_running, "
        "is_paused=excluded.is_paused, "
        "time_left=excluded.time_left, "
        "focus_duration=excluded.focus_duration, "
        "short_break_duration=excluded.short_break_duration, "
        "long_break_duration=excluded.long_break_duration, "
        "auto_start_breaks=excluded.auto_start_breaks, "
        "updated_at=excluded.updated_at";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = std::string("db prepare failed: ") + sqlite3_errmsg(m_Db);
        spdlog::error("db prepare failed in SavePomodoroState: {}", sqlite3_errmsg(m_Db));
        return false;
    }

    const std::string phase = state.value("phase", std::string("focus-1"));
    const int cycleStep = state.value("cycle_step", 0);
    const int isRunning = state.value("is_running", false) ? 1 : 0;
    const int isPaused = state.value("is_paused", false) ? 1 : 0;
    const int timeLeft = state.value("time_left", 25 * 60);
    const int focusDuration = state.value("focus_duration", 25 * 60);
    const int shortBreakDuration = state.value("short_break_duration", 5 * 60);
    const int longBreakDuration = state.value("long_break_duration", 20 * 60);
    const int autoStartBreaks = state.value("auto_start_breaks", true) ? 1 : 0;
    const double updatedAt = state.value("updated_at", static_cast<double>(std::time(nullptr)));

    sqlite3_bind_text(stmt, 1, phase.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, cycleStep);
    sqlite3_bind_int(stmt, 3, isRunning);
    sqlite3_bind_int(stmt, 4, isPaused);
    sqlite3_bind_int(stmt, 5, timeLeft);
    sqlite3_bind_int(stmt, 6, focusDuration);
    sqlite3_bind_int(stmt, 7, shortBreakDuration);
    sqlite3_bind_int(stmt, 8, longBreakDuration);
    sqlite3_bind_int(stmt, 9, autoStartBreaks);
    sqlite3_bind_double(stmt, 10, updatedAt);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        error = std::string("db write failed: ") + sqlite3_errmsg(m_Db);
        spdlog::error("SavePomodoroState failed: {}", sqlite3_errmsg(m_Db));
        return false;
    }

    return true;
}

// ─────────────────────────────────────
nlohmann::json SQLite::GetPomodoroTodayStats() {
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT focus_sessions, focus_seconds, updated_at "
        "FROM pomodoro_daily WHERE day = date('now', 'localtime')";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in GetPomodoroTodayStats: {}", sqlite3_errmsg(m_Db));
        return {{"day", ""}, {"focus_sessions", 0}, {"focus_seconds", 0}};
    }

    nlohmann::json out = {{"day", ""}, {"focus_sessions", 0}, {"focus_seconds", 0}};
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out["day"] = "today";
        out["focus_sessions"] = sqlite3_column_int(stmt, 0);
        out["focus_seconds"] = sqlite3_column_int(stmt, 1);
        out["updated_at"] = sqlite3_column_double(stmt, 2);
    } else {
        out["day"] = "today";
        out["updated_at"] = static_cast<double>(std::time(nullptr));
    }

    sqlite3_finalize(stmt);
    return out;
}

// ─────────────────────────────────────
bool SQLite::IncrementPomodoroFocusToday(int focusSeconds, std::string &error) {
    error.clear();
    if (focusSeconds < 0) {
        focusSeconds = 0;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO pomodoro_daily (day, focus_sessions, focus_seconds, updated_at) "
        "VALUES (date('now', 'localtime'), 1, ?, strftime('%s','now')) "
        "ON CONFLICT(day) DO UPDATE SET "
        "focus_sessions = focus_sessions + 1, "
        "focus_seconds = focus_seconds + excluded.focus_seconds, "
        "updated_at = excluded.updated_at";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = std::string("db prepare failed: ") + sqlite3_errmsg(m_Db);
        spdlog::error("db prepare failed in IncrementPomodoroFocusToday: {}", sqlite3_errmsg(m_Db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, focusSeconds);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        error = std::string("db write failed: ") + sqlite3_errmsg(m_Db);
        spdlog::error("IncrementPomodoroFocusToday failed: {}", sqlite3_errmsg(m_Db));
        return false;
    }

    return true;
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
nlohmann::json SQLite::FetchEvents(int days, int limit) {
    sqlite3_stmt *stmt = nullptr;

    if (days < 1) {
        days = 1;
    }
    if (limit < 1) {
        limit = 1;
    }
    if (limit > 20000) {
        limit = 20000;
    }

    const double from_epoch = GetLocalDayStartEpoch(days - 1);
    const double now_epoch = std::chrono::duration<double>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

    const char *sql = "SELECT app_id, title, task_category, state, duration "
                      "FROM focus_log "
                      "WHERE start_time >= ? "
                      "  AND start_time < ? "
                      "ORDER BY start_time "
                      "LIMIT ?";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in FetchLast7Days: {}", sqlite3_errmsg(m_Db));
        return {};
    }

    sqlite3_bind_double(stmt, 1, from_epoch);
    sqlite3_bind_double(stmt, 2, now_epoch);
    sqlite3_bind_int(stmt, 3, limit);

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
nlohmann::json SQLite::FetchHistory(int limit) {
    sqlite3_stmt *stmt = nullptr;

    if (limit < 1) {
        limit = 1;
    }
    if (limit > 10000) {
        limit = 10000;
    }

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
        FROM focus_log
        GROUP BY app_id, title
        ORDER BY total_duration DESC
        LIMIT ?
    )";

    if (sqlite3_prepare_v2(m_Db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("db prepare failed in FetchHistory: {}", sqlite3_errmsg(m_Db));
        return nlohmann::json::array();
    }

    sqlite3_bind_int(stmt, 1, limit);

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

