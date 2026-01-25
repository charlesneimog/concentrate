#include <string>
#include <filesystem>
#include <iostream>

#include <nlohmann/json.hpp>

class Anytype {
  public:
    Anytype();
    ~Anytype();

  private:
    nlohmann::json GetTasks(const std::string api_key, const std::string space_id);
    nlohmann::json GetAnytypeObjects(const nlohmann::json &payload);
    int GetExtractLength(const nlohmann::json &payload);
    nlohmann::json NormalizeTask(const nlohmann::json &obj, int fallback_id);
    nlohmann::json PropertyByKey(const nlohmann::json &properties, const std::string &key);
    nlohmann::json ExtractArray(const nlohmann::json &prop);
    std::string GetString(const nlohmann::json &j, const std::string &key,
                          const std::string &fallback);
};
