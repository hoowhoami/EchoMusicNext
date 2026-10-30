#include "LyricParser.h"
#include "TtmlParser_p.h"
#include <QFile>
#include <QJsonArray>
#include <QtTest>
using namespace EchoMusic;
namespace
{
QString document(const QString &body)
{
    return "<tt xmlns=\"http://www.w3.org/ns/ttml\"><body>" + body + "</body></tt>";
}
LyricParser::Result parse(const QString &text)
{
    return LyricParser::parse(LyricParser::Source::Document,
                              {{"format", "ttml"}, {"content", text}});
}
QJsonObject firstLine(const LyricParser::Result &result)
{
    const auto lines = result.lyrics["lines"].toArray();
    return lines.isEmpty() ? QJsonObject{} : lines.first().toObject();
}
} // namespace
class LyricTtmlTest final : public QObject
{
    Q_OBJECT
private slots:
    void sharedDocumentAndEmbeddedProfiles_data()
    {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<QString>("profile");
        QTest::newRow("relative-media") << "media" << "ttml-media";
        QTest::newRow("explicit-apple") << "apple" << "apple-lyrics-absolute";
    }
    void sharedDocumentAndEmbeddedProfiles()
    {
        QFETCH(QString, fixture);
        QFETCH(QString, profile);
        QFile file(QString(ECHO_TTML_FIXTURES) + '/' + fixture + ".ttml");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto text = QString::fromUtf8(file.readAll());
        const auto embedded =
            LyricParser::parse(LyricParser::Source::Embedded, {{"content", text}});
        QVERIFY2(embedded.error.isEmpty(), qPrintable(embedded.error));
        const auto authored = parse(text);
        QVERIFY2(authored.error.isEmpty(), qPrintable(authored.error));
        QCOMPARE(authored.lyrics, embedded.lyrics);
        QCOMPARE(authored.lyrics["source_format"].toString(), "ttml");
        QCOMPARE(authored.lyrics["timing_profile"].toString(), profile);
        QCOMPARE(authored.lyrics["lines"].toArray().size(), 1);
        const auto line = firstLine(authored);
        QCOMPARE(line["start_ms"].toInteger(), 10000);
        QCOMPARE(line["words"].toArray()[0].toObject()["start_ms"].toInteger(), 10000);
        const auto background = line["background_vocals"].toArray()[0].toObject();
        QCOMPARE(background["start_ms"].toInteger(), 11000);
        QCOMPARE(background["words"].toArray()[0].toObject()["start_ms"].toInteger(), 11000);
        if (fixture == "media") {
            QCOMPARE(line["text"].toString(), QString::fromUtf8("甲👩🏽‍🚀"));
            QCOMPARE(line["duration_ms"].toInteger(), 4000);
            QCOMPARE(line["words"].toArray()[0].toObject()["duration_ms"].toInteger(), 1000);
            QCOMPARE(background["text"].toString(), QString::fromUtf8("和声"));
            QCOMPARE(background["translation"].toString(), "Harmony");
            QCOMPARE(background["agent_id"].toString(), "back");
            QCOMPARE(line["translation"].toString(), "Main");
            QCOMPARE(line["romanization"].toString(), QString::fromUtf8("jiǎ"));
            QCOMPARE(line["id"].toString(), "line-1");
            QCOMPARE(line["agent_id"].toString(), "lead");
            QCOMPARE(line["song_part"].toString(), "Chorus");
            QCOMPARE(line["is_chorus"].toBool(), true);
            QCOMPARE(line["block_index"].toInt(), 1);
            QCOMPARE(authored.lyrics["agents"].toObject()["lead"].toObject()["name"].toString(),
                     "Alice");
        }
    }
    void simultaneousMainRowsDoNotInventHarmony()
    {
        const auto result =
            parse(document("<p begin='1s' end='2s'>A</p><p begin='1s' end='2s'>B</p>"));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.lyrics["lines"].toArray().size(), 2);
        QVERIFY(!firstLine(result).contains("background_vocals"));
        QVERIFY(!firstLine(result).contains("is_chorus"));
    }
    void exactTimeAccumulatesBeforeRounding_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<qint64>("start");
        QTest::addColumn<qint64>("duration");
        QTest::newRow("parent-unit")
            << "<div begin='1m'><p begin='2.0005s' dur='1500ms'>A</p></div>" << qint64(62001)
            << qint64(1500);
        QTest::newRow("clock") << "<p begin='01:02:03.456' end='01:02:04.456'>B</p>"
                               << qint64(3723456) << qint64(1000);
        QTest::newRow("sum-before-round")
            << "<div begin='0.0005s'><p begin='0.0005s' dur='0.001s'>C</p></div>" << qint64(1)
            << qint64(1);
        QTest::newRow("eighteen-digits-down")
            << "<p begin='0.000499999999999999s' dur='1s'>C</p>" << qint64(0) << qint64(1000);
        QTest::newRow("eighteen-digits-up")
            << "<p begin='0.000500000000000000s' dur='1s'>C</p>" << qint64(1) << qint64(1000);
        QTest::newRow("large-whole-fraction")
            << "<p begin='9007199254740990.499999999999999999ms' end='9007199254740991ms'>C</p>"
            << qint64(9007199254740990LL) << qint64(1);
    }
    void exactTimeAccumulatesBeforeRounding()
    {
        QFETCH(QString, body);
        QFETCH(qint64, start);
        QFETCH(qint64, duration);
        const auto result = parse(document(body));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(firstLine(result)["start_ms"].toInteger(), start);
        QCOMPARE(firstLine(result)["duration_ms"].toInteger(), duration);
    }
    void unicodeSpaceEntitiesAndBreaksRetainDisplayText()
    {
        const auto result =
            parse(document(QString::fromUtf8("<p begin='0s' end='1s' xml:space='preserve'>  ffi "
                                             "&amp; العربية<br/>👩🏽‍🚀  </p>")));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(firstLine(result)["text"].toString(),
                 QString::fromUtf8("  ffi & العربية\n👩🏽‍🚀  "));
        QVERIFY(firstLine(result)["words"].toArray().isEmpty());
    }
    void invalidTtmlNeverFallsBackToPlainXml_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("malformed") << "<tt xmlns='http://www.w3.org/ns/ttml'><body><p></body></tt>";
        QTest::newRow("reversed") << document("<p begin='2s' end='1s'>A</p>");
        QTest::newRow("frame") << document("<p begin='1f' end='2s'>A</p>");
        QTest::newRow("tick") << document("<p begin='1t' end='2s'>A</p>");
        QTest::newRow("negative") << document("<p begin='-1s' end='2s'>A</p>");
        QTest::newRow("non-finite") << document("<p begin='NaNs' end='2s'>A</p>");
        QTest::newRow("overflow") << document(
            "<p begin='9007199254740992ms' end='9007199254740993ms'>A</p>");
        QTest::newRow("sequential")
            << document("<div timeContainer='seq'><p begin='0s' end='1s'>A</p></div>");
        QTest::newRow("missing-duration") << document("<p>A</p>");
        QTest::newRow("external-entity")
            << "<!DOCTYPE tt [<!ENTITY x SYSTEM 'file:///unread'>]><tt "
               "xmlns='http://www.w3.org/ns/ttml'><body><p begin='0s' end='1s'>&x;</p></body></tt>";
        QTest::newRow("frame-multiplier")
            << "<tt xmlns='http://www.w3.org/ns/ttml' "
               "xmlns:p='http://www.w3.org/ns/ttml#parameter' p:frameRate='30' "
               "p:frameRateMultiplier='1000 1001'><body><p begin='0s' end='1s'>A</p></body></tt>";
        QTest::newRow("tick-rate")
            << "<tt xmlns='http://www.w3.org/ns/ttml' "
               "xmlns:p='http://www.w3.org/ns/ttml#parameter' p:tickRate='1000'><body><p "
               "begin='0s' end='1s'>A</p></body></tt>";
    }
    void invalidTtmlNeverFallsBackToPlainXml()
    {
        QFETCH(QString, text);
        for (const auto source : {LyricParser::Source::Embedded, LyricParser::Source::Document}) {
            const auto result = LyricParser::parse(source, {{"format", "ttml"}, {"content", text}});
            QVERIFY2(!result.error.isEmpty(), qPrintable(text));
            QVERIFY(result.lyrics.isEmpty());
        }
    }
    void depthRowsAndTextBudgetsAreEnforced()
    {
        const QString row("<p begin='0s' end='1s'>A</p>");
        const QStringList invalid = {
            document(QString("<div>").repeated(33) + row + QString("</div>").repeated(33)),
            document("<p begin='0s' end='1s'>" + QString(16385, 'a') + "</p>"),
            document(row.repeated(8193)),
            document("<p begin='0s' end='1s'>" + QString(4 * 1024 * 1024, 'a') + "</p>")};
        for (const auto &text : invalid) {
            const auto result = parse(text);
            QVERIFY(!result.error.isEmpty());
            QVERIFY(result.lyrics.isEmpty());
        }
    }
    void aliasesResolveByNamespaceAndMarkupKeepsSpaces()
    {
        const auto result = parse(
            "<a:tt xmlns:a='http://www.w3.org/ns/ttml' "
            "xmlns:m='http://www.w3.org/ns/ttml#metadata'><a:body><a:p begin='0s' end='3s'><a:span "
            "begin='0s' end='1s'>Hello </a:span><a:span begin='1s' end='2s'>world</a:span><a:span "
            "m:role='x-bg' begin='1s' end='2s'>Ah</a:span></a:p></a:body></a:tt>");
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(firstLine(result)["text"].toString(), "Hello world");
        QCOMPARE(firstLine(result)["background_vocals"].toArray()[0].toObject()["text"].toString(),
                 "Ah");
        const auto preserved =
            parse(document("<p begin='0s' end='1s'><span>A </span> <span>B</span></p>"));
        QVERIFY2(preserved.error.isEmpty(), qPrintable(preserved.error));
        QCOMPARE(firstLine(preserved)["text"].toString(), "A B");
        const auto foreign = parse(document(
            "<p begin='0s' end='1s' xmlns:x='urn:unrecognized'>A<span x:role='x-bg'>B</span></p>"));
        QVERIFY2(foreign.error.isEmpty(), qPrintable(foreign.error));
        QVERIFY(!firstLine(foreign).contains("background_vocals"));
    }
    void cancellationDuringTokenizationNeverPublishesPartialRows()
    {
        int visits = 0;
        const auto result =
            LyricCodecs::parseTtml(document(QString("<p begin='0s' end='1s'>A</p>").repeated(200)),
                                   [&] { return ++visits >= 70; });
        QCOMPARE(result.code, "Canceled");
        QVERIFY(result.lyrics.isEmpty());
        QVERIFY(visits >= 70 && visits < 100);
    }
    void controlsDuplicateIdsAndRequiredFeaturesAreRejected()
    {
        QString nul = document("<p begin='0s' end='1s'>AB</p>");
        nul.replace("AB", QString('A') + QChar(0) + 'B');
        const QStringList invalid = {
            nul, document("<p begin='0s' end='1s'>&#0;</p>"),
            document("<p xml:id='same' begin='0s' end='1s'>A</p><p xml:id='same' begin='1s' "
                     "end='2s'>B</p>"),
            document("<p begin='0s' end='1s' repeatCount='2'>A</p>"),
            document("<p begin='0s' end='1s' ttm:role='x-bg' "
                     "xmlns:ttm='http://www.w3.org/ns/ttml#metadata'>A</p>")};
        for (const auto &text : invalid) {
            const auto result = parse(text);
            QVERIFY2(!result.error.isEmpty(), qPrintable(text));
            QVERIFY(result.lyrics.isEmpty());
        }
    }
    void alternateTextPreservesSpaceAndRejectsForeignStructure()
    {
        const auto result = parse(document(QString::fromUtf8(
            "<p begin='0s' end='1s' xmlns:m='http://www.w3.org/ns/ttml#metadata'>A<span "
            "m:role='x-translation' xml:space='preserve'>  訳  </span></p>")));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(firstLine(result)["alternate_texts"].toArray()[0].toObject()["text"].toString(),
                 QString::fromUtf8("  訳  "));
        const QStringList invalid = {
            "<span m:role='x-translation'><span><span m:role='x-bg'>Do not flatten "
            "voices</span></span></span>",
            "<span m:role='x-translation'><image src='file:///unread'/></span>",
            QString("<span m:role='x-translation'>A</span>").repeated(65)};
        for (const auto &body : invalid) {
            const auto rejected = parse(
                document("<p begin='0s' end='1s' xmlns:m='http://www.w3.org/ns/ttml#metadata'>A" +
                         body + "</p>"));
            QVERIFY2(!rejected.error.isEmpty(), qPrintable(body));
            QVERIFY(rejected.lyrics.isEmpty());
        }
    }
    void cancellationDoesNotPublishRows()
    {
        const std::atomic_bool canceled(true);
        const auto result = LyricParser::parse(
            LyricParser::Source::Document, {{"format", "ttml"}, {"content", "<tt/>"}}, &canceled);
        QCOMPARE(result.code, "Canceled");
        QVERIFY(result.lyrics.isEmpty());
    }
};
QTEST_GUILESS_MAIN(LyricTtmlTest)
#include "LyricTtmlTest.moc"
