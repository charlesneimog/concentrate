#pragma once

#include <string>

struct FocusedWindow {
    int window_id = -1;
    std::string title;
    std::string app_id;
    bool valid = false;
};

FocusedWindow get_niri_focused_window();
