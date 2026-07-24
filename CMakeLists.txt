# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
if(DEFINED ENV{PICO_BOARD})
  set(PICO_BOARD $ENV{PICO_BOARD} CACHE STRING "Board type")
else()
  set(PICO_BOARD pico CACHE STRING "Board type")
endif()

cmake_minimum_required(VERSION 3.10...4.0)
include(CMakePrintHelpers)

# enable compile commands for use by IDE autocompletion
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.1.1")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 2.1.1 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

# set the version for webconfig, etc. based on git
find_package(Git)
execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty --abbrev=7
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	OUTPUT_VARIABLE GIT_REPO_VERSION
	OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --abbrev=7
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	OUTPUT_VARIABLE GIT_REPO_BUILD_ID
	OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX REPLACE "v([0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1" CMAKE_GIT_REPO_VERSION ${GIT_REPO_VERSION})
string(REGEX REPLACE "^(.......-.*)|(.......)$" "0.0.0" CMAKE_GIT_REPO_VERSION ${CMAKE_GIT_REPO_VERSION}) # fix if all we have is the git SHA
# NOBD Release Version — update this number before each release build
set(NOBD_RELEASE "23")
set(GIT_REPO_VERSION "v${CMAKE_GIT_REPO_VERSION}-nobd-${NOBD_RELEASE}")

configure_file("headers/version.h.in" "headers/version.h")
message("GIT_REPO_VERSION is ${GIT_REPO_VERSION}")
message("CMAKE_GIT_REPO_VERSION is ${CMAKE_GIT_REPO_VERSION}")
message("GIT_REPO_BUILD_ID is ${GIT_REPO_BUILD_ID}")

# Uncomment the next line for an unomptimized build for debugging. Use in conjunction with the Debug build type.
# set(PICO_DEOPTIMIZED_DEBUG 1)

project(GP2040-CE-NOBD LANGUAGES C CXX ASM VERSION ${CMAKE_GIT_REPO_VERSION})
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(DEFINED ENV{GP2040_BOARDCONFIG})
  set(GP2040_BOARDCONFIG $ENV{GP2040_BOARDCONFIG})
elseif(NOT DEFINED GP2040_BOARDCONFIG)
  set(GP2040_BOARDCONFIG Pico)
endif()

if(DEFINED ENV{SKIP_SUBMODULES})
  set(SKIP_SUBMODULES $ENV{SKIP_SUBMODULES})
elseif(NOT DEFINED SKIP_SUBMODULES)
  set(SKIP_SUBMODULES FALSE)
endif()

if(DEFINED ENV{SKIP_WEBBUILD})
  set(SKIP_WEBBUILD $ENV{SKIP_WEBBUILD})
elseif(NOT DEFINED SKIP_WEBBUILD)
  set(SKIP_WEBBUILD FALSE)
endif()


if(SKIP_SUBMODULES)
  cmake_print_variables(SKIP_SUBMODULES)
else()
  find_package(Git)
  if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
      execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                      RESULT_VARIABLE GIT_SUBMOD_RESULT)
      if(NOT GIT_SUBMOD_RESULT EQUAL "0")
          message(FATAL_ERROR "git submodule update --init --recursive failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
      else()
          message("Submodules updated")
      endif()
  endif()
endif()

if(SKIP_WEBBUILD)
  cmake_print_variables(SKIP_WEBBUILD)
else()
  message(STATUS "Not Skipping WebBuild")
  include(${CMAKE_SOURCE_DIR}/modules/FindNodeJS.cmake)
  include(${CMAKE_SOURCE_DIR}/modules/FindNPM.cmake)
  if(NODEJS_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/www")
    if(NPM_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/www/package.json")
      execute_process(COMMAND ${NPM_EXECUTABLE} ci
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/www
                      RESULT_VARIABLE NPM_CI_RESULT)
      if(NOT NPM_CI_RESULT EQUAL "0")
        message(FATAL_ERROR "npm ci failed with ${NPM_CI_RESULT}")
      endif()
      execute_process(COMMAND ${NPM_EXECUTABLE} run build
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/www
                      RESULT_VARIABLE NPM_BUILD_RESULT)
      if(NOT NPM_BUILD_RESULT EQUAL "0")
        message(FATAL_ERROR "npm run build failed with ${NPM_BUILD_RESULT}")
      endif()
    endif()
  endif()
endif()


set(PICO_BOARD_HEADER_DIRS ${CMAKE_SOURCE_DIR}/configs/${GP2040_BOARDCONFIG})
include(FetchContent)
FetchContent_Declare(ArduinoJson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG        v6.21.2
)
FetchContent_MakeAvailable(ArduinoJson)

if(DEFINED ENV{PICO_PIO_USB_PATH})
  message(STATUS "Found custom Pico-PIO-USB path, using it.")
  set(PICO_PIO_USB_PATH $ENV{PICO_PIO_USB_PATH})
elseif(NOT DEFINED PICO_PIO_USB_PATH)
  message(STATUS "Using default Pico-PIO-USB.")
  set(PICO_PIO_USB_PATH ${CMAKE_SOURCE_DIR}/lib/pico_pio_usb)
endif()

add_compile_options(-Wall
        -Wtype-limits
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        )

include(compile_proto.cmake)
compile_proto()

#pull in tinyUSB
set(PICO_TINYUSB_PATH "${CMAKE_CURRENT_LIST_DIR}/lib/tinyusb")

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# Remove unused code and data
add_compile_options(-fdata-sections -ffunction-sections)
add_link_options(-Wl,--gc-sections)

add_subdirectory(lib)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # Activate some compiler / linker options to aid us with diagnosing stack space issues in Debug builds
  add_compile_options(-fstack-usage -Wstack-usage=500)
  add_compile_definitions(PICO_USE_STACK_GUARDS=1)
endif()

# We want a larger stack of 4kb per core instead of the default 2kb
add_compile_definitions(PICO_STACK_SIZE=0x1000)

add_executable(${PROJECT_NAME}
# --- vendored DST-proven input core (source of truth: nobd-zero-v2/firmware/core) ---
src/input/core/analog.c
src/input/core/buffer.c
src/input/core/focus.c
src/input/core/hotkeys.c
src/input/core/input_pipeline.c
src/input/core/macros.c
src/input/core/profiles.c
src/input/core/remap.c
src/input/core/reverse.c
src/input/core/socd.c
src/input/core/sync_window.c
src/input/core/turbo.c
src/platform/main.cpp
src/platform/gp2040.cpp
src/platform/gp2040aux.cpp
src/platform/faultcapture.cpp
src/input/gamepad.cpp
src/input/GamepadState.cpp
src/platform/addonmanager.cpp
src/ui/playerleds.cpp
src/reflect/shared/xinput_host.cpp
src/reflect/shared/xgip_protocol.cpp
src/reflect/shared/xsm3/excrypt_des.c
src/reflect/shared/xsm3/excrypt_parve.c
src/reflect/shared/xsm3/excrypt_sha.c
src/reflect/shared/xsm3/usbdsec.c
src/reflect/shared/xsm3/xsm3.c
src/reflect/astro/AstroDriver.cpp
src/reflect/egret/EgretDriver.cpp
src/reflect/hid/HIDDriver.cpp
src/reflect/keyboard/KeyboardDriver.cpp
src/reflect/mdmini/MDMiniDriver.cpp
src/reflect/neogeo/NeoGeoDriver.cpp
src/reflect/net/NetDriver.cpp
src/reflect/pcengine/PCEngineDriver.cpp
src/reflect/ps3/PS3Driver.cpp
src/reflect/ps4/PS4Auth.cpp
src/reflect/ps4/PS4AuthUSBListener.cpp
src/reflect/ps4/PS4Driver.cpp
src/reflect/psclassic/PSClassicDriver.cpp
src/reflect/switch/SwitchDriver.cpp
src/reflect/switchpro/SwitchProDriver.cpp
src/reflect/xbone/XBOneAuth.cpp
src/reflect/xbone/XBOneAuthUSBListener.cpp
src/reflect/xbone/XBOneDriver.cpp
src/reflect/xboxog/xid/xid_driver.c
src/reflect/xboxog/xid/xid_gamepad.c
src/reflect/xboxog/xid/xid_remote.c
src/reflect/xboxog/xid/xid_steelbattalion.c
src/reflect/xboxog/xid/xid.c
src/reflect/xboxog/XboxOriginalDriver.cpp
src/reflect/xinput/XInputAuth.cpp
src/reflect/xinput/XInputAuthUSBListener.cpp
src/reflect/xinput/XInputDriver.cpp
src/reflect/dreamcast/maple_bus.cpp
src/reflect/dreamcast/DreamcastDriver.cpp
src/reflect/dreamcast/DreamcastVMU.cpp
src/net/w6100.cpp
src/hal/i2c/i2cdevicebase.cpp
src/hal/i2c/pcf8575/pcf8575.cpp
src/hal/i2c/ssd1306/obd_ssd1306.cpp
src/hal/i2c/ssd1306/tiny_ssd1306.cpp
src/ui/display/ui/elements/GPWidget.cpp
src/ui/display/ui/elements/GPButton.cpp
src/ui/display/ui/elements/GPLever.cpp
src/ui/display/ui/elements/GPLabel.cpp
src/ui/display/ui/elements/GPMenu.cpp
src/ui/display/ui/elements/GPScreen.cpp
src/ui/display/ui/elements/GPShape.cpp
src/ui/display/ui/elements/GPSprite.cpp
src/ui/display/ui/screens/ButtonLayoutScreen.cpp
src/ui/display/ui/screens/ConfigScreen.cpp
src/ui/display/ui/screens/MainMenuScreen.cpp
src/ui/display/ui/screens/PinViewerScreen.cpp
src/ui/display/ui/screens/RestartScreen.cpp
src/ui/display/ui/screens/StatsScreen.cpp
src/ui/display/ui/screens/SplashScreen.cpp
src/ui/display/ui/screens/DisplaySaverScreen.cpp
src/ui/display/ui/screens/SystemErrorScreen.cpp
src/ui/display/GPGFX.cpp
src/ui/display/GPGFX_UI.cpp
src/reflect/drivermanager.cpp
src/platform/eventmanager.cpp
src/platform/layoutmanager.cpp
src/platform/peripheralmanager.cpp
src/platform/storagemanager.cpp
src/platform/system.cpp
src/platform/usbdriver.cpp
src/platform/usbhostmanager.cpp
src/platform/config_legacy.cpp
src/platform/config_utils.cpp
src/platform/webconfig.cpp
src/addons/analog.cpp
src/addons/board_led.cpp
src/addons/bootsel_button.cpp
src/addons/focus_mode.cpp
src/addons/he_trigger.cpp
src/addons/buzzerspeaker.cpp
src/addons/dualdirectional.cpp
src/addons/keyboard_host.cpp
src/addons/keyboard_host_listener.cpp
src/addons/i2canalog1219.cpp
src/addons/i2c_gpio_pcf8575.cpp
src/addons/display.cpp
src/addons/neopicoleds.cpp
src/addons/playerleds.cpp
src/addons/reactiveleds.cpp
src/addons/rotaryencoder.cpp
src/addons/reverse.cpp
src/addons/drv8833_rumble.cpp
src/addons/turbo.cpp
src/addons/slider_socd.cpp
src/addons/wiiext.cpp
src/addons/input_macro.cpp
src/addons/snes_input.cpp
src/addons/tilt.cpp
src/addons/spi_analog_ads1256.cpp
src/addons/gamepad_usb_host.cpp
src/addons/gamepad_usb_host_listener.cpp
src/addons/tg16_input.cpp
src/ui/animation/animation.cpp
src/ui/animation/animationstation.cpp
src/ui/animation/effects/chase.cpp
src/ui/animation/effects/customtheme.cpp
src/ui/animation/effects/customthemepressed.cpp
src/ui/animation/effects/rainbow.cpp
src/ui/animation/effects/staticcolor.cpp
src/ui/animation/effects/statictheme.cpp
${PROTO_OUTPUT_DIR}/enums.pb.c
${PROTO_OUTPUT_DIR}/config.pb.c
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME}_${CMAKE_PROJECT_VERSION}-nobd-${NOBD_RELEASE}_${GP2040_BOARDCONFIG})

# Generate PIO header for Dreamcast Maple Bus
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/reflect/dreamcast/maple.pio)
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/net/uart_rx.pio)

pico_set_program_name(${PROJECT_NAME} "GP2040-CE-NOBD")
pico_set_program_version(${PROJECT_NAME} ${GIT_REPO_VERSION})

target_link_libraries(${PROJECT_NAME}
pico_stdlib
pico_bootsel_via_double_reset
tinyusb_host
tinyusb_pico_pio_usb
CRC32
FlashPROM
ADS1219
ADS1256
NeoPico
OneBitDisplay
ArduinoJson
rndis
hardware_adc
hardware_pwm
hardware_pio
hardware_dma
hardware_spi
ioLibrary_Driver
PicoPeripherals
WiiExtension
SNESpad
pico_mbedtls
nanopb
)

target_include_directories(${PROJECT_NAME} PUBLIC
headers
headers/addons
headers/configs
headers/drivers
headers/drivers/shared
headers/drivers/dreamcast
headers/events
headers/interfaces
headers/interfaces/i2c
headers/interfaces/i2c/ads1219
headers/interfaces/i2c/pcf8575
headers/interfaces/i2c/ssd1306
headers/interfaces/i2c/wiiextension
headers/gamepad
headers/display
headers/display/fonts
headers/display/ui
headers/display/ui/static
headers/display/ui/elements
headers/display/ui/screens
headers/animationstation
headers/animationstation/effects
configs/${GP2040_BOARDCONFIG}
${PROTO_OUTPUT_DIR}
${CMAKE_BINARY_DIR}/headers
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
  PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
  BOARD_CONFIG_FILE_NAME="$<TARGET_FILE_BASE_NAME:${PROJECT_NAME}>"
  GP2040_BOARDCONFIG="${GP2040_BOARDCONFIG}"
)

target_include_directories(${PROJECT_NAME}  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
  )

pico_add_extra_outputs(${PROJECT_NAME})

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}_${CMAKE_PROJECT_VERSION}_${GP2040_BOARDCONFIG}.uf2
    ${CMAKE_CURRENT_LIST_DIR}/README.md
    DESTINATION .
)

if (NOT (DEFINED ENV(CI)) AND (EXISTS ${CMAKE_SOURCE_DIR}/modules/Custom.cmake))
  message(STATUS "Found custom script.")
	include(${CMAKE_SOURCE_DIR}/modules/Custom.cmake)
endif()
