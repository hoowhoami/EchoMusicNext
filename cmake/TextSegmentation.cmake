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
    message(STATUS "Plugin word segmentation: ICU ${ICU_VERSION}")
else()
    message(STATUS "Plugin word segmentation unavailable: compatible ICU 78.x was not found")
endif()
