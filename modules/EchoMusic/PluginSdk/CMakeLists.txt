find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS GuiPrivate QuickPrivate)
qt_add_qml_module(
    echo_plugin_sdk
    STATIC
    URI
    EchoMusic.PluginSdk
    VERSION
    1.0
    DEPENDENCIES
    EchoMusic.Shell
    SOURCES
    EchoPluginGraphics.h
    EchoPluginGraphics.cpp
    EchoPluginGraphicsSharing.cpp
    EchoGpuTextureSource.cpp
    graphics/GpuSharedResources.cpp
    graphics/GpuTextureSources.cpp
    graphics/GpuVertexLayout.cpp
    graphics/GpuVertexLayout_p.h
    EchoGpuSurface.h
    EchoGpuSurface.cpp
    graphics/GpuRhi_p.h
    graphics/GpuTextureLayout_p.h
    graphics/GpuRhiResources.cpp
    graphics/GpuSubmissions.cpp
    graphics/GpuRhiTransfers.cpp
    graphics/GpuRhiPasses.cpp
    graphics/GpuRhiRenderer.cpp
    graphics/GpuRhiOutput.cpp
    EchoUiPartRef.h
    EchoUiPartRef.cpp
    EchoPluginUiParts.h
    EchoPluginUiParts.cpp
    EchoUiPartMount.h
    EchoUiPartMount.cpp
    EchoPluginUiStyles.h
    EchoPluginUiStyles.cpp
    EchoPluginAppearance.h
    EchoPluginAppearance.cpp
    EchoPluginAppearanceOverrides.cpp
    EchoPluginViewContext.h
    EchoPluginViewContext.cpp
    PresentationScope.h
    PresentationScope.cpp
    EchoPluginAudio.h
    EchoPluginAudio.cpp
    GlyphRun.h
    GlyphRun.cpp
    GlyphRunCanvas_p.h
    GlyphRunCanvas.cpp
    GlyphRunCurveGeometry_p.h
    GlyphRunCurveGeometry.cpp
    FloatTexture.h
    FloatTexture.cpp
    NativeShaderEffect.h
    NativeShaderEffect.cpp
    PointMesh.h
    PointMesh.cpp
    ImageCapture.h
    ImageCapture.cpp
    EchoPluginText.h
    EchoPluginText.cpp
    EchoPluginAssets.h
    EchoPluginAssets.cpp
    EchoPluginImageDropArea.h
    EchoPluginImageDropArea.cpp
    EchoPluginAssetAnimation.h
    EchoPluginAssetArchive.h
    EchoPluginAssetArchive.cpp
    EchoPluginOperation.h
    EchoPluginOperation.cpp
    EchoPluginPromise.h
    EchoPluginRegistration.h
    EchoPluginRegistration.cpp
    EchoPluginAudioSource.h
    EchoPluginAudioSource.cpp
    EchoPluginPlaylist.h
    EchoPluginPlaylist.cpp
    EchoPluginLyrics.h
    EchoPluginLyrics.cpp
    EchoPluginFiles.h
    EchoPluginFiles.cpp
    EchoPluginFileIo.cpp
    EchoPluginFilePicker.h
    EchoPluginFilePicker.cpp
    EchoPluginDownloads.h
    EchoPluginDownloads.cpp
    EchoPluginPlayer.h
    EchoPluginPlayer.cpp
    EchoPluginPlaybackTransition.h
    EchoPluginPlaybackTransition.cpp
    EchoPluginSnapshot_p.h
    EchoPluginContext.h
    EchoPluginContext.cpp
    EchoPluginPresentation.cpp
    EchoPluginAction.h
    EchoPluginAction.cpp
    EchoPluginCover.h
    EchoPluginCover.cpp
    EchoPluginAppIcons.h
    EchoPluginAppIcons.cpp
    EchoPluginHost.h
    EchoPluginNavigation.cpp
    EchoPluginSettings.cpp
    EchoPluginUi.h
    EchoPluginUi.cpp
    EchoPluginUiEntries.cpp
    EchoPluginWindow.h
    EchoPluginWindow.cpp
    EchoPluginTcp.h
    EchoPluginTcp.cpp
    EchoPluginWebServer.h
    EchoPluginWebServer.cpp
    PluginWebServerWorker_p.h
    PluginWebServerWorker.cpp
    EchoPluginProcess.h
    EchoPluginProcess.cpp
    EchoPluginServer.h
    EchoPluginServer.cpp
    EchoPluginPermissions.h
    EchoPluginPermissions.cpp
    EchoPluginNetwork.h
    EchoPluginNetwork.cpp
    EchoPluginSqlite.h
    EchoPluginSqlite.cpp
    EchoPluginBackups.h
    EchoPluginBackups.cpp
    EchoPluginIo.h
    EchoPluginIo.cpp
    EchoPluginRegistry.h
    EchoPluginRegistry.cpp
    EchoPluginShortcuts.h
    EchoPluginShortcuts.cpp
    EchoPluginToast.h
    EchoPluginToast.cpp
    EchoPluginKugou.h
    EchoPluginKugou.cpp
    EchoPluginBusiness_p.h
    EchoPluginBusinessArguments.cpp
    EchoPluginBusinessCatalog.cpp
    EchoPluginBusinessLibrary.cpp
    EchoPluginBusinessSocial.cpp
    EchoPluginBusinessAccount.cpp
    EchoPluginBusinessMedia.cpp
    EchoPluginLyricEffects.h
    EchoPluginLyricEffects.cpp
    EchoPluginScroll.h
    EchoPluginScroll.cpp
    EchoPluginTasks.h
    EchoPluginTasks.cpp
    EchoPluginTask.h
    EchoPluginTask.cpp
    PluginSdkContract.h
    PluginSdkContract.cpp
    QML_FILES
    EchoPluginPage.qml
    EchoPluginPlaybackControls.qml
    EchoPluginProgress.qml
    EchoPluginLyricsView.qml
)
target_include_directories(echo_plugin_sdk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_plugin_sdk
    PUBLIC Qt6::Qml
           echo_controls
           echo_plugin_packages
           echo_shell
           echo_theme
           echo_playback_service
           echo_account
    PUBLIC echo_plugin_files
    PRIVATE Qt6::Sql
            Qt6::Network
            Qt6::WebSockets
            Qt6::Concurrent
            Qt6::Widgets
            Qt6::GuiPrivate
            Qt6::QuickPrivate
            echo_text_segmentation
            echo_build_options
)

qt_add_resources(echo_plugin_sdk echo_plugin_js PREFIX "/echo/plugins" FILES sdk.mjs)

target_link_libraries(echo_plugin_sdk PUBLIC echo_backups)

if(APPLE)
    target_sources(echo_plugin_sdk PRIVATE MacPluginWindow.mm)
    target_link_libraries(echo_plugin_sdk PRIVATE "-framework Cocoa")
endif()
if(WIN32)
    target_link_libraries(echo_plugin_sdk PRIVATE ws2_32)
    target_compile_definitions(echo_plugin_sdk PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

target_link_libraries(echo_plugin_sdk PRIVATE echo_settings_keys)

qt_add_shaders(
    echo_plugin_sdk
    echo_plugin_glyph_shaders
    BATCHABLE
    PREFIX
    "/qt/qml/EchoMusic/PluginSdk"
    FILES
    shaders/glyph-run.vert
    shaders/glyph-run.frag
)

qt_add_shaders(
    echo_plugin_sdk
    echo_plugin_glyph_canvas_shaders
    PREFIX
    "/qt/qml/EchoMusic/PluginSdk"
    BASE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
    shaders/glyph-canvas/quad.vert
    shaders/glyph-canvas/filter.vert
    shaders/glyph-canvas/rescale.frag
    shaders/glyph-canvas/blur.frag
    shaders/glyph-canvas/ink.frag
    shaders/glyph-canvas/compose.frag
)

qt_add_executable(echo-plugin-asset-worker AssetImageWorker.cpp AssetSvgValidation.cpp)
set_target_properties(echo-plugin-asset-worker PROPERTIES MACOSX_BUNDLE FALSE)
target_include_directories(echo-plugin-asset-worker PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(
    echo-plugin-asset-worker PRIVATE Qt6::Gui Qt6::Svg echo_ffi echo_build_options
)
echo_copy_rust_runtime(echo-plugin-asset-worker)
if(APPLE)
    set_target_properties(
        echo-plugin-asset-worker PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks"
    )
elseif(UNIX)
    set_target_properties(echo-plugin-asset-worker PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()
add_dependencies(echo_plugin_sdk echo-plugin-asset-worker)
target_compile_definitions(
    echo_plugin_sdk PRIVATE ECHO_PLUGIN_ASSET_WORKER_PATH="$<TARGET_FILE:echo-plugin-asset-worker>"
)

# QRhi is intentionally isolated inside the version-pinned host implementation.
target_link_libraries(echo_plugin_sdk PRIVATE Qt6::GuiPrivate)
qt_add_shaders(
    echo_plugin_sdk
    echo_gpu_shaders
    PREFIX
    /echo/graphics
    GLSL
    "300 es,330"
    FILES
    graphics/shaders/composite.vert
    graphics/shaders/composite.frag
)

qt_add_shaders(
    echo_plugin_sdk
    echo_gpu_transfer_shaders
    PREFIX
    /echo/graphics
    GLSL
    "310 es,430"
    FILES
    graphics/shaders/copy-buffer.comp
)
