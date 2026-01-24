#include "common.h"

#include <stdexcept>
#include <strings.h>

// ─────────────────────────────────────
json parse_json_or_throw(const std::string &body) {
    try {
        return json::parse(body);
    } catch (const std::exception &e) {
        throw std::runtime_error(e.what());
    }
}

// ─────────────────────────────────────
int get_int(const json &j, const std::string &key, int fallback) {
    if (!j.contains(key)) return fallback;
    if (j.at(key).is_number_integer()) return j.at(key).get<int>();
    if (j.at(key).is_number()) return static_cast<int>(j.at(key).get<double>());
    return fallback;
}

// ─────────────────────────────────────
double get_double(const json &j, const std::string &key, double fallback) {
    if (!j.contains(key)) return fallback;
    if (j.at(key).is_number()) return j.at(key).get<double>();
    return fallback;
}

// ─────────────────────────────────────
std::string get_string(const json &j, const std::string &key, const std::string &fallback) {
    if (!j.contains(key)) return fallback;
    if (j.at(key).is_string()) return j.at(key).get<std::string>();
    return fallback;
}

// ─────────────────────────────────────
json parse_json_array_or_empty(const std::string &s) {
    try {
        auto v = json::parse(s);
        if (v.is_array()) return v;
    } catch (...) {
    }
    return json::array();
}

// ─────────────────────────────────────
std::vector<std::string> json_array_to_strings(const json &arr) {
    std::vector<std::string> out;
    if (!arr.is_array()) return out;
    for (const auto &v : arr) {
        if (v.is_string()) out.push_back(v.get<std::string>());
    }
    return out;
}

// ─────────────────────────────────────
json merge_unique(const json &a, const json &b) {
    std::vector<std::string> merged;
    auto push_unique = [&](const std::string &s) {
        for (const auto &e : merged) {
            if (strcasecmp(e.c_str(), s.c_str()) == 0) return;
        }
        merged.push_back(s);
    };
    for (const auto &s : json_array_to_strings(a)) push_unique(s);
    for (const auto &s : json_array_to_strings(b)) push_unique(s);
    return json(merged);
}
