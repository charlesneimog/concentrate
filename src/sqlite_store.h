#pragma once

#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

void init_db(const std::string &db_path);
void insert_event(const std::string &db_path,
                  const std::string &app_id,
                  int window_id,
                  const std::string &title,
                  double start_time,
                  double end_time,
                  double duration);

bool create_task(const std::string &db_path, const json &data, std::string &error);
bool update_task(const std::string &db_path, const json &data, std::string &error);
bool upsert_activity_category(const std::string &db_path,
                              const std::string &app_id,
                              const std::string &title,
                              const std::string &category,
                              std::string &error);

json fetch_events(const std::string &db_path);
json fetch_tasks(const std::string &db_path);
json fetch_categories(const std::string &db_path);
json fetch_history(const std::string &db_path);
