#pragma once

#include <string>

enum FocusState { FOCUSED = 1, UNFOCUSED = 2, IDLE = 3 , DISABLE = 4};

enum MonitoringState { MONITORING_ENABLE = 1, MONITORING_DISABLE = 2 };

enum LogLevel { LOG_DEBUG, LOG_INFO, LOG_OFF };

struct FocusedWindow {
    int window_id = -1;
    std::string title;
    std::string app_id;
    bool valid = false;
    std::string category;
};
