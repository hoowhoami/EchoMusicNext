#include "PersonalFmController.h"
#include "PlaybackService.h"
#include "DiscoveryService.h"
#include "DiscoveryRouteCoordinator.h"
#include "AccountService.h"
#include "NetworkPreferences.h"
#include "RouteController.h"
#include "ShellSettings.h"
#include "ThemeStore.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWheelEvent>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QNetworkProxy>
#include <QScopeGuard>
#include <QSet>
#include <QTemporaryDir>
#include <QProcess>
#include <QSqlQuery>
#include <QUuid>
#include <QtTest>
#include <iostream>
using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static void hold(PlaybackService &p)
    {
        p.m_ready = false;
    }
    static AudioEngine *engine(PlaybackService &p)
    {
        return &p.m_engine;
    }
    static void loaded(PlaybackService &p)
    {
        p.m_loading = false;
        p.m_loaded = true;
        p.m_error.clear();
        emit p.changed();
    }
    static void failure(PlaybackService &p)
    {
        p.m_loading = false;
        p.m_error = "candidate unavailable";
        emit p.changed();
    }
};
class TrackTransitionControllerTestAccess
{
public:
    static bool prepared(const TrackTransitionController &transition)
    {
        return transition.m_prepared;
    }
    static quint64 sequence(const TrackTransitionController &transition)
    {
        return transition.m_sequence;
    }
};
class PersonalFmControllerTestAccess
{
public:
    static quint64 resetRequest(PersonalFmController &fm)
    {
        return fm.m_resetRequest;
    }
    static quint64 refillRequest(PersonalFmController &fm)
    {
        return fm.m_refillRequest;
    }
    static int bufferCount(PersonalFmController &fm)
    {
        return fm.m_buffer.size();
    }
    static int requestCount(PersonalFmController &fm)
    {
        return fm.m_requests.size();
    }
    static QList<quint64> requests(PersonalFmController &fm)
    {
        return fm.m_requests.keys();
    }
    static void complete(PersonalFmController &fm, quint64 id, const QJsonArray &songs)
    {
        fm.succeeded(id, QJsonDocument(QJsonObject{{"songs", songs}}).toJson());
    }
    static void failed(PersonalFmController &fm, quint64 id)
    {
        fm.failed(id, "request failed");
    }
    static ApiClient::FmRequest parameters(PersonalFmController &fm, const PlaybackTrack &track)
    {
        return fm.parameters(ApiClient::FmAction::Play, &track);
    }
    static void refill(PersonalFmController &fm)
    {
        fm.refill();
    }
};
} // namespace EchoMusic
namespace
{
QJsonArray songs()
{
    QJsonArray result;
    for (int i = 0; i < 5; ++i)
        result.append(QJsonObject{
            {"id", QString::number(32112294 + i)},
            {"hash", QString(32, QChar('a' + i))},
            {"title", QString("私人 FM 曲目 %1").arg(i + 1)},
            {"artists", QJsonArray{QJsonObject{{"name", "领域测试歌手"}, {"id", 7}}}},
            {"album_id", "968123"},
            {"album_title", "领域测试专辑"},
            {"duration_ms", 256000},
            {"cover_url",
             "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='180' "
             "height='180'><rect width='180' height='180' fill='%233f6987'/></svg>"},
            {"metadata", QJsonObject{{"audio_id", 1308303 + i},
                                     {"language", "国语"},
                                     {"recommendation", QJsonObject{{"headline", "推荐理由"}}}}},
            {"personal_fm", QJsonObject{{"mark", "track-mark"}, {"similarity", "80%"}}}});
    return result;
}
QQuickItem *visual(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = visual(child, name))
            return found;
    return nullptr;
}
struct Fixture
{
    QTemporaryDir dir;
    EchoMusic::SettingsStore prefs{dir.filePath("prefs.ini")};
    ApiClient api{DeviceIdentity::temporary()};
    std::unique_ptr<PlaybackService> playback;
    Fixture()
    {
        prefs.setValue("playback/volume", 0);
        prefs.setValue("playback/transitionMode", "none");
        api.setNetworkTransport(1); // Loopback refusal; test never contacts a provider.
        api.setAccount(900001, "offline-domain-fixture");
        playback = std::make_unique<PlaybackService>(api, prefs);
        PlaybackServiceTestAccess::hold(*playback);
    }
    PersonalFmController &fm()
    {
        return *playback->personalFm();
    }
    void preview()
    {
        fm().setPageActive(true, true);
        PersonalFmControllerTestAccess::complete(
            fm(), PersonalFmControllerTestAccess::resetRequest(fm()), songs());
    }
};
} // namespace
class PersonalFmTest : public QObject
{
    Q_OBJECT
private slots:
    void preferencesSurviveProcessRestartAndFailureRollback()
    {
        QTemporaryDir dir;
        for (const auto &stage : {"read-0-0", "write-3-2", "read-3-2", "write-1-1", "read-1-1",
                                  "rollback-1-1", "read-1-1"}) {
            QProcess child;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("ECHO_FM_PREFS_PROBE", dir.filePath("prefs.ini"));
            environment.insert("ECHO_FM_PREFS_STAGE", stage);
            environment.insert("QT_QPA_PLATFORM", "offscreen");
            child.setProcessEnvironment(environment);
            child.setProcessChannelMode(QProcess::MergedChannels);
            child.start(QCoreApplication::applicationFilePath(), {"preferencesRestartWorker"});
            QVERIFY(child.waitForFinished(20000));
            const auto output = child.readAll();
            QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                     output.constData());
        }
    }
    void preferencesRestartWorker()
    {
        const auto path = qEnvironmentVariable("ECHO_FM_PREFS_PROBE");
        if (path.isEmpty())
            QSKIP("Invoked only by the isolated process-restart test");
        const auto stage = qEnvironmentVariable("ECHO_FM_PREFS_STAGE").split('-');
        QCOMPARE(stage.size(), 3);
        const int mode = stage[1].toInt(), pool = stage[2].toInt();
        EchoMusic::SettingsStore prefs(path);
        ApiClient api(DeviceIdentity::temporary());
        api.setNetworkTransport(1);
        QVERIFY(api.setAccount(900001, "offline-domain-fixture"));
        QTemporaryDir dir;
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        PlaybackServiceTestAccess::hold(playback);
        auto &fm = *playback.personalFm();
        if (stage[0] != "read") {
            fm.setPageActive(true, true);
            PersonalFmControllerTestAccess::complete(
                fm, PersonalFmControllerTestAccess::resetRequest(fm), songs());
            fm.setMode(stage[0] == "rollback" ? 2 : mode);
            auto request = PersonalFmControllerTestAccess::resetRequest(fm);
            if (request) {
                if (stage[0] == "rollback")
                    PersonalFmControllerTestAccess::failed(fm, request);
                else
                    PersonalFmControllerTestAccess::complete(fm, request, songs());
            }
            fm.setSongPool(stage[0] == "rollback" ? 2 : pool);
            request = PersonalFmControllerTestAccess::resetRequest(fm);
            if (request) {
                if (stage[0] == "rollback")
                    PersonalFmControllerTestAccess::failed(fm, request);
                else
                    PersonalFmControllerTestAccess::complete(fm, request, songs());
            }
        }
        QCOMPARE(fm.mode(), mode);
        QCOMPARE(fm.songPool(), pool);
        QVERIFY(!fm.active()); // Preference restore never starts an FM session.
        QVERIFY(!playback.hasTrack());
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void previewAndCandidateCommitHaveSeparateOwnership()
    {
        Fixture f;
        const auto ordinary = PlaybackTrack::fromDomain(songs().last().toObject());
        f.playback->playTracks({ordinary}, 0, "其它来源", "other");
        f.preview();
        QCOMPARE(f.playback->sourceKey(), QString("other"));
        QVERIFY(!f.playback->queueCatalog()->modelForKey(PersonalFmController::queueKey()));
        QCOMPARE(PersonalFmControllerTestAccess::bufferCount(f.fm()), 5);
        f.fm().play();
        auto *history = f.playback->queue();
        QCOMPARE(history->rowCount(), 0); // URL preparation is not an audible commit.
        QCOMPARE(PersonalFmControllerTestAccess::bufferCount(f.fm()), 5);
        PlaybackServiceTestAccess::loaded(*f.playback);
        QCOMPARE(history->rowCount(), 1);
        QCOMPARE(PersonalFmControllerTestAccess::bufferCount(f.fm()), 4);
        const auto refill = PersonalFmControllerTestAccess::refillRequest(f.fm());
        QVERIFY(refill);
        PersonalFmControllerTestAccess::refill(f.fm());
        QCOMPARE(PersonalFmControllerTestAccess::refillRequest(f.fm()), refill);
        PersonalFmControllerTestAccess::complete(f.fm(), refill, {});
        PersonalFmControllerTestAccess::refill(f.fm());
        QCOMPARE(PersonalFmControllerTestAccess::refillRequest(f.fm()), quint64(0));
        const auto query =
            PersonalFmControllerTestAccess::parameters(f.fm(), f.playback->currentTrack());
        QCOMPARE(query.audioId.value(), quint64(1308303));
        QCOMPARE(query.mark, QString("track-mark"));
        auto missingAudio = f.playback->currentTrack();
        missingAudio.catalogMetadata.remove("audio_id");
        QCOMPARE(PersonalFmControllerTestAccess::parameters(f.fm(), missingAudio).audioId.value(),
                 missingAudio.songId);
        QVERIFY(!missingAudio.catalogMetadata.contains("audio_id"));
        f.playback->next(true); // FM ignores the ordinary list loop/shuffle mode.
        QCOMPARE(history->rowCount(), 1);
        const auto failed = f.playback->trackId();
        PlaybackServiceTestAccess::failure(*f.playback);
        QCOMPARE(history->rowCount(), 1);
        QVERIFY(f.fm().nextTrack());
        QVERIFY(f.fm().nextTrack()->id != failed);
        f.playback->next();
        PlaybackServiceTestAccess::loaded(*f.playback);
        QCOMPARE(history->rowCount(), 2);
    }
    void resetCancellationAndAccountEpochRejectStaleWork()
    {
        Fixture f;
        f.fm().setPageActive(true, true);
        const auto retired = PersonalFmControllerTestAccess::resetRequest(f.fm());
        f.fm().setPageActive(false, true);
        PersonalFmControllerTestAccess::complete(f.fm(), retired, songs());
        QVERIFY(f.fm().current().isEmpty());
        QCOMPARE(PersonalFmControllerTestAccess::requestCount(f.fm()), 0);
        f.preview();
        f.fm().setMode(2);
        const auto modeRequest = PersonalFmControllerTestAccess::resetRequest(f.fm());
        QCOMPARE(f.fm().mode(), 2);
        PersonalFmControllerTestAccess::failed(f.fm(), modeRequest);
        QCOMPARE(f.fm().mode(), 0);
        QCOMPARE(f.prefs.value("personalFm/mode").toInt(), 0);
        f.fm().setSongPool(2);
        const auto accountRequest = PersonalFmControllerTestAccess::resetRequest(f.fm());
        QVERIFY(f.api.setAccount(0, {}));
        PersonalFmControllerTestAccess::complete(f.fm(), accountRequest, songs());
        QVERIFY(f.fm().current().isEmpty());
        QVERIFY(!f.fm().signedIn());
        QCOMPARE(PersonalFmControllerTestAccess::requestCount(f.fm()), 0);
    }
    void queuedNextAndDislikeUseTheSharedPlayer()
    {
        Fixture f;
        f.preview();
        f.fm().play();
        PlaybackServiceTestAccess::loaded(*f.playback);
        const auto oldId = f.playback->trackId();
        const auto before = PersonalFmControllerTestAccess::requestCount(f.fm());
        f.fm().favoriteFeedback(f.playback->currentTrack(), true);
        QCOMPARE(PersonalFmControllerTestAccess::requestCount(f.fm()), before + 1);
        auto unrelated = PlaybackTrack::fromDomain(songs().last().toObject());
        f.fm().favoriteFeedback(unrelated, true);
        QCOMPARE(PersonalFmControllerTestAccess::requestCount(f.fm()), before + 1);
        f.fm().dislike();
        QVERIFY(!f.playback->playing());
        QVERIFY(f.playback->trackId() != oldId);
        QCOMPARE(f.playback->queue()->rowCount(), 0);
        PlaybackServiceTestAccess::loaded(*f.playback);
        QCOMPARE(f.playback->queue()->rowCount(), 1);
        auto track = PlaybackTrack::fromDomain(songs().last().toObject());
        auto *manual = f.playback->queueCatalog()->modelForKey("default");
        const auto extra = PlaybackTrack::fromDomain(songs()[3].toObject());
        f.playback->appendTracks({track, extra}, true);
        QCOMPARE(f.playback->queue()->rowCount(), 1); // Manual insertion is not FM history.
        QCOMPARE(f.fm().nextTrack()->id, track.id);
        f.playback->next();
        QCOMPARE(f.playback->sourceKey(), QString("default"));
        QCOMPARE(f.playback->trackId(), track.id);
        QCOMPARE(manual->nextKeys().size(), 1);
        f.playback->next();
        QCOMPARE(f.playback->trackId(), extra.id);
        QVERIFY(manual->nextKeys().isEmpty());
        QVERIFY(!f.playback->queueCatalog()->modelForKey(PersonalFmController::queueKey()));
        const auto feedbackCount = PersonalFmControllerTestAccess::requestCount(f.fm());
        f.fm().favoriteFeedback(track, false);
        QCOMPARE(PersonalFmControllerTestAccess::requestCount(f.fm()), feedbackCount);
    }
    void transitionConsumesForeignNextOnlyOnAdoption()
    {
        Fixture f;
        f.preview();
        f.fm().play();
        PlaybackServiceTestAccess::loaded(*f.playback);
        const auto first = PlaybackTrack::fromDomain(songs()[3].toObject());
        const auto second = PlaybackTrack::fromDomain(songs()[4].toObject());
        f.playback->appendTracks({first, second}, true);
        auto *manual = f.playback->queueCatalog()->modelForKey("default");
        QCOMPARE(f.fm().nextTrack()->id, first.id);
        QCOMPARE(manual->nextKeys().size(), 2); // Preparation only reads.
        f.fm().adoptTransition(first);
        QCOMPARE(f.playback->sourceKey(), QString("default"));
        QCOMPARE(f.playback->queue()->at(f.playback->queue()->currentIndex())->id, first.id);
        QCOMPARE(manual->nextKeys(), QStringList{second.key()});
        QVERIFY(!f.playback->queueCatalog()->modelForKey(PersonalFmController::queueKey()));
    }
    void priorSourcePrioritySurvivesStorageAndOwnV2Upgrade()
    {
        Fixture f;
        auto *catalog = f.playback->queueCatalog();
        const auto first = PlaybackTrack::fromDomain(songs()[0].toObject());
        const auto second = PlaybackTrack::fromDomain(songs()[1].toObject());
        catalog->modelForKey("default")->append({second}, true);
        catalog->activate("older", "Older", {first}, 0);
        catalog->activate("newer", "Newer", {second}, 0);
        catalog->activate("older", "Older", {first}, 0);
        catalog->activate(PersonalFmController::queueKey(), "FM", {}, -1);
        catalog->modelForKey("older")->restoreNext({first.key()});
        catalog->modelForKey("newer")->restoreNext({second.key()});
        QCOMPARE(catalog->priorityKey(PersonalFmController::queueKey()), QString("older"));
        const auto states = catalog->snapshot();
        const auto taken = catalog->takePriority(PersonalFmController::queueKey(), first.id);
        QCOMPARE(taken.first, QString("newer"));
        QCOMPARE(taken.second, 0);
        QVERIFY(catalog->modelForKey("older")->nextKeys().isEmpty());
        QVERIFY(!catalog->modelForKey("default")->nextKeys().isEmpty());
        QVERIFY(catalog->modelForKey("newer")->nextKeys().isEmpty());
        const auto path = f.dir.filePath("queue-priority.sqlite");
        {
            PlaybackRepository repository(path);
            QSignalSpy loaded(&repository, &PlaybackRepository::loaded),
                failed(&repository, &PlaybackRepository::failed);
            repository.save(9, states, PersonalFmController::queueKey(), "older");
            repository.load(9, 101);
            QTRY_COMPARE(loaded.count(), 1);
            QCOMPARE(failed.count(), 0);
            QCOMPARE(loaded.first()[4].toString(), QString("older"));
            PlaybackQueueCatalog restored(nullptr);
            restored.restore(loaded.first()[2].value<PlaybackQueueStates>(),
                             loaded.first()[3].toString(), loaded.first()[4].toString());
            QCOMPARE(restored.priorityKey(PersonalFmController::queueKey()), QString("older"));
            QSignalSpy closed(&repository, &PlaybackRepository::closed);
            repository.shutdown();
            QTRY_COMPARE(closed.count(), 1);
        }
        // Reproduce this project's pre-FM v2 schema, preserving the real queue
        // rows. Reopening must add only its new account-owned preference.
        const auto connection = "fm-migration-" + QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("ALTER TABLE playback_accounts DROP COLUMN last_local_queue"));
            QVERIFY(query.exec("ALTER TABLE playback_queues DROP COLUMN last_used"));
            QVERIFY(query.exec("PRAGMA user_version=2"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        {
            PlaybackRepository repository(path);
            QSignalSpy loaded(&repository, &PlaybackRepository::loaded),
                failed(&repository, &PlaybackRepository::failed);
            repository.load(9, 102);
            QTRY_COMPARE(loaded.count(), 1);
            QCOMPARE(failed.count(), 0);
            QCOMPARE(loaded.first()[2].value<PlaybackQueueStates>().size(), states.size());
            QCOMPARE(loaded.first()[3].toString(), PersonalFmController::queueKey());
            QCOMPARE(loaded.first()[4].toString(), QString("default"));
            QSignalSpy closed(&repository, &PlaybackRepository::closed);
            repository.shutdown();
            QTRY_COMPARE(closed.count(), 1);
        }
    }
    void nativePage()
    {
        const bool live = qEnvironmentVariableIsSet("ECHO_LIVE_PERSONAL_FM");
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        prefs.setValue("playback/transitionMode", "none");
        DeviceIdentity device = DeviceIdentity::temporary();
        QJsonObject identity;
        if (live) {
            const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                    std::istreambuf_iterator<char>());
            identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
            device = {identity["mid"].toString(),
                      identity["dfid"].toString(),
                      identity["guid"].toString(),
                      identity["mac"].toString(),
                      identity["device_name"].toString(),
                      "-",
                      "-"};
        }
        ApiClient api(device);
        NetworkPreferences network(prefs, api);
        QVERIFY(network.transportAvailable());
        const auto previous = QNetworkProxy::applicationProxy();
        const auto restore = qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previous); });
        network.installApplicationTransport();
        if (live)
            QVERIFY(api.setAccount(quint64(identity["userid"].toInteger()),
                                   identity["token"].toString(), identity["t1"].toString()));
        else {
            QVERIFY(api.setAccount(900001, "offline-domain-fixture"));
            QVERIFY(api.setNetworkTransport(1));
        }
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        ThemeStore theme(prefs);
        ShellSettings settings(prefs);
        RouteController router;
        DiscoveryService service(api, playback, account);
        DiscoveryRouteCoordinator routes(service, router, live);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &messages) {
            for (const auto &message : messages)
                warnings.append(message.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import EchoMusic.Shell
            import EchoMusic.Theme
            Window {
                id: host
                required property RouteController router
                required property ShellSettings settings
                width:1150; height:800; visible:true
                Rectangle { anchors.fill:parent; color:Theme.main }
                Item {
                    width:parent.width/host.settings.uiScale; height:parent.height/host.settings.uiScale
                    scale:host.settings.uiScale; transformOrigin:Item.TopLeft
                    RouteHost { anchors.fill:parent; router:host.router; settings:host.settings }
                }
            })",
                          QUrl("qrc:/personal-fm-production-route-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->resize(1150, 620);
        QVERIFY(router.navigate("personal-fm"));
        auto *fm = playback.personalFm();
        if (!live) {
            fm->setPageActive(true, true);
            PersonalFmControllerTestAccess::complete(
                *fm, PersonalFmControllerTestAccess::resetRequest(*fm), songs());
        }
        QTRY_VERIFY_WITH_TIMEOUT(!fm->previewLoading(), 15000);
        QVERIFY2(!fm->current().isEmpty(), qPrintable(fm->errorMessage()));
        QTRY_VERIFY(visual(window->contentItem(), "personalFmPage"));
        QTRY_VERIFY(visual(window->contentItem(), "personalFmModeSwitch"));
        auto *scroll = visual(window->contentItem(), "personalFmScrollView");
        QVERIFY(scroll);
        QSignalSpy initialFrames(window, &QQuickWindow::frameSwapped);
        window->update();
        QTRY_VERIFY(!initialFrames.isEmpty());
        const auto firstY = [&] {
            return scroll->property("originY").toDouble() -
                   scroll->property("topMargin").toDouble();
        };
        qInfo() << "FM initial scroll bounds" << scroll->property("contentY") << firstY()
                << scroll->property("contentHeight") << scroll->height();
        QVERIFY(qAbs(scroll->property("contentY").toDouble() - firstY()) < 0.5);
        const auto artifacts = qEnvironmentVariable("ECHO_FM_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(window->grabWindow().save(artifacts + "/fm-initial-short.png"));
        }
        window->resize(1150, 480);
        for (int i = 0; i < 5; ++i)
            settings.zoomIn();
        initialFrames.clear();
        window->update();
        QTRY_VERIFY(!initialFrames.isEmpty());
        QTRY_VERIFY(qAbs(scroll->property("contentY").toDouble() - firstY()) < 0.5);
        // The initial value must not pin the view or reset a cached route.
        const auto wheelPoint =
            scroll->mapToScene(QPointF(scroll->width() / 2, scroll->height() * .8));
        QWheelEvent wheel(wheelPoint, window->mapToGlobal(wheelPoint.toPoint()), {}, {0, -120},
                          Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QCoreApplication::sendEvent(window, &wheel);
        QTRY_VERIFY(scroll->property("contentY").toDouble() > firstY() + 1);
        QTRY_VERIFY(!scroll->property("moving").toBool());
        const auto savedY = scroll->property("contentY").toDouble();
        auto *fmRoute = router.current();
        QVERIFY(router.navigate("ranking"));
        router.back();
        QCOMPARE(router.current(), fmRoute);
        initialFrames.clear();
        window->update();
        QTRY_VERIFY(!initialFrames.isEmpty());
        QTRY_VERIFY(qAbs(scroll->property("contentY").toDouble() - savedY) < 0.5);
        scroll->setProperty("contentY", firstY()); // Test setup for the remaining visual matrix.
        settings.resetZoom();
        window->resize(1150, 800);
        auto *play = visual(window->contentItem(), "personalFmPlay");
        QVERIFY(play);
        QVERIFY(playback.sourceKey() !=
                PersonalFmController::queueKey()); // Entering only previews.
        QVERIFY(!playback.hasTrack());
        const auto imagesSettled = [&] {
            QList<QQuickItem *> pending{window->contentItem()};
            while (!pending.isEmpty()) {
                auto *item = pending.takeLast();
                if ((item->objectName() == "coverImage" ||
                     item->objectName() == "personalFmVinylTexture") &&
                    item->property("status").toInt() == 2)
                    return false;
                pending.append(item->childItems());
            }
            return true;
        };
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (int zoom : {0, 5}) {
                settings.resetZoom();
                for (int i = 0; i < zoom; ++i)
                    settings.zoomIn();
                QTRY_VERIFY_WITH_TIMEOUT(imagesSettled(), 15000);
                QSignalSpy frames(window, &QQuickWindow::frameSwapped);
                window->update();
                QTRY_VERIFY(!frames.isEmpty());
                auto *poster = visual(window->contentItem(), "personalFmPoster");
                QVERIFY(poster);
                QCOMPARE(poster->width(), 248.);
                if (!artifacts.isEmpty())
                    QVERIFY(window->grabWindow().save(
                        artifacts + QString("/fm-%1-%2.png")
                                        .arg(mode == ThemeStore::Dark ? "dark" : "light",
                                             zoom ? "125" : "100")));
            }
        }
        QTRY_VERIFY_WITH_TIMEOUT(imagesSettled(), 15000);
        QSignalSpy settledFrame(window, &QQuickWindow::frameSwapped);
        window->update();
        QTRY_VERIFY(!settledFrame.isEmpty());
        const auto activeAnimations = [&] {
            QStringList names;
            QSet<QObject *> objects;
            QList<QQuickItem *> items{window->contentItem()};
            // Route ownership belongs to the coordinator, while its visual
            // parent is the window. Inspect both trees, not only root children.
            while (!items.isEmpty()) {
                auto *item = items.takeLast();
                items.append(item->childItems());
                objects.insert(item);
                for (auto *object : item->findChildren<QObject *>())
                    objects.insert(object);
            }
            for (auto *object : objects) {
                const auto type = QString::fromLatin1(object->metaObject()->className());
                if ((type.contains("Animation") || type.contains("Animator")) &&
                    object->property("running").toBool())
                    names.append(type);
            }
            return names;
        };
        qInfo() << "FM active animations before idle measurement:" << activeAnimations();
        QTRY_VERIFY_WITH_TIMEOUT(activeAnimations().isEmpty(), 4000);
        QSignalSpy idleFrames(window, &QQuickWindow::frameSwapped);
        QTest::qWait(350); // Measure a static, fully loaded page, not a layout correction.
        qInfo() << "FM preview frame submissions during 350ms idle:" << idleFrames.size();
        if (QGuiApplication::platformName() == "cocoa")
            QVERIFY(idleFrames.size() <= 1);
        if (live) {
            settings.resetZoom();
            QVERIFY(QTest::qWaitForWindowExposed(window));
            auto *modes = visual(window->contentItem(), "personalFmModeSwitch");
            auto *pools = visual(window->contentItem(), "personalFmPoolSwitch");
            QVERIFY(modes && pools);
            const auto choose = [&](QQuickItem *group, int index) {
                auto *button = visual(group, QString("tab-%1").arg(index));
                QVERIFY(button);
                QSignalSpy clicked(button, SIGNAL(clicked()));
                QTest::mouseClick(
                    window, Qt::LeftButton, Qt::NoModifier,
                    button->mapToScene({button->width() / 2, button->height() / 2}).toPoint());
                QCOMPARE(clicked.count(), 1);
            };
            for (int index : {1, 2, 3, 0}) {
                choose(modes, index);
                QTRY_VERIFY_WITH_TIMEOUT(!fm->previewLoading(), 15000);
                QVERIFY2(fm->errorMessage().isEmpty(), qPrintable(fm->errorMessage()));
                QCOMPARE(fm->mode(), index);
                QVERIFY(!playback.hasTrack());
            }
            for (int index : {1, 2, 0}) {
                choose(pools, index);
                QTRY_VERIFY_WITH_TIMEOUT(!fm->previewLoading(), 15000);
                QVERIFY2(fm->errorMessage().isEmpty(), qPrintable(fm->errorMessage()));
                QCOMPARE(fm->songPool(), index);
                QVERIFY(!playback.hasTrack());
            }
            QTRY_VERIFY_WITH_TIMEOUT(playback.engineReady(), 15000);
            QSignalSpy clicked(play, SIGNAL(clicked()));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              play->mapToScene({26, 26}).toPoint());
            QTRY_COMPARE(clicked.count(), 1);
            QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && !playback.loading(), 30000);
            QTRY_VERIFY_WITH_TIMEOUT(playback.positionMs() > 500, 5000);
            QCOMPARE(playback.sourceKey(), PersonalFmController::queueKey());
            QCOMPARE(playback.queue()->rowCount(), 1);
            if (!artifacts.isEmpty()) {
                QTRY_VERIFY_WITH_TIMEOUT(imagesSettled(), 15000);
                QVERIFY(window->grabWindow().save(artifacts + "/fm-playing.png"));
            }
            const auto first = playback.trackId();
            QSignalSpy nativeEvents(PlaybackServiceTestAccess::engine(playback),
                                    &AudioEngine::eventReceived);
            playback.transition()->setMode("gapless");
            QVERIFY(playback.durationMs() > 15000);
            playback.seek(playback.durationMs() - 15000);
            QTRY_VERIFY_WITH_TIMEOUT(
                TrackTransitionControllerTestAccess::prepared(*playback.transition()), 15000);
            QCOMPARE(playback.queue()->rowCount(),
                     1); // Native preparation still has no history row.
            const auto preparedSequence =
                TrackTransitionControllerTestAccess::sequence(*playback.transition());
            qInfo() << "FM prepared sequence:" << preparedSequence;
            QSignalSpy transitions(playback.transition(), &TrackTransitionController::notice);
            // The decode worker can already have queued the prepared deck at
            // EOF ahead of the audible clock. Wait for that real boundary;
            // manual skip may legitimately use the normal-load fallback.
            QTRY_VERIFY_WITH_TIMEOUT(
                playback.playing() && !playback.loading() && playback.trackId() != first, 30000);
            QCOMPARE(playback.queue()->rowCount(), 2);
            const auto adoptedPreparedSource = [&] {
                for (const auto &row : nativeEvents) {
                    const auto event = row.first().toJsonObject();
                    if (event.value("event") == "file-loaded" &&
                        quint64(event.value("seq").toInteger()) == preparedSequence)
                        return true;
                }
                return false;
            };
            for (const auto &row : nativeEvents) {
                const auto event = row.first().toJsonObject();
                const auto kind = event.value("event").toString();
                if (kind.startsWith("next-source-") || kind == "file-loaded")
                    qInfo() << "FM native boundary:" << kind
                            << "prepared=" << event.value("prepared").toBool()
                            << "committed=" << event.value("committed").toBool()
                            << "sequence=" << event.value("seq").toInteger()
                            << "message=" << event.value("message").toString() << "mode="
                            << event.value("transition").toObject().value("mode").toString();
            }
            QTRY_VERIFY_WITH_TIMEOUT(adoptedPreparedSource(), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(transitions.count(), 1, 5000);
            QVERIFY(transitions.first().first().toString().contains(QStringLiteral("无缝")));
            playback.transition()->setMode("none");
            const auto second = playback.trackId();
            playback.setPlayMode(PlaybackService::SingleLoop);
            QVERIFY(playback.durationMs() > 2000);
            playback.seek(playback.durationMs() - 1200);
            QTRY_VERIFY_WITH_TIMEOUT(
                playback.playing() && !playback.loading() && playback.trackId() != second, 30000);
            QCOMPARE(playback.queue()->rowCount(), 3);
            const auto third = playback.trackId();
            auto *dislike = visual(window->contentItem(), "personalFmDislike");
            QVERIFY(dislike && dislike->isEnabled());
            const auto prior = PersonalFmControllerTestAccess::requests(*fm);
            QSignalSpy feedback(&api, &ApiClient::succeeded);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              dislike->mapToScene({17, 17}).toPoint());
            QList<quint64> reports;
            for (auto id : PersonalFmControllerTestAccess::requests(*fm))
                if (!prior.contains(id))
                    reports.append(id);
            QVERIFY(!reports.isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(
                playback.playing() && !playback.loading() && playback.trackId() != third, 30000);
            QCOMPARE(playback.queue()->rowCount(), 3);
            const auto reportsFinished = [&] {
                for (const auto id : reports) {
                    bool found = false;
                    for (const auto &row : feedback)
                        found |= row[0].toULongLong() == id;
                    if (!found)
                        return false;
                }
                return true;
            };
            QTRY_VERIFY_WITH_TIMEOUT(reportsFinished(), 15000);
            playback.stop();
        }
        auto *disc = visual(window->contentItem(), "personalFmCurrentDisc");
        QVERIFY(disc);
        auto *coverImage = visual(disc, "coverImage");
        QVERIFY(coverImage);
        QVERIFY(router.navigate("home"));
        QTRY_VERIFY(coverImage->property("source").toUrl().isEmpty());
        auto *turn = disc->findChild<QObject *>("personalFmDiscTurn");
        QVERIFY(turn);
        QVERIFY(!turn->property("running").toBool());
        QSignalSpy retiredFrames(window, &QQuickWindow::frameSwapped);
        window->update();
        QTRY_VERIFY(!retiredFrames.isEmpty());
        window->hide();
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        // Visual bindings borrow RouteContext; retire them before its owner.
        root.reset();
        router.shutdown();
        service.shutdown();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
        api.shutdown();
    }
};
QTEST_MAIN(PersonalFmTest)
#include "PersonalFmTest.moc"
