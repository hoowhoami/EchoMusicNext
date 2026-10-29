add_library(
    echo_settings_keys STATIC
    StorageFence.h
    StorageFence.cpp
    SettingsStore.h
    SettingsStore.cpp
    PortableSettingsCodec.cpp
    PortableSettingsCodec.h
    SettingsKeys.h
    SettingsKeys.def
)
target_include_directories(echo_settings_keys PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(echo_settings_keys PUBLIC Qt6::Core)
