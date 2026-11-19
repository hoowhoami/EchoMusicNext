# ICU word breaking is optional for the host and negotiated by plugins. There
# is deliberately no fallback to Qt's differently tailored word boundaries.
option(ECHO_TEXT_SEGMENTATION "Enable ICU-backed plugin word segmentation when available" ON)
add_library(echo_text_segmentation INTERFACE)
if(ECHO_TEXT_SEGMENTATION)
    find_package(ICU 78.2 QUIET COMPONENTS uc i18n data)
endif()
if(ECHO_TEXT_SEGMENTATION
   AND ICU_FOUND
   AND ICU_VERSION VERSION_LESS 79
)
    target_compile_definitions(echo_text_segmentation INTERFACE ECHO_HAS_ICU_SEGMENTATION=1)
    target_link_libraries(echo_text_segmentation INTERFACE ICU::i18n ICU::uc ICU::data)
    # Packaging consumes the libraries actually selected by CMake, rather than
    # guessing from a second ICU_ROOT or the deployment machine's system ICU.
    file(REAL_PATH "${ICU_INCLUDE_DIR}/.." _echo_icu_prefix)
    file(
        GENERATE
        OUTPUT "${CMAKE_BINARY_DIR}/generated/echo-icu-$<CONFIG>.json"
        CONTENT
            "{\n  \"version\": \"${ICU_VERSION}\",\n  \"root\": \"${_echo_icu_prefix}\",\n  \"uc\": \"$<TARGET_FILE:ICU::uc>\",\n  \"i18n\": \"$<TARGET_FILE:ICU::i18n>\",\n  \"data\": \"$<TARGET_FILE:ICU::data>\"\n}\n"
    )
    message(STATUS "Plugin word segmentation: ICU ${ICU_VERSION}")
else()
    file(REMOVE "${CMAKE_BINARY_DIR}/generated/echo-icu-Release.json")
    if(ECHO_DISTRIBUTION_BUILD)
        message(
            FATAL_ERROR
                "Distribution builds require ICU word segmentation. Run scripts/install-icu.py and configure with -DICU_ROOT=<prefix>."
        )
    endif()
    message(STATUS "Plugin word segmentation unavailable: compatible ICU 78.x was not found")
endif()
