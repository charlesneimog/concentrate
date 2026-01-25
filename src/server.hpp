#include <string>
#include <filesystem>
#include <iostream>

class Server {
  public:
    Server(std::filesystem::path root, const unsigned port);

    // Init Serve
    bool InitServer();

  private:
    std::filesystem::path m_Root;
    const unsigned m_Port;
};
