#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "EchoPluginLyrics.h"
#include "LyricExtensions.h"

namespace
{
EchoPluginContext *lyricsPlugin(Host &host, const QString &id, const QByteArray &body)
{
    const auto source = pluginPackage(
        host.temporary.path(), id, "export function activate(c){" + body + "}",
        {{"capabilities", QJsonArray{"lyrics", "unrestrictedNetwork"}},
         {"requires",
          QJsonObject{{"hostVersion", ">=0.1.0"},
                      {"sdkVersion", ">=1.0.0"},
                      {"features", QJsonArray{"lifecycle", "lyrics.resolvers", "lyrics.write"}}}}});
    host.runtime.importDirectory(source);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000 &&
           host.runtime.state(host.runtime.installationForId(id)) != PluginRuntime::Active)
        QTest::qWait(1);
    return host.runtime.context(host.runtime.installationForId(id));
}
PlaybackTrack localLyricsTrack(Host &host, const QString &id)
{
    constexpr int rate = 8000;
    QByteArray wave;
    QDataStream data(&wave, QIODevice::WriteOnly);
    data.setByteOrder(QDataStream::LittleEndian);
    data.writeRawData("RIFF", 4);
    data << quint32(36 + rate * 40);
    data.writeRawData("WAVEfmt ", 8);
    data << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2)
         << quint16(2) << quint16(16);
    data.writeRawData("data", 4);
    data << quint32(rate * 40);
    wave.append(QByteArray(rate * 40, char(0)));
    const auto path = host.temporary.path() + '/' + id + ".wav";
    write(path, wave);
    PlaybackTrack track;
    track.id = id;
    track.localPath = path;
    track.durationMs = 20000;
    return track;
}
QString firstLine(LyricModel *model)
{
    return model->lineAt(0).value("lineText").toString();
}
int sourceRow(LyricSourceController *source, const QString &id)
{
    for (int i = 0; i < source->rowCount(); ++i)
        if (source->data(source->index(i), Qt::UserRole + 1).toString() == id)
            return i;
    return -1;
}
QVariantMap lyricDocument(const QString &text)
{
    return {{"format", "lrc"}, {"content", "[00:01]" + text}, {"source", "Writer"}};
}
} // namespace
void PluginRuntimeTest::lyricResolversFormatsAndManualPriority()
{
    Host host;
    auto *scope = lyricsPlugin(host, "lyric-formats", R"(
      c.ui.setState({format:'lrc',calls:0});
      c.lyrics.registerResolver('main',{order:10,match:function(v){return Promise.resolve(true);},resolve:function(v){
        var f=c.ui.state.format;c.ui.setState({format:f,calls:c.ui.state.calls+1});
        return {format:f,source:'Plugin lyrics',content:f==='lrc'?'[offset:125]\n[00:01]LRC':f==='krc'?'[1000,1000]<0,500,0>K<500,500,0>RC':'[1000,1000](1000,500,0)Y(1500,500,0)RC',translation:'[00:01]Translated',romanization:'[00:01]Romanized'};
      }});
    )");
    QVERIFY(scope);
    auto *source = host.playback.lyricSource();
    QAbstractItemModelTester rows(source, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const auto track = localLyricsTrack(host, "one");
    host.playback.playTracks({track}, 0, "Lyrics", "queue:manual");
    QTRY_VERIFY_WITH_TIMEOUT(host.playback.playing(), 15000);
    host.playback.ensureLyrics();
    QTRY_COMPARE(firstLine(host.playback.lyrics()), "LRC");
    QCOMPARE(source->currentId(), "plugin/lyric-formats/main");
    QCOMPARE(scope->lyrics()->snapshot().value("source").toString(), "Plugin lyrics");
    QVERIFY(host.playback.lyrics()->hasTranslation());
    QVERIFY(host.playback.lyrics()->hasRomanization());
    for (const auto &format : {"krc", "yrc"}) {
        scope->ui()->setState({{"format", format}, {"calls", 0}});
        source->reload();
        QTRY_COMPARE(firstLine(host.playback.lyrics()), QString(format).toUpper());
        const auto words = host.playback.lyrics()->lineAt(0).value("words").toList();
        QCOMPARE(words.size(), 2);
        QCOMPARE(words[1].toMap().value("start_ms").toInt(), 1500);
    }
    // Existing source/preview model carries the resolver result; applying it
    // persists a manual preference that both reload and writes must respect.
    source->select(sourceRow(source, "plugin/lyric-formats/main"));
    QTRY_COMPARE(firstLine(source->preview()), "YRC");
    source->applySelected();
    QVERIFY(source->manual());
    auto *blocked = scope->lyrics()->write(scope->player()->revision(), lyricDocument("Override"));
    auto root = host.engine.newQObject(blocked);
    QCOMPARE(blocked->state(), EchoPluginOperation::Failed);
    QCOMPARE(blocked->errorCode(), "ManualSelection");
    source->updateEmbedded(track.stableKey(), "Local", "Artist",
                           {{"scrollable", true},
                            {"offset_ms", 0},
                            {"lines", QJsonArray{QJsonObject{{"text", "Embedded"},
                                                             {"start_ms", 1000},
                                                             {"duration_ms", 1000},
                                                             {"words", QJsonArray{}}}}}});
    QVERIFY(sourceRow(source, "embedded") >= 0);
    source->select(sourceRow(source, "embedded"));
    QCOMPARE(firstLine(source->preview()), "Embedded");
    source->applySelected();
    QTRY_COMPARE(firstLine(host.playback.lyrics()), "Embedded");
    const auto calls = scope->ui()->state().value("calls").toInt();
    source->reload();
    QCOMPARE(scope->ui()->state().value("calls").toInt(), calls);
    QCOMPARE(firstLine(host.playback.lyrics()), "Embedded");
    source->restoreAuto();
    QTRY_VERIFY(!source->manual());
    QTRY_COMPARE(firstLine(host.playback.lyrics()), "YRC");
}
void PluginRuntimeTest::lyricWritesLayersCancellationAndRetirement()
{
    Host host;
    auto *base = lyricsPlugin(host, "lyric-base",
                              "c.lyrics.registerResolver('main',{resolve:function(){return "
                              "{format:'lrc',content:'[00:01]Base'};}});");
    auto *one = lyricsPlugin(host, "lyric-one", "");
    auto *two = lyricsPlugin(host, "lyric-two", "");
    QVERIFY(base && one && two);
    const auto track = localLyricsTrack(host, "one");
    host.playback.playTracks({track}, 0, "Lyrics", "queue:manual");
    QTRY_VERIFY_WITH_TIMEOUT(host.playback.playing(), 15000);
    host.playback.ensureLyrics();
    QTRY_COMPARE(firstLine(host.playback.lyrics()), "Base");
    QList<QJSValue> roots;
    const auto keep = [&](EchoPluginOperation *op) {
        roots.append(host.engine.newQObject(op));
        return op;
    };
    host.engine.evaluate("var lyricSnapshotCount=0;");
    auto *watch = one->lyrics()->onSnapshot(
        "watch", host.engine.evaluate("(function(){++lyricSnapshotCount;})"));
    QVERIFY(watch);
    QTRY_VERIFY(host.engine.globalObject().property("lyricSnapshotCount").toInt() > 0);
    auto revision = one->player()->revision();
    auto *op = keep(one->lyrics()->write(revision, lyricDocument("One")));
    QTRY_COMPARE(op->state(), EchoPluginOperation::Succeeded);
    QTRY_COMPARE(firstLine(host.playback.lyrics()), "One");
    QTRY_VERIFY(host.engine.globalObject().property("lyricSnapshotCount").toInt() > 1);
    watch->dispose();
    const auto deliveries = host.engine.globalObject().property("lyricSnapshotCount").toInt();
    op = keep(two->lyrics()->write(revision, lyricDocument("Two")));
    QTRY_COMPARE(op->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(firstLine(host.playback.lyrics()), "Two");
    QTest::qWait(1);
    QCOMPARE(host.engine.globalObject().property("lyricSnapshotCount").toInt(), deliveries);
    op = keep(two->lyrics()->write(revision, {{"format", "yrc"}, {"content", "invalid"}}));
    QTRY_COMPARE(op->state(), EchoPluginOperation::Failed);
    QCOMPARE(op->errorCode(), "InvalidLyrics");
    QCOMPARE(firstLine(host.playback.lyrics()), "Two");
    op = keep(two->lyrics()->clear(revision));
    QTRY_COMPARE(op->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(firstLine(host.playback.lyrics()), "One");
    op = keep(two->lyrics()->write(revision, {{"format", "lrc"}, {"content", ""}}));
    QTRY_COMPARE(op->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(host.playback.lyrics()->rowCount(), 0);
    QVERIFY(host.runtime.setEnabled(host.runtime.installationForId("lyric-two"), false));
    QTRY_COMPARE(firstLine(host.playback.lyrics()), "One");
    // Stop retains the displayed lyrics and their owner, so disabling a writer
    // after Stop still restores the accepted resolver result.
    host.playback.stop();
    QVERIFY(host.runtime.setEnabled(host.runtime.installationForId("lyric-one"), false));
    QTRY_COMPARE(firstLine(host.playback.lyrics()), "Base");
    revision = base->player()->revision();
    op = keep(base->lyrics()->write(revision, lyricDocument("Late")));
    const auto next = localLyricsTrack(host, "two");
    host.playback.playTracks({next}, 0, "Lyrics", "queue:manual");
    QTRY_COMPARE(op->state(), EchoPluginOperation::Canceled);
    op = keep(base->lyrics()->write(revision, lyricDocument("Stale")));
    QCOMPARE(op->errorCode(), "StaleTrack");
    host.playback.ensureLyrics();
    QTRY_COMPARE(firstLine(host.playback.lyrics()), "Base");
    // Both the production lyric model and snapshot must lose provider content
    // on retirement, without keeping a dangling copy in the playback service.
    QVERIFY(host.runtime.setEnabled(host.runtime.installationForId("lyric-base"), false));
    QTRY_VERIFY(firstLine(host.playback.lyrics()) != "Base");
}
void PluginRuntimeTest::lyricResolverCancellationAbortsLinkedTransport()
{
    Host host;
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QList<QPointer<QTcpSocket>> sockets;
    connect(&server, &QTcpServer::newConnection, &server,
            [&] { sockets.append(server.nextPendingConnection()); });
    const auto url = QString("http://127.0.0.1:%1/pending").arg(server.serverPort());
    auto *scope = lyricsPlugin(
        host, "lyric-cancel",
        "c.lyrics.registerResolver('io',{resolve:function(v){var op=c.net.request({url:'" +
            url.toUtf8() +
            "'});v.request.track(op);v.request.canceledChanged.connect(function(){c.ui.setState({"
            "canceled:op.state===4,invalidated:v.request.canceled});});return new "
            "Promise(function(){});}});");
    QVERIFY(scope);
    auto *source = host.playback.lyricSource();
    PlaybackTrack track;
    track.id = "one";
    source->load(track);
    QTRY_COMPARE(sockets.size(), 1);
    track.id = "two";
    source->load(track);
    QTRY_VERIFY(scope->ui()->state().value("canceled").toBool());
    QTRY_VERIFY(scope->ui()->state().value("invalidated").toBool());
    QTRY_COMPARE(sockets[0]->state(), QAbstractSocket::UnconnectedState);
    QTRY_COMPARE(sockets.size(), 2);
    QVERIFY(QMetaObject::invokeMethod(&host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QTRY_COMPARE(sockets[1]->state(), QAbstractSocket::UnconnectedState);
    track.id = "three";
    source->load(track);
    QTRY_COMPARE(sockets.size(), 3);
    QVERIFY(host.runtime.setEnabled(host.runtime.installationForId("lyric-cancel"), false));
    QTRY_COMPARE(sockets[2]->state(), QAbstractSocket::UnconnectedState);
    QTRY_VERIFY(!source->loading());
    for (auto socket : sockets)
        if (socket)
            socket->deleteLater();
}
void PluginRuntimeTest::lyricResolverErrorsContinueAndLateResultsAreDiscarded()
{
    Host host;
    auto *scope = lyricsPlugin(host, "lyric-errors", R"(
      c.lyrics.registerResolver('throws',{order:0,resolve:function(){throw new Error('bad');}});
      c.lyrics.registerResolver('invalid',{order:1,resolve:function(){return {format:'yrc',content:'bad'};}});
      c.lyrics.registerResolver('never',{order:2,resolve:function(){return new Promise(function(){});}});
      c.lyrics.registerResolver('good',{order:3,resolve:function(v){return {format:'lrc',content:'[00:01]'+v.track.id};}});
    )");
    QVERIFY(scope);
    QSignalSpy errors(scope, &EchoPluginContext::callbackFailed);
    auto *source = host.playback.lyricSource();
    PlaybackTrack track;
    track.id = "one";
    source->load(track);
    QTRY_COMPARE_WITH_TIMEOUT(source->currentId(), "plugin/lyric-errors/good", 7000);
    QCOMPARE(firstLine(source->preview()), "one");
    QVERIFY(errors.size() >= 3);
    auto *lease = scope->lyrics()->registerResolver(
        "late", host.engine.evaluate("({order:-1,resolve:function(v){return new "
                                     "Promise(function(resolve){pendingLyric=resolve;});}})"));
    QVERIFY(lease);
    QTRY_VERIFY(host.engine.globalObject().property("pendingLyric").isCallable());
    source->clear();
    host.engine.evaluate(
        "if(typeof pendingLyric==='function') pendingLyric({format:'lrc',content:'[00:01]Late'});");
    QTest::qWait(30);
    QCOMPARE(source->currentId(), QString());
    QCOMPARE(source->preview()->rowCount(), 0);
    QCOMPARE(source->rowCount(), 0);
}
