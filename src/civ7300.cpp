/*
 * Civ7300
 *
 * (c) 2026 Erik Tkal
 *
 */

#include "civ7300.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "timemgr.h"

namespace
{
    constexpr uint8_t civCmdSetMode = 0x1A;
    constexpr uint8_t civCmdReadFrequency = 0x03;
    constexpr uint8_t civCmdReadMode = 0x04;
    constexpr uint8_t civCmdTransceiveFrequency = 0x00;
    constexpr uint8_t civCmdTransceiveMode = 0x01;
    constexpr uint8_t civCmdReadRadioId = 0x19;
    constexpr uint8_t civSubReadRadioId = 0x00;
    constexpr uint8_t civExpectedRadioId = 0x94;           // IC-7300's CI-V transceiver ID
    constexpr uint32_t civFrequencyCheckIntervalMs = 100;  // Frequency polling interval
    constexpr uint32_t civRadioIdCheckIntervalMs = 1000;   // Minimum spacing between radio ID poll attempts
    constexpr uint32_t civClockSetRetryIntervalMs = 10000; // Retry time on clock set failure
    constexpr uint8_t civScSetModeMisc = 0x05;
    constexpr uint16_t civSubDate = 94;
    constexpr uint16_t civSubTime = 95;
    constexpr uint16_t civSubUtcOffset = 96;
    constexpr uint8_t civResponseOk = 0xFB;
    constexpr uint8_t civResponseNg = 0xFA;
    constexpr uint8_t civBroadcastAddr = 0x00;

    uint8_t toBcd(int value)
    {
        return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
    }

    int fromBcd(uint8_t value)
    {
        return ((value >> 4) * 10) + (value & 0x0F);
    }

    std::string toHexString(const uint8_t* pData, size_t nLen)
    {
        std::ostringstream oss;
        oss << std::hex << std::uppercase << std::setfill('0');
        for (size_t i = 0; i < nLen; ++i)
        {
            if (i > 0)
            {
                oss << ' ';
            }
            oss << std::setw(2) << static_cast<unsigned int>(pData[i]);
        }
        return oss.str();
    }

    std::string civModeName(uint8_t mode)
    {
        switch (mode)
        {
        case 0x00:
            return "LSB";
        case 0x01:
            return "USB";
        case 0x02:
            return "AM";
        case 0x03:
            return "CW";
        case 0x04:
            return "RTTY";
        case 0x05:
            return "FM";
        case 0x06:
            return "WFM";
        case 0x07:
            return "CW-R";
        case 0x08:
            return "RTTY-R";
        case 0x17:
            return "DV";
        default:
            return "unknown (0x" + toHexString(&mode, 1) + ")";
        }
    }

    // Builds the "Sc + sub-sub-command" prefix shared by all Set mode 1A 05 date/time/UTC commands.
    std::vector<uint8_t> setModeDataArea(uint16_t subSubCmd)
    {
        return {civScSetModeMisc, toBcd(static_cast<int>(subSubCmd / 100)), toBcd(static_cast<int>(subSubCmd % 100))};
    }
} // namespace

Civ7300::Civ7300(RadioBridge::Shared spBridge, uint8_t radioAddr, uint8_t controllerAddr, uint32_t timeCheckIntervalMs)
    : m_spBridge(std::move(spBridge)),
      m_radioAddr(radioAddr),
      m_controllerAddr(controllerAddr),
      m_timeCheckIntervalMs(timeCheckIntervalMs)
{
    m_spBridge->SetRadioDataSink([this](const uint8_t* pData, size_t nLen) {
        onRadioData(pData, nLen);
    });
    m_nextTimeCheckTime = make_timeout_time_ms(timeCheckIntervalMs);
    m_nextFrequencyCheckTime = make_timeout_time_ms(civFrequencyCheckIntervalMs);
    m_nextRadioIdCheckTime = get_absolute_time();
}

void Civ7300::GetDate(DateCallback callback)
{
    sendCommand(civCmdSetMode, setModeDataArea(civSubDate), [callback = std::move(callback)](bool bOk, const std::vector<uint8_t>& resp) {
        if (!callback)
        {
            return;
        }
        if (!bOk || resp.size() < 7)
        {
            callback(false, 0, 0, 0);
            return;
        }
        callback(true, fromBcd(resp[3]) * 100 + fromBcd(resp[4]), fromBcd(resp[5]), fromBcd(resp[6]));
    });
}

void Civ7300::SetDate(int year, int month, int day, Completion callback)
{
    std::vector<uint8_t> dataArea = setModeDataArea(civSubDate);
    dataArea.push_back(toBcd(year / 100));
    dataArea.push_back(toBcd(year % 100));
    dataArea.push_back(toBcd(month));
    dataArea.push_back(toBcd(day));
    sendCommand(civCmdSetMode, std::move(dataArea), [callback = std::move(callback)](bool bOk, const std::vector<uint8_t>&) {
        if (callback)
        {
            callback(bOk);
        }
    });
}

void Civ7300::GetTime(TimeCallback callback)
{
    sendCommand(civCmdSetMode, setModeDataArea(civSubTime), [callback = std::move(callback)](bool bOk, const std::vector<uint8_t>& resp) {
        if (!callback)
        {
            return;
        }
        if (!bOk || resp.size() < 5)
        {
            callback(false, 0, 0);
            return;
        }
        callback(true, fromBcd(resp[3]), fromBcd(resp[4]));
    });
}

void Civ7300::SetTime(int hour, int minute, Completion callback)
{
    std::vector<uint8_t> dataArea = setModeDataArea(civSubTime);
    dataArea.push_back(toBcd(hour));
    dataArea.push_back(toBcd(minute));
    sendCommand(civCmdSetMode, std::move(dataArea), [callback = std::move(callback)](bool bOk, const std::vector<uint8_t>&) {
        if (callback)
        {
            callback(bOk);
        }
    });
}

void Civ7300::GetUtcOffset(OffsetCallback callback)
{
    sendCommand(civCmdSetMode,
                setModeDataArea(civSubUtcOffset),
                [callback = std::move(callback)](bool bOk, const std::vector<uint8_t>& resp) {
                    if (!callback)
                    {
                        return;
                    }
                    if (!bOk || resp.size() < 6)
                    {
                        callback(false, 0.0f);
                        return;
                    }
                    const float magnitude = static_cast<float>(fromBcd(resp[3])) + (static_cast<float>(fromBcd(resp[4])) / 60.0f);
                    const bool bNegative = resp[5] == 0x01;
                    callback(true, bNegative ? -magnitude : magnitude);
                });
}

void Civ7300::SetUtcOffset(float offsetHours, Completion callback)
{
    const bool bNegative = offsetHours < 0.0f;
    const float magnitude = std::min(std::fabs(offsetHours), 14.0f); // radio only accepts 00:00-14:00
    const int hour = static_cast<int>(magnitude);
    const int minute = std::lround((magnitude - static_cast<float>(hour)) * 60.0f);

    std::vector<uint8_t> dataArea = setModeDataArea(civSubUtcOffset);
    dataArea.push_back(toBcd(hour));
    dataArea.push_back(toBcd(minute));
    dataArea.push_back(bNegative ? 0x01 : 0x00);
    sendCommand(civCmdSetMode, std::move(dataArea), [callback = std::move(callback)](bool bOk, const std::vector<uint8_t>&) {
        if (callback)
        {
            callback(bOk);
        }
    });
}

// Get the current VFO, frequency and mode
void Civ7300::GetFrequency(FrequencyCallback callback)
{
    sendCommand(
        civCmdReadFrequency,
        {},
        [callback = std::move(callback)](bool bOk, const std::vector<uint8_t>& resp) {
            if (!callback)
            {
                return;
            }
            if (!bOk || resp.size() != 5)
            {
                callback(false, 0);
                return;
            }
            uint64_t frequencyHz = 0;
            uint64_t placeValue = 1;
            for (uint8_t byte : resp)
            {
                frequencyHz += static_cast<uint64_t>(fromBcd(byte)) * placeValue;
                placeValue *= 100;
            }
            callback(true, frequencyHz);
        },
        300);
}

void Civ7300::GetRadioId(RadioIdCallback callback)
{
    sendCommand(civCmdReadRadioId, {civSubReadRadioId}, [callback = std::move(callback)](bool bOk, const std::vector<uint8_t>& resp) {
        if (!callback)
        {
            return;
        }
        callback(bOk && resp.size() >= 2, resp.size() >= 2 ? resp[1] : 0);
    });
}

void Civ7300::EnableFrequencyPoll(bool enabled)
{
    m_bFrequencyPollEnabled = enabled;
}

void Civ7300::EnableTimeSync(bool enabled)
{
    m_bTimeSyncEnabled = enabled;
}

void Civ7300::EnableTrafficLogging(bool enabled)
{
    m_bLogTraffic = enabled;
}

void Civ7300::DoWork()
{
    processCommand();

    if (m_bFrequencyPollEnabled && m_bRadioConnected && !m_bFrequencyRequestPending &&
        absolute_time_diff_us(get_absolute_time(), m_nextFrequencyCheckTime) <= 0)
    {
        m_nextFrequencyCheckTime = make_timeout_time_ms(civFrequencyCheckIntervalMs);
        m_bFrequencyRequestPending = true;
        GetFrequency([this](bool bOk, uint64_t frequencyHz) {
            m_bFrequencyRequestPending = false;
            if (bOk && (!m_bHaveLastFrequency || frequencyHz != m_lastFrequencyHz))
            {
                logFrequency(frequencyHz);
                m_lastFrequencyHz = frequencyHz;
                m_bHaveLastFrequency = true;
            }
        });
    }

    if (!m_bTimeSyncEnabled)
    {
        return;
    }

    if (m_syncState == SyncState::WaitingForMinute)
    {
        if (absolute_time_diff_us(get_absolute_time(), m_applyAtTime) > 0)
        {
            return;
        }
        m_nextTimeCheckTime = make_timeout_time_ms((((m_timeCheckIntervalMs / 1000 / 60) * 60) - 2) * 1000);
        beginClockWrite();
        return;
    }

    if (m_syncState != SyncState::Idle)
    {
        return;
    }

    if (!m_bRadioConnected)
    {
        if (!m_bRadioIdRequestPending && TimeMgr::IsWallClockValid() &&
            absolute_time_diff_us(get_absolute_time(), m_nextRadioIdCheckTime) <= 0)
        {
            m_nextRadioIdCheckTime = make_timeout_time_ms(civRadioIdCheckIntervalMs);
            m_bRadioIdRequestPending = true;
            GetRadioId([this](bool bOk, uint8_t radioId) {
                m_bRadioIdRequestPending = false;
                if (bOk && radioId == civExpectedRadioId)
                {
                    m_bRadioConnected = true;
                    m_nextTimeCheckTime = get_absolute_time();
                    LogInfo("Radio ID verified as IC-7300 (0x94)");
                }
            });
        }
        return;
    }

    if (absolute_time_diff_us(get_absolute_time(), m_nextTimeCheckTime) > 0)
    {
        return;
    }
    m_nextTimeCheckTime = make_timeout_time_ms(m_timeCheckIntervalMs);
    if (TimeMgr::IsWallClockValid())
    {
        beginClockRead();
    }
}

void Civ7300::beginClockRead()
{
    m_syncState = SyncState::ReadingClock;
    GetDate([this](bool bOk, int year, int month, int day) {
        if (!bOk)
        {
            m_syncState = SyncState::Idle;
            LogInfo("Failed to read radio clock");
            return;
        }
        m_radioYear = year;
        m_radioMonth = month;
        m_radioDay = day;
        GetTime([this](bool bTimeOk, int hour, int minute) {
            if (!bTimeOk)
            {
                m_syncState = SyncState::Idle;
                LogInfo("Failed to read radio clock");
                return;
            }
            m_radioHour = hour;
            m_radioMinute = minute;
            GetUtcOffset([this](bool bOffsetOk, float offset) {
                if (!bOffsetOk)
                {
                    m_syncState = SyncState::Idle;
                    LogInfo("Failed to read radio clock");
                    return;
                }
                m_radioOffset = offset;
                evaluateClockRead();
            });
        });
    });
}

void Civ7300::evaluateClockRead()
{
    const uint64_t nowUtc = TimeMgr::CurrentEpochSeconds();
    const float offsetHours = TimeMgr::TimeZoneOffsetHours();
    const std::time_t localTime = static_cast<std::time_t>(nowUtc) + static_cast<std::time_t>(std::lround(offsetHours * 3600.0f));
    std::tm tmLocal {};
    gmtime_r(&localTime, &tmLocal);

    const bool bDateMismatch = m_radioYear != tmLocal.tm_year + 1900 || m_radioMonth != tmLocal.tm_mon + 1 || m_radioDay != tmLocal.tm_mday;
    const bool bTimeMismatch = m_radioHour != tmLocal.tm_hour || m_radioMinute != tmLocal.tm_min;
    const bool bOffsetMismatch = std::fabs(m_radioOffset - offsetHours) > 0.01f;

    if (!bDateMismatch && !bTimeMismatch && !bOffsetMismatch)
    {
#if (!TIME_SYNC_ALWAYS_SYNC)
        LogInfo("Radio clock is in sync");
        m_bClockSynced = true;
        m_clockSyncSource = TimeMgr::GetTimeSource();
        m_syncState = SyncState::Idle;
        return;
#endif
    }

#if (!TIME_SYNC_ALWAYS_SYNC)
    if (bDateMismatch || bTimeMismatch)
    {
        LogInfo("Radio date/time mismatch detected; correcting at next minute boundary");
#else
    LogInfo("Updating clock at next minute boundary");
#endif
        m_bNeedOffsetFix = bOffsetMismatch;
        const uint64_t secondsIntoMinute = nowUtc % 60;
        const uint64_t secondsUntilNextMinute = (secondsIntoMinute == 0) ? 60 : (60 - secondsIntoMinute);
        m_applyAtTime = delayed_by_us(get_absolute_time(), secondsUntilNextMinute * 1000000ULL);
        m_syncState = SyncState::WaitingForMinute;
#if (!TIME_SYNC_ALWAYS_SYNC)
    }
    else
    {
        LogInfo("Radio UTC offset mismatch detected; correcting now");
        m_syncState = SyncState::WritingOffset;
        SetUtcOffset(offsetHours, [this](bool bOk) {
            m_syncState = SyncState::Idle;
            if (!bOk)
            {
                LogInfo("Failed to correct radio UTC offset");
                m_nextTimeCheckTime = make_timeout_time_ms(civClockSetRetryIntervalMs);
                return;
            }
            m_bClockSynced = true;
            m_clockSyncSource = TimeMgr::GetTimeSource();
        });
    }
#endif
}

void Civ7300::beginClockWrite()
{
    const uint64_t nowUtc = TimeMgr::CurrentEpochSeconds();
    const float offsetHours = TimeMgr::TimeZoneOffsetHours();
    const std::time_t localTime = static_cast<std::time_t>(nowUtc) + static_cast<std::time_t>(std::lround(offsetHours * 3600.0f));
    std::tm tmLocal {};
    gmtime_r(&localTime, &tmLocal);

    m_syncState = SyncState::WritingClock;
    SetDate(tmLocal.tm_year + 1900, tmLocal.tm_mon + 1, tmLocal.tm_mday, [this, tmLocal, offsetHours](bool bDateOk) {
        if (!bDateOk)
        {
            failClockWrite();
            return;
        }
        SetTime(tmLocal.tm_hour, tmLocal.tm_min, [this, offsetHours](bool bTimeOk) {
            if (!bTimeOk)
            {
                failClockWrite();
                return;
            }
            if (m_bNeedOffsetFix)
            {
                SetUtcOffset(offsetHours, [this](bool bOffsetOk) {
                    if (!bOffsetOk)
                    {
                        failClockWrite();
                        return;
                    }
                    m_syncState = SyncState::Idle;
                    m_bClockSynced = true;
                    m_clockSyncSource = TimeMgr::GetTimeSource();
                    LogInfo("Radio clock set");
                });
                return;
            }
            m_syncState = SyncState::Idle;
            m_bClockSynced = true;
            m_clockSyncSource = TimeMgr::GetTimeSource();
            LogInfo("Radio clock set");
        });
    });
}

void Civ7300::failClockWrite()
{
    m_syncState = SyncState::Idle;
    LogInfo("Failed to set radio clock");
    m_nextTimeCheckTime = make_timeout_time_ms(civClockSetRetryIntervalMs);
}

void Civ7300::sendCommand(uint8_t cmd, std::vector<uint8_t> dataArea, CommandCallback callback, uint32_t timeoutMs)
{
    m_commandQueue.push_back({cmd, std::move(dataArea), timeoutMs, std::move(callback)});
}

void Civ7300::processCommand()
{
    if (!m_bCommandActive && !m_commandQueue.empty())
    {
        CommandRequest request = std::move(m_commandQueue.front());
        m_commandQueue.pop_front();

        std::vector<uint8_t> frame;
        frame.reserve(request.dataArea.size() + 6);
        frame.push_back(0xFE);
        frame.push_back(0xFE);
        frame.push_back(m_radioAddr);
        frame.push_back(m_controllerAddr);
        frame.push_back(request.cmd);
        frame.insert(frame.end(), request.dataArea.begin(), request.dataArea.end());
        frame.push_back(0xFD);

        m_rxBuf.clear();
        m_expectedCmd = request.cmd;
        m_activeCommandCallback = std::move(request.callback);
        m_commandDeadline = make_timeout_time_ms(request.timeoutMs);
        m_bCommandActive = true;
        if (m_bLogTraffic)
        {
            LogInfo("CIV TX: " + toHexString(frame.data(), frame.size()));
        }
        m_spBridge->SendToRadio(frame.data(), frame.size());
    }

    if (!m_bCommandActive)
    {
        return;
    }

    std::vector<uint8_t> response;
    bool bOk = false;
    if (tryParseFrame(response, bOk))
    {
        m_bCommandActive = false;
        CommandCallback callback = std::move(m_activeCommandCallback);
        if (callback)
        {
            callback(bOk, response);
        }
        return;
    }

    if (absolute_time_diff_us(get_absolute_time(), m_commandDeadline) <= 0)
    {
        m_bCommandActive = false;
        CommandCallback callback = std::move(m_activeCommandCallback);
        if (m_bRadioConnected)
        {
            LogInfo("Radio communication timeout; marking radio disconnected");
        }
        m_bRadioConnected = false;
        m_bClockSynced = false;
        m_clockSyncSource = TimeMgr::TimeSource::Unknown;
        m_syncState = SyncState::Idle;
        if (callback)
        {
            callback(false, {});
        }
    }
}

void Civ7300::onRadioData(const uint8_t* pData, size_t nLen)
{
    if (m_bLogTraffic)
    {
        LogInfo("CIV RX: " + toHexString(pData, nLen));
    }
    m_rxBuf.insert(m_rxBuf.end(), pData, pData + nLen);
    processTransceiveFrames();

    constexpr size_t kMaxBufSize = 512; // bound growth from bus traffic we're not waiting on
    if (m_rxBuf.size() > kMaxBufSize)
    {
        m_rxBuf.erase(m_rxBuf.begin(), m_rxBuf.begin() + (m_rxBuf.size() - kMaxBufSize));
    }
}

void Civ7300::logFrequency(uint64_t frequencyHz)
{
    LogInfo("VFO frequency: " + std::to_string(frequencyHz) + " Hz");
}

void Civ7300::processTransceiveFrames()
{
    size_t scanStart = 0;
    while (m_rxBuf.size() >= 5 && scanStart + 5 <= m_rxBuf.size())
    {
        const auto itStart = std::find(m_rxBuf.begin() + scanStart, m_rxBuf.end() - 1, static_cast<uint8_t>(0xFE));
        if (itStart == m_rxBuf.end() - 1)
        {
            return;
        }
        if (*(itStart + 1) != 0xFE)
        {
            scanStart = static_cast<size_t>(itStart - m_rxBuf.begin()) + 1;
            continue;
        }

        const auto itEnd = std::find(itStart + 2, m_rxBuf.end(), static_cast<uint8_t>(0xFD));
        if (itEnd == m_rxBuf.end())
        {
            return;
        }

        const size_t frameStart = static_cast<size_t>(itStart - m_rxBuf.begin());
        const size_t frameLen = static_cast<size_t>(itEnd - itStart) + 1;
        const size_t payloadLen = frameLen - 5;
        if (m_rxBuf[frameStart + 2] == civBroadcastAddr && m_rxBuf[frameStart + 3] == m_radioAddr && payloadLen >= 1)
        {
            const uint8_t command = m_rxBuf[frameStart + 4];
            const uint8_t* payload = m_rxBuf.data() + frameStart + 5;
            if (command == civCmdTransceiveFrequency && payloadLen == 6)
            {
                uint64_t frequencyHz = 0;
                uint64_t placeValue = 1;
                for (size_t i = 0; i < 5; ++i)
                {
                    frequencyHz += static_cast<uint64_t>(fromBcd(payload[i])) * placeValue;
                    placeValue *= 100;
                }
                if (!m_bHaveLastFrequency || frequencyHz != m_lastFrequencyHz)
                {
                    if (m_bLogTraffic)
                    {
                        LogInfo("CIV transceive frequency detected");
                    }
                    logFrequency(frequencyHz);
                    m_lastFrequencyHz = frequencyHz;
                    m_bHaveLastFrequency = true;
                }
            }
            else if (command == civCmdTransceiveMode && payloadLen >= 2)
            {
                const uint8_t mode = payload[0];
                if (!m_bHaveLastMode || mode != m_lastMode)
                {
                    if (m_bLogTraffic)
                    {
                        LogInfo("CIV transceive mode detected");
                    }
                    LogInfo("VFO mode: " + civModeName(mode));
                    m_lastMode = mode;
                    m_bHaveLastMode = true;
                }
            }
            m_rxBuf.erase(m_rxBuf.begin() + frameStart, m_rxBuf.begin() + frameStart + frameLen);
            scanStart = 0;
            continue;
        }
        scanStart = frameStart + frameLen;
    }
}

bool Civ7300::tryParseFrame(std::vector<uint8_t>& outData, bool& outOk)
{
    while (m_rxBuf.size() >= 5)
    {
        if (m_rxBuf[0] != 0xFE || m_rxBuf[1] != 0xFE)
        {
            m_rxBuf.erase(m_rxBuf.begin());
            continue;
        }

        const auto itEnd = std::find(m_rxBuf.begin() + 2, m_rxBuf.end(), static_cast<uint8_t>(0xFD));
        if (itEnd == m_rxBuf.end())
        {
            return false; // frame not complete yet
        }

        const size_t frameLen = static_cast<size_t>(itEnd - m_rxBuf.begin()) + 1;
        const uint8_t to = m_rxBuf[2];
        const uint8_t from = m_rxBuf[3];
        const size_t payloadStart = 4;
        const size_t payloadLen = frameLen - 1 - payloadStart;

        if (to == m_controllerAddr && from == m_radioAddr && payloadLen >= 1)
        {
            const uint8_t payloadByte0 = m_rxBuf[payloadStart];
            if (payloadLen == 1 && payloadByte0 == civResponseOk)
            {
                if (m_bLogTraffic)
                {
                    LogInfo("CIV RX: " + toHexString(m_rxBuf.data(), frameLen));
                }
                outOk = true;
                outData.clear();
                m_rxBuf.erase(m_rxBuf.begin(), m_rxBuf.begin() + frameLen);
                return true;
            }
            if (payloadLen == 1 && payloadByte0 == civResponseNg)
            {
                if (m_bLogTraffic)
                {
                    LogInfo("CIV RX: " + toHexString(m_rxBuf.data(), frameLen));
                }
                outOk = false;
                outData.clear();
                m_rxBuf.erase(m_rxBuf.begin(), m_rxBuf.begin() + frameLen);
                return true;
            }
            if (payloadByte0 == m_expectedCmd)
            {
                if (m_bLogTraffic)
                {
                    LogInfo("CIV RX: " + toHexString(m_rxBuf.data(), frameLen));
                }
                outOk = true;
                outData.assign(m_rxBuf.begin() + payloadStart + 1, m_rxBuf.begin() + frameLen - 1);
                m_rxBuf.erase(m_rxBuf.begin(), m_rxBuf.begin() + frameLen);
                return true;
            }
        }

        // Not the frame we're waiting for (other bus traffic); drop it and keep scanning.
        m_rxBuf.erase(m_rxBuf.begin(), m_rxBuf.begin() + frameLen);
    }
    return false;
}
