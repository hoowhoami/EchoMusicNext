qt_add_qml_module(
    echo_discovery
    STATIC
    URI
    EchoMusic.Discovery
    DEPENDENCIES
    EchoMusic.Playback
    EchoMusic.Music
    SOURCES
    RecognitionCapture.h
    RecognitionCapture.cpp
    RecognitionService.h
    RecognitionService.cpp
    DiscoveryService.h
    DiscoveryService.cpp
    ExploreService.h
    ExploreService.cpp
    DiscoveryRouteCoordinator.h
    DiscoveryRouteCoordinator.cpp
    QML_FILES
    RecognizePage.qml
    RecognitionOrb.qml
    RecognitionMatchRow.qml
    HomePage.qml
    HomeFeatureCard.qml
    HomeSongCard.qml
    DiscoverySongsPage.qml
    DiscoveryThemedCover.qml
    DailyRecommendPage.qml
    RankingPage.qml
    ExplorePage.qml
    ExploreHeader.qml
    FreeListenPage.qml
    FreeListenCover.qml
    DiscoverFlowPage.qml
    DiscoverFlowArtwork.qml
    DiscoverFlowBackdrop.qml
    DiscoverFlowInfo.qml
    PersonalFmPage.qml
    PersonalFmVinyl.qml
)
qt_add_shaders(
    echo_discovery echo_discover_shaders PREFIX /qt/qml/EchoMusic/Discovery FILES
    shaders/discover-artwork.frag
)
target_link_libraries(
    echo_discovery
    PUBLIC echo_music
    PRIVATE echo_bridge
            echo_account
            echo_playback_service
            echo_playback
            echo_playbackplugin
            echo_shell
            echo_shellplugin
            echo_music
            echo_musicplugin
            echo_theme
            echo_themeplugin
            echo_controls
            echo_controlsplugin
            echo_build_options
            Qt6::Quick
            Qt6::QuickControls2
)
target_include_directories(echo_discovery PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(echo_discovery PRIVATE echo_settings_keys)
