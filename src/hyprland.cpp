#include "hyprland.hpp"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstring>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static std::filesystem::path ResolveHyprBaseDir() {
    const char *xdgRuntimeDirEnv = std::getenv("XDG_RUNTIME_DIR");
    std::filesystem::path xdgRuntimeDir;
    if (xdgRuntimeDirEnv != nullptr && *xdgRuntimeDirEnv) {
        xdgRuntimeDir = std::filesystem::path(xdgRuntimeDirEnv);
    }

    if (!xdgRuntimeDir.empty() && std::filesystem::exists(xdgRuntimeDir / "hypr")) {
        return xdgRuntimeDir / "hypr";
    }

    // Match waybar behavior.
    spdlog::debug("$XDG_RUNTIME_DIR/hypr does not exist, falling back to /tmp/hypr");
    return std::filesystem::path("/tmp") / "hypr";
}

// ─────────────────────────────────────
static bool Contains(const std::vector<std::string> &v, const std::string &needle) {
    for (const auto &s : v) {
        if (s == needle) {
            return true;
        }
    }
    return false;
}

// ─────────────────────────────────────
HyprlandIPC::HyprlandIPC() : m_InstanceSig(GetEnvInstanceSignature()) {
    if (!m_InstanceSig.empty()) {
        m_SocketFolder = GetSocketFolderForInstance(m_InstanceSig);
    }
}

// ─────────────────────────────────────
HyprlandIPC::~HyprlandIPC() {
    StopEventStream();
    CloseFd(m_StreamFd);
}

// ─────────────────────────────────────
std::string HyprlandIPC::GetEnvInstanceSignature() {
    const char *env = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (env == nullptr) {
        return {};
    }
    return std::string(env);
}

// ─────────────────────────────────────
std::filesystem::path HyprlandIPC::GetSocketFolderForInstance(const std::string &instanceSig) {
    return ResolveHyprBaseDir() / instanceSig;
}

// ─────────────────────────────────────
bool HyprlandIPC::IsAvailable() const {
    if (m_InstanceSig.empty() || m_SocketFolder.empty()) {
        return false;
    }

    // Hyprland exposes both sockets; require at least socket1 for querying.
    std::error_code ec;
    return std::filesystem::exists(m_SocketFolder / ".socket.sock", ec);
}

// ─────────────────────────────────────
void HyprlandIPC::CloseFd(int &fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

// ─────────────────────────────────────
bool HyprlandIPC::ConnectStreamFd(int &fd) {
    if (m_InstanceSig.empty()) {
        return false;
    }

    if (fd >= 0) {
        return true;
    }

    const int new_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (new_fd < 0) {
        spdlog::error("Failed to create Hyprland IPC socket: {}", std::strerror(errno));
        return false;
    }

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    const auto socketPath = (m_SocketFolder / ".socket2.sock").string();
    if (socketPath.size() >= sizeof(addr.sun_path)) {
        spdlog::error("Hyprland socket path too long");
        ::close(new_fd);
        return false;
    }

    std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(new_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        const int err = errno;
        spdlog::debug("Failed to connect to Hyprland event socket: {}", std::strerror(err));
        ::close(new_fd);
        return false;
    }

    fd = new_fd;
    return true;
}

// ─────────────────────────────────────
bool HyprlandIPC::SendAll(int fd, const void *data, std::size_t size) {
    const char *ptr = static_cast<const char *>(data);
    std::size_t remaining = size;

    while (remaining > 0) {
        const ssize_t sent = ::send(fd, ptr, remaining, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (sent == 0) {
            return false;
        }
        ptr += static_cast<std::size_t>(sent);
        remaining -= static_cast<std::size_t>(sent);
    }

    return true;
}

// ─────────────────────────────────────
bool HyprlandIPC::ReadLine(int fd, std::string &out_line, std::string &buffer,
                           std::chrono::milliseconds timeout) {
    out_line.clear();

    const auto find_newline = [&buffer]() -> std::size_t { return buffer.find('\n'); };

    if (auto pos = find_newline(); pos != std::string::npos) {
        out_line = buffer.substr(0, pos);
        buffer.erase(0, pos + 1);
        return true;
    }

    pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    const int rc = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
    if (rc <= 0) {
        return false;
    }

    if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return false;
    }

    char tmp[4096];
    const ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
    if (n <= 0) {
        return false;
    }

    buffer.append(tmp, static_cast<std::size_t>(n));

    if (auto pos = find_newline(); pos != std::string::npos) {
        out_line = buffer.substr(0, pos);
        buffer.erase(0, pos + 1);
        return true;
    }

    return false;
}

// ─────────────────────────────────────
std::string HyprlandIPC::EventNameFromLine(const std::string &line) {
    // Hyprland events look like: "activewindow>>Class,Title" or "workspace>>id".
    // Waybar splits on the first '>' character.
    const auto pos = line.find_first_of('>');
    if (pos == std::string::npos) {
        return {};
    }
    return line.substr(0, pos);
}

// ─────────────────────────────────────
std::optional<nlohmann::json>
HyprlandIPC::SendJsonRequest(const std::string &rq, std::chrono::milliseconds timeout) const {
    if (!IsAvailable()) {
        return std::nullopt;
    }

    const auto socketPath = m_SocketFolder / ".socket.sock";

    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        spdlog::warn("Hyprland IPC: socket() failed: {}", std::strerror(errno));
        return std::nullopt;
    }

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    const std::string socketPathStr = socketPath.string();
    if (socketPathStr.size() >= sizeof(addr.sun_path)) {
        close(fd);
        spdlog::warn("Hyprland IPC: socket path too long");
        return std::nullopt;
    }
    std::strncpy(addr.sun_path, socketPathStr.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        spdlog::debug("Hyprland IPC: connect() failed: {}", std::strerror(errno));
        close(fd);
        return std::nullopt;
    }

    // Request format matches waybar: prefix with "j/" for JSON.
    const std::string request = "j/" + rq;
    if (!SendAll(fd, request.data(), request.size())) {
        close(fd);
        return std::nullopt;
    }

    // Read full response until EOF, with a conservative timeout.
    std::string response;
    std::string buffer;
    buffer.resize(8192);

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            break;
        }
        pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const int prc = ::poll(&pfd, 1, static_cast<int>(remaining.count()));
        if (prc <= 0) {
            break;
        }

        // Note: Hyprland typically closes socket1 after sending the reply.
        // poll() often reports POLLIN|POLLHUP together; still read any available data.
        if ((pfd.revents & (POLLERR | POLLNVAL)) != 0) {
            break;
        }
        if ((pfd.revents & POLLIN) == 0) {
            if ((pfd.revents & POLLHUP) != 0) {
                break;
            }
            continue;
        }

        const ssize_t n = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (n <= 0) {
            break;
        }
        response.append(buffer.data(), static_cast<std::size_t>(n));
    }

    close(fd);

    if (response.empty()) {
        return std::nullopt;
    }

    try {
        return nlohmann::json::parse(response);
    } catch (const std::exception &e) {
        spdlog::debug("Hyprland IPC: failed to parse JSON reply for '{}': {}", rq, e.what());
        return std::nullopt;
    }
}

// ─────────────────────────────────────
std::optional<std::pair<std::string, std::string>>
HyprlandIPC::GetActiveClassAndTitle(std::chrono::milliseconds timeout) const {
    // Prefer the direct activewindow query.
    if (auto j = SendJsonRequest("activewindow", timeout); j.has_value() && j->is_object()) {
        std::string cls;
        std::string title;
        if (j->contains("class") && (*j)["class"].is_string()) {
            cls = (*j)["class"].get<std::string>();
        }
        if (j->contains("title") && (*j)["title"].is_string()) {
            title = (*j)["title"].get<std::string>();
        }
        if (!cls.empty() || !title.empty()) {
            return std::make_pair(cls, title);
        }
    }

    // Fallback similar to Waybar:
    // - activeworkspace -> lastwindow, lastwindowtitle
    // - clients -> find matching address -> class/title
    const auto ws = SendJsonRequest("activeworkspace", timeout);
    if (!ws.has_value() || !ws->is_object()) {
        return std::nullopt;
    }

    std::string lastWindow;
    std::string lastTitle;
    if (ws->contains("lastwindow") && (*ws)["lastwindow"].is_string()) {
        lastWindow = (*ws)["lastwindow"].get<std::string>();
    }
    if (ws->contains("lastwindowtitle") && (*ws)["lastwindowtitle"].is_string()) {
        lastTitle = (*ws)["lastwindowtitle"].get<std::string>();
    }

    if (lastWindow.empty() && lastTitle.empty()) {
        return std::nullopt;
    }

    if (!lastWindow.empty()) {
        const auto clients = SendJsonRequest("clients", timeout);
        if (clients.has_value() && clients->is_array()) {
            for (const auto &c : *clients) {
                if (!c.is_object()) {
                    continue;
                }
                if (!c.contains("address") || !c["address"].is_string()) {
                    continue;
                }

                if (c["address"].get<std::string>() != lastWindow) {
                    continue;
                }

                std::string cls;
                std::string title;
                if (c.contains("class") && c["class"].is_string()) {
                    cls = c["class"].get<std::string>();
                }
                if (c.contains("title") && c["title"].is_string()) {
                    title = c["title"].get<std::string>();
                }
                if (title.empty()) {
                    title = lastTitle;
                }
                return std::make_pair(cls, title);
            }
        }
    }

    // Final fallback: return whatever title we got.
    return std::make_pair(std::string{}, lastTitle);
}

// ─────────────────────────────────────
bool HyprlandIPC::StartEventStream(std::function<void(const std::string &event)> callback,
                                   std::vector<std::string> only_events,
                                   std::chrono::milliseconds reconnect_delay) {
    if (m_StreamRunning.load()) {
        return true;
    }
    if (m_InstanceSig.empty()) {
        return false;
    }

    // Require socket2 to exist for streaming.
    std::error_code ec;
    if (!std::filesystem::exists(m_SocketFolder / ".socket2.sock", ec)) {
        return false;
    }

    m_StopStream.store(false);
    m_StreamRunning.store(true);

    m_StreamThread =
        std::thread([this, callback, only_events = std::move(only_events), reconnect_delay]() {
            while (!m_StopStream.load()) {
                CloseFd(m_StreamFd);

                if (!ConnectStreamFd(m_StreamFd)) {
                    std::this_thread::sleep_for(reconnect_delay);
                    continue;
                }

                std::string buffer;
                std::string line;

                while (!m_StopStream.load()) {
                    if (!ReadLine(m_StreamFd, line, buffer, std::chrono::milliseconds(30000))) {
                        break; // reconnect
                    }

                    if (line.empty()) {
                        continue;
                    }

                    const std::string evName = EventNameFromLine(line);
                    if (!only_events.empty() && !Contains(only_events, evName)) {
                        continue;
                    }

                    try {
                        if (callback) {
                            callback(line);
                        }
                    } catch (...) {
                        spdlog::error("HyprlandIPC callback failed");
                    }
                }

                CloseFd(m_StreamFd);
                if (!m_StopStream.load()) {
                    std::this_thread::sleep_for(reconnect_delay);
                }
            }

            CloseFd(m_StreamFd);
            m_StreamRunning.store(false);
        });

    return true;
}

// ─────────────────────────────────────
void HyprlandIPC::StopEventStream() {
    m_StopStream.store(true);

    if (m_StreamThread.joinable()) {
        m_StreamThread.join();
    }

    CloseFd(m_StreamFd);
    m_StreamRunning.store(false);
}

// ─────────────────────────────────────
bool HyprlandIPC::IsEventStreamRunning() const {
    return m_StreamRunning.load();
}
