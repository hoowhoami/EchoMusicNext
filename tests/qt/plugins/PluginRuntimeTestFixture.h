#include "PluginPaths.h"
#include "PluginInstallationStore.h"
#include "PluginInstallTransaction.h"
#pragma once
#include "PluginRuntime.h"
#include "../../../app/ApplicationShutdown.h"
#include "PluginManager.h"
#include "SharingCommands.h"
#include "StorageSnapshot.h"
#include "PluginMarketplaceStats.h"
#include "EchoPluginUi.h"
#include "EchoPluginIo.h"
#include "EchoPluginAudio.h"
#include "PlayerSpectrum.h"
#include "FloatingPlayerController.h"
#include <QDataStream>
#include <QWheelEvent>
#include <QProcess>
#include <QProcessEnvironment>
#include <cmath>
#include <ctime>
#include <optional>
#include "PlaybackService.h"
#include "AccountService.h"
#include "ShellController.h"
#include "ThemeStore.h"
#include "ShellSettings.h"
#include "SidebarLayoutModel.h"
#include "ShortcutService.h"
#include "VerificationService.h"
#include "NetworkPreferences.h"
#include <QNetworkProxy>
#include <QScopeGuard>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QAbstractItemModelTester>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <ApplicationUrlHandler.h>
#include <QFileOpenEvent>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include "UiTypography.h"
#include "NativeWindow.h"
#include <QQmlComponent>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic::PluginTest
{
inline QQuickItem *visual(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *item = visual(child, name))
            return item;
    return nullptr;
}
class DirectoryCapture : public QObject
{
    Q_OBJECT
public:
    QUrl opened;
    ~DirectoryCapture() override
    {
        QDesktopServices::unsetUrlHandler("file");
    }
public slots:
    void open(const QUrl &url)
    {
        opened = url;
    }
};
inline void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        qFatal("Cannot write fixture");
}
inline QString package(const QString &root, const QByteArray &entry,
                       QStringList features = {"lifecycle"}, QStringList capabilities = {})
{
    const auto path = root + "/source";
    QJsonObject manifest{
        {"schemaVersion", 1},
        {"format", "echomod"},
        {"id", "test.runtime"},
        {"name", "Runtime integration"},
        {"version", "1.0.0"},
        {"kind", "qml-js"},
        {"entry", "entry.mjs"},
        {"requires", QJsonObject{{"hostVersion", ">=0.1.0 <1.0.0"},
                                 {"sdkVersion", ">=1.0.0 <2.0.0"},
                                 {"features", QJsonArray::fromStringList(features)}}},
        {"capabilities", QJsonArray::fromStringList(capabilities)},
        {"contexts", QJsonArray{"main"}}};
    write(path + "/manifest.json", QJsonDocument(manifest).toJson());
    write(path + "/entry.mjs", entry);
    return path;
}
struct Host
{
    QTemporaryDir temporary;
    EchoMusic::SettingsStore settings{temporary.path() + "/settings.ini"};
    ApiClient api{DeviceIdentity::temporary()};
    AccountService account{api};
    PlaybackService playback{api, settings, nullptr, temporary.path() + "/playback.sqlite"};
    ThemeStore theme{settings};
    RouteController router;
    ShellSettings shellSettings{settings};
    ShellController shell{settings, router};
    FloatingPlayerController floating{settings, false};
    SidebarLayoutModel sidebar{settings, nullptr};
    ShortcutService shortcuts{settings, false};
    VerificationService verification{api};
    QQmlEngine engine;
    PluginRuntime runtime{engine,
                          settings,
                          {playback,
                           theme,
                           shell,
                           router,
                           account,
                           api,
                           &floating,
                           nullptr,
                           &sidebar,
                           &shortcuts,
                           &verification,
                           nullptr,
                           nullptr,
                           {},
                           &shellSettings},
                          temporary.path() + "/plugins"};
    explicit Host(const std::function<void(const QString &)> &prepareRoot = {})
    {
        theme.attachToEngine(engine);
        router.registerPage(
            {"search", "/search", QUrl("qrc:/search.qml"), {}, false, false, false, {}});
        router.navigate("search");
        QObject::connect(&runtime, &PluginRuntime::pluginFailed, &runtime,
                         [](const QString &id, const QString &message) {
                             qWarning().noquote() << id << message;
                         });
        if (prepareRoot)
            prepareRoot(runtime.directory());
        runtime.start();
        QElapsedTimer deadline;
        deadline.start();
        while (runtime.loading() && deadline.elapsed() < 7000)
            QTest::qWait(1);
        if (runtime.loading())
            qFatal("Plugin startup discovery did not settle");
    }
    ~Host()
    {
        QEventLoop loop;
        QObject::connect(&runtime, &PluginRuntime::closed, &loop, &QEventLoop::quit);
        QTimer::singleShot(7000, &loop, &QEventLoop::quit);
        runtime.shutdown();
        loop.exec();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        playback.shutdown();
        api.shutdown();
        // Temp-profile image/private caches must not outlive a drained fixture.
        QDir(PluginPaths(runtime.directory()).cache()).removeRecursively();
    }
    QString installation()
    {
        return runtime.data(runtime.index(0), PluginRuntime::InstallationRole).toString();
    }
};
inline QString pluginPackage(const QString &root, const QString &id, const QByteArray &entry,
                             const QJsonObject &extra = {})
{
    const auto source = package(root + '/' + id, entry);
    QFile file(source + "/manifest.json");
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read plugin fixture");
    auto manifest = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    manifest["id"] = id;
    for (auto it = extra.begin(); it != extra.end(); ++it)
        manifest[it.key()] = it.value();
    write(source + "/manifest.json", QJsonDocument(manifest).toJson());
    return source;
}
} // namespace EchoMusic::PluginTest
using namespace EchoMusic::PluginTest;
