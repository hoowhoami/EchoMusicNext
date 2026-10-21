set_directory_properties(PROPERTIES QT_EXCLUDE_FROM_TRANSLATION ON)
qt_add_executable(echo-translation-test TranslationTest.cpp)
target_link_libraries(echo-translation-test PRIVATE echo_platform Qt6::Qml Qt6::Test)
qt_add_translations(
    echo-translation-test
    LUPDATE_TARGET
    echo_translation_fixture_lupdate
    SOURCE_TARGETS
    echo-translation-test
    TS_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/fixture_en_US.ts"
    RESOURCE_PREFIX
    /test-i18n
    NO_GENERATE_PLURALS_TS_FILE
)
add_test(NAME translations COMMAND echo-translation-test)
set_tests_properties(translations PROPERTIES TIMEOUT 15)

qt_add_executable(echo-application-lifecycle-test ApplicationLifecycleTest.cpp)
target_include_directories(echo-application-lifecycle-test PRIVATE ${PROJECT_SOURCE_DIR}/app)
target_link_libraries(echo-application-lifecycle-test PRIVATE Qt6::Test echo_build_options)
add_test(NAME application-lifecycle COMMAND echo-application-lifecycle-test)
set_tests_properties(application-lifecycle PROPERTIES TIMEOUT 15)
option(ECHO_BUILD_LIFECYCLE_SANITIZER_TEST "Build the focused lifecycle ASan/UBSan probe" OFF)
if(ECHO_BUILD_LIFECYCLE_SANITIZER_TEST)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "The focused sanitizer probe requires Clang or GCC")
    endif()
    qt_add_executable(echo-application-lifecycle-sanitized-test ApplicationLifecycleTest.cpp)
    target_include_directories(
        echo-application-lifecycle-sanitized-test PRIVATE ${PROJECT_SOURCE_DIR}/app
    )
    target_compile_options(
        echo-application-lifecycle-sanitized-test PRIVATE -fsanitize=address,undefined
                                                          -fno-omit-frame-pointer
    )
    target_link_options(
        echo-application-lifecycle-sanitized-test PRIVATE -fsanitize=address,undefined
    )
    target_link_libraries(
        echo-application-lifecycle-sanitized-test PRIVATE Qt6::Test echo_build_options
    )
    set_target_properties(
        echo-application-lifecycle-sanitized-test
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/architecture-validation"
    )
    add_test(NAME application-lifecycle-sanitized COMMAND echo-application-lifecycle-sanitized-test)
    set_tests_properties(application-lifecycle-sanitized PROPERTIES TIMEOUT 20)
endif()

qt_add_executable(echo-foundation-test FoundationTest.cpp)
qt_add_executable(echo-back-to-top-test BackToTopTest.cpp)
qt_add_qml_module(echo-back-to-top-test URI EchoMusic.BackToTopTests QML_FILES BackToTopHarness.qml)
target_link_libraries(
    echo-back-to-top-test
    PRIVATE echo_controls
            echo_controlsplugin
            echo_theme
            echo_themeplugin
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Test
)
add_test(NAME back-to-top COMMAND echo-back-to-top-test)
set_tests_properties(back-to-top PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
qt_add_executable(echo-about-service-test AboutServiceTest.cpp)
qt_add_qml_module(
    echo-about-service-test URI EchoMusic.UpdateTests QML_FILES UpdateDialogHarness.qml
)
target_link_libraries(
    echo-about-service-test
    PRIVATE echo_settings
            echo_settingsplugin
            echo_controls
            echo_controlsplugin
            echo_theme
            echo_themeplugin
            echo_network
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Test
)
add_test(NAME about-service COMMAND echo-about-service-test)
set_tests_properties(about-service PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
find_program(ECHO_NODE_EXECUTABLE NAMES node REQUIRED)
add_test(NAME update-release-contract COMMAND ${ECHO_NODE_EXECUTABLE} --test
                                              ${PROJECT_SOURCE_DIR}/tools/release/contract.test.mjs
)
add_test(
    NAME update-helper
    COMMAND
        ${CMAKE_COMMAND} -E env
        "ECHO_UPDATE_HELPER_PROBE=$<TARGET_FILE:echo-update-helper-test-probe>"
        ${ECHO_NODE_EXECUTABLE} --test ${PROJECT_SOURCE_DIR}/tools/update-helper/helper.test.mjs
)
set_tests_properties(update-helper PROPERTIES TIMEOUT 30)
qt_add_executable(echo-shortcut-service-test ShortcutServiceTest.cpp)
if(APPLE)
    set_target_properties(
        echo-shortcut-service-test
        PROPERTIES MACOSX_BUNDLE TRUE
                   MACOSX_BUNDLE_GUI_IDENTIFIER "${ECHO_APPLICATION_ID}.shortcut-probe"
                   MACOSX_BUNDLE_BUNDLE_NAME "EchoMusicNext shortcut probe"
    )
endif()
qt_add_executable(echo-settings-backup-test SettingsBackupTest.cpp)
target_link_libraries(
    echo-settings-backup-test PRIVATE echo_settings echo_storage Qt6::Sql Qt6::Test
)
add_test(NAME settings-backup COMMAND echo-settings-backup-test)
set_tests_properties(settings-backup PROPERTIES TIMEOUT 45)
target_link_libraries(echo-shortcut-service-test PRIVATE echo_shell Qt6::Test)
add_test(NAME shortcut-service COMMAND echo-shortcut-service-test)
set_tests_properties(shortcut-service PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
qt_add_executable(echo-cast-discovery-test CastDiscoveryTest.cpp)
qt_add_executable(echo-cast-media-test CastMediaTest.cpp)
target_link_libraries(
    echo-cast-media-test PRIVATE echo_playback_service echo_network Qt6::Gui Qt6::Network Qt6::Test
)
add_test(NAME cast-media COMMAND echo-cast-media-test)
set_tests_properties(cast-media PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
target_link_libraries(echo-cast-discovery-test PRIVATE echo_playback_service Qt6::Test)
add_test(NAME cast-discovery COMMAND echo-cast-discovery-test)
set_tests_properties(cast-discovery PROPERTIES TIMEOUT 20)
qt_add_executable(echo-single-instance-test SingleInstanceTest.cpp)
target_link_libraries(echo-single-instance-test PRIVATE echo_platform Qt6::Test)
add_test(NAME single-instance COMMAND echo-single-instance-test)
set_tests_properties(single-instance PROPERTIES TIMEOUT 30)
qt_add_executable(echo-relaunch-test RelaunchTest.cpp)
set_target_properties(echo-relaunch-test PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
target_link_libraries(echo-relaunch-test PRIVATE echo_platform Qt6::Test)
add_test(NAME relaunch COMMAND echo-relaunch-test)
set_tests_properties(relaunch PROPERTIES TIMEOUT 30)
target_link_libraries(
    echo-foundation-test PRIVATE echo_account echo_library echo_network echo_storage Qt6::Test
)
add_test(NAME foundation COMMAND echo-foundation-test)
qt_add_executable(echo-account-persistence-test AccountPersistenceTest.cpp)
target_link_libraries(
    echo-account-persistence-test PRIVATE echo_account echo_storage Qt6::Gui Qt6::Sql Qt6::Test
)
add_test(NAME account-persistence COMMAND echo-account-persistence-test)
set_tests_properties(
    account-persistence PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45
)
echo_copy_rust_runtime(echo-account-persistence-test)
qt_add_executable(echo-playlist-mutation-test PlaylistMutationTest.cpp)
target_link_libraries(echo-playlist-mutation-test PRIVATE echo_library Qt6::Test)
add_test(NAME playlist-mutations COMMAND echo-playlist-mutation-test)
qt_add_executable(echo-sidebar-navigation-test SidebarNavigationTest.cpp)
target_link_libraries(echo-sidebar-navigation-test PRIVATE echo_shell echo_libraryui Qt6::Test)
add_test(NAME sidebar-navigation COMMAND echo-sidebar-navigation-test)
echo_copy_rust_runtime(echo-sidebar-navigation-test)
qt_add_executable(echo-song-playback-ui-test SongPlaybackUiTest.cpp)
target_link_libraries(
    echo-song-playback-ui-test
    PRIVATE echo_search
            echo_searchplugin
            echo_music
            echo_musicplugin
            echo_theme
            echo_themeplugin
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Test
)
add_test(NAME song-playback-ui COMMAND echo-song-playback-ui-test)
set_tests_properties(
    song-playback-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90 RUN_SERIAL TRUE
)
echo_copy_rust_runtime(echo-song-playback-ui-test)
qt_add_executable(echo-search-model-test SearchModelTest.cpp)
target_link_libraries(echo-search-model-test PRIVATE echo_search echo_storage Qt6::Test)
add_test(NAME search-model COMMAND echo-search-model-test)
add_test(NAME qml-smoke COMMAND echomusicnext --smoke-test --log-dir
                                "${CMAKE_CURRENT_BINARY_DIR}/logs"
)
set_tests_properties(qml-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
echo_copy_rust_runtime(echo-search-model-test)
echo_copy_rust_runtime(echo-foundation-test)
echo_copy_rust_runtime(echo-playlist-mutation-test)

# Private binding enumeration is an opt-in diagnostic, not a regular test dependency.
option(ECHO_ENABLE_QML_BINDING_PROBE "Enable Qt-private QML binding diagnostics" OFF)
qt_add_executable(echo-controls-ui-test ControlsUiTest.cpp)
if(ECHO_ENABLE_QML_BINDING_PROBE)
    find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS QmlPrivate)
    target_link_libraries(echo-controls-ui-test PRIVATE Qt6::QmlPrivate)
    target_compile_definitions(echo-controls-ui-test PRIVATE ECHO_ENABLE_QML_BINDING_PROBE=1)
endif()
target_include_directories(echo-controls-ui-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(
    echo-controls-ui-test
    PRIVATE controls/ControlsUiTest.h
            controls/ControlsUiTestFixture.h
            controls/ControlsUiInputs.cpp
            controls/ControlsUiPopups.cpp
            controls/ControlsUiTypography.cpp
            controls/ControlsUiMemory.cpp
            controls/ControlsUiEffects.cpp
            controls/ControlsUiBehavior.cpp
)
qt_add_executable(echo-tab-count-alignment-test TabCountAlignmentTest.cpp)
qt_add_executable(echo-background-banding-test BackgroundBandingTest.cpp)
target_link_libraries(
    echo-background-banding-test PRIVATE echo_theme echo_themeplugin Qt6::Quick Qt6::Test
)
add_test(NAME background-banding COMMAND echo-background-banding-test)
set_tests_properties(
    background-banding PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45
)
target_link_libraries(
    echo-tab-count-alignment-test PRIVATE echo_controls echo_controlsplugin echo_theme Qt6::Quick
                                          Qt6::QuickControls2 Qt6::Test
)
add_test(NAME tab-count-alignment COMMAND echo-tab-count-alignment-test)
set_tests_properties(
    tab-count-alignment PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45
)
qt_add_qml_module(
    echo-controls-ui-test
    URI
    EchoMusic.ControlsTests
    VERSION
    1.0
    QML_FILES
    ControlsHarness.qml
    ProgressHarness.qml
    SelectHarness.qml
    BackdropHarness.qml
    PopupScrollHarness.qml
    TypographyHarness.qml
    BadgeTagHarness.qml
    TextTagAlignmentHarness.qml
    DialogLayoutHarness.qml
    ControlSizeHarness.qml
)
target_link_libraries(
    echo-controls-ui-test PRIVATE echo_controls echo_controlsplugin echo_theme Qt6::Quick
                                  Qt6::QuickControls2 Qt6::Test
)
set(ECHO_CONTROLS_TEST_GROUPS controls-ui-inputs controls-ui-popups controls-ui-typography
                              controls-ui-effects controls-ui-behavior
)
add_test(
    NAME controls-ui-inputs
    COMMAND
        echo-controls-ui-test continuousAngleWheelPreservesDistanceAndPixelInput
        wheelCancellationAndMotionPreferences pageWheelRegionsPreserveNativeEvents
        imageDensityTracksScaleWithoutTrackingScroll
        selectProjectionKeepsTypedValuesAndTracksModelChanges
        selectSearchMultiKeyboardPlacementAndVirtualization
        numberInputCommitsAndClampsAcrossAllInputPaths
)
add_test(
    NAME controls-ui-popups
    COMMAND
        echo-controls-ui-test scaledPopupTextKeepsNativePixelResolution
        floatingPanelEdgesAndMenuHeight menusDrawersAndFileDialogsReleaseAfterClose
        businessPopupsAreCreatedOnDemandAndDestroyedAfterExit
        tooltipPopupsExistOnlyDuringTheirTrigger
        nativeMenuCancelsVisibleAndDelayedHintsUntilNewHover dialogHeaderBodyAndFooterShareInsets
        drawerFixedActionsSurviveLongTitlesAndVisibilityChanges
        hintsAvoidTriggersAndWindowEdgesAcrossZoom popupsCancelVisibleAndPendingHints
        popupAndPageTracksShareRightInsetAndRetainDragMapping
)
add_test(
    NAME controls-ui-typography
    COMMAND
        echo-controls-ui-test badgeIconAndCountUseSameInkCenter
        textTagAlignmentAcrossProductionRecipes plainTextRetainsInkWithoutTagLayouts
        sharedControlsRetireUnusedContent badgesAndTagsKeepDistinctContracts
        globalTypographyRetainsFallbackAndLocalStyles centeredContentAcrossSizesAndPadding
        iconTextContentCentersRenderedInk allBundledGlyphsTintAndPreserveAlpha
)
add_test(
    NAME controls-ui-effects
    COMMAND
        echo-controls-ui-test scrollContentCannotPaintOverRoundedCornersOrBorder
        frostedCornersRetainAntialiasCoverage colorPresetsKeepVisibleEdgesInBothThemes
        backdropSourceIsWindowLocalAndReleasedWhenHidden backdropSamplerTracksBothItemTrees
        floatingSurfaceMatchesRendererCapabilities floatingFrostSamplesTheSceneAndStopsOnHide
        floatingEffectsAreOwnedByVisibleSurfaces floatingBorderCompositesOverItsOwnFill
        themeCustomColorHoverAndRenderedPixels sourceActionSurfacesAndSelectStates
)
add_test(
    NAME controls-ui-behavior
    COMMAND
        echo-controls-ui-test listLocationPreservesVisibleRowsAndChecksOcclusion
        coverKeepsNativeResolutionUnderAncestorZoom coverCropAndRoundedPixels
        iconRasterTracksZoomAndReleasesWhenHidden clearActionIsOneUserEditButProgrammaticClearIsNot
        boundedListAutoHeightSurvivesReopenAndVariableRows progressClimaxTicksAndWaitPreview
        playPauseTransitionKeepsEndpointsAndInterrupts progressBusyRendersAndRetiresNativeAnimation
        sourcePauseKeepsComparableInkToPlay disabledIconHoverIsOptInAndNeverActivates
        shadowsReleaseTheirNativeMaterials
        sourceControlRolesKeepOrdinaryActionsAndLargeFormsDistinct
)

target_compile_definitions(
    echo-controls-ui-test
    PRIVATE ECHO_ICON_SOURCE_DIR="${PROJECT_SOURCE_DIR}/modules/EchoMusic/Controls/icons"
)
set_tests_properties(
    ${ECHO_CONTROLS_TEST_GROUPS} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45
)
add_test(
    NAME controls-backdrop-software
    COMMAND
        echo-controls-ui-test backdropSourceIsWindowLocalAndReleasedWhenHidden
        backdropSamplerTracksBothItemTrees floatingSurfaceMatchesRendererCapabilities
        floatingFrostSamplesTheSceneAndStopsOnHide floatingEffectsAreOwnedByVisibleSurfaces
)
set_tests_properties(
    controls-backdrop-software
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30
)

qt_add_executable(echo-shell-test ShellTest.cpp)
qt_add_executable(echo-window-resize-test WindowResizeTest.cpp)
target_link_libraries(echo-window-resize-test PRIVATE echo_window_resize Qt6::Qml Qt6::Test)
add_test(NAME window-resize-sync COMMAND echo-window-resize-test)
set_tests_properties(
    window-resize-sync PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30
)
qt_add_executable(echo-native-menus-test NativeMenusTest.cpp)
if(APPLE)
    target_sources(echo-native-menus-test PRIVATE NativeMenusMacTest.mm)
    target_sources(echo-native-menus-test PRIVATE ../../app/assets/icon.icns)
    set_source_files_properties(
        ../../app/assets/icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources
    )
    set_target_properties(
        echo-native-menus-test
        PROPERTIES MACOSX_BUNDLE TRUE
                   MACOSX_BUNDLE_GUI_IDENTIFIER "${ECHO_APPLICATION_ID}.native-menu-probe"
                   MACOSX_BUNDLE_BUNDLE_NAME "EchoMusicNext"
                   MACOSX_BUNDLE_ICON_FILE icon.icns
    )
endif()
target_link_libraries(echo-native-menus-test PRIVATE echo_shell Qt6::Widgets Qt6::Test)
add_test(NAME native-menus COMMAND echo-native-menus-test)
set_tests_properties(native-menus PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
echo_copy_rust_runtime(echo-native-menus-test)
qt_add_qml_module(
    echo-shell-test
    URI
    EchoMusic.ShellTests
    VERSION
    1.0
    QML_FILES
    ShellHarness.qml
    ProbePage.qml
)
target_link_libraries(
    echo-shell-test
    PRIVATE echo_appui
            echo_appuiplugin
            echo_shell
            echo_search
            echo_theme
            Qt6::QuickControls2
            Qt6::Test
)
target_link_libraries(echo-shell-test PRIVATE echo_accountui echo_libraryui)
add_test(NAME shell COMMAND echo-shell-test)
# Includes the complete production UI matrices and popup destruction cycles.
# Retain every assertion; bound the expanded suite rather than aborting at 30s.
set_tests_properties(shell PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
qt_add_executable(echo-shell-actions-test ShellActionTest.cpp)
target_link_libraries(
    echo-shell-actions-test PRIVATE echo_shell echo_shellplugin echo_controls Qt6::Quick
                                    Qt6::QuickControls2 Qt6::Test
)
add_test(NAME shell-actions COMMAND echo-shell-actions-test)
set_tests_properties(shell-actions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
echo_copy_rust_runtime(echo-shell-test)

qt_add_executable(echo-library-pages-test LibraryPagesTest.cpp)
target_link_libraries(
    echo-library-pages-test
    PRIVATE echo_theme
            echo_themeplugin
            echo_favorites
            echo_favoritesplugin
            echo_cloud
            echo_cloudplugin
            echo_purchased
            echo_purchasedplugin
            echo_playback_service
            echo_playback
            echo_playbackplugin
            echo_library
            echo_libraryui
            echo_account
            echo_accountui
            echo_accountuiplugin
            echo_shell
            echo_shellplugin
            echo_music
            echo_musicplugin
            echo_controls
            echo_controlsplugin
            echo_bridge
            echo_network
            echo_storage
            echo_build_options
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Sql
            Qt6::Test
)
add_test(NAME library-pages COMMAND echo-library-pages-test)
set_tests_properties(library-pages PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
echo_copy_rust_runtime(echo-library-pages-test)

qt_add_executable(echo-search-ui-test SearchUiTest.cpp)
qt_add_qml_module(
    echo-search-ui-test
    URI
    EchoMusic.SearchUiTests
    VERSION
    1.0
    QML_FILES
    PageScrollHarness.qml
    SongGeometryHarness.qml
)
target_link_libraries(
    echo-search-ui-test
    PRIVATE echo_appui
            echo_appuiplugin
            echo_shell
            echo_search
            echo_theme
            Qt6::QuickControls2
            Qt6::Test
)
target_link_libraries(echo-search-ui-test PRIVATE echo_accountui echo_libraryui)
add_test(NAME search-ui COMMAND echo-search-ui-test)
set_tests_properties(search-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
qt_add_executable(echo-theme-test ThemeTest.cpp)
if(APPLE)
    target_sources(echo-theme-test PRIVATE NativeWindowAppearanceProbe.mm)
    target_link_libraries(echo-theme-test PRIVATE "-framework Cocoa")
endif()
target_compile_definitions(
    echo-theme-test
    PRIVATE ECHO_THEME_GOLDEN="${PROJECT_SOURCE_DIR}/tests/fixtures/theme/accent-goldens.json"
)
target_link_libraries(
    echo-theme-test
    PRIVATE echo_appui
            echo_appuiplugin
            echo_shell
            echo_search
            echo_theme
            echo_controls
            Qt6::QuickControls2
            Qt6::Test
)
target_link_libraries(
    echo-theme-test PRIVATE echo_accountui echo_libraryui echo_playlists echo_playlistsplugin
)
add_test(NAME theme COMMAND echo-theme-test)
set_tests_properties(theme PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
echo_copy_rust_runtime(echo-search-ui-test)
echo_copy_rust_runtime(echo-theme-test)

qt_add_executable(echo-verification-ui-test VerificationUiTest.cpp)
qt_add_qml_module(
    echo-verification-ui-test
    URI
    EchoMusic.VerificationTests
    VERSION
    1.0
    QML_FILES
    VerificationHarness.qml
)
target_link_libraries(
    echo-verification-ui-test
    PRIVATE echo_accountui
            echo_accountuiplugin
            echo_theme
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            Qt6::Qml
            Qt6::QuickControls2
            Qt6::Test
)
add_test(NAME verification-ui COMMAND echo-verification-ui-test)
set_tests_properties(
    verification-ui PROPERTIES ENVIRONMENT
                               "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic" TIMEOUT 45
)
echo_copy_rust_runtime(echo-verification-ui-test)

qt_add_executable(echo-desktop-pages-test DesktopPagesTest.cpp)
target_include_directories(echo-desktop-pages-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(
    echo-desktop-pages-test
    PRIVATE desktop/DesktopPagesTest.h
            desktop/DesktopPagesTestFixture.h
            desktop/DesktopPagesSettings1.cpp
            desktop/DesktopPagesSettings2.cpp
            desktop/DesktopPagesPlayback1.cpp
            desktop/DesktopPagesPlayback2.cpp
            desktop/DesktopPagesPlayback3.cpp
            desktop/DesktopPagesNavigation1.cpp
            desktop/DesktopPagesNavigation2.cpp
            desktop/DesktopPagesPresentation1.cpp
            desktop/DesktopPagesPresentation2.cpp
            desktop/DesktopPagesPopups.cpp
            desktop/DesktopPagesLibrary.cpp
)
if(APPLE)
    target_sources(echo-desktop-pages-test PRIVATE NativeWindowAppearanceProbe.mm)
    target_link_libraries(echo-desktop-pages-test PRIVATE "-framework Cocoa")
    add_executable(echo-macos-fullscreen-host MACOSX_BUNDLE MacFullscreenHost.mm)
    set_target_properties(
        echo-macos-fullscreen-host
        PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "${ECHO_APPLICATION_ID}.tests.fullscreen-host"
                   QT_QML_MODULE_NO_IMPORT_SCAN TRUE
    )
    target_compile_options(echo-macos-fullscreen-host PRIVATE -fobjc-arc)
    target_link_libraries(echo-macos-fullscreen-host PRIVATE "-framework Cocoa")
    qt_add_executable(
        echo-mac-floating-test MacFloatingWindowTest.cpp NativeWindowAppearanceProbe.mm
    )
    target_link_libraries(
        echo-mac-floating-test PRIVATE echo_shell Qt6::Widgets Qt6::Test "-framework Cocoa"
    )
    set_target_properties(echo-mac-floating-test PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    add_dependencies(echo-mac-floating-test echo-macos-fullscreen-host)
    add_test(NAME mac-floating-windows COMMAND echo-mac-floating-test)
    set_tests_properties(
        mac-floating-windows PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45
    )
endif()
qt_add_executable(echo-network-consumers-test NetworkConsumersTest.cpp)
target_link_libraries(
    echo-network-consumers-test PRIVATE echo_network Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test
)
set_target_properties(echo-network-consumers-test PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
add_test(NAME network-consumers COMMAND echo-network-consumers-test)
set_tests_properties(
    network-consumers PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45
)
echo_copy_rust_runtime(echo-network-consumers-test)
target_link_libraries(
    echo-desktop-pages-test
    PRIVATE echo_playback
            echo_artists
            echo_artistsplugin
            echo_albums
            echo_albumsplugin
            echo_details
            echo_detailsplugin
            echo_startup
            echo_appui
            echo_playlists
            echo_playlistsplugin
            echo_plugins
            echo_logging
            echo_appuiplugin
            echo_profile
            echo_profileplugin
            echo_settings
            echo_settingsplugin
            echo_shell
            echo_accountui
            echo_libraryui
            echo_search
            echo_theme
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Test
)
set(ECHO_DESKTOP_TEST_GROUPS desktop-pages-settings desktop-pages-playback desktop-pages-navigation
                             desktop-pages-presentation desktop-pages-popups desktop-pages-library
)
add_test(
    NAME desktop-pages-settings
    COMMAND
        echo-desktop-pages-test startupUsesRealFailuresAndReleasesItsEntirePage
        recoveryStartupWindowRetriesAndExitsWithoutBusinessOwners
        settingsSearchClearRestoresCategoriesAndKeepsFocus
        lyricFontSettingsApplyToProductionWindows
        lyricVariantSettingUsesProductionSelectAndReleasesPopup miniWidthPreferencesClampAndRestore
        lyricSettingsShareCoverPickerAndExcludeTaskbar fullscreenShortcutReachesModalPlaybackPage
        productionExperimentalIdentitySettings productionExperimentalLoggingSettings
        productionNetworkSettingsDraftApplyResetAndTheme
        productionPlayerEngineSettingsDecimalInputAndLifecycle
        productionVariableDialogSizingReopens settingsDefaultsAndSourceEffectFileManager
        playbackZoomShortcutsFollowSharedScale windowSettingsNeverHideWithoutNativeRecovery
        startupEntryEscapesBothDesktopParsingLayers
        sidebarLayoutPersistsAndKeepsGroupsAndOneShortcut sidebarShortcutCollapseFrames
        sidebarShortcutHoverDragAcrossThemesAndScales sidebarShortcutHoverDragAndFocus
        signedOutAccountPopoverAndOddShortcutWidths remainingSettingsProductionSections
        settingsFilterAndUnsupportedProviderNeverLoadStub
        globalFontUsesInstalledFamilyAndRestoresPreference
        productionProfileSettingsAndSidebarOverlays
)
add_test(
    NAME desktop-pages-playback
    COMMAND
        echo-desktop-pages-test playbackQueueLocationSkipsVisibleCurrentSong
        playbackFallbackControlsUseProductionUi floatingLyricsFollowRealAudioAndStopMotionOnPause
        miniProgressVolumeBackdropFavoriteAndDesktopBadge
        floatingPreferencesAndFilteredTimelineKeepIdentity
        desktopLyricsMatchSourceChromeAndHoverUnlock
        nativeFloatingWindowsShareStateAndReleaseAcrossCycles
        nativePlaybackSurvivesRepeatedAppearanceUpdates
        productionHeaderWindowActionsStayAtRightEdge playbackQueueEmptyIndicatorAndCloseIcon
        playbackChromeMatchesSource playbackLoadingRevealsFinalPlayPauseIcon
        playbackToolbarsMatchSourceAcrossWidths productionPlaybackPopoverSpacingAndEffectMarkers
        productionEmbeddedLyricsFromActualReader productionLyricPreviewKeepsSymmetricInsets
        productionLyricKeywordSearchFromLiveNativeApi
        productionLyricAutomaticFallbackFromLiveNativeApi productionLyricSelectionFromLiveNativeApi
        realPlaybackSurfacesAndBoundedFiftyThousandQueue
)
add_test(
    NAME desktop-pages-navigation
    COMMAND
        echo-desktop-pages-test productionPopupsFollowRequestOwnership
        playerMetadataLinksUseProductionRoutes profileWritesRejectInvalidDraftAndCancelOlderRead
        avatarUploadRejectsInvalidFilesAndIgnoresOldAccountCompletion
        productionCollectionAndSocialEntrypoints socialAndDeviceCachesBelongToAccountAndTab
        productionSidebarImportEntranceAndModes productionSidebarOrderEditor
        sidebarHomeAndDragTransaction sidebarHistoryNavigationRevealsPlaylistSection
        productionSidebarScrollbar productionSidebarCardsMenusAndLayout
        accountProfileAndGradeRejectOldAccountResult
        fixedProfileSocialAndSidebarActionsKeepIntrinsicWidths productionProfileNameTagsStayAligned
        productionProfileFieldsAndMembershipDates productionAccountSurfacesAndAnchors
        profileEditorKeepsEqualColumnsAndDateActionsClear
        profileEditorValidationFeedbackStaysVisibleWithoutScrolling
        profileDevicesKeepInlineStatusAndBoundedViewport profileRelatedScrollBarsUseNativeMapping
)
add_test(
    NAME desktop-pages-presentation
    COMMAND
        echo-desktop-pages-test audibleTransitionPublishesOneToastThroughShell
        nativePlaybackFailurePublishesVisibleToast nativeOutputDeviceSelectHasOneDefaultChoice
        committedTrackClimaxFromLiveNativeApi productionEffectPanelAlignmentAndCompactContentTabs
        productionResolvedQualityMarksPickerAndToolbar productionAddToReopenStableHeight
        productionAddToHeaderAlignment preferencesPreserveUnknownTagsValidateAndKeepFailedDraft
        missingMembershipCodesKeepProductsWithoutInventingKnownRights
        missingPreferenceFactsDoNotBecomeExplicitClears liveSameAvatarThroughQtService
        screenshotTargetSnapshotAndDuplicateConfirmation
        incompleteAndRepeatedLibraryIdsKeepEveryRow
        libraryConsumersAcceptPageEchoAndCountVariations identityProjectionAndSessionIsolation
        productionIdentitySummaryAndAvatarCorners liveContentBlacklistThroughQtService
        contentBlacklistPaginationRemovalAndSessionIsolation
        productionContentBlacklistKeepsPreferencesDraftAndVirtualScroll
        deviceRemovalRequiresConfirmationRefreshAndCurrentSession
)
add_test(NAME desktop-pages-popups COMMAND echo-desktop-pages-test
                                           productionQualityPopupContainsAndScrollsAllOptions
)
add_test(
    NAME desktop-pages-library
    COMMAND
        echo-desktop-pages-test livePlaylistImportsThroughQtService
        playlistImportBackgroundResultsStopAndSessionIsolation
        playlistOrderKeepsFixedSlotsAndRejectsChangedVersions
        songMenuBlacklistUsesIndexLoadAndSharedWriteChannel
)

set_tests_properties(
    ${ECHO_DESKTOP_TEST_GROUPS} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45
)
echo_copy_rust_runtime(echo-desktop-pages-test)

qt_add_executable(echo-logging-test LoggingTest.cpp)
set_target_properties(echo-logging-test PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
target_link_libraries(echo-logging-test PRIVATE echo_logging echo_bridge Qt6::Qml Qt6::Test)
add_test(NAME logging COMMAND echo-logging-test)
set_tests_properties(logging PROPERTIES TIMEOUT 30)
echo_copy_rust_runtime(echo-logging-test)
qt_add_executable(echo-crash-probe CrashProbe.cpp)
target_link_libraries(echo-crash-probe PRIVATE echo_logging Qt6::Core)
echo_copy_rust_runtime(echo-crash-probe)
echo_copy_crash_runtime(echo-crash-probe)
qt_add_executable(echo-crash-diagnostics-test CrashDiagnosticsTest.cpp)
target_link_libraries(
    echo-crash-diagnostics-test PRIVATE echo_crash_diagnostics Qt6::Core Qt6::Test crashpad::client
)
add_dependencies(echo-crash-diagnostics-test echo-crash-probe)
add_test(NAME crash-diagnostics COMMAND echo-crash-diagnostics-test)
set_tests_properties(crash-diagnostics PROPERTIES TIMEOUT 180)

qt_add_executable(echo-playback-test PlaybackTest.cpp)
qt_add_executable(echo-player-engine-settings-test PlayerEngineSettingsTest.cpp)
target_link_libraries(echo-player-engine-settings-test PRIVATE echo_playback_service Qt6::Test)
add_test(NAME player-engine-settings COMMAND echo-player-engine-settings-test)
set_tests_properties(player-engine-settings PROPERTIES TIMEOUT 45)
echo_copy_rust_runtime(echo-player-engine-settings-test)
target_link_libraries(echo-playback-test PRIVATE echo_playback_service Qt6::Test)
add_test(NAME playback COMMAND echo-playback-test)
set_tests_properties(playback PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
echo_copy_rust_runtime(echo-playback-test)

qt_add_executable(echo-toast-service-test ToastServiceTest.cpp)
target_link_libraries(echo-toast-service-test PRIVATE echo_shell Qt6::Test)
add_test(NAME toast-service COMMAND echo-toast-service-test)
set_tests_properties(toast-service PROPERTIES TIMEOUT 15)
qt_add_executable(echo-plugin-runtime-test PluginRuntimeTest.cpp)
target_include_directories(echo-plugin-runtime-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(
    echo-plugin-runtime-test
    PRIVATE plugins/PluginRuntimeTest.h
            plugins/PluginRuntimeTestFixture.h
            plugins/PluginRuntimeManagement1.cpp
            plugins/PluginRuntimeManagement2.cpp
            plugins/PluginRuntimeRuntime.cpp
            plugins/PluginRuntimeShutdown.cpp
            plugins/PluginRuntimeDependencies.cpp
            plugins/PluginRuntimeCapabilities.cpp
            plugins/PluginRuntimeUiParts.cpp
)
target_link_libraries(
    echo-plugin-runtime-test
    PRIVATE echo_platform
            echo_plugins
            echo_pluginsplugin
            echo_plugin_sdkplugin
            Qt6::Test
            Qt6::Sql
            Qt6::Widgets
            Qt6::Network
)
set(ECHO_PLUGINS_TEST_GROUPS
    plugin-runtime-management plugin-runtime-runtime plugin-runtime-shutdown
    plugin-runtime-dependencies plugin-runtime-capabilities plugin-runtime-ui-parts
)
add_test(
    NAME plugin-runtime-management
    COMMAND
        echo-plugin-runtime-test sharedPluginVersionWarnings
        sharedPluginPagePresentationAndRedirect catalogInstallationSourceIsNotCompatibility
        rootDirectoryDiscoveryUpdatesAndRemovesOwnedInput
        rootDiscoverySkipsStoresAndContinuesPastInvalidInput
        refreshReloadsAllInstalledPluginsAndPreservesDisabledState
        directoryActionOpensTheDisplayedRoot managementUpdateRollsBackPackageAndPrivateData
        localBatchContinuesAfterInvalidPackage arrivingInstallFilesCompleteDependencyBatch
        qtFileOpenQueuesBeforeServiceInitialization githubMirrorFallbackAndPostShareUnifiedProxy
        sourceCatalogNetworkCacheAndOfficialProtection managementHeaderAndLoadingContentStayAligned
        managementPageUsesProductionModelsAndDialogs
        marketplaceInstallsActualPackageAndRejectsTampering
)
add_test(NAME plugin-runtime-runtime
         COMMAND echo-plugin-runtime-test packageIntegrityAndCompatibility
                 reloadUsesNewCodeUrlAndIdentity actualHttpAndCancellation
)
add_test(
    NAME plugin-runtime-shutdown
    COMMAND
        echo-plugin-runtime-test dependencyGraphAndVersionFailuresNeverExecuteCode
        metadataCommitFailureRetiresNewCodeAndRetainsRecoverySnapshot
        backupBarrierDrainsScopesWithoutChangingEnabledPreference realActivationSdkAndRetirement
        failureRollsBackAndPendingActivationCancels applicationQuitDrainsPluginIoFixture
        applicationQuitDrainsPluginIo ioDestructionJoinsAcceptedStorageWrites
        interruptedProcessFixture killedProcessRecovery promiseCanceledBeforeCommit
        rejectedPromiseAndLifecycleTimeout persistedInstallAndSafeMode
)
add_test(NAME plugin-runtime-dependencies
         COMMAND echo-plugin-runtime-test dependentServicesActivationCancellationAndProviderReload
                 optionalDependencyAndServiceLeasePreserveOwnership
)
add_test(
    NAME plugin-runtime-capabilities
    COMMAND
        echo-plugin-runtime-test settingsContributionRetiresWithScope
        statisticsUseSampledWireAndRejectInvalidNumbers spectrumCapabilityAndOwnerLifetime
        builtAuthorPackageCommands
)

add_test(
    NAME plugin-runtime-ui-parts
    COMMAND
        echo-plugin-runtime-test uiStyleLayersRestoreLiveBindings
        uiPartsIdentityObservationAndPublicActions uiPartMountsReplaceAndRetire
        uiPartsProductionPlaybackAndMini uiPartInstanceStylesAndLeasesStayBounded
)

set_tests_properties(
    ${ECHO_PLUGINS_TEST_GROUPS} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90
)
echo_copy_rust_runtime(echo-plugin-runtime-test)

add_test(
    NAME plugin-application-smoke
    COMMAND
        ${CMAKE_COMMAND} "-DAPP=$<TARGET_FILE:echomusicnext>"
        "-DPLUGIN=${PROJECT_SOURCE_DIR}/tests/fixtures/plugins/application"
        "-DLOG_DIR=${CMAKE_CURRENT_BINARY_DIR}/plugin-smoke-logs" -P
        "${CMAKE_CURRENT_SOURCE_DIR}/PluginApplicationSmoke.cmake"
)
set_tests_properties(
    plugin-application-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 25
)

# Real ABI2 library exercises the host catalog and audio actor without network.
add_library(echo-dsp-test-provider SHARED DspTestProvider.cpp)
target_include_directories(echo-dsp-test-provider PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(echo-dsp-test-provider PRIVATE Qt6::Core)
add_library(echo-dsp-bare-test-provider SHARED DspTestProvider.cpp)
target_include_directories(echo-dsp-bare-test-provider PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(echo-dsp-bare-test-provider PRIVATE Qt6::Core)
target_compile_definitions(echo-dsp-bare-test-provider PRIVATE DSP_TEST_NO_MANIFEST)
qt_add_executable(echo-dsp-provider-test DspProviderTest.cpp)
target_include_directories(echo-dsp-provider-test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(echo-dsp-provider-test PRIVATE echo_playback_service Qt6::Test)
target_compile_definitions(
    echo-dsp-provider-test
    PRIVATE ECHO_DSP_IMPORT_SHARED
            DSP_BARE_TEST_LIBRARY="$<TARGET_FILE:echo-dsp-bare-test-provider>"
            DSP_TEST_LIBRARY="$<TARGET_FILE:echo-dsp-test-provider>"
)
add_dependencies(echo-dsp-provider-test echo-dsp-test-provider echo-dsp-bare-test-provider)
add_test(NAME dsp-provider COMMAND echo-dsp-provider-test)
set_tests_properties(dsp-provider PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
echo_copy_rust_runtime(echo-dsp-provider-test)

qt_add_executable(echo-dsp-provider-ui-test DspProviderUiTest.cpp)
qt_add_qml_module(
    echo-dsp-provider-ui-test
    URI
    EchoMusic.DspTests
    VERSION
    1.0
    QML_FILES
    DspHarness.qml
)
target_link_libraries(
    echo-dsp-provider-ui-test
    PRIVATE echo_playback
            echo_playbackplugin
            echo_controls
            echo_controlsplugin
            echo_theme
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Test
            Qt6::Widgets
)
target_compile_definitions(
    echo-dsp-provider-ui-test PRIVATE DSP_TEST_LIBRARY="$<TARGET_FILE:echo-dsp-test-provider>"
)
add_dependencies(echo-dsp-provider-ui-test echo-dsp-test-provider)
add_test(NAME dsp-provider-ui COMMAND echo-dsp-provider-ui-test)
set_tests_properties(dsp-provider-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
echo_copy_rust_runtime(echo-dsp-provider-ui-test)

add_test(
    NAME plugin-share-application-smoke
    COMMAND
        echomusicnext --smoke-test --open-url
        "echomusicnext://plugin?source=https://github.com/hoowhoami/EchoMusicNextPlugins&id=spectrum-visualizer&version=1.0.0&path=plugins/spectrum-visualizer"
        --log-dir "${CMAKE_CURRENT_BINARY_DIR}/plugin-share-logs"
)
set_tests_properties(
    plugin-share-application-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 25
)

qt_add_executable(echo-playback-page-test PlaybackPageTest.cpp PlaybackPageTest.h)
qt_add_qml_module(
    echo-playback-page-test
    URI
    EchoMusic.PlaybackPageTests
    VERSION
    1.0
    QML_FILES
    PlaybackPageHarness.qml
    PlaybackBackgroundHarness.qml
    RESOURCES
    amll-motion-observations.json
    amll-interlude-observations.json
    amll-timeline-observations.json
    amll-background-observations.json
)
target_link_libraries(
    echo-playback-page-test
    PRIVATE Qt6::Multimedia
            echo_playback
            echo_playbackplugin
            echo_shell
            echo_shellplugin
            echo_theme
            echo_themeplugin
            echo_controls
            echo_controlsplugin
            Qt6::Widgets
            Qt6::Qml
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Test
)
add_test(NAME playback-page COMMAND echo-playback-page-test)
set_tests_properties(playback-page PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
echo_copy_rust_runtime(echo-playback-page-test)
qt_add_executable(echo-network-transport-test NetworkTransportTest.cpp)
# This executable evaluates isolated PAC JavaScript only; it must not import
# every application QML plugin found by the source-tree import scanner.
set_target_properties(echo-network-transport-test PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
target_link_libraries(
    echo-network-transport-test PRIVATE echo_network Qt6::Network Qt6::Sql Qt6::Test
)
if(APPLE)
    target_link_libraries(echo-network-transport-test PRIVATE "-framework SystemConfiguration")
endif()
add_test(NAME echo-network-transport COMMAND echo-network-transport-test)
set_tests_properties(echo-network-transport PROPERTIES TIMEOUT 45)
qt_add_executable(echo-proxy-authentication-test ProxyAuthenticationTest.cpp)
set_target_properties(echo-proxy-authentication-test PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
target_link_libraries(echo-proxy-authentication-test PRIVATE echo_network Qt6::Network Qt6::Test)
add_test(NAME proxy-authentication COMMAND echo-proxy-authentication-test)
set_tests_properties(proxy-authentication PROPERTIES TIMEOUT 45)
if(UNIX AND NOT APPLE)
    find_program(ECHO_GLIB_COMPILE_SCHEMAS glib-compile-schemas REQUIRED)
    set(ECHO_NETWORK_TEST_SCHEMA_DIR "${CMAKE_CURRENT_BINARY_DIR}/network-test-schemas")
    add_custom_command(
        OUTPUT "${ECHO_NETWORK_TEST_SCHEMA_DIR}/gschemas.compiled"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ECHO_NETWORK_TEST_SCHEMA_DIR}"
        COMMAND
            "${ECHO_GLIB_COMPILE_SCHEMAS}" --strict --targetdir "${ECHO_NETWORK_TEST_SCHEMA_DIR}"
            "${PROJECT_SOURCE_DIR}/tests/fixtures/network/gsettings"
        DEPENDS
            "${PROJECT_SOURCE_DIR}/tests/fixtures/network/gsettings/org.echomusic.network-test.gschema.xml"
        VERBATIM
    )
    qt_add_executable(
        echo-linux-network-monitor-test LinuxNetworkMonitorTest.cpp
        "${ECHO_NETWORK_TEST_SCHEMA_DIR}/gschemas.compiled"
    )
    set_target_properties(
        echo-linux-network-monitor-test PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE
    )
    target_compile_definitions(
        echo-linux-network-monitor-test
        PRIVATE ECHO_NETWORK_TEST_SCHEMA_DIR="${ECHO_NETWORK_TEST_SCHEMA_DIR}"
    )
    target_link_libraries(
        echo-linux-network-monitor-test PRIVATE echo_network PkgConfig::Gio Qt6::Test
    )
    add_test(NAME linux-network-monitor COMMAND echo-linux-network-monitor-test)
    set_tests_properties(linux-network-monitor PROPERTIES TIMEOUT 15)
endif()
qt_add_executable(echo-network-video-test NetworkVideoTest.cpp)
set_target_properties(echo-network-video-test PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
target_link_libraries(
    echo-network-video-test PRIVATE echo_network Qt6::Multimedia Qt6::Gui Qt6::Test
)
add_test(NAME network-video COMMAND echo-network-video-test)
set_tests_properties(network-video PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

# These tests instantiate C++ services only. Their PAC dependency on Qt Qml
# must not scan/import every application UI plugin into a headless executable.
set_target_properties(
    echo-foundation-test echo-player-engine-settings-test echo-playback-test
    PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE
)

qt_add_executable(echo-discovery-test DiscoveryTest.cpp)
target_link_libraries(
    echo-discovery-test
    PRIVATE echo_settings
            echo_settingsplugin
            echo_discovery
            echo_discoveryplugin
            echo_theme
            echo_themeplugin
            echo_shell
            echo_shellplugin
            echo_playback_service
            Qt6::Quick
            Qt6::Test
            Qt6::Sql
)
add_test(NAME discovery COMMAND echo-discovery-test)
set_tests_properties(discovery PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

qt_add_executable(echo-playlist-detail-test PlaylistDetailTest.cpp)
target_link_libraries(
    echo-playlist-detail-test
    PRIVATE echo_playlists
            echo_playlistsplugin
            echo_libraryui
            echo_theme
            echo_themeplugin
            echo_shell
            echo_shellplugin
            echo_music
            echo_musicplugin
            echo_playback_service
            Qt6::Quick
            Qt6::Test
)
add_test(NAME playlist-detail COMMAND echo-playlist-detail-test)
set_tests_properties(playlist-detail PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(echo-album-detail-test AlbumDetailTest.cpp)
target_link_libraries(
    echo-album-detail-test
    PRIVATE echo_albums
            echo_albumsplugin
            echo_theme
            echo_themeplugin
            echo_shell
            echo_shellplugin
            echo_music
            echo_musicplugin
            echo_playback_service
            Qt6::Quick
            Qt6::Test
)
add_test(NAME album-detail COMMAND echo-album-detail-test)
set_tests_properties(album-detail PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(echo-artist-detail-test ArtistDetailTest.cpp)
target_link_libraries(
    echo-artist-detail-test
    PRIVATE echo_artists
            echo_artistsplugin
            echo_theme
            echo_themeplugin
            echo_shell
            echo_shellplugin
            echo_music
            echo_musicplugin
            echo_playback_service
            Qt6::Quick
            Qt6::Test
)
add_test(NAME artist-detail COMMAND echo-artist-detail-test)
set_tests_properties(artist-detail PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(echo-history-test HistoryTest.cpp)
target_link_libraries(
    echo-history-test
    PRIVATE echo_history
            echo_historyplugin
            echo_theme
            echo_themeplugin
            echo_shell
            echo_shellplugin
            echo_music
            echo_musicplugin
            echo_playback_service
            Qt6::Quick
            Qt6::Test
)
add_test(NAME history COMMAND echo-history-test)
set_tests_properties(history PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(echo-motion-test MotionTest.cpp)
target_link_libraries(
    echo-motion-test PRIVATE echo_controls echo_controlsplugin echo_theme echo_themeplugin
                             Qt6::Quick Qt6::Test
)
add_test(NAME motion COMMAND echo-motion-test)
set_tests_properties(motion PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

# QML-local interaction contracts complement the C++ native-window/service tests.
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS QuickTest)
qt_add_executable(echo-quick-controls-test QuickControlsTest.cpp)
# Only Controls/Theme are under test; scanning all sibling QML harnesses would
# import the application, Rust playback and browser verification into this small runner.
qt_add_qml_module(
    echo-quick-controls-test URI EchoMusic.QuickControlsTests NO_IMPORT_SCAN QML_FILES
    quick/tst_ControlKeyboard.qml
)
target_compile_definitions(
    echo-quick-controls-test PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/quick"
)
target_link_libraries(
    echo-quick-controls-test PRIVATE echo_controls echo_controlsplugin echo_theme echo_themeplugin
                                     Qt6::QuickTest Qt6::QuickControls2
)
add_test(NAME quick-controls COMMAND echo-quick-controls-test)
set_tests_properties(quick-controls PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

qt_add_executable(echo-browser-verification-test BrowserVerificationTest.cpp)
target_link_libraries(
    echo-browser-verification-test
    PRIVATE echo_account
            echo_shell
            echo_shellplugin
            Qt6::Gui
            Qt6::Network
            Qt6::Quick
            Qt6::Widgets
            Qt6::Test
)
add_test(NAME browser-verification COMMAND echo-browser-verification-test)
set_tests_properties(
    browser-verification PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45
)
echo_copy_rust_runtime(echo-browser-verification-test)

qt_add_executable(echo-startup-test StartupTest.cpp)
target_link_libraries(
    echo-startup-test PRIVATE echo_startup echo_storage echo_network echo_playback_service
                              Qt6::Test
)
add_test(NAME startup COMMAND echo-startup-test)
set_tests_properties(startup PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
echo_copy_rust_runtime(echo-startup-test)

qt_add_executable(echo-details-test DetailsTest.cpp)
target_link_libraries(
    echo-details-test
    PRIVATE echo_details
            echo_detailsplugin
            echo_albums
            echo_albumsplugin
            echo_artists
            echo_artistsplugin
            echo_playlists
            echo_playlistsplugin
            echo_theme
            echo_themeplugin
            echo_shell
            echo_shellplugin
            echo_music
            echo_musicplugin
            echo_playback_service
            Qt6::Quick
            Qt6::Sql
            Qt6::Test
)
add_test(NAME details COMMAND echo-details-test)
set_tests_properties(details PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

qt_add_executable(echo-personal-fm-test PersonalFmTest.cpp)
target_link_libraries(
    echo-personal-fm-test
    PRIVATE echo_discovery
            echo_discoveryplugin
            echo_music
            echo_musicplugin
            echo_playback
            echo_playbackplugin
            echo_shell
            echo_shellplugin
            echo_theme
            echo_themeplugin
            echo_controls
            echo_controlsplugin
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Test
)
add_test(NAME personal-fm COMMAND echo-personal-fm-test)
set_tests_properties(
    personal-fm PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90 RUN_SERIAL TRUE
)
echo_copy_rust_runtime(echo-personal-fm-test)

qt_add_executable(echo-queue-semantics-test QueueSemanticsTest.cpp)
target_link_libraries(
    echo-queue-semantics-test PRIVATE echo_playback_service echo_discovery Qt6::Test
)
target_compile_definitions(
    echo-queue-semantics-test
    PRIVATE
        ECHO_QUEUE_ORACLE="${PROJECT_SOURCE_DIR}/tests/fixtures/playback/queue-order-oracle.json"
)
add_test(NAME queue-semantics COMMAND echo-queue-semantics-test)
set_tests_properties(
    queue-semantics PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90 RUN_SERIAL TRUE
)
echo_copy_rust_runtime(echo-queue-semantics-test)

qt_add_executable(echo-recognition-test RecognitionTest.cpp)
target_link_libraries(
    echo-recognition-test
    PRIVATE echo_discovery
            echo_discoveryplugin
            echo_music
            echo_musicplugin
            echo_playback
            echo_playbackplugin
            echo_shell
            echo_shellplugin
            echo_theme
            echo_themeplugin
            echo_controls
            echo_controlsplugin
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Test
)
target_compile_definitions(
    echo-recognition-test
    PRIVATE
        ECHO_RECOGNITION_DOMAIN="${PROJECT_SOURCE_DIR}/tests/fixtures/domain/recognition-20261018.json"
)
add_test(NAME recognition COMMAND echo-recognition-test)
set_tests_properties(recognition PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(echo-playback-discussion-test PlaybackDiscussionTest.cpp)
target_link_libraries(
    echo-playback-discussion-test
    PRIVATE echo_details
            echo_detailsplugin
            echo_theme
            echo_themeplugin
            echo_shell
            echo_shellplugin
            echo_music
            echo_musicplugin
            echo_playback_service
            Qt6::Quick
            Qt6::Sql
            Qt6::Test
)
add_test(NAME playback-discussion COMMAND echo-playback-discussion-test)
set_tests_properties(
    playback-discussion PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90 RUN_SERIAL
                                   TRUE
)

qt_add_executable(echo-listen-together-test ListenTogetherTest.cpp)
target_link_libraries(echo-listen-together-test PRIVATE echo_playback_service Qt6::Test)
add_test(NAME listen-together COMMAND echo-listen-together-test)
set_tests_properties(
    listen-together PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90 RUN_SERIAL TRUE
)
echo_copy_rust_runtime(echo-listen-together-test)

qt_add_executable(echo-listen-together-ui-test ListenTogetherUiTest.cpp)
if(APPLE)
    # Cocoa must give this interactive native probe its own foreground identity,
    # independently of the user's running production application.
    set_target_properties(
        echo-listen-together-ui-test
        PROPERTIES MACOSX_BUNDLE TRUE
                   MACOSX_BUNDLE_GUI_IDENTIFIER "${ECHO_APPLICATION_ID}.tests.listen-together-ui"
                   MACOSX_BUNDLE_BUNDLE_NAME "EchoMusicNext Together UI Test"
    )
endif()
target_link_libraries(
    echo-listen-together-ui-test
    PRIVATE echo_listen_together
            echo_listen_togetherplugin
            echo_playback
            echo_playbackplugin
            echo_music
            echo_musicplugin
            echo_shell
            echo_shellplugin
            echo_theme
            echo_themeplugin
            echo_controls
            echo_controlsplugin
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Test
)
add_test(NAME listen-together-ui COMMAND echo-listen-together-ui-test)
set_tests_properties(
    listen-together-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90 RUN_SERIAL
                                  TRUE
)
echo_copy_rust_runtime(echo-listen-together-ui-test)

qt_add_executable(echo-share-target-test ShareTargetTest.cpp)
target_link_libraries(echo-share-target-test PRIVATE echo_sharing Qt6::Test)
add_test(NAME share-target COMMAND echo-share-target-test)

qt_add_executable(echo-share-resolve-test ShareResolveTest.cpp)
target_link_libraries(
    echo-share-resolve-test PRIVATE echo_sharing_ui echo_sharing_uiplugin Qt6::Test
)
add_test(NAME share-resolve COMMAND echo-share-resolve-test)
set_tests_properties(share-resolve PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
echo_copy_rust_runtime(echo-share-resolve-test)

qt_add_executable(echo-collection-social-test CollectionSocialTest.cpp)
target_link_libraries(echo-collection-social-test PRIVATE echo_account echo_library Qt6::Test)
set_target_properties(echo-collection-social-test PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
add_test(NAME collection-social COMMAND echo-collection-social-test)
echo_copy_rust_runtime(echo-collection-social-test)

qt_add_executable(echo-sharing-ui-test SharingUiTest.cpp)
if(APPLE)
    set_target_properties(
        echo-sharing-ui-test
        PROPERTIES MACOSX_BUNDLE TRUE
                   MACOSX_BUNDLE_GUI_IDENTIFIER "${ECHO_APPLICATION_ID}.tests.sharing-ui"
                   MACOSX_BUNDLE_BUNDLE_NAME "EchoMusicNext Sharing UI Test"
    )
endif()
target_link_libraries(
    echo-sharing-ui-test
    PRIVATE echo_sharing_ui
            echo_sharing_uiplugin
            echo_history
            echo_historyplugin
            echo_theme
            echo_themeplugin
            echo_shell
            echo_shellplugin
            echo_music
            echo_musicplugin
            Qt6::Quick
            Qt6::Test
)
add_test(NAME sharing-ui COMMAND echo-sharing-ui-test)
set_tests_properties(
    sharing-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 RUN_SERIAL TRUE
)
echo_copy_rust_runtime(echo-sharing-ui-test)

# Pixel/mask/input contracts need the production scene graph. Keep the
# explicit controls-backdrop-software fallback test independent.
set(ECHO_RENDERED_TEST_GROUPS
    history
    sharing-ui
    song-playback-ui
    shell
    playback-page
    discovery
    details
    ${ECHO_DESKTOP_TEST_GROUPS}
    ${ECHO_CONTROLS_TEST_GROUPS}
)
if(APPLE)
    set(echo_test_platform cocoa)
elseif(WIN32)
    set(echo_test_platform windows)
else()
    # CI supplies Xvfb for xcb. This uses RHI rather than offscreen software.
    set(echo_test_platform xcb)
endif()
set_tests_properties(
    ${ECHO_RENDERED_TEST_GROUPS} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=${echo_test_platform}"
                                            RUN_SERIAL TRUE
)
set_tests_properties(${ECHO_DESKTOP_TEST_GROUPS} playback-page PROPERTIES TIMEOUT 240)
set_tests_properties(${ECHO_CONTROLS_TEST_GROUPS} PROPERTIES TIMEOUT 120)
set_tests_properties(discovery details PROPERTIES TIMEOUT 180)

add_test(
    NAME share-application-smoke
    COMMAND echomusicnext --smoke-test --open-url "echomusicnext://album/123?title=Native%20share"
            --log-dir "${CMAKE_CURRENT_BINARY_DIR}/share-logs"
)
set_tests_properties(
    share-application-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 25
)

target_link_libraries(
    echo-sharing-ui-test
    PRIVATE echo_albums
            echo_albumsplugin
            echo_artists
            echo_artistsplugin
            echo_playlists
            echo_playlistsplugin
            echo_details
            echo_detailsplugin
)

qt_add_executable(echo-playback-source-fallback-test PlaybackSourceFallbackTest.cpp)
target_link_libraries(echo-playback-source-fallback-test PRIVATE echo_playback_service Qt6::Test)
add_test(NAME playback-source-fallback COMMAND echo-playback-source-fallback-test)
set_tests_properties(
    playback-source-fallback PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90
)
echo_copy_rust_runtime(echo-playback-source-fallback-test)

qt_add_executable(echo-experimental-settings-test ExperimentalSettingsTest.cpp)
target_link_libraries(
    echo-experimental-settings-test
    PRIVATE echo_settings
            echo_account
            echo_storage
            echo_platform
            Qt6::Qml
            Qt6::Test
            Qt6::Gui
)
add_test(NAME experimental-settings COMMAND echo-experimental-settings-test)
set_tests_properties(
    experimental-settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30
)
echo_copy_rust_runtime(echo-experimental-settings-test)

if(APPLE)
    add_library(echo_native_test_support STATIC NativeWindowTestSupport.mm)
    target_link_libraries(echo_native_test_support PRIVATE "-framework AppKit")
    foreach(
        echo_test_target IN
        ITEMS echo-song-playback-ui-test
              echo-controls-ui-test
              echo-tab-count-alignment-test
              echo-background-banding-test
              echo-shell-test
              echo-window-resize-test
              echo-search-ui-test
              echo-theme-test
              echo-desktop-pages-test
              echo-mac-floating-test
              echo-plugin-runtime-test
              echo-playback-page-test
              echo-discovery-test
              echo-history-test
              echo-details-test
              echo-playback-discussion-test
              echo-listen-together-ui-test
              echo-sharing-ui-test
    )
        target_link_libraries(${echo_test_target} PRIVATE echo_native_test_support)
    endforeach()
endif()

if(APPLE)
    foreach(echo_native_ui_target IN ITEMS echo-controls-ui-test echo-desktop-pages-test)
        set_target_properties(
            ${echo_native_ui_target}
            PROPERTIES MACOSX_BUNDLE TRUE
                       MACOSX_BUNDLE_GUI_IDENTIFIER
                       "io.github.hoowhoami.echomusicnext.tests.${echo_native_ui_target}"
                       MACOSX_BUNDLE_BUNDLE_NAME "${echo_native_ui_target}"
        )
    endforeach()
endif()

# Direct SQL consumers declare their own compile/link requirements.
target_link_libraries(echo-settings-backup-test PRIVATE Qt6::Sql)
target_link_libraries(echo-account-persistence-test PRIVATE Qt6::Sql)
target_link_libraries(echo-library-pages-test PRIVATE Qt6::Sql)
target_link_libraries(echo-playback-test PRIVATE Qt6::Sql)
target_link_libraries(echo-dsp-provider-test PRIVATE Qt6::Sql)
target_link_libraries(echo-network-transport-test PRIVATE Qt6::Sql)
target_link_libraries(echo-discovery-test PRIVATE Qt6::Sql)
target_link_libraries(echo-startup-test PRIVATE Qt6::Sql)
target_link_libraries(echo-details-test PRIVATE Qt6::Sql)
target_link_libraries(echo-personal-fm-test PRIVATE Qt6::Sql)
target_link_libraries(echo-playback-discussion-test PRIVATE Qt6::Sql)
target_link_libraries(echo-listen-together-test PRIVATE Qt6::Sql)

# QTEST_MAIN chooses QApplication from QT_WIDGETS_LIB. These fixtures formerly
# obtained it through the playback facade; declare the test application choice
# explicitly so PRIVATE service dependencies cannot silently change the harness.
foreach(
    echo_qapplication_test IN
    ITEMS echo-share-resolve-test
          echo-window-resize-test
          echo-personal-fm-test
          echo-sharing-ui-test
          echo-library-pages-test
          echo-dsp-provider-test
          echo-details-test
          echo-artist-detail-test
          echo-listen-together-test
          echo-discovery-test
          echo-experimental-settings-test
          echo-plugin-runtime-test
          echo-recognition-test
          echo-listen-together-ui-test
          echo-playback-discussion-test
          echo-playlist-detail-test
          echo-account-persistence-test
          echo-album-detail-test
          echo-history-test
          echo-queue-semantics-test
          echo-startup-test
          echo-playback-test
          echo-shell-actions-test
)
    target_link_libraries(${echo_qapplication_test} PRIVATE Qt6::Widgets)
endforeach()
