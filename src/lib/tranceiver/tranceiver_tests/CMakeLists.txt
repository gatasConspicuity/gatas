cmake_minimum_required(VERSION 3.18)
project(tranceiver_tests LANGUAGES C CXX)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    add_subdirectory(../../../vendor/etl etlcpp)
endif()
include(FetchContent)
FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.5.1)
FetchContent_MakeAvailable(Catch2)
set(driver ../lr2021driver)
add_executable(tranceiver_test
    tranceiver_test.cpp
    ../../utils/ace/src/manchester.cpp
    ../../core/ace/src/basemodule.cpp
    ../../core/ace/src/coreutils.cpp
    ../../core/ace/src/models.cpp
    ../ace/src/abstracttransceiver.cpp
    ../ace/src/radiohardware.cpp
    ../ace/src/rxdataframequeue.cpp
    ../ace/src/sx1262.cpp
    ../ace/src/sx1262_hal.cpp
    ../sx1262driver/src/sx126x.c
    ../ace/src/lr2021.cpp
    ../ace/src/lr2021_hal.cpp
    ${driver}/src/lr20xx_system.c
    ${driver}/src/lr20xx_regmem.c
    ${driver}/src/lr20xx_pram.c
    ${driver}/src/lr20xx_radio_common.c
    ${driver}/src/lr20xx_radio_fifo.c
    ${driver}/src/lr20xx_radio_fsk.c
    ${driver}/src/lr20xx_radio_lora.c
    ${driver}/src/lr20xx_workarounds.c)
target_compile_features(tranceiver_test PRIVATE cxx_std_20)
target_compile_definitions(tranceiver_test PRIVATE UNIT_TESTING GATAS_MAXIMUM_TCP_CLIENTS=4 GATAS_MAX_RADIOS=2 GATAS_MAX_SOURCE_PER_RADIO=3)
target_include_directories(tranceiver_test PRIVATE ../../mocks ../../core ../../utils ../ ${driver}/inc)
target_compile_options(tranceiver_test PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/platform.hpp>")
target_link_libraries(tranceiver_test PRIVATE Catch2WithMain etl)
add_custom_command(TARGET tranceiver_test POST_BUILD COMMAND tranceiver_test)
