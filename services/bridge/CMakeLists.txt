add_library(
    echo_bridge STATIC ApiClient.cpp ApiClient.h ListenTogetherApi.h ApiClientListenTogether.cpp
)
target_sources(
    echo_bridge
    PRIVATE ApiDomains_p.h
            AuthClient.cpp
            CatalogClient.cpp
            CloudClient.cpp
            SocialClient.cpp
            LibraryClient.cpp
            MediaClient.cpp
)
target_include_directories(echo_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_bridge
    PUBLIC echo_device_identity Qt6::Gui
    PRIVATE echo_build_options echo_ffi
)
target_include_directories(echo_bridge PRIVATE ${PROJECT_SOURCE_DIR}/include)
