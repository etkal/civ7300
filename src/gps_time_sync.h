/*
 * GpsTimeSync class
 *
 * (c) 2026 Erik Tkal
 *
 * Reads NMEA-0183 sentences from a GPS device on a UART and uses the RMC sentence's time/date
 * fields to keep TimeMgr's wall clock synchronized, as a serial alternative to NTP.
 *
 */

#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "uart.h"

class GpsTimeSync
{
public:
    typedef std::shared_ptr<GpsTimeSync> Shared;

    explicit GpsTimeSync(Uart::Shared spGpsUart);
    ~GpsTimeSync() = default;

    // Call repeatedly from the main loop to read and process bytes from the GPS UART.
    void DoWork();

private:
    void processByte(char ch);
    void processSentence(const std::string& strSentence);
    static bool validateChecksum(const std::string& strSentence);

    Uart::Shared m_spGpsUart;
    std::string m_strLine;
};
