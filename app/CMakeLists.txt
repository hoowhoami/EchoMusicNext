qt_add_executable(
    echomusicnext
    main.cpp
    Application.h
    Application.cpp
    ApplicationShutdown.h
    AppContext_p.h
    AppContext.cpp
    ShellBootstrap.cpp
    CoreBootstrap.cpp
    AccountBootstrap.cpp
    RouteCatalog.cpp
    PageWiring.cpp
    PluginWiring.cpp
    ShutdownWiring.cpp
    SmokeBootstrap.cpp
    UiBootstrap.cpp
    RecoveryBootstrap.cpp
)
set(ECHO_APPLICATION_OUTPUT_DIRECTORY
    ""
    CACHE PATH "Optional isolated executable output for active-session validation"
)
if(ECHO_APPLICATION_OUTPUT_DIRECTORY)
    set_target_properties(
        echomusicnext PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${ECHO_APPLICATION_OUTPUT_DIRECTORY}"
    )
endif()
if(NOT APPLE)
    if(WIN32)
        set(ECHO_APP_ICON assets/icon.ico)
    else()
        set(ECHO_APP_ICON assets/linux-icon.png)
    endif()
    # The taskbar lyric strip falls back to qrc:/echo/app/linux-icon.png when
    # nothing is playing, so that file has to be in the resource even where the
    # app icon is something else. On Linux it already is the app icon: listing
    # it twice would put a duplicate entry in the generated .qrc.
    set(ECHO_APP_RESOURCES ${ECHO_APP_ICON})
    if(NOT ECHO_APP_ICON STREQUAL "assets/linux-icon.png")
        list(APPEND ECHO_APP_RESOURCES assets/linux-icon.png)
    endif()
    qt_add_resources(
        echomusicnext echo_application_icon PREFIX /echo/app FILES ${ECHO_APP_RESOURCES}
    )
    target_compile_definitions(
        echomusicnext PRIVATE ECHO_APP_ICON_PATH=":/echo/app/${ECHO_APP_ICON}"
    )
endif()
if(APPLE)
    target_sources(echomusicnext PRIVATE assets/icon.icns)
    set_source_files_properties(assets/icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(echomusicnext PROPERTIES MACOSX_BUNDLE_ICON_FILE icon.icns)
elseif(WIN32)
    target_sources(echomusicnext PRIVATE Application.manifest)
    enable_language(RC)
    configure_file(Application.rc.in ${CMAKE_CURRENT_BINARY_DIR}/Application.rc @ONLY)
    target_sources(
        echomusicnext PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/Application.rc echomusicnext.exe.manifest
    )
elseif(UNIX)
    include(GNUInstallDirs)
    configure_file(
        echomusicnext.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/${ECHO_APPLICATION_ID}.desktop @ONLY
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${ECHO_APPLICATION_ID}.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES echomusicnext-mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    install(
        FILES assets/linux-icon.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME ${ECHO_APPLICATION_ID}.png
    )
    install(
        FILES ../modules/EchoMusic/Shell/assets/tray-symbolic.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/status
        RENAME ${ECHO_APPLICATION_ID}-symbolic.svg
    )
endif()
target_compile_definitions(
    echomusicnext
    PRIVATE ECHO_DISTRIBUTION_BUILD=$<BOOL:${ECHO_DISTRIBUTION_BUILD}>
            ECHO_APPLICATION_ID="${ECHO_APPLICATION_ID}" ECHO_APP_VERSION="${ECHO_RELEASE_VERSION}"
)
target_link_libraries(
    echomusicnext
    PRIVATE echo_startup
            echo_plugins
            echo_pluginsplugin
            echo_plugin_sdkplugin
            echo_playback_service
            echo_playback
            echo_build_options
            echo_account
            echo_accountui
            echo_library
            echo_libraryui
            echo_logging
            echo_network
            echo_storage
            echo_discovery
            echo_discoveryplugin
            echo_listen_together
            echo_listen_togetherplugin
            echo_sharing_ui
            echo_sharing_uiplugin
            echo_playlists
            echo_playlistsplugin
            echo_details
            echo_detailsplugin
            echo_albums
            echo_albumsplugin
            echo_artists
            echo_artistsplugin
            echo_history
            echo_historyplugin
            echo_favorites
            echo_favoritesplugin
            echo_cloud
            echo_cloudplugin
            echo_purchased
            echo_purchasedplugin
            echo_search
            echo_profile
            echo_shell
            echo_theme
            echo_appui
            echo_appuiplugin
            echo_settings
            Qt6::Core
            Qt6::Gui
            Qt6::Widgets
            Qt6::Qml
            Qt6::QuickControls2
)
set_target_properties(
    echomusicnext
    PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
               MACOSX_BUNDLE TRUE
               MACOSX_BUNDLE_GUI_IDENTIFIER "${ECHO_APPLICATION_ID}"
               MACOSX_BUNDLE_BUNDLE_NAME EchoMusicNext
               MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
               MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
               WIN32_EXECUTABLE TRUE
)
echo_copy_rust_runtime(echomusicnext)

add_dependencies(echomusicnext echo-theme-image-worker)
add_dependencies(echomusicnext echo-update-helper)
echo_copy_crash_runtime(echomusicnext)
if(APPLE)
    install(TARGETS crashpad_handler RUNTIME DESTINATION "echomusicnext.app/Contents/Helpers")
else()
    install(TARGETS crashpad_handler RUNTIME DESTINATION bin)
    if(WIN32)
        install(TARGETS crashpad_wer RUNTIME DESTINATION bin)
    endif()
endif()
include(GNUInstallDirs)
install(
    TARGETS echomusicnext
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(APPLE)
    install(TARGETS echo-theme-image-worker echo-update-helper
            RUNTIME DESTINATION "echomusicnext.app/Contents/Helpers"
    )
    install(FILES "$<TARGET_FILE:echo_ffi>" DESTINATION "echomusicnext.app/Contents/Frameworks")
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/CHANGELOG.md"
                  "${PROJECT_SOURCE_DIR}/docs/third-party-notices.md"
            DESTINATION "echomusicnext.app/Contents/Resources/licenses"
    )
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/licenses/"
            DESTINATION "echomusicnext.app/Contents/Resources/licenses/third-party"
    )
else()
    install(TARGETS echo-theme-image-worker echo-update-helper
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES "$<TARGET_FILE:echo_ffi>"
            DESTINATION "$<IF:$<BOOL:${WIN32}>,${CMAKE_INSTALL_BINDIR},${CMAKE_INSTALL_LIBDIR}>"
    )
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/CHANGELOG.md"
                  "${PROJECT_SOURCE_DIR}/docs/third-party-notices.md"
            DESTINATION share/echomusicnext/licenses
    )
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/licenses/"
            DESTINATION share/echomusicnext/licenses/third-party
    )
    set_target_properties(echomusicnext PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()
if(APPLE)
    add_custom_command(
        TARGET echomusicnext
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:echomusicnext>/Contents/Helpers"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:echo-theme-image-worker>"
                "$<TARGET_BUNDLE_DIR:echomusicnext>/Contents/Helpers/echo-theme-image-worker"
    )
else()
    add_custom_command(
        TARGET echomusicnext
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:echo-theme-image-worker>"
                "$<TARGET_FILE_DIR:echomusicnext>"
    )
endif()

target_link_libraries(echomusicnext PRIVATE echo_settings_keys)
