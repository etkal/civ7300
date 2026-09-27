# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if(EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.24)

# Set the board type for pin and LED decisions
#set(PICO_BOARD pico)
#set(PICO_BOARD pico2)
set(PICO_BOARD pico_w)
#set(PICO_BOARD pico2_w)
#set(PICO_BOARD waveshare_rp2040_zero)
#set(PICO_BOARD seeed_xiao_rp2040)

# Some LED modules use RGB ordering for the WS2812 LEDs
#add_compile_definitions(WS2812_LED_IS_RGB)

# Time zone for logging timestamps (examples: UTC, America/New_York, Europe/London)
add_compile_definitions(TIME_ZONE=\"America/New_York\")
# DST override for logging timestamps: AUTOMATIC, NO, or YES
add_compile_definitions(USE_DST=AUTOMATIC)
# Legacy fallback for simple numeric offsets
add_compile_definitions(GMT_OFFSET=-4.0)

# Time sync sources for correcting the device clock (and hence the radio's CI-V clock). Both may be
# enabled at once: GPS updates opportunistically whenever NMEA sentences arrive, while NTP (pico_w/
# pico2_w only) is attempted periodically once Wifi is connected. Also, the TIME_SYNC_ALWAYS_SYNC
# option can be used to periodically set the radio clock even if it appears to be in sync, in order
# to maintain more accurate time.
set(TIME_SYNC_USE_GPS ON)
set(TIME_SYNC_USE_NTP ON)
set(TIME_SYNC_ALWAYS_SYNC ON)

# Set pico platform definitions for conditional compilation
if(PICO_BOARD STREQUAL pico_w OR PICO_BOARD STREQUAL pico2_w)
    add_compile_definitions(PLATFORM_PICO_W)
endif()
if(PICO_BOARD STREQUAL pico OR PICO_BOARD STREQUAL pico2 OR PICO_BOARD STREQUAL pico_w OR PICO_BOARD STREQUAL pico2_w)
    add_compile_definitions(PLATFORM_PICO)
endif()

# Time synchronization configuration
if(TIME_SYNC_USE_GPS)
    add_compile_definitions(TIME_SYNC_USE_GPS=1)
else()
    add_compile_definitions(TIME_SYNC_USE_GPS=0)
endif()
if(TIME_SYNC_USE_NTP AND (PICO_BOARD STREQUAL pico_w OR PICO_BOARD STREQUAL pico2_w))
    add_compile_definitions(TIME_SYNC_USE_NTP=1)
else()
    add_compile_definitions(TIME_SYNC_USE_NTP=0)
endif()
if(TIME_SYNC_ALWAYS_SYNC)
    # Always sync periodically, keeps more accurate time
    add_compile_definitions(TIME_SYNC_ALWAYS_SYNC=1)
else()
    add_compile_definitions(TIME_SYNC_ALWAYS_SYNC=0)
endif()

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(civ7300 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(PICO_CXX_ENABLE_EXCEPTIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_compile_options(-Wall
    -Wno-format # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
    -Wno-unused-function # we have some for the docs that aren't called
    -Wno-maybe-uninitialized
)

add_executable(civ7300)
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/generated)
# generate the header file into the source tree as it is included in the RP2040 datasheet
pico_generate_pio_header(civ7300
    ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio
    OUTPUT_DIR
    ${CMAKE_CURRENT_LIST_DIR}/generated
)

add_subdirectory(src)

target_include_directories(civ7300 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(civ7300
    pico_stdlib
    pico_stdio
    hardware_gpio
    hardware_pio
    hardware_uart
    hardware_dma
    pico_aon_timer
)

if(PICO_BOARD STREQUAL pico_w OR PICO_BOARD STREQUAL pico2_w)
    target_link_libraries(civ7300
        pico_cyw43_arch_lwip_threadsafe_background
    )
endif()

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(civ7300)
