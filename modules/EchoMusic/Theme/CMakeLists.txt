# Decode unbounded original dimensions outside the GUI process. Only this
# helper raises Qt's global image allocation cap; source pixel/file limits remain.
qt_add_executable(
    echo-theme-image-worker ThemeImageWorker.cpp ThemeImageStore.cpp ThemeImageStore.h
)
target_link_libraries(echo-theme-image-worker PRIVATE Qt6::Gui echo_build_options)
set_target_properties(echo-theme-image-worker PROPERTIES MACOSX_BUNDLE FALSE WIN32_EXECUTABLE TRUE)
set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(
    echo_theme
    STATIC
    URI
    EchoMusic.Theme
    DEPENDENCIES
    QtQuick
    QML_FILES
    Theme.qml
    ThemeColors.qml
    ThemeBackground.qml
    ExtensionBackground.qml
    ExtensionGradient.qml
    SOURCES
    AppearanceConfiguration.h
    AppearanceConfiguration.cpp
    AppearanceCatalog.h
    AppearanceCatalog.cpp
    AppearanceHost.h
    AppearanceHost.cpp
    ThemeStore.h
    ThemeStore.cpp
    UiTypography.h
    UiTypography.cpp
    AccentPalette.h
    AccentPalette.cpp
    ThemeImageStore.h
    ThemeImageStore.cpp
    ThemeImageImport.cpp
)
target_include_directories(echo_theme PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_theme
    PUBLIC Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent Qt6::Multimedia
    PRIVATE echo_build_options
)

add_dependencies(echo_theme echo-theme-image-worker)
qt_add_shaders(
    echo_theme echo_atmosphere_shaders PREFIX /qt/qml/EchoMusic/Theme FILES shaders/atmosphere.frag
)
target_compile_definitions(
    echo_theme PRIVATE ECHO_THEME_WORKER_PATH="$<TARGET_FILE:echo-theme-image-worker>"
)

target_link_libraries(echo-theme-image-worker PRIVATE echo_settings_keys)

# Theme public headers expose SettingsStore; consumers need its include path.
target_link_libraries(echo_theme PUBLIC echo_settings_keys)
if(UNIX AND NOT APPLE)
    target_sources(echo_theme PRIVATE LinuxSystemTheme.cpp LinuxSystemTheme.h)
    target_link_libraries(echo_theme PRIVATE Qt6::DBus)
endif()
