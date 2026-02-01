#pragma once

#include <sqlite3.h>
#include <nlohmann/json.hpp>

#include <string>

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

    bool CreateTask(const nlohmann::json &data, std::string &error);
    bool UpdateTask(const nlohmann::json &data, std::string &error);

    // nlohmann::json FetchTodayFocusSummary();
    nlohmann::json FetchTodayCategorySummary();

    // What I did
    nlohmann::json GetFocusSummary(int days);
    nlohmann::json GetTodayFocusTimeSummary();

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
    nlohmann::json FetchEvents();
    nlohmann::json FetchTasks();
    nlohmann::json FetchCategories();
    nlohmann::json FetchHistory();

    // History
    nlohmann::json GetFocusPercentageByCategory(int days);
    nlohmann::json FetchDailyAppUsageByAppId(int days);

  private:
    void Init();
    void UpsertCategory(const std::string &category, const nlohmann::json &allowedAppIds,
                        const nlohmann::json &allowedTitles);
    void ExecIgnoringErrors(const std::string &sql);

  private:
    sqlite3 *m_Db;
    std::string m_DbPath;
    JsonParse m_JsonParse;
};
