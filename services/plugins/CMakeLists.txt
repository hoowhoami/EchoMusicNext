add_library(
    echo_plugin_packages STATIC
    PluginDatabaseWorker.h
    PluginDatabaseWorker.cpp
    PluginPackage.h
    PluginPackage.cpp
    PluginPaths.h
    PluginPaths.cpp
    PluginInstallationStore.h
    PluginInstallationStore.cpp
    PluginInstallTransaction.h
    PluginInstallTransaction.cpp
    PluginSourceEntry.h
    PluginSourceEntry.cpp
    PluginMarketplaceStats.h
    PluginMarketplaceStats.cpp
    PluginDependencyGraph.h
    PluginDependencyGraph.cpp
)
target_include_directories(echo_plugin_packages PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_plugin_packages
    PUBLIC Qt6::Core
    PRIVATE Qt6::Sql
    PRIVATE echo_build_options echo_text_segmentation
)

# Rust extraction stays behind this service; UI modules never include its ABI.
target_link_libraries(echo_plugin_packages PRIVATE echo_ffi)
target_include_directories(echo_plugin_packages PRIVATE ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(echo_plugin_packages PUBLIC echo_storage)
