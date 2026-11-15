cmake_minimum_required(VERSION 3.18)

project(bmp580 VERSION 0.0.0 LANGUAGES CXX)
include(${CMAKE_CURRENT_SOURCE_DIR}/../gatas_module.cmake)

gatas_add_module(
    SOURCE_FILES
        ace/src/bmp580.cpp
    PICO_EXTRA_LIBS
        hardware_i2c
)
