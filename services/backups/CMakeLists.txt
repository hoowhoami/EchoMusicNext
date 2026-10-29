add_library(
    echo_backups STATIC BackupHost.h BackupHost.cpp SettingsBackupArchive.h
                        SettingsBackupArchive.cpp SettingsBackupRestore.h SettingsBackupRestore.cpp
)
target_include_directories(echo_backups PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_backups
    PUBLIC Qt6::Core
    PRIVATE echo_plugin_packages echo_build_options
)

target_link_libraries(echo_backups PUBLIC echo_settings_keys)
