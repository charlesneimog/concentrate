#include "anytype_api.h"
#include "http_server.h"
#include "window_focus.h"
#include "secret_store.h"
#include "sqlite_store.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

namespace {
using steady_clock = std::chrono::steady_clock;

// ─────────────────────────────────────
std::string get_exe_dir() {
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return exe.parent_path().string();
    }
    return std::filesystem::current_path().string();
}

// ─────────────────────────────────────
std::string ensure_db_path() {
    // get user home directory
    std::string home_dir;
    const char *home_env = std::getenv("HOME");
    if (home_env && *home_env) {
        home_dir = home_env;
    } else {
        std::cerr << "Error: HOME environment variable not set\n";
        exit(1);
    }
    std::filesystem::path path(home_dir + "/.local/focusservice/data.sqlite");
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return path.string();
}
} // namespace

// ─────────────────────────────────────
int main(int argc, char *argv[]) {
    int i = 1;
    while (i < argc) {
        std::string arg = argv[i];
        if (arg == "--anytype-api-key") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --anytype-api-key requires a value\n";
                return 1;
            }

            std::string api_key = argv[i + 1];
            save_secret_string("anytype_api_key", api_key);
            i += 2;
        } else if (arg == "--anytype-space-id") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --anytype-space-id requires a value\n";
                return 1;
            }

            std::string space_id = argv[i + 1];
            save_secret_string("anytype_space_id", space_id);
            i += 2;
        } else {
            break;
        }
    }

    refresh_anytype_cache(nullptr);

    const std::string db_path = ensure_db_path();
    try {
        init_db(db_path);
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    std::mutex current_mutex;
    json current_focus = json::object();

    const std::string base_dir = get_exe_dir();
    start_http_server(base_dir, db_path, current_mutex, current_focus);

    int last_window_id = -1;
    std::string last_title;
    std::string last_app_id;

    steady_clock::time_point focus_start_steady;
    std::chrono::system_clock::time_point focus_start_system;

    while (true) {
        FocusedWindow fw = get_niri_focused_window();
        if (!fw.valid) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        auto now_steady = steady_clock::now();
        auto now_system = std::chrono::system_clock::now();
        if (last_window_id == -1) {
            last_window_id = fw.window_id;
            last_title = fw.title;
            last_app_id = fw.app_id;
            focus_start_steady = now_steady;
            focus_start_system = now_system;
            double start_time = std::chrono::duration<double>(now_system.time_since_epoch()).count();
            json updated = json::object();
            updated["app_id"] = fw.app_id;
            updated["window_id"] = fw.window_id;
            updated["title"] = fw.title;
            updated["start_time"] = start_time;
            updated["updated_at"] = std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            {
                std::lock_guard<std::mutex> lock(current_mutex);
                current_focus = updated;
            }
        } else if (fw.window_id != last_window_id) {
            double elapsed = std::chrono::duration<double>(now_steady - focus_start_steady).count();
            double start_time = std::chrono::duration<double>(focus_start_system.time_since_epoch()).count();
            double end_time = std::chrono::duration<double>(now_system.time_since_epoch()).count();
            insert_event(db_path, last_app_id, last_window_id, last_title, start_time, end_time, elapsed);

            last_window_id = fw.window_id;
            last_title = fw.title;
            last_app_id = fw.app_id;
            focus_start_steady = now_steady;
            focus_start_system = now_system;

            json updated = json::object();
            updated["app_id"] = fw.app_id;
            updated["window_id"] = fw.window_id;
            updated["title"] = fw.title;
            updated["start_time"] = end_time;
            updated["updated_at"] = std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            {
                std::lock_guard<std::mutex> lock(current_mutex);
                current_focus = updated;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
