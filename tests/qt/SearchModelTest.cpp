#include "SearchModel.h"
#include "SearchResultModel.h"
#include "SearchSuggestionModel.h"
#include "StorageService.h"
#include <QTemporaryDir>
#include <QAbstractItemModelTester>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

using namespace EchoMusic;
namespace EchoMusic
{
class SearchModelTestAccess
{
public:
    // Canonical domain/control-flow seam. No provider fixture or HTTP acceptance
    // is inferred from these synthetic display records; Rust owns wire parsing.
    static quint64 prepare(SearchModel &model, ApiClient &api, quint32 page,
                           SearchModel::Kind kind = SearchModel::Kind::Song)
    {
        model.cancelRequests();
        model.m_kind = kind;
        model.m_keyword = QStringLiteral("测试");
        auto &bucket = model.active();
        bucket.revision = api.sessionRevision();
        bucket.pendingPage = page;
        bucket.state = page == 1 ? SearchModel::State::Loading : SearchModel::State::LoadingMore;
        bucket.request = api.capabilities();
        return bucket.request;
    }
    static quint64 pending(SearchModel &model, ApiClient &api, SearchModel::Kind kind,
                           quint32 page = 1)
    {
        auto &bucket = model.m_buckets[std::size_t(kind)];
        bucket.revision = api.sessionRevision();
        bucket.pendingPage = page;
        bucket.state = SearchModel::State::Loading;
        bucket.request = api.capabilities(); // Local FFI capability request, no HTTP.
        return bucket.request;
    }
    static void succeed(SearchModel &model, quint64 id, const QByteArray &payload)
    {
        model.succeeded(id, payload);
    }
    static quint64 prepareHot(SearchModel &model, ApiClient &api)
    {
        model.m_hotRevision = api.sessionRevision();
        model.m_hotRequest = api.capabilities(); // Offline lifecycle seam.
        return model.m_hotRequest;
    }
    static quint64 pendingHot(SearchModel &model)
    {
        return model.m_hotRequest;
    }
    static void fail(SearchModel &model, quint64 id)
    {
        model.failed(id, 5, QByteArrayLiteral("sensitive raw error"));
    }
    static quint64 suggestionPending(SearchSuggestionModel &model, ApiClient &api)
    {
        model.m_debounce.stop();
        model.m_requestedQuery = model.m_query;
        model.m_request = api.capabilities(); // Offline request identity seam.
        return model.m_request;
    }
    static void suggest(SearchSuggestionModel &model, quint64 id, const QByteArray &payload)
    {
        model.succeeded(id, payload);
    }
    static quint64 defaultPending(SearchSuggestionModel &model, ApiClient &api)
    {
        model.m_started = true;
        model.m_defaultRequest = api.capabilities();
        return model.m_defaultRequest;
    }
    static quint64 defaultRequest(const SearchSuggestionModel &model)
    {
        return model.m_defaultRequest;
    }
    static bool suggestionTimerActive(const SearchSuggestionModel &model)
    {
        return model.m_debounce.isActive();
    }
};
} // namespace EchoMusic
class SearchModelTest : public QObject
{
    Q_OBJECT
    static QByteArray songs(int page, int count, int total)
    {
        QJsonArray items;
        for (int i = 0; i < count; ++i) {
            items.append(
                QJsonObject{{"id", QString::number(page * 100 + i)},
                            {"title", QStringLiteral("歌曲 %1").arg(i)},
                            {"artists", QJsonArray{QJsonObject{{"id", 1}, {"name", "歌手"}}}},
                            {"album_title", "专辑"},
                            {"cover_url", ""},
                            {"duration_ms", 185000}});
        }
        return QJsonDocument(QJsonObject{{"kind", "songs"},
                                         {"page", QJsonObject{{"page", page},
                                                              {"page_size", 30},
                                                              {"total", total},
                                                              {"items", items}}}})
            .toJson();
    }
    static void drain(ApiClient &api)
    {
        QSignalSpy closed(&api, &ApiClient::closed);
        api.shutdown();
        if (closed.isEmpty())
            QVERIFY(closed.wait(5000));
    }
private slots:
    void recommendationsFollowPopupRatherThanResultsRoute()
    {
        ApiClient api;
        SearchModel model(api);
        // Already-defined Rust domain JSON, not an upstream response fixture.
        const QByteArray response(
            R"([{"title":"推荐","keywords":[{"keyword":"音乐","reason":""}]}])");
        model.setDiscoveryActive(true);
        const auto request = SearchModelTestAccess::prepareHot(model, api);
        QVERIFY(request);
        model.setPageActive(false);
        QCOMPARE(SearchModelTestAccess::pendingHot(model), request);
        SearchModelTestAccess::succeed(model, request, response);
        QCOMPARE(model.hotGroups()->rowCount(), 1);
        const auto closing = SearchModelTestAccess::prepareHot(model, api);
        model.setDiscoveryActive(false);
        QCOMPARE(SearchModelTestAccess::pendingHot(model), quint64(0));
        SearchModelTestAccess::succeed(model, closing, QByteArray("[]"));
        QCOMPARE(model.hotGroups()->rowCount(), 1); // Late empty result cannot erase cache.
        model.setDiscoveryActive(true);
        const auto oldSession = SearchModelTestAccess::prepareHot(model, api);
        api.advanceSession();
        QCOMPARE(SearchModelTestAccess::pendingHot(model), quint64(0));
        SearchModelTestAccess::succeed(model, oldSession, response);
        QCOMPARE(model.hotGroups()->rowCount(), 0);
        drain(api);
    }
    void disablingDefaultSearchCancelsAndRejectsLateResult()
    {
        ApiClient api(DeviceIdentity::temporary());
        SearchModel model(api);
        auto &suggestions = *model.suggestions();
        // Fresh models do not request the optional recommended placeholder.
        suggestions.initialize();
        QCOMPARE(SearchModelTestAccess::defaultRequest(suggestions), quint64(0));
        SearchSuggestionModel pending(api, nullptr);
        pending.setDefaultEnabled(true);
        const auto canceled = SearchModelTestAccess::defaultPending(pending, api);
        // A response already queued before disabling must not restore promotions.
        pending.setDefaultEnabled(false);
        QCOMPARE(SearchModelTestAccess::defaultRequest(pending), quint64(0));
        SearchModelTestAccess::suggest(
            pending, canceled, R"({"promotions":[{"main_title":"迟到推荐词","subtitle":"广告"}]})");
        QVERIFY(pending.defaultKeyword().isEmpty());
        QVERIFY(pending.promotions().isEmpty());
        pending.shutdown();
        suggestions.shutdown();
        api.shutdown();
    }
    void suggestionsRejectChangedQueryClosedPopupAndOldSession()
    {
        ApiClient api;
        SearchModel model(api);
        auto &suggestions = *model.suggestions();
        QAbstractItemModelTester tester(&suggestions,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        const auto payload = QByteArrayLiteral(
            R"({"groups":[{"title":"单曲","items":[{"text":"测试一"},{"text":"测试二"}]},{"title":"MV","items":[{"text":"不显示的视频建议"}]}]})");
        suggestions.setActive(true);
        suggestions.setQuery(QStringLiteral("测试"));
        QVERIFY(SearchModelTestAccess::suggestionTimerActive(suggestions));
        auto stale = SearchModelTestAccess::suggestionPending(suggestions, api);
        suggestions.setQuery(QStringLiteral("另一个词"));
        SearchModelTestAccess::suggest(suggestions, stale, payload);
        QCOMPARE(suggestions.rowCount(), 0);
        auto current = SearchModelTestAccess::suggestionPending(suggestions, api);
        SearchModelTestAccess::suggest(suggestions, current, payload);
        QCOMPARE(suggestions.rowCount(), 2);
        QCOMPARE(suggestions.textAt(0), QStringLiteral("测试一"));
        QVERIFY(suggestions.data(suggestions.index(0), SearchSuggestionModel::GroupStart).toBool());
        QVERIFY(
            !suggestions.data(suggestions.index(1), SearchSuggestionModel::GroupStart).toBool());
        QVERIFY(!suggestions.loading());
        QVERIFY(suggestions.textAt(-1).isEmpty());
        suggestions.setQuery(QStringLiteral("关闭"));
        stale = SearchModelTestAccess::suggestionPending(suggestions, api);
        suggestions.setActive(false);
        QVERIFY(!SearchModelTestAccess::suggestionTimerActive(suggestions));
        SearchModelTestAccess::suggest(suggestions, stale, payload);
        QCOMPARE(suggestions.rowCount(), 0);
        suggestions.setActive(true);
        stale = SearchModelTestAccess::suggestionPending(suggestions, api);
        api.advanceSession();
        SearchModelTestAccess::suggest(suggestions, stale, payload);
        QCOMPARE(suggestions.rowCount(), 0);
        suggestions.shutdown();
        QVERIFY(!SearchModelTestAccess::suggestionTimerActive(suggestions));
        drain(api);
    }
    void songRowsExposeVipAndQualityBadgesFromCatalogMetadata()
    {
        ApiClient api;
        SearchModel model(api);
        const auto id = SearchModelTestAccess::prepare(model, api, 1);
        const QByteArray payload = QByteArrayLiteral(
            R"({"kind":"songs","page":{"page":1,"page_size":30,"total":1,"items":[)"
            R"({"id":"32100650","hash":"B3A52A7A958BF0AED0EBFBA2E9A818B7",)"
            R"("title":"晴天","artists":[{"id":3520,"name":"周杰伦"}],)"
            R"("album_title":"叶惠美","cover_url":"","duration_ms":269000,"privilege":10,)"
            R"("metadata":{"payment_type":3,"old_copyright_code":0,"originality_code":1,)"
            R"("resources":[{"quality":"flac","hash":"0A69169202DE95AAF24A9944CCF0730D"}],)"
            R"("rights":{"quality_map":{"attr0":16}}}}]}})");
        SearchModelTestAccess::succeed(model, id, payload);
        QCOMPARE(model.count(), 1);
        const auto row = model.index(0);
        const auto tags = model.data(row, SearchModel::PrivilegeTags).toList();
        QCOMPARE(tags.size(), 1);
        QCOMPARE(tags.first().toMap().value("label").toString(), QStringLiteral("VIP"));
        QCOMPARE(model.data(row, SearchModel::QualityLabel).toString(), QStringLiteral("SQ"));
        // The page binds the proxy model, so forwarded roles must match.
        const auto projected = model.results()->index(0, 0);
        QCOMPARE(model.results()->data(projected, SearchModel::QualityLabel).toString(),
                 QStringLiteral("SQ"));
        QCOMPARE(model.results()->data(projected, SearchModel::PrivilegeTags).toList().size(), 1);
        drain(api);
    }
    void liveAnonymousSearchAndPagination()
    {
        if (qEnvironmentVariable("ECHO_LIVE_SEARCH") != QStringLiteral("1"))
            QSKIP("Opt-in anonymous read-only integration; ordinary CI stays offline");
        ApiClient api;
        SearchModel model(api);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        tester.setUseFetchMore(false);
        for (auto kind :
             {SearchModel::Kind::Song, SearchModel::Kind::Playlist, SearchModel::Kind::Album,
              SearchModel::Kind::Artist, SearchModel::Kind::Lyric, SearchModel::Kind::Mv}) {
            model.search(QStringLiteral("周杰伦"), kind);
            QTRY_VERIFY_WITH_TIMEOUT(model.state() == SearchModel::State::Ready ||
                                         model.state() == SearchModel::State::Error,
                                     25000);
            QCOMPARE(model.state(), SearchModel::State::Ready);
            QVERIFY(model.count() > 0);
            const auto firstCount = model.count();
            if (model.hasMore()) {
                model.loadMore();
                QTRY_VERIFY_WITH_TIMEOUT(model.state() == SearchModel::State::Ready ||
                                             model.state() == SearchModel::State::Error,
                                         25000);
                QCOMPARE(model.state(), SearchModel::State::Ready);
                QVERIFY(model.count() > firstCount);
            }
            qInfo() << "Anonymous category" << kind << "visible rows" << model.count()
                    << "remote total" << model.total();
        }
        model.showDiscovery();
        QCOMPARE(model.count(), 0);
        drain(api);
    }
    void paginationErrorKeepsRowsAndEmptyPageTerminates()
    {
        ApiClient api;
        SearchModel model(api);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto id = SearchModelTestAccess::prepare(model, api, 1);
        SearchModelTestAccess::succeed(model, id, songs(1, 30, 61));
        QCOMPARE(model.count(), 30);
        QCOMPARE(model.total(), 61);
        QVERIFY(model.hasMore());
        QCOMPARE(model.data(model.index(0), SearchModel::DurationText).toString(),
                 QStringLiteral("3:05"));
        id = SearchModelTestAccess::prepare(model, api, 2);
        SearchModelTestAccess::fail(model, id);
        QCOMPARE(model.count(), 30);
        QVERIFY(model.hasMore());
        QCOMPARE(model.state(), SearchModel::State::Error);
        QVERIFY(!model.errorMessage().contains(QStringLiteral("sensitive")));
        id = SearchModelTestAccess::prepare(model, api, 2);
        SearchModelTestAccess::succeed(model, id, songs(2, 30, 61));
        QCOMPARE(model.count(), 60);
        id = SearchModelTestAccess::prepare(model, api, 3);
        SearchModelTestAccess::succeed(model, id, songs(3, 0, 61));
        QCOMPARE(model.count(), 60);
        QVERIFY(!model.hasMore());
        drain(api);
    }
    void oldRequestAndOldSessionCannotRestoreResults()
    {
        ApiClient api;
        SearchModel model(api);
        const auto old = SearchModelTestAccess::prepare(model, api, 1);
        model.showDiscovery();
        const auto current = SearchModelTestAccess::prepare(model, api, 1);
        SearchModelTestAccess::succeed(model, old, songs(1, 30, 30));
        QCOMPARE(model.count(), 0);
        SearchModelTestAccess::succeed(model, current, songs(1, 30, 30));
        QCOMPARE(model.count(), 30);
        const auto sessionRequest = SearchModelTestAccess::prepare(model, api, 2);
        api.advanceSession();
        QCOMPARE(model.count(), 0);
        SearchModelTestAccess::succeed(model, sessionRequest, songs(2, 30, 60));
        QCOMPARE(model.count(), 0);
        drain(api);
    }
    void wrongDomainKindIsRejectedButPageEchoDoesNotRejectRows()
    {
        ApiClient api;
        SearchModel model(api);
        auto id = SearchModelTestAccess::prepare(model, api, 1, SearchModel::Kind::Album);
        SearchModelTestAccess::succeed(model, id, songs(1, 30, 30));
        QCOMPARE(model.state(), SearchModel::State::Error);
        QCOMPARE(model.count(), 0);
        id = SearchModelTestAccess::prepare(model, api, 1);
        SearchModelTestAccess::succeed(model, id, songs(2, 35, 1));
        QCOMPARE(model.state(), SearchModel::State::Ready);
        QCOMPARE(model.count(), 35);
        QCOMPARE(model.total(), 1);
        drain(api);
    }
    void categoryCacheAndPresentationRemainIndependent()
    {
        ApiClient api;
        SearchModel model(api);
        QAbstractItemModelTester sourceTester(
            &model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester proxyTester(
            model.results(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto id = SearchModelTestAccess::prepare(model, api, 1);
        SearchModelTestAccess::succeed(model, id, songs(1, 30, 60));
        model.results()->setQuery(QStringLiteral("歌曲 29"));
        QCOMPARE(model.results()->rowCount(), 1);
        model.results()->toggleSort(SearchResultModel::Title);
        SearchModelTestAccess::pending(model, api, SearchModel::Kind::Album);
        model.setKind(SearchModel::Kind::Album);
        QCOMPARE(model.count(), 0);
        QCOMPARE(model.results()->query(), QString{});
        model.setKind(SearchModel::Kind::Song);
        QCOMPARE(model.count(), 30);
        QCOMPARE(model.state(), SearchModel::State::Ready);
        QCOMPARE(model.results()->query(), QStringLiteral("歌曲 29"));
        QCOMPARE(model.results()->rowCount(), 1);
        QCOMPARE(model.results()->sortDirection(), SearchResultModel::Ascending);
        model.results()->toggleSort(SearchResultModel::Title);
        QCOMPARE(model.results()->sortDirection(), SearchResultModel::Descending);
        model.results()->toggleSort(SearchResultModel::Title);
        QCOMPARE(model.results()->sortField(), SearchResultModel::Unsorted);
        model.showDiscovery();
        QCOMPARE(model.results()->query(), QString{});
        QCOMPARE(model.results()->rowCount(), 0);
        drain(api);
    }
    void backgroundCategoryCompletesWithoutChangingActiveRows()
    {
        ApiClient api;
        SearchModel model(api);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        const auto songRequest = SearchModelTestAccess::prepare(model, api, 1);
        const auto albumRequest =
            SearchModelTestAccess::pending(model, api, SearchModel::Kind::Album);
        model.setKind(SearchModel::Kind::Album);
        const auto payload =
            QJsonDocument(
                QJsonObject{
                    {"kind", "albums"},
                    {"page",
                     QJsonObject{{"page", 1},
                                 {"page_size", 30},
                                 {"total", 1},
                                 {"items", QJsonArray{QJsonObject{{"id", 1},
                                                                  {"title", "专辑"},
                                                                  {"artists", QJsonArray{}},
                                                                  {"published_at", "2026-01-01"},
                                                                  {"cover_url", ""}}}}}}})
                .toJson();
        SearchModelTestAccess::succeed(model, albumRequest, payload);
        QCOMPARE(model.count(), 1);
        QSignalSpy inserts(&model, &QAbstractItemModel::rowsInserted);
        SearchModelTestAccess::succeed(model, songRequest, songs(1, 30, 30));
        QCOMPARE(model.count(), 1);
        QCOMPARE(inserts.count(), 0);
        QCOMPARE(model.data(model.index(0), SearchModel::Title).toString(), QStringLiteral("专辑"));
        model.setKind(SearchModel::Kind::Song);
        QCOMPARE(model.count(), 30);
        QCOMPARE(model.state(), SearchModel::State::Ready);
        drain(api);
    }
    void reentrantAccountChangeDefersResetAndRejectsOldData()
    {
        ApiClient api;
        SearchModel model(api);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        connect(&model, &QAbstractItemModel::rowsInserted, &api, &ApiClient::advanceSession);
        const auto id = SearchModelTestAccess::prepare(model, api, 1);
        SearchModelTestAccess::succeed(model, id, songs(1, 30, 30));
        QTRY_COMPARE(model.count(), 0);
        SearchModelTestAccess::succeed(model, id, songs(1, 30, 30));
        QCOMPARE(model.count(), 0);
        drain(api);
    }
    void historyIsBoundedAndShutdownDrainsWrites()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("music.sqlite"));
        {
            StorageService storage;
            QSignalSpy ready(&storage, &StorageService::initialized);
            QSignalSpy closed(&storage, &StorageService::closed);
            storage.initialize(path);
            QTRY_COMPARE(ready.count(), 1);
            for (int i = 0; i < 25; ++i)
                storage.recordSearch(QString::number(i));
            storage.recordSearch(QStringLiteral("  7  "));
            storage.shutdown();
            QTRY_COMPARE(closed.count(), 1);
        }
        StorageService reopened;
        QSignalSpy ready(&reopened, &StorageService::initialized);
        reopened.initialize(path);
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(reopened.searchHistory().size(), 20);
        QCOMPARE(reopened.searchHistory().first(), QStringLiteral("7"));
        QVERIFY(!reopened.searchHistory().contains(QStringLiteral("0")));
        reopened.removeSearch(QStringLiteral("7"));
        QTRY_COMPARE(reopened.searchHistory().first(), QStringLiteral("24"));
        reopened.clearSearches();
        QTRY_VERIFY(reopened.searchHistory().isEmpty());
        QSignalSpy closed(&reopened, &StorageService::closed);
        reopened.shutdown();
        QTRY_COMPARE(closed.count(), 1);
    }
    void reentrantRowObserverQueuesReset()
    {
        ApiClient api;
        SearchModel model(api);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        connect(&model, &QAbstractItemModel::rowsInserted, &model, [&] { model.showDiscovery(); });
        const auto id = SearchModelTestAccess::prepare(model, api, 1);
        SearchModelTestAccess::succeed(model, id, songs(1, 30, 30));
        QCOMPARE(model.count(), 30);
        QTRY_COMPARE(model.count(), 0);
        QCOMPARE(model.state(), SearchModel::State::Idle);
        drain(api);
    }
};
QTEST_GUILESS_MAIN(SearchModelTest)
#include "SearchModelTest.moc"
