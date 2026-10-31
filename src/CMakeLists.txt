cmake_minimum_required(VERSION 3.10)
project(MasterProject)

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
add_subdirectory(lib/adslace/adslace_tests)
