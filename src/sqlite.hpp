#pragma once

#include <sqlite3.h>
#include <nlohmann/json.hpp>

#include <string>

#include "json.hpp"

class SQLite {
  public:
    SQLite(const std::string &db_path);
    ~SQLite();

    void InsertEvent(const std::string &appId, int windowId, const std::string &title,
                     double start_time, double end_time, double duration);

    bool CreateTask(const nlohmann::json &data, std::string &error);
    bool UpdateTask(const nlohmann::json &data, std::string &error);

    bool UpsertActivityCategory(const std::string &appId, const std::string &title,
                                const std::string &category, std::string &error);
    void InsertFocusState(int state, double start, double end, double duration);
    nlohmann::json FetchTodayFocusSummary();

    // Daily Tasks
    nlohmann::json FetchRecurringTasks();
    bool UpsertRecurringTask(const nlohmann::json &data, std::string &error);
    nlohmann::json FetchTodayCategorySummary();
    bool ExcludeRecurringTask(const std::string &name, std::string &error);

    // Anytype
    nlohmann::json FetchEvents();
    nlohmann::json FetchTasks();
    nlohmann::json FetchCategories();
    nlohmann::json FetchHistory();

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
