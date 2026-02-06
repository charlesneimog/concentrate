#include "niri.hpp"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstring>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// ─────────────────────────────────────
NiriIPC::NiriIPC() : m_SocketPath(GetEnvSocketPath()) {}


// ─────────────────────────────────────
NiriIPC::~NiriIPC() {
    StopEventStream();
    DisconnectQuery();
}

// ─────────────────────────────────────
std::string NiriIPC::GetEnvSocketPath() {
    const char *env = std::getenv("NIRI_SOCKET");
    if (env == nullptr) {
        return {};
    }
    return std::string(env);
}

// ─────────────────────────────────────
bool NiriIPC::IsAvailable() const {
    return !m_SocketPath.empty();
}

// ─────────────────────────────────────
bool NiriIPC::ConnectFd(int &fd) {
    if (!IsAvailable()) {
        return false;
    }

    if (fd >= 0) {
        return true;
    }

    const int new_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (new_fd < 0) {
        spdlog::error("Failed to create Niri socket: {}", std::strerror(errno));
        return false;
    }

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (m_SocketPath.size() >= sizeof(addr.sun_path)) {
        spdlog::error("NIRI_SOCKET path too long");
        ::close(new_fd);
        return false;
    }

    std::strncpy(addr.sun_path, m_SocketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(new_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        const int err = errno;
        if (err != ENOENT && err != ECONNREFUSED) {
            spdlog::warn("Failed to connect to niri socket: {}", std::strerror(err));
        }
        ::close(new_fd);
        return false;
    }

    fd = new_fd;
    return true;
}

// ─────────────────────────────────────
void NiriIPC::CloseFd(int &fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

// ─────────────────────────────────────
bool NiriIPC::ConnectQuery() {
    return ConnectFd(m_QueryFd);
}

// ─────────────────────────────────────
void NiriIPC::DisconnectQuery() {
    CloseFd(m_QueryFd);
}

// ─────────────────────────────────────
bool NiriIPC::IsQueryConnected() const {
    return m_QueryFd >= 0;
}

// ─────────────────────────────────────
bool NiriIPC::SendAll(int fd, const void *data, std::size_t size) {
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
bool NiriIPC::ReadLine(int fd, std::string &out_line, std::string &buffer,
                      std::chrono::milliseconds timeout) {
    out_line.clear();

    const auto find_newline = [&buffer]() -> std::size_t { return buffer.find('\n'); };

    // Fast path: we already have a full line.
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

    // No full line yet.
    return false;
}

// ─────────────────────────────────────
std::optional<nlohmann::json> NiriIPC::SendEnumRequest(const std::string &enum_name,
                                                     std::chrono::milliseconds timeout) {
    if (!ConnectQuery()) {
        return std::nullopt;
    }

    const std::string request = "\"" + enum_name + "\"\n";
    if (!SendAll(m_QueryFd, request.data(), request.size())) {
        spdlog::warn("Failed to send niri IPC request");
        DisconnectQuery();
        return std::nullopt;
    }

    std::string buffer;
    std::string line;
    if (!ReadLine(m_QueryFd, line, buffer, timeout)) {
        // Either timeout or the connection got closed; keep behavior conservative.
        spdlog::debug("No response from niri IPC (timeout/disconnect)");
        DisconnectQuery();
        return std::nullopt;
    }

    try {
        return nlohmann::json::parse(line);
    } catch (const std::exception &e) {
        spdlog::warn("Failed to parse niri IPC response JSON: {}", e.what());
        return std::nullopt;
    }
}

// ─────────────────────────────────────
bool NiriIPC::HasAnyOfKeys(const nlohmann::json &j, const std::vector<std::string> &keys) {
    if (!j.is_object()) {
        return false;
    }
    for (const auto &k : keys) {
        if (j.contains(k)) {
            return true;
        }
    }
    return false;
}

// ─────────────────────────────────────
bool NiriIPC::StartEventStream(std::function<void(const nlohmann::json &event)> callback,
                              std::vector<std::string> only_events,
                              std::chrono::milliseconds reconnect_delay) {
    if (m_StreamRunning.load()) {
        return true;
    }

    if (!IsAvailable()) {
        return false;
    }

    m_StopStream.store(false);
    m_StreamRunning.store(true);

    m_StreamThread = std::thread([this, callback, only_events = std::move(only_events),
                                  reconnect_delay]() {
        while (!m_StopStream.load()) {
            // Establish a dedicated stream connection.
            CloseFd(m_StreamFd);

            if (!ConnectFd(m_StreamFd)) {
                std::this_thread::sleep_for(reconnect_delay);
                continue;
            }

            const std::string subscribe = "\"EventStream\"\n";
            if (!SendAll(m_StreamFd, subscribe.data(), subscribe.size())) {
                spdlog::debug("Failed to subscribe to Niri EventStream");
                CloseFd(m_StreamFd);
                std::this_thread::sleep_for(reconnect_delay);
                continue;
            }

            std::string buffer;
            std::string line;

            while (!m_StopStream.load()) {
                if (!ReadLine(m_StreamFd, line, buffer, std::chrono::milliseconds(30000))) {
                    // Reconnect.
                    break;
                }

                if (line.empty()) {
                    continue;
                }

                nlohmann::json ev;
                try {
                    ev = nlohmann::json::parse(line);
                } catch (const std::exception &e) {
                    spdlog::debug("Ignoring non-JSON niri stream line: {}", e.what());
                    continue;
                }

                if (!only_events.empty() && !HasAnyOfKeys(ev, only_events)) {
                    continue;
                }

                try {
                    callback(ev);
                } catch (...) {
                    // Never let callbacks kill the stream thread.
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
void NiriIPC::StopEventStream() {
    m_StopStream.store(true);

    if (m_StreamThread.joinable()) {
        m_StreamThread.join();
    }

    CloseFd(m_StreamFd);
    m_StreamRunning.store(false);
}

// ─────────────────────────────────────
bool NiriIPC::IsEventStreamRunning() const {
    return m_StreamRunning.load();
}
