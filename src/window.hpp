#include <string>
#include <filesystem>
#include <iostream>

#include <nlohmann/json.hpp>
#include <systemd/sd-bus.h>

#include "common.hpp"

class Window {
  public:
    Window();
    // ~Window();
    FocusedWindow GetFocusedWindow();
    void setLastActivity(std::chrono::steady_clock::time_point lastActivity);
    enum WM { NIRI, SWAY, HYPRLAND, GNOME, KDE };

  private:
    FocusedWindow GetNiriFocusedWindow();
    WM m_WM;
};
