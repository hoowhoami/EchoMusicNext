# Release tags supply the application version. Rust crate versions are independent.
set(echo_version_ref "$ENV{GITHUB_REF}")
set(echo_version_tag "")
if(echo_version_ref MATCHES "^refs/tags/")
    string(REGEX REPLACE "^refs/tags/" "" echo_version_tag "${echo_version_ref}")
elseif(
    echo_version_ref STREQUAL ""
    AND NOT DEFINED ECHO_RELEASE_VERSION
    AND "$ENV{RELEASE_VERSION}" STREQUAL ""
)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match --match "v[0-9]*" HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
            OUTPUT_VARIABLE echo_version_tag
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        )
    endif()
endif()
if(NOT echo_version_tag STREQUAL "")
    if(NOT echo_version_tag MATCHES "^v")
        message(FATAL_ERROR "Release tags must start with v")
    endif()
    string(SUBSTRING "${echo_version_tag}" 1 -1 echo_tag_version)
    if(DEFINED ECHO_RELEASE_VERSION AND NOT ECHO_RELEASE_VERSION STREQUAL echo_tag_version)
        message(FATAL_ERROR "ECHO_RELEASE_VERSION differs from the release tag")
    endif()
    if(NOT "$ENV{RELEASE_VERSION}" STREQUAL "" AND NOT "$ENV{RELEASE_VERSION}" STREQUAL
                                                   echo_tag_version
    )
        message(FATAL_ERROR "RELEASE_VERSION differs from the release tag")
    endif()
    set(ECHO_RELEASE_VERSION "${echo_tag_version}")
elseif(NOT DEFINED ECHO_RELEASE_VERSION)
    if(NOT "$ENV{RELEASE_VERSION}" STREQUAL "")
        set(ECHO_RELEASE_VERSION "$ENV{RELEASE_VERSION}")
    else()
        set(ECHO_RELEASE_VERSION "0.1.0-dev")
    endif()
endif()
# project(VERSION) and native numeric metadata use the core; About/update use
# the full SemVer. Reject leading zeroes in numeric prerelease identifiers too.
set(echo_semver
    "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?(\\+[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?$"
)
if(NOT ECHO_RELEASE_VERSION MATCHES "${echo_semver}")
    message(FATAL_ERROR "Application version must be strict SemVer: ${ECHO_RELEASE_VERSION}")
endif()
set(ECHO_RELEASE_CORE "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
string(REGEX REPLACE "\\+.*$" "" echo_prerelease "${ECHO_RELEASE_VERSION}")
if(echo_prerelease MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+-(.*)$")
    string(REPLACE "." ";" echo_identifiers "${CMAKE_MATCH_1}")
    foreach(echo_identifier IN LISTS echo_identifiers)
        if(echo_identifier MATCHES "^0[0-9]+$")
            message(FATAL_ERROR "Numeric prerelease identifiers cannot have leading zeroes")
        endif()
    endforeach()
endif()
