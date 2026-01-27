#include <string>
#include <filesystem>
#include <iostream>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "secrets.hpp"

class Anytype {
  public:
    Anytype();
    ~Anytype();

    // Login
    std::string LoginChallengeId();
    std::string CreateApiKey(const std::string &challenge_id, const std::string &code);
    void SetDefaultSpace(std::string space_id);

    void SetSpaceID(std::string &key);
    void GetSpaceID();

    // Get
    nlohmann::json GetSpaces();
    nlohmann::json GetTasks();
    nlohmann::json GetPage(const std::string &id);

  private:
    nlohmann::json GetAnytypeObjects(const nlohmann::json &payload);
    int GetExtractLength(const nlohmann::json &payload);
    nlohmann::json NormalizeTask(const nlohmann::json &obj, int fallback_id);
    nlohmann::json PropertyByKey(const nlohmann::json &properties, const std::string &key);
    nlohmann::json ExtractArray(const nlohmann::json &prop);
    std::string GetString(const nlohmann::json &j, const std::string &key,
                          const std::string &fallback);

    Secrets m_Secrets;
};
