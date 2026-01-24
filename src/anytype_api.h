#pragma once

#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

struct AnytypeCache {
    json tasks = json::array();
    double updated_at = 0.0;
    std::string error;
    bool ready = false;
};

bool refresh_anytype_cache(std::string *error_out);
AnytypeCache get_anytype_cache();
