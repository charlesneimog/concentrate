#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>


class NiriIPC {
  public:
    NiriIPC();
    ~NiriIPC();

    NiriIPC(const NiriIPC &) = delete;
    NiriIPC &operator=(const NiriIPC &) = delete;

    bool IsAvailable() const;

    // One-shot request/response connection.
    bool ConnectQuery();
    void DisconnectQuery();
    bool IsQueryConnected() const;

    // Send a serde-enum style request like "\"FocusedWindow\"\n" and parse one JSON line response.
    std::optional<nlohmann::json> SendEnumRequest(const std::string &enum_name,
                                       std::chrono::milliseconds timeout =
                                           std::chrono::milliseconds(1000));

    // Event stream connection (long-lived).
    bool StartEventStream(std::function<void(const nlohmann::json &event)> callback,
                          std::vector<std::string> only_events,
                          std::chrono::milliseconds reconnect_delay =
                              std::chrono::milliseconds(1000));
    void StopEventStream();
    bool IsEventStreamRunning() const;

  private:
    static std::string GetEnvSocketPath();
    bool ConnectFd(int &fd);
    static void CloseFd(int &fd);
    bool SendAll(int fd, const void *data, std::size_t size);
    bool ReadLine(int fd, std::string &out_line, std::string &buffer,
                  std::chrono::milliseconds timeout);
    static bool HasAnyOfKeys(const nlohmann::json &j, const std::vector<std::string> &keys);

  private:
    std::string m_SocketPath;

    int m_QueryFd = -1;

    std::atomic<bool> m_StreamRunning{false};
    std::atomic<bool> m_StopStream{false};
    std::thread m_StreamThread;
    int m_StreamFd = -1;
};
