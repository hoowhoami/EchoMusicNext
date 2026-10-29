#include "AboutService.h"
#include "SettingsMarkdownRenderer.h"
#include "ThemeStore.h"
#include "UiTypography.h"
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include "NetworkBootstrap.h"
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QStandardPaths>

#include <QTemporaryDir>
#include <QTextDocument>
#include <QTextBlock>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class AboutServiceTestAccess
{
public:
    static void endpoint(AboutService &s, QUrl url)
    {
        s.m_releaseEndpoint = url;
    }
    static void prepareDownload(AboutService &s, const QUrl &url, const QByteArray &payload)
    {
        s.m_distribution = true;
        s.m_packageKind = "appimage";
        s.m_resultState = AboutService::UpdateAvailable;
        s.m_release.version = "9.0.0";
        s.m_asset = {"test.AppImage", "appimage", payload.size(),
                     QCryptographicHash::hash(payload, QCryptographicHash::Sha512).toHex(), url};
    }
    static QString package(const AboutService &s)
    {
        return s.m_packagePath;
    }
    static void state(AboutService &s, AboutService::DownloadState state)
    {
        s.m_release.notes = "## 更新说明\n\n中文说明与 Release notes。";
        s.m_downloadState = state;
        s.m_downloadError =
            state == AboutService::DownloadFailed ? "更新文件校验失败，请重新下载" : QString();
        s.m_received = 42;
        s.m_total = 100;
        emit s.changed();
    }
};
} // namespace EchoMusic
class HttpServer : public QTcpServer
{
public:
    QByteArray body = "[]";
    int requests = 0, status = 200;
    bool hold = false;
    std::function<QByteArray(const QByteArray &)> respond;
    HttpServer()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    auto bytes = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", bytes);
                    if (!bytes.contains("\r\n\r\n") || socket->property("handled").toBool())
                        return;
                    socket->setProperty("handled", true);
                    ++requests;
                    if (hold)
                        return;
                    const auto payload = respond ? respond(bytes) : body;
                    socket->write(
                        "HTTP/1.1 " + QByteArray::number(status) +
                        " Response\r\nContent-Length: " + QByteArray::number(payload.size()) +
                        "\r\nConnection: close\r\n\r\n" + payload);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QUrl url() const
    {
        return QUrl(QString("http://127.0.0.1:%1/releases").arg(serverPort()));
    }
};
class AboutServiceTest : public QObject
{
    Q_OBJECT
private slots:
    void semverPrecedence()
    {
        const QStringList ordered{"1.0.0-alpha",  "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                                  "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1",       "1.0.0",
                                  "1.0.1",        "2.0.0"};
        for (qsizetype i = 0; i < ordered.size(); ++i) {
            QVERIFY(ReleaseManifest::validVersion(ordered[i]));
            for (qsizetype j = i + 1; j < ordered.size(); ++j) {
                QVERIFY(ReleaseManifest::compareVersions(ordered[i], ordered[j]) < 0);
                QVERIFY(ReleaseManifest::compareVersions(ordered[j], ordered[i]) > 0);
            }
        }
        QCOMPARE(ReleaseManifest::compareVersions("1.2.3+build.9", "1.2.3+other"), 0);
        for (const auto &bad : {"v1.0.0", "01.0.0", "1.0", "1.0.0-beta.01", "1.0.0-", "../1.0.0"})
            QVERIFY(!ReleaseManifest::validVersion(bad));
    }
    void releaseOriginAndPlatformValidation()
    {
        QString error;
        const QString prefix = "https://github.com/hoowhoami/EchoMusicNext/releases/";
        QJsonObject release{{"tag_name", "v9.0.0"},
                            {"html_url", prefix + "tag/v9.0.0"},
                            {"prerelease", true},
                            {"assets", QJsonArray{QJsonObject{
                                           {"name", "update-Windows-x64.json"},
                                           {"browser_download_url",
                                            prefix + "download/v9.0.0/update-Windows-x64.json"}}}}};
        auto parsed = ReleaseManifest::releases(QJsonDocument(QJsonArray{release}).toJson(),
                                                "Windows", "x64", &error);
        QCOMPARE(parsed.size(), 1);
        QVERIFY(parsed[0].prerelease);
        QVERIFY(!parsed[0].manifestUrl.isEmpty());
        const auto published = parsed[0];
        release["html_url"] = "https://example.com/releases/tag/v9.0.0";
        QVERIFY(ReleaseManifest::releases(QJsonDocument(QJsonArray{release}).toJson(), "Windows",
                                          "x64", &error)
                    .isEmpty());
        QJsonObject asset{{"name", "EchoMusicNext-9.0.0-Windows-x64-Setup.exe"},
                          {"kind", "nsis"},
                          {"size", 8},
                          {"sha512", QString(128, 'a')}};
        QJsonObject manifest{
            {"schema", 1},        {"applicationId", "io.github.hoowhoami.echomusicnext"},
            {"version", "9.0.0"}, {"platform", "Windows"},
            {"arch", "x64"},      {"assets", QJsonArray{asset}}};
        auto assets = ReleaseManifest::assets(QJsonDocument(manifest).toJson(), published,
                                              "Windows", "x64", &error);
        QCOMPARE(assets.size(), 1);
        QVERIFY(error.isEmpty());
        QCOMPARE(assets[0].url.toString(), prefix + "download/v9.0.0/" + asset["name"].toString());
        manifest["arch"] = "arm64";
        QVERIFY(ReleaseManifest::assets(QJsonDocument(manifest).toJson(), published, "Windows",
                                        "x64", &error)
                    .isEmpty());
        manifest["arch"] = "x64";
        asset["sha512"] = "bad";
        manifest["assets"] = QJsonArray{asset};
        QVERIFY(ReleaseManifest::assets(QJsonDocument(manifest).toJson(), published, "Windows",
                                        "x64", &error)
                    .isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void singleFlightAndChannelCancellation()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("preferences.ini"));
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences network(settings, api);
        HttpServer server;
        AboutService about(settings, network);
        AboutServiceTestAccess::endpoint(about, server.url());
        QVERIFY(about.changelog().contains("0.1.0"));
        about.check();
        about.check();
        QTRY_VERIFY(!about.checking());
        QCOMPARE(server.requests, 1);
        QCOMPARE(about.resultState(), AboutService::NoPublishedRelease);
        QVERIFY(about.resultOpen());
        about.closeResult();
        about.check(true);
        QTRY_VERIFY(!about.checking());
        QVERIFY(!about.resultOpen());
        server.hold = true;
        about.check();
        QTRY_VERIFY(server.requests >= 2);
        about.setPrerelease(true);
        QVERIFY(!about.checking());
        QVERIFY(about.statusText().isEmpty());
        about.shutdown();
        api.shutdown();
    }
    void discoveryFetchesMatchingManifestAndHonorsChannel()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("preferences.ini"));
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences network(settings, api);
        HttpServer server;
        network.setGithubProxyUrl(server.url().toString());
        const auto os = ReleaseManifest::platform(), arch = ReleaseManifest::architecture();
        const auto kind = os == "macOS" ? "dmg" : os == "Windows" ? "nsis" : "tar";
        const auto suffix = os == "macOS" ? ".dmg" : os == "Windows" ? "-Setup.exe" : ".tar.gz";
        const QString prefix = "https://github.com/hoowhoami/EchoMusicNext/releases/";
        QStringList manifestVersions;
        server.respond = [&](const QByteArray &request) {
            if (request.contains("update-")) {
                const QString version =
                    request.contains("v9.0.0-beta.1") ? "9.0.0-beta.1" : "8.0.0";
                manifestVersions.append(version);
                const QJsonObject asset{
                    {"name", "EchoMusicNext-" + version + '-' + os + '-' + arch + suffix},
                    {"kind", kind},
                    {"size", 10},
                    {"sha512", QString(128, 'a')}};
                return QJsonDocument(
                           QJsonObject{{"schema", 1},
                                       {"applicationId", "io.github.hoowhoami.echomusicnext"},
                                       {"version", version},
                                       {"platform", os},
                                       {"arch", arch},
                                       {"assets", QJsonArray{asset}}})
                    .toJson();
            }
            QJsonArray releases;
            for (const auto &version : {QString("8.0.0"), QString("9.0.0-beta.1")})
                releases.append(QJsonObject{
                    {"tag_name", "v" + version},
                    {"html_url", prefix + "tag/v" + version},
                    {"prerelease", version.contains('-')},
                    {"body", "notes " + version},
                    {"assets",
                     QJsonArray{QJsonObject{
                         {"name", ReleaseManifest::manifestName(os, arch)},
                         {"browser_download_url", prefix + "download/v" + version + '/' +
                                                      ReleaseManifest::manifestName(os, arch)}}}}});
            return QJsonDocument(releases).toJson();
        };
        AboutService about(settings, network);
        AboutServiceTestAccess::endpoint(about, server.url());
        about.check();
        QTRY_VERIFY(!about.checking());
        QCOMPARE(about.resultState(), AboutService::UpdateAvailable);
        QCOMPARE(about.releaseVersion(), QString("8.0.0"));
        QCOMPARE(manifestVersions, QStringList{"8.0.0"});
        about.setPrerelease(true);
        about.check();
        QTRY_VERIFY(!about.checking());
        QCOMPARE(about.releaseVersion(), QString("9.0.0-beta.1"));
        QCOMPARE(manifestVersions.last(), QString("9.0.0-beta.1"));
        about.shutdown();
        api.shutdown();
    }
    void downloadIntegrityCancelAndReopen()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("preferences.ini"));
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences network(settings, api);
        HttpServer server;
        const QByteArray payload(512 * 1024, 'x');
        server.body = payload;
        AboutService about(settings, network);
        AboutServiceTestAccess::prepareDownload(about, server.url(), payload);
        about.download();
        about.closeResult();
        QTRY_COMPARE(about.downloadState(), AboutService::Downloaded);
        QCOMPARE(about.downloadPercent(), 100.0);
        QFile file(AboutServiceTestAccess::package(about));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), payload);
        file.close();
        about.check();
        QVERIFY(about.resultOpen());
        about.cancelDownload();
        QVERIFY(!QFile::exists(file.fileName()));
        server.body[0] = 'z';
        about.download();
        QTRY_COMPARE(about.downloadState(), AboutService::DownloadFailed);
        QVERIFY(about.downloadError().contains(QStringLiteral("校验失败")));
        about.cancelDownload();
        server.hold = true;
        const auto previous = server.requests;
        about.download();
        QTRY_COMPARE(server.requests, previous + 1);
        about.cancelDownload();
        QCOMPARE(about.downloadState(), AboutService::Idle);
        QTest::qWait(50);
        QCOMPARE(about.downloadState(), AboutService::Idle);
        about.shutdown();
        api.shutdown();
    }
    void updatePopupCreatesOnDemandAndRestoresServiceState()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("preferences.ini"));
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences network(settings, api);
        AboutService about(settings, network);
        AboutServiceTestAccess::prepareDownload(about, QUrl("https://example.invalid/package"),
                                                "payload");
        ThemeStore theme(settings);
        theme.setMotionEnabled(false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &items) {
            for (const auto &error : items)
                warnings.append(error.toString());
        });
        QQmlComponent component(&engine,
                                QUrl("qrc:/qt/qml/EchoMusic/UpdateTests/UpdateDialogHarness.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"about", QVariant::fromValue(&about)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(window);
        auto *owner = root->property("popupOwner").value<QObject *>();
        QVERIFY(owner);
        QVERIFY(!owner->property("item").value<QObject *>());
        QTest::qWait(50);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (const auto state : {AboutService::Downloading, AboutService::Downloaded,
                                     AboutService::Installing, AboutService::DownloadFailed}) {
                AboutServiceTestAccess::state(about, state);
                QVERIFY(QMetaObject::invokeMethod(root.get(), "showUpdate"));
                QTRY_VERIFY(owner->property("opened").toBool());
                QPointer<QObject> popup = owner->property("item").value<QObject *>();
                QVERIFY(popup);
                QCOMPARE(popup->property("downloading").toBool(),
                         state == AboutService::Downloading);
                QCOMPARE(popup->property("downloaded").toBool(), state == AboutService::Downloaded);
                QCOMPARE(popup->property("installing").toBool(), state == AboutService::Installing);
                QTest::qWait(30);
                auto *content = popup->property("contentItem").value<QQuickItem *>();
                QVERIFY(content);
                QVERIFY(content->width() > 0 && content->height() > 0 && content->isVisible());
                if (const auto output = qEnvironmentVariable("ECHO_UPDATE_UI_OUTPUT");
                    !output.isEmpty()) {
                    const auto image = window->grabWindow();
                    QVERIFY(!image.isNull());
                    QDir().mkpath(output);
                    QVERIFY(image.save(
                        output + QString("/update-%1-%2.png").arg(int(mode)).arg(int(state))));
                }
                QVERIFY(QMetaObject::invokeMethod(root.get(), "closeUpdate"));
                QTRY_VERIFY(!popup);
                QVERIFY(!owner->property("item").value<QObject *>());
                QCOMPARE(about.downloadState(), state);
            }
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        root.reset();
        about.shutdown();
        api.shutdown();
    }
    void nativeMarkdownDoesNotLoadImagesOrUnsafeLinks()
    {
        SettingsMarkdownRenderer renderer;
        QFont font;
        font.setFamilies({"Helvetica Neue", "PingFang SC", "sans-serif"});
        font.setPixelSize(14);
        const auto html = renderer.render(
            "# 标题 Heading\n\n说明 "
            "**Bold**\n\n[发布页](https://github.com/hoowhoami/EchoMusicNext)\n\n[本地](file:///"
            "private/secret) ![图片](https://example.invalid/pixel.png)",
            font, Qt::black, Qt::gray, Qt::blue);
        QTextDocument document;
        document.setHtml(html);
        QVERIFY(document.toPlainText().contains("标题 Heading"));
        int links = 0;
        for (auto block = document.begin(); block.isValid(); block = block.next())
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const auto format = it.fragment().charFormat();
                QVERIFY(!format.isImageFormat());
                if (format.isAnchor()) {
                    ++links;
                    QCOMPARE(QUrl(format.anchorHref()).scheme(), QString("https"));
                }
            }
        QCOMPARE(links, 1);
    }
};
int main(int argc, char **argv)
{
    initializeNetworkEnvironment();
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    configureUiTypography();
    QCoreApplication::setApplicationName("EchoMusicNext-update-tests");
    QStandardPaths::setTestModeEnabled(true);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    AboutServiceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "AboutServiceTest.moc"
