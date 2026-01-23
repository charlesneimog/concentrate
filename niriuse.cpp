#include <iostream>
#include <cstdio>
#include <string>
#include <chrono>
#include <thread>

using steady_clock = std::chrono::steady_clock;

/* Estrutura mínima */
struct FocusedWindow {
    int window_id = -1;
    std::string title;
    std::string app_id;
    bool valid = false;
};

/* Parse apenas a janela focada (baixo uso de RAM) */
FocusedWindow get_focused_window() {
    FILE *pipe = popen("niri msg windows", "r");
    if (!pipe) {
        return {};
    }

    char buf[4096];
    FocusedWindow fw;
    bool focused = false;

    while (fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);

        // Nova janela
        if (line.rfind("Window ID ", 0) == 0) {
            if (focused && fw.window_id != -1) {
                fw.valid = true;
                break;
            }
            focused = (line.find("(focused)") != std::string::npos);
            if (focused) {
                size_t id_start = 10; // after "Window ID "
                size_t id_end = line.find(':', id_start);
                if (id_end != std::string::npos) {
                    fw.window_id = std::stoi(line.substr(id_start, id_end - id_start));
                }
            }
            continue;
        }

        if (!focused) {
            continue;
        }

        // Title
        if (line.find("Title:") != std::string::npos) {
            size_t pos = line.find("Title:") + 6;
            fw.title = line.substr(pos);
            fw.title.erase(0, fw.title.find_first_not_of(" \t\""));
            fw.title.erase(fw.title.find_last_not_of("\"\r\n") + 1);
        }
        // App ID
        else if (line.find("App ID:") != std::string::npos) {
            size_t pos = line.find("App ID:") + 7;
            fw.app_id = line.substr(pos);
            fw.app_id.erase(0, fw.app_id.find_first_not_of(" \t\""));
            fw.app_id.erase(fw.app_id.find_last_not_of("\"\r\n") + 1);
            if (!fw.title.empty()) {
                fw.valid = true;
                break; // já temos tudo
            }
        }
    }

    if (focused && fw.window_id != -1 && !fw.title.empty() && !fw.app_id.empty()) {
        fw.valid = true;
    }

    pclose(pipe);
    return fw;
}

std::string json_escape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
        }
    }
    return out;
}

std::string shell_escape_single_quotes(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\'') {
            out += "'\"'\"'";
        } else {
            out += c;
        }
    }
    return out;
}

void post_async(const std::string &path, const std::string &payload) {
    std::string cmd = "curl -s -m 2 --connect-timeout 1 -X POST http://127.0.0.1:8079" +
                      path +
                      " -H \"Content-Type: application/json\" -d '" +
                      shell_escape_single_quotes(payload) + "'";
    std::thread([cmd]() {
        std::system(cmd.c_str());
    }).detach();
}

int main() {
    int last_window_id = -1;
    std::string last_title;
    std::string last_app_id;

    steady_clock::time_point focus_start_steady;
    std::chrono::system_clock::time_point focus_start_system;

    while (true) {
        FocusedWindow fw = get_focused_window();
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
            std::string current_payload = "{"
                                          "\"window_id\":" +
                                          std::to_string(fw.window_id) +
                                          ","
                                          "\"title\":\"" +
                                          json_escape(fw.title) +
                                          "\"," 
                                          "\"app_id\":\"" +
                                          json_escape(fw.app_id) +
                                          "\"," 
                                          "\"start_time\":" +
                                          std::to_string(start_time) + "}";
            post_async("/current", current_payload);
        } else if (fw.window_id != last_window_id) {
            double elapsed = std::chrono::duration<double>(now_steady - focus_start_steady).count();
            double start_time = std::chrono::duration<double>(focus_start_system.time_since_epoch()).count();
            double end_time = std::chrono::duration<double>(now_system.time_since_epoch()).count();

            std::string payload = "{"
                                  "\"window_id\":" +
                                  std::to_string(last_window_id) +
                                  ","
                                  "\"title\":\"" +
                                  json_escape(last_title) +
                                  "\","
                                  "\"app_id\":\"" +
                                  json_escape(last_app_id) +
                                  "\","
                                  "\"start_time\":" +
                                  std::to_string(start_time) +
                                  ","
                                  "\"end_time\":" +
                                  std::to_string(end_time) +
                                  ","
                                  "\"duration\":" +
                                  std::to_string(elapsed) + "}";

            post_async("/event", payload);

            // troca de foco
            last_window_id = fw.window_id;
            last_title = fw.title;
            last_app_id = fw.app_id;
            focus_start_steady = now_steady;
            focus_start_system = now_system;

            std::string current_payload = "{"
                                          "\"window_id\":" +
                                          std::to_string(fw.window_id) +
                                          ","
                                          "\"title\":\"" +
                                          json_escape(fw.title) +
                                          "\"," 
                                          "\"app_id\":\"" +
                                          json_escape(fw.app_id) +
                                          "\"," 
                                          "\"start_time\":" +
                                          std::to_string(end_time) + "}";
            post_async("/current", current_payload);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
