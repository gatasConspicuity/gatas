cmake_minimum_required(VERSION 3.18)
project(utils_tests)
include(FetchContent)

message(STATUS "Building tests.")

add_definitions(-DCATCH_CONFIG_NO_POSIX_SIGNALS)
add_definitions(-DUNIT_TESTING)
add_definitions(-DGATAS_EMAFLOAT_K_FACTOR_5PS=1.9f)
add_definitions(-DGATAS_MAXIMUM_TCP_CLIENTS=4)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)


# Pull in the Catch2 framework.
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.1)
FetchContent_MakeAvailable(Catch2)

# All mocks we might need
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/mocks")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../ace")

# Other dependencies
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/core")

# Add vendor modules
if(${CMAKE_SOURCE_DIR} STREQUAL ${PROJECT_SOURCE_DIR})
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor/etl etlcpp)
endif()


# These examples use the standard separate compilation
set( SOURCES_IDIOMATIC_EXAMPLES
      # Tests
      moreutils_test.cpp
      packetbuffer_test.cpp
      gulp_test.cpp
      ddb_test.cpp
      manchester_test.cpp
      EMA_test.cpp
      bitutils_test.cpp
      circularbuffer_test.cpp
)

string( REPLACE ".cpp" "" BASENAMES_IDIOMATIC_EXAMPLES "${SOURCES_IDIOMATIC_EXAMPLES}" )
set( TARGETS_IDIOMATIC_EXAMPLES ${BASENAMES_IDIOMATIC_EXAMPLES} )

set( ACE_SOURCE_FILES
        ../ace/src/bitutils.cpp
        ../ace/src/encryption.cpp
        ../ace/src/EMA.cpp
        ../ace/src/moreutils.cpp
        ../ace/src/manchester.cpp
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
      #      core
      )
endforeach()

# Only these transport tests use raw-lwIP mocks that assert lock ownership.
add_executable(tcpclient_test
    tcpclient_test.cpp
    ../../airconnect/ace/src/airconnect.cpp
    ../../core/ace/src/basemodule.cpp
    ../../core/ace/src/coreutils.cpp
    ../../core/ace/src/models.cpp)
target_include_directories(tcpclient_test BEFORE PRIVATE network_mocks)
target_include_directories(tcpclient_test PRIVATE ..)
target_link_libraries(tcpclient_test Catch2WithMain etl)
list(APPEND ALL_EXAMPLE_TARGETS tcpclient_test)


list(APPEND CATCH_WARNING_TARGETS ${ALL_EXAMPLE_TARGETS})
set(CATCH_WARNING_TARGETS ${CATCH_WARNING_TARGETS} PARENT_SCOPE)


# Exercise the embedded PIO allocation path against isolated hardware mocks.
add_executable(picopio_test picopio_test.cpp ../ace/src/picopio.cpp)
target_include_directories(picopio_test BEFORE PRIVATE pio_mocks)
target_compile_definitions(picopio_test PRIVATE PICO_RP2040=1)
target_link_libraries(picopio_test PRIVATE Catch2WithMain)
add_custom_command(TARGET picopio_test POST_BUILD COMMAND picopio_test)
