add_library(echo_cache INTERFACE)
target_include_directories(echo_cache INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(echo_cache INTERFACE Qt6::Core)
qt_add_library(
    echo_session_cache STATIC SessionDatabase.h SessionDatabase.cpp JsonRecordStore.h
    JsonRecordStore.cpp
)
target_include_directories(echo_session_cache PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_session_cache
    PUBLIC echo_cache Qt6::Sql
    PRIVATE echo_build_options
)
