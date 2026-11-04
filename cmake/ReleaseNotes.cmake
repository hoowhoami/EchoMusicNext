set(ECHO_CHANGELOG_FILE
    "$ENV{ECHO_CHANGELOG_FILE}"
    CACHE FILEPATH "Optional prepared release notes input"
)
if(NOT ECHO_CHANGELOG_FILE)
    set(ECHO_CHANGELOG_FILE "${PROJECT_SOURCE_DIR}/CHANGELOG.md")
endif()
set(ECHO_PACKAGED_CHANGELOG_FILE "${ECHO_CHANGELOG_FILE}")
if(ECHO_DISTRIBUTION_BUILD)
    find_program(ECHO_RELEASE_NODE NAMES node REQUIRED)
    set(ECHO_PACKAGED_CHANGELOG_FILE "${CMAKE_BINARY_DIR}/generated/CHANGELOG.md")
    execute_process(
        COMMAND "${ECHO_RELEASE_NODE}" "${PROJECT_SOURCE_DIR}/tools/release/changelog.mjs" prepare
                "${ECHO_RELEASE_VERSION}" "${CMAKE_BINARY_DIR}/generated" "${ECHO_CHANGELOG_FILE}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        RESULT_VARIABLE echo_notes_result
        OUTPUT_VARIABLE echo_notes_output
        ERROR_VARIABLE echo_notes_error
    )
    if(NOT echo_notes_result EQUAL 0)
        message(FATAL_ERROR "Cannot prepare release notes: ${echo_notes_error}")
    endif()
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "${ECHO_CHANGELOG_FILE}"
                 "${PROJECT_SOURCE_DIR}/tools/release/changelog.mjs"
    )
endif()
