#include <string>
#include <filesystem>
#include <iostream>
#include <memory>

// parts
#include "server.hpp"
#include "anytype.hpp"
#include "window.hpp"
#include "secrets.hpp"

class FocusService {
  public:
    FocusService(const unsigned port, const unsigned ping);
    ~FocusService();

  private:
    std::filesystem::path GetRootOfBinary();

    // Anytype
    nlohmann::json GetTasks();

  private:
    const unsigned m_Port;
    const unsigned m_Ping;
    std::filesystem::path m_Root;

    // Parts
    std::unique_ptr<Server> m_Server;
    std::unique_ptr<Anytype> m_Anytype;
    std::unique_ptr<Window> m_Window;
    std::unique_ptr<Secrets> m_Secrets;
};
