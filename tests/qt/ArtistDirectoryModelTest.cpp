#include "ArtistDirectoryModel.h"
#include "ExploreService.h"
#include "SongCatalogModel.h"
#include "SongMetadataService.h"
#include "SettingsStore.h"
#include <QAbstractItemModelTester>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace EchoMusic;
namespace EchoMusic
{
class ArtistDirectoryModelTestAccess
{
public:
    static bool persisted(const ArtistDirectoryModel &model)
    {
        return model.m_store && model.m_store->persisted(model.m_source);
    }
    static auto statistics(const ArtistDirectoryModel &model)
    {
        return model.m_hot.statistics();
    }
    static bool hasFallback(const ArtistDirectoryModel &model)
    {
        return !model.m_fallback.isEmpty();
    }
    static bool ready(const ArtistDirectoryModel &model, int row)
    {
        return model.m_hot.peek(row).has_value();
    }
    static void original(ArtistDirectoryModel &model, int row, QObject *consumer,
                         JsonRecordStore::Completion done)
    {
        model.m_store->read({model.m_source, row}, consumer, std::move(done));
    }
};
class ExploreServiceTestAccess
{
public:
    static void complete(ExploreService &service, ApiClient &api, const QJsonArray &groups)
    {
        service.m_pending.insert(70001, {ExploreService::Artists, api.sessionRevision()});
        service.accept(70001, QJsonDocument(QJsonObject{{"groups", groups}}).toJson());
    }
};
} // namespace EchoMusic
namespace
{
QJsonArray artists(int count = 1500)
{
    QJsonArray hot, a, b;
    for (int i = 0; i < count; ++i) {
        const QJsonObject artist{{"id", QString::number(i + 1)},
                                 {"title", QString("歌手 %1").arg(i + 1)},
                                 {"cover_url", QString("https://example.test/%1.jpg").arg(i + 1)},
                                 {"metadata", QJsonObject{{"opaque", QString(1024, 'x')},
                                                          {"unknown", QJsonValue::Null}}}};
        (i < 20 ? hot : i < 1000 ? a : b).append(artist);
    }
    return {QJsonObject{{"label", "热门"}, {"artists", hot}},
            QJsonObject{{"label", "A"}, {"artists", a}},
            QJsonObject{{"label", "B"}, {"artists", b}}};
}
} // namespace
class ArtistDirectoryModelTest final : public QObject
{
    Q_OBJECT
private slots:
    void adjacentColdDemandsShareOneRead()
    {
        QTemporaryDir dir;
        SessionDatabase database(dir.filePath("session"));
        {
            ArtistDirectoryModel model;
            model.attach(&database);
            model.replace(artists());
            QTRY_VERIFY(ArtistDirectoryModelTestAccess::persisted(model));
            QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
            for (int row = 0; row < 20; ++row)
                QVERIFY(!model.index(row).data(SongCatalogModel::PresentationReady).toBool());
            QTRY_VERIFY(ArtistDirectoryModelTestAccess::ready(model, 0));
            int publishedRows = 0;
            for (const auto &change : changes)
                publishedRows +=
                    change[1].value<QModelIndex>().row() - change[0].value<QModelIndex>().row() + 1;
            qInfo() << "20 adjacent cold artist demands published rows:" << publishedRows;
            QVERIFY2(publishedRows <= 32, "Adjacent demands repeatedly read overlapping batches");
            for (int row = 0; row < 20; ++row)
                QCOMPARE(model.index(row).data(SongCatalogModel::Title).toString(),
                         QString("歌手 %1").arg(row + 1));
        }
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void sqlWindowRetainsOriginalRecordsAndBoundsPresentation()
    {
        QTemporaryDir dir;
        SessionDatabase database(dir.filePath("session"));
        {
            ArtistDirectoryModel model;
            model.attach(&database);
            QAbstractItemModelTester check(&model,
                                           QAbstractItemModelTester::FailureReportingMode::QtTest);
            model.replace(artists());
            QCOMPARE(model.count(), 20);
            QTRY_VERIFY(ArtistDirectoryModelTestAccess::persisted(model));
            QVERIFY(!ArtistDirectoryModelTestAccess::hasFallback(model));
            QCOMPARE(ArtistDirectoryModelTestAccess::statistics(model).entries, 0);
            QVERIFY(model.selectLetter("全部"));
            QCOMPARE(model.count(), 1500);
            for (int i = 0; i < 1500; i += 32) {
                const auto row = model.index(i);
                QCOMPARE(row.data(SongCatalogModel::ResourceId).toString(), QString::number(i + 1));
                QTRY_COMPARE(row.data(SongCatalogModel::Title).toString(),
                             QString("歌手 %1").arg(i + 1));
                const auto stats = ArtistDirectoryModelTestAccess::statistics(model);
                QVERIFY(stats.entries <= 256);
                QVERIFY(stats.cost <= 512 * 1024);
            }
            QVERIFY(ArtistDirectoryModelTestAccess::statistics(model).evicted > 0);
            QVERIFY(model.selectLetter("A"));
            QCOMPARE(model.count(), 980);
            QCOMPARE(model.index(0).data(SongCatalogModel::ResourceId).toString(), "21");
            QTRY_COMPARE(model.index(0).data(SongCatalogModel::Title).toString(), "歌手 21");
            QObject consumer;
            QJsonArray original;
            QString error;
            bool done = false;
            ArtistDirectoryModelTestAccess::original(model, 1499, &consumer,
                                                     [&](auto rows, auto failure) {
                                                         original = rows;
                                                         error = failure;
                                                         done = true;
                                                     });
            QTRY_VERIFY(done);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(original.first().toObject()["metadata"].toObject()["opaque"].toString().size(),
                     1024);
            QVERIFY(original.first().toObject()["metadata"].toObject()["unknown"].isNull());
            model.setActive(false);
            QVERIFY(model.selectLetter("B"));
            model.index(499).data(SongCatalogModel::Title);
            model.replace({QJsonObject{
                {"label", "B"},
                {"artists", QJsonArray{QJsonObject{{"id", "new"}, {"title", "替换后的歌手"}}}}}});
            model.setActive(true);
            QCOMPARE(model.count(), 1);
            QTRY_COMPARE(model.index(0).data(SongCatalogModel::Title).toString(), "替换后的歌手");
            QCOMPARE(model.index(0).data(SongCatalogModel::ResourceId).toString(), "new");
            model.clear();
            QCOMPARE(model.count(), 0);
        }
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void lostSourceKeepsLetterAndRetiresOldDeliveries()
    {
        QTemporaryDir dir;
        SessionDatabase database(dir.filePath("session"));
        {
            SettingsStore prefs(dir.filePath("prefs.ini"));
            ApiClient api(DeviceIdentity::temporary());
            SongMetadataService metadata(api);
            metadata.enableSqlIndex(database);
            PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
            playback.attachMetadata(metadata);
            RouteController router;
            ExploreService service(api, playback, router, false);
            service.registerRoutes(router);
            QVERIFY(router.navigate("artist-recommendations"));
            ExploreServiceTestAccess::complete(service, api, artists());
            auto *model = service.artists();
            QTRY_VERIFY(ArtistDirectoryModelTestAccess::persisted(*model));
            service.selectLetter("B");
            QCOMPARE(model->count(), 500);
            QSignalSpy invalidated(&database, &SessionDatabase::invalidated);
            QVERIFY(database.submit(
                this,
                [](QSqlDatabase &db, const QString &) {
                    const auto path = db.databaseName();
                    db.close();
                    return QFile::remove(path);
                },
                [](bool) {}));
            QVERIFY(database.submit(
                this, [](QSqlDatabase &, const QString &) { return true; }, [](bool) {}));
            model->index(0).data(SongCatalogModel::Title);
            QTRY_COMPARE(invalidated.size(), 1);
            QVERIFY(model->cacheRecovering());
            QCOMPARE(service.artistLetter(), "B");
            QCOMPARE(model->count(), 500);
            ExploreServiceTestAccess::complete(service, api, artists());
            QVERIFY(!model->cacheRecovering());
            QCOMPARE(service.artistLetter(), "B");
            QTRY_COMPARE(model->index(0).data(SongCatalogModel::Title).toString(), "歌手 1001");
            api.advanceSession();
            QCOMPARE(model->count(), 0);
            QVERIFY(service.artistLetter().isEmpty());
            service.shutdown();
            router.shutdown();
            QSignalSpy drained(&playback, &PlaybackService::closed);
            playback.shutdown();
            QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
            api.shutdown();
        }
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
};
QTEST_MAIN(ArtistDirectoryModelTest)
#include "ArtistDirectoryModelTest.moc"
