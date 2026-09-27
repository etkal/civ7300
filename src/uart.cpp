/*
 * Uart class
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

#include "uart.h"

#include <string>

#include "hardware/gpio.h"
#include "hardware/irq.h"

Uart::Uart()
{
}

Uart::~Uart()
{
    if (m_dmaChanRx >= 0)
    {
        dma_channel_abort(m_dmaChanRx);
        dma_channel_unclaim(m_dmaChanRx);
        m_dmaChanRx = -1;
    }
}

void Uart::Initialize(uart_inst_t* pUart, uint tx_gpio, uint rx_gpio, uint baudrate, uint data_bits, uint stop_bits, uart_parity_t parity)
{
    m_pUart = pUart;
    m_tx_gpio = tx_gpio;
    m_rx_gpio = rx_gpio;
    m_data_bits = data_bits;
    m_stop_bits = stop_bits;
    m_parity = parity;
    m_baudrate = baudrate;

    uart_init(m_pUart, m_baudrate);
    gpio_set_function(m_tx_gpio, GPIO_FUNC_UART);
    gpio_set_function(m_rx_gpio, GPIO_FUNC_UART);
    uart_set_hw_flow(m_pUart, false, false);
    uart_set_format(m_pUart, m_data_bits, m_stop_bits, m_parity);

    // Set up a circular DMA buffer for UART RX. A single DMA channel continuously streams bytes from
    // the UART data register into m_szDmaBuf in ring mode, wrapping automatically at the end. There is
    // no interrupt: callers chase the hardware write pointer (derived from the channel's remaining
    // transfer_count) and consume bytes at their leisure. This is race-free because the DMA only ever
    // appends and we only ever read up to the last-known write position.
    uart_set_fifo_enabled(m_pUart, true); // FIFO must be enabled for UART DMA operation
    uart_set_irqs_enabled(m_pUart, false, false);

    m_dmaChanRx = dma_claim_unused_channel(true);
    m_iDmaReadPos = 0;

    dma_channel_config cfgRx = dma_channel_get_default_config(m_dmaChanRx);
    channel_config_set_transfer_data_size(&cfgRx, DMA_SIZE_8);
    channel_config_set_read_increment(&cfgRx, false);
    channel_config_set_write_increment(&cfgRx, true);
    channel_config_set_dreq(&cfgRx, uart_get_dreq_num(m_pUart, false));     // pace by UART RX
    channel_config_set_ring(&cfgRx, true, __builtin_ctz(UART_DMA_BUFSIZE)); // wrap write address at buffer size
    dma_channel_configure(m_dmaChanRx,
                          &cfgRx,
                          m_szDmaBuf,                // circular destination buffer
                          &uart_get_hw(m_pUart)->dr, // UART data register
                          0xFFFFFFFF,                // effectively never stop; ring mode wraps the address
                          true);                     // start immediately
}

void Uart::Write(const uint8_t* pData, size_t nLen)
{
    if (nullptr == m_pUart || nullptr == pData || 0 == nLen)
    {
        return;
    }
    uart_write_blocking(m_pUart, pData, nLen);
}

size_t Uart::TryWrite(const uint8_t* pData, size_t nLen)
{
    if (nullptr == m_pUart || nullptr == pData || 0 == nLen)
    {
        return 0;
    }

    size_t nWritten = 0;
    while (nWritten < nLen && uart_is_writable(m_pUart))
    {
        uart_putc_raw(m_pUart, pData[nWritten]);
        ++nWritten;
    }
    return nWritten;
}

void Uart::WriteString(const std::string& strData)
{
    Write(reinterpret_cast<const uint8_t*>(strData.data()), strData.size());
}

size_t Uart::Read(uint8_t* pBuf, size_t nBufSize)
{
    if (nullptr == m_pUart || nullptr == pBuf || 0 == nBufSize || m_dmaChanRx < 0)
    {
        return 0;
    }

    // Current write position is the channel's write address register relative to the buffer base;
    // in ring mode this always stays within [0, UART_DMA_BUFSIZE). Read a snapshot before consuming.
    uintptr_t nWriteAddr = dma_channel_hw_addr(m_dmaChanRx)->write_addr;
    uintptr_t nBase = (uintptr_t)m_szDmaBuf;
    size_t nWritePos = (size_t)(nWriteAddr - nBase);
    if (nWritePos >= UART_DMA_BUFSIZE)
    {
        nWritePos = 0; // defensive clamp (should not happen in ring mode)
    }

    if (nWritePos == m_iDmaReadPos)
    {
        return 0; // nothing new
    }

    size_t nAvailable = (nWritePos > m_iDmaReadPos) ? (nWritePos - m_iDmaReadPos) : (UART_DMA_BUFSIZE - m_iDmaReadPos + nWritePos);
    size_t nToCopy = (nAvailable < nBufSize) ? nAvailable : nBufSize;

    for (size_t i = 0; i < nToCopy; ++i)
    {
        pBuf[i] = m_szDmaBuf[(m_iDmaReadPos + i) % UART_DMA_BUFSIZE];
    }
    m_iDmaReadPos = (m_iDmaReadPos + nToCopy) % UART_DMA_BUFSIZE;

    return nToCopy;
}
