/*
 * RadioBridge class
 *
 * (c) 2026 Erik Tkal
 *
 */

#include "radio_bridge.h"

#include <algorithm>

RadioBridge::RadioBridge(Uart::Shared spRadioUart)
    : m_spRadioUart(std::move(spRadioUart))
{
}

void RadioBridge::DoWork()
{
    flushRadioTx();
    pollRadio();
}

void RadioBridge::SendToRadio(const uint8_t* pData, size_t nLen)
{
    if (nullptr == pData || 0 == nLen)
    {
        return;
    }
    if (m_txQueueOffset != 0 && m_txQueueOffset * 2 >= m_txQueue.size())
    {
        m_txQueue.erase(m_txQueue.begin(), m_txQueue.begin() + m_txQueueOffset);
        m_txQueueOffset = 0;
    }
    m_txQueue.insert(m_txQueue.end(), pData, pData + nLen);
}

void RadioBridge::SendToRadio(const std::string& strData)
{
    SendToRadio(reinterpret_cast<const uint8_t*>(strData.data()), strData.size());
}

void RadioBridge::SetRadioDataSink(std::function<void(const uint8_t*, size_t)> sink)
{
    m_radioDataSink = std::move(sink);
}

void RadioBridge::flushRadioTx()
{
    if (m_txQueueOffset == m_txQueue.size())
    {
        m_txQueue.clear();
        m_txQueueOffset = 0;
        return;
    }

    const size_t nWritten = m_spRadioUart->TryWrite(m_txQueue.data() + m_txQueueOffset, m_txQueue.size() - m_txQueueOffset);
    m_nEchoPending += nWritten;
    m_txQueueOffset += nWritten;
    if (m_txQueueOffset == m_txQueue.size())
    {
        m_txQueue.clear();
        m_txQueueOffset = 0;
    }
}

void RadioBridge::pollRadio()
{
    uint8_t szBuf[64];
    size_t nLen = m_spRadioUart->Read(szBuf, sizeof(szBuf));

    size_t i = 0;
    while (i < nLen)
    {
        if (m_nEchoPending > 0)
        {
            // Discard bytes that are the echo of something we sent
            size_t nDiscard = std::min(m_nEchoPending, nLen - i);
            m_nEchoPending -= nDiscard;
            i += nDiscard;
        }
        else
        {
            // Genuine data from the radio: forward the remainder of this chunk to the sink
            if (m_radioDataSink)
            {
                m_radioDataSink(&szBuf[i], nLen - i);
            }
            i = nLen;
        }
    }
}
