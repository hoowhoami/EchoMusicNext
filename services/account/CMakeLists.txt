add_library(
    echo_account STATIC
    LoginCandidateModel.cpp
    LoginCandidateModel.h
    AccountService.cpp
    AccountService.h
    AccountIdentity.h
    AccountIdentity.cpp
    MembershipService.h
    MembershipService.cpp
    ProfileService.cpp
    ProfileService.h
    SocialUserModel.cpp
    SocialUserModel.h
    LoginDeviceModel.cpp
    LoginDeviceModel.h
    ContentBlacklistService.cpp
    ContentBlacklistService.h
    ContentBlacklistModel.cpp
    ContentBlacklistModel.h
    ListeningPreferencesService.cpp
    ListeningPreferencesService.h
    PreferenceStrengthModel.cpp
    PreferenceStrengthModel.h
    QrLoginService.cpp
    QrLoginService.h
    BrowserVerificationSession.cpp
    BrowserVerificationSession.h
    VerificationService.cpp
    VerificationService.h
    SavedAccountModel.cpp
    SavedAccountModel.h
)
target_include_directories(echo_account PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    echo_account
    PUBLIC echo_bridge echo_storage echo_session_cache Qt6::Network
    PRIVATE echo_build_options echo_platform Qt6::Concurrent Qt6::Gui
)
qt_extract_metatypes(echo_account)

qt_add_resources(
    echo_account
    browser_verification
    PREFIX
    "/echo/auth"
    BASE
    "web"
    FILES
    web/browser-verification.html
)
