qt_add_qml_module(
    echo_sharing_ui
    STATIC
    URI
    EchoMusic.Sharing
    DEPENDENCIES
    EchoMusic.Music
    EchoMusic.Playback
    EchoMusic.Shell
    SOURCES
    ShareResolveService.h
    ShareResolveService.cpp
    ShareSongRecovery.h
    ShareSongRecovery.cpp
    QML_FILES
    ShareResolvePage.qml
)
target_include_directories(echo_sharing_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_sharing_ui
    PUBLIC echo_sharing echo_shell echo_playback_service
    PRIVATE echo_music
            echo_musicplugin
            echo_shellplugin
            echo_playback
            echo_playbackplugin
            echo_controls
            echo_controlsplugin
            echo_theme
            echo_themeplugin
            echo_build_options
            Qt6::Quick
            Qt6::QuickControls2
)
