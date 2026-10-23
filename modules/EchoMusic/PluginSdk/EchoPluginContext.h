#pragma once
#include "PluginPackage.h"
#include "EchoPluginOperation.h"
#include "EchoPluginRegistration.h"
#include "EchoPluginPlayer.h"
#include "EchoPluginPlaylist.h"
#include "EchoPluginLyrics.h"
#include "EchoPluginFiles.h"
#include "EchoPluginDownloads.h"
#include "EchoPluginTasks.h"
#include "EchoPluginShortcuts.h"
#include "EchoPluginToast.h"
#include "EchoPluginScroll.h"
#include "EchoPluginLyricEffects.h"
#include "EchoPluginKugou.h"
#include "EchoPluginUi.h"
#include "EchoPluginIo.h"
#include "EchoPluginSqlite.h"
#include "EchoPluginBackups.h"
#include "EchoPluginNetwork.h"
#include "EchoPluginPermissions.h"
#include "EchoPluginWebServer.h"
#include "EchoPluginRegistry.h"
#include "EchoPluginAudio.h"
#include "EchoPluginAppearance.h"
#include "EchoPluginViewContext.h"
#include <QJSValue>
#include <QQmlEngine>
#include <QPointer>
#include <QFont>
#include <QColor>
#include <QHash>
namespace EchoMusic
{
class EchoPluginUi;
class EchoPluginIo;
class PlaybackService;
class ThemeStore;
class ShellController;
class RouteController;
class AccountService;
class ApiClient;
class SidebarLayoutModel;
class ShortcutService;
class VerificationService;
class BackupHost;
class FloatingPlayerController;
struct PluginHost
{
    PlaybackService &playback;
    ThemeStore &theme;
    ShellController &shell;
    RouteController &router;
    AccountService &account;
    ApiClient &api;
    FloatingPlayerController *floating = nullptr;
    PluginFileService *files = nullptr;
    SidebarLayoutModel *sidebar = nullptr;
    ShortcutService *shortcuts = nullptr;
    VerificationService *verification = nullptr;
    BackupHost *backups = nullptr;
    PluginPermissionStore *permissions = nullptr;
    std::function<void(QQuickWindow *)> attachPluginWindow = {};
};
/// One activation's GUI-thread scope. No raw host object is exposed to JS/QML.
/// beginRetire invalidates delivery before cleanup, then waits for SQL drain.
class EchoPluginContext final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the plugin runtime")
    Q_PROPERTY(EchoMusic::EchoPluginAppearance *theme READ appearance CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginAppearance *appearance READ appearance CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginFonts *fonts READ fonts CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginWindows *windows READ windows CONSTANT)
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString installationId READ installationId CONSTANT)
    Q_PROPERTY(QString activationGeneration READ activationGeneration CONSTANT)
    Q_PROPERTY(QString contextId READ contextId CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(EchoMusic::EchoPluginAudio *audio READ audio CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginLyrics *lyrics READ lyrics CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginFiles *files READ files CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginDownloads *downloads READ downloads CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginPlaylist *playlist READ playlist CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginPlayer *player READ player CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginUi *ui READ ui CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginShortcuts *shortcuts READ shortcuts CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginToast *toast READ toast CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginKugou *kugou READ kugou CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginLyricEffects *lyricEffects READ lyricEffects CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginScroll *scroll READ scroll CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginTasks *tasks READ tasks CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginBackups *backups READ backups CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginSqlite *sqlite READ sqlite CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginIo *io READ io CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginNetwork *net READ net CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginPermissions *permissions READ permissions CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginWebServer *webServer READ webServer CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginRegistry *plugins READ plugins CONSTANT)
    Q_PROPERTY(QFont font READ font NOTIFY appearanceChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY appearanceChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY appearanceChanged)
public:
    EchoPluginContext(PluginPackage package, quint64 generation, QQmlEngine &engine,
                      PluginHost host, QString dataPath, QObject *parent,
                      EchoPluginContext *activation = nullptr, QString contextId = "main");
    ~EchoPluginContext() override;
    EchoPluginFiles *files() const
    {
        return m_files;
    }
    EchoPluginDownloads *downloads() const
    {
        return m_downloads;
    }
    EchoPluginContext *activationContext() const
    {
        return m_activation ? m_activation.data() : const_cast<EchoPluginContext *>(this);
    }
    EchoPluginContext *createPresentation(QObject *owner, const QString &surface);
    QString registrationKey(const QString &kind, const QString &id) const;
    EchoPluginAppearance *appearance() const
    {
        return m_appearance;
    }
    EchoPluginFonts *fonts() const
    {
        return m_fonts;
    }
    EchoPluginWindows *windows() const
    {
        return m_windows;
    }
    QString id() const
    {
        return m_package.id;
    }
    QString installationId() const
    {
        return m_package.installationId;
    }
    QString activationGeneration() const
    {
        return QString::number(m_generation);
    }
    QString contextId() const
    {
        return m_contextId;
    }
    bool active() const
    {
        return m_accepting;
    }
    bool committed() const
    {
        return m_committed;
    }
    EchoPluginAudio *audio() const
    {
        return m_audio;
    }
    EchoPluginLyrics *lyrics()
    {
        return m_lyrics;
    }
    EchoPluginPlaylist *playlist()
    {
        return m_playlist;
    }
    EchoPluginPlayer *player()
    {
        return m_player;
    }
    EchoPluginUi *ui()
    {
        return m_ui;
    }
    EchoPluginNetwork *net() const
    {
        return m_net;
    }
    EchoPluginShortcuts *shortcuts() const
    {
        return m_shortcuts;
    }
    EchoPluginToast *toast() const
    {
        return m_toast;
    }
    EchoPluginKugou *kugou() const
    {
        return m_kugou;
    }
    EchoPluginLyricEffects *lyricEffects() const
    {
        return m_lyricEffects;
    }
    EchoPluginScroll *scroll() const
    {
        return m_scroll;
    }
    EchoPluginTasks *tasks() const
    {
        return m_tasks;
    }
    EchoPluginBackups *backups() const
    {
        return m_backups;
    }
    EchoPluginSqlite *sqlite() const
    {
        return m_sqlite;
    }
    EchoPluginIo *io()
    {
        return m_io;
    }
    EchoPluginRegistry *plugins() const
    {
        return m_plugins;
    }
    EchoPluginWebServer *webServer() const
    {
        return m_webServer;
    }
    EchoPluginPermissions *permissions() const
    {
        return m_permissions;
    }
    EchoPluginOperation *invokeRegistered(const QJSValue &callback,
                                          QPointer<EchoPluginRegistration> lease,
                                          const QVariantMap &arguments);
    QFont font() const;
    QColor accent() const;
    bool dark() const;
    Q_INVOKABLE bool supports(const QString &feature) const;
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *onDispose(const QJSValue &callback);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *setTimer(const QString &id, int intervalMs,
                                                            bool repeat, const QJSValue &callback);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *onPlayerChanged(const QString &id,
                                                                   const QJSValue &callback);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *registerCommand(const QString &id,
                                                                   const QJSValue &callback);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *executeCommand(const QString &id,
                                                               const QVariantMap &arguments = {});
    EchoPluginOperation *operation();
    EchoPluginOperation *rejected(const QString &code, const QString &message);
    EchoPluginOperation *execute(std::function<void()> command, bool trackScoped = false);
    EchoPluginRegistration *registration(const QString &kind, const QString &id);
    QJSValue call(const QJSValue &callback, const QJSValueList &args = {});
    bool commit();
    void beginRetire();
    bool drained() const;
    QQmlEngine &engine()
    {
        return m_engine;
    }
    const PluginPackage &package() const
    {
        return m_package;
    }
    const PluginHost &host() const
    {
        return m_host;
    }
    bool storageFrozen() const
    {
        return activationContext()->m_storageFrozen;
    }
    void freezeStorage(bool frozen)
    {
        activationContext()->m_storageFrozen = frozen;
    }
    void storageBarrier(std::function<void()> ready);
    QString dataPath() const
    {
        return m_dataPath;
    }
    quint64 accountEpoch() const
    {
        return m_accountEpoch;
    }
signals:
    void activeChanged();
    void appearanceChanged();
    void resourcesDrained();
    void callbackFailed(const QString &message);

protected:
    bool eventFilter(QObject *, QEvent *) override;

private:
    QPointer<EchoPluginContext> m_activation;
    QPointer<QObject> m_presentationOwner;
    QString m_contextId;
    QList<QPointer<EchoPluginContext>> m_presentations;
    quint64 m_presentationSequence = 0;
    QString m_dataPath;
    PluginPackage m_package;
    quint64 m_generation, m_revision = 0, m_accountEpoch = 0;
    QQmlEngine &m_engine;
    PluginHost m_host;
    EchoPluginAppearance *m_appearance;
    EchoPluginFonts *m_fonts;
    EchoPluginWindows *m_windows;
    EchoPluginAudio *m_audio;
    EchoPluginPlayer *m_player;
    EchoPluginPlaylist *m_playlist;
    EchoPluginFiles *m_files = nullptr;
    EchoPluginDownloads *m_downloads = nullptr;
    EchoPluginLyrics *m_lyrics;
    EchoPluginUi *m_ui;
    EchoPluginTasks *m_tasks;
    EchoPluginShortcuts *m_shortcuts;
    EchoPluginToast *m_toast;
    EchoPluginKugou *m_kugou;
    EchoPluginLyricEffects *m_lyricEffects;
    EchoPluginScroll *m_scroll;
    EchoPluginIo *m_io;
    EchoPluginSqlite *m_sqlite;
    EchoPluginBackups *m_backups;
    EchoPluginNetwork *m_net;
    EchoPluginPermissions *m_permissions;
    EchoPluginWebServer *m_webServer;
    EchoPluginRegistry *m_plugins;
    bool m_storageFrozen = false;
    bool m_accepting = true, m_committed = false, m_retiring = false;
    QList<QPointer<EchoPluginOperation>> m_operations;
    QList<QPointer<EchoPluginRegistration>> m_resources;
    QHash<QString, QPointer<EchoPluginRegistration>> m_registrations;
    QHash<QString, QJSValue> m_commands;
};
} // namespace EchoMusic
