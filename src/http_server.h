#pragma once

#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

void start_http_server(const std::string &base_dir,
                       const std::string &db_path,
                       std::mutex &current_mutex,
                       json &current_focus);
