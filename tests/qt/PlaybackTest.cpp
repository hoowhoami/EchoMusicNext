#include "PlaybackQueueCatalog.h"
#include "PlaybackRepository.h"
#include "AudioEngine.h"
#include "PlaybackService.h"
#include "PlaybackSourcePlan.h"
#include "SongCommands.h"
#include "SongShareText.h"
#include "ShareTarget.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QApplication>
#include <QFileDialog>
#include <QJsonDocument>
#include <iostream>
#include "LyricModel.h"
#include "LyricTextController.h"
#include "SleepTimer.h"
#include <QAbstractItemModelTester>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QJsonArray>
#include <QDataStream>
#include <QFile>
#include <QSqlQuery>
#include <QTcpServer>
#include <QtTest>
#include <QNetworkProxy>
#include <QScopeGuard>
using namespace EchoMusic;
namespace EchoMusic
{
class MembershipServiceTestAccess
{
public:
    static quint64 begin(MembershipService &service, ApiClient &api)
    {
        service.m_revision = api.sessionRevision();
        return service.m_request = api.capabilities(); // No-network domain/lifecycle seam.
    }
    static void finish(MembershipService &service, quint64 id, const QJsonObject &domain)
    {
        service.succeed(id, QJsonDocument(domain).toJson(QJsonDocument::Compact));
    }
};
class TrackTransitionControllerTestAccess
{
public:
    static QString formatNotice(const QJsonObject &event)
    {
        return TrackTransitionController::formatNotice(event);
    }
    static void seedResolved(TrackTransitionController &transition)
    {
        transition.m_resolved = {{"urls", QJsonArray{"domain-only-prepared-source"}}};
        transition.m_prepared = true;
    }
    static bool prepared(TrackTransitionController &transition)
    {
        return transition.m_prepared;
    }
    static quint64 revision(TrackTransitionController &transition)
    {
        return transition.m_revision;
    }
};
class AudioEffectCatalogTestAccess
{
public:
    static void finish(AudioEffectCatalog &catalog, ApiClient &api, const QJsonObject &domain)
    {
        catalog.cancel();
        catalog.m_revision = api.sessionRevision();
        catalog.m_request = api.capabilities();
        catalog.succeeded(catalog.m_request, QJsonDocument(domain).toJson());
    }
};
class PlaybackClimaxModelTestAccess
{
public:
    static quint64 begin(PlaybackClimaxModel &model, ApiClient &api, qint64 duration)
    {
        model.clear();
        model.m_duration = duration;
        model.m_revision = api.sessionRevision();
        return model.m_request = api.capabilities(); // No-network lifecycle seam.
    }
    static void finish(PlaybackClimaxModel &model, quint64 id, const QJsonArray &domain)
    {
        model.succeed(id, QJsonDocument(domain).toJson(QJsonDocument::Compact));
    }
};
class PlaybackServiceTestAccess
{
public:
    static ListeningReporter &listening(PlaybackService &p)
    {
        return p.m_listening;
    }
    static void resolvedQuality(PlaybackService &playback, const QString &quality)
    {
        playback.m_resolvedQuality = quality;
        emit playback.changed();
    }
    static void advanceListening(PlaybackService &playback, const QString &sourceKind, double time)
    {
        PlaybackTrack track;
        track.id = QStringLiteral("1");
        track.hash = QString(32, 'a');
        track.songId = 1;
        track.durationMs = 60000;
        playback.m_track = track;
        playback.m_sourceKind = sourceKind;
        playback.m_ready = true;
        playback.m_loaded = true;
        playback.m_loading = false;
        playback.m_playing = true;
        playback.m_error.clear();
        playback.consumeOutputEvent({{"event", "time-update"}, {"time", time}});
    }
    static void offerBackupUrl(PlaybackService &playback, const QString &url)
    {
        playback.m_urls = {url};
        playback.m_urlIndex = 0;
        playback.m_loaded = true;
        playback.m_seamless = false;
    }
    static void outputEvent(PlaybackService &playback, const QJsonObject &event)
    {
        playback.consumeOutputEvent(event);
    }
};
class PlaybackAudioControlsTestAccess
{
public:
    static void pendingOutput(PlaybackAudioControls &controls, quint64 revision)
    {
        controls.m_outputRevision = revision;
        controls.m_outputPending = true;
    }
};
class LyricSourceControllerTestAccess
{
public:
    static quint64 search(LyricSourceController &sources, ApiClient &api,
                          const PlaybackTrack &track)
    {
        sources.clear();
        if (sources.m_cacheTrack != track.key()) {
            sources.m_cache.clear();
            sources.m_cacheOrder.clear();
            sources.m_cacheTrack = track.key();
        }
        sources.m_track = track;
        sources.m_revision = api.sessionRevision();
        sources.m_search = api.capabilities();
        sources.m_activatePending = true;
        return sources.m_search;
    }
    static void succeed(LyricSourceController &sources, quint64 id, const QByteArray &bytes)
    {
        const auto object = QJsonDocument::fromJson(bytes).object();
        if (id == sources.m_download && object.contains("lines")) {
            sources.m_download = 0;
            sources.acceptParsed(object);
        } else
            sources.succeeded(id, bytes);
    }
    static void seedCandidates(LyricSourceController &sources)
    {
        if (sources.m_search)
            sources.m_api.cancel(sources.m_search);
        sources.m_search = 0;
        sources.m_catalogReady = true;
        sources.beginResetModel();
        sources.m_candidates = {
            {"a", "", "First", "Artist", "官方推荐歌词", "中文", 4000, 60, true, true},
            {"b", "", "Second", "Artist", "ugc", "中文", 4000, 40, true, true}};
        sources.m_selected = 0;
        sources.endResetModel();
    }
    static quint64 searchRequest(const LyricSourceController &s)
    {
        return s.m_search;
    }
    static quint64 defaultRequest(const LyricSourceController &s)
    {
        return s.m_defaultSearch;
    }
    static quint64 downloadRequest(const LyricSourceController &s)
    {
        return s.m_download;
    }
    static void fail(LyricSourceController &s, quint64 id)
    {
        s.failed(id, 0, R"({"message":"test failure"})");
    }
    static void seedPreview(LyricSourceController &sources, quint64 request, const QString &id)
    {
        sources.m_download = request;
        sources.m_downloadId = id;
        sources.m_error.clear();
    }
};
class ListeningReporterTestAccess
{
public:
    static void tick(ListeningReporter &r, qint64 now, qint64 position, double speed = 1,
                     bool advancing = true, const QString &track = "catalog/1", quint64 song = 1,
                     bool eligible = true)
    {
        r.tickAt(track, song, eligible, advancing, position, speed, now);
    }
    static qint64 duration(const ListeningReporter &r)
    {
        return r.m_duration;
    }
    static qint64 pending(const ListeningReporter &r)
    {
        return r.m_pendingMs;
    }
    static int queued(const ListeningReporter &r)
    {
        return r.m_events.size();
    }
    static quint64 eventRequest(const ListeningReporter &r)
    {
        return r.m_eventRequest;
    }
    static quint64 gradeRequest(const ListeningReporter &r)
    {
        return r.m_gradeRequest;
    }
    static quint64 ledger(const ListeningReporter &r)
    {
        return r.m_ledger;
    }
    static QVariantMap tail(const ListeningReporter &r)
    {
        if (r.m_events.isEmpty())
            return {};
        const auto &e = r.m_events.last();
        return {{"song", qulonglong(e.song)},
                {"duration", e.duration},
                {"completed", e.completed},
                {"end", e.kind == ApiClient::ListeningEvent::End}};
    }
    static void finish(ListeningReporter &r, quint64 id, const QJsonObject &value)
    {
        r.m_api.cancel(id);
        r.succeed(id, QJsonDocument(value).toJson());
    }
    static void fail(ListeningReporter &r, quint64 id, const char *kind)
    {
        r.m_api.cancel(id);
        r.fail(id, QJsonDocument(QJsonObject{{"kind", kind}}).toJson());
    }
    static void expireShutdown(ListeningReporter &r)
    {
        r.reset();
    }
};
class ApiClientTestAccess
{
public:
    static bool replayable(const ApiClient &api, quint64 id)
    {
        return api.m_pending.value(id).automaticVerification;
    }
    static QJsonObject query(const ApiClient &api, quint64 id)
    {
        return api.m_pending.value(id).request.value("query").toObject();
    }
};
} // namespace EchoMusic
class PlaybackTest : public QObject
{
    Q_OBJECT
    static PlaybackTrack track(int i)
    {
        PlaybackTrack t;
        t.id = QString::number(i);
        t.localPath = QStringLiteral("/music/%1.wav").arg(i);
        t.title = QStringLiteral("曲目 %1").arg(i);
        t.durationMs = 4000;
        return t;
    }
    static void completeLyricPreview(LyricSourceController &sources, ApiClient &api,
                                     const QByteArray &lyrics)
    {
        if (const auto request = LyricSourceControllerTestAccess::downloadRequest(sources)) {
            api.cancel(request);
            LyricSourceControllerTestAccess::succeed(sources, request, lyrics);
        }
        QVERIFY(sources.preview()->rowCount() > 0);
    }
    static bool has(const QSignalSpy &spy, const QString &kind, quint64 sequence = 0)
    {
        for (const auto &entry : spy) {
            const auto event = entry[0].toJsonObject();
            if (event.value("event").toString() == kind &&
                (!sequence || quint64(event.value("seq").toDouble()) == sequence))
                return true;
        }
        return false;
    }
    static double position(const QSignalSpy &spy)
    {
        for (auto it = spy.crbegin(); it != spy.crend(); ++it) {
            const auto event = (*it)[0].toJsonObject();
            if (event.value("event").toString() == "time-update")
                return event.value("time").toDouble();
        }
        return -1;
    }
private slots:
    void localFilePickerReturnsImmediatelyAndCancelsOnSessionOrShutdown()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService player(api, settings);
        QSignalSpy closed(&player, &PlaybackService::closed);
        auto visibleDialog = []() -> QFileDialog * {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *dialog = qobject_cast<QFileDialog *>(widget);
                    dialog && dialog->isVisible())
                    return dialog;
            return nullptr;
        };
        player.openLocalFiles();
        QTRY_VERIFY(visibleDialog());
        QPointer<QFileDialog> first = visibleDialog();
        player.openLocalFiles();
        QCOMPARE(visibleDialog(), first.data());
        QVERIFY(api.setAccount(900001, "lifecycle-test-only"));
        QTRY_VERIFY(first.isNull());
        QVERIFY(!player.hasTrack());
        player.openLocalFiles();
        QTRY_VERIFY(visibleDialog());
        QPointer<QFileDialog> second = visibleDialog();
        player.shutdown();
        QVERIFY(second.isNull());
        QTRY_COMPARE(closed.size(), 1);
        player.openLocalFiles();
        QVERIFY(!visibleDialog());
        api.shutdown();
    }
    void listeningEventsCountOnlyAdvancingAudioAndNeverReplayAcrossAccounts()
    {
        QTcpServer transport;
        QVERIFY(transport.listen(QHostAddress::LocalHost));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(transport.serverPort()));
        QVERIFY(api.setAccount(900001, "protocol-test-only"));
        ListeningReporter report(api);
        using A = ListeningReporterTestAccess;
        A::tick(report, 0, 0);
        const auto start = A::eventRequest(report);
        QVERIFY(start);
        QCOMPARE(ApiClientTestAccess::query(api, start).value("event").toString(), "start");
        QVERIFY(!ApiClientTestAccess::replayable(api, start));
        A::tick(report, 1000, 1000);
        QCOMPARE(A::duration(report), 1000);
        A::tick(report, 2000, 3000, 2);
        QCOMPARE(A::duration(report), 2000);
        A::tick(report, 3000, 30000); // seek discontinuity
        QCOMPARE(A::duration(report), 2000);
        A::tick(report, 4000, 30000); // stalled
        QCOMPARE(A::duration(report), 2000);
        A::tick(report, 12000, 38000); // suspend/long gap
        QCOMPARE(A::duration(report), 2000);
        A::tick(report, 13000, 39000, 1, false); // paused
        A::tick(report, 24000, 40000);
        QCOMPARE(A::duration(report), 2000);
        A::tick(report, 25000, 41000);
        QCOMPARE(A::duration(report), 3000);
        report.flush(true);
        QCOMPARE(A::queued(report), 1);
        QCOMPARE(A::tail(report).value("duration").toLongLong(), 3000);
        QVERIFY(A::tail(report).value("completed").toBool());
        A::fail(report, start, "transport");
        QCOMPARE(A::eventRequest(report), quint64(0));
        QCOMPARE(A::queued(report), 1); // end survives failed start; spacing still applies
        A::tick(report, 26000, 0, 1, true, "catalog/2", 2);
        QCOMPARE(A::queued(report), 2);
        QVERIFY(api.setAccount(900002, "next-protocol-test"));
        QCOMPARE(A::queued(report), 0);
        QCOMPARE(A::pending(report), 0);
        A::finish(report, start, {{"accepted", true}}); // old result is inert
        QCOMPARE(A::queued(report), 0);
        A::tick(report, 27000, 0, 1, true, "local", 1, false);
        QCOMPARE(A::queued(report), 0);
        QSignalSpy drained(&report, &ListeningReporter::drained);
        report.shutdown();
        QCOMPARE(drained.size(), 1);
        api.shutdown();
    }
    void listeningScopeIsOnlineCatalogOnly()
    {
        QTcpServer transport;
        QVERIFY(transport.listen(QHostAddress::LocalHost));
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("listening.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(transport.serverPort()));
        QVERIFY(api.setAccount(900001, "protocol-test-only"));
        PlaybackService playback(api, settings);
        auto &report = PlaybackServiceTestAccess::listening(playback);
        using A = ListeningReporterTestAccess;
        for (const auto &kind : {QString("cloud"), QString("mv")}) {
            PlaybackServiceTestAccess::advanceListening(playback, kind, 0);
            PlaybackServiceTestAccess::advanceListening(playback, kind, 1);
            QCOMPARE(A::duration(report), qint64(0));
            QCOMPARE(A::eventRequest(report), quint64(0));
            QCOMPARE(A::queued(report), 0);
        }
        PlaybackServiceTestAccess::advanceListening(playback, "catalog", 2);
        QVERIFY(A::eventRequest(report));
    }
    void recoverableMediaErrorKeepsListeningSegment()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("backup.wav");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        constexpr quint32 bytes = 48000 * 2 * 2;
        stream.writeRawData("RIFF", 4);
        stream << quint32(36 + bytes);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000) << quint32(192000)
               << quint16(4) << quint16(16);
        stream.writeRawData("data", 4);
        stream << bytes;
        stream.writeRawData(QByteArray(bytes, 0).constData(), int(bytes));
        file.close();
        QTcpServer transport;
        QVERIFY(transport.listen(QHostAddress::LocalHost));
        EchoMusic::SettingsStore settings(directory.filePath("listening.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(transport.serverPort()));
        QVERIFY(api.setAccount(900001, "protocol-test-only"));
        PlaybackService playback(api, settings);
        playback.setVolume(0);
        auto &report = PlaybackServiceTestAccess::listening(playback);
        using A = ListeningReporterTestAccess;
        A::tick(report, 0, 0);
        A::tick(report, 1000, 1000);
        QCOMPARE(A::duration(report), qint64(1000));
        PlaybackServiceTestAccess::offerBackupUrl(playback, path);
        PlaybackServiceTestAccess::outputEvent(
            playback, {{"event", "error"}, {"message", "decode"}, {"error_code", "decode"}});
        QCOMPARE(A::duration(report), qint64(1000));
    }
    void listeningGradeWindowAnchorsToSixtyListenedSeconds()
    {
        QTcpServer transport;
        QVERIFY(transport.listen(QHostAddress::LocalHost));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(transport.serverPort()));
        QVERIFY(api.setAccount(900001, "protocol-test-only"));
        ListeningReporter report(api);
        using A = ListeningReporterTestAccess;
        // Playback starts after the reporter exists, so the construction-time
        // grade window is not aligned to the first full minute of listening.
        for (int time = 500; time <= 60000; time += 500)
            A::tick(report, time, time);
        QCOMPARE(A::pending(report), 59500);
        QVERIFY(!A::gradeRequest(report));
        A::tick(report, 60500, 60500);
        QCOMPARE(A::pending(report), 60000);
        QVERIFY(A::gradeRequest(report));
    }
    void listeningGradeBaselineTransportRetriesOncePerWindow()
    {
        QTcpServer transport;
        QVERIFY(transport.listen(QHostAddress::LocalHost));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(transport.serverPort()));
        QVERIFY(api.setAccount(900001, "protocol-test-only"));
        ListeningReporter report(api);
        using A = ListeningReporterTestAccess;
        for (int i = 0; i <= 60; ++i)
            A::tick(report, i * 1000, i * 1000);
        const auto first = A::gradeRequest(report);
        QVERIFY(first);
        A::fail(report, first, "transport");
        QCOMPARE(A::pending(report), 60000); // baseline failure keeps the minute
        const auto retry = A::gradeRequest(report);
        QVERIFY(retry);
        QVERIFY(retry != first);
        A::fail(report, retry, "transport");
        QCOMPARE(A::pending(report), 60000);
        QCOMPARE(A::gradeRequest(report), quint64(0)); // only one free retry
        for (int i = 61; i <= 120; ++i)
            A::tick(report, i * 1000, i * 1000);
        QVERIFY(A::gradeRequest(report)); // next window can retry again
        api.shutdown();
    }
    void listeningMinuteLedgerSeparatesGradeAndDropsUncertainWrites()
    {
        QTcpServer transport;
        QVERIFY(transport.listen(QHostAddress::LocalHost));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(transport.serverPort()));
        QVERIFY(api.setAccount(900001, "protocol-test-only"));
        ListeningReporter report(api);
        using A = ListeningReporterTestAccess;
        for (int i = 0; i <= 60; ++i)
            A::tick(report, i * 1000, i * 1000);
        QCOMPARE(A::pending(report), 60000);
        auto grade = A::gradeRequest(report);
        QVERIFY(grade);
        QVERIFY(!ApiClientTestAccess::replayable(api, grade));
        A::finish(report, grade, {{"listening_seconds", 1000}});
        grade = A::gradeRequest(report);
        QVERIFY(grade);
        QCOMPARE(ApiClientTestAccess::query(api, grade).value("baseline_seconds").toInteger(),
                 1000);
        QCOMPARE(ApiClientTestAccess::query(api, grade).value("increment_seconds").toInteger(), 60);
        QVERIFY(!ApiClientTestAccess::replayable(api, grade));
        A::finish(report, grade, {{"listening_seconds", 1000}}); // settlement lags
        QCOMPARE(A::ledger(report), 1060);
        QCOMPARE(A::pending(report), 0);
        for (int i = 61; i <= 120; ++i)
            A::tick(report, i * 1000, i * 1000);
        A::finish(report, A::gradeRequest(report), {{"listening_seconds", 1001}});
        grade = A::gradeRequest(report);
        QCOMPARE(ApiClientTestAccess::query(api, grade).value("baseline_seconds").toInteger(),
                 1060);
        A::fail(report, grade, "transport");
        QCOMPARE(A::pending(report), 0); // possibly committed, no replay
        for (int i = 121; i <= 180; ++i)
            A::tick(report, i * 1000, i * 1000);
        A::finish(report, A::gradeRequest(report), {{"listening_seconds", 1120}});
        A::fail(report, A::gradeRequest(report), "business");
        QCOMPARE(A::pending(report), 60000); // definite rejection can await next minute
        QSignalSpy drained(&report, &ListeningReporter::drained);
        report.shutdown();
        QCOMPARE(drained.size(), 0);
        A::expireShutdown(report);
        QCOMPARE(drained.size(), 1);
        QCOMPARE(A::queued(report), 0);
        api.shutdown();
    }
    void nonWordLyricsPreserveSecondaryTextAndDisableRuby()
    {
        LyricModel model(nullptr);
        QJsonObject line{{"text", "原词 AB"},     {"start_ms", 1000},
                         {"duration_ms", 3000},   {"words", QJsonArray{}},
                         {"translation", "翻译"}, {"romanization", "yuan ci AB"}};
        QVERIFY(model.replace({{"lines", QJsonArray{line}}}));
        QVERIFY(!model.lineAt(0).value("wordTimed").toBool());
        QVERIFY(model.rubyAt(0).isEmpty());
        QCOMPARE(model.lineAt(0).value("romanization").toString(), "yuan ci AB");
        for (const auto &unit : model.verticalAt(0))
            QVERIFY(!unit.toMap().value("wordTimed").toBool());
        line.insert("words", QJsonArray{QJsonObject{
                                 {"text", "原词 AB"}, {"start_ms", 1000}, {"duration_ms", 3000}}});
        QVERIFY(model.replace({{"lines", QJsonArray{line}}}));
        QVERIFY(model.lineAt(0).value("wordTimed").toBool());
        QVERIFY(!model.rubyAt(0).isEmpty()); // a single genuinely timed word is valid
        line.insert("words", QJsonArray{QJsonObject{
                                 {"text", "原词 AB"}, {"start_ms", 0}, {"duration_ms", 0}}});
        QVERIFY(model.replace({{"lines", QJsonArray{line}}}));
        QVERIFY(!model.lineAt(0).value("wordTimed").toBool());
    }
    void climaxDomainDurationAndSessionIsolation()
    {
        ApiClient api(DeviceIdentity::temporary());
        PlaybackClimaxModel model(api);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QJsonArray domain{QJsonObject{{"start_ms", 81000}, {"end_ms", 117100}},
                                QJsonObject{{"start_ms", 81400}, {"end_ms", 118000}},
                                QJsonObject{{"start_ms", 81800}, {"end_ms", 119000}}};
        const auto old = PlaybackClimaxModelTestAccess::begin(model, api, 240000);
        const auto current = PlaybackClimaxModelTestAccess::begin(model, api, 0);
        PlaybackClimaxModelTestAccess::finish(model, old, domain);
        QCOMPARE(model.rowCount(), 0);
        PlaybackClimaxModelTestAccess::finish(model, current, domain);
        QCOMPARE(model.rowCount(), 0); // Keep metadata while native duration is unknown.
        model.setDuration(240000);
        QCOMPARE(model.rowCount(), 1); // Source compares adjacent sorted entries.
        QCOMPARE(model.data(model.index(0), PlaybackClimaxModel::Start).toDouble(),
                 81000. / 240000);
        QCOMPARE(model.data(model.index(0), PlaybackClimaxModel::End).toDouble(), 117100. / 240000);
        model.setDuration(100000);
        // Normalized spacing grows when duration shrinks; all three now exceed 0.2%.
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.data(model.index(0), PlaybackClimaxModel::End).toDouble(), 1.);
        model.setDuration(81000);
        QCOMPARE(model.rowCount(), 0);
        model.setDuration(240000);
        QCOMPARE(model.rowCount(), 1);
        const auto retired = PlaybackClimaxModelTestAccess::begin(model, api, 240000);
        QVERIFY(api.setDeviceIdentity(DeviceIdentity::temporary()));
        PlaybackClimaxModelTestAccess::finish(model, retired, domain);
        QCOMPARE(model.rowCount(), 0);
        PlaybackTrack local;
        local.localPath = "/test/local-without-hash.wav";
        model.load(local, 240000);
        QCOMPARE(model.rowCount(), 0);
        QSignalSpy closed(&api, &ApiClient::closed);
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 10000);
    }

    void climaxPartialRecordsDoNotRejectRemainingMarkersOrLargeArrays()
    {
        ApiClient api(DeviceIdentity::temporary());
        PlaybackClimaxModel model(api);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        QJsonArray domain{QJsonObject{{"end_ms", 500}}, QJsonObject{{"hash", "known-only"}}};
        for (int i = 1; i <= 300; ++i)
            domain.append(QJsonObject{{"start_ms", i * 3000}, {"end_ms", QJsonValue::Null}});
        const auto id = PlaybackClimaxModelTestAccess::begin(model, api, 1000000);
        PlaybackClimaxModelTestAccess::finish(model, id, domain);
        QCOMPARE(model.rowCount(), 300);
        QCOMPARE(model.data(model.index(0), PlaybackClimaxModel::Start).toDouble(), 0.003);
        QCOMPARE(model.data(model.index(299), PlaybackClimaxModel::End).toDouble(), 0.9);
        QSignalSpy closed(&api, &ApiClient::closed);
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 10000);
    }

    void progressBusyRejectsStaleDiagnosticStreams()
    {
        PlaybackProgressState state;
        const auto event = [](const char *kind, int id, int generation, QJsonObject fields = {}) {
            fields.insert("event", kind);
            fields.insert("event_id", id);
            fields.insert("track_seq", 7);
            fields.insert("generation", generation);
            return fields;
        };
        const auto core = [&](int id, int generation, const char *value) {
            state.observe(event("core-state-change", id, generation, {{"core_state", value}}));
        };
        const auto ao = [&](int id, int generation, bool paused, QJsonValue percent) {
            state.observe(event("ao-state-change", id, generation,
                                {{"ao_paused", paused}, {"ao_buffering_state", percent}}));
        };
        core(10, 1, "buffering");
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Ready);
        ao(11, 1, true, 30);
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Buffering);
        QCOMPARE(state.reason(true, false), PlaybackProgressState::Ready);
        QCOMPARE(state.reason(false, true), PlaybackProgressState::Ready);
        ao(12, 1, false, 30);
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Ready);
        ao(13, 1, true, 100);
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Ready);
        ao(14, 1, true, QJsonValue());
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Ready);
        // Coalescing may deliver this newer output tick BEFORE both snapshots.
        state.observe(event("time-update", 30, 1, {{"time", 1.0}}));
        core(28, 1, "buffering");
        ao(29, 1, true, 20);
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Ready);
        ao(31, 1, true, 20);
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Buffering);
        // A context-free confirmation does not stand for actual output progress.
        state.observe({{"event", "time-update"}, {"time", 9.}});
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Buffering);
        state.observe(event("seek", 32, 2, {{"time", 3.}}));
        QCOMPARE(state.reason(true, false), PlaybackProgressState::Seeking);
        state.observe(event("seeked", 33, 1, {{"time", 3.}}));
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Seeking);
        core(34, 2, "seeking");
        state.observe(event("seeked", 35, 2, {{"time", 3.}}));
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Ready);
        // Both snapshots must belong to the latest generation.
        core(36, 3, "buffering");
        ao(37, 2, true, 30);
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Ready);
        ao(38, 3, true, 30);
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Buffering);
        core(39, 3, "error");
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Ready);
        state.observe(event("seek", 40, 4, {{"time", 7.}}));
        // Decoder reopen can publish a newer AO context before seeked arrives.
        ao(41, 5, true, 0);
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Seeking);
        state.observe(event("seeked", 42, 4, {{"time", 7.}}));
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Seeking);
        state.observe(event("seeked", 43, 5, {{"time", 7.}}));
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Ready);
        state.reset();
        QCOMPARE(state.reason(true, true), PlaybackProgressState::Ready);
    }

    void volumeDefaultsPreserveSavedLevelsAndMuteRecovery_data()
    {
        QTest::addColumn<bool>("saved");
        QTest::addColumn<double>("initial");
        QTest::addColumn<double>("restored");
        QTest::newRow("fresh") << false << 50. << 50.;
        QTest::newRow("saved-muted") << true << 0. << 50.;
        QTest::newRow("saved-custom") << true << 37.5 << 37.5;
        QTest::newRow("saved-full") << true << 100. << 100.;
    }
    void volumeDefaultsPreserveSavedLevelsAndMuteRecovery()
    {
        QFETCH(bool, saved);
        QFETCH(double, initial);
        QFETCH(double, restored);
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("volume.ini"));
        if (saved)
            settings.setValue("playback/volume", initial);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, settings);
        QCOMPARE(playback.volume(), initial);
        QCOMPARE(settings.contains("playback/volume"), saved);
        if (initial > 0) {
            playback.toggleMute();
            QCOMPARE(playback.volume(), 0.);
        }
        playback.toggleMute();
        QCOMPARE(playback.volume(), restored);
        QCOMPARE(settings.value("playback/volume").toDouble(), restored);
        QSignalSpy closed(&playback, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        playback.shutdown();
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
    }
    void sharedMembershipCacheDeduplicatesAndRejectsOldAccountFacts()
    {
        ApiClient api;
        MembershipService membership(api);
        QSignalSpy settled(&membership, &MembershipService::settled);
        membership.ensure();
        QVERIFY(membership.facts().known);
        QVERIFY(!membership.facts().superVip);
        QCOMPARE(settled.size(), 1);
        QVERIFY(api.setAccount(900001, "domain-only-test"));
        QVERIFY(!membership.facts().known);
        const auto request = MembershipServiceTestAccess::begin(membership, api);
        QVERIFY(request);
        membership.ensure();
        QVERIFY(membership.loading());
        QCOMPARE(settled.size(), 1);
        const QJsonObject conceptMembership{
            {"user_id", 900001},
            {"user_type_code", 0},
            {"vip_type_code", 0},
            {"products", QJsonArray{QJsonObject{{"product", "svip"}, {"active", true}}}}};
        MembershipServiceTestAccess::finish(membership, request, conceptMembership);
        QVERIFY(membership.facts().known);
        QVERIFY(membership.facts().conceptVip);
        QVERIFY(!membership.facts().superVip);
        membership.ensure();
        QVERIFY(!membership.loading());
        QCOMPARE(settled.size(), 3);
        const auto older = MembershipServiceTestAccess::begin(membership, api);
        QVERIFY(api.setAccount(900002, "domain-only-next-account"));
        QVERIFY(!membership.facts().known);
        MembershipServiceTestAccess::finish(membership, older, conceptMembership);
        QVERIFY(!membership.facts().known);
        QVERIFY(membership.domain().isEmpty());
        const auto wrongUser = MembershipServiceTestAccess::begin(membership, api);
        MembershipServiceTestAccess::finish(membership, wrongUser, conceptMembership);
        QVERIFY(!membership.facts().known);
        QVERIFY(!membership.loading());
        QVERIFY(!membership.errorMessage().isEmpty());
        const auto valid = MembershipServiceTestAccess::begin(membership, api);
        MembershipServiceTestAccess::finish(membership, valid,
                                            {{"user_id", 900002},
                                             {"user_type_code", 16},
                                             {"vip_type_code", 0},
                                             {"products", QJsonArray{}}});
        QVERIFY(membership.facts().superVip);
        QVERIFY(membership.facts().conceptVip);
        QVERIFY(api.setAccount(0, {}));
        QVERIFY(!membership.facts().known);
        membership.ensure();
        QVERIFY(membership.facts().known);
        QVERIFY(!membership.facts().conceptVip);
        QSignalSpy closed(&api, &ApiClient::closed);
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    }
    void resolvedQualityPresentationAndMembershipChangesInvalidatePreparedSources()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("preferences.ini"));
        ApiClient api;
        QVERIFY(api.setAccount(900001, "domain-only-test"));
        PlaybackService playback(api, settings);
        playback.setQuality("viper_tape");
        QCOMPARE(playback.effectiveQuality(), QString("viper_tape"));
        PlaybackServiceTestAccess::resolvedQuality(playback, "hi_res");
        QCOMPARE(playback.effectiveQuality(), QString("hi_res"));
        QCOMPARE(playback.quality(), QString("viper_tape"));
        QCOMPARE(playback.preferences()->defaultQuality(), QString("hi_res"));
        auto &membership = *playback.memberships();
        auto &transition = *playback.transition();
        const QJsonObject superVip{{"user_id", 900001},
                                   {"user_type_code", 16},
                                   {"vip_type_code", 0},
                                   {"products", QJsonArray{}}};
        QSignalSpy factsChanged(&membership, &MembershipService::factsChanged);
        const auto initial = MembershipServiceTestAccess::begin(membership, api);
        const auto before = TrackTransitionControllerTestAccess::revision(transition);
        MembershipServiceTestAccess::finish(membership, initial, superVip);
        // Initial lookup must not cancel the source which is waiting for it.
        QCOMPARE(TrackTransitionControllerTestAccess::revision(transition), before);
        QCOMPARE(factsChanged.size(), 1);
        TrackTransitionControllerTestAccess::seedResolved(transition);
        const auto unchanged = MembershipServiceTestAccess::begin(membership, api);
        MembershipServiceTestAccess::finish(membership, unchanged, superVip);
        QVERIFY(TrackTransitionControllerTestAccess::prepared(transition));
        QCOMPARE(factsChanged.size(), 1);
        auto nonVip = superVip;
        nonVip["user_type_code"] = 0;
        const auto changed = MembershipServiceTestAccess::begin(membership, api);
        MembershipServiceTestAccess::finish(membership, changed, nonVip);
        QVERIFY(!TrackTransitionControllerTestAccess::prepared(transition));
        QVERIFY(TrackTransitionControllerTestAccess::revision(transition) > before);
        QCOMPARE(factsChanged.size(), 2);
        QSignalSpy closed(&playback, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        playback.shutdown();
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
    }
    void catalogCompatibilityCandidatesUseResourceHashesAndReferenceOrder()
    {
        using Quality = ApiClient::PlaybackQuality;
        using Effect = ApiClient::SoundEffect;
        // Domain-only inputs test selection; upstream decoding is covered independently.
        const QJsonObject privileges{
            {"variants", QJsonArray{QJsonObject{{"quality", "128"}, {"hash", "sd"}},
                                    QJsonObject{{"quality", "320"}, {"hash", "hq"}},
                                    QJsonObject{{"quality", "flac"}, {"hash", "sq"}},
                                    QJsonObject{{"quality", "high"}, {"hash", "hr"}},
                                    QJsonObject{{"quality", "viper_clear"}, {"hash", "clear"}},
                                    QJsonObject{{"quality", "viper_tape"}, {"hash", "tape"}},
                                    QJsonObject{{"quality", "viper_atmos"}, {"hash", "atmos"}},
                                    QJsonObject{{"quality", "acappella"}, {"hash", "vocal"}}}}};
        const auto plan =
            PlaybackSourcePlan::catalog("original", privileges, Quality::ViperTape, Effect::Vocal,
                                        true, true, {true, true, true});
        QCOMPARE(plan.size(), 10); // Two effects, six qualities, two original-hash fallbacks.
        QCOMPARE(plan[0].hash, QString("vocal"));
        QCOMPARE(plan[1].hash, QString("original"));
        QCOMPARE(plan[0].effect, Effect::Vocal);
        const QStringList hashes{"tape", "clear", "hr", "sq", "hq", "sd"};
        for (int i = 0; i < hashes.size(); ++i) {
            QCOMPARE(plan[i + 2].hash, hashes[i]);
            QCOMPARE(plan[i + 2].effect, Effect::None);
        }
        QCOMPARE(plan[8].quality, Quality::Automatic);
        QVERIFY(plan[8].originPage.isEmpty());
        QCOMPARE(plan[9].originPage, QString("356753938"));
        QVERIFY(plan[9].resolvedQuality.isEmpty());
        const auto strict = PlaybackSourcePlan::catalog("original", privileges, Quality::Flac,
                                                        Effect::None, false, true);
        QCOMPARE(strict.size(), 2);
        QCOMPARE(strict[0].hash, QString("sq"));
        QCOMPARE(strict[1].quality, Quality::Automatic);
        QCOMPARE(strict[1].originPage, QString("356753938"));
        const auto noViper = PlaybackSourcePlan::catalog(
            "original", privileges, Quality::ViperAtmos, Effect::None, true, false);
        QCOMPARE(noViper.size(), 6);
        QCOMPARE(noViper[0].quality, Quality::HiRes);
        auto incomplete = privileges;
        incomplete["variants"] = QJsonArray{QJsonObject{{"quality", "flac"}, {"hash", "sq"}}};
        const auto missing = PlaybackSourcePlan::catalog("original", incomplete, Quality::HiRes,
                                                         Effect::None, true, true);
        QCOMPARE(missing.size(), 3);
        QCOMPARE(missing[0].quality, Quality::Flac);
        QVERIFY(missing[1].resolvedQuality.isEmpty());
        const auto unknown =
            PlaybackSourcePlan::catalog("original", {}, Quality::HiRes, Effect::None, false, true);
        QCOMPARE(unknown.size(), 1);
        QVERIFY(unknown[0].resolvedQuality.isEmpty());
        const auto denied =
            PlaybackSourcePlan::catalog("original", privileges, Quality::ViperTape, Effect::None,
                                        true, true, {true, true, false});
        QCOMPARE(denied.size(), 6);
        QCOMPARE(denied[0].quality, Quality::HiRes);
        QVERIFY(PlaybackSourcePlan::catalog("original", privileges, Quality::ViperTape,
                                            Effect::None, false, true, {true, true, false})
                    .isEmpty());
        QCOMPARE(PlaybackSourcePlan::qualityAccessReason(Quality::ViperTape, {}, true),
                 QString("会员权益尚未确认"));
        QCOMPARE(
            PlaybackSourcePlan::qualityAccessReason(Quality::ViperTape, {true, true, false}, true),
            QString("需超级 VIP"));
        QVERIFY(
            PlaybackSourcePlan::catalog({}, privileges, Quality::HiRes, Effect::None, true, true)
                .isEmpty());
    }
    void playbackPreferencesSeparateSelectionAndDefaultQuality()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("preferences.ini"));
        ApiClient api;
        PlaybackService playback(api, settings);
        auto *preferences = playback.preferences();
        QCOMPARE(preferences->defaultQuality(), QString("hi_res"));
        QCOMPARE(playback.quality(), QString("hi_res"));
        QVERIFY(preferences->volumeFade());
        QVERIFY(preferences->compatibilityMode());
        preferences->setCompatibilityMode(false);
        preferences->setQueueMode("single");
        preferences->setDefaultQuality("flac");
        playback.playFromSelection({track(0), track(1), track(2)}, 1, "Selection", "selection",
                                   true);
        QCOMPARE(playback.queue()->rowCount(), 1);
        QCOMPARE(playback.currentTrack().key(), track(1).key());
        QCOMPARE(playback.quality(), QString("flac"));
        playback.setQuality("higher");
        QCOMPARE(preferences->defaultQuality(), QString("flac"));
        preferences->setDefaultQuality("hi_res");
        QCOMPARE(playback.quality(), QString("higher"));
        preferences->setDefaultQuality("flac");
        playback.playFromSelection({track(0), track(1), track(2)}, 0, "All", "all", false);
        QCOMPARE(playback.queue()->rowCount(), 3);
        QCOMPARE(playback.quality(), QString("flac"));
        preferences->setViperQualityEnabled(true);
        preferences->setDefaultQuality("viper_tape");
        QCOMPARE(preferences->defaultQuality(), QString("viper_tape"));
        QCOMPARE(playback.quality(), QString("viper_tape"));
        preferences->setViperQualityEnabled(false);
        QCOMPARE(preferences->defaultQuality(), QString("hi_res"));
        preferences->setDefaultQuality("viper_atmos");
        QCOMPARE(preferences->defaultQuality(), QString("hi_res"));
        preferences->setVolumeFadeMs(100);
        QCOMPARE(preferences->volumeFadeMs(), 500);
        QSignalSpy closed(&playback, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        playback.shutdown();
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
        PlaybackPreferences reopened(settings);
        QCOMPARE(reopened.queueMode(), QString("single"));
        QCOMPARE(reopened.defaultQuality(), QString("hi_res"));
        QVERIFY(!reopened.viperQualityEnabled());
        QVERIFY(!reopened.compatibilityMode());
    }
    void audibleTransitionNoticeUsesActualModeAndRoundedTimes()
    {
        const auto format = [](const QString &mode, double overlap, double start) {
            return TrackTransitionControllerTestAccess::formatNotice(
                {{"transition", QJsonObject{{"mode", mode}, {"overlap_secs", overlap}}},
                 {"time", start}});
        };
        QCOMPARE(format("automix-pro", 4.26, 12.04), QString("节奏融合 · 4.3 秒 · 跳过 12 秒"));
        QCOMPARE(format("automix-basic", 6, 0), QString("自然衔接 · 6 秒"));
        QCOMPARE(format("fade", 0.01, 0.01), QString("淡入淡出"));
        QCOMPARE(format("gapless", 7, 2.26), QString("无缝 · 跳过 2.3 秒"));
        QCOMPARE(format("fade", -1, -3), QString("淡入淡出"));
        QVERIFY(format("none", 4, 5).isEmpty());
        QVERIFY(format("unknown", 4, 5).isEmpty());
        QVERIFY(TrackTransitionControllerTestAccess::formatNotice({}).isEmpty());
    }
    void nativePreparedBoundaryRetainsQueueUntilCommit()
    {
        // Real local readers/output, with no simulated protocol response. The
        // generated tone is muted; this covers handoff identity, not listening.
        QTemporaryDir dir;
        PlaybackTracks tracks;
        for (int j = 0; j < 2; ++j) {
            const auto path = dir.filePath(QString("tone%1.wav").arg(j));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QDataStream stream(&file);
            stream.setByteOrder(QDataStream::LittleEndian);
            constexpr quint32 bytes = 48000 * 10 * 2 * 2;
            stream.writeRawData("RIFF", 4);
            stream << quint32(36 + bytes);
            stream.writeRawData("WAVEfmt ", 8);
            stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000) << quint32(192000)
                   << quint16(4) << quint16(16);
            stream.writeRawData("data", 4);
            stream << bytes;
            for (int i = 0; i < 48000 * 10; ++i) {
                const auto sample = qint16(
                    std::sin(double(i) * 2. * 3.141592653589793 * (220 + j * 110) / 48000) * 1000);
                stream << sample << sample;
            }
            file.close();
            auto t = track(j);
            t.localPath = path;
            t.durationMs = 10000;
            tracks.append(t);
        }
        EchoMusic::SettingsStore settings(dir.filePath("settings.ini"));
        ApiClient api;
        PlaybackService playback(api, settings);
        QSignalSpy closed(&playback, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        playback.setVolume(0);
        QSignalSpy notices(playback.transition(), &TrackTransitionController::notice);
        playback.transition()->setMode("gapless");
        QCOMPARE(notices.size(), 0); // Settings and preparation are not audible boundaries.
        playback.playTracks(tracks, 0, "Transition", "transition-test");
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(
            TrackTransitionControllerTestAccess::prepared(*playback.transition()), 15000);
        QCOMPARE(playback.queue()->currentIndex(), 0);
        QCOMPARE(playback.currentTrack().key(), tracks[0].key());
        QVERIFY(playback.positionMs() > 0);
        // EchoMusic manual clicks synchronously select a new track; only a
        // natural boundary waits for the already prepared native deck.
        QTRY_COMPARE_WITH_TIMEOUT(playback.currentTrack().key(), tracks[1].key(), 15000);
        QCOMPARE(playback.queue()->currentIndex(), 1);
        QTRY_COMPARE(notices.size(), 1);
        QCOMPARE(notices.first().first().toString(), QString("无缝"));
        QVERIFY2(playback.errorMessage().isEmpty(), qPrintable(playback.errorMessage()));
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing(), 5000);
        playback.next();
        QCOMPARE(playback.currentTrack().key(), tracks[0].key());
        QCOMPARE(playback.queue()->currentIndex(), 0);
        QCOMPARE(notices.size(),
                 1); // Manual intent selects immediately, without another adoption notice.
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && !playback.loading(), 10000);
        QList<PlaybackService::ProgressBusyReason> progress;
        connect(&playback, &PlaybackService::progressBusyReasonChanged, &playback,
                [&] { progress.append(playback.progressBusyReason()); });
        playback.seek(2000);
        QTRY_VERIFY_WITH_TIMEOUT(progress.contains(PlaybackService::ProgressSeeking), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(playback.progressBusyReason(), PlaybackService::ProgressReady,
                                  5000);
        QVERIFY(progress.contains(PlaybackService::ProgressReady));
        const auto retired = TrackTransitionControllerTestAccess::revision(*playback.transition());
        playback.transition()->handleEvent({{"event", "next-source-prepared"},
                                            {"preparation_revision", qint64(retired - 1)},
                                            {"prepared", true}});
        QVERIFY(!TrackTransitionControllerTestAccess::prepared(*playback.transition()));
        QCOMPARE(notices.size(), 1); // A retired preparation cannot re-announce a transition.
        // Close while the real decoder is seeking: QML must receive Ready
        // immediately, even though shutdown no longer emits general changed().
        connect(&playback, &PlaybackService::progressBusyReasonChanged, &playback, [&] {
            if (playback.progressBusyReason() == PlaybackService::ProgressSeeking)
                playback.shutdown();
        });
        playback.seek(3000);
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        QCOMPARE(playback.progressBusyReason(), PlaybackService::ProgressReady);
        QCOMPARE(progress.last(), PlaybackService::ProgressReady);
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
    }
    void localEffectBatchImportsCopyRenameAndPreserveDisabledState()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("audio.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, settings);
        auto *controls = playback.audioControls();
        QList<QUrl> urls;
        for (int index = 0; index < 2; ++index) {
            const auto path = directory.filePath(QString("本地 %1.wav").arg(index));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QDataStream stream(&file);
            stream.setByteOrder(QDataStream::LittleEndian);
            stream.writeRawData("RIFF", 4);
            stream << quint32(36 + 128);
            stream.writeRawData("WAVEfmt ", 8);
            stream << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000)
                   << quint16(2) << quint16(16);
            stream.writeRawData("data", 4);
            stream << quint32(128);
            for (int sample = 0; sample < 64; ++sample)
                stream << qint16(sample == index ? 12000 : 0);
            file.close();
            urls.append(QUrl::fromLocalFile(path));
        }
        const auto invalid = directory.filePath("invalid.wav");
        QFile bad(invalid);
        QVERIFY(bad.open(QIODevice::WriteOnly));
        QVERIFY(bad.write("invalid audio") > 0);
        bad.close();
        urls.append(QUrl::fromLocalFile(invalid));
        QSignalSpy imported(controls, &PlaybackAudioControls::impulseFilesImported);
        QSignalSpy notices(controls, &PlaybackAudioControls::fileNotice);
        controls->importImpulseResponseFiles(urls);
        QVERIFY(controls->importingImpulseResponses());
        controls->importImpulseResponseFiles(urls); // Concurrent clicks do not duplicate the batch.
        QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 1, 15000);
        QVERIFY(!controls->importingImpulseResponses());
        QCOMPARE(controls->impulseResponses().size(), 2);
        QVERIFY(!controls->impulseEnabled());
        QCOMPARE(controls->impulseResponse(),
                 controls->impulseResponses().first().toMap().value("path").toString());
        QVERIFY(notices.last().at(1).toBool());
        for (int index = 0; index < 2; ++index) {
            const auto entry = controls->impulseResponses().at(index).toMap();
            const auto path = entry.value("path").toString();
            QVERIFY(path != urls.at(index).toLocalFile());
            QVERIFY(path.startsWith(controls->effectFilesRoot() + "/local/"));
            QFile source(urls.at(index).toLocalFile()), copy(path);
            QVERIFY(source.open(QIODevice::ReadOnly) && copy.open(QIODevice::ReadOnly));
            QCOMPARE(source.readAll(), copy.readAll());
            QCOMPARE(entry.value("source").toString(), QString("local"));
            QCOMPARE(entry.value("name").toString(), QString("本地 %1").arg(index));
        }
        const auto path = controls->impulseResponses().first().toMap().value("path").toString();
        controls->renameImpulseResponse(path, QStringLiteral(" 新音效.wav "));
        QCOMPARE(controls->impulseResponses().first().toMap().value("name").toString(),
                 QStringLiteral("新音效"));
        controls->renameImpulseResponse(path, "  ");
        QCOMPARE(controls->impulseResponses().first().toMap().value("name").toString(),
                 QStringLiteral("新音效"));
        const auto second = controls->impulseResponses().last().toMap().value("path").toString();
        controls->renameImpulseResponse(second, QStringLiteral("新音效.wav"));
        QCOMPARE(controls->impulseResponses().last().toMap().value("name").toString(),
                 QStringLiteral("新音效 2"));
        controls->renameImpulseResponse(path, QStringLiteral("新音效"));
        QCOMPARE(controls->impulseResponses().first().toMap().value("name").toString(),
                 QStringLiteral("新音效"));
        controls->removeImpulseResponse(path);
        QCOMPARE(controls->impulseResponses().size(), 1);
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(controls->impulseResponse().isEmpty());
        QVERIFY(!controls->impulseEnabled());
        QVERIFY(QFileInfo::exists(urls.first().toLocalFile()));
        controls->importImpulseResponseFiles({QUrl::fromLocalFile(invalid)});
        QTRY_VERIFY_WITH_TIMEOUT(!controls->importingImpulseResponses(), 15000);
        QCOMPARE(controls->impulseResponses().size(), 1);
        QVERIFY(!notices.last().at(1).toBool());
        // Drain an in-flight probe/copy without publishing further catalog state.
        controls->importImpulseResponseFiles(urls);
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    }
    void outputChoicesExcludeNativeSelectorsAndKeepDefaultHardware()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("audio.ini"));
        settings.setValue("audio/outputDevice", "auto");
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, settings);
        auto *controls = playback.audioControls();
        QCOMPARE(controls->outputDevice(), QString());
        const QJsonArray devices{
            QJsonObject{{"name", "auto"}, {"description", "Autoselect device"}},
            QJsonObject{{"name", "default"}, {"description", "Default"}},
            QJsonObject{{"name", "null"}, {"description", "Null output"}},
            QJsonObject{{"name", ""}, {"description", "Empty selector"}},
            QJsonObject{{"name", "speaker"}, {"description", "扬声器"}, {"is_default", true}},
            QJsonObject{{"name", "speaker-alias"}, {"description", "扬声器"}},
            QJsonObject{{"name", "USB DAC"}, {"description", ""}}};
        controls->handleEvent({{"event", "audio-device-list-changed"}, {"devices", devices}});
        QCOMPARE(controls->devices().size(), 2);
        QCOMPARE(controls->devices().at(0).toMap().value("name").toString(), QString("speaker"));
        QVERIFY(controls->devices().at(0).toMap().value("is_default").toBool());
        QCOMPARE(controls->devices().at(1).toMap().value("name").toString(), QString("USB DAC"));
        controls->setOutputDevice("USB DAC");
        controls->setOutputDevice("default");
        QCOMPARE(controls->outputDevice(), QString());
        controls->setOutputDevice("speaker");
        QSignalSpy disconnected(controls, &PlaybackAudioControls::selectedOutputDisconnected);
        controls->handleEvent({{"event", "audio-device-list-changed"},
                               {"devices", QJsonArray{devices.at(0)}},
                               {"disconnected_devices", QJsonArray{devices.at(4)}}});
        QVERIFY(controls->devices().isEmpty());
        QCOMPARE(controls->outputDevice(), QString("speaker"));
        QCOMPARE(disconnected.size(), 1);
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    }
    void loudnessCeilingAndOutputAcknowledgementsPreserveAppliedState()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("audio.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, settings);
        auto *controls = playback.audioControls();
        QVERIFY(!controls->volumeNormalization());
        controls->setVolumeNormalization(true);
        QVERIFY(controls->volumeNormalization());
        QCOMPARE(controls->referenceLufs(), -14.);
        controls->setTrackLoudness({{"loudness", -30.}, {"peak", -1.}, {"gain", 12.}});
        QCOMPARE(controls->normalizationGainDb(),
                 .5); // Peak, not upstream gain, limits amplification.
        QCOMPARE(playback.volume(), 50.);
        controls->setVolumeNormalization(false);
        QCOMPARE(controls->normalizationGainDb(), 0.);
        controls->setVolumeNormalization(true);
        controls->setTrackLoudness({{"loudness", -10.}, {"peak", -2.}, {"gain", 0.}});
        QCOMPARE(controls->normalizationGainDb(), -4.);
        controls->setTrackLoudness({{"loudness", 0.}, {"peak", 0.}, {"gain", 0.}});
        QCOMPARE(controls->normalizationGainDb(), 0.);
        controls->setOutputDevice("output-a");
        PlaybackAudioControlsTestAccess::pendingOutput(*controls, 11);
        controls->handleEvent({{"event", "output-applied"},
                               {"output_revision", 11},
                               {"output_device", "output-a"},
                               {"exclusive_output", false}});
        QCOMPARE(controls->appliedOutputDevice(), QString("output-a"));
        controls->setOutputDevice("output-b");
        PlaybackAudioControlsTestAccess::pendingOutput(*controls, 12);
        controls->handleEvent(
            {{"event", "control-error"}, {"output_revision", 11}, {"message", "stale"}});
        QCOMPARE(controls->outputDevice(), QString("output-b"));
        controls->handleEvent(
            {{"event", "control-error"}, {"output_revision", 12}, {"message", "unavailable"}});
        QCOMPARE(controls->outputDevice(), QString("output-a"));
        QCOMPARE(settings.value("audio/outputDevice").toString(), QString("output-a"));
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    }
    void downloadedAndEmbeddedLyricsUseCppParserAndCancel()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("lyrics.ini"));
        ApiClient api(DeviceIdentity::temporary());
        LyricSourceController sources(api, settings);
        auto current = track(1);
        LyricSourceControllerTestAccess::search(sources, api, current);
        LyricSourceControllerTestAccess::seedCandidates(sources);
        LyricSourceControllerTestAccess::seedPreview(sources, 999999, "a");
        const auto document =
            QJsonDocument(
                QJsonObject{{"format", "lrc"},
                            {"encoding", "base64"},
                            {"content",
                             QString::fromLatin1(
                                 QByteArray("[00:01][00:03]Repeated\n[00:02]Verse").toBase64())}})
                .toJson();
        LyricSourceControllerTestAccess::succeed(sources, 999999, document);
        QVERIFY(sources.previewLoading());
        QTRY_VERIFY(!sources.previewLoading());
        QCOMPARE(sources.preview()->rowCount(), 3);
        LyricSourceControllerTestAccess::seedPreview(sources, 999998, "a");
        LyricSourceControllerTestAccess::succeed(sources, 999998, document);
        sources.closeSelection();
        QTest::qWait(30);
        QCOMPARE(sources.preview()->rowCount(), 0);
        sources.updateEmbeddedDocument(current.stableKey(), "Embedded", "Artist",
                                       "[00:01]Hello\n[00:01]你好\n[00:03]Goodbye\n[00:03]再见");
        QTRY_VERIFY(sources.rowCount() > 2);
        sources.updateEmbeddedDocument(current.stableKey(), "Embedded", "Artist", "[00:01]Stale");
        sources.clear();
        QTest::qWait(30);
        QCOMPARE(sources.rowCount(), 0);
        QCOMPARE(sources.preview()->rowCount(), 0);
    }
    void lyricCatalogOrderPreviewCloseAndFreshAutoSelection()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("sources.ini"));
        ApiClient api(DeviceIdentity::temporary());
        LyricSourceController sources(api, settings);
        QAbstractItemModelTester modelTester(
            &sources, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy activated(&sources, &LyricSourceController::activated);
        QSignalSpy notices(&sources, &LyricSourceController::notice);
        // Domain presentation rows, not fabricated upstream protocol samples.
        const auto candidate = [](const QString &id) {
            return QJsonObject{{"source_id", id},
                               {"access_key", "domain-only-key"},
                               {"title", id},
                               {"artist", "Artist"},
                               {"nickname", "Uploader"},
                               {"source_label", "ugc"},
                               {"duration_ms", 4000},
                               {"score", 80},
                               {"word_timed", true},
                               {"scrollable", true}};
        };
        const auto bytes = [](const QJsonArray &rows) { return QJsonDocument(rows).toJson(); };
        const QByteArray lyric =
            R"({"offset_ms":0,"scrollable":true,"lines":[{"text":"Preview","start_ms":0,"duration_ms":1000,"words":[]}]})";
        const auto complete = [&](quint64 id, const QByteArray &payload) {
            QVERIFY(id);
            api.cancel(id);
            LyricSourceControllerTestAccess::succeed(sources, id, payload);
        };
        auto lyricTrack = track(1);
        lyricTrack.hash = QString(32, QLatin1Char('1'));
        sources.load(lyricTrack);
        complete(LyricSourceControllerTestAccess::searchRequest(sources), bytes({candidate("a")}));
        completeLyricPreview(sources, api, lyric);
        QCOMPARE(activated.size(), 1);
        QCOMPARE(sources.currentId(), QString("a"));
        sources.openSelection();
        complete(LyricSourceControllerTestAccess::searchRequest(sources),
                 bytes({candidate("a"), candidate("b")}));
        QVERIFY(sources.loading());
        QCOMPARE(LyricSourceControllerTestAccess::downloadRequest(sources), quint64(0));
        complete(LyricSourceControllerTestAccess::defaultRequest(sources), bytes({candidate("b")}));
        QCOMPARE(sources.index(0).data(Qt::UserRole + 1).toString(), QString("b"));
        QCOMPARE(sources.selectedIndex(), 1);
        completeLyricPreview(sources, api, lyric);
        QCOMPARE(activated.size(), 1);
        sources.select(0);
        completeLyricPreview(sources, api, lyric);
        QCOMPARE(activated.size(), 1);
        sources.applySelected();
        QCOMPARE(sources.currentId(), QString("b"));
        QVERIFY(sources.manual());
        QCOMPARE(notices.size(), 1);
        sources.closeSelection();
        QCOMPARE(sources.preview()->rowCount(), 0);
        sources.openSelection();
        QCOMPARE(sources.selectedIndex(), 0);
        QCOMPARE(sources.preview()->rowCount(), 1);
        sources.refresh();
        complete(LyricSourceControllerTestAccess::searchRequest(sources),
                 bytes({candidate("a"), candidate("b")}));
        complete(LyricSourceControllerTestAccess::defaultRequest(sources), bytes({candidate("b")}));
        const auto abandoned = LyricSourceControllerTestAccess::downloadRequest(sources);
        QVERIFY(abandoned);
        sources.closeSelection();
        complete(abandoned, lyric);
        QCOMPARE(sources.preview()->rowCount(), 0);
        QCOMPARE(activated.size(), 2);
        sources.openSelection();
        sources.refresh();
        complete(LyricSourceControllerTestAccess::searchRequest(sources),
                 bytes({candidate("a"), candidate("b")}));
        const auto defaultRequest = LyricSourceControllerTestAccess::defaultRequest(sources);
        api.cancel(defaultRequest);
        LyricSourceControllerTestAccess::fail(sources, defaultRequest);
        QVERIFY(!sources.loading());
        QCOMPARE(sources.currentId(), QString("b"));
        QVERIFY(!sources.errorMessage().isEmpty());
        sources.restoreAuto();
        complete(LyricSourceControllerTestAccess::searchRequest(sources), bytes({candidate("a")}));
        completeLyricPreview(sources, api, lyric);
        QCOMPARE(sources.currentId(), QString("a"));
        QVERIFY(!sources.manual());
        sources.load(track(2));
        complete(abandoned, lyric);
        QCOMPARE(sources.preview()->rowCount(), 0);
        sources.clear();
    }
    void lyricKeywordSearchKeepsActivationIdentityAndRestoresFreshKeys()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("keywords.ini"));
        ApiClient api(DeviceIdentity::temporary());
        LyricSourceController sources(api, settings);
        QAbstractItemModelTester modelTester(
            &sources, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy activated(&sources, &LyricSourceController::activated);
        const auto candidate = [](const QString &id) {
            return QJsonObject{{"source_id", id},       {"access_key", "ephemeral-domain-key"},
                               {"title", id},           {"artist", "Artist"},
                               {"source_label", "ugc"}, {"duration_ms", 4000},
                               {"score", 80},           {"word_timed", true},
                               {"scrollable", true}};
        };
        const QByteArray lyric =
            R"({"offset_ms":0,"scrollable":true,"lines":[{"text":"Preview","start_ms":0,"duration_ms":1000,"words":[]}]})";
        const auto complete = [&](quint64 id, const QByteArray &payload) {
            QVERIFY(id);
            api.cancel(id);
            LyricSourceControllerTestAccess::succeed(sources, id, payload);
        };
        const auto rows = [&](quint64 id, const QStringList &ids) {
            QJsonArray values;
            for (const auto &value : ids)
                values.append(candidate(value));
            complete(id, QJsonDocument(values).toJson());
        };
        auto original = track(1);
        original.hash = QString(32, '1');
        original.artist = "Original Artist";
        sources.load(original);
        QCOMPARE(sources.searchTitle(), original.title);
        QCOMPARE(sources.searchArtist(), original.artist);
        rows(LyricSourceControllerTestAccess::searchRequest(sources), {"auto"});
        completeLyricPreview(sources, api, lyric);
        sources.openSelection();
        rows(LyricSourceControllerTestAccess::searchRequest(sources), {"auto"});
        rows(LyricSourceControllerTestAccess::defaultRequest(sources), {"auto"});
        completeLyricPreview(sources, api, lyric);
        QCOMPARE(activated.size(), 1);
        sources.setSearchTitle("  晴天  ");
        sources.setSearchArtist(" 周杰伦 ");
        sources.search();
        auto request = LyricSourceControllerTestAccess::searchRequest(sources);
        QCOMPARE(ApiClientTestAccess::query(api, request),
                 (QJsonObject{{"keywords", "周杰伦 - 晴天"},
                              {"duration_ms", 4000},
                              {"all_candidates", true}}));
        QCOMPARE(sources.rowCount(), 0);
        QCOMPARE(sources.preview()->rowCount(), 0);
        rows(request, {"custom"});
        QCOMPARE(LyricSourceControllerTestAccess::defaultRequest(sources), quint64(0));
        QVERIFY(!sources.index(0).data(Qt::UserRole + 11).toBool());
        auto stalePreview = LyricSourceControllerTestAccess::downloadRequest(sources);
        sources.search();
        const auto staleSearch = LyricSourceControllerTestAccess::searchRequest(sources);
        sources.search();
        request = LyricSourceControllerTestAccess::searchRequest(sources);
        complete(stalePreview, lyric);
        rows(staleSearch, {"stale"});
        QCOMPARE(sources.rowCount(), 0);
        rows(request, {"custom"});
        completeLyricPreview(sources, api, lyric);
        QCOMPARE(sources.currentId(), QString("auto"));
        QCOMPARE(activated.size(), 1);
        QVERIFY(!sources.manual());
        sources.applySelected();
        QCOMPARE(sources.currentId(), QString("custom"));
        QCOMPARE(activated.size(), 2);
        QCOMPARE(activated.last().at(1).toString(), original.key());
        QVERIFY(sources.manual());
        for (const auto &key : settings.allKeys())
            QVERIFY(!settings.value(key).toString().contains("ephemeral-domain-key"));
        sources.closeSelection();
        sources.openSelection();
        QCOMPARE(sources.searchTitle(), original.title);
        QCOMPARE(sources.searchArtist(), original.artist);
        QCOMPARE(LyricSourceControllerTestAccess::searchRequest(sources), quint64(0));
        QCOMPARE(LyricSourceControllerTestAccess::defaultRequest(sources), quint64(0));
        QCOMPARE(sources.rowCount(), 2);
        QCOMPARE(sources.index(sources.selectedIndex()).data(Qt::UserRole + 1).toString(),
                 QString("custom"));
        if (LyricSourceControllerTestAccess::downloadRequest(sources))
            completeLyricPreview(sources, api, lyric);
        sources.applySelected(); // Re-applying the remembered row keeps its query.
        sources.closeSelection();
        sources.load(original);
        request = LyricSourceControllerTestAccess::searchRequest(sources);
        QCOMPARE(ApiClientTestAccess::query(api, request).value("keywords").toString(),
                 QString("周杰伦 - 晴天"));
        rows(request, {"custom"});
        completeLyricPreview(sources, api, lyric);
        QCOMPARE(sources.currentId(), QString("custom"));
        sources.openSelection();
        sources.restoreAuto();
        rows(LyricSourceControllerTestAccess::searchRequest(sources), {"auto"});
        completeLyricPreview(sources, api, lyric);
        QVERIFY(!sources.manual());
        QVERIFY(settings.allKeys().isEmpty());
        sources.closeSelection();
        sources.load(original);
        const auto automatic = LyricSourceControllerTestAccess::searchRequest(sources);
        sources.openSelection();
        sources.setSearchTitle(" 晴天 ");
        sources.setSearchArtist("  ");
        sources.search();
        QCOMPARE(LyricSourceControllerTestAccess::searchRequest(sources), automatic);
        rows(automatic, {"auto"});
        if (const auto download = LyricSourceControllerTestAccess::downloadRequest(sources))
            complete(download, lyric);
        QCOMPARE(sources.currentId(), QString("auto"));
        request = LyricSourceControllerTestAccess::searchRequest(sources);
        QCOMPARE(ApiClientTestAccess::query(api, request).value("keywords").toString(),
                 QString("晴天"));
        rows(request, {});
        QCOMPARE(sources.rowCount(), 0);
        sources.setSearchTitle(" ");
        sources.setSearchArtist("周杰伦");
        sources.search();
        request = LyricSourceControllerTestAccess::searchRequest(sources);
        QCOMPARE(ApiClientTestAccess::query(api, request).value("keywords").toString(),
                 QString("周杰伦"));
        api.cancel(request);
        LyricSourceControllerTestAccess::fail(sources, request);
        QCOMPARE(sources.errorMessage(), QString("搜索失败，请重试"));
        sources.setSearchArtist("  ");
        QVERIFY(!sources.canSearch());
        sources.search();
        QCOMPARE(LyricSourceControllerTestAccess::searchRequest(sources), quint64(0));
        sources.setSearchTitle("Stale");
        sources.search();
        request = LyricSourceControllerTestAccess::searchRequest(sources);
        auto next = original;
        next.id = "next";
        next.hash = QString(32, '2');
        next.title = "Next Song";
        sources.load(next);
        QCOMPARE(sources.searchTitle(), next.title);
        rows(request, {"stale"});
        QCOMPARE(sources.rowCount(), 0);
        api.advanceSession();
        QVERIFY(sources.searchTitle().isEmpty());
        QVERIFY(sources.searchArtist().isEmpty());
        QVERIFY(!sources.loading());
    }
    void lyricAutomaticFallbackPreservesQueryModeIdentityAndManualPriority()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("fallback.ini"));
        ApiClient api(DeviceIdentity::temporary());
        LyricSourceController sources(api, settings);
        QAbstractItemModelTester modelTester(
            &sources, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy activated(&sources, &LyricSourceController::activated);
        // Canonical-domain application inputs, never claimed as upstream samples.
        const auto candidate = [](const QString &id) {
            return QJsonObject{{"source_id", id},       {"access_key", "ephemeral-domain-key"},
                               {"title", id},           {"artist", "Artist"},
                               {"source_label", "ugc"}, {"duration_ms", 4000},
                               {"score", 80},           {"word_timed", true},
                               {"scrollable", true}};
        };
        const QByteArray lyric =
            R"({"offset_ms":0,"scrollable":true,"lines":[{"text":"Fallback","start_ms":0,"duration_ms":1000,"words":[]}]})";
        const auto complete = [&](quint64 id, const QByteArray &payload) {
            QVERIFY(id);
            api.cancel(id);
            LyricSourceControllerTestAccess::succeed(sources, id, payload);
        };
        const auto rows = [&](quint64 id, const QStringList &ids) {
            QJsonArray values;
            for (const auto &value : ids)
                values.append(candidate(value));
            complete(id, QJsonDocument(values).toJson());
        };
        const auto searchId = [&] {
            return LyricSourceControllerTestAccess::searchRequest(sources);
        };
        const auto defaultId = [&] {
            return LyricSourceControllerTestAccess::defaultRequest(sources);
        };
        const auto finishPreview = [&] { completeLyricPreview(sources, api, lyric); };
        auto original = track(1);
        original.hash = QString(32, '1');
        original.title = " Song ";
        original.artist = " Artist ";
        sources.load(original);
        QCOMPARE(ApiClientTestAccess::query(api, searchId()).value("hash").toString(),
                 original.hash);
        sources.setSearchTitle("Dialog draft");
        sources.setSearchArtist("Unrelated");
        rows(searchId(), {});
        QCOMPARE(ApiClientTestAccess::query(api, searchId()),
                 (QJsonObject{{"keywords", "Artist - Song"},
                              {"duration_ms", 4000},
                              {"all_candidates", false}}));
        rows(searchId(), {"best"});
        QCOMPARE(defaultId(), quint64(0));
        finishPreview();
        QCOMPARE(sources.currentId(), QString("best"));
        QCOMPARE(activated.last().at(1).toString(), original.key());
        QVERIFY(!sources.manual());
        QVERIFY(settings.allKeys().isEmpty());
        sources.openSelection();
        rows(searchId(), {});
        QCOMPARE(ApiClientTestAccess::query(api, searchId()).value("all_candidates").toBool(),
                 true);
        rows(searchId(), {"other", "best"});
        QCOMPARE(ApiClientTestAccess::query(api, defaultId()),
                 (QJsonObject{{"keywords", "Artist - Song"},
                              {"duration_ms", 4000},
                              {"all_candidates", false}}));
        rows(defaultId(), {"best"});
        QCOMPARE(sources.index(0).data(Qt::UserRole + 1).toString(), QString("best"));
        QVERIFY(sources.index(0).data(Qt::UserRole + 11).toBool());
        finishPreview();
        QCOMPARE(activated.size(), 1);
        sources.select(1);
        finishPreview();
        sources.applySelected();
        QCOMPARE(sources.currentId(), QString("other"));
        QVERIFY(sources.manual());
        sources.closeSelection();
        sources.openSelection();
        QCOMPARE(searchId(), quint64(0)); // Reuse the automatic fallback catalog.
        QCOMPARE(sources.index(sources.selectedIndex()).data(Qt::UserRole + 1).toString(),
                 QString("other"));
        sources.refresh();
        rows(searchId(), {});
        rows(searchId(), {"best"});
        rows(defaultId(), {"best"});
        QCOMPARE(sources.rowCount(), 2); // Remembered selection survives a new auto list.
        QCOMPARE(sources.index(sources.selectedIndex()).data(Qt::UserRole + 1).toString(),
                 QString("other"));
        finishPreview();
        sources.applySelected();
        sources.closeSelection();
        sources.load(original);
        QCOMPARE(ApiClientTestAccess::query(api, searchId()).value("keywords").toString(),
                 QString("Artist - Song"));
        rows(searchId(), {"other"});
        QCOMPARE(defaultId(), quint64(0));
        finishPreview();
        QCOMPARE(sources.currentId(), QString("other"));
        const auto activatedBeforeMissing = activated.size();
        sources.load(original);
        rows(searchId(), {"best"});
        QCOMPARE(activated.size(), activatedBeforeMissing);
        QVERIFY(sources.manual());
        QVERIFY(sources.currentId().isEmpty());
        QCOMPARE(sources.selectedIndex(), -1);
        QVERIFY(!sources.errorMessage().isEmpty());
        QVERIFY(!settings.allKeys().isEmpty());
        sources.restoreAuto();
        QVERIFY(ApiClientTestAccess::query(api, searchId()).contains("hash"));
        rows(searchId(), {});
        rows(searchId(), {"best"});
        finishPreview();
        QCOMPARE(sources.currentId(), QString("best"));
        QVERIFY(!sources.manual());
        QVERIFY(settings.allKeys().isEmpty());
        sources.openSelection();
        rows(searchId(), {});
        const auto closedRequest = searchId();
        sources.closeSelection();
        rows(closedRequest, {"obsolete"});
        QCOMPARE(sources.rowCount(), 0);
        QCOMPARE(sources.currentId(), QString("best"));
        sources.load(original);
        rows(searchId(), {});
        const auto staleRequest = searchId();
        auto next = original;
        next.id = "next";
        next.hash = QString(32, '2');
        sources.load(next);
        rows(staleRequest, {"obsolete"});
        QCOMPARE(sources.rowCount(), 0);
        auto request = searchId();
        api.cancel(request);
        LyricSourceControllerTestAccess::fail(sources, request);
        QCOMPARE(searchId(), quint64(0));
        QCOMPARE(defaultId(), quint64(0));
        QVERIFY(!sources.errorMessage().isEmpty()); // Error is not an empty-match fallback.
        next.title = "  ";
        sources.load(next);
        rows(searchId(), {});
        QCOMPARE(searchId(), quint64(0));
        QCOMPARE(sources.rowCount(), 0); // Artist-only is unsafe for auto.
        next.title = "Song";
        next.artist.clear();
        sources.load(next);
        const auto beforeEmptyAuto = activated.size();
        sources.openSelection();
        rows(searchId(), {});
        rows(searchId(), {});
        QVERIFY(ApiClientTestAccess::query(api, searchId()).contains("hash"));
        QVERIFY(ApiClientTestAccess::query(api, searchId()).value("all_candidates").toBool());
        rows(searchId(), {});
        rows(searchId(), {"preview-only"});
        rows(defaultId(), {});
        finishPreview();
        QCOMPARE(activated.size(), beforeEmptyAuto);
        QVERIFY(sources.currentId().isEmpty());
        QCOMPARE(sources.rowCount(), 1);
        sources.closeSelection();
        sources.load(next);
        complete(searchId(), QJsonDocument(QJsonArray{QJsonObject{{"source_id", "unusable"},
                                                                  {"access_key", ""}}})
                                 .toJson());
        QCOMPARE(ApiClientTestAccess::query(api, searchId()).value("keywords").toString(),
                 QString("Song"));
        rows(searchId(), {});
        QCOMPARE(searchId(), quint64(0));
        QCOMPARE(defaultId(), quint64(0));
        QVERIFY(!sources.loading());
        QCOMPARE(sources.state(), QString("empty"));
        sources.load(original);
        rows(searchId(), {});
        request = searchId();
        api.advanceSession();
        rows(request, {"obsolete"});
        QCOMPARE(sources.rowCount(), 0);
        QVERIFY(!sources.loading());
    }
    void lyricContentsSurviveAccountSwitchWithoutSharingCandidateKeys()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore settings(dir.filePath("lyrics.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(101, "offline-fixture"));
        LyricSourceController sources(api, settings);
        const auto song = track(1);
        sources.load(song);
        LyricSourceControllerTestAccess::seedCandidates(sources);
        const auto request = api.capabilities();
        LyricSourceControllerTestAccess::seedPreview(sources, request, "a");
        api.cancel(request);
        LyricSourceControllerTestAccess::succeed(
            sources, request,
            R"({"lines":[{"text":"Cached text","start_ms":1000,"duration_ms":2000,"words":[]}]})");
        QCOMPARE(sources.preview()->rowCount(), 1);
        QVERIFY(api.setAccount(202, "offline-fixture"));
        QCOMPARE(sources.rowCount(), 0);
        QCOMPARE(sources.preview()->rowCount(), 0);
        sources.load(song);
        LyricSourceControllerTestAccess::seedCandidates(sources);
        sources.select(0);
        QCOMPARE(LyricSourceControllerTestAccess::downloadRequest(sources), quint64(0));
        QCOMPARE(sources.preview()->rowCount(), 1);
        sources.load(track(2));
        LyricSourceControllerTestAccess::seedCandidates(sources);
        sources.select(0);
        QCOMPARE(sources.preview()->rowCount(), 0);
    }
    void manualLyricSourceIsSharedAcrossAccounts()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore settings(dir.filePath("lyrics.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(101, "offline-fixture"));
        LyricSourceController sources(api, settings);
        PlaybackTrack track;
        track.localPath = "/domain-only/local.flac";
        const QJsonObject lyrics{{"scrollable", true},
                                 {"lines", QJsonArray{QJsonObject{{"text", "shared lyrics"},
                                                                  {"start_ms", 1000},
                                                                  {"duration_ms", 2000},
                                                                  {"words", QJsonArray{}}}}}};
        const auto load = [&] {
            sources.load(track);
            sources.updateEmbedded(track.key(), "Local", "Artist", lyrics);
        };
        load();
        sources.openSelection();
        sources.select(0);
        sources.applySelected();
        QVERIFY(sources.manual());
        for (const auto account : {202ULL, 0ULL, 101ULL}) {
            QVERIFY(api.setAccount(account, account ? "offline-fixture" : ""));
            load();
            QVERIFY(sources.manual());
            QCOMPARE(sources.currentId(), QString("embedded"));
        }
        QSignalSpy applied(&sources, &LyricSourceController::applied);
        QSignalSpy activated(&sources, &LyricSourceController::activated);
        const auto preferenceKeys = settings.allKeys().filter("lyrics/sources/");
        QCOMPARE(preferenceKeys.size(), 1);
        const auto preference = preferenceKeys.first();
        QCOMPARE(settings.value(preference).toString(), QString("embedded"));

        // Restore commits only after automatic matching has produced usable
        // lyrics. A failed request must leave the shared manual choice intact.
        sources.restoreAuto();
        auto request = LyricSourceControllerTestAccess::searchRequest(sources);
        QVERIFY(request);
        QVERIFY(sources.applying());
        QVERIFY(sources.manual());
        api.cancel(request);
        LyricSourceControllerTestAccess::fail(sources, request);
        QVERIFY(!sources.applying());
        QVERIFY(sources.manual());
        QCOMPARE(sources.currentId(), QString("embedded"));
        QCOMPARE(settings.value(preference).toString(), QString("embedded"));
        QCOMPARE(applied.size(), 0);
        QCOMPARE(activated.size(), 0);

        sources.restoreAuto();
        const auto abandoned = LyricSourceControllerTestAccess::searchRequest(sources);
        QVERIFY(abandoned);
        QVERIFY(api.setAccount(202, "offline-fixture"));
        load();
        const auto activatedBeforeStale = activated.size();
        LyricSourceControllerTestAccess::succeed(sources, abandoned, "[]");
        QVERIFY(sources.manual());
        QVERIFY(!sources.applying());
        QCOMPARE(sources.currentId(), QString("embedded"));
        QCOMPARE(settings.value(preference).toString(), QString("embedded"));
        QCOMPARE(activated.size(), activatedBeforeStale);
        QCOMPARE(applied.size(), 0);

        // This fixture has one automatic keyword query and embedded lyrics.
        // Complete that domain response before checking preference removal or
        // changing accounts; changing first would cancel the restore itself.
        sources.restoreAuto();
        request = LyricSourceControllerTestAccess::searchRequest(sources);
        QVERIFY(request);
        QCOMPARE(ApiClientTestAccess::query(api, request).value("keywords").toString(),
                 QString("Artist - Local"));
        QVERIFY(sources.manual());
        QCOMPARE(settings.value(preference).toString(), QString("embedded"));
        api.cancel(request);
        LyricSourceControllerTestAccess::succeed(sources, request, "[]");
        QVERIFY(!sources.applying());
        QVERIFY(!sources.loading());
        QVERIFY(!sources.previewLoading());
        QVERIFY(!sources.manual());
        QCOMPARE(sources.currentId(), QString("embedded"));
        QCOMPARE(applied.size(), 1);
        QCOMPARE(activated.size(), activatedBeforeStale + 1);
        QVERIFY(settings.allKeys().filter("lyrics/sources/").isEmpty());
        QVERIFY(api.setAccount(101, "offline-fixture"));
        load();
        QVERIFY(!sources.manual());
        QVERIFY(settings.allKeys().filter("lyrics/sources/").isEmpty());
    }
    void embeddedLyricsKeepManualPriorityAndOrderedMetadataFallback()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore settings(dir.filePath("lyrics.ini"));
        ApiClient api(DeviceIdentity::temporary());
        LyricSourceController sources(api, settings);
        QAbstractItemModelTester tester(&sources,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy active(&sources, &LyricSourceController::activated);
        PlaybackTrack track;
        track.id = "embedded-domain";
        track.hash = QString(32, 'A');
        track.title = "Catalog";
        track.artist = "Artist";
        track.durationMs = 5000;
        const QJsonObject lyric{{"scrollable", true},
                                {"offset_ms", 0},
                                {"lines", QJsonArray{QJsonObject{{"text", "内嵌歌词"},
                                                                 {"start_ms", 1000},
                                                                 {"duration_ms", 2000},
                                                                 {"words", QJsonArray{}}}}}};
        const auto finish = [&](quint64 request, const QByteArray &payload) {
            QVERIFY(request);
            api.cancel(request);
            LyricSourceControllerTestAccess::succeed(sources, request, payload);
        };
        const auto search = [&] { return LyricSourceControllerTestAccess::searchRequest(sources); };
        sources.load(track);
        sources.updateEmbedded("old-track", "Wrong", "Wrong", lyric);
        QCOMPARE(sources.rowCount(), 0);
        sources.updateEmbedded(track.stableKey(), "Embedded title", "Embedded artist", lyric);
        QCOMPARE(sources.rowCount(), 1);
        QVERIFY(sources.loading());
        finish(search(), "[]");
        QCOMPARE(ApiClientTestAccess::query(api, search()).value("keywords").toString(),
                 QString("Artist - Catalog"));
        finish(search(), "[]");
        QCOMPARE(ApiClientTestAccess::query(api, search()).value("keywords").toString(),
                 QString("Embedded artist - Embedded title"));
        finish(search(), "[]");
        QCOMPARE(active.size(), 1);
        QCOMPARE(sources.currentId(), QString("embedded"));
        QVERIFY(!sources.manual());
        sources.openSelection();
        if (search()) {
            finish(search(), "[]");
            finish(search(), "[]");
            finish(search(), "[]");
        }
        sources.select(0);
        sources.applySelected();
        QVERIFY(sources.manual());
        QCOMPARE(sources.preview()->lineAt(0).value("lineText").toString(), QString("内嵌歌词"));
        sources.closeSelection();
        sources.load(track);
        QCOMPARE(search(), 0ULL);
        QVERIFY(sources.manual());
        sources.updateEmbedded(track.stableKey(), "Embedded title", "Embedded artist", lyric);
        QCOMPARE(sources.currentId(), QString("embedded"));
        QVERIFY(!sources.loading());
        sources
            .openSelection(); // The embedded choice remains usable while the online catalog loads.
        sources.select(0);
        sources.applySelected();
        QCOMPARE(search(), 0ULL);
        QCOMPARE(sources.currentId(), QString("embedded"));
        sources.restoreAuto();
        finish(search(), "[]");
        finish(search(), "[]");
        finish(search(), "[]");
        QVERIFY(!sources.manual());
        QCOMPARE(sources.currentId(), QString("embedded"));
        sources.clear();
        QCOMPARE(sources.rowCount(), 0);
        sources.updateEmbedded(track.stableKey(), "Stale", "Stale", lyric);
        QCOMPARE(sources.rowCount(), 0);
        PlaybackTrack masked = track;
        masked.title = "***";
        masked.artist = "*****";
        sources.load(masked);
        sources.updateEmbedded(masked.stableKey(), "Actual title", "Actual artist", lyric);
        QCOMPARE(sources.searchTitle(), QString("Actual title"));
        QCOMPARE(sources.searchArtist(), QString("Actual artist"));
        finish(search(), "[]");
        QCOMPARE(ApiClientTestAccess::query(api, search()).value("keywords").toString(),
                 QString("Actual artist - Actual title"));
        finish(search(), "[]");
        QVERIFY(!sources.loading());
        QCOMPARE(sources.currentId(), QString("embedded"));
        // Plain embedded text never enters the playback timeline.
        sources.clear();
        PlaybackTrack local;
        local.localPath = "/presentation/local.flac";
        sources.load(local);
        auto plain = lyric;
        QJsonArray plainRows;
        for (int row = 0; row < 23; ++row)
            plainRows.append(QJsonObject{{"text", QString::number(row)},
                                         {"start_ms", 0},
                                         {"duration_ms", 0},
                                         {"words", QJsonArray{}}});
        plain["lines"] = plainRows;
        plain["scrollable"] = false;
        sources.updateEmbedded(local.stableKey(), {}, {}, plain);
        QCOMPARE(sources.currentId(), QString("embedded"));
        QCOMPARE(sources.preview()->rowCount(), 23);
        QCOMPARE(sources.previewRows()->rowCount(), 18);
        sources.preview()->setPosition(100000);
        QCOMPARE(sources.preview()->currentIndex(), -1);
    }
    void staticLyricsNeverAcquireATimelineIndex()
    {
        LyricModel lyrics(nullptr);
        const QJsonArray lines{QJsonObject{{"text", "Static one"},
                                           {"start_ms", 0},
                                           {"duration_ms", 0},
                                           {"words", QJsonArray{}}},
                               QJsonObject{{"text", "Static two"},
                                           {"start_ms", 0},
                                           {"duration_ms", 0},
                                           {"words", QJsonArray{}}}};
        QVERIFY(lyrics.replace({{"scrollable", false}, {"offset_ms", 0}, {"lines", lines}}));
        QCOMPARE(lyrics.rowCount(), 2);
        QVERIFY(!lyrics.scrollable());
        lyrics.setPosition(120000);
        QCOMPARE(lyrics.currentIndex(), -1);
        QCOMPARE(lyrics.indexAt(120000), -1);
    }
    void lyricPreviewDoesNotApplyUntilRequestedAndDropsPreviousTrackResults()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore settings(dir.filePath("sources.ini"));
        ApiClient api(DeviceIdentity::temporary());
        LyricSourceController sources(api, settings);
        QAbstractItemModelTester modelTester(
            &sources, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy activated(&sources, &LyricSourceController::activated);
        auto lyricTrack = track(1);
        lyricTrack.hash = QString(32, QLatin1Char('1'));
        const auto search = LyricSourceControllerTestAccess::search(sources, api, lyricTrack);
        LyricSourceControllerTestAccess::seedCandidates(sources);
        const auto preview = api.capabilities();
        LyricSourceControllerTestAccess::seedPreview(sources, preview, "a");
        const QByteArray lyrics =
            R"({"offset_ms":0,"lines":[{"text":"A","start_ms":0,"duration_ms":1000,"words":[],"translation":"译文","romanization":"a"}]})";
        LyricSourceControllerTestAccess::succeed(sources, preview, lyrics);
        QCOMPARE(activated.size(), 1);
        QCOMPARE(sources.currentId(), QString("a"));
        QVERIFY(sources.preview()->hasTranslation());
        QVERIFY(sources.preview()->hasRomanization());
        sources.select(1);
        QCOMPARE(activated.size(), 1);
        const auto next = api.capabilities();
        LyricSourceControllerTestAccess::seedPreview(sources, next, "b");
        LyricSourceControllerTestAccess::succeed(sources, next, lyrics);
        QCOMPARE(activated.size(), 1);
        QCOMPARE(sources.currentId(), QString("a"));
        sources.applySelected();
        QCOMPARE(activated.size(), 2);
        QVERIFY(sources.manual());
        QCOMPARE(sources.currentId(), QString("b"));
        sources.restoreAuto();
        LyricSourceControllerTestAccess::seedCandidates(sources);
        const auto restored = api.capabilities();
        LyricSourceControllerTestAccess::seedPreview(sources, restored, "a");
        LyricSourceControllerTestAccess::succeed(sources, restored, lyrics);
        QCOMPARE(activated.size(), 3);
        QCOMPARE(sources.currentId(), QString("a"));
        QVERIFY(!sources.manual());
        LyricSourceControllerTestAccess::search(sources, api, track(2));
        LyricSourceControllerTestAccess::succeed(sources, preview, lyrics);
        QCOMPARE(activated.size(), 3);
        QCOMPARE(sources.preview()->rowCount(), 0);
        api.cancel(search);
        sources.clear();
    }
    void addingToRetainedQueuePreservesActiveIdentityAndAccountScope()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        ApiClient api(DeviceIdentity::temporary());
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        PlaybackService playback(api, settings);
        playback.attachLibrary(library, mutations);
        auto *catalog = playback.queueCatalog();
        PlaybackTracks a{track(1), track(2)}, b{track(3), track(4)};
        auto *retained = catalog->activate("source/a", "Source A", a, 1);
        auto *active = catalog->activate("source/b", "Source B", b, 0);
        catalog->setViewIndex(1);
        auto *actions = playback.libraryActions();
        QAbstractItemModelTester tester(actions,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        actions->prepareViewedQueue();
        actions->setMultiple(true);
        actions->selectAll(true);
        QCOMPARE(actions->selectedCount(), 3);
        QSignalSpy completed(actions, &PlaybackLibraryActions::completed);
        actions->submitSelection();
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(catalog->activeModel(), active);
        QCOMPARE(catalog->viewModel(), retained);
        QCOMPARE(active->currentIndex(), 0);
        QCOMPARE(active->rowCount(), 4);
        QCOMPARE(catalog->snapshot().first().tracks.size(), 2);
        QVERIFY(!actions->busy());
        actions->prepareViewedQueue();
        actions->setMultiple(true);
        actions->selectAll(true);
        QVERIFY(api.setAccount(1, QStringLiteral("test-only-token")));
        QCOMPARE(actions->selectedCount(), 0);
        QVERIFY(!actions->favorite());
        QVERIFY(api.setAccount(0, QString()));
        QSignalSpy login(actions, &PlaybackLibraryActions::loginRequested);
        actions->toggleFavorite();
        QCOMPARE(login.size(), 1);
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    }
    void deadlineSurvivesSuspensionAndFinishesOnlyRequestedTrack()
    {
        qint64 now = 100000;
        SleepTimer timer(nullptr, [&] { return now; });
        QSignalSpy actions(&timer, &SleepTimer::actionRequested);
        QVERIFY(!timer.start(0, "pause"));
        QVERIFY(!timer.start(30, "unknown"));
        QVERIFY(timer.start(1, "pause"));
        now += 60500; // No ticks during simulated system sleep.
        QVERIFY(timer.tick("track-a", true));
        QCOMPARE(actions.size(), 1);
        QVERIFY(!timer.active());
        QVERIFY(!timer.tick("track-a", true));
        timer.executionFinished();
        timer.setFinishTrack(true);
        QVERIFY(timer.start(1, "quit"));
        now += 61000;
        QVERIFY(!timer.tick("track-a", true));
        QVERIFY(timer.waiting());
        QVERIFY(timer.trackEnded());
        QCOMPARE(actions.size(), 2);
        QCOMPARE(actions.last()[0].toString(), QString("quit"));
        timer.executionFinished("platform failure");
        QCOMPARE(timer.errorMessage(), QString("platform failure"));
        QVERIFY(timer.start(1, "pause"));
        now += 61000;
        QVERIFY(!timer.tick("track-a", true));
        QVERIFY(timer.tick("track-b", true));
        timer.executionFinished();
        QVERIFY(timer.start(1, "pause"));
        timer.cancel();
        now += 61000;
        QVERIFY(!timer.trackEnded());
        QCOMPARE(actions.size(), 3);
    }
    void contextualNativeConversionKeepsWordTimingAndRejectsStaleTrack()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore settings(dir.filePath("lyrics.ini"));
        NativeBridge native;
        LyricModel lyrics(nullptr);
        LyricTextController text(lyrics, native, settings);
        text.setWantTranslation(true);
        text.setWantRomanization(true);
        const auto make = [](const QString &value) {
            QJsonArray words;
            int start = 1000;
            for (auto scalar : value.toUcs4()) {
                const char32_t character = char32_t(scalar);
                words.append(QJsonObject{{"text", QString::fromUcs4(&character, 1)},
                                         {"start_ms", start},
                                         {"duration_ms", 100}});
                start += 100;
            }
            return QJsonObject{{"offset_ms", 0},
                               {"lines", QJsonArray{QJsonObject{{"text", value},
                                                                {"start_ms", 1000},
                                                                {"duration_ms", start - 1000},
                                                                {"words", words}}}}};
        };
        QVERIFY(text.replace(make(QStringLiteral("頭髮😀")), "a"));
        text.setConversion("simplified");
        QTRY_COMPARE_WITH_TIMEOUT(lyrics.data(lyrics.index(0), Qt::UserRole + 1).toString(),
                                  QStringLiteral("头发😀"), 10000);
        const auto words = lyrics.data(lyrics.index(0), Qt::UserRole + 4).toList();
        QCOMPARE(words[2].toMap().value("text").toString(), QStringLiteral("😀"));
        QCOMPARE(words[2].toMap().value("start_ms").toInt(), 1200);
        text.setGlobalOffsetMs(200);
        text.adjustTrackOffset(1);
        text.setPosition(700);
        QCOMPARE(lyrics.currentIndex(), 0);
        QCOMPARE(lyrics.offsetMs(), qint64(-300));
        text.setConversion("traditional");
        text.clear();
        QVERIFY(text.wantTranslation());
        QVERIFY(text.wantRomanization());
        QVERIFY(!lyrics.hasTranslation());
        QVERIFY(!lyrics.hasRomanization());
        QVERIFY(text.replace(make(QStringLiteral("后台")), "b"));
        text.setConversion("none");
        QTest::qWait(150);
        QCOMPARE(lyrics.data(lyrics.index(0), Qt::UserRole + 1).toString(), QStringLiteral("后台"));
        QCOMPARE(lyrics.offsetMs(), qint64(-200));
        QSignalSpy closed(&native, &NativeBridge::closed);
        native.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 10000);
    }
    void traditionalVariantsUseNativeWorkerAndRestorePreference()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("lyrics.ini");
        EchoMusic::SettingsStore settings(path);
        NativeBridge native;
        LyricModel lyrics(nullptr);
        LyricTextController text(lyrics, native, settings);
        const QString input = QStringLiteral(
            "因为这里的群众，峰回路转。才华 著作 梁先生 桥梁 人参 参加 口吃 吃饭𠮷🙂");
        const QString expected = QStringLiteral(
            "因爲這裏的羣衆，峯迴路轉。才華 著作 梁先生 橋樑 人蔘 參加 口吃 喫飯𠮷🙂");
        QJsonArray originalWords;
        for (const auto scalar : input.toUcs4()) {
            const char32_t character = char32_t(scalar);
            originalWords.append(QJsonObject{{"text", QString::fromUcs4(&character, 1)},
                                             {"start_ms", 1000 + originalWords.size() * 100},
                                             {"duration_ms", 100}});
        }
        const QJsonObject original{
            {"offset_ms", 50},
            {"lines", QJsonArray{QJsonObject{{"text", input},
                                             {"translation", "因为群众"},
                                             {"romanization", "yin wei"},
                                             {"start_ms", 1000},
                                             {"duration_ms", originalWords.size() * 100},
                                             {"words", originalWords}}}}};
        QVERIFY(text.replace(original, "variant-track"));
        text.setPosition(1500);
        text.setConversion("traditional");
        QTRY_COMPARE_WITH_TIMEOUT(
            lyrics.lineAt(0).value("lineText").toString(),
            QStringLiteral(
                "因為這裡的群眾，峰迴路轉。才華 著作 梁先生 橋樑 人參 參加 口吃 吃飯𠮷🙂"),
            10000);
        text.setConversion("traditional-variants");
        QTRY_COMPARE_WITH_TIMEOUT(lyrics.lineAt(0).value("lineText").toString(), expected, 10000);
        QCOMPARE(lyrics.data(lyrics.index(0), Qt::UserRole + 5).toString(),
                 QStringLiteral("因爲羣衆"));
        QCOMPARE(lyrics.data(lyrics.index(0), Qt::UserRole + 6).toString(),
                 QStringLiteral("yin wei"));
        const auto words = lyrics.data(lyrics.index(0), Qt::UserRole + 4).toList();
        QCOMPARE(words.size(), originalWords.size());
        QString joined;
        for (int i = 0; i < words.size(); ++i) {
            const auto word = words[i].toMap();
            joined += word.value("text").toString();
            QCOMPARE(word.value("start_ms").toInt(), 1000 + i * 100);
            QCOMPARE(word.value("duration_ms").toInt(), 100);
        }
        QCOMPARE(joined, expected);
        QCOMPARE(lyrics.offsetMs(), 50);
        QCOMPARE(lyrics.currentIndex(), 0);
        QVERIFY(text.errorMessage().isEmpty());
        settings.sync();
        EchoMusic::SettingsStore reopened(path);
        LyricModel restored(nullptr);
        LyricTextController again(restored, native, reopened);
        QCOMPARE(again.conversion(), QStringLiteral("traditional-variants"));
        QVERIFY(again.replace(original, "variant-track"));
        QTRY_COMPARE_WITH_TIMEOUT(restored.lineAt(0).value("lineText").toString(), expected, 10000);
        text.setConversion("none");
        QCOMPARE(lyrics.lineAt(0).value("lineText").toString(), input);
        // A discarded in-flight variant result must not overwrite the next track.
        text.setConversion("traditional-variants");
        text.clear();
        text.setConversion("none");
        QVERIFY(text.replace(original, "next-track"));
        QTest::qWait(150);
        QCOMPARE(lyrics.lineAt(0).value("lineText").toString(), input);
        QSignalSpy closed(&native, &NativeBridge::closed);
        native.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 10000);
    }
    void singleAndBatchPlayNextFollowSourceOrdering()
    {
        PlaybackQueueModel queue(nullptr);
        queue.replace({track(1), track(2), track(3), track(4)}, 0);
        queue.append({track(2), track(3)}, true);
        queue.append({track(4)}, true);
        QCOMPARE(queue.at(queue.takeNextPriority())->id, QString("4"));
        QCOMPARE(queue.at(queue.takeNextPriority())->id, QString("2"));
        queue.append({track(3)}, true);
        QCOMPARE(queue.at(queue.takeNextPriority())->id, QString("3"));
        QCOMPARE(queue.takeNextPriority(), -1);
    }
    void largeQueueKeepsIdentityThroughMovesAndPriority()
    {
        PlaybackQueueModel model(nullptr);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        PlaybackTracks tracks;
        for (int i = 0; i < 50000; ++i)
            tracks.append(track(i));
        tracks.append(track(3));
        model.replace(tracks, 30000);
        QCOMPARE(model.rowCount(), 50000);
        model.append({track(40000), track(12), track(40000)}, true);
        int target = -1;
        for (int i = 0; i < model.rowCount(); ++i)
            if (model.at(i)->id == "40000")
                target = i;
        model.move(target, 2);
        QCOMPARE(model.at(model.currentIndex())->id, QString("30000"));
        QCOMPARE(model.at(model.takeNextPriority())->id, QString("12"));
        QCOMPARE(model.takeNextPriority(), 2);
        QCOMPARE(model.takeNextPriority(), -1);
        model.remove(0);
        QCOMPARE(model.at(model.currentIndex())->id, QString("30000"));
        QSignalSpy removed(&model, &PlaybackQueueModel::currentRemoved);
        model.remove(model.currentIndex());
        QCOMPARE(removed.size(), 1);
        QCOMPARE(model.currentIndex(), -1);
    }
    void catalogBrowsingAndSourceRetention()
    {
        PlaybackQueueCatalog catalog(nullptr);
        QAbstractItemModelTester tester(&catalog,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        for (int i = 0; i < 6; ++i)
            catalog.activate(QString::number(i), QString::number(i), {track(i)}, 0);
        QCOMPARE(catalog.snapshot().size(), 4);
        QCOMPARE(catalog.snapshot().first().key, QString("2"));
        QCOMPARE(catalog.activeModel()->at(0)->id, QString("5"));
        catalog.setViewIndex(1);
        QVERIFY(catalog.activeModel() != catalog.viewModel());
        QCOMPARE(catalog.activeModel()->at(0)->id, QString("5"));
        auto states = catalog.snapshot();
        const auto key = catalog.activeKey();
        catalog.restore(states, key);
        QCOMPARE(catalog.snapshot().size(), 4);
        QCOMPARE(catalog.activeModel()->at(0)->id, QString("5"));
        catalog.setViewIndex(1);
        catalog.activateViewed();
        QCOMPARE(catalog.activeIndex(), catalog.viewIndex());
    }
    void sqlRoundtripAccountIsolationAndDrain()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("playback.sqlite");
        PlaybackRepository repository(path);
        QSignalSpy failed(&repository, &PlaybackRepository::failed),
            loaded(&repository, &PlaybackRepository::loaded),
            closed(&repository, &PlaybackRepository::closed);
        PlaybackQueueStates states{
            {"default", "我的队列", {track(1), track(2)}, 1, {track(1).key()}}};
        repository.save(101, states, "default");
        repository.record(101, track(1), 1250);
        repository.load(202, 9);
        repository.load(101, 10);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 2, 5000);
        QCOMPARE(failed.size(), 0);
        QVERIFY(qvariant_cast<PlaybackQueueStates>(loaded[0][2]).isEmpty());
        const auto restored = qvariant_cast<PlaybackQueueStates>(loaded[1][2]);
        QCOMPARE(restored.size(), 1);
        QCOMPARE(restored[0].tracks.size(), 2);
        QCOMPARE(restored[0].current, 1);
        QCOMPARE(restored[0].next, states[0].next);
        QCOMPARE(restored[0].tracks[0].localPath, track(1).localPath);
        repository.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 5000);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "playback-test-reader");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(
                "SELECT listened_ms,play_count FROM playback_history WHERE account_id='101'"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 1250);
            QCOMPARE(q.value(1).toInt(), 1);
            db.close();
        }
        QSqlDatabase::removeDatabase("playback-test-reader");
    }
    void domainCatalogCountsAndLyricSizesDoNotRejectValidData()
    {
        LyricModel lyrics(nullptr);
        QJsonArray words;
        for (int i = 0; i < 2049; ++i)
            words.append(QJsonObject{{"text", "字"}, {"start_ms", i}, {"duration_ms", 1}});
        QJsonArray lines;
        lines.append(QJsonObject{{"text", QString(65537, QChar('A'))},
                                 {"start_ms", 0},
                                 {"duration_ms", 3000},
                                 {"words", words}});
        for (int i = 1; i < 10001; ++i)
            lines.append(QJsonObject{{"text", QString::number(i)},
                                     {"start_ms", i * 3000},
                                     {"duration_ms", 3000},
                                     {"words", QJsonArray{}}});
        QVERIFY(lyrics.replace({{"lines", lines}}));
        QCOMPARE(lyrics.rowCount(), 10001);
        QTemporaryDir dir;
        EchoMusic::SettingsStore settings(dir.filePath("catalog.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, settings);
        QJsonArray items;
        for (int i = 0; i < 35; ++i)
            items.append(QJsonObject{{"id", i},
                                     {"title", QString::number(i)},
                                     {"source", "artist"},
                                     {"retained_field", i + 100}});
        auto *catalog = playback.effectCatalog();
        AudioEffectCatalogTestAccess::finish(*catalog, api, {{"items", items}, {"total", 1}});
        QCOMPARE(catalog->rowCount(), 35);
        QVERIFY(catalog->errorMessage().isEmpty());
        QCOMPARE(
            catalog->data(catalog->index(34), Qt::UserRole + 1).toMap()["retained_field"].toInt(),
            134);
        LyricSourceController sources(api, settings);
        auto id = LyricSourceControllerTestAccess::search(sources, api, track(1));
        QJsonArray candidates;
        for (int i = 0; i < 600; ++i)
            candidates.append(QJsonObject{{"source_id", QString::number(i)},
                                          {"access_key", "domain-key"},
                                          {"title", QString::number(i)}});
        LyricSourceControllerTestAccess::succeed(sources, id, QJsonDocument(candidates).toJson());
        QCOMPARE(sources.rowCount(), 600);
        sources.clear();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
    void lyricOffsetSeekAndValidation()
    {
        LyricModel model(nullptr);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QJsonObject line{{"text", "中文 ABC"},
                               {"start_ms", 1000},
                               {"duration_ms", 500},
                               {"words", QJsonArray{}}};
        QVERIFY(model.replace({{"offset_ms", -250}, {"lines", QJsonArray{line}}}));
        model.setPosition(749);
        QCOMPARE(model.currentIndex(), -1);
        model.setPosition(750);
        QCOMPARE(model.currentIndex(), 0);
        QCOMPARE(model.data(model.index(0), Qt::UserRole + 2).toLongLong(), 750);
        QVERIFY(!model.replace({{"lines", QJsonArray{QJsonObject{}}}}));
        QCOMPARE(model.rowCount(), 1);
        auto malformed = line;
        malformed.insert("start_ms", 1.e300);
        QVERIFY(!model.replace({{"lines", QJsonArray{malformed}}}));
        malformed.insert("start_ms", -1);
        QVERIFY(!model.replace({{"lines", QJsonArray{malformed}}}));
        malformed.insert("start_ms", 1.5);
        QVERIFY(!model.replace({{"lines", QJsonArray{malformed}}}));
        QCOMPARE(model.rowCount(), 1);
    }
    void liveListeningReportThroughActualPlayback()
    {
        if (qEnvironmentVariable("ECHO_LIVE_LISTENING") != "1")
            QSKIP("Opt-in current account with real audio; secrets arrive on stdin only");
        const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        DeviceIdentity device{identity.value("mid").toString(),
                              identity.value("dfid").toString(),
                              identity.value("guid").toString(),
                              identity.value("mac").toString(),
                              "EchoMusicNext",
                              "-",
                              "-"};
        ApiClient api(device);
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("live.ini"));
        settings.setValue("playback/volume", 0);
        settings.setValue("playback/defaultQuality", "standard");
        NetworkPreferences network(settings, api);
        QVERIFY(network.transportAvailable());
        const auto previousProxy = QNetworkProxy::applicationProxy();
        const auto restoreProxy =
            qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previousProxy); });
        network.installApplicationTransport();
        QVERIFY(api.setAccount(quint64(identity.value("userid").toInteger()),
                               identity.value("token").toString(),
                               identity.value("t1").toString()));
        QSignalSpy results(&api, &ApiClient::succeeded), failures(&api, &ApiClient::failed);
        const auto request = api.albumSongs(960399, 1, 1);
        const auto findResult = [&](quint64 id) -> QJsonObject {
            for (const auto &row : results)
                if (row[0].toULongLong() == id)
                    return QJsonDocument::fromJson(row[1].toByteArray()).object();
            return {};
        };
        QTRY_VERIFY_WITH_TIMEOUT(!findResult(request).isEmpty(), 30000);
        const auto songs = findResult(request).value("page").toObject().value("items").toArray();
        QVERIFY(!songs.isEmpty());
        auto song = PlaybackTrack::fromDomain(songs.first().toObject().value("song").toObject());
        QVERIFY(song.playable());
        PlaybackService playback(api, settings, nullptr, directory.filePath("live.sqlite"),
                                 &network);
        QSignalSpy drained(&playback, &PlaybackService::listeningReportsDrained),
            closed(&playback, &PlaybackService::closed);
        playback.playTracks({song}, 0, "Live report", "live-listening");
        QTRY_VERIFY_WITH_TIMEOUT(
            playback.playing() && !playback.loading() && playback.positionMs() > 1000, 45000);
        // This is an actual engine clock, not an injected position or fake duration.
        for (int i = 0; i < 125; ++i) {
            QTest::qWait(1000);
            QVERIFY2(playback.errorMessage().isEmpty(), "Actual audio playback failed");
            QVERIFY(playback.playing());
        }
        auto &reporter = PlaybackServiceTestAccess::listening(playback);
        qInfo() << "Actual audio wall time ms:" << ListeningReporterTestAccess::duration(reporter)
                << "unreported ms:" << ListeningReporterTestAccess::pending(reporter);
        QVERIFY(ListeningReporterTestAccess::duration(reporter) > 60000);
        playback.stop();
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        int accepted = 0, grades = 0;
        for (const auto &row : results) {
            const auto value = QJsonDocument::fromJson(row[1].toByteArray()).object();
            accepted += value.value("accepted").toBool();
            grades += value.contains("listening_seconds");
        }
        qInfo() << "Real listening acknowledgements:" << accepted << "grade responses:" << grades;
        QCOMPARE(accepted, 2);
        QVERIFY(grades >= 2);
        QVERIFY2(failures.isEmpty(),
                 "A native network/report stage failed; credentials suppressed");
        api.shutdown();
    }
    void livePlaybackThroughQtService()
    {
        if (qEnvironmentVariable("ECHO_LIVE_PLAYBACK") != "1")
            QSKIP("Opt-in real account/audio chain; credentials only in process memory");
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
        NetworkPreferences network(settings, api);
        QVERIFY(network.transportAvailable());
        const auto previousProxy = QNetworkProxy::applicationProxy();
        const auto restoreProxy =
            qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previousProxy); });
        network.installApplicationTransport();
        QVERIFY(api.setAccount(quint64(identity.value("userid").toInteger()),
                               identity.value("token").toString(),
                               identity.value("t1").toString()));
        QSignalSpy results(&api, &ApiClient::succeeded), failures(&api, &ApiClient::failed);
        const auto request = api.albumSongs(960399, 1, 2);
        QTRY_VERIFY_WITH_TIMEOUT(!results.isEmpty(), 30000);
        QCOMPARE(results.first()[0].toULongLong(), request);
        const auto songs = QJsonDocument::fromJson(results.first()[1].toByteArray())
                               .object()
                               .value("page")
                               .toObject()
                               .value("items")
                               .toArray();
        QVERIFY(!songs.isEmpty());
        auto song = PlaybackTrack::fromDomain(songs.first().toObject().value("song").toObject());
        QVERIFY(song.playable());
        PlaybackService playback(api, settings, nullptr, dir.filePath("playback.sqlite"), &network);
        QSignalSpy closed(&playback, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        playback.playTracks({song}, 0, "采样专辑", "live-album");
        QTRY_VERIFY_WITH_TIMEOUT(!playback.loading(), 45000);
        QVERIFY2(playback.errorMessage().isEmpty(),
                 "Native playback source or engine failed (details suppressed)");
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && playback.positionMs() > 200, 10000);
        QCOMPARE(playback.resolvedQuality(), QString("hi_res"));
        QCOMPARE(playback.effectiveQuality(), QString("hi_res"));
        QTRY_VERIFY_WITH_TIMEOUT(playback.lyricState() != "loading", 30000);
        QCOMPARE(playback.lyricState(), QString("ready"));
        QVERIFY(playback.lyrics()->rowCount() > 0);
        playback.pause();
        QTRY_VERIFY(!playback.playing());
        playback.seek(15000);
        QTRY_VERIFY_WITH_TIMEOUT(playback.positionMs() > 14000, 10000);
        playback.setQuality("higher");
        QTRY_VERIFY_WITH_TIMEOUT(!playback.loading(), 45000);
        QVERIFY2(playback.errorMessage().isEmpty(), qPrintable(playback.errorMessage()));
        QTRY_VERIFY_WITH_TIMEOUT(playback.positionMs() > 14000, 10000);
        QVERIFY(!playback.playing());
        playback.play();
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing(), 10000);
        playback.setQuality("flac");
        QTRY_VERIFY_WITH_TIMEOUT(!playback.loading(), 45000);
        QVERIFY2(playback.errorMessage().isEmpty(), qPrintable(playback.errorMessage()));
        QVERIFY(playback.playing());
        QVERIFY(playback.positionMs() > 14000);
        // The current account has concept membership but no Super VIP. Shared
        // membership gating must skip Viper before tracker requests and resolve
        // a real Hi-Res source, keeping the explicit preference unchanged.
        const auto failuresBeforeFallback = failures.size();
        playback.setQuality("viper_tape");
        QTRY_VERIFY_WITH_TIMEOUT(!playback.loading(), 45000);
        QVERIFY2(playback.errorMessage().isEmpty(), "Catalog compatibility fallback failed");
        QCOMPARE(playback.quality(), QString("viper_tape"));
        QCOMPARE(playback.resolvedQuality(), QString("hi_res"));
        QCOMPARE(playback.preferences()->defaultQuality(), QString("hi_res"));
        QCOMPARE(failures.size(), failuresBeforeFallback);
        QVERIFY(playback.playing());
        QVERIFY(playback.positionMs() > 14000);
        playback.setQuality("flac");
        QTRY_VERIFY_WITH_TIMEOUT(!playback.loading(), 45000);
        QVERIFY(playback.errorMessage().isEmpty());
        for (const auto &effect : {"piano", "vocal", "accompaniment", "none"}) {
            playback.setSongEffect(effect);
            QTRY_VERIFY_WITH_TIMEOUT(!playback.loading(), 45000);
            QVERIFY2(playback.errorMessage().isEmpty(), "Effect source/decoder failed");
            QCOMPARE(playback.appliedSongEffect(), QString(effect));
            QVERIFY2(playback.songEffectError().isEmpty(), "Effect silently fell back");
            QTRY_VERIFY_WITH_TIMEOUT(playback.playing(), 10000);
            QVERIFY(playback.positionMs() > 14000);
        }
        auto *catalog = playback.effectCatalog();
        for (const auto &category : {"artist", "market", "headphone"}) {
            catalog->selectCategory(category);
            QTRY_VERIFY_WITH_TIMEOUT(!catalog->loading(), 30000);
            const auto failure =
                failures.isEmpty()
                    ? QJsonObject{}
                    : QJsonDocument::fromJson(failures.last()[2].toByteArray()).object();
            QVERIFY2(catalog->errorMessage().isEmpty(),
                     qPrintable(QString("Effect category=%1 kind=%2 code=%3")
                                    .arg(category, failure.value("kind").toString())
                                    .arg(failure.value("code").toInt())));
            QVERIFY(catalog->rowCount() > 0);
        }
        catalog->selectBrand(0);
        QTRY_VERIFY_WITH_TIMEOUT(!catalog->loading(), 30000);
        QVERIFY2(catalog->errorMessage().isEmpty(), qPrintable(catalog->errorMessage()));
        QVERIFY(catalog->rowCount() > 0);
        api.setAccount(0, QString());
        QTRY_VERIFY(!playback.hasTrack());
        QCOMPARE(playback.queue()->rowCount(), 0);
        playback.shutdown();
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
    }
    void actualDecoderTransportAndShutdown()
    {
        // Real FFmpeg/CPAL path with a silent four-second WAV. This checks engine
        // clock/transport and drain, not audible fidelity or other platforms.
        QTemporaryDir dir;
        const auto path = dir.filePath("silence.wav");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QDataStream s(&f);
        s.setByteOrder(QDataStream::LittleEndian);
        const quint32 n = 48000 * 4 * 2 * 2;
        s.writeRawData("RIFF", 4);
        s << quint32(36 + n);
        s.writeRawData("WAVEfmt ", 8);
        s << quint32(16) << quint16(1) << quint16(2) << quint32(48000) << quint32(192000)
          << quint16(4) << quint16(16);
        s.writeRawData("data", 4);
        s << n;
        s.writeRawData(QByteArray(n, 0).constData(), int(n));
        f.close();
        AudioEngine engine;
        QSignalSpy events(&engine, &AudioEngine::eventReceived),
            closed(&engine, &AudioEngine::closed);
        QTRY_VERIFY_WITH_TIMEOUT(has(events, "ready"), 15000);
        engine.load(path, 11);
        QTRY_VERIFY_WITH_TIMEOUT(has(events, "file-loaded", 11), 15000);
        engine.command(AudioEngine::Play);
        QTRY_VERIFY_WITH_TIMEOUT(position(events) > .15, 5000);
        const auto beforeCancellation = position(events);
        engine.cancelSourceReplacement();
        QTRY_VERIFY_WITH_TIMEOUT(position(events) > beforeCancellation + .1, 5000);
        engine.command(AudioEngine::Pause);
        QTest::qWait(200);
        const double paused = position(events);
        QTest::qWait(250);
        QVERIFY(std::abs(position(events) - paused) < .07);
        engine.command(AudioEngine::Seek, 1.5);
        QTRY_VERIFY_WITH_TIMEOUT(has(events, "seeked"), 5000);
        engine.load(path, 12);
        engine.load(path, 13);
        QTRY_VERIFY_WITH_TIMEOUT(has(events, "file-loaded", 13), 15000);
        engine.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        const int count = events.size();
        QTest::qWait(50);
        QCOMPARE(events.size(), count);
    }
    void songShareAndMenuCommandsUseSourceFormatsWithoutUpstreamFixtures()
    {
        // 分享文本使用标题，32位 hash 决定可分享性；链接保留专辑身份。
        // 菜单反馈由共享命令服务发出一次。
        PlaybackTrack track;
        track.id = "32042818";
        track.hash = "105A2705BEEE36A8BED029A8E1A6311D";
        track.title = QStringLiteral("晴天");
        track.artist = QStringLiteral("周杰伦");
        track.albumId = 960399;
        QVERIFY(isSongShareHash(track.hash.toLower()));
        QVERIFY(!isSongShareHash(QString(31, QLatin1Char('A'))));
        QVERIFY(!isSongShareHash(QStringLiteral("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz")));
        const auto url = songShareUrl(track);
        QVERIFY(url.startsWith(QStringLiteral("https://hoowhoami.github.io/EchoMusicNext/share/")));
        const auto target = parseShareTarget(url);
        QVERIFY(target);
        QCOMPARE(target->type, ShareResource::Song);
        QCOMPARE(target->id, QStringLiteral("105a2705beee36a8bed029a8e1a6311d"));
        QCOMPARE(target->query.value("albumId"), QStringLiteral("960399"));
        QCOMPARE(target->query.value("albumAudioId"), QStringLiteral("32042818"));
        QCOMPARE(songShareText(track, QStringLiteral("测试昵称")),
                 QStringLiteral("测试昵称 给你分享了歌曲「晴天」，快去看看吧\n") + url);
        QVERIFY(songShareText(track, QString()).startsWith(QStringLiteral("EchoMusic ")));
        auto unavailable = track;
        unavailable.hash.clear();
        QVERIFY(songShareUrl(unavailable).isEmpty());
        QVERIFY(songShareText(unavailable, QStringLiteral("测试昵称")).isEmpty());
        QVERIFY(QGuiApplication::clipboard());
        SongCommands commands;
        QSignalSpy notices(&commands, &SongCommands::notice);
        QVERIFY(commands.share(track));
        QCOMPARE(QGuiApplication::clipboard()->text(),
                 songShareText(track, QStringLiteral("EchoMusic")));
        QCOMPARE(notices.last().at(0).toString(), QStringLiteral("分享链接已复制"));
        QCOMPARE(notices.last().at(1).toInt(), 1);
        QVERIFY(commands.copyInfo(track));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("晴天 - 周杰伦"));
        auto artistOnly = track;
        artistOnly.title.clear();
        QVERIFY(commands.copyInfo(artistOnly));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("周杰伦"));
        auto emptyInfo = track;
        emptyInfo.title.clear();
        emptyInfo.artist.clear();
        const auto beforeEmpty = notices.size();
        QVERIFY(!commands.copyInfo(emptyInfo));
        QCOMPARE(notices.size(), beforeEmpty);
        const auto beforeUnavailable = notices.size();
        QVERIFY(!commands.share(unavailable));
        QCOMPARE(notices.size(), beforeUnavailable); // 源 ctxShareSong 不可分享时静默返回
        // 未接入黑名单/账号服务时保持不可用，不伪造入口。
        QVERIFY(!commands.blacklistAvailable(track));
        QVERIFY(!commands.addToBlacklist(track));
        QVERIFY(!commands.busy());
    }
};
QTEST_MAIN(PlaybackTest)
#include "PlaybackTest.moc"
