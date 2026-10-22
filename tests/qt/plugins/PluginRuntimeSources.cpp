#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "EchoPluginAudioSource.h"
#include "EchoPluginPlaylist.h"
#include "PlaybackSourceExtensions.h"
#include "TrackTransitionController.h"

namespace EchoMusic
{
class PlaybackSourceRequestTestAccess
{
public:
    static void publish(PlaybackSourceRequest &request, const QJsonObject &source,
                        const QString &kind)
    {
        request.publish(source, kind, "standard");
    }
};
} // namespace EchoMusic
namespace
{
QString sourcePackage(Host &host, const QByteArray &body,
                      QStringList capabilities = {"audioSource"})
{
    return package(host.temporary.path(), "export function activate(c) {" + body + "}",
                   {"lifecycle", "playback.audioSource", "playback.queues"}, capabilities);
}
EchoPluginContext *installSource(Host &host, const QString &path)
{
    host.runtime.importDirectory(path);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000 &&
           host.runtime.state(host.installation()) != PluginRuntime::Active)
        QTest::qWait(1);
    return host.runtime.context(host.installation());
}
QJsonObject resolvedSource(const QSignalSpy &spy)
{
    return spy.last().at(2).value<QJsonObject>();
}
PlaybackTrack customTrack()
{
    PlaybackTrack track;
    track.id = "test-custom";
    track.title = "Custom native track";
    track.durationMs = 12000;
    track.catalogMetadata["plugin_source"] = "test.runtime/radio";
    return track;
}
QString quietWav(Host &host)
{
    constexpr int rate = 48000, seconds = 20;
    QByteArray pcm(rate * seconds * 2, char(0));
    QByteArray wave;
    QDataStream data(&wave, QIODevice::WriteOnly);
    data.setByteOrder(QDataStream::LittleEndian);
    data.writeRawData("RIFF", 4);
    data << quint32(36 + pcm.size());
    data.writeRawData("WAVEfmt ", 8);
    data << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2)
         << quint16(2) << quint16(16);
    data.writeRawData("data", 4);
    data << quint32(pcm.size());
    wave.append(pcm);
    const auto path = host.temporary.path() + "/source.wav";
    write(path, wave);
    return QUrl::fromLocalFile(path).toString();
}
} // namespace
void PluginRuntimeTest::audioSourceStagesTransformsAndRetirement()
{
    Host host;
    const auto source = sourcePackage(host, R"(
      c.ui.setState({trace:[],mode:'final-fallback'});
      function record(value) {c.ui.setState({mode:c.ui.state.mode,trace:c.ui.state.trace.concat([value])});}
      ['before-catalog','after-catalog','final-fallback'].forEach(function(position) {
        c.player.audioSource.registerResolver(position, {position:position,
          match:function(v) {record('match:'+position);return Promise.resolve(true);},
          resolve:function(v) {record('resolve:'+position);return c.ui.state.mode===position ? {sources:[{url:'https://audio.test/a',audioTrackId:2},{url:'https://audio.test/b',audioTrackId:1}],quality:'flac',loudness:{lufs:-14,gain:1,peak:.9},noticeCode:'custom'} : null;}
        });
      });
      c.player.audioSource.registerResolver('patch', {order:2000,
        transform:function(v) {record('transform:'+v.stage);return {durationMs:12345};}
      });
    )");
    auto *scope = installSource(host, source);
    QVERIFY(scope);
    MembershipService membership(host.api, nullptr);
    PlaybackSourceRequest request(host.api, membership, nullptr, nullptr,
                                  host.playback.sourceExtensions());
    QSignalSpy resolved(&request, &PlaybackSourceRequest::resolved);
    QSignalSpy failed(&request, &PlaybackSourceRequest::failed);
    request.resolve(customTrack(), ApiClient::PlaybackQuality::Standard,
                    ApiClient::SoundEffect::None);
    QTRY_COMPARE(resolved.size(), 1);
    QCOMPARE(failed.size(), 0);
    const auto value = resolvedSource(resolved);
    QCOMPARE(value.value("quality").toString(), "flac");
    QCOMPARE(value.value("duration_ms").toInt(), 12345);
    QCOMPARE(value.value("sources").toArray().size(), 2);
    QCOMPARE(PlaybackSourceExtensions::audioTrack(value, "https://audio.test/a"), 2);
    QCOMPARE(value.value("loudness").toDouble(), -14);
    QCOMPARE(scope->ui()->state().value("trace").toStringList(),
             QStringList({"match:before-catalog", "resolve:before-catalog", "match:after-catalog",
                          "resolve:after-catalog", "match:final-fallback", "resolve:final-fallback",
                          "transform:final-fallback"}));
    // Native catalog/cloud candidates pass through the same typed transform chain.
    for (const auto &kind : {"catalog", "cloud"}) {
        resolved.clear();
        PlaybackSourceRequestTestAccess::publish(
            request,
            {{"urls", QJsonArray{"https://audio.test/native"}},
             {"backup_urls", QJsonArray{"https://audio.test/backup"}}},
            kind);
        QTRY_COMPARE(resolved.size(), 1);
        QCOMPARE(resolvedSource(resolved).value("urls").toArray().size(), 2);
        QCOMPARE(resolvedSource(resolved).value("duration_ms").toInt(), 12345);
    }
    // A successful before-catalog candidate resumes the normal fallback chain
    // only after its URLs fail; it must not repeat the same provider forever.
    scope->ui()->setState({{"trace", QVariantList{}}, {"mode", "before-catalog"}});
    resolved.clear();
    request.resolve(customTrack(), ApiClient::PlaybackQuality::Standard,
                    ApiClient::SoundEffect::None);
    QTRY_COMPARE(resolved.size(), 1);
    QVERIFY(request.continueAfterFailure());
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(resolved.size(), 1);
    QVERIFY(host.runtime.setEnabled(host.installation(), false));
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Disabled);
    resolved.clear();
    request.resolve(customTrack(), ApiClient::PlaybackQuality::Standard,
                    ApiClient::SoundEffect::None);
    QTRY_COMPARE(failed.size(), 2);
    QCOMPARE(resolved.size(), 0);
}
void PluginRuntimeTest::audioSourceErrorsCancellationAndValidation()
{
    Host host;
    const auto source = sourcePackage(host, R"(
      c.player.audioSource.registerResolver('invalid', {order:1,resolve:function(){return {urls:['javascript:bad']};}});
      c.player.audioSource.registerResolver('throws', {order:2,resolve:function(){throw 'primitive error';}});
      c.player.audioSource.registerResolver('rejects', {order:3,resolve:function(){return Promise.reject('rejected');}});
      c.player.audioSource.registerResolver('good', {order:4,resolve:function(){return 'https://audio.test/good';}});
      c.player.audioSource.registerResolver('patch', {order:10,transform:function(){return {quality:'higher'};}});
      c.player.audioSource.registerResolver('badPatch', {order:11,transform:function(){return {quality:'invented'};}});
      c.player.audioSource.registerResolver('keep', {order:12,transform:function(){return null;}});
      c.registerCommand('pending',function(){return c.player.audioSource.registerResolver('wait',{order:0,resolve:function(){return new Promise(function(resolve){c.setTimer('late',50,false,function(){resolve('https://audio.test/stale');});});}})!==null;});
      c.registerCommand('replaceWait',function(){return c.player.audioSource.registerResolver('wait',{order:0,resolve:function(){return 'https://audio.test/replacement';}})!==null;});
      c.registerCommand('never',function(){return c.player.audioSource.registerResolver('wait',{order:0,resolve:function(){return new Promise(function(){});}})!==null;});
    )");
    auto *scope = installSource(host, source);
    QVERIFY(scope);
    QVERIFY(!scope->player()->audioSource()->registerResolver(
        "bad", host.engine.evaluate("({position:'catalog',resolve:function(){}})")));
    QSignalSpy errors(scope, &EchoPluginContext::callbackFailed);
    MembershipService membership(host.api, nullptr);
    PlaybackSourceRequest request(host.api, membership, nullptr, nullptr,
                                  host.playback.sourceExtensions());
    QSignalSpy resolved(&request, &PlaybackSourceRequest::resolved);
    request.resolve(customTrack(), ApiClient::PlaybackQuality::Standard,
                    ApiClient::SoundEffect::None);
    QTRY_COMPARE(resolved.size(), 1);
    QCOMPARE(resolvedSource(resolved).value("quality").toString(), "higher");
    QVERIFY(errors.size() >= 3);
    auto *pending = scope->executeCommand("pending");
    auto root = host.engine.newQObject(pending);
    QTRY_VERIFY(pending->finished());
    resolved.clear();
    request.resolve(customTrack(), ApiClient::PlaybackQuality::Standard,
                    ApiClient::SoundEffect::None);
    QTest::qWait(10);
    request.cancel();
    QTest::qWait(80);
    QCOMPARE(resolved.size(), 0);
    // Replacing the lease cancels an in-flight callback; the job skips the old
    // contribution rather than accepting its late Promise or a new revision.
    request.resolve(customTrack(), ApiClient::PlaybackQuality::Standard,
                    ApiClient::SoundEffect::None);
    QTest::qWait(5);
    auto *replacement = scope->executeCommand("replaceWait");
    auto replacementRoot = host.engine.newQObject(replacement);
    QTRY_VERIFY(replacement->finished());
    QTRY_COMPARE(resolved.size(), 1);
    QCOMPARE(resolvedSource(resolved).value("urls").toArray().first().toString(),
             "https://audio.test/good");
    resolved.clear();
    auto *never = scope->executeCommand("never");
    auto neverRoot = host.engine.newQObject(never);
    QTRY_VERIFY(never->finished());
    request.resolve(customTrack(), ApiClient::PlaybackQuality::Standard,
                    ApiClient::SoundEffect::None);
    QTRY_COMPARE_WITH_TIMEOUT(resolved.size(), 1, 5000);
    QVERIFY(std::any_of(errors.begin(), errors.end(), [](const auto &row) {
        return row.first().toString().contains("timed out");
    }));
}
void PluginRuntimeTest::pluginQueuesDriveRealPlaybackAndRejectStaleCommands()
{
    Host host;
    const auto url = quietWav(host);
    const auto source = sourcePackage(
        host,
        "c.player.audioSource.registerResolver('file',{resolve:function(v){c.ui.setState({"
        "preloading:v.preloading,forceReload:v.forceReload,sourceId:v.track.sourceId});return " +
            QJsonDocument(QJsonArray{url}).toJson(QJsonDocument::Compact).mid(1).chopped(1) +
            ";}});");
    auto *scope = installSource(host, source);
    QVERIFY(scope);
    QTRY_VERIFY_WITH_TIMEOUT(host.playback.engineReady(), 15000);
    auto *playlist = scope->playlist();
    QList<QJSValue> roots;
    const auto keep = [&](EchoPluginOperation *operation) {
        roots.append(host.engine.newQObject(operation));
        return operation;
    };
    const QVariantList songs{
        QVariantMap{{"id", "one"},
                    {"title", "One"},
                    {"sourceId", "station1"},
                    {"data", QVariantMap{{"station", 1}}}},
        QVariantMap{{"id", "two"}, {"title", "Two"}, {"sourceId", "station2"}}};
    auto *op = keep(playlist->replace("radio", "Radio", songs));
    QTRY_COMPARE(op->state(), EchoPluginOperation::Succeeded);
    const auto key = "plugin/test.runtime/radio";
    auto *model = host.playback.queueCatalog()->modelForKey(key);
    QVERIFY(model);
    QCOMPARE(model->rowCount(), 2);
    QVERIFY(host.playback.queueCatalog()->activeKey() != key);
    op = keep(playlist->play(key, 0));
    QTRY_COMPARE(op->state(), EchoPluginOperation::Succeeded);
    QTRY_VERIFY_WITH_TIMEOUT(host.playback.playing(), 15000);
    QCOMPARE(host.playback.title(), "One");
    QCOMPARE(host.playback.sourceKind(), "plugin");
    QTRY_VERIFY(host.playback.positionMs() > 0);
    QVERIFY(scope->ui()->state().value("sourceId").toString().startsWith("test.runtime/station"));
    QTRY_VERIFY_WITH_TIMEOUT(scope->ui()->state().value("preloading").toBool(), 15000);
    QVERIFY(!model->tracks().first().toDomain().contains("url"));
    const auto revision = QString::number(model->revision());
    op = keep(playlist->move(key, revision, 0, 1));
    model->append({customTrack()}, false);
    QTRY_COMPARE(op->state(), EchoPluginOperation::Failed);
    QCOMPARE(op->errorCode(), "StaleQueue");
    const auto rows = playlist->tracks(key, 0, 1);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().toMap().value("data").toMap().value("station").toInt(), 1);
    const auto roundTrip = playlist->tracks(key, 0, 2);
    op = keep(playlist->append(key, roundTrip));
    QTRY_COMPARE(op->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(model->rowCount(), 3);
    op = keep(playlist->append(key, {QVariantMap{{"id", "next"}, {"sourceId", "next"}}}, "next"));
    QTRY_COMPARE(op->state(), EchoPluginOperation::Succeeded);
    QVERIFY(!model->nextKeys().isEmpty());
    // Arbitrary host/source keys cannot be overwritten through replace().
    op = keep(playlist->replace("fm/personal", "Bad", songs));
    QCOMPARE(op->state(), EchoPluginOperation::Failed);
    // Existing host queues are shared, not copied into a plugin-owned catalog.
    op = keep(playlist->replace("other", "Other", songs));
    QTRY_COMPARE(op->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(host.playback.title(), "One");
    QCOMPARE(host.playback.queueCatalog()->activeKey(), key);
    op = keep(playlist->clear(key, QString::number(model->revision())));
    QTRY_COMPARE(op->state(), EchoPluginOperation::Succeeded);
    QVERIFY(!host.playback.hasTrack());
    QCOMPARE(host.playback.lyrics()->rowCount(), 0);
    // Provider unload cancels pending source work and queued mutations.
    op = keep(playlist->replace("cancel", "Cancel", songs));
    scope->beginRetire();
    QTRY_COMPARE(op->state(), EchoPluginOperation::Canceled);
}

void PluginRuntimeTest::audioSourceTransformReturnSemantics()
{
    Host host;
    auto *scope = installSource(host, sourcePackage(host, R"(
      c.ui.setState({mode:'keep'});
      c.player.audioSource.registerResolver('edit', {transform:function(v){
        if(c.ui.state.mode==='keep')return null;
        if(c.ui.state.mode==='reject')return false;
        if(c.ui.state.mode==='url')return 'https://audio.test/new';
        if(c.ui.state.mode==='patch-url')return {url:'https://audio.test/patched'};
        if(c.ui.state.mode==='invalid')return {sources:[{url:'https://audio.test/new',audioTrackId:-2}]};
        return {loudness:{gain:3},noticeCode:'changed'};
      }});
    )"));
    QVERIFY(scope);
    const QJsonObject original{{"urls", QJsonArray{"https://audio.test/old"}},
                               {"sources", QJsonArray{QJsonObject{{"url", "https://audio.test/old"},
                                                                  {"audioTrackId", 2}}}},
                               {"quality", "flac"},
                               {"gain", 1},
                               {"source_kind", "catalog"}};
    for (const auto &mode : {"keep", "reject", "url", "patch-url", "invalid", "patch"}) {
        scope->ui()->setState({{"mode", mode}});
        bool finished = false;
        QJsonObject result;
        QObject owner;
        host.playback.sourceExtensions()->transform("catalog", {}, original, &owner,
                                                    [&](QJsonObject value) {
                                                        result = value;
                                                        finished = true;
                                                    });
        QTRY_VERIFY(finished);
        if (QString(mode) == "reject")
            QVERIFY(result.isEmpty());
        else if (QString(mode) == "keep" || QString(mode) == "invalid")
            QCOMPARE(result, original);
        else if (QString(mode) == "url" || QString(mode) == "patch-url") {
            QCOMPARE(result.value("urls").toArray().size(), 1);
            QVERIFY(result.value("urls").toArray().first().toString() != "https://audio.test/old");
            QCOMPARE(PlaybackSourceExtensions::audioTrack(
                         result, result.value("urls").toArray().first().toString()),
                     0);
            QCOMPARE(result.value("quality").toString(), "flac");
        } else {
            QCOMPARE(result.value("gain").toInt(), 3);
            QCOMPARE(result.value("notice_code").toString(), "changed");
        }
    }
}

void PluginRuntimeTest::audioSourceCancellationAbortsLinkedTransport()
{
    Host host;
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QPointer<QTcpSocket> socket;
    QObject::connect(&server, &QTcpServer::newConnection, &server,
                     [&] { socket = server.nextPendingConnection(); });
    const auto url = QString("http://127.0.0.1:%1/pending").arg(server.serverPort());
    const auto source = sourcePackage(
        host,
        "c.player.audioSource.registerResolver('io',{resolve:function(v){var "
        "op=c.net.request({url:'" +
            url.toUtf8() +
            "'});"
            "if(!v.request.track(op))throw new Error('request tracking failed');"
            "v.request.canceledChanged.connect(function(){c.ui.setState({canceled:op.state===4,"
            "invalidated:v.request.canceled});});"
            "return new Promise(function(){});}});",
        {"audioSource", "unrestrictedNetwork"});
    auto *scope = installSource(host, source);
    QVERIFY(scope);
    MembershipService membership(host.api, nullptr);
    PlaybackSourceRequest request(host.api, membership, nullptr, nullptr,
                                  host.playback.sourceExtensions());
    QSignalSpy resolved(&request, &PlaybackSourceRequest::resolved);
    request.resolve(customTrack(), ApiClient::PlaybackQuality::Standard,
                    ApiClient::SoundEffect::None);
    QTRY_VERIFY(socket);
    request.cancel();
    QTRY_VERIFY(scope->ui()->state().value("canceled").toBool());
    QTRY_VERIFY(scope->ui()->state().value("invalidated").toBool());
    QTRY_COMPARE(socket->state(), QAbstractSocket::UnconnectedState);
    QCOMPARE(resolved.size(), 0);
    socket->deleteLater();
}
