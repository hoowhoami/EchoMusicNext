qt_add_qml_module(
    echo_plugins
    STATIC
    URI
    EchoMusic.Plugins
    VERSION
    1.0
    DEPENDENCIES
    EchoMusic.PluginSdk
    SOURCES
    PluginRuntime.h
    PluginRuntime.cpp
    PluginManager.h
    PluginManager.cpp
    PluginListModel.h
    PluginGridModel.h
    QML_FILES
    PluginManagementPage.qml
    PluginCard.qml
    PluginActionButton.qml
    PluginMarketplaceSkeleton.qml
    PluginSourceDialog.qml
    PluginGrantsDialog.qml
    PluginDataDialog.qml
    PluginShareResolvePage.qml
    PluginUninstallDialog.qml
    PluginFailureDialog.qml
    PluginSettingsDialog.qml
    PluginLocalInstallOverlay.qml
)
target_include_directories(echo_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_plugins
    PUBLIC echo_plugin_sdk Qt6::Qml
    PRIVATE echo_network
            Qt6::Concurrent
            Qt6::Network
            Qt6::QuickControls2
            echo_controls
            echo_controlsplugin
            echo_theme
            echo_themeplugin
            echo_build_options
)

target_link_libraries(echo_plugins PRIVATE echo_settings_keys)

target_sources(
    echo_plugins PRIVATE PluginRuntimeHelpers_p.h PluginRuntimeActivation.cpp
                         PluginRuntimeMutation.cpp PluginRuntimeDiscovery.cpp PluginRuntimeData.cpp
)

target_sources(
    echo_plugins PRIVATE PluginManagerHelpers_p.h PluginManagerCatalog.cpp
                         PluginManagerInstallation.cpp PluginManagerSharing.cpp
)
