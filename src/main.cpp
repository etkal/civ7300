/*
 * Copyright (c) 2026 Erik Tkal
 *
 * civ7300: interfaces with a CI-V radio connected to UART0 (other CI-V controllers attach directly
 * to the radio's CI-V bus) and keeps the radio's clock synchronized from a serial GPS device on
 * UART1 and/or NTP over Wifi (pico_w/pico2_w only), selected at build time.
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

#include <iostream>

#include "pico/stdlib.h"

#if defined(PLATFORM_PICO_W)
#include "pico/cyw43_arch.h"
#endif
#include "network_info.h"
#include "wifi_connection.h"

#include "button.h"
#include "civ7300.h"
#include "gps_time_sync.h"
#include "led.h"
#include "radio_bridge.h"
#include "timemgr.h"
#include "uart.h"

#define UART0_DEVICE uart0 // Radio (CI-V)
#define PIN_UART0_TX 0
#define PIN_UART0_RX 1

#if defined(WAVESHARE_RP2040_ZERO)
#define UART1_DEVICE uart1 // GPS device
#define PIN_UART1_TX 4
#define PIN_UART1_RX 5
#elif defined(PLATFORM_PICO)
#define UART1_DEVICE uart1 // GPS device
#define PIN_UART1_TX 4
#define PIN_UART1_RX 5
#endif

#define CIV_BAUD_RATE  19200
#define GPS_BAUD_RATE  9600 // Standard NMEA-0183 baud rate for most serial GPS devices
#define DATA_BITS      8
#define STOP_BITS      1
#define PARITY         UART_PARITY_NONE

#define USE_WS2812_PIN 28 // Override
// #define USE_LED_PIN 16    // Override

// GPIO pin for a button
#define PIN_BUTTON 6

#if TIME_SYNC_USE_GPS && !defined(UART1_DEVICE)
#error "GPS time synchronization requires a serial GPS device (UART1) to be defined"
#endif

extern "C"
{
    int _getentropy(void* buffer, size_t length)
    {
        (void)buffer;
        (void)length;
        return ENOSYS;
    }
}

int main()
{
    stdio_usb_init();

#if !defined(NDEBUG)
    timer_hw->dbgpause = 0;
    sleep_ms(5000);
#endif

#if defined(PLATFORM_PICO_W)
    if (cyw43_arch_init())
    {
        std::cout << "Failed to initialize cyw43 hardware" << std::endl;
        return 1;
    }
#endif

    LogInfo("Starting civ7300 radio bridge application...");

    TimeMgr::InitializeSingleton(TIME_ZONE); // Needed for logging timestamps

    LogInfo("Creating LED and button objects...");

#if defined(SEEED_XIAO_RP2040)
    // Clear LED(s) on XIAO (default on)
    LED_pico ledBlue(25);  // blue
    LED_pico ledGreen(16); // green
    LED_pico ledRed(17);   // red
#endif

    // Create the LED object
    LED::Shared spLED;
#if defined(USE_WS2812_PIN)
    spLED = std::make_shared<LED_neo>(3, USE_WS2812_PIN);
    spLED->Initialize();
    spLED->SetPixel(0, led_green);
#elif defined(PICO_DEFAULT_WS2812_PIN) && !defined(USE_LED_PIN)
    spLED = std::make_shared<LED_neo>(1, PICO_DEFAULT_WS2812_PIN);
    spLED->Initialize();
    spLED->SetPixel(0, led_green);
#elif defined(USE_LED_PIN)
    spLED = std::make_shared<LED_pico>(USE_LED_PIN);
    spLED->Initialize();
    spLED->SetIgnore({led_red, led_magenta});
#elif defined(PICO_DEFAULT_LED_PIN)
    spLED = std::make_shared<LED_pico>(PICO_DEFAULT_LED_PIN);
    spLED->Initialize();
    spLED->SetIgnore({led_red, led_magenta});
#elif defined(PLATFORM_PICO_W)
    spLED = std::make_shared<LED_pico_w>(CYW43_WL_GPIO_LED_PIN);
    spLED->Initialize();
    spLED->SetIgnore({led_red, led_magenta});
#endif

    // Create the button object
    Button::Shared spButton;
#if defined(PIN_BUTTON)
    spButton = std::make_shared<Button>(PIN_BUTTON);
    spButton->Initialize();
#endif

    WifiConnection::Shared spWifi = std::make_shared<WifiConnection>();
#if TIME_SYNC_USE_NTP
    LogInfo("Initializing Wifi connection object...");
    // Wifi connection is driven asynchronously from the main loop; NTP sync is gated on it being up.
    spWifi->Initialize(g_szWifiSsid, g_szWifiPassword);
    TimeMgr::EnableNtpAutoRetry();
#endif

    LogInfo("Creating UART and CI-V objects...");

    // UART0: connected to the radio (CI-V, bidirectional through external buffers)
    Uart::Shared spRadioUart = std::make_shared<Uart>();
    spRadioUart->Initialize(UART0_DEVICE, PIN_UART0_TX, PIN_UART0_RX, CIV_BAUD_RATE, DATA_BITS, STOP_BITS, PARITY);

    // The radio bridge handles data transfers to and from the radio
    RadioBridge::Shared spBridge = std::make_shared<RadioBridge>(spRadioUart);

    // UART1: connected to a serial GPS device, used to keep the radio's clock synchronized
    GpsTimeSync::Shared spGpsTimeSync;
#if TIME_SYNC_USE_GPS && defined(UART1_DEVICE)
    Uart::Shared spGpsUart = std::make_shared<Uart>();
    spGpsUart->Initialize(UART1_DEVICE, PIN_UART1_TX, PIN_UART1_RX, GPS_BAUD_RATE, DATA_BITS, STOP_BITS, PARITY);
    spGpsTimeSync = std::make_shared<GpsTimeSync>(spGpsUart);
#endif // TIME_SYNC_USE_GPS && defined(UART1_DEVICE)

    LogInfo("Creating the Civ7300 object...");

    // Periodically verifies/corrects the radio's date, time, and UTC offset via CI-V commands.
    Civ7300::Shared spCiv7300 = std::make_shared<Civ7300>(spBridge);
    spCiv7300->EnableFrequencyPoll(false);
    spCiv7300->EnableTimeSync(true);
    spCiv7300->EnableTrafficLogging(false);

    // Initialize the previous state variables for the main loop LED status updates.
    uint64_t prevNowSecond = TimeMgr::CurrentEpochSeconds();
    bool bPrevRadioConnected {true};
    bool bPrevClockSynced {true};
    WifiState prevWifiState = WifiState::Unknown;

    LogInfo("Running radio bridge...");
    while (true)
    {
        // Update the status LEDs.  Customize this based on the LED configuration you have.
        {
            uint64_t nowSecond = TimeMgr::CurrentEpochSeconds();
            bool bRadioConnected = spCiv7300->IsRadioConnected();
            bool bClockSynced = spCiv7300->IsClockSynced();
            WifiState wifiState = spWifi->GetState();

            if (bRadioConnected != bPrevRadioConnected || bClockSynced != bPrevClockSynced)
            {
                bPrevClockSynced = bClockSynced;
                bPrevRadioConnected = bRadioConnected;
                uint32_t statusColor = !bRadioConnected ? led_red : bClockSynced ? led_green : led_orange;
                LED::GetLED(0)->SetPixel(2, statusColor);
            }

            if (wifiState != prevWifiState)
            {
                prevWifiState = wifiState;
                uint32_t wifiColor = WifiState::Connected == wifiState    ? led_blue
                                     : WifiState::Connecting == wifiState ? led_orange
                                                                          : led_red;
                LED::GetLED(0)->SetPixel(1, wifiColor);
            }

            if (nowSecond != prevNowSecond)
            {
                prevNowSecond = nowSecond;
                TimeMgr::TimeSource clockSyncSource = TimeMgr::GetTimeSource();
                uint32_t blinkColor = TimeMgr::TimeSource::Gps == clockSyncSource   ? led_green
                                      : TimeMgr::TimeSource::Ntp == clockSyncSource ? led_blue
                                                                                    : led_red;
                LED::GetLED(0)->SetPixel(0, scale_color(blinkColor, 255));
                LED::GetLED(0)->Blink_ms(0, 50);
            }
        }

        // Perform any main loop work for LEDs (e.g. turning off)
        if (spLED)
        {
            spLED->DoWork();
        }

        // Perform any main loop work for the radio bridge
        spBridge->DoWork();

        // Handle any time sync tasks
        if (spGpsTimeSync)
        {
            spGpsTimeSync->DoWork();
        }

        // Perform any main loop work for the WiFi module
        spWifi->DoWork();

        // Attempt NTP time sync if WiFi is connected
        if (spWifi->IsConnected())
        {
            TimeMgr::AttemptNtpTimeSync();
        }

        // Perform any main loop work for the CI-V radio
        spCiv7300->DoWork();

        // Perform any main loop work for the button module
        if (spButton)
        {
            // placeholder for button-triggered commands to the radio
        }

        tight_loop_contents(); // Does nothing, just a marker
    }

#if defined(PLATFORM_PICO_W)
    cyw43_arch_deinit();
#endif

    return 0;
}
