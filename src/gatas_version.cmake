include_guard(GLOBAL)

# git describe selects HEAD's tag when present, otherwise the nearest reachable tag.
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE TAG_RESULT
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT TAG_RESULT EQUAL 0)
    set(GIT_TAG "UNKNOWN_TAG")
endif()

function(gatas_version_components tag)
    set(major 0)
    set(minor 0)
    set(bugfix 0)
    if("${tag}" MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)([-+].*)?$")
        set(major "${CMAKE_MATCH_1}")
        set(minor "${CMAKE_MATCH_2}")
        set(bugfix "${CMAKE_MATCH_3}")
        foreach(component IN ITEMS major minor bugfix)
            if(${component} GREATER 255)
                message(FATAL_ERROR "Version component in ${tag} exceeds one byte")
            endif()
        endforeach()
    endif()
    set(BUILD_VERSION_MAJOR "${major}" PARENT_SCOPE)
    set(BUILD_VERSION_MINOR "${minor}" PARENT_SCOPE)
    set(BUILD_VERSION_BUGFIX "${bugfix}" PARENT_SCOPE)
endfunction()

gatas_version_components("${GIT_TAG}")
if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_definitions(
        -DBUILD_VERSION_MAJOR=${BUILD_VERSION_MAJOR}
        -DBUILD_VERSION_MINOR=${BUILD_VERSION_MINOR}
        -DBUILD_VERSION_BUGFIX=${BUILD_VERSION_BUGFIX}
    )
endif()
