#pragma once

#include <sqlite3.h>
#include <nlohmann/json.hpp>

#include <string>

#include "json.hpp"

class SQLite {
  public:
    SQLite(const std::string &db_path);
    ~SQLite();

    void InsertEvent(const std::string &app_id, int window_id, const std::string &title,
                     double start_time, double end_time, double duration);

    bool CreateTask(const nlohmann::json &data, std::string &error);
    bool UpdateTask(const nlohmann::json &data, std::string &error);

    bool UpsertActivityCategory(const std::string &app_id, const std::string &title,
                                const std::string &category, std::string &error);

    nlohmann::json FetchEvents();
    nlohmann::json FetchTasks();
    nlohmann::json FetchCategories();
    nlohmann::json FetchHistory();

  private:
    void Init();
    void UpsertCategory(const std::string &category, const nlohmann::json &allowed_app_ids,
                        const nlohmann::json &allowed_titles);
    void ExecIgnoringErrors(const std::string &sql);

  private:
    sqlite3 *m_Db;
    std::string m_DbPath;
    JsonParse m_JsonParse;
};
