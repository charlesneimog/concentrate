#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class HyprlandIPC {
  public:
    HyprlandIPC();
    ~HyprlandIPC();

    HyprlandIPC(const HyprlandIPC &) = delete;
    HyprlandIPC &operator=(const HyprlandIPC &) = delete;

    bool IsAvailable() const;

    // Socket1 JSON request (hyprctl-like). Example: "activewindow".
    std::optional<nlohmann::json> SendJsonRequest(
        const std::string &rq,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) const;

    // Convenience for concentrate: returns {class,title} of the currently active window.
    // Uses `activewindow` when available, with a fallback to `activeworkspace + clients`.
    std::optional<std::pair<std::string, std::string>> GetActiveClassAndTitle(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) const;

    // Socket2 event stream. Callback receives the full raw event line.
    bool StartEventStream(std::function<void(const std::string &event)> callback,
                          std::vector<std::string> only_events,
                          std::chrono::milliseconds reconnect_delay =
                              std::chrono::milliseconds(1000));
    void StopEventStream();
    bool IsEventStreamRunning() const;

  private:
    static std::string GetEnvInstanceSignature();
    static std::filesystem::path GetSocketFolderForInstance(const std::string &instanceSig);

    bool ConnectStreamFd(int &fd);
    static void CloseFd(int &fd);

    static bool SendAll(int fd, const void *data, std::size_t size);
    bool ReadLine(int fd, std::string &out_line, std::string &buffer,
                  std::chrono::milliseconds timeout);

    static std::string EventNameFromLine(const std::string &line);

  private:
    std::string m_InstanceSig;
    std::filesystem::path m_SocketFolder;

    std::atomic<bool> m_StreamRunning{false};
    std::atomic<bool> m_StopStream{false};
    std::thread m_StreamThread;
    int m_StreamFd = -1;
};
