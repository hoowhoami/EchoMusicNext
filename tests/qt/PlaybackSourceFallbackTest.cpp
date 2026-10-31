#include "PlaybackSourceRequest.h"
#include "PlaybackSourcePlan.h"
#include "PlaybackService.h"
#include "TrackTransitionController.h"
#include <QTcpSocket>
#include "NetworkPreferences.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QScopeGuard>
#include <QNetworkProxy>
#include <QGuiApplication>
#include <iostream>
#include <iterator>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class ApiClientTestAccess
{
public:
    static quint64 pending(ApiClient &a, const QString &op)
    {
        for (const auto &p : a.m_pending)
            if (p.request.value("operation").toString() == op)
                return p.logicalId;
        return 0;
    }
    static QJsonObject query(ApiClient &a, quint64 id)
    {
        for (const auto &p : a.m_pending)
            if (p.logicalId == id)
                return p.request.value("query").toObject();
        return {};
    }
    static void finish(ApiClient &a, quint64 id, const QJsonValue &v)
    {
        a.cancel(id);
        const auto b = v.isArray() ? QJsonDocument(v.toArray()).toJson()
                                   : QJsonDocument(v.toObject()).toJson();
        emit a.succeeded(id, b);
    }
    static void fail(ApiClient &a, quint64 id)
    {
        a.cancel(id);
        emit a.failed(id, 1, R"({"message":"source unavailable"})");
    }
};
class MembershipServiceTestAccess
{
public:
    static void ready(MembershipService &m)
    {
        m.m_facts = {true, true, true};
        m.m_age.start();
    }
};
class PlaybackSourceRequestTestAccess
{
public:
    static QString catalogHash(const PlaybackSourceRequest &s)
    {
        return s.m_catalogHash;
    }
    static void exhausted(PlaybackSourceRequest &s, ApiClient &api, const PlaybackTrack &track)
    {
        s.cancel();
        s.m_track = track;
        s.m_context = true;
        s.m_revision = api.sessionRevision();
        s.m_exhausted = true;
        s.m_cloudTried = true;
        s.m_catalogStarted = true;
        s.m_kind = "catalog";
    }
};
class PlaybackServiceTestAccess
{
public:
    static void seedCatalogSelection(PlaybackService &p, const PlaybackTrack &track,
                                     const QJsonObject &rights)
    {
        p.m_track = track;
        p.m_privileges = rights;
        MembershipServiceTestAccess::ready(p.m_membership);
    }
    static void downshiftedSelection(PlaybackService &p)
    {
        // Domain presentation state of an HQ preference currently playing SD.
        p.m_quality = "higher";
        p.m_resolvedQuality = "standard";
        p.m_sourceKind = p.m_sourcePreference = "catalog";
    }
    static void runningCatalog(PlaybackService &p)
    {
        p.m_engine.blockSignals(true); // Isolate the controlled native acknowledgements.
        p.m_loaded = p.m_playing = p.m_ready = true;
        p.m_audioSequence = p.m_audioSerial = 17;
        p.m_sourceRevision = 1;
        p.m_resolvedQuality = "standard";
        p.m_sourceKind = p.m_sourcePreference = "catalog";
    }
    static void nativeNotifications(PlaybackService &p)
    {
        p.m_engine.blockSignals(false);
    }
    static quint64 sourceRevision(PlaybackService &p)
    {
        return p.m_sourceRevision;
    }
    static bool sourcePrepared(const PlaybackService &p)
    {
        return p.m_pendingSourceIdentity.has_value();
    }
    static bool engineReady(const PlaybackService &p)
    {
        return p.m_ready;
    }
    static QString catalogResource(const PlaybackService &p)
    {
        return PlaybackSourceRequestTestAccess::catalogHash(p.m_source);
    }
    static void event(PlaybackService &p, const QJsonObject &event)
    {
        p.consumeOutputEvent(event);
    }
    static void observeEngine(PlaybackService &p, QList<QJsonObject> *acceptedSources = nullptr)
    {
        QObject::connect(
            &p.m_engine, &AudioEngine::eventReceived, &p,
            [&p, acceptedSources](const QJsonObject &e) {
                const auto kind = e.value("event").toString();
                if (acceptedSources &&
                    e.value("track_seq").toInteger() == qint64(p.m_audioSequence) &&
                    (kind == "file-loaded" ||
                     (kind == "source-switched" &&
                      e.value("source_revision").toInteger() == qint64(p.m_sourceRevision))))
                    acceptedSources->append(e);
                if (kind == "file-loaded" || kind == "error" || kind == "source-switched")
                    qInfo() << "Source probe engine" << kind << "seq" << e.value("seq").toInteger()
                            << "track" << e.value("track_seq").toInteger() << "revision"
                            << e.value("source_revision").toInteger() << "expected"
                            << p.m_audioSequence << p.m_sourceRevision << "source" << p.sourceKind()
                            << "loading" << p.loading() << "error" << p.errorMessage();
            });
    }
    static void failAudio(PlaybackService &p)
    {
        p.consumeOutputEvent({{"event", "error"},
                              {"error_code", "decode"},
                              {"message", "invalid data"},
                              {"track_seq", qint64(p.m_audioSequence)}});
    }
    static void loadRejectedNormalMedia(PlaybackService &p, const QString &url)
    {
        PlaybackSourceRequestTestAccess::exhausted(p.m_source, p.m_api, p.m_track);
        p.m_transition->clear();
        p.m_engine.command(AudioEngine::Stop);
        p.m_urls = {url};
        p.m_urlIndex = 0;
        p.m_loading = true;
        p.m_loaded = false;
        p.m_seamless = false;
        p.m_sourceKind = "catalog";
        p.m_sourceFailure.clear();
        p.m_mvFailure.clear();
        p.m_audioTrack = 0;
        p.m_resumePosition = -1;
        p.loadNextUrl();
    }
};
} // namespace EchoMusic
namespace
{
const QString hashA(32, 'a'), hashB(32, 'b'), hashC(32, 'c');
QJsonObject file(quint64 song, const QString &hash, quint64 id, const QString &standard = {})
{
    return {{"song", QJsonObject{{"id", QString::number(song)}, {"hash", hash}}},
            {"file_id", qint64(id)},
            {"audio_id", qint64(song + 1000)},
            {"hash_std", standard},
            {"source_file_name", "Test.flac"}};
}
QJsonObject page(const QJsonArray &items, int total)
{
    return {{"page", QJsonObject{{"items", items}, {"total", total}}}};
}
QJsonObject source(const QString &url)
{
    return {{"urls", QJsonArray{url}}, {"backup_urls", QJsonArray{}}};
}
struct Fixture
{
    QTcpServer sink;
    ApiClient api{DeviceIdentity::temporary()};
    MembershipService membership{api};
    CloudAudioIndex cloud{api};
    PlaybackSourceRequest request{api, membership, nullptr, &cloud};
    Fixture()
    {
        sink.listen(QHostAddress::LocalHost);
        api.setNetworkTransport(sink.serverPort());
        api.setAccount(900001, "isolated-token");
        MembershipServiceTestAccess::ready(membership);
    }
    ~Fixture()
    {
        request.cancel();
        api.shutdown();
    }
};
} // namespace
class PlaybackSourceFallbackTest : public QObject
{
    Q_OBJECT
private slots:
    void replacedCatalogResource_data()
    {
        QTest::addColumn<bool>("known");
        QTest::addColumn<bool>("preloading");
        QTest::addColumn<bool>("compatibility");
        QTest::newRow("current-known-compatible") << true << false << true;
        QTest::newRow("current-fetched-strict") << false << false << false;
        QTest::newRow("preloaded-known-compatible") << true << true << true;
        QTest::newRow("preloaded-fetched-strict") << false << true << false;
    }
    void replacedCatalogResource()
    {
        QFETCH(bool, known);
        QFETCH(bool, preloading);
        QFETCH(bool, compatibility);
        Fixture f;
        PlaybackSourceRequest request(f.api, f.membership);
        QSignalSpy resolved(&request, &PlaybackSourceRequest::resolved);
        QSignalSpy failed(&request, &PlaybackSourceRequest::failed);
        PlaybackTrack track;
        track.id = "795678654";
        track.songId = 795678654;
        track.albumId = 162333400;
        track.hash = hashA;
        const QJsonObject missing{
            {"hash", hashA}, {"resource_error", 6}, {"variants", QJsonArray{}}};
        request.resolve(track, ApiClient::PlaybackQuality::HiRes, ApiClient::SoundEffect::None,
                        known ? missing : QJsonObject{}, compatibility, true, {}, {}, false, false,
                        preloading);
        if (!known) {
            const auto id = ApiClientTestAccess::pending(f.api, "privilege_lite");
            QVERIFY(id);
            ApiClientTestAccess::finish(f.api, id, QJsonArray{missing});
        }
        const auto locator = ApiClientTestAccess::pending(f.api, "song_url_auth_merge");
        QVERIFY(locator);
        // Fresh membership can settle synchronously inside startCatalog().
        // Its reentrant continuation must not issue another old-hash query.
        QVERIFY(!ApiClientTestAccess::pending(f.api, "privilege_lite"));
        const auto query = ApiClientTestAccess::query(f.api, locator);
        QCOMPARE(query.value("hash").toString(), hashA);
        QCOMPARE(query.value("quality").toString(), QString("automatic"));
        QCOMPARE(query.value("song_id").toInteger(), qint64(track.songId));
        QCOMPARE(query.value("album_id").toInteger(), qint64(track.albumId));
        auto locatorSource = source("https://example.invalid/locator-only");
        locatorSource["hash"] = hashB;
        ApiClientTestAccess::finish(f.api, locator, locatorSource);
        QVERIFY(resolved.isEmpty()); // The locator's SD URL must never be adopted.
        const auto rights = ApiClientTestAccess::pending(f.api, "privilege_lite");
        QVERIFY(rights);
        QCOMPARE(ApiClientTestAccess::query(f.api, rights)
                     .value("resources")
                     .toArray()
                     .first()
                     .toObject()
                     .value("hash")
                     .toString(),
                 hashB);
        const QJsonObject current{
            {"hash", hashB},
            {"resource_error", 0},
            {"variants", QJsonArray{QJsonObject{{"quality", "128"}, {"hash", hashB}},
                                    QJsonObject{{"quality", "high"}, {"hash", hashC}}}}};
        ApiClientTestAccess::finish(f.api, rights, QJsonArray{current});
        const auto actual = ApiClientTestAccess::pending(f.api, "song_url_auth_merge");
        QVERIFY(actual);
        QCOMPARE(ApiClientTestAccess::query(f.api, actual).value("hash").toString(), hashC);
        QCOMPARE(ApiClientTestAccess::query(f.api, actual).value("quality").toString(),
                 QString("hi_res"));
        auto audio = source("https://example.invalid/actual-hires");
        audio["hash"] = hashC;
        ApiClientTestAccess::finish(f.api, actual, audio);
        QCOMPARE(resolved.size(), 1);
        QVERIFY(failed.isEmpty());
        QCOMPARE(qvariant_cast<PlaybackTrack>(resolved[0][0]).stableKey(), track.stableKey());
        QCOMPARE(resolved[0][1].toJsonObject().value("hash").toString(), hashB);
        QCOMPARE(resolved[0][2].toJsonObject().value("urls").toArray().first().toString(),
                 QString("https://example.invalid/actual-hires"));
        QCOMPARE(resolved[0][4].toString(), QString("hi_res"));
    }
    void catalogRefreshIsBounded_data()
    {
        QTest::addColumn<QString>("failure");
        for (const auto *name :
             {"request-failed", "same-hash", "invalid-hash", "no-url", "replacement-also-missing"})
            QTest::newRow(name) << QString(name);
    }
    void catalogRefreshIsBounded()
    {
        QFETCH(QString, failure);
        Fixture f;
        PlaybackSourceRequest request(f.api, f.membership);
        QSignalSpy resolved(&request, &PlaybackSourceRequest::resolved);
        QSignalSpy failed(&request, &PlaybackSourceRequest::failed);
        PlaybackTrack track;
        track.id = "old-song";
        track.songId = 123;
        track.albumId = 456;
        track.hash = hashA;
        request.resolve(track, ApiClient::PlaybackQuality::HiRes, ApiClient::SoundEffect::None,
                        {{"hash", hashA}, {"resource_error", 6}, {"variants", QJsonArray{}}});
        const auto id = ApiClientTestAccess::pending(f.api, "song_url_auth_merge");
        QVERIFY(id);
        if (failure == "request-failed") {
            ApiClientTestAccess::fail(f.api, id);
        } else {
            auto candidate = source("https://example.invalid/locator");
            candidate["hash"] = failure == "same-hash"      ? hashA
                                : failure == "invalid-hash" ? QString("not-a-hash")
                                                            : hashB;
            if (failure == "no-url")
                candidate["urls"] = QJsonArray{};
            ApiClientTestAccess::finish(f.api, id, candidate);
            if (failure == "replacement-also-missing") {
                const auto rights = ApiClientTestAccess::pending(f.api, "privilege_lite");
                QVERIFY(rights);
                ApiClientTestAccess::finish(f.api, rights,
                                            QJsonArray{QJsonObject{{"hash", hashB},
                                                                   {"resource_error", 6},
                                                                   {"variants", QJsonArray{}}}});
            }
        }
        QCOMPARE(failed.size(), 1);
        QVERIFY(resolved.isEmpty());
        QVERIFY(!ApiClientTestAccess::pending(f.api, "song_url_auth_merge"));
        QVERIFY2(!ApiClientTestAccess::pending(f.api, "privilege_lite"),
                 qPrintable(
                     QJsonDocument(ApiClientTestAccess::query(f.api, ApiClientTestAccess::pending(
                                                                         f.api, "privilege_lite")))
                         .toJson()));
    }
    void catalogRefreshCancellation_data()
    {
        QTest::addColumn<bool>("rightsStage");
        QTest::addColumn<bool>("accountChange");
        QTest::newRow("cancel-locator") << false << false;
        QTest::newRow("cancel-rights") << true << false;
        QTest::newRow("account-locator") << false << true;
        QTest::newRow("account-rights") << true << true;
    }
    void catalogRefreshCancellation()
    {
        QFETCH(bool, rightsStage);
        QFETCH(bool, accountChange);
        Fixture f;
        PlaybackSourceRequest request(f.api, f.membership);
        QSignalSpy resolved(&request, &PlaybackSourceRequest::resolved);
        QSignalSpy privileges(&request, &PlaybackSourceRequest::privilegesReady);
        QSignalSpy failed(&request, &PlaybackSourceRequest::failed);
        PlaybackTrack track;
        track.id = "cancelled-song";
        track.songId = 123;
        track.albumId = 456;
        track.hash = hashA;
        request.resolve(track, ApiClient::PlaybackQuality::HiRes, ApiClient::SoundEffect::None,
                        {{"hash", hashA}, {"resource_error", 6}, {"variants", QJsonArray{}}});
        auto id = ApiClientTestAccess::pending(f.api, "song_url_auth_merge");
        QVERIFY(id);
        auto candidate = source("https://example.invalid/late");
        candidate["hash"] = hashB;
        if (rightsStage) {
            ApiClientTestAccess::finish(f.api, id, candidate);
            id = ApiClientTestAccess::pending(f.api, "privilege_lite");
            QVERIFY(id);
        }
        if (accountChange)
            QVERIFY(f.api.setAccount(900002, "other-isolated-token"));
        else
            request.cancel();
        ApiClientTestAccess::finish(
            f.api, id,
            rightsStage
                ? QJsonValue(QJsonArray{QJsonObject{
                      {"hash", hashB},
                      {"variants", QJsonArray{QJsonObject{{"quality", "high"}, {"hash", hashC}}}}}})
                : QJsonValue(candidate));
        QVERIFY(resolved.isEmpty());
        QVERIFY(privileges.isEmpty());
        QVERIFY(failed.isEmpty());
        QVERIFY(!ApiClientTestAccess::pending(f.api, "song_url_auth_merge"));
        QVERIFY(!ApiClientTestAccess::pending(f.api, "privilege_lite"));
    }
    void otherRightsFailuresDoNotRefresh_data()
    {
        QTest::addColumn<int>("resourceError");
        QTest::newRow("ordinary-unsupported") << 0;
        QTest::newRow("other-rejection") << 2;
    }
    void otherRightsFailuresDoNotRefresh()
    {
        QFETCH(int, resourceError);
        Fixture f;
        PlaybackSourceRequest request(f.api, f.membership);
        QSignalSpy failed(&request, &PlaybackSourceRequest::failed);
        PlaybackTrack track;
        track.id = "unavailable-song";
        track.songId = 123;
        track.albumId = 456;
        track.hash = hashA;
        request.resolve(
            track, ApiClient::PlaybackQuality::HiRes, ApiClient::SoundEffect::None,
            {{"hash", hashA}, {"resource_error", resourceError}, {"variants", QJsonArray{}}});
        QCOMPARE(failed.size(), 1);
        QVERIFY(!ApiClientTestAccess::pending(f.api, "song_url_auth_merge"));
    }
    void qualityIdentityWaitsForNativeAdoption_data()
    {
        QTest::addColumn<bool>("accepted");
        QTest::newRow("adopt") << true;
        QTest::newRow("retain-old-on-rejection") << false;
    }
    void qualityIdentityWaitsForNativeAdoption()
    {
        QFETCH(bool, accepted);
        Fixture f;
        QTemporaryDir dir;
        SettingsStore settings(dir.filePath("settings.ini"));
        PlaybackService playback(f.api, settings, nullptr, dir.filePath("playback.sqlite"));
        QTRY_VERIFY(PlaybackServiceTestAccess::engineReady(playback));
        PlaybackTrack track;
        track.id = "source-adoption";
        track.hash = hashA;
        const QJsonObject rights{
            {"variants", QJsonArray{QJsonObject{{"quality", "128"}, {"hash", hashA}},
                                    QJsonObject{{"quality", "320"}, {"hash", hashB}}}}};
        PlaybackServiceTestAccess::seedCatalogSelection(playback, track, rights);
        PlaybackServiceTestAccess::runningCatalog(playback);
        const auto restore =
            qScopeGuard([&] { PlaybackServiceTestAccess::nativeNotifications(playback); });
        QSignalSpy failure(&playback, &PlaybackService::sourceOperationFailed);
        playback.setQuality("higher");
        quint64 request = 0;
        QTRY_VERIFY((request = ApiClientTestAccess::pending(f.api, "song_url_auth_merge")));
        auto candidate = source("https://example.invalid/prepared-hq");
        candidate["hash"] = hashB;
        ApiClientTestAccess::finish(f.api, request, candidate);
        QTRY_VERIFY(PlaybackServiceTestAccess::sourcePrepared(playback));
        QCOMPARE(playback.quality(), QString("higher"));
        QCOMPARE(playback.resolvedQuality(), QString("standard"));
        QCOMPARE(playback.effectiveQuality(), QString("standard"));
        const auto revision = PlaybackServiceTestAccess::sourceRevision(playback);
        PlaybackServiceTestAccess::event(playback, {{"event", "source-switched"},
                                                    {"track_seq", 17},
                                                    {"source_revision", qint64(revision - 1)},
                                                    {"time", 1.},
                                                    {"duration", 60.}});
        QCOMPARE(playback.effectiveQuality(), QString("standard"));
        if (accepted) {
            PlaybackServiceTestAccess::event(playback, {{"event", "source-switched"},
                                                        {"track_seq", 17},
                                                        {"source_revision", qint64(revision)},
                                                        {"time", 1.},
                                                        {"duration", 60.}});
            QCOMPARE(playback.resolvedQuality(), QString("higher"));
            QVERIFY(failure.isEmpty());
        } else {
            PlaybackServiceTestAccess::event(
                playback,
                {{"event", "error"},
                 {"track_seq", 17},
                 {"source_revision", qint64(revision)},
                 {"message",
                  "source switch cancelled: insufficient prepared audio; old source kept"}});
            QCOMPARE(failure.size(), 1);
            QCOMPARE(playback.resolvedQuality(), QString("standard"));
            QCOMPARE(playback.effectiveQuality(), QString("standard"));
            QVERIFY(playback.playing());
        }
    }
    void selectingQualityDifferentFromActualSourceDoesNotShortCircuit()
    {
        Fixture f;
        QTemporaryDir directory;
        SettingsStore settings(directory.filePath("settings.ini"));
        PlaybackService playback(f.api, settings);
        PlaybackTrack track;
        track.id = "downshifted-selection";
        track.hash = hashA;
        const QJsonObject rights{
            {"variants", QJsonArray{QJsonObject{{"quality", "128"}, {"hash", hashA}},
                                    QJsonObject{{"quality", "320"}, {"hash", hashB}}}}};
        PlaybackServiceTestAccess::seedCatalogSelection(playback, track, rights);
        PlaybackServiceTestAccess::downshiftedSelection(playback);
        QCOMPARE(playback.effectiveQuality(), QString("standard"));
        playback.setQuality("higher");
        quint64 id = 0;
        QTRY_VERIFY((id = ApiClientTestAccess::pending(f.api, "song_url_auth_merge")));
        QCOMPARE(ApiClientTestAccess::query(f.api, id).value("quality").toString(),
                 QString("higher"));
        playback.shutdown();
    }
    void automaticFallbackUsesReturnedFileQuality_data()
    {
        QTest::addColumn<QString>("returnedHash");
        QTest::addColumn<QString>("expected");
        QTest::newRow("actual-standard") << hashA << QString("standard");
        QTest::newRow("actual-ogg-high") << QString(32, 'd') << QString("higher");
        QTest::newRow("unidentified-file") << QString(32, 'e') << QString("unknown");
    }
    void automaticFallbackUsesReturnedFileQuality()
    {
        QFETCH(QString, returnedHash);
        QFETCH(QString, expected);
        Fixture f;
        QSignalSpy resolved(&f.request, &PlaybackSourceRequest::resolved);
        PlaybackTrack track;
        track.id = "automatic-quality";
        track.hash = hashA;
        const QJsonObject rights{
            {"variants", QJsonArray{QJsonObject{{"quality", "128"}, {"hash", hashA}},
                                    QJsonObject{{"quality", "320"}, {"hash", hashB}},
                                    QJsonObject{{"quality", "flac"}, {"hash", hashC}}}},
            {"ogg_high", QJsonObject{{"hash", QString(32, 'd')}}}};
        f.request.resolve(track, ApiClient::PlaybackQuality::HiRes, ApiClient::SoundEffect::None,
                          rights, true);
        for (const auto &quality : {"flac", "higher", "standard"}) {
            const auto id = ApiClientTestAccess::pending(f.api, "song_url_auth_merge");
            QVERIFY(id);
            QCOMPARE(ApiClientTestAccess::query(f.api, id).value("quality").toString(), quality);
            ApiClientTestAccess::fail(f.api, id);
        }
        const auto automatic = ApiClientTestAccess::pending(f.api, "song_url_auth_merge");
        QCOMPARE(ApiClientTestAccess::query(f.api, automatic).value("quality").toString(),
                 QString("automatic"));
        auto returned = source("https://example.invalid/automatic");
        returned.insert("hash", returnedHash);
        ApiClientTestAccess::finish(f.api, automatic, returned);
        QCOMPARE(resolved.size(), 1);
        QCOMPARE(resolved[0][4].toString(), expected);
    }
    void liveCatalogQualitySelection_data()
    {
        QTest::addColumn<QString>("hash");
        QTest::addColumn<quint64>("songId");
        QTest::addColumn<quint64>("albumId");
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("initialQuality");
        QTest::addColumn<QString>("targetQuality");
        QTest::addColumn<QString>("actualQuality");
        QTest::addColumn<bool>("compatibility");
        QTest::addColumn<bool>("manual");
        QTest::newRow("default-hires-descends-to-sq")
            << QString("25a6f521a0335d52a94627e6da1291cd") << quint64(28006146) << quint64(8513112)
            << QStringLiteral("星星和雨的夜") << QString("standard") << QString("hi_res")
            << QString("flac") << true << false;
        QTest::newRow("yumeng-default-hq-strict")
            << QString("afb1eed627ae609837004f3ef9cd4e73") << quint64(106489261)
            << quint64(15462792) << QStringLiteral("遇梦记 (洛天依ver.)") << QString("higher")
            << QString("higher") << QString("higher") << false << false;
        QTest::newRow("yumeng-manual-hq-strict")
            << QString("afb1eed627ae609837004f3ef9cd4e73") << quint64(106489261)
            << quint64(15462792) << QStringLiteral("遇梦记 (洛天依ver.)") << QString("standard")
            << QString("higher") << QString("higher") << false << true;
        QTest::newRow("yumeng-default-hq-compatible")
            << QString("afb1eed627ae609837004f3ef9cd4e73") << quint64(106489261)
            << quint64(15462792) << QStringLiteral("遇梦记 (洛天依ver.)") << QString("higher")
            << QString("higher") << QString("higher") << true << false;
        QTest::newRow("yumeng-manual-hq-compatible")
            << QString("afb1eed627ae609837004f3ef9cd4e73") << quint64(106489261)
            << quint64(15462792) << QStringLiteral("遇梦记 (洛天依ver.)") << QString("standard")
            << QString("higher") << QString("higher") << true << true;
        QTest::newRow("yumeng-start-hires-compatible")
            << QString("afb1eed627ae609837004f3ef9cd4e73") << quint64(106489261)
            << quint64(15462792) << QStringLiteral("遇梦记 (洛天依ver.)") << QString("hi_res")
            << QString("hi_res") << QString("higher") << true << false;
        QTest::newRow("replaced-hires-compatible")
            << QString("de3d63f0a6f9f867d56ff38a0159fe35") << quint64(795678654)
            << quint64(162333400) << QStringLiteral("笨鸥") << QString("hi_res")
            << QString("hi_res") << QString("hi_res") << true << false;
        QTest::newRow("replaced-manual-hq-compatible")
            << QString("de3d63f0a6f9f867d56ff38a0159fe35") << quint64(795678654)
            << quint64(162333400) << QStringLiteral("笨鸥") << QString("standard")
            << QString("higher") << QString("higher") << true << true;
    }
    void liveCatalogQualitySelection()
    {
        if (qEnvironmentVariable("ECHO_LIVE_CATALOG_QUALITY") != "1")
            QSKIP("Opt-in selected account; stdin credentials only");
        QFETCH(QString, hash);
        QFETCH(quint64, songId);
        QFETCH(quint64, albumId);
        QFETCH(QString, title);
        QFETCH(QString, initialQuality);
        QFETCH(QString, targetQuality);
        QFETCH(QString, actualQuality);
        QFETCH(bool, compatibility);
        QFETCH(bool, manual);
        const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        DeviceIdentity device{identity.value("mid").toString(),
                              identity.value("dfid").toString(),
                              identity.value("guid").toString(),
                              identity.value("mac").toString(),
                              "EchoMusicNext",
                              identity.value("uuid").toString(),
                              "-"};
        ApiClient api(device);
        QTemporaryDir dir;
        SettingsStore settings(dir.filePath("settings.ini"));
        settings.setValue("playback/volume", 0);
        settings.setValue("playback/defaultQuality", initialQuality);
        settings.setValue("playback/compatibilityMode", compatibility);
        settings.setValue("playerEngine/audioSamplerate", "48000");
        settings.setValue("playback/transitionMode", "none");
        settings.setValue("playback/mvAudioFallbackEnabled", false);
        NetworkPreferences network(settings, api);
        QVERIFY(network.transportAvailable());
        const auto previous = QNetworkProxy::applicationProxy();
        const auto restore = qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previous); });
        network.installApplicationTransport();
        QVERIFY(api.setAccount(quint64(identity.value("userid").toInteger()),
                               identity.value("token").toString(),
                               identity.value("t1").toString()));
        QList<QJsonObject> acceptedSources;
        PlaybackService playback(api, settings, nullptr, dir.filePath("playback.sqlite"), &network);
        PlaybackServiceTestAccess::observeEngine(playback, &acceptedSources);
        QSignalSpy sourceFailures(&playback, &PlaybackService::sourceOperationFailed);
        QSignalSpy playbackClosed(&playback, &PlaybackService::closed);
        QSignalSpy apiClosed(&api, &ApiClient::closed);
        const auto cleanup = qScopeGuard([&] {
            playback.shutdown();
            api.shutdown();
            QVERIFY2(QTest::qWaitFor(
                         [&] { return !playbackClosed.isEmpty() && !apiClosed.isEmpty(); }, 15000),
                     "Live quality probe did not drain playback and API before teardown");
        });
        PlaybackTrack track;
        track.id = QString::number(songId);
        track.hash = hash;
        track.songId = songId;
        track.albumId = albumId;
        track.title = title;
        playback.playTracks({track}, 0, "Catalog quality verification",
                            "catalog-quality-verification");
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() || !playback.errorMessage().isEmpty(), 30000);
        QVERIFY2(playback.errorMessage().isEmpty(), qPrintable(playback.errorMessage()));
        QTRY_VERIFY_WITH_TIMEOUT(!acceptedSources.isEmpty(), 30000);
        QCOMPARE(playback.resolvedQuality(),
                 initialQuality == targetQuality ? actualQuality : initialQuality);
        if (hash == "de3d63f0a6f9f867d56ff38a0159fe35") {
            const auto current = PlaybackServiceTestAccess::catalogResource(playback);
            QVERIFY(current.compare(hash, Qt::CaseInsensitive) != 0);
            qInfo() << "Live catalog resource refreshed" << current;
        }
        QTRY_VERIFY_WITH_TIMEOUT(playback.clockPosition() > 100, 10000);
        if (initialQuality != targetQuality) {
            if (manual)
                QTRY_VERIFY_WITH_TIMEOUT(playback.clockPosition() > 5000, 15000);
            acceptedSources.clear();
            if (manual)
                playback.setQuality(targetQuality);
            else
                playback.preferences()->setDefaultQuality(targetQuality);
            QTRY_COMPARE(playback.quality(), targetQuality);
            QTRY_COMPARE_WITH_TIMEOUT(playback.resolvedQuality(), actualQuality, 30000);
            QTRY_VERIFY_WITH_TIMEOUT(!acceptedSources.isEmpty(), 30000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && !playback.loading(), 30000);
        const auto adoptedPosition = playback.clockPosition();
        QTRY_VERIFY_WITH_TIMEOUT(playback.clockPosition() > adoptedPosition + 100, 10000);
        if (manual) {
            // Exercise later Vorbis block boundaries and repeated same-song
            // switches, rather than only a freshly opened stream at time zero.
            for (const auto &choice :
                 {QString("standard"), QString("higher"), QString("standard"), QString("higher")}) {
                const auto previous = playback.effectiveQuality();
                acceptedSources.clear();
                playback.setQuality(choice);
                QCOMPARE(playback.effectiveQuality(), previous);
                QTRY_COMPARE_WITH_TIMEOUT(playback.resolvedQuality(), choice, 30000);
                QTRY_VERIFY_WITH_TIMEOUT(!acceptedSources.isEmpty(), 30000);
                QVERIFY(sourceFailures.isEmpty());
                const auto position = playback.clockPosition();
                QTRY_VERIFY_WITH_TIMEOUT(playback.clockPosition() > position + 1000, 10000);
            }
        }
        QVERIFY(sourceFailures.isEmpty());
        QVERIFY2(playback.errorMessage().isEmpty(), qPrintable(playback.errorMessage()));
        qInfo() << "Live catalog quality" << title << "compatibility" << compatibility << "manual"
                << manual << playback.quality() << playback.resolvedQuality()
                << playback.effectiveQuality() << playback.sourceKind() << "position advanced"
                << playback.clockPosition();
        QCOMPARE(playback.quality(), targetQuality);
        QCOMPARE(playback.resolvedQuality(), actualQuality);
        QCOMPARE(playback.effectiveQuality(), actualQuality);
        QCOMPARE(playback.sourceKind(), QString("catalog"));
        QCOMPARE(playback.preferences()->defaultQuality(), manual ? initialQuality : targetQuality);
    }

    void changingDefaultUsesLadderUnlessCurrentQualityWasChosen_data()
    {
        QTest::addColumn<bool>("manual");
        QTest::newRow("follow-new-default") << false;
        QTest::newRow("preserve-current-choice") << true;
    }
    void changingDefaultUsesLadderUnlessCurrentQualityWasChosen()
    {
        QFETCH(bool, manual);
        Fixture f;
        QTemporaryDir directory;
        SettingsStore settings(directory.filePath("settings.ini"));
        settings.setValue("playback/defaultQuality", "standard");
        PlaybackService playback(f.api, settings);
        PlaybackTrack track;
        track.id = "current-quality-selection";
        track.hash = hashA;
        const QJsonObject rights{
            {"hash", hashA},
            {"variants", QJsonArray{QJsonObject{{"quality", "128"}, {"hash", hashA}},
                                    QJsonObject{{"quality", "320"}, {"hash", hashB}},
                                    QJsonObject{{"quality", "flac"}, {"hash", hashC}}}}};
        PlaybackServiceTestAccess::seedCatalogSelection(playback, track, rights);
        quint64 previous = 0;
        if (manual) {
            playback.setQuality("higher");
            QTRY_VERIFY((previous = ApiClientTestAccess::pending(f.api, "song_url_auth_merge")));
        }
        playback.preferences()->setDefaultQuality("hi_res");
        QCOMPARE(playback.preferences()->defaultQuality(), QString("hi_res"));
        QCOMPARE(playback.quality(), manual ? QString("higher") : QString("hi_res"));
        quint64 id = 0;
        QTRY_VERIFY((id = ApiClientTestAccess::pending(f.api, "song_url_auth_merge")));
        if (manual) {
            QCOMPARE(id, previous);
            QCOMPARE(ApiClientTestAccess::query(f.api, id).value("quality").toString(),
                     QString("higher"));
        } else {
            for (const auto &quality : {QString("flac"), QString("higher"), QString("standard")}) {
                id = ApiClientTestAccess::pending(f.api, "song_url_auth_merge");
                QVERIFY(id);
                QCOMPARE(ApiClientTestAccess::query(f.api, id).value("quality").toString(),
                         quality);
                ApiClientTestAccess::fail(f.api, id);
            }
        }
        QVERIFY(!playback.playing());
        playback.shutdown();
    }
    void missingPreferredQualityDescendsBeforeAutomaticFallback_data()
    {
        QTest::addColumn<int>("failures");
        QTest::newRow("hires-missing-use-flac") << 0;
        QTest::newRow("flac-unavailable-use-hq") << 1;
        QTest::newRow("hq-unavailable-use-standard") << 2;
    }
    void missingPreferredQualityDescendsBeforeAutomaticFallback()
    {
        QFETCH(int, failures);
        Fixture f;
        QSignalSpy resolved(&f.request, &PlaybackSourceRequest::resolved);
        PlaybackTrack track;
        track.id = "quality-ladder";
        track.hash = hashA;
        track.albumId = 1;
        track.songId = 2;
        const QJsonObject rights{{"variants", QJsonArray{QJsonObject{{"quality", "128"},
                                                                     {"level", 2},
                                                                     {"hash", hashA},
                                                                     {"privilege", 8},
                                                                     {"service_status", 1}},
                                                         QJsonObject{{"quality", "320"},
                                                                     {"level", 4},
                                                                     {"hash", hashB},
                                                                     {"privilege", 10},
                                                                     {"service_status", 0}},
                                                         QJsonObject{{"quality", "flac"},
                                                                     {"level", 5},
                                                                     {"hash", hashC},
                                                                     {"privilege", 10},
                                                                     {"service_status", 0}}}}};
        f.request.resolve(track, ApiClient::PlaybackQuality::HiRes, ApiClient::SoundEffect::None,
                          rights, true);
        const QStringList qualities{"flac", "higher", "standard"};
        const QStringList hashes{hashC, hashB, hashA};
        for (int index = 0; index <= failures; ++index) {
            const auto id = ApiClientTestAccess::pending(f.api, "song_url_auth_merge");
            QVERIFY(id);
            const auto query = ApiClientTestAccess::query(f.api, id);
            QCOMPARE(query.value("quality").toString(), qualities[index]);
            QCOMPARE(query.value("hash").toString(), hashes[index]);
            QVERIFY(!query.contains("origin_page"));
            if (index < failures)
                ApiClientTestAccess::fail(f.api, id);
            else
                ApiClientTestAccess::finish(f.api, id, source("https://example.invalid/quality"));
        }
        QCOMPARE(resolved.size(), 1);
        QCOMPARE(resolved[0][4].toString(), qualities[failures]);
        QVERIFY(!ApiClientTestAccess::pending(f.api, "song_url_auth_merge"));
    }
    void qualityRightsRespectFreeResourcesAndUnknownFacts()
    {
        using Q = ApiClient::PlaybackQuality;
        auto rights = [](const QString &q, int privilege, int status) {
            return QJsonObject{{"variants", QJsonArray{QJsonObject{{"quality", q},
                                                                   {"privilege", privilege},
                                                                   {"service_status", status},
                                                                   {"hash", hashA}}}}};
        };
        QVERIFY(PlaybackSourcePlan::qualityAccessReason(Q::ViperTape, {true, false, false}, true,
                                                        rights("viper_tape", 0, 1))
                    .isEmpty());
        QVERIFY(!PlaybackSourcePlan::qualityAccessReason(Q::ViperTape, {true, true, false}, true,
                                                         rights("viper_tape", 10, 0))
                     .isEmpty());
        QVERIFY(PlaybackSourcePlan::qualityAccessReason(Q::Standard, {}, true, rights("128", 10, 0))
                    .isEmpty());
        QVERIFY(PlaybackSourcePlan::qualityAccessReason(Q::Flac, {true, true, false}, true,
                                                        rights("flac", 10, 0))
                    .isEmpty());
        QVERIFY(!PlaybackSourcePlan::qualityAccessReason(Q::HiRes, {true, true, true}, true,
                                                         rights("flac", 0, 1))
                     .isEmpty());
        const QJsonObject unknown{
            {"variants", QJsonArray{QJsonObject{{"quality", "flac"}, {"hash", hashA}}}}};
        QVERIFY(PlaybackSourcePlan::qualityAccessReason(Q::Flac, {}, true, unknown).isEmpty());
        const QJsonObject duplicate{
            {"variants",
             QJsonArray{QJsonObject{{"quality", "128"}, {"hash", hashA}, {"level", 2}},
                        QJsonObject{{"quality", "128"}, {"hash", hashB}, {"level", 1}}}}};
        QCOMPARE(
            PlaybackSourcePlan::qualityVariant(Q::Standard, duplicate).value("hash").toString(),
            hashB);
        const auto plan = PlaybackSourcePlan::catalog(hashC, duplicate, Q::Standard,
                                                      ApiClient::SoundEffect::None, false, true);
        QCOMPARE(plan.first().hash, hashA);
    }
    void cloudIndexKeepsFirstMatchAndCancelsOldAccount()
    {
        Fixture f;
        f.cloud.ensure();
        auto id = ApiClientTestAccess::pending(f.api, "user_cloud");
        QVERIFY(id);
        ApiClientTestAccess::finish(f.api, id,
                                    page({file(12, hashA, 1, hashC), file(12, hashB, 2)}, 3));
        id = ApiClientTestAccess::pending(f.api, "user_cloud");
        QVERIFY(id);
        QCOMPARE(ApiClientTestAccess::query(f.api, id).value("page").toInt(), 2);
        ApiClientTestAccess::finish(f.api, id, page({file(13, hashB, 3)}, 3));
        PlaybackTrack track;
        track.id = "12";
        track.songId = 12;
        track.hash = hashC;
        QCOMPARE(f.cloud.source(track).value("file_id").toInt(), 1);
        track.songId = 0;
        track.hash = hashB;
        QCOMPARE(f.cloud.source(track).value("file_id").toInt(), 2);
        const auto byAudio =
            PlaybackTrack::fromDomain({{"id", "0"}, {"hash", hashC}, {"audio_id", 1012}});
        QCOMPARE(f.cloud.source(byAudio).value("file_id").toInt(), 1);
        const auto byStd = PlaybackTrack::fromDomain(
            {{"id", "0"}, {"hash", QString(32, 'd')}, {"hash_std", hashC}});
        QCOMPARE(f.cloud.source(byStd).value("file_id").toInt(), 1);
        QCOMPARE(byAudio.catalogMetadata.value("audio_id").toInt(), 1012);
        f.cloud.invalidate();
        f.cloud.ensure();
        id = ApiClientTestAccess::pending(f.api, "user_cloud");
        QVERIFY(id);
        f.api.setAccount(900002, "other-isolated-token");
        emit f.api.succeeded(id, QJsonDocument(page({file(12, hashA, 1)}, 1)).toJson());
        QVERIFY(f.cloud.source(track).isEmpty());
    }
    void catalogThenCloudAndCloudPriorityUseDistinctIdentities()
    {
        Fixture f;
        QSignalSpy resolved(&f.request, &PlaybackSourceRequest::resolved);
        PlaybackTrack track;
        track.id = "12";
        track.songId = 12;
        track.hash = hashC;
        f.cloud.ensure();
        auto id = ApiClientTestAccess::pending(f.api, "user_cloud");
        ApiClientTestAccess::finish(f.api, id, page({file(12, hashA, 71, hashC)}, 1));
        const QJsonObject rights{
            {"variants", QJsonArray{QJsonObject{{"quality", "flac"}, {"hash", hashB}}}}};
        f.request.resolve(track, ApiClient::PlaybackQuality::Flac, ApiClient::SoundEffect::None,
                          rights, false);
        id = ApiClientTestAccess::pending(f.api, "song_url_auth_merge");
        QVERIFY(id);
        QCOMPARE(ApiClientTestAccess::query(f.api, id).value("hash").toString(), hashB);
        ApiClientTestAccess::fail(f.api, id);
        id = ApiClientTestAccess::pending(f.api, "song_url_auth_merge");
        QVERIFY(id);
        ApiClientTestAccess::fail(f.api, id);
        id = ApiClientTestAccess::pending(f.api, "user_cloud_url");
        QVERIFY(id);
        QCOMPARE(ApiClientTestAccess::query(f.api, id).value("hash").toString(), hashA);
        QCOMPARE(ApiClientTestAccess::query(f.api, id).value("album_audio_id").toInt(), 12);
        ApiClientTestAccess::finish(f.api, id, source("https://example.invalid/cloud"));
        QCOMPARE(resolved.size(), 1);
        QCOMPARE(resolved[0][2].toJsonObject().value("source_kind").toString(), QString("cloud"));
        track.id = "cloud-file:71";
        track.catalogMetadata = {
            {"source_kind", "cloud"},
            {"cloud_source", CloudAudioIndex::fromRow(file(12, hashA, 71, hashC))}};
        f.request.resolve(track, ApiClient::PlaybackQuality::Flac, ApiClient::SoundEffect::None,
                          rights, false);
        QVERIFY(!ApiClientTestAccess::pending(f.api, "song_url_auth_merge"));
        id = ApiClientTestAccess::pending(f.api, "user_cloud_url");
        QVERIFY(id);
        ApiClientTestAccess::finish(f.api, id, source("https://example.invalid/cloud"));
        QVERIFY(f.request.continueAfterFailure());
        id = ApiClientTestAccess::pending(f.api, "song_url_auth_merge");
        QVERIFY(id);
        QCOMPARE(ApiClientTestAccess::query(f.api, id).value("hash").toString(), hashB);
        QCOMPARE(resolved.last()[0].value<PlaybackTrack>().id, QString("cloud-file:71"));
    }
    void decoderFailureContinuesCandidateAndStaleReplyCannotResolve()
    {
        Fixture f;
        QSignalSpy resolved(&f.request, &PlaybackSourceRequest::resolved);
        PlaybackTrack track;
        track.id = "12";
        track.songId = 12;
        track.hash = hashA;
        const QJsonObject rights{
            {"variants", QJsonArray{QJsonObject{{"quality", "high"}, {"hash", hashB}},
                                    QJsonObject{{"quality", "flac"}, {"hash", hashC}}}}};
        f.request.resolve(track, ApiClient::PlaybackQuality::HiRes, ApiClient::SoundEffect::None,
                          rights, true);
        auto id = ApiClientTestAccess::pending(f.api, "song_url_auth_merge");
        QVERIFY(id);
        ApiClientTestAccess::finish(f.api, id, source("https://example.invalid/first"));
        QCOMPARE(resolved.size(), 1);
        QVERIFY(f.request.continueAfterFailure());
        id = ApiClientTestAccess::pending(f.api, "song_url_auth_merge");
        QVERIFY(id);
        QCOMPARE(ApiClientTestAccess::query(f.api, id).value("hash").toString(), hashC);
        f.request.cancel();
        emit f.api.succeeded(id, QJsonDocument(source("https://example.invalid/stale")).toJson());
        QCOMPARE(resolved.size(), 1);
        QVERIFY(!PlaybackSourceRequest::mediaError(
            {{"error_code", "output-stream"}, {"message", "decode failed"}}));
        QVERIFY(PlaybackSourceRequest::mediaError({{"error_code", "decode"}}));
    }
    void mvSelectionIsBoundedAndPendingQueriesCancel()
    {
        Fixture f;
        QSignalSpy resolved(&f.request, &PlaybackSourceRequest::resolved);
        PlaybackTrack track;
        track.id = "12";
        track.songId = 12;
        track.hash = hashA;
        QVERIFY(f.request.tryMv(track));
        auto id = ApiClientTestAccess::pending(f.api, "kmr_audio_mv");
        QVERIFY(id);
        QJsonArray formats{QJsonObject{{"hash", hashA}, {"codec", "MKV"}, {"size_bytes", 100}},
                           QJsonObject{{"hash", hashB}, {"codec", "MKV"}, {"size_bytes", 10}},
                           QJsonObject{{"hash", hashC}, {"codec", "H.264"}, {"size_bytes", 20}}};
        QJsonArray videos{QJsonObject{{"audio_fallback_eligible", false},
                                      {"duration_ms", 1000},
                                      {"recommendation_code", 9},
                                      {"sources", formats}},
                          QJsonObject{{"audio_fallback_eligible", true},
                                      {"duration_ms", 1000},
                                      {"sources", formats}}};
        QCOMPARE(PlaybackSourceRequest::mvHashes(videos), QStringList({hashB, hashC}));
        ApiClientTestAccess::finish(f.api, id, videos);
        int count = 0;
        while ((id = ApiClientTestAccess::pending(f.api, "video_url"))) {
            ApiClientTestAccess::finish(
                f.api, id,
                QJsonObject{{"url", QString("https://example.invalid/mv%1").arg(++count)},
                            {"backup_urls", QJsonArray{}}});
        }
        QCOMPARE(count, 2);
        QCOMPARE(resolved.size(), 1);
        QCOMPARE(resolved[0][2].toJsonObject().value("source_kind").toString(), QString("mv"));
        QVERIFY(!f.request.tryMv(track));
        f.request.cancel();
        QVERIFY(f.request.tryMv(track));
        id = ApiClientTestAccess::pending(f.api, "kmr_audio_mv");
        f.request.disableMv();
        emit f.api.succeeded(id, QJsonDocument(videos).toJson());
        QCOMPARE(resolved.size(), 1);
    }
    void cloudLoudnessFailureKeepsCloudAndCancelDiscardsParallelMv()
    {
        Fixture f;
        QSignalSpy resolved(&f.request, &PlaybackSourceRequest::resolved);
        QSignalSpy failed(&f.request, &PlaybackSourceRequest::failed);
        PlaybackTrack track;
        track.id = "cloud-file:1";
        track.songId = 12;
        track.hash = hashA;
        track.catalogMetadata = {
            {"source_kind", "cloud"},
            {"cloud_source", CloudAudioIndex::fromRow(file(12, hashA, 1, hashB))}};
        f.request.resolve(track, ApiClient::PlaybackQuality::Standard, ApiClient::SoundEffect::None,
                          {}, true, true, {}, {}, true);
        auto id = ApiClientTestAccess::pending(f.api, "user_cloud_url");
        QVERIFY(id);
        ApiClientTestAccess::finish(f.api, id, source("https://example.invalid/cloud"));
        QCOMPARE(resolved.size(), 0);
        id = ApiClientTestAccess::pending(f.api, "song_url_auth_merge");
        QVERIFY(id);
        QCOMPARE(ApiClientTestAccess::query(f.api, id).value("hash").toString(), hashB);
        ApiClientTestAccess::fail(f.api, id);
        QCOMPARE(resolved.size(), 1);
        QCOMPARE(resolved[0][2].toJsonObject().value("source_kind").toString(), QString("cloud"));
        QCOMPARE(failed.size(), 0);
        f.request.cancel();
        QVERIFY(f.request.tryMv(track));
        id = ApiClientTestAccess::pending(f.api, "kmr_audio_mv");
        QJsonArray videos{
            QJsonObject{{"audio_fallback_eligible", true},
                        {"duration_ms", 1000},
                        {"sources", QJsonArray{QJsonObject{{"hash", hashA}, {"codec", "MKV"}},
                                               QJsonObject{{"hash", hashB}, {"codec", "H.264"}}}}}};
        ApiClientTestAccess::finish(f.api, id, videos);
        const auto first = ApiClientTestAccess::pending(f.api, "video_url");
        QVERIFY(first);
        f.request.cancel();
        emit f.api.succeeded(
            first, QJsonDocument(QJsonObject{{"url", "https://example.invalid/stale"}}).toJson());
        QCOMPARE(resolved.size(), 1);
        QCOMPARE(ApiClientTestAccess::pending(f.api, "video_url"), 0);
        QVERIFY(f.request.tryMv(track));
        id = ApiClientTestAccess::pending(f.api, "kmr_audio_mv");
        ApiClientTestAccess::finish(
            f.api, id,
            QJsonArray{QJsonObject{{"audio_fallback_eligible", false},
                                   {"audio_fallback_facts", QJsonObject{{"short_code", 4}}}}});
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed[0][0].toString().contains("短视频"));
    }
    void mvPreferenceIsOptInAndPersists()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore settings(dir.filePath("settings.ini"));
        PlaybackPreferences first(settings);
        QVERIFY(!first.mvAudioFallbackEnabled());
        first.setMvAudioFallbackEnabled(true);
        PlaybackPreferences second(settings);
        QVERIFY(second.mvAudioFallbackEnabled());
        second.setMvAudioFallbackEnabled(false);
    }
    void liveCloudAndMvUseActualAudioEngine();
};

void PlaybackSourceFallbackTest::liveCloudAndMvUseActualAudioEngine()
{
    if (qEnvironmentVariable("ECHO_LIVE_SOURCE_FALLBACK") != "1")
        QSKIP("Opt-in current account/audio; stdin credentials only");
    const std::string input((std::istreambuf_iterator<char>(std::cin)),
                            std::istreambuf_iterator<char>());
    const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
    DeviceIdentity device{identity.value("mid").toString(),
                          identity.value("dfid").toString(),
                          identity.value("guid").toString(),
                          identity.value("mac").toString(),
                          identity.value("device_name").toString(),
                          "-",
                          "-"};
    ApiClient api(device);
    QTemporaryDir dir;
    EchoMusic::SettingsStore settings(dir.filePath("settings.ini"));
    settings.setValue("playback/volume", 0);
    settings.setValue("playback/defaultQuality", "standard");
    settings.setValue("playback/transitionMode", "none");
    NetworkPreferences network(settings, api);
    QVERIFY(network.transportAvailable());
    const auto previous = QNetworkProxy::applicationProxy();
    const auto restore = qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previous); });
    network.installApplicationTransport();
    QVERIFY(api.setAccount(quint64(identity.value("userid").toInteger()),
                           identity.value("token").toString(), identity.value("t1").toString()));
    QSignalSpy result(&api, &ApiClient::succeeded);
    const auto response = [&](quint64 id) {
        for (const auto &row : result)
            if (row[0].toULongLong() == id)
                return row[1].toByteArray();
        return QByteArray();
    };
    auto id = api.userCloud(1, 100);
    QTRY_VERIFY_WITH_TIMEOUT(!response(id).isEmpty(), 30000);
    const auto files = QJsonDocument::fromJson(response(id))
                           .object()
                           .value("page")
                           .toObject()
                           .value("items")
                           .toArray();
    QVERIFY(!files.isEmpty());
    QJsonObject selected;
    for (const auto &v : files) {
        const auto row = v.toObject();
        if (row.value("song").toObject().value("hash").toString().size() == 32 &&
            row.value("size_bytes").toInteger() > 1024 * 1024 &&
            row.value("hash_std").toString().size() == 32) {
            selected = row;
            break;
        }
    }
    QVERIFY(!selected.isEmpty());
    auto cloudSong = selected.value("song").toObject();
    cloudSong["metadata"] =
        QJsonObject{{"source_kind", "cloud"}, {"cloud_source", CloudAudioIndex::fromRow(selected)}};
    if (cloudSong.value("id").toString().isEmpty() || cloudSong.value("id").toString() == "0")
        cloudSong["id"] = "cloud-file:" + QString::number(selected.value("file_id").toInteger());
    auto cloudTrack = PlaybackTrack::fromDomain(cloudSong);
    QVERIFY(cloudTrack.playable());
    PlaybackService playback(api, settings, nullptr, dir.filePath("playback.sqlite"), &network);
    playback.audioControls()->setVolumeNormalization(false);
    PlaybackServiceTestAccess::observeEngine(playback);
    QSignalSpy closed(&playback, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
    const auto cleanup = qScopeGuard([&] {
        playback.shutdown();
        api.shutdown();
    });
    playback.playTracks({cloudTrack}, 0, "Source verification", "source-verification");
    QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && playback.positionMs() > 1000, 45000);
    QCOMPARE(playback.sourceKind(), QString("cloud"));
    QVERIFY2(playback.errorMessage().isEmpty(), qPrintable(playback.errorMessage()));
    qInfo() << "Actual cloud URL/native playback accepted";
    id = api.albumSongs(966846, 1, 50);
    QTRY_VERIFY_WITH_TIMEOUT(!response(id).isEmpty(), 30000);
    const auto songs = QJsonDocument::fromJson(response(id))
                           .object()
                           .value("page")
                           .toObject()
                           .value("items")
                           .toArray();
    PlaybackTrack track;
    for (const auto &v : songs) {
        const auto song = v.toObject().value("song").toObject();
        if (song.value("id").toString() == "32100650") {
            track = PlaybackTrack::fromDomain(song);
            break;
        }
    }
    QVERIFY(track.playable());
    QTcpServer rejected;
    QVERIFY(rejected.listen(QHostAddress::LocalHost));
    bool notFound = false;
    connect(&rejected, &QTcpServer::newConnection, &rejected, [&] {
        while (auto *socket = rejected.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                socket->readAll();
                const QByteArray bytes = "invalid-media";
                socket->write(
                    notFound
                        ? QByteArray("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: "
                                     "close\r\n\r\n")
                        : QByteArray(
                              "HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: ") +
                              QByteArray::number(bytes.size()) + "\r\nConnection: close\r\n\r\n" +
                              bytes);
                socket->disconnectFromHost();
            });
        }
    });
    playback.preferences()->setMvAudioFallbackEnabled(true);
    for (const bool networkFailure : {false, true}) {
        notFound = networkFailure;
        playback.playTracks({track}, 0, "Source verification", "source-verification");
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && !playback.loading(), 45000);
        PlaybackServiceTestAccess::loadRejectedNormalMedia(
            playback, QString("http://127.0.0.1:%1/rejected.wav").arg(rejected.serverPort()));
        QTRY_VERIFY_WITH_TIMEOUT(playback.sourceKind() == "mv" && playback.playing() &&
                                     playback.positionMs() > 1000,
                                 60000);
        QVERIFY2(playback.errorMessage().isEmpty(), qPrintable(playback.errorMessage()));
        QVERIFY(playback.positionMs() < 15000);
        playback.seek(60000);
        QTRY_VERIFY_WITH_TIMEOUT(playback.positionMs() > 59000, 15000);
        qInfo() << "Actual MV query/URL/audio/seek accepted after controlled"
                << (networkFailure ? "HTTP 404" : "native decode failure");
    }
    playback.preferences()->setMvAudioFallbackEnabled(false);
    QCOMPARE(playback.sourceKind(), QString("mv"));
    QVERIFY(playback.playing());
    playback.setQuality("standard");
    QTimer::singleShot(10000, &playback, [&] {
        qInfo() << "Catalog restore state" << playback.sourceKind() << playback.loading()
                << playback.playing() << playback.errorMessage();
    });
    QTRY_VERIFY_WITH_TIMEOUT(playback.sourceKind() == "catalog" && !playback.loading(), 15000);
    QVERIFY2(playback.errorMessage().isEmpty(), qPrintable(playback.errorMessage()));
    playback.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 20000);
    api.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
}
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    PlaybackSourceFallbackTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "PlaybackSourceFallbackTest.moc"
