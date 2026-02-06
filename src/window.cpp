#include "window.hpp"

#include <spdlog/spdlog.h>

// ─────────────────────────────────────
Window::Window() {
    m_WM = NIRI;

    if (!m_Niri.IsAvailable()) {
        spdlog::warn("NIRI_SOCKET is not set; window focus tracking will fall back to idle/polling");
        return;
    }

    spdlog::info("Window manager detected: NIRI");
}

// ─────────────────────────────────────
Window::~Window() {
    StopEventStream();
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
bool Window::StartEventStream(const std::function<void()> &on_relevant_event) {
    if (m_WM != NIRI) {
        return false;
    }
    if (!m_Niri.IsAvailable()) {
        return false;
    }

    // Subscribe only to the events we care about.
    const std::vector<std::string> only = {
        "WindowFocusChanged",
        "WindowOpenedOrChanged",
        "WindowClosed",
        "WorkspaceActivated",
    };

    return m_Niri.StartEventStream(
        [on_relevant_event](const nlohmann::json &) {
            if (on_relevant_event) {
                on_relevant_event();
            }
        },
        only);
}

// ─────────────────────────────────────
void Window::StopEventStream() {
    m_Niri.StopEventStream();
}

// ─────────────────────────────────────
bool Window::IsEventStreamRunning() const {
    return m_Niri.IsEventStreamRunning();
}

// ─────────────────────────────────────
bool Window::IsAvailable() const {
    return m_Niri.IsAvailable();
}

// ─────────────────────────────────────
FocusedWindow Window::GetNiriFocusedWindow() {
    FocusedWindow focus;

    const auto root_opt = m_Niri.SendEnumRequest("FocusedWindow");
    if (!root_opt.has_value()) {
        spdlog::debug("No response from niri FocusedWindow IPC");
        return focus;
    }

    const nlohmann::json &root = *root_opt;

    // Expected: { "Ok": { "FocusedWindow": { ... } } }
    if (!root.contains("Ok") || !root["Ok"].is_object() || !root["Ok"].contains("FocusedWindow")) {
        spdlog::debug("Unexpected IPC response format");
        return focus;
    }

    const auto &fw = root["Ok"]["FocusedWindow"];

    // FocusedWindow can be null
    if (fw.is_null()) {
        spdlog::debug("No focused window");
        return focus;
    }

    // ---- Extract fields ----
    if (fw.contains("id") && fw["id"].is_number_integer()) {
        focus.window_id = fw["id"].get<int>();
    }

    if (fw.contains("title") && fw["title"].is_string()) {
        focus.title = fw["title"].get<std::string>();
    }

    if (fw.contains("app_id") && fw["app_id"].is_string()) {
        focus.app_id = fw["app_id"].get<std::string>();
    }

    const bool is_focused =
        fw.contains("is_focused") && fw["is_focused"].is_boolean() && fw["is_focused"].get<bool>();

    focus.valid = (focus.window_id != -1 && is_focused);
    return focus;
}
