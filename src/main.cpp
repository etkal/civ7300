/*
 * Copyright (c) 2026 Erik Tkal
 *
 * civ7300: interfaces with a CI-V radio connected to UART0 (other CI-V controllers attach directly
 * to the radio's CI-V bus) and keeps the radio's clock synchronized from a serial GPS device on
 * UART1 and/or NTP over Wifi (pico_w/pico2_w only), selected at build time.
 *
 */

#include <iostream>

#include "pico/stdlib.h"

#if defined(PLATFORM_PICO_W)
#include "pico/cyw43_arch.h"
#if TIMEMGR_ENABLE_NTP
#include "network_info.h"
#include "wifi_connection.h"
#endif
#endif

#include "button.h"
#include "civ7300.h"
#if TIME_SYNC_USE_GPS
#include "gps_time_sync.h"
#endif
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

    TimeMgr::InitializeSingleton(TIME_ZONE); // Needed for logging timestamps
    LogInfo("Starting civ7300 radio bridge application...");

#if defined(PLATFORM_PICO_W) && TIMEMGR_ENABLE_NTP
    // Wifi connection is driven asynchronously from the main loop; NTP sync is gated on it being up.
    WifiConnection::Shared spWifi = std::make_shared<WifiConnection>(g_szWifiSsid, g_szWifiPassword);
    spWifi->Initialize();
    TimeMgr::EnableNtpAutoRetry();
#endif

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

    LogInfo("Creating UART objects...");

    // UART0: connected to the radio (CI-V, bidirectional through external buffers)
    Uart::Shared spRadioUart = std::make_shared<Uart>();
    spRadioUart->Initialize(UART0_DEVICE, PIN_UART0_TX, PIN_UART0_RX, CIV_BAUD_RATE, DATA_BITS, STOP_BITS, PARITY);

    RadioBridge::Shared spBridge = std::make_shared<RadioBridge>(spRadioUart);

    // UART1: connected to a serial GPS device, used to keep the radio's clock synchronized
#if TIME_SYNC_USE_GPS
    GpsTimeSync::Shared spGpsTimeSync;
#if defined(UART1_DEVICE)
    Uart::Shared spGpsUart = std::make_shared<Uart>();
    spGpsUart->Initialize(UART1_DEVICE, PIN_UART1_TX, PIN_UART1_RX, GPS_BAUD_RATE, DATA_BITS, STOP_BITS, PARITY);
    spGpsTimeSync = std::make_shared<GpsTimeSync>(spGpsUart);
#endif
#endif

    // Periodically verifies/corrects the radio's date, time, and UTC offset via CI-V commands.
    Civ7300::Shared spCiv7300 = std::make_shared<Civ7300>(spBridge);
    spCiv7300->EnableFrequencyPoll(true);
    spCiv7300->EnableTimeSync(true);
    spCiv7300->EnableTrafficLogging(false);
    spLED->SetPixel(led_all, led_red);
    spLED->Show();

    uint64_t prevNowSecond = TimeMgr::CurrentEpochSeconds();
    bool bPrevRadioConnected {false};
    bool bPrevClockSynced {false};
#if defined(PLATFORM_PICO_W) && TIMEMGR_ENABLE_NTP
    WifiState prevWifiState = WifiState::Unknown;
#endif
    LogInfo("Running radio bridge...");
    while (true)
    {
        // Update the status LEDs.  Customize this based on the LED configuration you have.
        {
            uint64_t nowSecond = TimeMgr::CurrentEpochSeconds();
            bool bRadioConnected = spCiv7300->IsRadioConnected();
            bool bClockSynced = spCiv7300->IsClockSynced();
#if defined(PLATFORM_PICO_W) && TIMEMGR_ENABLE_NTP
            WifiState wifiState = spWifi ? spWifi->GetState() : WifiState::Unknown;
#endif
            if (nowSecond != prevNowSecond)
            {
                prevNowSecond = nowSecond;
                TimeMgr::TimeSource clockSyncSource = TimeMgr::GetTimeSource();
                uint32_t blinkColor = TimeMgr::TimeSource::Gps == clockSyncSource   ? led_green
                                      : TimeMgr::TimeSource::Ntp == clockSyncSource ? led_blue
                                                                                    : led_red;
                LED::GetLED(0)->Blink_ms(0, 30, blinkColor);
            }

            if (bRadioConnected != bPrevRadioConnected || bClockSynced != bPrevClockSynced)
            {
                bPrevClockSynced = bClockSynced;
                bPrevRadioConnected = bRadioConnected;
                uint32_t statusColor = !bRadioConnected ? led_red : bClockSynced ? led_green : led_yellow;
                LED::GetLED(0)->SetPixel(2, statusColor);
            }

#if defined(PLATFORM_PICO_W) && TIMEMGR_ENABLE_NTP
            if (wifiState != prevWifiState)
            {
                prevWifiState = wifiState;
                uint32_t wifiColor = WifiState::Connected == wifiState    ? led_blue
                                     : WifiState::Connecting == wifiState ? led_yellow
                                                                          : led_yellow;
                LED::GetLED(0)->SetPixel(1, wifiColor);
            }
#endif
        }

        if (spLED)
        {
            spLED->DoWork();
        }
        spBridge->DoWork();
#if TIME_SYNC_USE_GPS
        if (spGpsTimeSync)
        {
            spGpsTimeSync->DoWork();
        }
#endif
#if defined(PLATFORM_PICO_W) && TIMEMGR_ENABLE_NTP
        spWifi->DoWork();
        if (spWifi->IsConnected())
        {
            TimeMgr::AttemptNtpTimeSync();
        }
#endif
        spCiv7300->DoWork();
        if (spButton)
        {
            // placeholder for button-triggered commands to the radio
        }
        tight_loop_contents();
    }

#if defined(PLATFORM_PICO_W)
    cyw43_arch_deinit();
#endif

    return 0;
}
