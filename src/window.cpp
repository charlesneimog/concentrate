#include "window.hpp"

#include <spdlog/spdlog.h>

// ─────────────────────────────────────
Window::Window() {
    if (m_Niri.IsAvailable()) {
        m_WM = NIRI;
        spdlog::info("Window manager detected: NIRI");
        return;
    }

    if (m_Hypr.IsAvailable()) {
        m_WM = HYPRLAND;
        spdlog::info("Window manager detected: HYPRLAND");
        return;
    }

    // Window manager IPC not available at startup; allow fallback behavior.
    m_WM = NIRI;
    spdlog::warn("No supported window manager IPC detected; focus tracking will fall back to idle/polling");
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
    case HYPRLAND:
        return GetHyprlandFocusedWindow();
    default:
        spdlog::error("No supported window manager selected");
        break;
    }
    return {};
}

bool Window::StartEventStream(const std::function<void()> &on_relevant_event) {
    if (m_WM == NIRI) {
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

    if (m_WM == HYPRLAND) {
        if (!m_Hypr.IsAvailable()) {
            return false;
        }

        const std::vector<std::string> only = {
            "activewindow",
            "activewindowv2",
            "openwindow",
            "closewindow",
            "windowtitle",
            "windowtitlev2",
        };

        return m_Hypr.StartEventStream(
            [on_relevant_event](const std::string &) {
                if (on_relevant_event) {
                    on_relevant_event();
                }
            },
            only);
    }

    return false;
}

void Window::StopEventStream() {
    if (m_WM == NIRI) {
        m_Niri.StopEventStream();
    } else if (m_WM == HYPRLAND) {
        m_Hypr.StopEventStream();
    }
}

bool Window::IsEventStreamRunning() const {
    if (m_WM == NIRI) {
        return m_Niri.IsEventStreamRunning();
    }
    if (m_WM == HYPRLAND) {
        return m_Hypr.IsEventStreamRunning();
    }
    return false;
}

bool Window::IsAvailable() const {
    if (m_WM == NIRI) {
        return m_Niri.IsAvailable();
    }
    if (m_WM == HYPRLAND) {
        return m_Hypr.IsAvailable();
    }
    return false;
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

// ─────────────────────────────────────
FocusedWindow Window::GetHyprlandFocusedWindow() {
    FocusedWindow focus;

    const auto ct = m_Hypr.GetActiveClassAndTitle();
    if (!ct.has_value()) {
        spdlog::debug("No response from Hyprland IPC (active window)");
        return focus;
    }

    focus.app_id = ct->first;
    focus.title = ct->second;
    focus.valid = (!focus.app_id.empty() || !focus.title.empty());
    return focus;
}
