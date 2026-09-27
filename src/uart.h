/*
 * Uart class
 *
 * (c) 2026 Erik Tkal
 *
 * Generic UART wrapper with DMA-buffered, nonblocking RX and blocking/nonblocking TX methods.
 * RX bytes are streamed into a circular buffer by DMA (no interrupt); callers poll with Read().
 * This class has no knowledge of any particular protocol (e.g. NMEA, CI-V).
 *
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>

#include "hardware/dma.h"
#include "hardware/uart.h"

auto constexpr UART_DMA_BUFSIZE = 256; // Circular DMA buffer size; must be a power of two for ring mode

class Uart
{
public:
    typedef std::shared_ptr<Uart> Shared;

    Uart();
    ~Uart();

    void Initialize(uart_inst_t* pUart,
                    uint tx_gpio,
                    uint rx_gpio,
                    uint baudrate,
                    uint data_bits = 8,
                    uint stop_bits = 1,
                    uart_parity_t parity = UART_PARITY_NONE);

    // Write bytes out the UART. Blocks until the data has been queued to the UART hardware.
    void Write(const uint8_t* pData, size_t nLen);
    // Write as many bytes as the UART TX FIFO can accept without waiting.
    size_t TryWrite(const uint8_t* pData, size_t nLen);
    void WriteString(const std::string& strData);

    // Copy up to nBufSize received bytes into pBuf, returning the number of bytes copied (0 if none available).
    size_t Read(uint8_t* pBuf, size_t nBufSize);

    inline uart_inst_t* GetUart() const
    {
        return m_pUart;
    }

private:
    uart_inst_t* m_pUart {nullptr};
    uint m_tx_gpio {0};
    uint m_rx_gpio {0};
    uint m_data_bits {0};
    uint m_stop_bits {0};
    uart_parity_t m_parity {UART_PARITY_NONE};
    uint m_baudrate {0};

    // DMA ring state. A single channel streams UART RX bytes into m_szDmaBuf in ring mode; callers
    // chase the hardware write pointer (derived from the channel's transfer_count).
    int m_dmaChanRx {-1};     // DMA channel: UART RX -> circular buffer
    size_t m_iDmaReadPos {0}; // our read offset into the circular buffer
    // DMA circular buffer for UART RX
    uint8_t m_szDmaBuf[UART_DMA_BUFSIZE] __attribute__((aligned(UART_DMA_BUFSIZE))); // ring buffer, aligned to its size
};
