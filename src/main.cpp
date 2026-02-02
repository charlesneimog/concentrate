#include "Concentrate.hpp"

#include <iostream>
#include <string>

// ─────────────────────────────────────
int main(int argc, char *argv[]) {

    auto print_usage = [](const char *exe) {
        std::cerr << "Usage: " << exe
                  << " [--port <1-65535>] [--ping <seconds>] [--logdebug|--loginfo|--logoff]\n";
    };

    unsigned ServerPort = 7079;
    unsigned PingEach = 1; // Seconds to request AppID from Window
    LogLevel log_level = LOG_OFF; // default

    auto parse_u32 = [&](const std::string &value, const char *flag, unsigned long min, unsigned long max, unsigned &out) -> bool {
        try {
            if (value.empty()) {
                std::cerr << flag << " requires a value" << std::endl;
                return false;
            }
            size_t idx = 0;
            unsigned long parsed = std::stoul(value, &idx, 10);
            if (idx != value.size()) {
                std::cerr << "Invalid value for " << flag << ": " << value << std::endl;
                return false;
            }
            if (parsed < min || parsed > max) {
                std::cerr << "Out of range value for " << flag << ": " << value
                          << " (expected " << min << ".." << max << ")" << std::endl;
                return false;
            }
            out = static_cast<unsigned>(parsed);
            return true;
        } catch (...) {
            std::cerr << "Invalid value for " << flag << ": " << value << std::endl;
            return false;
        }
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }

        if (arg == "--logdebug") {
            log_level = LOG_DEBUG;
            continue;
        }
        if (arg == "--loginfo") {
            log_level = LOG_INFO;
            continue;
        }
        if (arg == "--logoff") {
            log_level = LOG_OFF;
            continue;
        }

        if (arg == "--port" || arg.rfind("--port=", 0) == 0) {
            std::string value;
            if (arg == "--port") {
                if (i + 1 >= argc) {
                    std::cerr << "--port requires a value" << std::endl;
                    print_usage(argv[0]);
                    return 1;
                }
                value = argv[++i];
            } else {
                value = arg.substr(std::string("--port=").size());
            }

            if (!parse_u32(value, "--port", 1, 65535, ServerPort)) {
                print_usage(argv[0]);
                return 1;
            }
            continue;
        }

        if (arg == "--ping" || arg.rfind("--ping=", 0) == 0) {
            std::string value;
            if (arg == "--ping") {
                if (i + 1 >= argc) {
                    std::cerr << "--ping requires a value" << std::endl;
                    print_usage(argv[0]);
                    return 1;
                }
                value = argv[++i];
            } else {
                value = arg.substr(std::string("--ping=").size());
            }

            if (!parse_u32(value, "--ping", 1, 86400, PingEach)) {
                print_usage(argv[0]);
                return 1;
            }
            continue;
        }

        std::cerr << "Unknown argument: " << arg << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    Concentrate concentrate(ServerPort, PingEach, log_level);
    return 0;
}
