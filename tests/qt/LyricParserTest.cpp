#include "LyricParser.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSignalSpy>
#include <QtTest>
#include <memory>
using namespace EchoMusic;
class LyricParserTest final : public QObject
{
    Q_OBJECT
private slots:
    void conformance_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<QString>("corpus");
        for (const auto path : {ECHO_LYRIC_CONFORMANCE, ECHO_LYRIC_PRIVATE_CONFORMANCE}) {
            QFile file(path);
            if (!file.exists() && QString::fromUtf8(path) == ECHO_LYRIC_PRIVATE_CONFORMANCE) {
                QTest::newRow("local-captures-unavailable") << -1 << QString::fromUtf8(path);
                continue;
            }
            QVERIFY(file.open(QIODevice::ReadOnly));
            const auto cases = QJsonDocument::fromJson(file.readAll()).array();
            for (qsizetype i = 0; i < cases.size(); ++i)
                QTest::newRow(qPrintable(QString::number(i) + "-" +
                                         cases[i].toObject().value("name").toString()))
                    << int(i) << QString::fromUtf8(path);
        }
    }
    void conformance()
    {
        QFETCH(int, index);
        QFETCH(QString, corpus);
        if (index < 0)
            QSKIP("Private API captures are local-only and omitted from the public repository");
        QFile file(corpus);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto test = QJsonDocument::fromJson(file.readAll()).array()[index].toObject();
        const auto source = test.value("source").toString();
        const auto result =
            LyricParser::parse(source == "embedded"     ? LyricParser::Source::Embedded
                               : source == "downloaded" ? LyricParser::Source::Downloaded
                                                        : LyricParser::Source::Document,
                               test.value("input").toObject().toVariantMap());
        const auto expected = test.value("expected").toObject();
        if (expected.contains("error"))
            QCOMPARE(result.code, expected.value("error").toString());
        else {
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            QCOMPARE(result.lyrics, expected);
        }
    }
    void longDocumentsAndExpansionBudgets()
    {
        QString lrc;
        for (int i = 0; i < 10001; ++i)
            lrc += "[00:01.00]line-" + QString::number(i) + '\n';
        auto value =
            LyricParser::parse(LyricParser::Source::Downloaded,
                               {{"format", "lrc"},
                                {"encoding", "base64"},
                                {"content", QString::fromLatin1(lrc.toUtf8().toBase64())}});
        QVERIFY2(value.error.isEmpty(), qPrintable(value.error));
        QCOMPARE(value.lyrics["lines"].toArray().size(), 10001);
        value = LyricParser::parse(
            LyricParser::Source::Document,
            {{"format", "krc"},
             {"content", QString("[0,100000]") + QString("<0,10,0>字").repeated(2049)}});
        QVERIFY2(value.error.isEmpty(), qPrintable(value.error));
        QCOMPARE(value.lyrics["lines"].toArray()[0].toObject()["words"].toArray().size(), 2049);
        QString stamps;
        for (int i = 0; i < 20001; ++i)
            stamps += '[' + QString::number(i / 60) + ':' + QString::number(i % 60) + ']';
        value = LyricParser::parse(LyricParser::Source::Document,
                                   {{"format", "lrc"}, {"content", stamps + 'x'}});
        QCOMPARE(value.code, "resource_limit");
        stamps.clear();
        for (int i = 0; i < 1000; ++i)
            stamps += '[' + QString::number(i / 60) + ':' + QString::number(i % 60) + ']';
        value = LyricParser::parse(LyricParser::Source::Document,
                                   {{"format", "lrc"}, {"content", stamps + QString(20000, 'x')}});
        QCOMPARE(value.code, "resource_limit");
        value = LyricParser::parse(
            LyricParser::Source::Document,
            {{"format", "krc"}, {"content", "a3JjMQ=="}, {"encoding", "base64"}});
        QCOMPARE(value.code, "decode");
        value = LyricParser::parse(LyricParser::Source::Downloaded,
                                   {{"format", "lrc"},
                                    {"content", QByteArray::fromHex("ff").toBase64()},
                                    {"encoding", "base64"}});
        QCOMPARE(value.code, "decode");
        value = LyricParser::parse(
            LyricParser::Source::Downloaded,
            {{"format", "lrc"},
             {"content", QString::fromLatin1(QByteArray::fromHex("c3").toBase64())},
             {"encoding", "base64"}});
        QCOMPARE(value.code, "decode");
        auto compressed = qCompress(QByteArray(5 * 1024 * 1024, 'x')).mid(4);
        const unsigned char key[] = {0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47,
                                     0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69};
        for (qsizetype i = 0; i < compressed.size(); ++i)
            compressed[i] = char(uchar(compressed[i]) ^ key[i % 16]);
        value = LyricParser::parse(
            LyricParser::Source::Document,
            {{"format", "krc"},
             {"content", QString::fromLatin1((QByteArray("krc1") + compressed).toBase64())},
             {"encoding", "base64"}});
        QCOMPARE(value.code, "resource_limit");
        std::atomic_bool canceled{true};
        value = LyricParser::parse(LyricParser::Source::Document,
                                   {{"format", "lrc"}, {"content", lrc}}, &canceled);
        QCOMPARE(value.code, "Canceled");
    }
    void asyncOwnersAndBoundedQueue()
    {
        LyricParser parser;
        int completed = 0;
        auto owner = std::make_unique<QObject>();
        for (int i = 0; i < 8; ++i)
            parser.submit(LyricParser::Source::Document,
                          {{"format", "lrc"}, {"content", "[00:01]Hello"}}, owner.get(),
                          [&](auto) { ++completed; });
        bool busy = false;
        QObject owner2;
        parser.submit(LyricParser::Source::Document,
                      {{"format", "lrc"}, {"content", "[00:01]World"}}, &owner2,
                      [&](auto r) { busy = r.code == "Busy"; });
        owner.reset();
        QTRY_VERIFY(busy);
        QTest::qWait(30);
        QCOMPARE(completed, 0);
        bool done = false;
        parser.submit(LyricParser::Source::Document,
                      {{"format", "yrc"}, {"content", "[1000,1000](1000,1000,0)World"}}, &owner2,
                      [&](auto r) { done = r.error.isEmpty(); });
        QTRY_VERIFY(done);
    }
};
QTEST_GUILESS_MAIN(LyricParserTest)
#include "LyricParserTest.moc"
