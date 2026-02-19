#pragma once

#include <sqlite3.h>
#include <nlohmann/json.hpp>

#include <string>
#include <array>

#include "json.hpp"
#include "common.hpp"

class SQLite {
  public:
    SQLite(const std::string &db_path);
    ~SQLite();

    void InsertEventNew(const std::string &appId, const std::string &title,
                        const std::string &taskCategory, double start_time, double end_time,
                        double duration, int state);
    bool UpdateEventNew(const std::string &appId, const std::string &title,
                        const std::string &taskCategory, double end_time, double duration,
                        int state);

    void InsertMonitoringSession(double start_time, double end_time, double duration, int state);
    bool UpdateMonitoringSession(double end_time, double duration, int state);
    nlohmann::json GetTodayMonitoringTimeSummary();

    void InsertHydrationResponse(const std::string &answer, double prompted_at,
                   double answered_at);
    nlohmann::json GetHydrationSummaryLast24h();

    bool CreateTask(const nlohmann::json &data, std::string &error);
    bool UpdateTask(const nlohmann::json &data, std::string &error);

    // nlohmann::json FetchTodayFocusSummary();
    nlohmann::json FetchTodayCategorySummary();

    // What I did
    nlohmann::json GetFocusSummary(int days);
    nlohmann::json GetTodayFocusTimeSummary();
    nlohmann::json GetTodayDailyActivitiesSummary();

    // Recurring Tasks
    void AddRecurringTask(const std::string &name, const std::vector<std::string> &appIds,
                          const std::vector<std::string> &appTitles, const std::string &icon = "",
                          const std::string &color = "");
    void UpdateRecurringTask(const std::string &name, const std::vector<std::string> &appIds,
                             const std::vector<std::string> &appTitles,
                             const std::string &icon = "", const std::string &color = "");
    void ExcludeRecurringTask(const std::string &name);
    nlohmann::json FetchRecurringTasks();

    // Anytype
    nlohmann::json FetchEvents(int days = 7, int limit = 2000);
    nlohmann::json FetchTasks();
    nlohmann::json FetchCategories();
    nlohmann::json FetchHistory(int limit = 500);

    // History
    nlohmann::json GetFocusPercentageByCategory(int days);
    nlohmann::json FetchDailyAppUsageByAppId(int days);
    nlohmann::json GetCategoryTimeSummary(int days);
    nlohmann::json GetCategoryFocusSplit(int days);

    // Pomodoro
    nlohmann::json GetPomodoroState();
    bool SavePomodoroState(const nlohmann::json &state, std::string &error);
    nlohmann::json GetPomodoroTodayStats();
    bool IncrementPomodoroFocusToday(int focusSeconds, std::string &error);

  private:
    // Returns Unix epoch seconds (UTC) for local midnight N days ago.
    // days = 0 -> today at 00:00 local time
    // days = 1 -> yesterday at 00:00 local time
    double GetLocalDayStartEpoch(int days);

    void Init();
    void PrepareStatements();
    void UpsertCategory(const std::string &category, const nlohmann::json &allowedAppIds,
                        const nlohmann::json &allowedTitles);
    void ExecIgnoringErrors(const std::string &sql);

  private:
    sqlite3 *m_Db;
    std::string m_DbPath;
    JsonParse m_JsonParse;

    sqlite3_stmt *m_InsertEventStmt = nullptr;
    sqlite3_stmt *m_UpdateEventStmt = nullptr;

    sqlite3_stmt *m_InsertMonitoringStmt = nullptr;
    sqlite3_stmt *m_UpdateMonitoringStmt = nullptr;

    // Small deterministic lookaside buffer to reduce heap churn.
    static constexpr int kLookasideSlotSize = 128;
    static constexpr int kLookasideSlotCount = 256; // 32 KiB
    std::array<unsigned char, kLookasideSlotSize * kLookasideSlotCount> m_Lookaside{};
};
