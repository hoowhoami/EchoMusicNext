# VERSION is the only application version source. Rust crates remain independent.
set(echo_version_file "${CMAKE_CURRENT_LIST_DIR}/../VERSION")
file(READ "${echo_version_file}" echo_file_version)
string(REGEX REPLACE "\r?\n$" "" echo_file_version "${echo_file_version}")
if(DEFINED ECHO_RELEASE_VERSION AND NOT ECHO_RELEASE_VERSION STREQUAL echo_file_version)
    message(FATAL_ERROR "ECHO_RELEASE_VERSION differs from VERSION; remove the override")
endif()
if(NOT "$ENV{RELEASE_VERSION}" STREQUAL "" AND NOT "$ENV{RELEASE_VERSION}" STREQUAL
                                               echo_file_version
)
    message(FATAL_ERROR "RELEASE_VERSION differs from VERSION")
endif()
if("$ENV{GITHUB_REF}" MATCHES "^refs/tags/" AND NOT "$ENV{GITHUB_REF}" STREQUAL
                                                "refs/tags/v${echo_file_version}"
)
    message(FATAL_ERROR "Release tag differs from VERSION")
endif()
set(ECHO_RELEASE_VERSION "${echo_file_version}")
if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "${echo_version_file}"
    )
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
