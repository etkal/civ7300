/*
 * Wifi connection state machine (station mode only, no server connectivity).
 *
 * (c) 2026 Erik Tkal
 *
 */

#include "wifi_connection.h"

#if defined(PLATFORM_PICO_W)

#include <algorithm>

#include "pico/cyw43_arch.h"

#include "timemgr.h"

namespace
{
    constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
    constexpr uint32_t RETRY_DELAY_INITIAL_MS = 2000;
    constexpr uint32_t RETRY_DELAY_STEP_MS = 2000;
    constexpr uint32_t RETRY_DELAY_MAX_MS = 10000;
} // namespace

WifiConnection::WifiConnection(std::string ssid, std::string password)
    : m_ssid(std::move(ssid)),
      m_password(std::move(password))
{
}

void WifiConnection::Initialize()
{
    m_state = WifiState::Disconnected;
    m_retryDelayMs = RETRY_DELAY_INITIAL_MS;
    m_nextConnectAttempt = get_absolute_time();
}

void WifiConnection::SetMessageCallback(MessageCallback callback)
{
    m_messageCallback = std::move(callback);
}

void WifiConnection::setState(WifiState state)
{
    if (state != m_state)
    {
        LogInfo("Wifi state " + StateToString(m_state) + " -> " + StateToString(state));
        m_state = state;
    }
}

void WifiConnection::scheduleRetry()
{
    m_nextConnectAttempt = make_timeout_time_ms(m_retryDelayMs);
    m_retryDelayMs = std::min(m_retryDelayMs + RETRY_DELAY_STEP_MS, RETRY_DELAY_MAX_MS);
}

void WifiConnection::DoWork()
{
    switch (m_state)
    {
    case WifiState::Disconnected:
        if (absolute_time_diff_us(get_absolute_time(), m_nextConnectAttempt) > 0)
        {
            return;
        }
        LogInfo("Wifi disconnected, initiating connection to " + m_ssid);
        if (m_messageCallback)
        {
            m_messageCallback("Initiating wifi connect");
        }
        cyw43_arch_enable_sta_mode();
        cyw43_wifi_pm(&cyw43_state, CYW43_PERFORMANCE_PM & ~0xf);
        cyw43_arch_wifi_connect_async(m_ssid.c_str(), m_password.c_str(), CYW43_AUTH_WPA2_AES_PSK);
        m_connectTimeout = make_timeout_time_ms(CONNECT_TIMEOUT_MS);
        setState(WifiState::Connecting);
        return;

    case WifiState::Connecting:
    {
        int linkStatus = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (linkStatus == CYW43_LINK_NONET)
        {
            // No matching AP found on that scan pass; reissue the join without resetting the timeout/backoff.
            cyw43_arch_wifi_connect_async(m_ssid.c_str(), m_password.c_str(), CYW43_AUTH_WPA2_AES_PSK);
            linkStatus = CYW43_LINK_JOIN;
        }
        if (CYW43_LINK_UP == linkStatus)
        {
            LogInfo("Wifi connected successfully");
            if (m_messageCallback)
            {
                m_messageCallback("Wifi connected");
            }
            m_retryDelayMs = RETRY_DELAY_INITIAL_MS;
            setState(WifiState::Connected);
            return;
        }
        if (linkStatus < 0)
        {
            LogInfo("Wifi connection failed");
            if (m_messageCallback)
            {
                m_messageCallback("Wifi connection failed");
            }
            cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
            scheduleRetry();
            setState(WifiState::Disconnected);
            return;
        }
        if (absolute_time_diff_us(get_absolute_time(), m_connectTimeout) < 0)
        {
            LogInfo("Wifi connection timed out; retrying");
            cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
            scheduleRetry();
            setState(WifiState::Disconnected);
        }
        return;
    }

    case WifiState::Connected:
        if (CYW43_LINK_UP != cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA))
        {
            LogInfo("Wifi link lost");
            if (m_messageCallback)
            {
                m_messageCallback("Wifi link lost");
            }
            scheduleRetry();
            setState(WifiState::Disconnected);
        }
        return;

    case WifiState::Error:
    default:
        scheduleRetry();
        setState(WifiState::Disconnected);
        return;
    }
}

std::string WifiConnection::StateToString(WifiState state)
{
    switch (state)
    {
    case WifiState::Disconnected:
        return "DISCONNECTED";
    case WifiState::Connecting:
        return "CONNECTING";
    case WifiState::Connected:
        return "CONNECTED";
    case WifiState::Error:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

#endif // defined(PLATFORM_PICO_W)
