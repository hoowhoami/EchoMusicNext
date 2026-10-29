add_library(
    echo_storage STATIC
    StorageSnapshot.h
    StorageSnapshot.cpp
    StorageService.cpp
    StorageService.h
    DatabaseWorker.cpp
    DatabaseWorker.h
    StoredPlaylistCover.h
)
target_include_directories(echo_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_storage
    PUBLIC Qt6::Core
    PRIVATE echo_build_options Qt6::Sql
)
