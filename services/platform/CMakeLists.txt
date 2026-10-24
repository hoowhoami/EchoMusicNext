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
    # Windows shell identity (AppUserModelID + its Start Menu shortcut); the
    # implementation is a no-op elsewhere.
    WindowsAppIdentity.h
    WindowsAppIdentity.cpp
    # Re-delivers the pointer position so a surface that became interactive
    # without pointer movement still gets hover; a no-op elsewhere.
    PointerHoverRefresh.h
    PointerHoverRefresh.cpp
    TaskbarProgressState.h
    TaskbarProgressState.cpp
    # Shared Win32 plumbing for the taskbar adapters (COM acquisition and GDI
    # conversions); callers only exist inside Q_OS_WIN blocks.
    WindowsTaskbarInterop.h
    WindowsTaskbarInterop.cpp
    # Shell owns this adapter on every platform; its implementation supplies
    # non-Windows no-ops as well as the Windows mount.
    WindowsTaskbarLyric.h
    WindowsTaskbarLyric.cpp
    WindowsTaskbarProgress.h
    WindowsTaskbarProgress.cpp
    WindowsTaskbarThumbar.h
    WindowsTaskbarThumbar.cpp
    WindowsTaskbarThumbnail.h
    WindowsTaskbarThumbnail.cpp
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
    )
    # comctl32: the strip's window-procedure subclass (GetWindowSubclass &
    # friends) for the taskbar context menu. The library is static, so CMake
    # propagates these as link-only dependencies - consumers of echo_platform
    # (app and the tests) get them transitively.
    target_link_libraries(
        echo_platform
        PRIVATE dwmapi
                comctl32
                user32
                gdi32
                shlwapi
                shell32
                propsys
                ole32
                oleaut32
                uuid
    )
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
