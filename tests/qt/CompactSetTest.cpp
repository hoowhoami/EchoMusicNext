#include "CompactSet.h"
#include "ValueCache.h"
#include <QtTest>

using namespace EchoMusic;
class CompactSetTest : public QObject
{
    Q_OBJECT
private slots:
    void valuePeekDoesNotPromoteOrExtendLifetime()
    {
        qint64 now = 0;
        ValueCache<QString, int> cache(2, 5, [&] { return now; });
        cache.insert("first", 1, 10, 2);
        cache.insert("second", 2, 20, 2);
        const auto &projection = cache;
        QCOMPARE(projection.peek("first"), std::optional(1));
        cache.insert("third", 3, 20, 2);
        QVERIFY(!cache.peek("first"));
        now = 20;
        QVERIFY(!cache.peek("second"));
        cache.insert("session", 4, decltype(cache)::NoExpiry, 2);
        now = 1000000;
        QCOMPARE(cache.peek("session"), std::optional(4));
    }
    void valueCacheDistinguishesEmptyHitsAndPreservesTtlOnUpdate()
    {
        qint64 now = 100;
        ValueCache<QString, QStringList> cache(2, 10, [&] { return now; });
        QVERIFY(cache.insert("empty", {}, 5, 2));
        const auto empty = cache.lookup("empty");
        QVERIFY(empty.has_value());
        QVERIFY(empty->isEmpty());
        QVERIFY(!cache.lookup("missing"));
        QVERIFY(cache.insert("gallery", {"one", "two"}, 10, 3));
        now = 109;
        QVERIFY(cache.update("gallery", {"two"}));
        QCOMPARE(cache.lookup("gallery"), std::optional(QStringList{"two"}));
        now = 110;
        QVERIFY(!cache.lookup("gallery"));
        QVERIFY(!cache.lookup("empty"));
        QCOMPARE(cache.statistics().cost, quint64(0));
        QCOMPARE(cache.statistics().expired, quint64(2));
    }
    void valueCacheEvictsLeastRecentAndEnforcesBothBudgets()
    {
        qint64 now = 0;
        ValueCache<QString, int> cache(2, 5, [&] { return now; });
        cache.insert("a", 1, 100, 2);
        cache.insert("b", 2, 100, 2);
        QCOMPARE(cache.lookup("a"), std::optional(1));
        cache.insert("c", 3, 100, 2);
        QVERIFY(!cache.lookup("b"));
        QCOMPARE(cache.lookup("a"), std::optional(1));
        cache.insert("d", 4, 100, 4);
        QVERIFY(!cache.lookup("a"));
        QVERIFY(!cache.lookup("c"));
        QCOMPARE(cache.statistics().cost, quint64(4));
        QVERIFY(!cache.insert("oversize", 1, 100, 6));
        QCOMPARE(cache.lookup("d"), std::optional(4));
        cache.clear();
        QCOMPARE(cache.statistics().cost, quint64(0));
        QCOMPARE(cache.size(), 0);
    }
    void singletonHasNoHashStorageAndDuplicatesDoNotPromote()
    {
        CompactSet<QString> values;
        QVERIFY(values.isEmpty());
        QCOMPARE(values.cbegin(), values.cend());
        QVERIFY(values.insert(QStringLiteral("row")));
        QVERIFY(!values.insert(QStringLiteral("row")));
        QCOMPARE(values.size(), 1);
        QVERIFY(!values.usesHashStorage());
        QCOMPARE(QStringList(values.cbegin(), values.cend()), QStringList{"row"});
        auto copy = values;
        QVERIFY(values.remove("row"));
        QVERIFY(values.isEmpty());
        QVERIFY(copy.contains("row"));
    }
    void fanOutCopiesAndDowngradesWithoutLosingMembers()
    {
        CompactSet<QString> values;
        QSet<QString> expected;
        for (int i = 0; i < 2000; ++i) {
            const auto key = QString::number(i);
            values.insert(key);
            expected.insert(key);
        }
        QVERIFY(values.usesHashStorage());
        QCOMPARE(QSet<QString>(values.cbegin(), values.cend()), expected);
        auto copy = values;
        for (int i = 0; i < 1999; ++i)
            QVERIFY(values.remove(QString::number(i)));
        QCOMPARE(values.size(), 1);
        QVERIFY(!values.usesHashStorage());
        QVERIFY(values.contains("1999"));
        QCOMPARE(copy.size(), 2000);
        values.unite(copy);
        values.unite(values);
        QCOMPARE(QSet<QString>(values.cbegin(), values.cend()), expected);
        QVERIFY(!values.remove("missing"));
        values.clear();
        QVERIFY(values.isEmpty());
    }
    void pointerRelationsRetireOnlyTheRequestedOwner()
    {
        QObject first, second;
        CompactSet<QObject *> values;
        values.insert(&first);
        auto copy = values;
        values.insert(&second);
        QVERIFY(values.remove(&first));
        QVERIFY(values.contains(&second));
        QVERIFY(!values.usesHashStorage());
        QVERIFY(copy.contains(&first));
        QVERIFY(!copy.contains(&second));
    }
};
QTEST_GUILESS_MAIN(CompactSetTest)
#include "CompactSetTest.moc"
