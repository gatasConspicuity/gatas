cmake_minimum_required(VERSION 3.18)

project(tranceiver VERSION 0.0.0 LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../gatas_module.cmake)

gatas_add_module(
    SOURCE_FILES
        ace/src/abstracttransceiver.cpp
        ace/src/radiohardware.cpp
        ace/src/lr2021.cpp
        ace/src/lr2021_hal.cpp
        ace/src/sx1262.cpp
        ace/src/sx1262_hal.cpp
        ace/src/rxdataframequeue.cpp

        lr2021driver/src/lr20xx_pram.c
        lr2021driver/src/lr20xx_system.c
        lr2021driver/src/lr20xx_regmem.c
        lr2021driver/src/lr20xx_radio_common.c
        lr2021driver/src/lr20xx_radio_fifo.c
        lr2021driver/src/lr20xx_radio_fsk.c
        lr2021driver/src/lr20xx_radio_lora.c
        lr2021driver/src/lr20xx_workarounds.c
        lr2021driver/src/lr20xx_driver_version.c

        # Driver from Semtech
        sx1262driver/src/sx126x.c
        sx1262driver/src/sx126x_lr_fhss.c
        sx1262driver/src/sx126x_driver_version.c
        sx1262driver/src/lr_fhss_mac.c
    TARGET_LINK_LIBS
        utils
    PICO_EXTRA_LIBS
        hardware_spi
)

target_include_directories(tranceiver PRIVATE lr2021driver/inc)
