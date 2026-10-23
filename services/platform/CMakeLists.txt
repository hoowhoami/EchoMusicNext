add_library(
    echo_platform STATIC
    StartupIntegration.h
    StartupIntegration.cpp
    SingleInstance.h
    SingleInstance.cpp
    GlobalShortcuts.h
    RelaunchService.h
    RelaunchService.cpp
    SystemSleepInhibitor.h
    SystemSleepInhibitor.cpp
    ApplicationUrlHandler.h
    ApplicationUrlHandler.cpp
    WindowBackdrop.h
    WindowBackdrop.cpp
    HyprlandWindowBackdrop.h
    HyprlandWindowBackdrop.cpp
    BrowserWindowLauncher.h
    BrowserWindowLauncher.cpp
)
target_compile_definitions(echo_platform PRIVATE ECHO_APPLICATION_ID="${ECHO_APPLICATION_ID}")
qt_extract_metatypes(echo_platform)
target_include_directories(echo_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(echo_platform PRIVATE ApplicationTranslations.h ApplicationTranslations.cpp)
target_link_libraries(
    echo_platform
    PUBLIC Qt6::Core Qt6::Network Qt6::Gui
    PRIVATE echo_build_options
)
if(APPLE)
    target_sources(
        echo_platform
        PRIVATE MacStartupIntegration.mm MacWindowBackdrop.mm MacSystemSleepInhibitor.mm
                MacGlobalShortcuts.mm MacBrowserWindowLauncher.mm
    )
    target_link_libraries(
        echo_platform PRIVATE "-framework ServiceManagement" "-framework Foundation"
                              "-framework Cocoa" "-framework IOKit" "-framework Carbon"
    )
endif()

if(WIN32)
    target_sources(
        echo_platform PRIVATE WindowsWindowBackdrop.h WindowsWindowBackdrop.cpp
                              WindowsSystemSleepInhibitor.cpp WindowsGlobalShortcuts.cpp
                              WindowsTaskbarLyric.h WindowsTaskbarLyric.cpp
    )
    # comctl32: the strip's window-procedure subclass (GetWindowSubclass &
    # friends) for the taskbar context menu. The library is static, so CMake
    # propagates these as link-only dependencies - consumers of echo_platform
    # (app and the tests) get them transitively.
    target_link_libraries(echo_platform PRIVATE dwmapi comctl32 user32 gdi32 shlwapi ole32
                                                 oleaut32 uuid)
endif()
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ECHO_GIO REQUIRED IMPORTED_TARGET gio-2.0)
    target_sources(echo_platform PRIVATE LinuxSystemSleepInhibitor.cpp LinuxGlobalShortcuts.cpp)
    target_link_libraries(
        echo_platform
        PUBLIC Qt6::DBus
        PRIVATE PkgConfig::ECHO_GIO
    )
    get_target_property(echo_gui_features Qt6::Gui QT_ENABLED_PUBLIC_FEATURES)
    if("xcb" IN_LIST echo_gui_features)
        find_package(X11 REQUIRED)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(ECHO_XCB REQUIRED IMPORTED_TARGET xcb)
        target_link_libraries(echo_platform PRIVATE X11::X11 PkgConfig::ECHO_XCB)
    endif()
endif()
