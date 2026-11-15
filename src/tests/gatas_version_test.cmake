cmake_minimum_required(VERSION 3.18)
include(${CMAKE_CURRENT_LIST_DIR}/../gatas_version.cmake)

function(check_version tag major minor bugfix)
    gatas_version_components("${tag}")
    if(NOT "${BUILD_VERSION_MAJOR}" STREQUAL "${major}" OR
       NOT "${BUILD_VERSION_MINOR}" STREQUAL "${minor}" OR
       NOT "${BUILD_VERSION_BUGFIX}" STREQUAL "${bugfix}")
        message(FATAL_ERROR "Unexpected numeric version for ${tag}")
    endif()
endfunction()

check_version("v3.6.0" 3 6 0)
check_version("v3.6.0-rc1" 3 6 0)
check_version("3.6.0" 3 6 0)
check_version("v3.6.0+build.42" 3 6 0)
check_version("v255.255.255" 255 255 255)
check_version("v0.0.0" 0 0 0)
check_version("UNKNOWN_TAG" 0 0 0)
check_version("not-a-version" 0 0 0)
check_version("v3.6" 0 0 0)
check_version("v3.6.0invalid" 0 0 0)

message(STATUS "Version resolver: all 10 cases passed; selected tag ${GIT_TAG}")
