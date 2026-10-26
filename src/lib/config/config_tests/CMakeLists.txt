cmake_minimum_required(VERSION 3.18)
project(tests)
include(FetchContent)
include(${CMAKE_CURRENT_LIST_DIR}/../../../gatas_version.cmake)

message(STATUS "Building tests.")

add_definitions(-DCATCH_CONFIG_NO_POSIX_SIGNALS)
add_definitions(-DUNIT_TESTING)
add_definitions(-DMOCK_REAL_MUTEXES)
add_definitions(-DGATAS_MAXIMUM_TCP_CLIENTS=4)
add_definitions(-D__flash_binary_end=0x4000000)
add_definitions(-DXIP_BASE=0x6000000)
add_definitions(-DGATAS_FLASH_SIGNATURE=250801)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

configure_file(test.json test.json COPYONLY)

# Pull in the Catch2 framework.
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.1)
FetchContent_MakeAvailable(Catch2)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/core")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../ace")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/mocks")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/utils")

# Add vendor modules
if(${CMAKE_SOURCE_DIR} STREQUAL ${PROJECT_SOURCE_DIR})
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor/etl etlcpp)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor/ArduinoJson ArduinoJson)
endif()

add_executable(jsonallocator_test jsonallocator_test.cpp)
target_link_libraries(jsonallocator_test PRIVATE Catch2WithMain ArduinoJson)

# These examples use the standard separate compilation
set( SOURCES_IDIOMATIC_EXAMPLES
   config_test.cpp
)

string( REPLACE ".cpp" "" BASENAMES_IDIOMATIC_EXAMPLES "${SOURCES_IDIOMATIC_EXAMPLES}" )
set( TARGETS_IDIOMATIC_EXAMPLES ${BASENAMES_IDIOMATIC_EXAMPLES} )

set( ACE_SOURCE_FILES
        ../../core/ace/src/basemodule.cpp
        ../../core/ace/src/models.cpp
        ../../core/ace/src/coreutils.cpp
        ../../utils/ace/src/moreutils.cpp
        ../../utils/ace/src/cobs.cpp
        ../ace/src/config.cpp
        ../../idle/ace/src/idle.cpp
        ../../gatasconnect/ace/src/gatasconnect.cpp
)


foreach(name ${TARGETS_IDIOMATIC_EXAMPLES})
  add_executable(${name} ${ACE_SOURCE_FILES} ${name}.cpp)

  # Run test for each target
  set(UNIT_TEST ${name})
  add_custom_command(
    TARGET ${UNIT_TEST}
    COMMENT "Run tests"
    POST_BUILD
    COMMAND ${UNIT_TEST})
endforeach()

set(ALL_EXAMPLE_TARGETS
  ${TARGETS_IDIOMATIC_EXAMPLES}
)

foreach( name ${ALL_EXAMPLE_TARGETS} )
    target_link_libraries(
      ${name}
      Catch2WithMain
      etl
      ArduinoJson
)
endforeach()


list(APPEND CATCH_WARNING_TARGETS ${ALL_EXAMPLE_TARGETS})
set(CATCH_WARNING_TARGETS
    ${CATCH_WARNING_TARGETS}
    PARENT_SCOPE)
