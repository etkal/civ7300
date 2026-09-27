# civ7300

RP2040 firmware that connects to an Icom IC-7300 over CI-V and synchronizes the radio clock from
GPS time or, optionally, NTP. Other controllers connect directly to the shared CI-V bus; echoes of
firmware transmissions are discarded. UART receive uses DMA-backed circular buffers, and CI-V
transmissions are queued and drained incrementally by the main loop.

## Connections

The radio CI-V UART is configured for 19200 baud; the GPS UART is configured for 9600 baud. Both
use 8 data bits, no parity, and 1 stop bit (8N1).

| Connection | UART | TX GPIO | RX GPIO |
| --- | --- | ---: | ---: |
| Radio CI-V | UART0 | GP0 | GP1 |
| GPS receiver (Pico / Pico W / Waveshare RP2040 Zero) | UART1 | GP4 | GP5 |

Other CI-V controllers share the radio bus directly. The radio connection expects the external
CI-V interface/buffers to provide the required electrical interface and route the bus echo to
UART0 RX.

## Radio Clock

The firmware includes IC-7300 CI-V support for reading and setting the date, time, and UTC offset.
Clock synchronization is enabled by default; frequency polling and CI-V traffic logging are
disabled. GPS synchronization is enabled and NTP synchronization is disabled by default in the
root `CMakeLists.txt`. When NTP is enabled on a Pico W, the firmware connects to Wi-Fi in station
mode. Once the wall clock is valid, it verifies the radio ID is an IC-7300 and checks the radio
clock every five minutes. Any date/time correction is applied at the next minute boundary because
the radio clock has minute precision. The defaults in the root `CMakeLists.txt` are the
`America/New_York` time zone and automatic daylight-saving adjustment (`TIME_ZONE` and `USE_DST`);
change these for the deployment location.

When NTP synchronization is enabled for Pico W / Pico 2 W, Wi-Fi credentials are supplied by the
local, untracked `src/network_info.h`. Start with `network_info.h.example`, copy it to
`src/network_info.h`, and replace the placeholder SSID and password.

## Build and Flash

The project uses the Raspberry Pi Pico SDK 2.3.0 and the CMake workflow provided by the Raspberry
Pi Pico VS Code extension. The selected board is `pico_w` in the root `CMakeLists.txt`; change
`PICO_BOARD` there to build for another supported board. Build with the extension's configure/build
commands, then flash the generated `build/civ7300.uf2` (or use the extension's load/flash command).

## Source Layout

- `src/uart.*` - protocol-agnostic UART driver with DMA-buffered RX and blocking/nonblocking TX APIs.
- `src/radio_bridge.*` - queues CI-V transmissions, cancels bus echoes, and exposes genuine radio
  data to CI-V clients.
- `src/civ7300.*` - asynchronous IC-7300 CI-V commands and periodic clock synchronization.
- `src/timemgr.*` - wall-clock validity, NTP, time-zone/DST handling, and timestamped logging.
- `src/wifi_connection.*` - asynchronous Pico W station-mode connection and retry state machine.
- `src/led.*`, `src/button.*` - LED and button support.
- `src/main.cpp` - application startup, board pins, and main work loop.
