/*
 * Wifi connection state machine (station mode only, no server connectivity).
 *
 * Copyright (c) 2026 Erik Tkal
 *
 */

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "pico/stdlib.h"

enum class WifiState
{
    Disabled,
    Disconnected,
    Connecting,
    Connected,
    Error,
    Unknown
};

// WifiConnection drives a non-blocking cyw43 station-mode connection state machine.
// Call DoWork() frequently from the main loop; IsConnected() reports link status.
class WifiConnection
{
public:
    typedef std::shared_ptr<WifiConnection> Shared;
    typedef std::function<void(const std::string&)> MessageCallback;

    WifiConnection();
    ~WifiConnection() = default;

    void Initialize(std::string ssid, std::string password);
    void Disable();
    void Enable();
    void DoWork();

    bool IsConnected() const
    {
        return m_state == WifiState::Connected;
    }
    WifiState GetState() const
    {
        return m_state;
    }

    void SetMessageCallback(MessageCallback callback);

    static std::string StateToString(WifiState state);

private:
    void setState(WifiState state);
    void scheduleRetry();

    bool m_bInitialized {false};
    bool m_bDisabling {false};
    std::string m_ssid;
    std::string m_password;
    WifiState m_state {WifiState::Disabled};
    MessageCallback m_messageCallback;

    absolute_time_t m_connectTimeout {};
    absolute_time_t m_nextConnectAttempt {};
    uint32_t m_retryDelayMs {0};
};
