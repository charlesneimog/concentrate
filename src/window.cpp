#include "window.hpp"

#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/un.h>

// ─────────────────────────────────────
Window::Window() {
    if (std::getenv("NIRI_SOCKET") != nullptr) {
        m_NiriSocket = std::getenv("NIRI_SOCKET");
        m_WM = NIRI;

        // Socket
        if (m_NiriSockfd >= 0) {
            return;
        }

        m_NiriSockfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_NiriSockfd < 0) {
            spdlog::error("Failed to create socket: {}", strerror(errno));
            return;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;

        // Ensure we don't overflow the path buffer
        if (m_NiriSocket.length() >= sizeof(addr.sun_path)) {
            close(m_NiriSockfd);
            m_NiriSockfd = -1;
            return;
        }

        strncpy(addr.sun_path, m_NiriSocket.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(m_NiriSockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            if (errno != ENOENT && errno != ECONNREFUSED) {
                spdlog::error("Failed to connect to niri socket: {}", strerror(errno));
            }
            close(m_NiriSockfd);
            m_NiriSockfd = -1;
            return;
        }
        spdlog::info("Window manager detected: NIRI");
    } else {
        spdlog::error("Only NIRI is supported");
        return;
    }
}

// ─────────────────────────────────────
Window::~Window() {
    switch (m_WM) {
    case NIRI: {
        if (m_NiriSockfd >= 0) {
            close(m_NiriSockfd);
            m_NiriSockfd = -1;
        }
    default:
        spdlog::error("Only NIRI is supported");
        break;
    }
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
    FocusedWindow focus;

    if (m_NiriSockfd < 0) {
        spdlog::error("Not connected to niri socket");
        return focus;
    }

    // ---- Send IPC request (Serde enum format) ----
    const std::string request_str = "\"FocusedWindow\"\n";

    ssize_t sent = send(m_NiriSockfd, request_str.c_str(), request_str.size(), 0);

    if (sent < 0) {
        spdlog::error("Failed to send request to niri: {}", strerror(errno));
        return focus;
    }

    // ---- Receive IPC response ----
    std::string response;
    char buffer[4096];

    struct timeval tv{};
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(m_NiriSockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t bytes_read;
    while ((bytes_read = recv(m_NiriSockfd, buffer, sizeof(buffer), 0)) > 0) {
        if (bytes_read > 0) {
            response.append(buffer, static_cast<std::size_t>(bytes_read));
        }
        if (response.find('\n') != std::string::npos) {
            break;
        }
    }

    if (response.empty()) {
        spdlog::debug("Empty response from niri IPC");
        return focus;
    }

    spdlog::debug("Raw response: {}", response);

    // ---- Parse JSON ----
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(response);
    } catch (const std::exception &e) {
        spdlog::error("JSON parse error: {}", e.what());
        return focus;
    }

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
