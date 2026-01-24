#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using json = nlohmann::json;

json parse_json_or_throw(const std::string &body);
int get_int(const json &j, const std::string &key, int fallback);
double get_double(const json &j, const std::string &key, double fallback);
std::string get_string(const json &j, const std::string &key, const std::string &fallback);
json parse_json_array_or_empty(const std::string &s);
std::vector<std::string> json_array_to_strings(const json &arr);
json merge_unique(const json &a, const json &b);
