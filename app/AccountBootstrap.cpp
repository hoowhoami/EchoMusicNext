#include "AppContext_p.h"
#include "AccountController.h"
#include "AccountService.h"
#include "ApiClient.h"
#include "ContentBlacklistService.h"
#include "DeviceRegistrationService.h"
#include "LibraryCollectionService.h"
#include "LibraryController.h"
#include "NetworkPreferences.h"
#include "PlaybackService.h"
#include "PlaylistImportTaskCoordinator.h"
#include "PlaylistMutationService.h"
#include "ProfileService.h"
#include "ListeningPreferencesService.h"
#include "QrLoginService.h"
#include "RouteController.h"
#include "SavedAccountModel.h"
#include "SearchModel.h"
#include "SearchSuggestionModel.h"
#include "SharingCommands.h"
#include "ShellController.h"
#include "ShellSettings.h"
#include "ShortcutActions.h"
#include "ShortcutService.h"
#include "SidebarRouteCoordinator.h"
#include "StartupController.h"
#include "StorageService.h"
#include "ThemeStore.h"
#include "UserPlaylistModel.h"
#include "VerificationService.h"
#include "WindowController.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QSet>
#include <QStandardPaths>
#include <QProcess>
#include <QUrlQuery>

namespace EchoMusic
{
void AppContext::createAccount()
{
    auto &settings = *preferences;
    auto &theme = *m_theme;
    auto &shell = *m_shell;
    auto &window = *m_window;
    auto &router = *m_router;
    auto &shellController = *m_shellController;
    auto &api = *m_api;
    auto &network = *m_network;
    auto &playback = *m_playback;
    m_account = std::make_unique<AccountService>(api, nullptr, playback.memberships());
    auto &account = *m_account;
    m_profile = std::make_unique<ProfileService>(api, account);
    auto &profile = *m_profile;
    bind(profile.preferences(), &ListeningPreferencesService::saved, &shellController,
         [&shellController] {
             shellController.toasts()->show(QObject::tr("听歌偏好已保存"), ToastService::Success);
         });
    bind(&profile, &EchoMusic::ProfileService::deviceRemovalFinished, &shellController,
         [&](bool success, const QString &message) {
             shellController.toasts()->show(message, success ? EchoMusic::ToastService::Success
                                                             : EchoMusic::ToastService::Danger);
         });
    bind(&profile, &EchoMusic::ProfileService::avatarFinished, &shellController,
         [&](bool success, const QString &message) {
             shellController.toasts()->show(message, success ? EchoMusic::ToastService::Success
                                                             : EchoMusic::ToastService::Danger);
         });
    bind(profile.blacklist(), &EchoMusic::ContentBlacklistService::removalFinished,
         &shellController, [&](bool success, const QString &message) {
             shellController.toasts()->show(message, success ? EchoMusic::ToastService::Success
                                                             : EchoMusic::ToastService::Danger);
         });
    m_userPlaylists = std::make_unique<UserPlaylistModel>(api);
    auto &userPlaylists = *m_userPlaylists;
    m_playlistMutations = std::make_unique<PlaylistMutationService>(api);
    auto &playlistMutations = *m_playlistMutations;
    bind(&playlistMutations, &EchoMusic::PlaylistMutationService::libraryInvalidated,
         &userPlaylists, &EchoMusic::UserPlaylistModel::reload);
    m_collections =
        std::make_unique<LibraryCollectionService>(api, userPlaylists, playlistMutations);
    m_qrLogin = std::make_unique<QrLoginService>(api);
    auto &qrLogin = *m_qrLogin;
    m_deviceRegistration = std::make_unique<DeviceRegistrationService>(api, settings);
    auto &deviceRegistration = *m_deviceRegistration;
    m_storage = std::make_unique<StorageService>();
    auto &storage = *m_storage;
    userPlaylists.attachStorage(storage);
    m_savedAccounts = std::make_unique<SavedAccountModel>(api, account, storage);
    auto &savedAccounts = *m_savedAccounts;
    bind(&savedAccounts, &SavedAccountModel::notice, &shellController,
         [&shellController](const QString &message, bool warning) {
             shellController.toasts()->show(message, warning ? ToastService::Warning
                                                             : ToastService::Success);
         });
    m_startup = std::make_unique<StartupController>(
        storage, api, deviceRegistration, playback, network, savedAccounts,
        applicationData + QStringLiteral("/music.sqlite"));
    m_accountController = std::make_unique<AccountController>(api, account, savedAccounts, qrLogin,
                                                              userPlaylists, !smoke);
    auto &accountController = *m_accountController;
    bind(accountController.verification(),
         &EchoMusic::VerificationService::returnToApplicationRequested, &window,
         &EchoMusic::WindowController::showWindow);
    m_libraryController =
        std::make_unique<LibraryController>(api, userPlaylists, playlistMutations);
    auto &libraryController = *m_libraryController;
    m_sidebarRoutes =
        std::make_unique<SidebarRouteCoordinator>(libraryController, userPlaylists, router);
    m_importTasks = std::make_unique<PlaylistImportTaskCoordinator>(
        *libraryController.importer(), *shellController.tasks(), settings);
    playback.attachLibrary(userPlaylists, playlistMutations);
    playback.listenTogether()->attachAccount(account);
    theme.setCoverColor(playback.coverColor());
    bind(&playback, &EchoMusic::PlaybackService::changed, &theme,
         [&] { theme.setCoverColor(playback.coverColor()); });
    shellController.attachPlayback(playback);
    shellController.attachWindows(window);
    // 歌曲菜单共享命令绑定账号与不感兴趣服务；toast 经 ShellController 统一呈现。
    playback.songCommands()->attach(*profile.blacklist(), account);
    m_shareCommands = std::make_unique<SharingCommands>(account);
    auto &shareCommands = *m_shareCommands;
    playback.songCommands()->attachSharing(shareCommands);
    bind(&shareCommands, &EchoMusic::SharingCommands::notice, &shellController,
         [&](const QString &message, bool warning) {
             shellController.toasts()->show(message, warning ? EchoMusic::ToastService::Warning
                                                             : EchoMusic::ToastService::Success);
         });
    m_shortcuts = std::make_unique<ShortcutService>(settings, !smoke);
    auto &shortcuts = *m_shortcuts;
    bind(window.floating(), &FloatingPlayerController::presentationWindowChanged, &shortcuts,
         [&] { shortcuts.attachMini(window.floating()->miniWindow()); });
    bind(&shortcuts, &EchoMusic::ShortcutService::notice, &shellController,
         [&](const QString &message) {
             shellController.toasts()->show(message, EchoMusic::ToastService::Warning);
         });
    m_shortcutActions =
        std::make_unique<ShortcutActions>(shortcuts, playback, shell, window, shellController);
    m_search = std::make_unique<SearchModel>(api);
    auto &search = *m_search;
    search.attachPlayback(playback);
    bind(&search, &EchoMusic::SearchModel::tracksPlayRequested, &playback,
         &EchoMusic::PlaybackService::playFromSelection);
    bind(&search, &EchoMusic::SearchModel::tracksAppendRequested, &playback,
         &EchoMusic::PlaybackService::appendTracks);
    bind(&search, &EchoMusic::SearchModel::trackPlayNowRequested, &playback,
         &EchoMusic::PlaybackService::queueAndPlayTrack);
    bind(&search, &EchoMusic::SearchModel::trackQueueRequested, &playback,
         [&](const EchoMusic::PlaybackTrack &track, bool cut) {
             playback.enqueueTracks({track}, cut);
         });
    search.suggestions()->setDefaultEnabled(shell.searchDefaultEnabled());
    bind(&shell, &EchoMusic::ShellSettings::changed, &search,
         [&] { search.suggestions()->setDefaultEnabled(shell.searchDefaultEnabled()); });
    search.setHistoryStore(storage);
}
} // namespace EchoMusic
