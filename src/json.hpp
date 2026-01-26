#pragma once

#include <string>
#include <nlohmann/json.hpp>

class JsonParse {
  public:
    int GetInt(const nlohmann::json &j, const std::string &key, int fallback);
    double GetDouble(const nlohmann::json &j, const std::string &key, double fallback);
    std::string GetString(const nlohmann::json &j, const std::string &key,
                          const std::string &fallback);
    std::vector<std::string> JsonArray2String(const nlohmann::json &arr);
    nlohmann::json MergeUnique(const nlohmann::json &a, const nlohmann::json &b);
};
