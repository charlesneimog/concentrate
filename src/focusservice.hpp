#include <filesystem>
#include <memory>
#include <mutex>
#include <fstream>

// Libs
#include <httplib.h>
#include <spdlog/spdlog.h>

// parts
#include "anytype.hpp"
#include "window.hpp"
#include "secrets.hpp"
#include "notification.hpp"
#include "sqlite.hpp"

#include "common.hpp"

#define ONFOCUSWARNINGAFTER 15 // seconds

class FocusService {
  public:
    FocusService(const unsigned port, const unsigned ping);
    ~FocusService();

  private:
    std::filesystem::path GetBinaryPath();
    std::filesystem::path GetDBPath();
    void UpdateAllowedApps();
    bool InitServer();

  private:
    const unsigned m_Port;
    const unsigned m_Ping;
    std::filesystem::path m_Root;
    std::mutex m_GlobalMutex;

    // Parts
    std::unique_ptr<Anytype> m_Anytype;
    std::unique_ptr<Window> m_Window;
    std::unique_ptr<Secrets> m_Secrets;
    std::unique_ptr<Notification> m_Notification;
    std::unique_ptr<SQLite> m_SQLite;

    // Server
    std::thread m_Thread;
    httplib::Server m_Server;
    std::string m_IndexHtml;
    std::string m_AppJs;
    FocusedWindow m_Fw;

    // Current Task
    std::vector<std::string> m_AllowedApps;
    std::vector<std::string> m_AllowedWindowTitles;
    std::string m_TaskTitle;
    bool m_IsFocused;
};
