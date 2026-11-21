cmake_minimum_required(VERSION 3.18)
project(adslace_tests LANGUAGES CXX)
include(FetchContent)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.1)
FetchContent_MakeAvailable(Catch2)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    add_subdirectory(../../../vendor/etl etlcpp)
endif()

add_executable(adslace_uplink_test
    adslace_uplink_test.cpp
    ../ace/src/adslace.cpp
    ../../core/ace/src/basemodule.cpp
    ../../core/ace/src/coreutils.cpp
    ../../core/ace/src/models.cpp)
target_compile_features(adslace_uplink_test PRIVATE cxx_std_20)
target_compile_definitions(adslace_uplink_test PRIVATE
    UNIT_TESTING CATCH_CONFIG_NO_POSIX_SIGNALS GATAS_MAXIMUM_TCP_CLIENTS=4)
target_include_directories(adslace_uplink_test PRIVATE
    .. ../../mocks ../../core ../../utils ../../../vendor/ADSL/include)
target_link_libraries(adslace_uplink_test Catch2WithMain etl)
add_custom_command(TARGET adslace_uplink_test POST_BUILD
    COMMAND adslace_uplink_test
    COMMENT "Run ADS-L uplink receive tests")
