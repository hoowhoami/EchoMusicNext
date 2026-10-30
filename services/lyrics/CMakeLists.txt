add_library(echo_lyrics STATIC LyricParser.h LyricParser.cpp TtmlParser_p.h TtmlParser.cpp)
target_include_directories(echo_lyrics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_lyrics
    PUBLIC Qt6::Core
    PRIVATE Qt6::Concurrent echo_build_options
)

if(WIN32)
    # The supported Windows baseline has no system zlib. Reuse the already
    # content-pinned Chromium zlib build, without adding a second copy/runtime.
    target_include_directories(echo_lyrics SYSTEM PRIVATE "${echo_crashpad_zlib_SOURCE_DIR}")
    target_link_libraries(echo_lyrics PRIVATE crashpad::zlib)
else()
    find_package(ZLIB REQUIRED)
    target_link_libraries(echo_lyrics PRIVATE ZLIB::ZLIB)
endif()
