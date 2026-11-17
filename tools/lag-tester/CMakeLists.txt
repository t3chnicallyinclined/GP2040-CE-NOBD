# lag-tester -- external controller-latency rig (RP2040 + MAX3421E USB Host FeatherWing).
# Standalone Pico SDK project. Build:
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
# Output: build/lag_tester.uf2
# The pico-sdk and picotool default to the copies the firmware build already downloaded into
# <repo>/build/_deps. Override with absolute -DPICO_SDK_PATH=... and -Dpicotool_DIR=....
cmake_minimum_required(VERSION 3.13)

# Must run before include(pico_sdk_import.cmake) (itself before project()). The import file is a
# verbatim SDK copy and only reads the environment, so the repo default is chosen here.
# FORCE is safe: the guards mean it only replaces an unset or *-NOTFOUND value.
set(NOBD_REPO_DEPS "${CMAKE_CURRENT_LIST_DIR}/../../build/_deps")
if(NOT PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH}
   AND EXISTS "${NOBD_REPO_DEPS}/pico_sdk-src/pico_sdk_init.cmake")
  get_filename_component(NOBD_SDK_DIR "${NOBD_REPO_DEPS}/pico_sdk-src" ABSOLUTE)
  set(PICO_SDK_PATH "${NOBD_SDK_DIR}" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)
endif()
if(NOT picotool_DIR AND EXISTS "${NOBD_REPO_DEPS}/picotool/picotoolConfig.cmake")
  get_filename_component(NOBD_PICOTOOL_DIR "${NOBD_REPO_DEPS}/picotool" ABSOLUTE)
  set(picotool_DIR "${NOBD_PICOTOOL_DIR}" CACHE PATH "Directory holding picotoolConfig.cmake" FORCE)
endif()

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
