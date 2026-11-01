#include "PlaybackService.h"
#include "AudioEffectCatalogCache.h"
#include "AudioEffectSearch.h"
#include "ThemeStore.h"
#include "NativeWindow.h"
#include <QApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QInputMethodEvent>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
// Canonical domain/UI inputs, not fabricated provider wire responses.
class AudioEffectCatalogTestAccess
{
public:
    static bool ready(AudioEffectCatalog &catalog)
    {
        catalog.m_cache->load();
        return catalog.m_cacheReady;
    }
    static void page(AudioEffectCatalog &catalog, ApiClient &api, const QJsonArray &rows,
                     int total = -1, const QString &scope = {})
    {
        if (catalog.m_request)
            api.cancel(catalog.m_request);
        catalog.m_request = api.capabilities();
        const auto request = catalog.m_request;
        catalog.m_requestScope = scope.isEmpty() ? catalog.scope() : scope;
        catalog.m_revision = api.sessionRevision();
        QJsonObject domain{{"items", rows}};
        if (total >= 0)
            domain["total"] = total;
        catalog.succeeded(request, QJsonDocument(domain).toJson());
        api.cancel(request);
    }
    static int nextPage(const AudioEffectCatalog &catalog)
    {
        return catalog.m_buckets.value(catalog.scope()).nextPage;
    }
    static quint64 request(const AudioEffectCatalog &catalog)
    {
        return catalog.m_request;
    }
    static void fail(AudioEffectCatalog &catalog)
    {
        catalog.failRead("Controlled failure");
    }
};
class ApiClientTestAccess
{
public:
    static void retire(ApiClient &api)
    {
        ++api.m_revision;
        emit api.sessionChanged(api.m_revision);
    }
};
} // namespace EchoMusic
namespace
{
struct Session
{
    QTemporaryDir directory;
    SettingsStore settings;
    ApiClient api{DeviceIdentity::temporary()};
    PlaybackService playback;
    QSignalSpy closed{&playback, &PlaybackService::closed}, apiClosed{&api, &ApiClient::closed};
    explicit Session(QString file = {})
        : settings(file.isEmpty() ? directory.filePath("settings.ini") : file),
          playback(api, settings)
    {
    }
    ~Session()
    {
        playback.shutdown();
        api.shutdown();
        (void)QTest::qWaitFor([&] { return !closed.isEmpty() && !apiClosed.isEmpty(); }, 15000);
    }
    AudioEffectCatalog &catalog()
    {
        return *playback.effectCatalog();
    }
};
QJsonObject effect(int id, QString name, QString author = {}, QString source = "artist")
{
    return {{"id", id},
            {"name", name},
            {"author", author},
            {"source", source},
            {"labels", QJsonArray{"低音", "Bass"}},
            {"intro", "温暖的人声与空间感"},
            {"sound_urls", QJsonArray{"https://fixture.kugou.com/signed-marker"}},
            {"unavailable_reason", "fixture unavailable"}};
}
QVariantMap row(AudioEffectCatalog &catalog, int index = 0)
{
    return catalog.data(catalog.index(index), Qt::UserRole + 1).toMap();
}
QQuickItem *visual(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = visual(child, name))
            return found;
    return nullptr;
}
} // namespace
class AudioEffectCatalogTest : public QObject
{
    Q_OBJECT
private slots:
    void searchAcrossPagesAndStableActivation()
    {
        Session s;
        auto &catalog = s.catalog();
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        AudioEffectCatalogTestAccess::page(
            catalog, s.api, {effect(1, "ZZ Top", "Alice"), effect(2, "温暖人声", "Bob")});
        AudioEffectCatalogTestAccess::page(catalog, s.api, {effect(3, "WH-1000XM5", "Sony")});
        QCOMPARE(catalog.cachedCount(), 3);
        catalog.setQuery("ｗｈ１０００　ＳＯＮＹ");
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.rowCount(), 1);
        QCOMPARE(row(catalog).value("id").toInt(), 3);
        const auto selected = row(catalog).value("key").toString();
        catalog.setQuery("Bob");
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(row(catalog).value("id").toInt(), 2);
        QSignalSpy notices(&catalog, &AudioEffectCatalog::notice);
        catalog.activateKey(selected);
        QCOMPARE(notices.size(), 1);
        QCOMPARE(notices.first().first().toString(), QString("fixture unavailable"));
        QVERIFY(!catalog.downloading());
        catalog.setQuery("温暖 空间");
        QTRY_VERIFY(!catalog.searching());
        QVERIFY(catalog.rowCount() > 0);
        catalog.setQuery("WH1000XM6");
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(row(catalog).value("id").toInt(), 3); // Bounded typo matching.
        catalog.setQuery({});
        QCOMPARE(catalog.rowCount(), 3);
        QCOMPARE(row(catalog).value("id").toInt(), 1);
    }
    void relevantOrderAndSupersededSearch()
    {
        Session s;
        auto &catalog = s.catalog();
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        AudioEffectCatalogTestAccess::page(
            catalog, s.api,
            {effect(1, "Other", "Bass"), effect(2, "Bass boosted"), effect(3, "Bass")});
        catalog.setQuery("bass");
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(row(catalog).value("id").toInt(), 3);
        catalog.setRelevance(false);
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(row(catalog).value("id").toInt(), 1);
        for (const auto &query : {"Other", "missing", "Bass boosted"})
            catalog.setQuery(query);
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.rowCount(), 1);
        QCOMPARE(row(catalog).value("id").toInt(), 2);
        catalog.savePosition(catalog.viewId(), row(catalog).value("key").toString(), -7, 120);
        const auto position = catalog.position();
        catalog.setQuery({});
        catalog.setQuery("Bass boosted");
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.position(), position);
    }
    void categoriesBrandsAndAliasesRetainTheirQueries()
    {
        Session s;
        auto &catalog = s.catalog();
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        AudioEffectCatalogTestAccess::page(catalog, s.api, {effect(1, "Artist")});
        catalog.setQuery("Artist");
        QTRY_VERIFY(!catalog.searching());
        catalog.selectCategory("headphone");
        AudioEffectCatalogTestAccess::page(catalog, s.api,
                                           {QJsonObject{{"id", 2}, {"name", "Sennheiser"}},
                                            QJsonObject{{"id", 3}, {"name", "Sony"}}});
        catalog.setQuery("sony");
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.rowCount(), 1);
        catalog.activateKey(row(catalog).value("key").toString());
        QCOMPARE(catalog.brandName(), QString("Sony"));
        QVERIFY(catalog.query().isEmpty());
        AudioEffectCatalogTestAccess::page(
            catalog, s.api,
            {effect(8, "WH-1000XM5", {}, "headphone"), effect(8, "WH-1000XM4", {}, "headphone")});
        QCOMPARE(catalog.cachedCount(), 1);
        catalog.setQuery("wh1000xm5");
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.rowCount(), 1);
        QVERIFY(row(catalog).value("aliases").toStringList().contains("WH-1000XM5"));
        AudioEffectCatalogTestAccess::page(catalog, s.api,
                                           {effect(8, "WH-1000XM3", {}, "headphone")});
        QVERIFY(!catalog.coverageText().contains("失败"));
        QTRY_VERIFY(!catalog.searching());
        catalog.refresh();
        AudioEffectCatalogTestAccess::page(catalog, s.api,
                                           {effect(8, "New Model", {}, "headphone")});
        AudioEffectCatalogTestAccess::page(catalog, s.api, {});
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.rowCount(), 0);
        catalog.setQuery("New Model");
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.rowCount(), 1);
        QVERIFY(row(catalog).value("aliases").toStringList().isEmpty());
        catalog.allBrands();
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.query(), QString("sony"));
        QCOMPARE(catalog.rowCount(), 1);
        catalog.selectCategory("artist");
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.query(), QString("Artist"));
        QCOMPARE(row(catalog).value("id").toInt(), 1);
    }
    void validationFindsLaterPagesAndRetiresRemovedEntries()
    {
        Session s;
        auto &catalog = s.catalog();
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        AudioEffectCatalogTestAccess::page(catalog, s.api,
                                           {effect(1, "First"), effect(2, "Selected")});
        const auto key = row(catalog, 1).value("key").toString();
        ApiClientTestAccess::retire(s.api);
        catalog.activateKey(key);
        catalog.setBrowsingActive(false);
        AudioEffectCatalogTestAccess::page(catalog, s.api, {effect(1, "First")});
        QVERIFY(catalog.validating());
        QSignalSpy notices(&catalog, &AudioEffectCatalog::notice);
        auto denied = effect(2, "Selected");
        denied["unavailable_reason"] = "Later page denied";
        AudioEffectCatalogTestAccess::page(catalog, s.api, {denied});
        QVERIFY(!catalog.validating());
        QCOMPARE(notices.last().first().toString(), QString("Later page denied"));
        ApiClientTestAccess::retire(s.api);
        catalog.activateKey(key);
        QTRY_VERIFY(AudioEffectCatalogTestAccess::request(catalog));
        const auto retiredRequest = AudioEffectCatalogTestAccess::request(catalog);
        ApiClientTestAccess::retire(s.api);
        const QJsonObject late{{"items", QJsonArray{effect(99, "Retired account")}}};
        emit s.api.succeeded(retiredRequest, QJsonDocument(late).toJson());
        QCOMPARE(catalog.cachedCount(), 2);
        QVERIFY(!catalog.validating());
        catalog.activateKey(key);
        AudioEffectCatalogTestAccess::page(catalog, s.api, {effect(1, "First")});
        AudioEffectCatalogTestAccess::page(catalog, s.api, {});
        QVERIFY(!catalog.validating());
        QVERIFY(notices.last().first().toString().contains("下架"));
        QCOMPARE(catalog.cachedCount(), 1);
    }
    void cacheFailuresAndTruncatedCoverage()
    {
        QTemporaryDir directory;
        const auto settingsFile = directory.filePath("preferences.ini");
        QString database;
        {
            Session s(settingsFile);
            auto &catalog = s.catalog();
            QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
            database = s.settings.files("audio-effect-catalog", SettingsDomain::Device) +
                       "/catalog.sqlite";
            AudioEffectCatalogTestAccess::page(catalog, s.api,
                                               {effect(1, "First"), effect(2, "Second")});
            AudioEffectCatalogTestAccess::page(catalog, s.api, {});
        }
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "effect-cache-truncate");
            db.setDatabaseName(database);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("DELETE FROM effect_entries WHERE identity='artist-2'"));
        }
        QSqlDatabase::removeDatabase("effect-cache-truncate");
        {
            Session s(settingsFile);
            auto &catalog = s.catalog();
            QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
            QCOMPARE(catalog.cachedCount(), 1);
            QVERIFY(!catalog.complete());
            QCOMPARE(AudioEffectCatalogTestAccess::nextPage(catalog), 1);
        }
        const auto corruptPath = directory.filePath("corrupt.sqlite");
        QFile file(corruptPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("invalid SQLite data");
        file.close();
        AudioEffectCatalogCache cache(corruptPath);
        QSignalSpy failed(&cache, &AudioEffectCatalogCache::failed);
        QSignalSpy loaded(&cache, &AudioEffectCatalogCache::loaded);
        QSignalSpy closed(&cache, &AudioEffectCatalogCache::closed);
        cache.load();
        QTRY_VERIFY(!loaded.isEmpty());
        QCOMPARE(failed.size(), 1);
        cache.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void largeDirectorySearchAndCoverageLimit()
    {
        Session s;
        auto &catalog = s.catalog();
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        QJsonArray rows;
        for (int i = 0; i < 10000; ++i)
            rows.append(effect(i + 1, QString("Effect %1").arg(i)));
        AudioEffectCatalogTestAccess::page(catalog, s.api, rows);
        QCOMPARE(catalog.cachedCount(), 10000);
        QVERIFY(!catalog.complete());
        QVERIFY(catalog.coverageText().contains("上限"));
        QElapsedTimer timer;
        timer.start();
        catalog.setQuery("Effect 9999");
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.rowCount(), 1);
        QCOMPARE(row(catalog).value("id").toInt(), 10000);
        qInfo() << "10,000-entry local query including debounce:" << timer.elapsed() << "ms";
    }
    void liveProviderEnumeration()
    {
        if (!qEnvironmentVariableIsSet("ECHO_EFFECT_SEARCH_LIVE"))
            QSKIP("Explicit live provider check only");
        Session s;
        auto &catalog = s.catalog();
        catalog.setBrowsingActive(true);
        const auto wait = [&] {
            return QTest::qWaitFor(
                [&] { return catalog.complete() || !catalog.errorMessage().isEmpty(); }, 60000);
        };
        QVERIFY(wait());
        QVERIFY2(catalog.complete(), qPrintable(catalog.errorMessage()));
        qInfo() << "Live artist entries:" << catalog.cachedCount();
        QVERIFY(catalog.cachedCount() > 0);
        QVariantMap downloadable;
        for (int i = 0; i < catalog.rowCount(); ++i) {
            const auto candidate = row(catalog, i);
            const auto bytes = candidate.value("file_size").toLongLong();
            if (!candidate.value("sound_urls").toList().isEmpty() &&
                candidate.value("vpf_urls").toList().isEmpty() &&
                candidate.value("unavailable_reason").toString().isEmpty() && bytes > 0 &&
                bytes < 65536) {
                downloadable = candidate;
                break;
            }
        }
        QVERIFY2(!downloadable.isEmpty(), "No small public IR resource currently available");
        catalog.setQuery(downloadable.value("name").toString());
        QTRY_VERIFY(!catalog.searching());
        QSignalSpy downloaded(&catalog, &AudioEffectCatalog::downloaded);
        catalog.activateKey(downloadable.value("key").toString());
        QTRY_VERIFY_WITH_TIMEOUT(!catalog.downloading(), 30000);
        QVERIFY2(!downloaded.isEmpty(), qPrintable(catalog.errorMessage()));
        const auto installed = s.playback.audioControls()->downloadedImpulseResponse(
            downloaded.first().first().toString());
        QVERIFY(QFileInfo(installed).size() > 0);
        QVERIFY(installed.startsWith(s.playback.audioControls()->effectFilesRoot()));
        qInfo() << "Live filtered selection downloaded bytes:" << QFileInfo(installed).size();
        catalog.setQuery({});
        catalog.selectCategory("headphone");
        QVERIFY(wait());
        QVERIFY2(catalog.complete(), qPrintable(catalog.errorMessage()));
        qInfo() << "Live brands:" << catalog.cachedCount();
        QVERIFY(catalog.cachedCount() > 0);
        catalog.activateKey(row(catalog).value("key").toString());
        QVERIFY(wait());
        QVERIFY2(catalog.complete(), qPrintable(catalog.errorMessage()));
        qInfo() << "Live selected brand resources:" << catalog.cachedCount();
        catalog.selectCategory("market");
        for (int sort = 2; sort <= 4; ++sort) {
            catalog.setSort(sort);
            QVERIFY(wait());
            QVERIFY2(catalog.complete(), qPrintable(catalog.errorMessage()));
            const auto count = catalog.cachedCount();
            qInfo() << "Live market sort/entries:" << sort << count;
            QVERIFY(count > 50);
            const auto last = row(catalog, count - 1);
            catalog.setQuery(last.value("name").toString());
            QTRY_VERIFY(!catalog.searching());
            QVERIFY(catalog.indexOfKey(last.value("key").toString()) >= 0);
            catalog.setQuery({});
            QCOMPARE(catalog.rowCount(), count);
        }
        catalog.setBrowsingActive(false);
    }
    void completenessPauseResumeAndRepeatedPage()
    {
        Session s;
        auto &catalog = s.catalog();
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        AudioEffectCatalogTestAccess::page(catalog, s.api, {effect(1, "First")}, 1);
        QVERIFY(!catalog.complete()); // A reported total alone is not proof of coverage.
        QCOMPARE(AudioEffectCatalogTestAccess::nextPage(catalog), 2);
        catalog.setBrowsingActive(true);
        QTRY_VERIFY(AudioEffectCatalogTestAccess::request(catalog));
        catalog.setBrowsingActive(false);
        QCOMPARE(AudioEffectCatalogTestAccess::request(catalog), quint64(0));
        QCOMPARE(AudioEffectCatalogTestAccess::nextPage(catalog), 2);
        AudioEffectCatalogTestAccess::page(catalog, s.api, {effect(1, "First")});
        QVERIFY(!catalog.complete());
        QVERIFY(catalog.coverageText().contains("失败"));
        QCOMPARE(AudioEffectCatalogTestAccess::nextPage(catalog), 2);
        catalog.refresh();
        AudioEffectCatalogTestAccess::page(catalog, s.api, {effect(2, "Replacement")});
        AudioEffectCatalogTestAccess::page(catalog, s.api, {});
        QVERIFY(catalog.complete());
        QCOMPARE(catalog.cachedCount(), 1);
        QCOMPARE(row(catalog).value("id").toInt(),
                 2); // Removed entries retired only after enumeration.
    }
    void diskCacheNeverRestoresAuthorizationOrSignedUrls()
    {
        QTemporaryDir directory;
        const auto settingsFile = directory.filePath("preferences.ini");
        QString database;
        {
            Session s(settingsFile);
            auto &catalog = s.catalog();
            QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
            database = s.settings.files("audio-effect-catalog", SettingsDomain::Device) +
                       "/catalog.sqlite";
            AudioEffectCatalogTestAccess::page(catalog, s.api,
                                               {effect(7, "Cached Artist", "Writer")});
            AudioEffectCatalogTestAccess::page(catalog, s.api, {});
        }
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "effect-cache-audit");
            db.setDatabaseName(database);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("SELECT metadata FROM effect_entries"));
            QVERIFY(query.next());
            const auto bytes = query.value(0).toByteArray();
            QVERIFY(!bytes.contains("signed-marker"));
            QVERIFY(!bytes.contains("unavailable_reason"));
            QVERIFY(!bytes.contains("_verified"));
            QVERIFY(bytes.contains("Cached Artist"));
        }
        QSqlDatabase::removeDatabase("effect-cache-audit");
        Session s(settingsFile);
        auto &catalog = s.catalog();
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        QCOMPARE(catalog.rowCount(), 1);
        QVERIFY(row(catalog).value("needs_validation").toBool());
        QVERIFY(!row(catalog).contains("sound_urls"));
        catalog.activateKey(row(catalog).value("key").toString());
        QVERIFY(catalog.validating());
        auto current = effect(7, "Cached Artist");
        current["unavailable_reason"] = "Current account denied";
        QSignalSpy notices(&catalog, &AudioEffectCatalog::notice);
        AudioEffectCatalogTestAccess::page(catalog, s.api, {current});
        QVERIFY(!catalog.validating());
        QCOMPARE(notices.last().first().toString(), QString("Current account denied"));
        ApiClientTestAccess::retire(s.api);
        QVERIFY(row(catalog).value("needs_validation").toBool());
        QVERIFY(!row(catalog).contains("sound_urls"));
    }
    void productionPopupSearchAndReopen_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<double>("scale");
        QTest::newRow("light-100") << false << 1.;
        QTest::newRow("light-125") << false << 1.25;
        QTest::newRow("dark-100") << true << 1.;
        QTest::newRow("dark-125") << true << 1.25;
    }
    void productionPopupSearchAndReopen()
    {
        QFETCH(bool, dark);
        QFETCH(double, scale);
        Session s;
        auto &catalog = s.catalog();
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        QJsonArray rows;
        for (int i = 0; i < 1000; ++i)
            rows.append(effect(i + 1, QString("Effect %1").arg(i)));
        AudioEffectCatalogTestAccess::page(catalog, s.api, rows);
        ThemeStore theme(s.settings);
        theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
        theme.setMotionEnabled(false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(
            &engine, QUrl("qrc:/qt/qml/EchoMusic/EffectSearchTests/EffectSearchHarness.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"playback", QVariant::fromValue(&s.playback)}, {"zoom", scale}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window && Test::prepareNativeInput(window));
        QQuickItem *search = nullptr, *list = nullptr;
        QTRY_VERIFY((search = visual(window->contentItem(), "effectPlazaSearch")));
        QTRY_VERIFY((list = visual(window->contentItem(), "effectPlazaResults")));
        catalog.setBrowsingActive(false);
        QTRY_VERIFY(list->property("contentHeight").toReal() > list->height());
        int delegates = 0;
        const auto countDelegates = [&](auto &&self, QQuickItem *item) -> void {
            if (QByteArray(item->metaObject()->className()).contains("PlaybackEffectCard"))
                ++delegates;
            for (auto *child : item->childItems())
                self(self, child);
        };
        countDelegates(countDelegates, window->contentItem());
        QVERIFY(delegates > 0 && delegates < 60);
        qInfo() << "Rendered effect delegates / directory rows:" << delegates
                << catalog.cachedCount();
        list->setProperty("contentY", 1500.);
        QTest::qWait(550); // End bounded initial position restoration before the user scroll.
        list->setProperty("contentY", 1500.);
        auto *plaza = visual(window->contentItem(), "effectPlaza");
        QVERIFY(plaza);
        QVERIFY(QMetaObject::invokeMethod(plaza, "remember"));
        const auto before = catalog.position();
        search->forceActiveFocus();
        for (const char character : QByteArray("Effect 999"))
            QTest::keyClick(window, character);
        QTRY_VERIFY(!catalog.searching() && catalog.query() == "Effect 999");
        QCOMPARE(catalog.rowCount(), 1);
        QCOMPARE(row(catalog).value("name").toString(), QString("Effect 999"));
        const auto captures = qEnvironmentVariable("ECHO_EFFECT_SEARCH_CAPTURES");
        if (!captures.isEmpty()) {
            QDir().mkpath(captures);
            window->update();
            QTest::qWait(60);
            QVERIFY(window->grabWindow().save(captures + '/' + QTest::currentDataTag() + ".png"));
        }
        auto *clear = visual(search, "clearTextAction");
        QVERIFY(clear);
        QTest::mouseClick(
            window, Qt::LeftButton, {},
            clear->mapToScene(QPointF(clear->width() / 2, clear->height() / 2)).toPoint());
        QTRY_VERIFY(catalog.query().isEmpty());
        QTRY_COMPARE(catalog.rowCount(), 1000);
        QTest::qWait(600);
        QVERIFY(QMetaObject::invokeMethod(plaza, "remember"));
        const auto restored = catalog.position();
        QCOMPARE(restored.value("key"), before.value("key"));
        QVERIFY2(qAbs(restored.value("offset").toReal() - before.value("offset").toReal()) < 2,
                 qPrintable(QString("before %1/%2, restored %3/%4")
                                .arg(before.value("y").toReal())
                                .arg(before.value("offset").toReal())
                                .arg(restored.value("y").toReal())
                                .arg(restored.value("offset").toReal())));
        search->forceActiveFocus();
        QInputMethodEvent preedit("人", {});
        QCoreApplication::sendEvent(window, &preedit);
        QVERIFY(search->property("inputMethodComposing").toBool());
        QVERIFY(catalog.query().isEmpty());
        QInputMethodEvent commit;
        commit.setCommitString("人声");
        QCoreApplication::sendEvent(window, &commit);
        QTRY_VERIFY(catalog.query() == "人声" && !catalog.searching());
        catalog.setQuery("Effect 999");
        QTRY_VERIFY(!catalog.searching());
        root.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!catalog.browsingActive());
        root.reset(component.createWithInitialProperties(
            {{"playback", QVariant::fromValue(&s.playback)}, {"zoom", scale}}));
        QVERIFY(root);
        window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(Test::prepareNativeInput(window));
        QTRY_VERIFY((search = visual(window->contentItem(), "effectPlazaSearch")));
        QCOMPARE(search->property("text").toString(), QString("Effect 999"));
        QCOMPARE(catalog.rowCount(), 1);
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    AudioEffectCatalogTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "AudioEffectCatalogTest.moc"
