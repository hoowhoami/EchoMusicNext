qt_add_qml_module(
    echo_listen_together
    STATIC
    URI
    EchoMusic.ListenTogether
    DEPENDENCIES
    EchoMusic.Playback
    EchoMusic.Music
    EchoMusic.Shell
    SOURCES
    ListenTogetherUi.h
    ListenTogetherUi.cpp
    QML_FILES
    ListenSurface.qml
    ListenLiveBadge.qml
    ListenTogetherPage.qml
    ListenRoomCard.qml
    ListenSession.qml
    ListenChatPanel.qml
    ListenChatMessage.qml
    ListenRoomDialogs.qml
    ListenSongPickerDialog.qml
)
target_link_libraries(
    echo_listen_together
    PUBLIC echo_music echo_shell echo_playback_service
    PRIVATE echo_bridge
            echo_library
            echo_playback
            echo_playbackplugin
            echo_shellplugin
            echo_musicplugin
            echo_theme
            echo_themeplugin
            echo_controls
            echo_controlsplugin
            echo_build_options
            Qt6::Quick
            Qt6::QuickControls2
)
target_include_directories(echo_listen_together PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_shaders(
    echo_listen_together echo_listen_surfaces PREFIX /qt/qml/EchoMusic/ListenTogether FILES
    shaders/listen-surface.frag
)
