qt_add_qml_module(
    echo_cloud
    STATIC
    URI
    EchoMusic.Cloud
    DEPENDENCIES
    EchoMusic.Controls
    EchoMusic.Music
    EchoMusic.Shell
    EchoMusic.Theme
    SOURCES
    CloudService.h
    CloudService.cpp
    CloudUploadService.h
    CloudUploadService.cpp
    CloudUploadFile.h
    CloudUploadFile.cpp
    QML_FILES
    CloudPage.qml
    CloudUploadDialog.qml
)
target_link_libraries(
    echo_cloud
    PUBLIC echo_music echo_shell echo_playback_service
    PRIVATE echo_ffi
            Qt6::Concurrent
            echo_bridge
            echo_build_options
            echo_theme
            echo_themeplugin
            echo_controls
            echo_controlsplugin
            echo_musicplugin
            echo_shellplugin
            echo_playback
            echo_playbackplugin
            Qt6::Quick
            Qt6::QuickControls2
)
target_include_directories(echo_cloud PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(echo_cloud PRIVATE ${PROJECT_SOURCE_DIR}/include)
