find_package(Git QUIET)
set(ECHO_SOURCE_REVISION unknown)
set(ECHO_SOURCE_STATE unknown)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE ECHO_SOURCE_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE ECHO_SOURCE_DIRTY
    )
    if(ECHO_SOURCE_DIRTY)
        set(ECHO_SOURCE_STATE dirty)
    else()
        set(ECHO_SOURCE_STATE clean)
    endif()
endif()
string(TIMESTAMP ECHO_BUILD_TIME "%Y%m%dT%H%M%SZ" UTC)
set(ECHO_BUILD_ID "${ECHO_BUILD_TIME}-${ECHO_SOURCE_REVISION}-${ECHO_SOURCE_STATE}")
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/EchoBuildInfo.h.in"
    "${CMAKE_BINARY_DIR}/generated/EchoBuildInfo.h" @ONLY
)
# Use the toolchain's normal configuration flags without adding Release debug
# information. ELF build IDs identify native crash modules without debug data.
if(UNIX AND NOT APPLE)
    add_link_options(-Wl,--build-id=sha1)
endif()
