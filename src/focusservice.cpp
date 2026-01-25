#include "focusservice.hpp"
#include <string>
#include <limits.h>
#include <unistd.h>
#include <thread>
#include <chrono>

#include <httplib.h>

// ─────────────────────────────────────
FocusService::FocusService(const unsigned port, const unsigned ping) : m_Port(port), m_Ping(ping) {
    // Server
    m_Root = GetRootOfBinary();
    if (!std::filesystem::exists(m_Root)) {
        std::cerr << "Root does not exists: " << m_Root << std::endl;
        exit(1);
    }
    m_Server = std::make_unique<Server>(m_Root, m_Port);
    std::cout << "FocusService running on http://localhost:" << std::to_string(m_Port) << std::endl;

    // Anytype
    m_Anytype = std::make_unique<Anytype>();

    // Windows API (get AppID, Title)
    m_Window = std::make_unique<Window>();

    // Secrets
    m_Secrets = std::make_unique<Secrets>();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(m_Ping));
    }
}

// ─────────────────────────────────────
FocusService::~FocusService() {}

// ─────────────────────────────────────
std::filesystem::path FocusService::GetRootOfBinary() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) {
        exit(1);
    }
    buf[len] = '\0';
    return std::filesystem::path(buf);
}
