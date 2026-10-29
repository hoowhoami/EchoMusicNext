#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(EchoMusic_ThemesPlugin)
#include "EchoPluginPlayer.h"
#include "../../services/settings/SettingsKeys.h"
#include "plugins/PluginRuntimeTestFixture.h"

namespace
{
QJsonObject lyricDocument()
{
    const QJsonObject word{
        {"text", "和声👩🏽‍🚀"}, {"start_ms", 1100}, {"duration_ms", 500}};
    const QJsonObject vocal{{"text", "和声👩🏽‍🚀"},
                            {"start_ms", 1100},
                            {"duration_ms", 500},
                            {"words", QJsonArray{word}},
                            {"translation", "Harmony"},
                            {"romanization", "hé shēng"},
                            {"agent_id", "v2"}};
    const QJsonObject line{{"text", "主唱"},
                           {"start_ms", 1000},
                           {"duration_ms", 2000},
                           {"words", QJsonArray{}},
                           {"translation", "Lead"},
                           {"id", "l1"},
                           {"agent_id", "v1"},
                           {"song_part", "Chorus"},
                           {"block_index", 1},
                           {"is_chorus", true},
                           {"background_vocals", QJsonArray{vocal}}};
    return {
        {"offset_ms", -100},
        {"scrollable", true},
        {"lines", QJsonArray{line}},
        {"source_format", "ttml"},
        {"timing_profile", "ttml-media"},
        {"agents",
         QJsonObject{{"v1", QJsonObject{{"id", "v1"}, {"name", "Alice"}, {"type", "person"}}}}}};
}
QByteArray taggedFlac(const QByteArray &lyrics)
{
    QFile file(QFINDTESTDATA("../../crates/echo-audio/tests/fixtures/embedded-metadata.flac"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto original = file.readAll();
    if (!original.startsWith("fLaC"))
        return {};
    auto result = original.first(4);
    qsizetype at = 4;
    bool replaced = false;
    while (at + 4 <= original.size()) {
        const auto header = original.mid(at, 4);
        const auto length = (quint32(quint8(header[1])) << 16) | (quint32(quint8(header[2])) << 8) |
                            quint8(header[3]);
        if (at + 4 + length > original.size())
            return {};
        if ((quint8(header[0]) & 0x7f) == 4) {
            QByteArray comments;
            QDataStream stream(&comments, QIODevice::WriteOnly);
            stream.setByteOrder(QDataStream::LittleEndian);
            const QByteArray vendor("EchoMusic real TTML tag regression");
            stream << quint32(vendor.size());
            stream.writeRawData(vendor.data(), vendor.size());
            stream << quint32(2);
            for (const auto &value :
                 {QByteArray("TITLE=Kept title"), QByteArray("LYRICS=") + lyrics}) {
                stream << quint32(value.size());
                stream.writeRawData(value.data(), value.size());
            }
            result += header.first(1);
            for (const auto shift : {16, 8, 0})
                result += char((comments.size() >> shift) & 255);
            result += comments;
            replaced = true;
        } else {
            result += original.mid(at, 4 + length);
        }
        at += 4 + length;
        if ((quint8(header[0]) & 0x80) != 0)
            break;
    }
    return replaced ? result + original.mid(at) : QByteArray{};
}
PlaybackTrack installTrack(Host &host, const QString &id, const QByteArray &lyrics)
{
    PlaybackTrack track;
    track.id = id;
    track.localPath = host.temporary.filePath(id + ".flac");
    track.title = id;
    write(track.localPath, taggedFlac(lyrics));
    // A real remembered source avoids network lookup and exercises the same
    // automatic embedded-source activation as production after its tag arrives.
    const auto key = SettingsKeys::Lyrics::SourcesPrefix +
                     QString::fromLatin1(QCryptographicHash::hash(track.stableKey().toUtf8(),
                                                                  QCryptographicHash::Sha256)
                                             .toHex());
    host.settings.setValue(key, "embedded");
    return track;
}
} // namespace

class LyricMetadataTest : public QObject
{
    Q_OBJECT
private slots:
    void explicitMetadataAndBackgroundTimesReachTheRealReadModel();
    void invalidNestedMetadataIsAtomicAndDoesNotReplaceThePreviousSong();
    void actualHostInfoAndReadModelExposeOnlyValidatedSourceFacts();
    void backgroundConversionPreservesTimingAndRetiredConversionsCannotReplaceLyrics();
    void realAudioTagWorkerPublishesTtmlAndItsExplicitBackgroundVoice();
    void explicitUnsupportedAudioTagReportsAnErrorWithoutPublishingXml();
    void retiredTrackAndSessionCannotDeliverOldEmbeddedMetadata();
    void metadataBudgetsAndMissingFactsAreValidatedAtomically();
};

void LyricMetadataTest::explicitMetadataAndBackgroundTimesReachTheRealReadModel()
{
    LyricModel model(nullptr);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    EchoPluginReadModel read(model, nullptr);
    QVERIFY(model.replace(lyricDocument()));
    const auto row = read.row(0);
    QCOMPARE(row.value("startMs").toLongLong(), 900);
    QCOMPARE(row.value("id").toString(), QString("l1"));
    QCOMPARE(row.value("agentId").toString(), QString("v1"));
    QCOMPARE(row.value("songPart").toString(), QString("Chorus"));
    QCOMPARE(row.value("blockIndex").toInt(), 1);
    QCOMPARE(row.value("isChorus").toBool(), true);
    const auto vocals = row.value("backgroundVocals").toList();
    QCOMPARE(vocals.size(), 1);
    const auto vocal = vocals.first().toMap();
    QCOMPARE(vocal.value("lineText").toString(), QString("和声👩🏽‍🚀"));
    QCOMPARE(vocal.value("startMs").toLongLong(), 1000);
    QCOMPARE(vocal.value("durationMs").toLongLong(), 500);
    QCOMPARE(vocal.value("words").toList().first().toMap().value("start_ms").toLongLong(), 1100);
    QCOMPARE(vocal.value("agentId").toString(), QString("v2"));
    QCOMPARE(vocal.value("translation").toString(), QString("Harmony"));
    model.clear();
    QCOMPARE(read.rowCount(), 0);
}
void LyricMetadataTest::invalidNestedMetadataIsAtomicAndDoesNotReplaceThePreviousSong()
{
    LyricModel model(nullptr);
    QVERIFY(model.replace(lyricDocument()));
    const auto previous = model.lineAt(0);
    for (const auto &patch : QList<QJsonObject>{{{"text", 12}},
                                                {{"start_ms", -1}},
                                                {{"duration_ms", 1.5}},
                                                {{"words", "invalid"}},
                                                {{"translation", QJsonArray{}}},
                                                {{"agent_id", QJsonArray{}}}}) {
        auto document = lyricDocument();
        auto line = document["lines"].toArray()[0].toObject();
        auto vocal = line["background_vocals"].toArray()[0].toObject();
        for (auto it = patch.begin(); it != patch.end(); ++it)
            vocal.insert(it.key(), it.value());
        line.insert("background_vocals", QJsonArray{vocal});
        document.insert("lines", QJsonArray{line});
        QVERIFY2(!model.replace(document),
                 qPrintable(QString::fromUtf8(QJsonDocument(patch).toJson())));
        QCOMPARE(model.lineAt(0), previous);
    }
}
void LyricMetadataTest::actualHostInfoAndReadModelExposeOnlyValidatedSourceFacts()
{
    Host host;
    host.runtime.importDirectory(
        package(host.temporary.path(), "export function activate(ctx) {}\n"));
    QString installation;
    QTRY_VERIFY(!(installation = host.runtime.installationForId("test.runtime")).isEmpty());
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *scope = host.runtime.context(installation);
    QVERIFY(host.playback.lyricText()->replace(lyricDocument(), "fixture"));
    const auto info = scope->player()->lyricsInfo();
    QCOMPARE(info.value("sourceFormat").toString(), QString("ttml"));
    QCOMPARE(info.value("timingProfile").toString(), QString("ttml-media"));
    QCOMPARE(info.value("hasBackgroundVocals").toBool(), true);
    QCOMPARE(info.value("agents").toMap().value("v1").toMap().value("name").toString(),
             QString("Alice"));
    const auto row = scope->player()->lyrics()->row(0);
    QCOMPARE(row.value("agentId").toString(), QString("v1"));
    QCOMPARE(row.value("backgroundVocals").toList().first().toMap().value("startMs").toLongLong(),
             1000);
    host.playback.lyricText()->clear();
    QCOMPARE(scope->player()->lyrics()->rowCount(), 0);
    QVERIFY(!scope->player()->lyricsInfo().contains("agents"));
}
void LyricMetadataTest::
    backgroundConversionPreservesTimingAndRetiredConversionsCannotReplaceLyrics()
{
    Host host;
    auto document = lyricDocument();
    auto line = document["lines"].toArray()[0].toObject();
    auto vocal = line["background_vocals"].toArray()[0].toObject();
    vocal.insert("text", "和聲👩🏽‍🚀");
    auto word = vocal["words"].toArray()[0].toObject();
    word.insert("text", "和聲👩🏽‍🚀");
    vocal.insert("words", QJsonArray{word});
    vocal.insert(
        "alternate_texts",
        QJsonArray{QJsonObject{{"role", "translation"}, {"lang", "zh-Hant"}, {"text", "和聲"}}});
    line.insert("background_vocals", QJsonArray{vocal});
    document.insert("lines", QJsonArray{line});
    auto *controller = host.playback.lyricText();
    QVERIFY(controller->replace(document, "fixture"));
    controller->setConversion("simplified");
    QTRY_COMPARE(host.playback.lyrics()
                     ->lineAt(0)
                     .value("backgroundVocals")
                     .toList()
                     .first()
                     .toMap()
                     .value("lineText")
                     .toString(),
                 QString("和声👩🏽‍🚀"));
    auto converted =
        host.playback.lyrics()->lineAt(0).value("backgroundVocals").toList().first().toMap();
    QCOMPARE(converted.value("startMs").toLongLong(), 1000);
    QCOMPARE(converted.value("words").toList().first().toMap().value("start_ms").toLongLong(),
             1100);
    QCOMPARE(converted.value("words").toList().first().toMap().value("text").toString(),
             QString("和声👩🏽‍🚀"));
    QCOMPARE(converted.value("alternateTexts").toList().first().toMap().value("text").toString(),
             QString("和声"));
    controller->setConversion("traditional");
    controller->clear();
    QTest::qWait(50);
    QCOMPARE(host.playback.lyrics()->rowCount(), 0);
    QVERIFY(host.playback.lyrics()->metadata().isEmpty());
}

void LyricMetadataTest::realAudioTagWorkerPublishesTtmlAndItsExplicitBackgroundVoice()
{
    Host host;
    host.playback.setVolume(0);
    QTRY_VERIFY_WITH_TIMEOUT(host.playback.engineReady(), 15000);
    QFile source(QFINDTESTDATA("../fixtures/lyrics/ttml/media.ttml"));
    QVERIFY(source.open(QIODevice::ReadOnly));
    // A valid >256KiB tag proves the original embedded-tag truncation cannot
    // accidentally pass a parser-only/model-only test.
    auto xml = source.readAll();
    const auto close = xml.indexOf("</tt>");
    QVERIFY(close > 0);
    xml.insert(close, "<!--" + QByteArray(300 * 1024, 'a') + "-->");
    const auto track = installTrack(host, "tagged-ttml", xml);
    QVERIFY(QFileInfo(track.localPath).size() > xml.size());
    host.playback.playTracks({track}, 0, "TTML", "ttml-native-tag");
    QTRY_COMPARE_WITH_TIMEOUT(host.playback.lyrics()->metadata().value("sourceFormat").toString(),
                              QString("ttml"), 15000);
    QCOMPARE(host.playback.lyricSource()->currentId(), QString("embedded"));
    QCOMPARE(host.playback.title(), QString("Kept title"));
    QVERIFY(!host.playback.cover().isEmpty());
    const auto row = host.playback.lyrics()->lineAt(0);
    QCOMPARE(row.value("lineText").toString(), QString("甲👩🏽‍🚀"));
    QCOMPARE(row.value("startMs").toLongLong(), 10000);
    const auto voice = row.value("backgroundVocals").toList().first().toMap();
    QCOMPARE(voice.value("lineText").toString(), QString("和声"));
    QCOMPARE(voice.value("startMs").toLongLong(), 11000);
    QCOMPARE(voice.value("translation").toString(), QString("Harmony"));
    QCOMPARE(voice.value("agentId").toString(), QString("back"));
    QVERIFY(host.playback.lyricSource()->embeddedError().isEmpty());
}

void LyricMetadataTest::explicitUnsupportedAudioTagReportsAnErrorWithoutPublishingXml()
{
    Host host;
    host.playback.setVolume(0);
    QTRY_VERIFY_WITH_TIMEOUT(host.playback.engineReady(), 15000);
    const auto track =
        installTrack(host, "unsupported-ttml",
                     "<tt xmlns='http://www.w3.org/ns/ttml'><body><div timeContainer='seq'><p "
                     "begin='0s' dur='1s'>Unusable</p></div></body></tt>");
    host.playback.playTracks({track}, 0, "TTML", "ttml-unsupported");
    QTRY_VERIFY_WITH_TIMEOUT(!host.playback.lyricSource()->embeddedError().isEmpty(), 15000);
    QCOMPARE(host.playback.lyricSource()->state(), QString("error"));
    QCOMPARE(host.playback.lyrics()->rowCount(), 0);
    QVERIFY(!host.playback.lyricSource()->errorMessage().isEmpty());
    QCOMPARE(host.playback.title(), QString("Kept title"));
    QVERIFY(!host.playback.cover().isEmpty());
}

void LyricMetadataTest::retiredTrackAndSessionCannotDeliverOldEmbeddedMetadata()
{
    Host host;
    host.playback.setVolume(0);
    QTRY_VERIFY_WITH_TIMEOUT(host.playback.engineReady(), 15000);
    const auto old = installTrack(
        host, "old-ttml",
        "<tt xmlns='http://www.w3.org/ns/ttml'><body><p begin='0s' end='1s'>OLD</p></body></tt>");
    const auto current = installTrack(host, "current-ttml",
                                      "<tt xmlns='http://www.w3.org/ns/ttml'><body><p begin='0s' "
                                      "end='1s'>CURRENT</p></body></tt>");
    host.playback.playTracks({old}, 0, "Old", "old-source");
    host.playback.playTracks({current}, 0, "Current", "current-source");
    QTRY_COMPARE_WITH_TIMEOUT(host.playback.lyrics()->lineAt(0).value("lineText").toString(),
                              QString("CURRENT"), 15000);
    // The public source controller also rejects an already parsed late old
    // track result, and rejects even the current key from an expired account.
    host.playback.lyricSource()->updateEmbedded(old.stableKey(), {}, {}, lyricDocument(),
                                                "Old error");
    QCOMPARE(host.playback.lyrics()->lineAt(0).value("lineText").toString(), QString("CURRENT"));
    QVERIFY(host.playback.lyricSource()->embeddedError().isEmpty());
    host.api.advanceSession();
    host.playback.lyricSource()->updateEmbedded(current.stableKey(), {}, {}, lyricDocument(),
                                                "Expired error");
    QCOMPARE(host.playback.lyricSource()->rowCount(), 0);
    QVERIFY(host.playback.lyricSource()->embeddedError().isEmpty());
}

void LyricMetadataTest::metadataBudgetsAndMissingFactsAreValidatedAtomically()
{
    LyricModel model(nullptr);
    QVERIFY(model.replace(lyricDocument()));
    const auto previous = model.lineAt(0);
    for (int kind = 0; kind < 5; ++kind) {
        auto document = lyricDocument();
        auto line = document["lines"].toArray()[0].toObject();
        const auto voice = line["background_vocals"].toArray()[0];
        if (kind == 0) {
            QJsonArray voices;
            for (int i = 0; i < 9; ++i)
                voices.append(voice);
            line.insert("background_vocals", voices);
        } else if (kind == 1) {
            line.insert("agent_id", QString(257, 'a'));
        } else if (kind == 2) {
            line.insert("is_chorus", "true");
        } else if (kind == 3) {
            auto entry = voice.toObject();
            entry.insert("text", QString(16385, 'a'));
            line.insert("background_vocals", QJsonArray{entry});
        } else {
            document.insert("agents", QJsonObject{{"v1", QJsonObject{{"id", "other"}}}});
        }
        document.insert("lines", QJsonArray{line});
        QVERIFY(!model.replace(document));
        QCOMPARE(model.lineAt(0), previous);
    }
    QVERIFY(model.replace({{"lines", QJsonArray{QJsonObject{{"text", "Plain"},
                                                            {"start_ms", 0},
                                                            {"duration_ms", 1000},
                                                            {"words", QJsonArray{}}}}}}));
    const auto row = model.lineAt(0);
    QVERIFY(!row.value("agentId").isValid());
    QVERIFY(!row.value("isChorus").isValid());
    QVERIFY(row.value("backgroundVocals").toList().isEmpty());
    QVERIFY(!model.metadata().contains("agents"));
}

QTEST_MAIN(LyricMetadataTest)
#include "LyricMetadataTest.moc"
