cmake_minimum_required(VERSION 3.18)
project(tests)
include(FetchContent)
include(${CMAKE_CURRENT_LIST_DIR}/../../../gatas_version.cmake)

message(STATUS "Building tests.")

add_definitions(-DCATCH_CONFIG_NO_POSIX_SIGNALS)
add_definitions(-DUNIT_TESTING)
add_definitions(-DGATAS_MAXIMUM_TCP_CLIENTS=4)
add_definitions(-DGATAS_MAX_RADIOS=2)
add_definitions(-DGATAS_MAX_SOURCE_PER_RADIO=3)


set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)


# Pull in the Catch2 framework.
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.1)
FetchContent_MakeAvailable(Catch2)


include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/mocks")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/core")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../ace")

if(${CMAKE_SOURCE_DIR} STREQUAL ${PROJECT_SOURCE_DIR})
    # This project is part of a larger build
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor/etl etlcpp)    
endif()


# These examples use the standard separate compilation
set( SOURCES_IDIOMATIC_EXAMPLES
   
   # Tests
   countryregulations2_test.cpp
   # radiotunerrx_test.cpp
)

string( REPLACE ".cpp" "" BASENAMES_IDIOMATIC_EXAMPLES "${SOURCES_IDIOMATIC_EXAMPLES}" )
set( TARGETS_IDIOMATIC_EXAMPLES ${BASENAMES_IDIOMATIC_EXAMPLES} )

set( ACE_SOURCE_FILES
 #    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/ace/messagerouter.cpp
        ../../core/ace/src/basemodule.cpp
        ../../core/ace/src/coreutils.cpp
        ../../core/ace/src/models.cpp

        ../ace/src/countryregulations_v2.cpp
#        ../ace/src/radiotunertx_v2.cpp
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
#      core
      etl
      
      )
endforeach()


list(APPEND CATCH_WARNING_TARGETS ${ALL_EXAMPLE_TARGETS})
set(CATCH_WARNING_TARGETS ${CATCH_WARNING_TARGETS} PARENT_SCOPE)


add_executable(tuner_pause_test
    tuner_pause_test.cpp ${ACE_SOURCE_FILES}
    ../ace/src/radiotunerrx_v2.cpp
    ../ace/src/radiotunertx_v2.cpp)
target_include_directories(tuner_pause_test PRIVATE ../../utils)
target_link_libraries(tuner_pause_test PRIVATE Catch2WithMain etl)
target_compile_features(tuner_pause_test PRIVATE cxx_std_20)
add_custom_command(TARGET tuner_pause_test POST_BUILD COMMAND tuner_pause_test)
