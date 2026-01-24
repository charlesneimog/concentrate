#include <cstdio>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "window_focus.h"

static FocusedWindow parse_json_response(const std::string& json_response) {
    FocusedWindow fw{};
    try {
        auto root = nlohmann::json::parse(json_response);
        nlohmann::json obj;
        if (root.contains("Ok") && root["Ok"].is_object() && root["Ok"].contains("FocusedWindow")) {
            const auto &fw_node = root["Ok"]["FocusedWindow"];
            if (fw_node.is_null() || !fw_node.is_object()) {
                return fw;
            }
            obj = fw_node;
        } else if (root.is_object() && root.contains("id")) {
            obj = root;
        } else {
            return fw;
        }
        if (obj.contains("id") && obj["id"].is_number_integer()) {
            fw.window_id = obj["id"].get<int>();
        }

        if (obj.contains("title") && obj["title"].is_string()) {
            fw.title = obj["title"].get<std::string>();
        }

        if (obj.contains("app_id") && obj["app_id"].is_string()) {
            fw.app_id = obj["app_id"].get<std::string>();
        }

        bool is_focused = false;
        if (obj.contains("is_focused") && obj["is_focused"].is_boolean()) {
            is_focused = obj["is_focused"].get<bool>();
        }

        if (fw.window_id != -1 && is_focused) {
            fw.valid = true;
        }
    } catch (const std::exception &) {
        return fw;
    }

    return fw;
}

FocusedWindow get_niri_focused_window() {
    FocusedWindow fw{};

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen("niri msg -j focused-window", "r"), pclose);
    if (!pipe) {
        return fw;
    }

    std::string response;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe.get())) {
        response += buffer;
    }

    if (!response.empty() && response.back() == '\n') {
        response.pop_back();
    }

    if (response.empty()) {
        return fw;
    }

    fw = parse_json_response(response);
    return fw;
}
