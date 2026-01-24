#pragma once

#include <string>

bool save_secret_string(const std::string &key, const std::string &value);
std::string load_secret_string(const std::string &key);
