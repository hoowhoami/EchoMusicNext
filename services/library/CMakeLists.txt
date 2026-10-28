add_library(
    echo_library STATIC
    LibraryCollectionService.cpp
    LibraryCollectionService.h
    SongMetadataService.cpp
    SongMetadataService.h
    EmbeddedCoverCache.cpp
    EmbeddedCoverCache.h
    EmbeddedCoverPresentation.h
    UserPlaylistModel.cpp
    UserPlaylistModel.h
    PlaylistMutationService.cpp
    PlaylistMutationService.h
)
target_include_directories(echo_library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_library
    PUBLIC echo_bridge echo_storage
    PRIVATE echo_build_options
)
