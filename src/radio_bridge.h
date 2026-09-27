/*
 * RadioBridge class
 *
 * (c) 2026 Erik Tkal
 *
 * Interfaces with a CI-V radio connected on a UART. The radio's CI-V bus is bidirectional through
 * external buffers: any bytes written to the radio are echoed back on its RX line, so those echoed
 * bytes must be discarded rather than treated as genuine data from the radio. Other controllers on
 * the CI-V bus attach directly to it, so this class only needs to talk to the radio itself.
 *
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <memory>
#include <functional>

#include "uart.h"

class RadioBridge
{
public:
    typedef std::shared_ptr<RadioBridge> Shared;

    explicit RadioBridge(Uart::Shared spRadioUart);
    ~RadioBridge() = default;

    // Call repeatedly from the main loop to move bytes to/from the radio.
    void DoWork();

    // Queue bytes for the radio; DoWork() transmits them incrementally and accounts for the echo.
    void SendToRadio(const uint8_t* pData, size_t nLen);
    void SendToRadio(const std::string& strData);

    // Registers a callback invoked with genuine (non-echo) bytes received from the radio. Used by
    // components (e.g. Civ7300) that need to observe the radio's responses to commands they send.
    void SetRadioDataSink(std::function<void(const uint8_t*, size_t)> sink);

private:
    void flushRadioTx();
    void pollRadio(); // read from the radio UART, discard echo, forward genuine data to the sink

    Uart::Shared m_spRadioUart;
    std::vector<uint8_t> m_txQueue;
    size_t m_txQueueOffset {0};
    size_t m_nEchoPending {0}; // number of not-yet-seen bytes we expect to be echoed back by the radio bus
    std::function<void(const uint8_t*, size_t)> m_radioDataSink;
};
