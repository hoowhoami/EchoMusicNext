#include "RecognitionService.h"
#include "PlaybackService.h"
#include "PlaybackLibraryActions.h"
#include "UserPlaylistModel.h"
#include "PlaylistMutationService.h"
#include "ThemeStore.h"
#include "ShellSettings.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSignalSpy>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class RecognitionServiceTestAccess
{
public:
    static void complete(RecognitionService &s, const QByteArray &bytes)
    {
        s.m_request = 90001;
        s.m_state = RecognitionService::Recognizing;
        s.finish(90001, bytes);
    }
    static void late(RecognitionService &s, const QByteArray &bytes)
    {
        s.finish(90001, bytes);
    }
};
} // namespace EchoMusic
class FakeCapture : public RecognitionCapture
{
public:
    quint64 generation = 0;
    int starts = 0, stops = 0, cancels = 0;
    QString source, device;
    void start(quint64 g, const QString &s, const QString &d) override
    {
        generation = g;
        source = s;
        device = d;
        ++starts;
    }
    void stop(quint64 g) override
    {
        generation = g;
        ++stops;
    }
    void cancel() override
    {
        ++cancels;
    }
    void ready()
    {
        emit started(generation, {});
    }
};
struct Session
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore preferences{directory.filePath("preferences.ini")};
    ApiClient api{DeviceIdentity::temporary()};
    PlaybackService playback{api, preferences, nullptr, directory.filePath("playback.sqlite")};
    FakeCapture capture;
    RouteController router;
    ThemeStore theme{preferences};
    ShellSettings settings{preferences};
    RecognitionService service{api, playback, capture, preferences, router};
    Session()
    {
        service.registerRoutes(router);
        theme.setMotionEnabled(false);
        settings.setMotionEnabled(false);
    }
    ~Session()
    {
        service.shutdown();
        router.shutdown();
        playback.shutdown();
        api.shutdown();
    }
};
static QByteArray domain()
{
    QFile file(QStringLiteral(ECHO_RECOGNITION_DOMAIN));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
static QQuickItem *visual(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = visual(child, name))
            return found;
    return nullptr;
}
class RecognitionTest : public QObject
{
    Q_OBJECT
private slots:
    void pendingPermissionCancelAndStaleStartAreHarmless()
    {
        Session s;
        s.service.setAvailable(true);
        s.service.start();
        QCOMPARE(s.service.state(), RecognitionService::Recording);
        QVERIFY(s.service.starting());
        const auto old = s.capture.generation;
        s.service.stop();
        QCOMPARE(s.service.state(), RecognitionService::Idle);
        emit s.capture.started(old, {});
        QCOMPARE(s.service.state(), RecognitionService::Idle);
        QVERIFY(!s.service.busy());
        s.service.start();
        QCOMPARE(s.capture.starts, 2);
        s.capture.ready();
        QVERIFY(!s.service.starting());
        s.service.setAvailable(false);
        QCOMPARE(s.service.state(), RecognitionService::Idle);
        QVERIFY(s.capture.cancels >= 3);
    }
    void tenSecondTimerStopsOnceAndHiddenOrAccountChangedClearsWork()
    {
        Session s;
        s.service.setAvailable(true);
        s.service.setSource("mic");
        s.service.audio()->setInputDevice("fixture-device");
        s.service.start();
        QCOMPARE(s.capture.source, "mic");
        QCOMPARE(s.capture.device, "fixture-device");
        s.capture.ready();
        QTRY_COMPARE_WITH_TIMEOUT(s.capture.stops, 1, 11000);
        QCOMPARE(s.service.recordingSeconds(), 10);
        QCOMPARE(s.service.state(), RecognitionService::Recognizing);
        s.service.stop();
        QCOMPARE(s.capture.stops, 1);
        s.service.setAvailable(false);
        emit s.capture.captured(s.capture.generation, QByteArray(), "no samples");
        QCOMPARE(s.service.state(), RecognitionService::Idle);
        s.service.setAvailable(true);
        s.service.start();
        s.capture.ready();
        QVERIFY(s.api.setAccount(7, "fixture-token"));
        QCOMPARE(s.service.state(), RecognitionService::Idle);
        QVERIFY(!s.service.busy());
    }
    void actualDomainCandidatesKeepEveryRowAndIgnoreLateReply()
    {
        Session s;
        QVERIFY(!domain().isEmpty());
        s.service.setAvailable(true);
        RecognitionServiceTestAccess::complete(s.service, domain());
        QCOMPARE(s.service.state(), RecognitionService::Success);
        QCOMPARE(s.service.matches()->count(), 5);
        QCOMPARE(s.service.matches()->item(0).value("resourceId").toString(), "339415062");
        QCOMPARE(s.service.matches()->item(0).value("durationMs").toLongLong(), 265430);
        QCOMPARE(s.service.confidence(0).toDouble(), 1.);
        QVERIFY(s.service.matches()
                    ->track("339415062")
                    ->catalogMetadata.value("resources")
                    .toArray()
                    .size() >= 8);
        s.service.reset();
        RecognitionServiceTestAccess::late(s.service, domain());
        QCOMPARE(s.service.state(), RecognitionService::Idle);
        QCOMPARE(s.service.matches()->count(), 0);
        RecognitionServiceTestAccess::complete(s.service, "[]");
        QCOMPARE(s.service.state(), RecognitionService::Failed);
        QVERIFY(s.service.errorMessage().contains(QStringLiteral("未识别到歌曲")));
    }
    void permissionAndDeviceErrorsPreserveRetryAndSettings()
    {
        Session s;
        s.service.setAvailable(true);
        s.service.setSource("mic");
        s.service.start();
        emit s.capture.started(s.capture.generation, "microphone permission denied");
        QCOMPARE(s.service.state(), RecognitionService::Failed);
        QVERIFY(s.service.errorMessage().contains(QStringLiteral("没有麦克风权限")));
        s.service.start();
        s.capture.ready();
        s.service.stop();
        emit s.capture.captured(s.capture.generation, {}, "input device is no longer available");
        QCOMPARE(s.service.state(), RecognitionService::Failed);
        QVERIFY(s.service.errorMessage().contains(QStringLiteral("已断开")));
        QCOMPARE(s.preferences.value("recognition/source").toString(), "mic");
        s.service.shutdown();
        const auto starts = s.capture.starts;
        s.service.setAvailable(true);
        s.service.start();
        QCOMPARE(s.capture.starts, starts);
    }
    void productionPageStatesAndPopupLifecycle_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<int>("zoom");
        for (bool dark : {false, true})
            for (int zoom : {100, 125})
                QTest::newRow(qPrintable(QString("%1-%2").arg(dark ? "dark" : "light").arg(zoom)))
                    << dark << zoom;
    }
    void productionPageStatesAndPopupLifecycle()
    {
        QFETCH(bool, dark);
        QFETCH(int, zoom);
        Session s;
        QQmlEngine engine;
        s.theme.attachToEngine(engine);
        s.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
        if (zoom == 125)
            for (int i = 0; i < 5; ++i)
                s.settings.zoomIn();
        QVERIFY(s.router.navigate("recognize"));
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &items) {
            for (const auto &warning : items)
                warnings.append(warning.toString());
        });
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/Discovery/RecognizePage.qml"));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"context", QVariant::fromValue(s.router.current())},
             {"router", QVariant::fromValue(&s.router)},
             {"settings", QVariant::fromValue(&s.settings)},
             {"service", QVariant::fromValue(&s.service)}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(page);
        QQuickWindow window;
        window.resize(900, 620);
        window.setColor(s.theme.main());
        page->setParentItem(window.contentItem());
        page->setWidth(window.width() / s.settings.uiScale());
        page->setHeight(window.height() / s.settings.uiScale());
        page->setTransformOrigin(QQuickItem::TopLeft);
        page->setScale(s.settings.uiScale());
        QCOMPARE(page->mapRectToScene(page->boundingRect()), QRectF(0, 0, 900, 620));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto artifacts = qEnvironmentVariable("ECHO_RECOGNITION_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        const auto capture = [&](const QString &state) {
            QTest::qWait(80);
            auto *viewport = visual(page, "recognitionScroll");
            if (!qFuzzyCompare(viewport->property("contentHeight").toReal(), viewport->height()))
                return false;
            if (!artifacts.isEmpty()) {
                auto *scroll = visual(page, "recognitionScroll");
                auto *stage = visual(page, "recognitionStage");
                qInfo() << state << "page" << page->mapRectToScene(page->boundingRect())
                        << "viewport" << scroll->width() << scroll->height() << "content"
                        << scroll->property("contentX") << scroll->property("contentY") << "stage"
                        << stage->mapRectToScene(stage->boundingRect());
            }
            if (artifacts.isEmpty())
                return true;
            return window.grabWindow().save(
                artifacts +
                QString("/%1-%2-%3.png").arg(state).arg(dark ? "dark" : "light").arg(zoom));
        };
        QVERIFY(capture("idle"));
        QCOMPARE(s.service.state(), RecognitionService::Idle);
        auto *owner = page->findChild<QObject *>("recognitionSourceOwner");
        QVERIFY(owner);
        QVERIFY(!owner->property("item").value<QObject *>());
        auto *source = visual(page, "recognitionSourceToggle");
        QVERIFY(source);
        QTest::mouseClick(
            &window, Qt::LeftButton, Qt::NoModifier,
            source->mapToScene(QPointF(source->width() / 2, source->height() / 2)).toPoint());
        QPointer<QObject> popup = owner->property("item").value<QObject *>();
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(capture("source"));
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QTRY_VERIFY(popup.isNull());
        auto *button = visual(page, "recognitionMainButton");
        QVERIFY(button);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene({70, 70}).toPoint());
        QCOMPARE(s.service.state(), RecognitionService::Recording);
        s.capture.ready();
        QVERIFY(capture("recording"));
        s.service.stop();
        QVERIFY(capture("recognizing"));
        emit s.capture.captured(s.capture.generation, {}, "no samples");
        QVERIFY(capture("failed"));
        s.service.reset();
        auto displayRows = QJsonDocument::fromJson(domain()).array();
        for (int i = 0; i < displayRows.size(); ++i) {
            auto match = displayRows[i].toObject();
            auto song = match.value("song").toObject();
            song.insert("cover_url", QString());
            match.insert("song", song);
            displayRows[i] = match;
        }
        RecognitionServiceTestAccess::complete(s.service, QJsonDocument(displayRows).toJson());
        QCOMPARE(s.service.matches()->count(), 5);
        QVERIFY(capture("result"));
        QVERIFY(visual(page, "recognitionMatches"));
        s.service.setAvailable(false);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window.close();
        page->setParentItem(nullptr);
    }
    void playlistOnlyTargetsDoNotChangeOtherAddEntrypoints()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        ApiClient api(DeviceIdentity::temporary());
        UserPlaylistModel library(api);
        PlaylistMutationService writer(api);
        PlaybackService playback(api, settings, nullptr, directory.filePath("playback.sqlite"));
        playback.attachLibrary(library, writer);
        FakeCapture capture;
        RouteController router;
        RecognitionService recognition(api, playback, capture, settings, router);
        recognition.registerRoutes(router);
        recognition.setAvailable(true);
        RecognitionServiceTestAccess::complete(recognition, domain());
        const auto track = recognition.matches()->track("339415062");
        QVERIFY(track);
        playback.queueCatalog()->activate("recognition-test", "识曲目标", {*track}, 0);
        auto *actions = playback.libraryActions();
        QSignalSpy shown(actions, &PlaybackLibraryActions::dialogRequested);
        actions->prepareTracks({*track});
        QVERIFY(actions->rowCount() > 0);
        recognition.addSong("339415062");
        QCOMPARE(shown.size(), 2);
        QCOMPARE(actions->rowCount(), 0);
        actions->prepareTracks({*track});
        QVERIFY(actions->rowCount() > 0);
        recognition.shutdown();
        router.shutdown();
        playback.shutdown();
        api.shutdown();
    }
    void liveRecognitionThroughQtAndRust()
    {
        const auto path = qEnvironmentVariable("ECHO_LIVE_RECOGNITION_PCM");
        if (path.isEmpty())
            QSKIP("Opt-in real PCM upload through native Qt/Rust path");
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto pcm = file.readAll();
        QCOMPARE(pcm.size(), 160000);
        Session s;
        s.service.setAvailable(true);
        s.service.start();
        s.capture.ready();
        s.service.stop();
        emit s.capture.captured(s.capture.generation, pcm, {});
        QTRY_VERIFY_WITH_TIMEOUT(!s.service.busy(), 30000);
        QCOMPARE(s.service.state(), RecognitionService::Success);
        QVERIFY(s.service.matches()->count() > 0);
        QVERIFY(!s.service.matches()->item(0).value("title").toString().isEmpty());
    }
};
QTEST_MAIN(RecognitionTest)
#include "RecognitionTest.moc"
