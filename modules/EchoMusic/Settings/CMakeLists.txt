qt_add_qml_module(
    echo_settings
    STATIC
    URI
    EchoMusic.Settings
    SOURCES
    SettingsCatalog.h
    SettingsCatalog.cpp
    ExperimentalSettingsService.h
    ExperimentalSettingsService.cpp
    SettingsServiceTypes.h
    AboutService.h
    AboutService.cpp
    AboutServiceDownload.cpp
    ReleaseManifest.h
    ReleaseManifest.cpp
    SettingsMarkdownRenderer.h
    SettingsMarkdownRenderer.cpp
    SettingsDataService.h
    SettingsDataService.cpp
    SettingsDataPluginApi.cpp
    QML_FILES
    SettingsNotice.qml
    PlayerSettingsSection.qml
    LyricSettingsSection.qml
    CommonLyricSettingsSection.qml
    PageLyricSettingsSection.qml
    DesktopLyricSettingsSection.qml
    TaskbarLyricSettingsSection.qml
    MiniLyricSettingsSection.qml
    LyricColorSettingsRow.qml
    PlaybackSettingsSection.qml
    AudioDeviceSettingsSection.qml
    AudioEffectSettingsSection.qml
    AudioEffectFilesDialog.qml
    AudioEffectFileRow.qml
    SettingsDialog.qml
    DisplayModeControl.qml
    SettingsSection.qml
    SettingsRow.qml
    SettingsDivider.qml
    InterfaceSettingsSection.qml
    WindowSettingsSection.qml
    FontSettingsSection.qml
    ExperimentalSettingsSection.qml
    SettingsUserInfoDialog.qml
    PluginSettingsSection.qml
    NetworkSettingsSection.qml
    ShortcutSettingsSection.qml
    ShortcutRecorder.qml
    AboutSettingsSection.qml
    SettingsTextDialog.qml
    SettingsChangelogDialog.qml
    DataSettingsSection.qml
    SettingsBackupProviderList.qml
    SettingsBackupConfirmation.qml
    CastSettingsSection.qml
    SettingsScrollBody.qml
    SettingsDisclaimerDialog.qml
    SettingsUpdateDialog.qml
)
target_compile_definitions(
    echo_settings PRIVATE ECHO_APP_VERSION="${ECHO_RELEASE_VERSION}"
                          ECHO_DISTRIBUTION_BUILD=$<BOOL:${ECHO_DISTRIBUTION_BUILD}>
)
set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/CHANGELOG.md" PROPERTIES QT_RESOURCE_ALIAS "CHANGELOG.md"
)
qt_add_resources(
    echo_settings settings_changelog PREFIX "/settings" FILES "${PROJECT_SOURCE_DIR}/CHANGELOG.md"
)
target_link_libraries(
    echo_settings
    PRIVATE echo_account
            echo_network
            echo_platform
            echo_logging
            echo_plugins
            echo_plugin_packages
            echo_storage
            echo_pluginsplugin
            echo_build_options
            echo_shell
            echo_shellplugin
            echo_theme
            echo_themeplugin
            echo_controls
            echo_controlsplugin
            echo_playback
            echo_playbackplugin
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            Qt6::QuickControls2
)

target_include_directories(echo_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(echo_settings PRIVATE echo_settings_keys)

target_link_libraries(echo_settings PUBLIC echo_backups)
