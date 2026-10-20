add_library(echo_window_resize STATIC WindowResizeSync.h WindowResizeSync.cpp)
target_include_directories(echo_window_resize PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_window_resize
    PUBLIC Qt6::Quick
    PRIVATE echo_build_options
)
if(WIN32)
    target_link_libraries(echo_window_resize PRIVATE dwmapi comctl32 user32)
endif()

qt_add_qml_module(
    echo_shell
    STATIC
    URI
    EchoMusic.Shell
    DEPENDENCIES
    EchoMusic.Playback
    SOURCES
    RouteController.h
    RouteController.cpp
    SidebarRouteCoordinator.h
    SidebarRouteCoordinator.cpp
    ShellSettings.h
    ShellSettings.cpp
    ShortcutService.h
    ShortcutService.cpp
    ShortcutActions.h
    ShortcutActions.cpp
    SidebarLayoutModel.h
    SidebarLayoutModel.cpp
    FloatingPlayerController.h
    FloatingPlayerController.cpp
    FloatingLyricPreferences.h
    FloatingLyricPreferences.cpp
    WindowController.h
    WindowController.cpp
    TrayIconTheme.h
    TrayIconTheme.cpp
    WindowPresentationDiagnostics.h
    WindowPresentationDiagnostics.cpp
    ShellActionModel.h
    ShellActionModel.cpp
    PluginSurfaceModel.h
    PluginSurfaceModel.cpp
    ShellController.h
    ShellController.cpp
    ToastService.h
    ToastService.cpp
    TaskCenterModel.h
    TaskCenterModel.cpp
    PlaylistImportTaskCoordinator.h
    PlaylistImportTaskCoordinator.cpp
    QML_FILES
    WindowCaptionControls.qml
    WindowActionGroup.qml
    WindowActions.qml
    DesktopLyricButton.qml
    DesktopLyricTextToggles.qml
    DesktopLyricWindow.qml
    DesktopLyricUnlockWindow.qml
    MiniLyricBackdrop.qml
    MiniVolumePopup.qml
    MiniPlayerWindow.qml
    RouteHost.qml
    RoutePage.qml
    TitleBar.qml
    Sidebar.qml
    SidebarShortcuts.qml
    SidebarShortcutCard.qml
    SidebarNavigation.qml
    SidebarPlaylistHeader.qml
    SidebarLayoutPopup.qml
    SidebarLayoutSection.qml
    SidebarShortcutPopup.qml
    PluginSurfaceHost.qml
    PlayerBar.qml
    PlayerSongText.qml
    ShellActionButton.qml
    ShellActionDragHandle.qml
    TitleBarMorePopup.qml
    TaskCenterDialog.qml
    UserAgreementDialog.qml
    ToastViewport.qml
    PlayerActionStrip.qml
    PlayerLayoutZone.qml
    PlayerLayoutBoard.qml
    PlayerBarMorePopup.qml
    ShellMoreHeading.qml
)
target_include_directories(echo_shell PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_shell
    PUBLIC Qt6::Quick echo_platform echo_playback_service
    PRIVATE echo_playback
            echo_window_resize
            echo_playbackplugin
            echo_build_options
            echo_theme
            echo_themeplugin
            echo_controls
            echo_controlsplugin
            echo_accountui
            echo_accountuiplugin
            echo_libraryui
            echo_libraryuiplugin
            Qt6::QuickControls2
            Qt6::Widgets
)

if(APPLE)
    enable_language(OBJCXX)
    target_sources(
        echo_shell PRIVATE MacWindowCaption.mm MacFloatingWindow.mm MacApplicationMenu.mm
                           MacDockMenu.mm
    )
    target_link_libraries(echo_shell PRIVATE "-framework Cocoa")
endif()
qt_add_resources(
    echo_shell
    echo_tray_assets
    PREFIX
    /echo/shell
    FILES
    assets/mac-tray.png
    assets/mac-template.png
    assets/mac-template@2x.png
)
target_compile_definitions(echo_shell PRIVATE ECHO_APPLICATION_ID="${ECHO_APPLICATION_ID}")
if(WIN32)
    target_link_libraries(echo_shell PRIVATE user32)
endif()

qt_add_shaders(
    echo_shell
    echo_mini_shaders
    BATCHABLE
    PREFIX
    /qt/qml/EchoMusic/Shell
    FILES
    shaders/mini-lyric-backdrop.frag
)

target_link_libraries(echo_window_resize PRIVATE echo_settings_keys)

target_link_libraries(echo_shell PRIVATE echo_settings_keys)
