add_library(
    echo_plugin_files STATIC
    PluginFileService.h
    PluginFileService.cpp
    PluginFileWorker_p.h
    PluginFileOperations.cpp
    PluginFileMedia.cpp
    PluginFileDownloads.cpp
    PluginFileTransfer.cpp
)
target_include_directories(
    echo_plugin_files
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(
    echo_plugin_files
    PUBLIC Qt6::Core
    PRIVATE Qt6::Network Qt6::Gui echo_ffi echo_build_options
)
