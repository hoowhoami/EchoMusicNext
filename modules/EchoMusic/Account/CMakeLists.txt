qt_add_qml_module(
    echo_accountui
    STATIC
    URI
    EchoMusic.Account
    SOURCES
    AccountTypes.h
    AccountController.h
    AccountController.cpp
    QML_FILES
    AccountAvatar.qml
    UserIdentityBadges.qml
    UserIdentitySummary.qml
    MembershipTag.qml
    SidebarAccount.qml
    AccountPopover.qml
    AccountSwitcherDialog.qml
    LogoutConfirmDialog.qml
    AuthExpiredDialog.qml
    LoginOverlay.qml
    LoginQrPanel.qml
    LoginSmsPanel.qml
    VerificationOverlay.qml
)
target_include_directories(echo_accountui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_accountui
    PUBLIC echo_account echo_library Qt6::Qml
    PRIVATE echo_build_options
            echo_theme
            echo_themeplugin
            echo_controls
            echo_controlsplugin
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            Qt6::QuickControls2
)
