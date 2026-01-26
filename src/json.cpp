#include "json.hpp"

// ─────────────────────────────────────
int JsonParse::GetInt(const nlohmann::json &j, const std::string &key, int fallback) {
    if (!j.contains(key)) {
        return fallback;
    }
    if (j.at(key).is_number_integer()) {
        return j.at(key).get<int>();
    }
    if (j.at(key).is_number()) {
        return static_cast<int>(j.at(key).get<double>());
    }
    return fallback;
}

// ─────────────────────────────────────
double JsonParse::GetDouble(const nlohmann::json &j, const std::string &key, double fallback) {
    if (!j.contains(key)) {
        return fallback;
    }
    if (j.at(key).is_number()) {
        return j.at(key).get<double>();
    }
    return fallback;
}

// ─────────────────────────────────────
std::string JsonParse::GetString(const nlohmann::json &j, const std::string &key,
                                 const std::string &fallback) {
    if (!j.contains(key)) {
        return fallback;
    }
    if (j.at(key).is_string()) {
        return j.at(key).get<std::string>();
    }
    return fallback;
}

// ─────────────────────────────────────
std::vector<std::string> JsonParse::JsonArray2String(const nlohmann::json &arr) {
    std::vector<std::string> out;
    if (!arr.is_array()) {
        return out;
    }
    for (const auto &v : arr) {
        if (v.is_string()) {
            out.push_back(v.get<std::string>());
        }
    }
    return out;
}

// ─────────────────────────────────────
nlohmann::json JsonParse::MergeUnique(const nlohmann::json &a, const nlohmann::json &b) {
    std::vector<std::string> merged;
    auto push_unique = [&](const std::string &s) {
        for (const auto &e : merged) {
            if (strcasecmp(e.c_str(), s.c_str()) == 0) {
                return;
            }
        }
        merged.push_back(s);
    };
    for (const auto &s : JsonArray2String(a)) {
        push_unique(s);
    }
    for (const auto &s : JsonArray2String(b)) {
        push_unique(s);
    }
    return nlohmann::json(merged);
}
