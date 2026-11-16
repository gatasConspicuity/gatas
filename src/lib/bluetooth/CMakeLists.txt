cmake_minimum_required(VERSION 3.18)

project(bluetooth VERSION 0.0.0 LANGUAGES CXX)
include(${CMAKE_CURRENT_SOURCE_DIR}/../gatas_module.cmake)

gatas_add_module(
    SOURCE_FILES
        ace/src/bluetooth.cpp
    TARGET_LINK_LIBS
        utils
    PICO_EXTRA_LIBS
        pico_btstack_ble
        pico_btstack_cyw43
        pico_cyw43_arch_lwip_sys_freertos
)

pico_btstack_make_gatt_header(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ace/gatas_gatt.gatt"
)
