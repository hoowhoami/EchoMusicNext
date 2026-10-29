add_library(
    echo_crash_diagnostics STATIC CrashDiagnostics.cpp CrashDiagnostics.h LogFiles.cpp LogFiles.h
                                  LogRedaction.cpp LogRedaction.h
)
target_include_directories(
    echo_crash_diagnostics
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(
    echo_crash_diagnostics
    PUBLIC Qt6::Core
    PRIVATE echo_build_options crashpad::client crashpad::snapshot
)
add_dependencies(echo_crash_diagnostics crashpad_handler)
if(WIN32)
    target_link_libraries(echo_crash_diagnostics PRIVATE shell32)
    add_dependencies(echo_crash_diagnostics crashpad_wer)
endif()
target_compile_definitions(
    echo_crash_diagnostics PRIVATE ECHO_BUILD_CONFIGURATION="$<CONFIG>" NOMINMAX
)
add_library(echo_logging STATIC LogService.cpp LogService.h)
target_include_directories(echo_logging PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(echo_logging PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(
    echo_logging
    PUBLIC Qt6::Core Qt6::Gui echo_crash_diagnostics
    PRIVATE echo_build_options echo_ffi
)
qt_extract_metatypes(echo_logging)

target_link_libraries(echo_crash_diagnostics PUBLIC echo_settings_keys)
target_link_libraries(echo_logging PUBLIC echo_settings_keys)
