add_library(
    echo_library STATIC
    PlaybackTrack.cpp
    PlaybackTrack.h
    PlaybackPayloadLease.h
    SongRowFacts.cpp
    SongRowFacts.h
    SongRightsFacts.h
    SongPresentation.cpp
    SongPresentation.h
    SongSourceCache.cpp
    SongSourceCache.h
    FavoriteSongIndex.cpp
    FavoriteSongIndex.h
    LibraryCollectionService.cpp
    LibraryCollectionService.h
    SongDatasetStore.cpp
    SongDatasetStore.h
    SongObservationIndex.cpp
    SongObservationIndex.h
    SongMetadataService.cpp
    SongMetadataService.h
    EmbeddedCoverCache.cpp
    EmbeddedCoverCache.h
    EmbeddedCoverPresentation.h
    ArtistCoverPresentation.h
    UserPlaylistModel.cpp
    UserPlaylistModel.h
    PlaylistMutationService.cpp
    PlaylistMutationService.h
)
target_include_directories(echo_library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_library
    PUBLIC echo_bridge echo_storage echo_cache echo_session_cache
    PRIVATE echo_build_options
)
