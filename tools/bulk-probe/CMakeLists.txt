# Track-B bulk-probe (Phase 0): WinUSB bulk streamer. Standalone Pico SDK project.
#   export PICO_SDK_PATH=C:/Users/trist/projects/GP2040-CE/build/_deps/pico_sdk-src
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -Dpicotool_DIR=C:/Users/trist/projects/gp2040-te/build/_deps/picotool
#   cmake --build build            # -> build/bulk_probe.uf2
cmake_minimum_required(VERSION 3.13)

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
