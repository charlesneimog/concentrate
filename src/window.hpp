#include <string>
#include <filesystem>
#include <iostream>

#include <nlohmann/json.hpp>
#include <systemd/sd-bus.h>

#include "common.hpp"
#include "niri.hpp"
#include "hyprland.hpp"

#include <functional>

class Window {
  public:
    Window();
    ~Window();
    FocusedWindow GetFocusedWindow();
    bool StartEventStream(const std::function<void()> &on_relevant_event);
    void StopEventStream();
    bool IsEventStreamRunning() const;
    bool IsAvailable() const;
    void setLastActivity(std::chrono::steady_clock::time_point lastActivity);
    enum WM { NIRI, SWAY, HYPRLAND, GNOME, KDE };

  private:
    FocusedWindow GetNiriFocusedWindow();
    FocusedWindow GetHyprlandFocusedWindow();
    WM m_WM;
    NiriIPC m_Niri;
    HyprlandIPC m_Hypr;
};
