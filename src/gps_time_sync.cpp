/*
 * GpsTimeSync class
 *
 * (c) 2026 Erik Tkal
 *
 */

#include "gps_time_sync.h"

#include <iomanip>
#include <sstream>
#include <vector>

#include "timemgr.h"
#include "led.h"

namespace
{
    auto constexpr gpsLineMaxLen = 96; // Max NMEA-0183 sentence length is actually 82 characters
} // namespace

GpsTimeSync::GpsTimeSync(Uart::Shared spGpsUart)
    : m_spGpsUart(std::move(spGpsUart))
{
}

void GpsTimeSync::DoWork()
{
    uint8_t szBuf[64];
    size_t nLen = m_spGpsUart->Read(szBuf, sizeof(szBuf));
    for (size_t i = 0; i < nLen; ++i)
    {
        processByte(static_cast<char>(szBuf[i]));
    }
}

void GpsTimeSync::processByte(char ch)
{
    if (ch == '$' && !m_strLine.empty())
    {
        m_strLine.clear(); // new sentence started before the previous one terminated; discard the partial data
    }
    if (ch == '\n')
    {
        if (!m_strLine.empty())
        {
            processSentence(m_strLine);
        }
        m_strLine.clear();
        return;
    }
    if (ch != '\r')
    {
        m_strLine.push_back(ch);
    }
    if (m_strLine.size() >= gpsLineMaxLen)
    {
        m_strLine.clear(); // overflow; resync on the next '$'
    }
}

void GpsTimeSync::processSentence(const std::string& strSentence)
{
    // Recognize any talker's RMC sentence (e.g. $GPRMC, $GNRMC), which carries both time and date.
    if (strSentence.size() < 6 || strSentence[0] != '$' || strSentence.compare(3, 3, "RMC") != 0)
    {
        return;
    }
    if (!validateChecksum(strSentence))
    {
        return;
    }

    const size_t nStar = strSentence.find('*');
    const std::string strBody = (nStar == std::string::npos) ? strSentence : strSentence.substr(0, nStar);

    std::vector<std::string> vFields;
    std::stringstream ss(strBody);
    std::string strField;
    while (std::getline(ss, strField, ','))
    {
        vFields.push_back(strField);
    }
    // Field 1 is the UTC time (HHMMSS.ss); field 9 is the UTC date (DDMMYY).
    if (vFields.size() < 10 || vFields[1].empty() || vFields[9].empty())
    {
        return;
    }

    const std::string& strGpsTime = vFields[1];
    const std::string& strGpsDate = vFields[9];
    if (!TimeMgr::IsGpsTimeDateWithinOneSecond(strGpsTime, strGpsDate))
    {
        LogInfo("Setting time from GPS sentence: " + strSentence);
        TimeMgr::SetTimeFromGps(strGpsTime, strGpsDate);
    }
}

bool GpsTimeSync::validateChecksum(const std::string& strSentence)
{
    const size_t nStar = strSentence.find('*');
    if (nStar == std::string::npos || strSentence.size() < nStar + 3)
    {
        return false;
    }

    uint8_t check = 0;
    for (size_t i = 1; i < nStar; ++i) // XOR of all bytes between '$' and '*'
    {
        check ^= static_cast<uint8_t>(strSentence[i]);
    }

    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(check);
    return oss.str() == strSentence.substr(nStar + 1, 2);
}
