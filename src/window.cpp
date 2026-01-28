#include "window.hpp"

#include <spdlog/spdlog.h>

// ─────────────────────────────────────
Window::Window() {
    if (std::getenv("NIRI_SOCKET") != nullptr) {
        m_WM = NIRI;
        spdlog::info("Window manager detected: NIRI");
    } else {
        spdlog::error("Only NIRI is supported");
        return;
    }
}

// ─────────────────────────────────────
FocusedWindow Window::GetFocusedWindow() {
    switch (m_WM) {
    case NIRI:
        return GetNiriFocusedWindow();
    default:
        spdlog::error("Only NIRI is supported");
        break;
    }
    return {};
}

// ─────────────────────────────────────
FocusedWindow Window::GetNiriFocusedWindow() {
    FocusedWindow Focus;

    FILE *pipe = popen("niri msg -j focused-window", "r");
    if (!pipe) {
        spdlog::error("Failed to open pipe for niri msg command");
        return Focus;
    }

    std::string response;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        response += buffer;
    }

    pclose(pipe); // close ASAP after reading

    if (!response.empty() && response.back() == '\n') {
        response.pop_back();
    }

    if (response.empty()) {
        spdlog::debug("No response from niri msg command");
        return Focus;
    }

    spdlog::debug("Niri response: {}", response);

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(response);
    } catch (const std::exception &e) {
        spdlog::error("JSON parse error: {}", e.what());
        return Focus;
    }

    const nlohmann::json *obj = nullptr;

    if (root.contains("Ok") && root["Ok"].is_object() && root["Ok"].contains("FocusedWindow") &&
        root["Ok"]["FocusedWindow"].is_object()) {

        obj = &root["Ok"]["FocusedWindow"];

    } else if (root.is_object() && root.contains("id")) {
        obj = &root;
    } else {
        spdlog::debug("Unexpected JSON structure in niri response");
        return Focus;
    }

    if (obj->contains("id") && (*obj)["id"].is_number_integer()) {
        Focus.window_id = (*obj)["id"].get<int>();
    }

    if (obj->contains("title") && (*obj)["title"].is_string()) {
        Focus.title = (*obj)["title"].get<std::string>();
    }

    if (obj->contains("app_id") && (*obj)["app_id"].is_string()) {
        Focus.app_id = (*obj)["app_id"].get<std::string>();
    }

    const bool is_focused = obj->contains("is_focused") && (*obj)["is_focused"].is_boolean()
                                ? (*obj)["is_focused"].get<bool>()
                                : false;

    Focus.valid = (Focus.window_id != -1 && is_focused);
    return Focus;
}
