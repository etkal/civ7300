/*
 * Civ7300
 *
 * Copyright (c) 2026 Erik Tkal
 *
 * Basic asynchronous support for the Icom CI-V remote control protocol on an IC-7300. It queues
 * command frames through RadioBridge and parses the resulting OK/NG/data responses. Currently the
 * date, time, UTC offset (time zone), and VFO frequency query commands are implemented (CI-V
 * command 1A, sub-command 05, sub-sub-commands 0094/0095/0096, per the IC-7300 CI-V reference). It
 * also periodically verifies the radio's clock against TimeMgr and corrects it if it has drifted.
 *
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <deque>
#include <functional>
#include <vector>
#include <memory>

#include "pico/time.h"

#include "radio_bridge.h"
#include "timemgr.h"

class Civ7300
{
public:
    typedef std::shared_ptr<Civ7300> Shared;
    using Completion = std::function<void(bool)>;
    using DateCallback = std::function<void(bool, int, int, int)>;
    using TimeCallback = std::function<void(bool, int, int)>;
    using OffsetCallback = std::function<void(bool, float)>;
    using FrequencyCallback = std::function<void(bool, uint64_t)>;
    using RadioIdCallback = std::function<void(bool, uint8_t)>;

    // radioAddr/controllerAddr are the CI-V addresses of the radio and of this application on the
    // CI-V bus (0x94/0xE0 are the diagram's example addresses; 0xE1 is used by default here to
    // avoid colliding with an external controller that uses the more common 0xE0).
    explicit Civ7300(RadioBridge::Shared spBridge,
                     uint8_t radioAddr = 0x94,
                     uint8_t controllerAddr = 0xE0);

    // Radio commands complete asynchronously; callbacks run from DoWork().
    void GetDate(DateCallback callback);
    void SetDate(int year, int month, int day, Completion callback = {});
    void GetTime(TimeCallback callback);
    void SetTime(int hour, int minute, Completion callback = {});
    void GetUtcOffset(OffsetCallback callback);
    void SetUtcOffset(float offsetHours, Completion callback = {});
    void GetFrequency(FrequencyCallback callback);
    void GetRadioId(RadioIdCallback callback);
    void EnableFrequencyPoll(bool enabled);
    void EnableTimeSync(bool enabled);
    void EnableTrafficLogging(bool enabled);

    // True once the radio has confirmed itself as an IC-7300; cleared on any communication error
    // (e.g. a command timeout) so a reconnect must be re-verified via GetRadioId.
    bool IsRadioConnected() const { return m_bRadioConnected; }
    // True once this class has successfully written the radio's clock; cleared whenever the radio
    // is found to be disconnected, since the radio's clock state is then no longer known.
    bool IsClockSynced() const { return m_bClockSynced; }

    // Call repeatedly from the main loop. Periodically reads the radio's date/time/UTC offset and
    // corrects any that differ from TimeMgr. Since the radio has no seconds field, a needed
    // date/time correction is deferred until the next minute boundary so the value written matches
    // the instant it takes effect.
    void DoWork();

private:
    using CommandCallback = std::function<void(bool, const std::vector<uint8_t>&)>;

    struct CommandRequest
    {
        uint8_t cmd;
        std::vector<uint8_t> dataArea;
        uint32_t timeoutMs;
        CommandCallback callback;
    };

    void sendCommand(uint8_t cmd, std::vector<uint8_t> dataArea, CommandCallback callback, uint32_t timeoutMs = 300);
    void processCommand();
    void beginClockRead();
    void evaluateClockRead();
    void beginClockWrite();
    void failClockWrite();

    void onRadioData(const uint8_t* pData, size_t nLen);
    void processTransceiveFrames();
    void logFrequency(uint64_t frequencyHz);
    bool tryParseFrame(std::vector<uint8_t>& outData, bool& outOk);

    enum class SyncState
    {
        Idle,
        WaitingForMinute,
        ReadingClock,
        WritingClock,
        WritingOffset,
    };

    RadioBridge::Shared m_spBridge;
    uint8_t m_radioAddr;
    uint8_t m_controllerAddr;
    uint32_t m_timeCheckIntervalMs;

    std::vector<uint8_t> m_rxBuf; // genuine radio bytes captured while awaiting a response
    std::deque<CommandRequest> m_commandQueue;
    CommandCallback m_activeCommandCallback;
    absolute_time_t m_commandDeadline;
    bool m_bCommandActive {false};
    uint8_t m_expectedCmd {0};
    bool m_bLogTraffic {false};

    absolute_time_t m_nextTimeCheckTime;
    absolute_time_t m_nextFrequencyCheckTime;
    absolute_time_t m_nextRadioIdCheckTime;
    bool m_bFrequencyPollEnabled {false};
    bool m_bTimeSyncEnabled {false};
    uint64_t m_lastFrequencyHz {0};
    bool m_bHaveLastFrequency {false};
    uint8_t m_lastMode {0};
    bool m_bHaveLastMode {false};
    SyncState m_syncState {SyncState::Idle};
    absolute_time_t m_applyAtTime; // WaitingForMinute: the next minute boundary to apply the fix at
    bool m_bNeedOffsetFix {false};
    bool m_bRadioConnected {false}; // set once the radio has confirmed itself as an IC-7300 (ID 0x94)
    bool m_bClockSynced {false};    // set once the radio's clock has been successfully written
    bool m_bFrequencyRequestPending {false};
    bool m_bRadioIdRequestPending {false};
    int m_radioYear {0};
    int m_radioMonth {0};
    int m_radioDay {0};
    int m_radioHour {0};
    int m_radioMinute {0};
    float m_radioOffset {0.0f};
};
