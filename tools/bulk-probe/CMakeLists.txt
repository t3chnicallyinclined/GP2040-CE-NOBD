# Track-B bulk-probe (Phase 0): WinUSB bulk streamer. Standalone Pico SDK project.
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build            # -> build/bulk_probe.uf2
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

project(bulk_probe C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(bulk_probe
    main.c
    usb_descriptors.c
)
target_include_directories(bulk_probe PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(bulk_probe
    pico_stdlib
    tinyusb_device
)
pico_enable_stdio_usb(bulk_probe 0)     # native USB is the WinUSB vendor interface, not stdio
pico_enable_stdio_uart(bulk_probe 0)
pico_add_extra_outputs(bulk_probe)
