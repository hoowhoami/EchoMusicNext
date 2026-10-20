cmake_minimum_required(VERSION 3.28)
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET
        "14.4"
        CACHE STRING "Minimum macOS version" FORCE
    )
endif()
project(
    EchoMusicNext
    VERSION 0.1.0
    LANGUAGES C CXX
)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(
    Qt6 6.12.0 EXACT REQUIRED
    COMPONENTS Core
               Gui
               Widgets
               Qml
               Quick
               QuickShapes
               Multimedia
               QuickEffects
               QuickControls2
               QuickDialogs2
               ShaderTools
               Svg
               Sql
               Network
               Concurrent
               Test
               LinguistTools
)
if(UNIX AND NOT APPLE)
    # Requiring this SDK module prevents an X11-only SDK silently satisfying the
    # declared Linux baseline. Actual X11/Wayland sessions still need acceptance.
    find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS WaylandClient DBus)
endif()
qt_standard_project_setup(REQUIRES 6.12)
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
include(CTest)
include(cmake/Rust.cmake)

include(cmake/ProjectOptions.cmake)
set(ECHO_RELEASE_VERSION
    "${PROJECT_VERSION}"
    CACHE STRING "Full release version including prerelease suffix"
)
string(REGEX REPLACE "[-+].*$" "" ECHO_RELEASE_CORE "${ECHO_RELEASE_VERSION}")
if(NOT ECHO_RELEASE_CORE STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR "ECHO_RELEASE_VERSION must use the declared project version")
endif()
option(ECHO_DISTRIBUTION_BUILD "Enable packaged application lifecycle actions" OFF)
include(cmake/BuildIdentity.cmake)
include(cmake/Crashpad.cmake)
add_subdirectory(tools/update-helper)
include(cmake/Curl.cmake)
add_subdirectory(services)
add_subdirectory(modules)
add_subdirectory(app)
if(BUILD_TESTING)
    add_subdirectory(tests/qt)
endif()

# Collect only owned production directories: FetchContent dependencies and
# test fixtures must never become application translation contexts.
set(echo_translation_targets)
foreach(echo_translation_directory IN ITEMS app services modules)
    qt_collect_translation_source_targets(
        echo_directory_translation_targets DIRECTORY
        "${PROJECT_SOURCE_DIR}/${echo_translation_directory}"
    )
    list(APPEND echo_translation_targets ${echo_directory_translation_targets})
endforeach()
list(REMOVE_DUPLICATES echo_translation_targets)
# Catalog extraction excludes tests and third-party sources. lrelease embeds
# committed catalogs; lupdate is an explicit development target, never a build mutation.
qt_add_translations(
    echomusicnext
    SOURCE_TARGETS
    ${echo_translation_targets}
    LUPDATE_TARGET
    echomusicnext_lupdate
    TS_FILES
    translations/echomusicnext_zh_CN.ts
    NO_GENERATE_PLURALS_TS_FILE
    LUPDATE_OPTIONS
    -no-obsolete
    -source-language
    zh_CN
)
