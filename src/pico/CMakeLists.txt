cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(PICO_CXX_ENABLE_EXCEPTIONS 0)
set(PICO_CXX_ENABLE_RTTI 0)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/generated)
set(DEFAULT_CONFIG_HEADER "${CMAKE_CURRENT_LIST_DIR}/generated/default_config.hpp")
set(OPTIMIZE_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/external/optimizejson.py
                    gatas_default_config.json ${DEFAULT_CONFIG_HEADER})

set(LOCAL_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/gatas_local_config.json")
if(EXISTS "${LOCAL_CONFIG_FILE}")
  list(APPEND OPTIMIZE_SCRIPT --overlay "${LOCAL_CONFIG_FILE}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               "${LOCAL_CONFIG_FILE}")
  message(STATUS "Applying local configuration overlay")
else()
  message(STATUS "No local configuration overlay found")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico_w)
endif()
if(NOT DEFINED PICO_PLATFORM)
    set(PICO_PLATFORM rp2040)
endif()

execute_process(
  COMMAND ${OPTIMIZE_SCRIPT}
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR} ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE)

# All defines go in config.hpp that should not be configurable for the board
# itself. Default configurations could go here if that makes sense
set(CONFIG_HEADER "${CMAKE_CURRENT_LIST_DIR}/generated/config.hpp")
configure_file(config.hpp.in ${CONFIG_HEADER} @ONLY)

add_library(gatas_build_options INTERFACE)
target_compile_options(gatas_build_options INTERFACE
  -include
  ${CONFIG_HEADER}
)

# Add buildtime timestamp Move this to a dedicated 'status' module to reduce
# build times
include(${CMAKE_CURRENT_LIST_DIR}/../gatas_version.cmake)
string(TIMESTAMP BUILD_TIMESTAMP_EPOCH "%s" UTC)

set(BUILD_TIME_HEADER "${CMAKE_CURRENT_LIST_DIR}/generated/build_time.hpp")
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%SZ" UTC)
configure_file(build_time.hpp.in "${BUILD_TIME_HEADER}" @ONLY)

# pico_sdk_import.cmake is a single file copied from this SDK note: this must
# happen before project() include(cmake/pico_sdk_import.cmake)7s
include(cmake/pico_sdk_import.cmake)
include(cmake/FreeRTOS_Kernel_import.cmake)

if(PICO_SDK_VERSION_STRING VERSION_LESS "2.1.0")
  message(
    FATAL_ERROR
      "Raspberry Pi Pico SDK version 2.1.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}"
  )
endif()

set(OUTPUT_NAME "GATAS_${PICO_PLATFORM}")
project(${OUTPUT_NAME} C CXX ASM)

# Allow user to override this option from command line: -DGATAS_UART_OVER_USB=1
option(GATAS_UART_OVER_USB "Enable UART over USB (1) or physical UART (0)" "")

# Set default based on build type if not provided
if(GATAS_UART_OVER_USB STREQUAL "")
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(GATAS_UART_OVER_USB 1 CACHE BOOL "Enable UART over USB by default in Release builds" FORCE)
  else()
    set(GATAS_UART_OVER_USB 0 CACHE BOOL "Enable UART over USB by default in Debug builds" FORCE)
  endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(FIRMWARE_COMPILE_OPTIONS
    -O2
    -Wall
    -fsingle-precision-constant
    -ffast-math
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
  )
else()
  set(FIRMWARE_COMPILE_OPTIONS
    -g
    -O0
    -Wall
    -fsingle-precision-constant
    -ffast-math
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
  )
endif()

set(FIRMWARE_LINK_OPTIONS
  -Wl,--print-memory-usage
)

pico_sdk_init()

add_executable(${PROJECT_NAME} main.cpp IdleMemory.c lwip_core_lock.c)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../vendor vendorLib)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../lib globalLib)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../lib/include)

target_link_libraries(${PROJECT_NAME} PRIVATE gatas_build_options)

target_compile_options(${PROJECT_NAME} PRIVATE
  ${FIRMWARE_COMPILE_OPTIONS}
)

target_link_options(${PROJECT_NAME} PRIVATE
  ${FIRMWARE_LINK_OPTIONS}
)


target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE FreeRTOS-Kernel-Heap4
          pico_btstack_ble
          pico_btstack_classic
          pico_btstack_cyw43
          pico_cyw43_arch_lwip_sys_freertos
          pico_stdlib
          hardware_gpio
          hardware_spi
#          hardware_rtc
          hardware_irq
          hardware_uart
          hardware_pio
          # Vendor
          GDL90
          minmea
          etl::etl
          fanet::fanet
          adsl::adsl
          ArduinoJson
          # /lib
          core
          pioserial
          serialadsb
          config
          gps
          adsbdecoder
          libmodes
          dump1090client
          wifiservice
          webserver
          gpsdecoder
          rtc
          acespi
          bmp280
          sx1262
          adslace
          flarmgatas
          fanetace
          ogn
          radiotuner
          gdl90service
          gdloverudp
          utils
          aircrafttracker
          dataport
          airconnect
          gatasconnect
          bluetooth
          idle
          )

target_compile_definitions(
  ${PROJECT_NAME}
  PRIVATE FREE_RTOS_KERNEL_SMP=1
          portSUPPORT_SMP=1
          GATAS_UART_OVER_USB=${GATAS_UART_OVER_USB}
          PICO_USE_STACK_GUARDS=1)

# Enable debugging over PICO Debugger
if(GATAS_UART_OVER_USB)
  pico_enable_stdio_usb(${PROJECT_NAME} 1)
  pico_enable_stdio_uart(${PROJECT_NAME} 0)
else()
  pico_enable_stdio_usb(${PROJECT_NAME} 0)
  pico_enable_stdio_uart(${PROJECT_NAME} 1)
endif()

# create map/bin/hex/uf2 file in addition to ELF. -> rp2_common.cmake
pico_add_extra_outputs(${PROJECT_NAME})
