target_sources(civ7300 PUBLIC
    button.cpp
    civ7300.cpp
    gps_time_sync.cpp
    led.cpp
    main.cpp
    radio_bridge.cpp
    timemgr.cpp
    uart.cpp
)

if(TIME_SYNC_USE_NTP AND (PICO_BOARD STREQUAL pico_w OR PICO_BOARD STREQUAL pico2_w))
    target_sources(civ7300 PUBLIC wifi_connection.cpp)
endif()
