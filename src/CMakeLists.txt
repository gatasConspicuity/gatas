cmake_minimum_required(VERSION 3.10)
project(MasterProject)
enable_testing()
include(${CMAKE_CURRENT_LIST_DIR}/gatas_version.cmake)

message("===============================================================================")
message("== Build and run unit tests                                                  ==")
message("===============================================================================")

# Add cmake modules
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/etl etlcpp)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/gdl90 GDL90)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/libcrc libcrc)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/libmodes libmodes)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/FLARM flarm::flarm)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/ArduinoJson ArduinoJson)

# Add subdirectories for each project
add_subdirectory(lib/adsbdecoder/adsbdecoder_tests)
add_subdirectory(lib/flarmgatas/flarm_tests)
add_subdirectory(lib/core/core_tests)
add_subdirectory(lib/utils/utils_tests)
add_subdirectory(lib/radiotuner/radiotuner_tests)
add_subdirectory(lib/aircrafttracker/aircrafttracker_tests)
add_subdirectory(lib/config/config_tests)
add_subdirectory(lib/gdl90service/gdl90service_tests)
add_subdirectory(lib/gps/gps_tests)

# Register every desktop test executable, including targets without a POST_BUILD
# hook, and rerun them with CTest even when Ninja has nothing to rebuild.
function(register_desktop_tests directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type "${target}" TYPE)
        if(type STREQUAL "EXECUTABLE" AND directory MATCHES "^${CMAKE_CURRENT_SOURCE_DIR}/lib/")
            add_test(NAME "${target}" COMMAND "$<TARGET_FILE:${target}>")
            set_tests_properties("${target}" PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target}>")
        endif()
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        register_desktop_tests("${child}")
    endforeach()
endfunction()

register_desktop_tests("${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME gatas_version_test COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/gatas_version_test.cmake")
