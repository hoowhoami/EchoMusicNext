#include "LyricModel.h"
#include <QAbstractItemModelTester>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

using EchoMusic::LyricModel;
namespace
{
QJsonObject document()
{
    const QJsonObject harmony{{"text", "和声👩🏽‍🚀"},
                              {"start_ms", 1100},
                              {"duration_ms", 500},
                              {"words", QJsonArray{QJsonObject{{"text", "和声👩🏽‍🚀"},
                                                               {"start_ms", 1100},
                                                               {"duration_ms", 500}}}},
                              {"agent_id", "harmony"}};
    return {
        {"offset_ms", -120},
        {"scrollable", true},
        {"source_format", "ttml"},
        {"timing_profile", "ttml-media"},
        {"lines", QJsonArray{QJsonObject{{"text", "主唱"},
                                         {"start_ms", 1000},
                                         {"duration_ms", 5000},
                                         {"words", QJsonArray{QJsonObject{{"text", "主唱"},
                                                                          {"start_ms", 1000},
                                                                          {"duration_ms", 5000}}}},
                                         {"translation", "Lead"},
                                         {"romanization", "zhǔ chàng"},
                                         {"id", "lead-1"},
                                         {"agent_id", "lead"},
                                         {"song_part", "Chorus"},
                                         {"background_vocals", QJsonArray{harmony}}},
                             QJsonObject{{"text", "作词：示例"},
                                         {"start_ms", 2000},
                                         {"duration_ms", 500},
                                         {"words", QJsonArray{}}}}}};
}
} // namespace
class LyricPresentationTest final : public QObject
{
    Q_OBJECT
private slots:
    void retainedRowsSurviveSourceClearAndReplacement()
    {
        LyricModel current(this), retained(this);
        QAbstractItemModelTester checker(&retained,
                                         QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(current.replace(document()));
        current.setFilter(true, "^作词");
        current.setPosition(1500);
        const auto first = current.lineAt(0), second = current.lineAt(1);
        const auto metadata = current.metadata();
        retained.copyPresentationFrom(current);
        QCOMPARE(retained.rowCount(), 2);
        QCOMPARE(retained.lineAt(0), first);
        QCOMPARE(retained.lineAt(1), second);
        QCOMPARE(retained.metadata(), metadata);
        QCOMPARE(retained.offsetMs(), -120);
        QCOMPARE(retained.currentIndex(), current.currentIndex());
        QCOMPARE(retained.endThrough(1), current.endThrough(1));
        QVERIFY(retained.hasTranslation());
        QVERIFY(retained.hasRomanization());
        current.clear();
        QCOMPARE(current.rowCount(), 0);
        QCOMPARE(retained.lineAt(0), first);
        auto replacement = document();
        replacement.insert("offset_ms", 800);
        QVERIFY(current.replace(replacement));
        current.setFilter(false, {});
        QCOMPARE(retained.offsetMs(), -120);
        QCOMPARE(retained.lineAt(1), second);
        retained.setPosition(2200);
        QCOMPARE(retained.visibleIndex(1), 0);
        QCOMPARE(retained.lineAt(0), first);
    }
    void emptyAndSelfCopiesKeepModelNotificationsBalanced()
    {
        LyricModel source(this), retained(this);
        QAbstractItemModelTester checker(&retained,
                                         QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(retained.replace(document()));
        QSignalSpy resets(&retained, &QAbstractItemModel::modelReset);
        retained.copyPresentationFrom(retained);
        QCOMPARE(resets.size(), 0);
        retained.copyPresentationFrom(source);
        QCOMPARE(resets.size(), 1);
        QCOMPARE(retained.rowCount(), 0);
        QCOMPARE(retained.currentIndex(), -1);
        QCOMPARE(retained.offsetMs(), 0);
        QVERIFY(retained.metadata().isEmpty());
    }
};
QTEST_MAIN(LyricPresentationTest)
#include "LyricPresentationTest.moc"
