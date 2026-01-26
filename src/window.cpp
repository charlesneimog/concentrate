#include "window.hpp"

Window::Window() {
    if (std::getenv("NIRI_SOCKET") != nullptr) {
        m_WM = NIRI;
    } else {
        std::cerr << "Only NIRI is supported" << std::endl;
    }
}

// ─────────────────────────────────────
FocusedWindow Window::GetFocusedWindow() {
    switch (m_WM) {
    case NIRI:
        return GetNiriFocusedWindow();
    default:
        std::cerr << "Only NIRI is supported" << std::endl;
        break;
    }
    return {};
}

// ─────────────────────────────────────
FocusedWindow Window::GetNiriFocusedWindow() {
    FocusedWindow Focus;

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen("niri msg -j focused-window", "r"), pclose);
    if (!pipe) {
        return Focus;
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
        return Focus;
    }

    auto root = nlohmann::json::parse(response);
    nlohmann::json obj;
    if (root.contains("Ok") && root["Ok"].is_object() && root["Ok"].contains("FocusedWindow")) {
        const auto &fw_node = root["Ok"]["FocusedWindow"];
        if (fw_node.is_null() || !fw_node.is_object()) {
            return Focus;
        }
        obj = fw_node;
    } else if (root.is_object() && root.contains("id")) {
        obj = root;
    } else {
        return Focus;
    }
    if (obj.contains("id") && obj["id"].is_number_integer()) {
        Focus.window_id = obj["id"].get<int>();
    }

    if (obj.contains("title") && obj["title"].is_string()) {
        Focus.title = obj["title"].get<std::string>();
    }

    if (obj.contains("app_id") && obj["app_id"].is_string()) {
        Focus.app_id = obj["app_id"].get<std::string>();
    }

    bool is_focused = false;
    if (obj.contains("is_focused") && obj["is_focused"].is_boolean()) {
        is_focused = obj["is_focused"].get<bool>();
    }

    if (Focus.window_id != -1 && is_focused) {
        Focus.valid = true;
    }

    return Focus;
}
