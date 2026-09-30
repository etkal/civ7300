/*
 * Wifi connection state machine (station mode only, no server connectivity).
 *
 * Copyright (c) 2026 Erik Tkal
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "wifi_connection.h"

#include <algorithm>

#if defined(PLATFORM_PICO_W)
#include "pico/cyw43_arch.h"
#endif

#include "timemgr.h"

namespace
{
    constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
    constexpr uint32_t RETRY_DELAY_INITIAL_MS = 1000;
    constexpr uint32_t RETRY_DELAY_MAX_MS = 120000; // 2 minutes
} // namespace

WifiConnection::WifiConnection()
{
}

#if defined(PLATFORM_PICO_W)
void WifiConnection::Initialize(std::string ssid, std::string password)
{
    m_ssid = std::move(ssid);
    m_password = std::move(password);
    m_retryDelayMs = RETRY_DELAY_INITIAL_MS;
    m_nextConnectAttempt = get_absolute_time();
    m_bInitialized = true;
}

void WifiConnection::Disable()
{
    LogInfo("Wifi connection disable requested");
    m_bDisabling = true;
}

void WifiConnection::Enable()
{
    LogInfo("Wifi connection enable requested");
    if (!m_bInitialized)
    {
        return;
    }
    if (m_state == WifiState::Disabled)
    {
        // Reset the retry delay and schedule the next connection attempt.
        m_retryDelayMs = RETRY_DELAY_INITIAL_MS;
        m_nextConnectAttempt = get_absolute_time();
        setState(WifiState::Disconnected);
    }
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
    m_retryDelayMs = std::min(m_retryDelayMs * 2, RETRY_DELAY_MAX_MS);
}
#endif // defined(PLATFORM_PICO_W)

void WifiConnection::DoWork()
{
    if (!m_bInitialized)
    {
        return;
    }
    if (m_bDisabling)
    {
        LogInfo("Disabling Wifi connection");
        cyw43_arch_disable_sta_mode();
        m_bDisabling = false;
        setState(WifiState::Disabled);
        return;
    }
#if defined(PLATFORM_PICO_W)
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
        scheduleRetry();
        setState(WifiState::Disconnected);
        return;

    case WifiState::Disabled:
    default:
        return; // do nothing
    }
#endif // defined(PLATFORM_PICO_W)
}

std::string WifiConnection::StateToString(WifiState state)
{
    switch (state)
    {
    case WifiState::Disabled:
        return "DISABLED";
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
