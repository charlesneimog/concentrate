#include <string>
#include <filesystem>
#include <iostream>

#include <nlohmann/json.hpp>

#include "common.hpp"

class Window {
  public:
    Window();
    FocusedWindow GetFocusedWindow();
    enum WM { NIRI, SWAY, HYPRLAND, GNOME, KDE };

  private:
    FocusedWindow GetNiriFocusedWindow();
    WM m_WM;
};
