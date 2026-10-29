# ApiClient consumes DeviceIdentity; registration consumes ApiClient. Keep the
# identity value type separate so these targets do not form a dependency cycle.
add_library(echo_device_identity STATIC DeviceIdentity.cpp DeviceIdentity.h)
target_include_directories(echo_device_identity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_device_identity
    PUBLIC Qt6::Core
    PRIVATE echo_build_options
)

add_library(
    echo_network STATIC
    DeviceRegistrationService.cpp
    DeviceRegistrationService.h
    NetworkConfiguration.cpp
    NetworkConfiguration.h
    NetworkBootstrap.cpp
    NetworkBootstrap.h
    NetworkPreferences.cpp
    NetworkPreferences.h
    ProxyRouting.cpp
    ProxyRouting.h
    ProxyFailureCache.h
    ProxyResolver.cpp
    ProxyResolver.h
    SystemNetworkMonitor.cpp
    SystemNetworkMonitor.h
    NetworkTransport.cpp
    NetworkTransport.h
    ProxySocketBridge.cpp
    ProxySocketBridge.h
    HttpProxyRequest.cpp
    HttpProxyRequest.h
    HttpRequestBody.cpp
    HttpRequestBody.h
    NetworkSocketBuffer.h
    NetworkMediaConnection.cpp
    NetworkMediaConnection.h
)
target_include_directories(echo_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_network
    PUBLIC echo_bridge echo_credentials
    PRIVATE echo_build_options Qt6::Network Qt6::Qml Qt6::Concurrent CURL::libcurl
)
qt_extract_metatypes(echo_network)
if(WIN32)
    target_sources(echo_network PRIVATE WindowsSystemNetworkMonitor.cpp)
    target_link_libraries(echo_network PRIVATE winhttp advapi32 iphlpapi ws2_32)
endif()
if(APPLE)
    target_sources(echo_network PRIVATE MacSystemNetworkMonitor.mm)
    target_link_libraries(echo_network PRIVATE "-framework SystemConfiguration")
endif()
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Libproxy REQUIRED IMPORTED_TARGET "libproxy-1.0>=0.4.17")
    pkg_check_modules(Gio REQUIRED IMPORTED_TARGET GLOBAL gio-2.0)
    target_sources(
        echo_network PRIVATE LinuxSystemProxy.cpp LinuxSystemProxy.h LinuxSystemNetworkMonitor.cpp
    )
    target_link_libraries(echo_network PRIVATE PkgConfig::Libproxy PkgConfig::Gio)
endif()

target_link_libraries(echo_device_identity PUBLIC echo_settings_keys)
target_link_libraries(echo_network PUBLIC echo_settings_keys)
