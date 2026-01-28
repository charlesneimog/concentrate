#include "focusservice.hpp"

// ─────────────────────────────────────
int main(int argc, char *argv[]) {

    LogLevel log_level = LOG_OFF; // default
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--logdebug") {
            log_level = LOG_DEBUG;
        } else if (arg == "--loginfo") {
            log_level = LOG_INFO;
        } else if (arg == "--logoff") {
            log_level = LOG_OFF;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return 1;
        }
    }

    unsigned ServerPort = 7079;
    unsigned PingEach = 2; // Seconds to request AppID from Window
    FocusService FocusService(ServerPort, PingEach, log_level);
    return 0;
}
