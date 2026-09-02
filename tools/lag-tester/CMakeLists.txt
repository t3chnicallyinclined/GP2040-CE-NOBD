# lag-tester -- external controller-latency rig (RP2040 + MAX3421E USB Host FeatherWing).
# Standalone Pico SDK project. Build:
#   export PICO_SDK_PATH=C:/Users/trist/projects/GP2040-CE/build/_deps/pico_sdk-src
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
# Output: build/lag_tester.uf2
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Board type")
include(pico_sdk_import.cmake)

project(lag_tester C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(lag_tester
    main.cpp
    xinput_host.cpp
)
target_include_directories(lag_tester PRIVATE ${CMAKE_CURRENT_LIST_DIR})

# The MAX3421E host-controller driver is not pulled in by the SDK's tinyusb_host lib; add it.
# (hcd_rp2040.c compiles to nothing while CFG_TUH_MAX3421 is set, so there is no clash.)
target_sources(lag_tester PRIVATE
    ${PICO_SDK_PATH}/lib/tinyusb/src/portable/analog/max3421/hcd_max3421.c
)

target_link_libraries(lag_tester
    pico_stdlib
    hardware_spi
    tinyusb_host
)

# results over UART0 (GP0 TX @ 115200). USB-CDC output can't coexist with tinyusb_host in the SDK,
# so read via a USB-serial adapter on GP0/GND, or the spare Pico flashed as a debugprobe.
pico_enable_stdio_usb(lag_tester 0)
pico_enable_stdio_uart(lag_tester 1)

pico_add_extra_outputs(lag_tester)
