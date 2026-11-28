#include "PlaybackService.h"
#include "AudioEffectCatalogCache.h"
#include "AudioEffectSearch.h"
#include "ThemeStore.h"
#include "NativeWindow.h"
#include <QApplication>
#include <QAbstractItemModelTester>
#include <QPointer>
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
#include <QFontDatabase>
#include <QDataStream>
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
        QVERIFY(QTest::qWaitFor([&] { return !catalog.m_writing && !catalog.m_searching; }, 15000));
    }
    static int nextPage(const AudioEffectCatalog &catalog)
    {
        return catalog.m_buckets.value(catalog.scope()).nextPage;
    }
    static quint64 request(const AudioEffectCatalog &catalog)
    {
        return catalog.m_request;
    }
    static void refreshGeneration(AudioEffectCatalog &catalog)
    {
        catalog.resetScope(catalog.scope());
    }
    static void fail(AudioEffectCatalog &catalog)
    {
        catalog.failRead("Controlled failure");
    }
    static void pauseAutomaticRequests(AudioEffectCatalog &catalog)
    {
        catalog.m_next.stop();
    }
    static void blockAutomaticRequests(AudioEffectCatalog &catalog)
    {
        QObject::disconnect(&catalog.m_next, &QTimer::timeout, &catalog, nullptr);
    }
    static void evictWindow(AudioEffectCatalog &catalog)
    {
        catalog.m_window.clear();
        catalog.m_oversized = {-1, {}};
    }
    static void unobservedProjection(AudioEffectCatalog &catalog)
    {
        auto completed = std::make_shared<bool>(false);
        catalog.m_cache->project(catalog.scope(), catalog.viewId(), {}, true,
                                 catalog.m_projectionGeneration + 1000, catalog.m_appliedProjection,
                                 {}, std::make_shared<std::atomic_bool>(false),
                                 [completed](auto) { *completed = true; });
        QVERIFY(QTest::qWaitFor([completed] { return *completed; }, 5000));
    }
    static void loseColdSource(AudioEffectCatalog &catalog, const QString &path)
    {
        auto &cache = *catalog.m_cache;
        QMetaObject::invokeMethod(
            cache.m_worker,
            [path] {
                for (const auto &name : QSqlDatabase::connectionNames()) {
                    if (!name.startsWith("effect-catalog-"))
                        continue;
                    auto db = QSqlDatabase::database(name, false);
                    if (db.databaseName() == path)
                        db.close();
                }
                QFile::remove(path);
                QFile::remove(path + "-wal");
                QFile::remove(path + "-shm");
            },
            Qt::BlockingQueuedConnection);
        catalog.m_window.clear();
    }
    static quint64 windowCost(const AudioEffectCatalog &catalog)
    {
        return catalog.m_window.statistics().cost;
    }
    static int windowRows(const AudioEffectCatalog &catalog)
    {
        return catalog.m_window.size();
    }
    static int verifiedRows(const AudioEffectCatalog &catalog)
    {
        return catalog.m_verified.size();
    }
    static int pendingReads(const AudioEffectCatalog &catalog)
    {
        return catalog.m_pendingWindows.size();
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
    (void)QTest::qWaitFor(
        [&] {
            const auto value = catalog.data(catalog.index(index), Qt::UserRole + 1).toMap();
            return !value.isEmpty() && !value.value("_loading").toBool();
        },
        5000);
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
    void productionEffectNamesStayWithinRows_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<double>("scale");
        for (auto mode : {ThemeStore::Light, ThemeStore::Dark})
            for (double scale : {1., 1.25})
                QTest::newRow(qPrintable(QString("%1-%2").arg(int(mode)).arg(scale)))
                    << int(mode) << scale;
    }
    void productionEffectNamesStayWithinRows()
    {
        QFETCH(int, mode);
        QFETCH(double, scale);
        Session s;
        s.playback.setVolume(0);
        ThemeStore theme(s.settings);
        theme.setMode(ThemeStore::Mode(mode));
        theme.setMotionEnabled(false);
        theme.setFloatingFrosted(false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        const QStringList names{"Dolby_Atmos室内系 声场Pro【黑钻壁蛇】超长名称不能盖住下一排按钮",
                                "Dolby ATMOS ((128K MP3)) [瑞昱高清音频] Default",
                                "Kugou_viper_black_diamond_headset SRS音效_1_1",
                                "Kugou_viper_black_diamond SRS音效_1_2",
                                "Realtek HD Audio16DB DTS SRS音效_2_1",
                                "SRS音效_2_2",
                                "A_very_long_unbroken_effect_name_without_any_spaces_1234567890",
                                "中文音效名称也必须保留两行省略并且不能上下交叠再长也不能覆盖旁边",
                                "第一行\n第二行\n第三行不应绘制",
                                "Ág, fallback 中英混排🙂组合 é",
                                "短名",
                                "最后一排的完整名称要能正常滚动定位与选用，不应被固定网格截掉"};
        auto *audio = s.playback.audioControls();
        for (int i = names.size() - 1; i >= 0; --i) {
            const auto path = s.directory.filePath(QString("name-%1.wav").arg(i));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QDataStream wave(&file);
            wave.setByteOrder(QDataStream::LittleEndian);
            wave.writeRawData("RIFF", 4);
            wave << quint32(36 + 128);
            wave.writeRawData("WAVEfmt ", 8);
            wave << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000)
                 << quint16(2) << quint16(16);
            wave.writeRawData("data", 4);
            wave << quint32(128);
            for (int j = 0; j < 64; ++j)
                wave << qint16(j == 0 ? 32767 : 0);
            file.close();
            audio->registerImpulseResponse(path, names[i]);
        }
        QCOMPARE(audio->impulseResponses().size(), names.size());
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.Controls
import EchoMusic.Playback
import EchoMusic.Theme
Window {
    id: host
    required property PlaybackService playback
    property real zoom: 1
    width: 1000; height: 800; visible: true; color: Theme.main
    EchoButton { id: anchor; x: 600; y: 740; text: "Effects" }
    PlaybackEffectPopup {
        id: effect; objectName: "localEffectPopup"
        playback: host.playback; anchorItem: anchor; uiScale: host.zoom
        tab: 2; libraryTab: 1
    }
    Component.onCompleted: Qt.callLater(function() { effect.open(); })
})",
                          QUrl("qrc:/effect-name-regression.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"playback", QVariant::fromValue(&s.playback)}, {"zoom", scale}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window && Test::prepareNativeInput(window));
        auto *popup = root->findChild<QObject *>("localEffectPopup");
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QQuickItem *grid = nullptr;
        QTRY_VERIFY(grid = visual(window->contentItem(), "myEffectGrid"));
        QTRY_COMPARE(grid->property("count").toInt(), names.size());
        const auto captures = qEnvironmentVariable("ECHO_EFFECT_NAMES_CAPTURES");
        if (!captures.isEmpty())
            QVERIFY(QDir().mkpath(captures));
        const QStringList families{QString{},
                                   QFontDatabase::systemFont(QFontDatabase::FixedFont).family()};
        for (int family = 0; family < families.size(); ++family)
            for (int fontScale : {100, 125}) {
                theme.setGlobalFont(families[family]);
                theme.setGlobalFontSizeScale(fontScale);
                QVERIFY(QMetaObject::invokeMethod(grid, "positionViewAtIndex", Q_ARG(int, 0),
                                                  Q_ARG(int, 0)));
                QTest::qWait(80);
                const auto image = window->grabWindow();
                QVERIFY(!image.isNull());
                const qreal density = qreal(image.width()) / window->width();
                const auto viewport = grid->mapRectToScene(grid->boundingRect());
                int checked = 0;
                const auto entries = audio->impulseResponses();
                for (int index = 0; index < entries.size(); ++index) {
                    const auto entry = entries[index].toMap();
                    auto *button = visual(grid, "myEffectPreset-" + QString::number(index));
                    if (!button)
                        continue;
                    auto *row = visual(button, "effectPresetLabel");
                    QVERIFY(row);
                    auto *text = row->property("textItem").value<QQuickItem *>();
                    QVERIFY(text);
                    QVERIFY(text->property("lineCount").toInt() <= 2);
                    QCOMPARE(row->property("text").toString(), entry.value("name").toString());
                    QVERIFY(button->height() < grid->property("cellHeight").toReal());
                    const auto bounds = button->mapRectToScene(button->boundingRect());
                    if (!viewport.contains(bounds))
                        continue;
                    ++checked;
                    const auto foreground = text->property("color").value<QColor>();
                    const auto sample = button->mapToScene({button->width() / 2, 2});
                    const auto background =
                        image.pixelColor(int(sample.x() * density), int(sample.y() * density));
                    const auto pixels = QRectF(bounds.topLeft() * density, bounds.size() * density)
                                            .toAlignedRect()
                                            .intersected(image.rect());
                    int inkTop = image.height(), inkBottom = -1;
                    for (int y = pixels.top(); y <= pixels.bottom(); ++y) {
                        double mass = 0;
                        for (int x = pixels.left() + 2; x < pixels.right() - 2; ++x) {
                            const auto color = image.pixelColor(x, y);
                            const double dr = foreground.red() - background.red();
                            const double dg = foreground.green() - background.green();
                            const double db = foreground.blue() - background.blue();
                            const auto alpha = ((color.red() - background.red()) * dr +
                                                (color.green() - background.green()) * dg +
                                                (color.blue() - background.blue()) * db) /
                                               (dr * dr + dg * dg + db * db);
                            mass += qBound(0., alpha, 1.);
                        }
                        if (mass >= 1.) {
                            inkTop = qMin(inkTop, y);
                            inkBottom = qMax(inkBottom, y);
                        }
                    }
                    QVERIFY(inkBottom >= inkTop);
                    const qreal actual = (inkTop + inkBottom + 1) / 2.;
                    const qreal expected = bounds.center().y() * density;
                    QVERIFY2(qAbs(actual - expected) <= 1.,
                             qPrintable(QString("%1: ink center %2, expected %3")
                                            .arg(entry.value("name").toString())
                                            .arg(actual)
                                            .arg(expected)));
                }
                QVERIFY(checked >= 3);
                if (!captures.isEmpty())
                    QVERIFY(image.save(captures + QString("/names-%1-font%2-size%3.png")
                                                      .arg(QTest::currentDataTag())
                                                      .arg(family)
                                                      .arg(fontScale)));
            }
        // Scroll through the real virtual grid and activate the final long name.
        QVERIFY(QMetaObject::invokeMethod(grid, "positionViewAtIndex", Q_ARG(int, names.size() - 1),
                                          Q_ARG(int, 3)));
        const auto lastPath = s.directory.filePath(QString("name-%1.wav").arg(names.size() - 1));
        QQuickItem *last = nullptr;
        QTRY_VERIFY(last = visual(grid, "myEffectPreset-" + QString::number(names.size() - 1)));
        auto *row = visual(last, "effectPresetLabel");
        QVERIFY(row && row->property("truncated").toBool());
        auto *tip = last->findChild<QObject *>("effectPresetToolTip");
        QVERIFY(tip && !tip->property("popup").value<QObject *>());
        QTest::mouseMove(window, last->mapToScene(last->boundingRect().center()).toPoint());
        QTRY_VERIFY(tip->property("visible").toBool());
        QCOMPARE(tip->property("text").toString(), names.last());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          last->mapToScene(last->boundingRect().center()).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending() && audio->impulseResponse() == lastPath,
                                 15000);
        QPointer<QObject> retiredTip(tip);
        const QString stressName = names[0] + " " + names[6];
        // Exercise the same production recipes at every entry. The song/EQ
        // labels and provider delegate are stressed at the presentation boundary;
        // this does not invent upstream responses or replace selection commands.
        QTRY_VERIFY_WITH_TIMEOUT(s.playback.engineReady(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!audio->providers()->busy(), 5000);
        audio->setImpulseEnabled(false);
        QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending(), 15000);
        audio->providers()->importFile(QUrl::fromLocalFile(DSP_TEST_LIBRARY));
        QTRY_VERIFY2_WITH_TIMEOUT(
            !audio->effectPending() && !audio->providerId().isEmpty(),
            qPrintable(audio->errorMessage() + " / " + audio->providers()->errorMessage()), 15000);
        auto &catalog = s.catalog();
        AudioEffectCatalogTestAccess::blockAutomaticRequests(catalog);
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        AudioEffectCatalogTestAccess::page(catalog, s.api, {effect(200, stressName)});
        AudioEffectCatalogTestAccess::page(catalog, s.api, {});
        catalog.selectCategory("headphone");
        AudioEffectCatalogTestAccess::page(catalog, s.api,
                                           {QJsonObject{{"id", 300}, {"name", stressName}}});
        AudioEffectCatalogTestAccess::page(catalog, s.api, {});
        catalog.selectCategory("artist");
        const QStringList sources{"local", "artist", "headphone", "market"};
        for (int source = 1; source < sources.size(); ++source) {
            const auto path = s.directory.filePath(sources[source] + ".wav");
            QVERIFY(QFile::copy(lastPath, path));
            audio->registerImpulseResponse(path, stressName + " " + sources[source],
                                           sources[source] + "-layout");
        }
        int captureIndex = 0;
        const auto checkName = [&](QQuickItem *label, QQuickItem *container, const QString &name) {
            QVERIFY(label && container);
            auto *text = label->property("textItem").value<QQuickItem *>();
            QVERIFY(text);
            QCOMPARE(label->property("text").toString(), name);
            QCOMPARE(label->property("maximumLineCount").toInt(), 2);
            const auto font = label->property("font").value<QFont>();
            QCOMPARE(font.pixelSize(), 12 * theme.globalFontSizeScale() / 100);
            QCOMPARE(label->property("lineHeight").toReal(), font.pixelSize() * 1.25);
            QCOMPARE(text->property("lineCount").toInt(), 2);
            QVERIFY(label->property("truncated").toBool());
            QVERIFY(label->height() <= container->height());
            if (!captures.isEmpty())
                QVERIFY(window->grabWindow().save(
                    captures +
                    QString("/all-%1-%2.png").arg(QTest::currentDataTag()).arg(++captureIndex)));
        };
        for (int fontScale : {100, 125}) {
            theme.setGlobalFontSizeScale(fontScale);
            QVERIFY(popup->setProperty("tab", 0));
            auto *song = visual(window->contentItem(), "songEffect-none");
            QVERIFY(song && song->setProperty("text", stressName));
            QTest::qWait(80);
            checkName(visual(song, "effectPresetLabel"), song, stressName);
            QVERIFY(popup->setProperty("tab", 1));
            auto *eq = visual(window->contentItem(), "equalizerPreset-0");
            QVERIFY(eq && eq->setProperty("text", stressName));
            QTest::qWait(80);
            checkName(visual(eq, "effectPresetLabel"), eq, stressName);
            QVERIFY(popup->setProperty("tab", 2));
            QVERIFY(popup->setProperty("libraryTab", 0));
            auto *providerGrid = visual(window->contentItem(), "dspPresetGrid");
            QVERIFY(providerGrid);
            QVERIFY(QMetaObject::invokeMethod(providerGrid, "positionViewAtIndex", Q_ARG(int, 0),
                                              Q_ARG(int, 0)));
            QQuickItem *option = nullptr;
            QTRY_VERIFY(option = visual(providerGrid, "dspPresetOption-gain"));
            auto *provider = visual(option, "dspPresetButton-gain");
            QVERIFY(provider);
            auto data = option->parentItem()->property("modelData").toMap();
            data["recommendedDevice"] = "headphone";
            data["label"] = stressName;
            QVERIFY(option->parentItem()->setProperty("modelData", data));
            QTest::qWait(80);
            auto *label = visual(option, "dspPresetLabel");
            checkName(label, option, stressName);
            auto *tag = visual(option, "dspPresetDeviceTag");
            auto *settings = visual(option, "dspPresetSettings-gain");
            QVERIFY(tag && tag->isVisible() && settings && settings->isVisible());
            const auto textBounds = label->property("textBox").value<QQuickItem *>();
            QVERIFY(textBounds);
            QVERIFY(textBounds->mapRectToScene(textBounds->boundingRect()).right() <=
                    tag->mapRectToScene(tag->boundingRect()).left());
            QVERIFY(option->height() < providerGrid->property("cellHeight").toReal());
            QVERIFY(popup->setProperty("libraryTab", 1));
            for (int source = 0; source < sources.size(); ++source) {
                QVERIFY(popup->setProperty("librarySourceTab", source));
                QQuickItem *sourceGrid = nullptr;
                QTRY_VERIFY(sourceGrid = visual(window->contentItem(), "myEffectGrid"));
                const int expectedCount = source == 0 ? names.size() : 1;
                QTRY_COMPARE(sourceGrid->property("count").toInt(), expectedCount);
                QTest::qWait(80); // Model replacement retires the previous delegate.
                QQuickItem *item = nullptr;
                QTRY_VERIFY(item = visual(sourceGrid, "myEffectPreset-0"));
                checkName(visual(item, "effectPresetLabel"), item,
                          source == 0 ? names[0] : stressName + " " + sources[source]);
            }
            QVERIFY(popup->setProperty("tab", 3));
            catalog.selectCategory("artist");
            QTest::qWait(100);
            QQuickItem *card = nullptr;
            QTRY_VERIFY(card = visual(window->contentItem(), "effectCardName"));
            auto *cardItem = card->parentItem()->parentItem()->parentItem();
            checkName(card, cardItem, stressName);
            auto *cardTip = cardItem->findChild<QObject *>("effectCardToolTip");
            QVERIFY(cardTip);
            QCOMPARE(cardTip->property("text").toString(), stressName);
            catalog.selectCategory("headphone");
            QTest::qWait(100);
            QQuickItem *brand = nullptr;
            QTRY_VERIFY(brand = visual(window->contentItem(), "effectBrandName"));
            checkName(brand, brand->parentItem()->parentItem()->parentItem(), stressName);
        }
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        QTRY_VERIFY(retiredTip.isNull());
        QVERIFY(warnings.isEmpty());
    }
    void windowedReadsStayBoundedAndRestoreColdAnchors()
    {
        Session s;
        auto &catalog = s.catalog();
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        QJsonArray entries;
        for (int i = 0; i < 10000; ++i)
            entries.append(effect(i + 1, QString("Directory %1").arg(i)));
        AudioEffectCatalogTestAccess::page(catalog, s.api, entries);
        AudioEffectCatalogTestAccess::page(catalog, s.api, {});
        QCOMPARE(catalog.rowCount(), 10000);
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (int i = 0; i < 10000; i += 997) {
                QCOMPARE(row(catalog, i).value("id").toInt(), i + 1);
                QVERIFY(AudioEffectCatalogTestAccess::windowRows(catalog) <= 128);
                QVERIFY(AudioEffectCatalogTestAccess::windowCost(catalog) <= 1024 * 1024);
                QVERIFY(AudioEffectCatalogTestAccess::pendingReads(catalog) <= 4);
            }
            QVERIFY(AudioEffectCatalogTestAccess::verifiedRows(catalog) <= 100);
        }
        const auto key = row(catalog, 7000).value("key").toString();
        catalog.savePosition(catalog.viewId(), key, -9, 420000);
        const auto position = catalog.position();
        catalog.setQuery("Directory 9999");
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.rowCount(), 1);
        QCOMPARE(row(catalog).value("id").toInt(), 10000);
        catalog.setQuery({});
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.indexOfKey(key), 7000);
        QTRY_COMPARE(catalog.indexOfKey("artist|artist-5001"), 5000);
        QCOMPARE(catalog.position(), position);
        catalog.setBrowsingActive(true);
        QTRY_VERIFY(!catalog.searching());
        catalog.setBrowsingActive(false);
        AudioEffectCatalogTestAccess::evictWindow(catalog);
        QCOMPARE(row(catalog, 4321).value("id").toInt(), 4322);
        qInfo() << "Directory count/window rows/window cost/verified rows:" << catalog.cachedCount()
                << AudioEffectCatalogTestAccess::windowRows(catalog)
                << AudioEffectCatalogTestAccess::windowCost(catalog)
                << AudioEffectCatalogTestAccess::verifiedRows(catalog);
    }
    void completelyLostCacheRefetchesWithoutLosingSearchOrPosition()
    {
        Session s;
        auto &catalog = s.catalog();
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        QJsonArray entries;
        for (int i = 0; i < 200; ++i)
            entries.append(effect(i + 1, QString("Recovery %1").arg(i)));
        AudioEffectCatalogTestAccess::page(catalog, s.api, entries);
        AudioEffectCatalogTestAccess::page(catalog, s.api, {});
        catalog.setQuery("Recovery");
        QTRY_VERIFY(!catalog.searching());
        const auto key = row(catalog, 150).value("key").toString();
        catalog.savePosition(catalog.viewId(), key, -7, 9600);
        const auto position = catalog.position();
        catalog.setBrowsingActive(true);
        QTRY_VERIFY(!catalog.searching());
        QSignalSpy notices(&catalog, &AudioEffectCatalog::notice);
        const auto path =
            s.settings.files("audio-effect-catalog", SettingsDomain::Device) + "/catalog.sqlite";
        AudioEffectCatalogTestAccess::loseColdSource(catalog, path);
        catalog.data(catalog.index(150), Qt::UserRole + 1);
        QTRY_VERIFY(AudioEffectCatalogTestAccess::request(catalog));
        QCOMPARE(catalog.query(), QString("Recovery"));
        QCOMPARE(catalog.position(), position);
        QVERIFY(catalog.positionPending());
        QCOMPARE(catalog.rowCount(), 200);
        AudioEffectCatalogTestAccess::page(catalog, s.api, entries);
        AudioEffectCatalogTestAccess::page(catalog, s.api, {});
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.rowCount(), 200);
        QCOMPARE(catalog.indexOfKey(key), 150);
        QVERIFY(!catalog.positionPending());
        QCOMPARE(row(catalog, 150).value("id").toInt(), 151);
        QCOMPARE(catalog.position(), position);
        QCOMPARE(notices.size(), 0);
        catalog.setBrowsingActive(false);
        QVERIFY(AudioEffectCatalogTestAccess::windowRows(catalog) <= 128);
    }
    void unlinkedCachePreservesReadableRecords()
    {
        Session s;
        auto &catalog = s.catalog();
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        AudioEffectCatalogTestAccess::page(catalog, s.api, {effect(1, "Preserved")});
        AudioEffectCatalogTestAccess::page(catalog, s.api, {});
        const auto key = row(catalog).value("key").toString();
        catalog.savePosition(catalog.viewId(), key, -3, 40);
        const auto position = catalog.position();
        const auto path =
            s.settings.files("audio-effect-catalog", SettingsDomain::Device) + "/catalog.sqlite";
        if (!QFile::remove(path))
            QSKIP("Platform prevents deleting an open SQLite file; complete loss is tested "
                  "separately");
        catalog.setQuery("Preserved");
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.rowCount(), 1);
        QCOMPARE(row(catalog).value("name").toString(), QString("Preserved"));
        QVERIFY(QFileInfo::exists(path));
        QCOMPARE(AudioEffectCatalogTestAccess::request(catalog), quint64(0));
        catalog.setQuery({});
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.position(), position);
    }
    void legacyCacheUpgradeAndOversizedRecordsStayReadable()
    {
        QTemporaryDir directory;
        const auto settings = directory.filePath("settings.ini");
        QString path;
        {
            Session s(settings);
            auto &catalog = s.catalog();
            QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
            path = s.settings.files("audio-effect-catalog", SettingsDomain::Device) +
                   "/catalog.sqlite";
            QJsonArray rows;
            for (int i = 0; i < 4; ++i) {
                auto value = effect(i + 1, QString("Large %1").arg(i), "Legacy writer");
                value["intro"] = QString(600 * 1024, 'x');
                rows.append(value);
            }
            AudioEffectCatalogTestAccess::page(catalog, s.api, rows);
            AudioEffectCatalogTestAccess::page(catalog, s.api, {});
        }
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "legacy-effect-upgrade");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("ALTER TABLE effect_entries DROP COLUMN search"));
            QVERIFY(q.exec("PRAGMA user_version=1"));
        }
        QSqlDatabase::removeDatabase("legacy-effect-upgrade");
        Session s(settings);
        auto &catalog = s.catalog();
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.cachedCount(), 4);
        for (int index : {0, 3, 1, 0}) {
            const auto value = row(catalog, index);
            QCOMPARE(value.value("id").toInt(), index + 1);
            QCOMPARE(value.value("intro").toString().size(), 600 * 1024);
            QVERIFY(!value.contains("sound_urls"));
        }
        catalog.setQuery("Legacy writer");
        QTRY_VERIFY(!catalog.searching());
        QCOMPARE(catalog.rowCount(), 4);
    }
    void realDirectorySnapshotUsesBoundedWindows()
    {
        const auto source = qEnvironmentVariable("ECHO_EFFECT_CACHE_SNAPSHOT");
        if (source.isEmpty())
            QSKIP("Explicit read-only public-directory snapshot benchmark");
        Session session;
        QTemporaryDir directory;
        const auto path = directory.filePath("catalog.sqlite");
        QVERIFY(QFile::copy(source, path));
        AudioEffectCatalog catalog(session.api, *session.playback.audioControls(), nullptr, path);
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        QTRY_VERIFY(!catalog.searching());
        for (const auto &category : QStringList{"artist", "headphone", "market"}) {
            catalog.selectCategory(category);
            AudioEffectCatalogTestAccess::pauseAutomaticRequests(catalog);
            for (int sort : category == "market" ? QList<int>{2, 3, 4} : QList<int>{2}) {
                catalog.setSort(sort);
                QTRY_VERIFY(!catalog.searching());
                const int count = catalog.rowCount();
                QVERIFY(count > 0);
                for (int cycle = 0; cycle < 3; ++cycle)
                    for (int index : QList<int>{0, count / 2, count - 1}) {
                        const auto value = row(catalog, index);
                        QVERIFY(!value.value("name").toString().isEmpty());
                        QVERIFY(!value.contains("sound_urls"));
                        QVERIFY(AudioEffectCatalogTestAccess::windowRows(catalog) <= 128);
                        QVERIFY(AudioEffectCatalogTestAccess::windowCost(catalog) <= 1024 * 1024);
                    }
                const auto last = row(catalog, count - 1);
                QElapsedTimer elapsed;
                elapsed.start();
                catalog.setQuery(last.value("name").toString());
                QTRY_VERIFY(!catalog.searching());
                QTRY_VERIFY(catalog.indexOfKey(last.value("key").toString()) >= 0);
                qInfo() << "Public snapshot category/sort/count/query-ms/hot-rows/hot-cost:"
                        << category << sort << count << elapsed.elapsed()
                        << AudioEffectCatalogTestAccess::windowRows(catalog)
                        << AudioEffectCatalogTestAccess::windowCost(catalog);
                catalog.setQuery({});
                QTRY_VERIFY(!catalog.searching());
                QCOMPARE(catalog.rowCount(), count);
            }
        }
        QSignalSpy closed(&catalog, &AudioEffectCatalog::closed);
        catalog.shutdown();
        QTRY_VERIFY(!closed.isEmpty());
    }
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
        QTRY_COMPARE(catalog.rowCount(), 3);
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
    void cachedRevalidationKeepsStableRowsAndCovers()
    {
        Session s;
        QAbstractItemModelTester tester(&s.catalog(),
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto &catalog = s.catalog();
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        QJsonArray items;
        for (int i = 0; i < 100; ++i)
            items.append(effect(i + 1, QString("Effect %1").arg(i)));
        AudioEffectCatalogTestAccess::page(catalog, s.api, items);
        QSignalSpy resets(&catalog, &QAbstractItemModel::modelReset);
        QSignalSpy changes(&catalog, &QAbstractItemModel::dataChanged);
        QSignalSpy inserted(&catalog, &QAbstractItemModel::rowsInserted);
        const auto firstKey = row(catalog).value("key").toString();
        AudioEffectCatalogTestAccess::refreshGeneration(catalog);
        auto refreshed = items.first().toObject();
        refreshed["author"] = "Updated author";
        AudioEffectCatalogTestAccess::page(catalog, s.api, {refreshed});
        QCOMPARE(resets.size(), 0);
        QVERIFY(!changes.isEmpty());
        QCOMPARE(catalog.keyAt(0), firstKey);
        QCOMPARE(row(catalog).value("author").toString(), QString("Updated author"));
        AudioEffectCatalogTestAccess::page(catalog, s.api, {effect(101, "New effect")});
        QCOMPARE(resets.size(), 0);
        QCOMPARE(inserted.size(), 1);
        QCOMPARE(catalog.rowCount(), 101);
        AudioEffectCatalogTestAccess::page(catalog, s.api, {});
        QCOMPARE(catalog.rowCount(), 2);
        QPersistentModelIndex first(catalog.index(0));
        AudioEffectCatalogTestAccess::refreshGeneration(catalog);
        AudioEffectCatalogTestAccess::page(catalog, s.api,
                                           {effect(101, "New effect"), effect(1, "Effect 0")});
        QCOMPARE(resets.size(), 0);
        QVERIFY(first.isValid());
        QCOMPARE(first.row(), 1);
        QCOMPARE(row(catalog, first.row()).value("id").toInt(), 1);
    }
    void unobservedProjectionKeepsMetadataUpdatesIncremental()
    {
        Session s;
        auto &catalog = s.catalog();
        QAbstractItemModelTester tester(&catalog,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        AudioEffectCatalogTestAccess::page(catalog, s.api,
                                           {effect(1, "Stable"), effect(2, "Other")});
        QSignalSpy resets(&catalog, &QAbstractItemModel::modelReset);
        AudioEffectCatalogTestAccess::unobservedProjection(catalog);
        AudioEffectCatalogTestAccess::evictWindow(catalog);
        QCOMPARE(row(catalog, 1).value("id").toInt(), 2);
        AudioEffectCatalogTestAccess::refreshGeneration(catalog);
        AudioEffectCatalogTestAccess::page(catalog, s.api, {effect(1, "Stable", "Updated")});
        QCOMPARE(resets.size(), 0);
        QCOMPARE(row(catalog).value("author").toString(), QString("Updated"));
    }
    void combinedEffectsExposeIrWithoutClaimingVpfSupport()
    {
        Session s;
        auto &catalog = s.catalog();
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        auto combined = effect(1, "Combined");
        combined.remove("unavailable_reason");
        combined["vpf_urls"] = QJsonArray{"https://fixture.kugou.com/effect.vpf"};
        auto vpfOnly = combined;
        vpfOnly["id"] = 2;
        vpfOnly["sound_urls"] = QJsonArray{};
        auto denied = combined;
        denied["id"] = 3;
        denied["unavailable_reason"] = "Account restriction";
        AudioEffectCatalogTestAccess::page(catalog, s.api, {combined, vpfOnly, denied});
        QVERIFY(row(catalog, 0).value("unavailable_reason").toString().isEmpty());
        QVERIFY(row(catalog, 0).value("partial").toBool());
        QVERIFY(row(catalog, 1).value("unavailable_reason").toString().contains("VPF"));
        QCOMPARE(row(catalog, 2).value("unavailable_reason").toString(),
                 QString("Account restriction"));
        QSignalSpy notices(&catalog, &AudioEffectCatalog::notice);
        catalog.activate(1);
        QVERIFY(!catalog.downloading());
        QCOMPARE(notices.size(), 1);
        catalog.activate(2);
        QCOMPARE(notices.last().first().toString(), QString("Account restriction"));
        const auto path = s.directory.filePath("downloaded.irs");
        const auto vpf = s.directory.filePath("effect.vpf");
        for (const auto &file : {path, vpf}) {
            QFile output(file);
            QVERIFY(output.open(QIODevice::WriteOnly));
            output.write("domain-only registration");
        }
        auto *audio = s.playback.audioControls();
        audio->registerImpulseResponse(path, "Combined", "artist-1", true, vpf);
        QVERIFY(audio->partialImpulseResponse(path));
        QCOMPARE(audio->vpfImpulseResponse(path), vpf);
        QVERIFY(row(catalog).value("partial").toBool());
        QVERIFY(audio->impulseResponses().first().toMap().value("partial").toBool());
    }
    void liveCombinedResourceDownload()
    {
        if (!qEnvironmentVariableIsSet("ECHO_EFFECT_SEARCH_LIVE"))
            QSKIP("Explicit live resource download only");
        const auto library = qEnvironmentVariable("ECHO_DSP_TEST_PROVIDER");
        if (library.isEmpty())
            QSKIP("Requires an explicitly supplied private Provider");
        Session s;
        auto *audio = s.playback.audioControls();
        QTRY_VERIFY_WITH_TIMEOUT(!audio->providers()->busy(), 10000);
        audio->providers()->importFile(QUrl::fromLocalFile(library));
        QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending() && audio->supportsVpf(), 15000);
        auto &catalog = s.catalog();
        catalog.selectCategory("market");
        catalog.setBrowsingActive(true);
        QVariantMap selected;
        const bool found = QTest::qWaitFor(
            [&] {
                for (int i = 0; i < catalog.rowCount(); ++i) {
                    const auto candidate = row(catalog, i);
                    if (!candidate.value("sound_urls").toList().isEmpty() &&
                        !candidate.value("vpf_urls").toList().isEmpty() &&
                        candidate.value("unavailable_reason").toString().isEmpty() &&
                        !candidate.value("needs_validation").toBool()) {
                        selected = candidate;
                        return true;
                    }
                }
                return !catalog.errorMessage().isEmpty() || catalog.complete();
            },
            60000);
        QVERIFY(found);
        QVERIFY2(!selected.isEmpty(), qPrintable(catalog.errorMessage()));
        QVERIFY(!selected.value("partial").toBool());
        QSignalSpy downloaded(&catalog, &AudioEffectCatalog::downloaded);
        catalog.activateKey(selected.value("key").toString());
        QTRY_VERIFY_WITH_TIMEOUT(!catalog.downloading(), 45000);
        QVERIFY2(!downloaded.isEmpty(), qPrintable(catalog.errorMessage()));
        const auto identity = downloaded.first().first().toString();
        const auto ir = audio->downloadedImpulseResponse(identity);
        const auto vpf = audio->vpfImpulseResponse(ir);
        QVERIFY(QFileInfo(ir).size() > 0);
        QVERIFY(QFileInfo(vpf).size() > 0);
        qInfo() << "Live combined resources, bytes:" << QFileInfo(ir).size()
                << QFileInfo(vpf).size();
        catalog.useDownloaded(identity);
        QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending(), 15000);
        QVERIFY2(audio->errorMessage().isEmpty(), qPrintable(audio->errorMessage()));
        QCOMPARE(QJsonDocument::fromJson(
                     audio->graph().value("provider_resource_json").toString().toUtf8())
                     .array()
                     .size(),
                 2);
        audio->deactivateProvider();
        QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending(), 15000);
        QVERIFY2(audio->errorMessage().isEmpty(), qPrintable(audio->errorMessage()));
        QVERIFY(audio->partialImpulseResponse(ir));
        QVERIFY(audio->impulseEnabled());
        catalog.setBrowsingActive(false);
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
            QTRY_VERIFY(catalog.indexOfKey(last.value("key").toString()) >= 0);
            catalog.setQuery({});
            QTRY_COMPARE(catalog.rowCount(), count);
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
        QVERIFY(catalog.loading());
        QTRY_VERIFY(AudioEffectCatalogTestAccess::request(catalog));
        catalog.setBrowsingActive(false);
        QVERIFY(!catalog.loading());
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
        AudioEffectCatalogTestAccess::blockAutomaticRequests(catalog);
        QTRY_VERIFY(AudioEffectCatalogTestAccess::ready(catalog));
        QJsonArray rows;
        for (int i = 0; i < 1000; ++i)
            rows.append(effect(i + 1, QString("Effect %1").arg(i)));
        AudioEffectCatalogTestAccess::page(catalog, s.api, rows);
        const auto coverPath = s.directory.filePath("effect-cover.png");
        QImage coverImage(40, 40, QImage::Format_ARGB32_Premultiplied);
        coverImage.fill(QColor("#64b920"));
        QVERIFY(coverImage.save(coverPath));
        auto firstRow = rows.first().toObject();
        firstRow["icon_url"] = QUrl::fromLocalFile(coverPath).toString();
        AudioEffectCatalogTestAccess::refreshGeneration(catalog);
        AudioEffectCatalogTestAccess::page(catalog, s.api, {firstRow});
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
        QVERIFY(!visual(window->contentItem(), "songEffect-none"));
        QVERIFY(!visual(window->contentItem(), "equalizerBand-0"));
        QVERIFY(!visual(window->contentItem(), "impulseMixMode"));
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
        QPointer<QQuickItem> stableCover = visual(window->contentItem(), "effectCardCover");
        QVERIFY(stableCover);
        QTRY_COMPARE(stableCover->property("status").toInt(), 1);
        QSignalSpy imageStatus(stableCover, SIGNAL(statusChanged()));
        QSignalSpy modelResets(&catalog, &QAbstractItemModel::modelReset);
        for (int page = 0; page < 3; ++page) {
            AudioEffectCatalogTestAccess::refreshGeneration(catalog);
            firstRow["author"] = QString("Author %1").arg(page);
            AudioEffectCatalogTestAccess::page(catalog, s.api, {firstRow});
            QCoreApplication::processEvents();
            QVERIFY(stableCover);
            QCOMPARE(stableCover->property("status").toInt(), 1);
        }
        QCOMPARE(modelResets.size(), 0);
        QCOMPARE(imageStatus.size(), 0);
        auto *refresh = visual(window->contentItem(), "effectPlazaRefresh");
        QPointer<QQuickItem> refreshIcon = visual(window->contentItem(), "effectPlazaRefreshIcon");
        QVERIFY(refresh && refreshIcon);
        auto *rotation = refreshIcon->findChild<QObject *>("spinnerRotation");
        QVERIFY(rotation);
        QTRY_COMPARE(refreshIcon->property("status").toInt(), 1);
        theme.setMotionEnabled(true);
        catalog.setBrowsingActive(true);
        AudioEffectCatalogTestAccess::pauseAutomaticRequests(catalog);
        QVERIFY(catalog.loading());
        QTRY_VERIFY(rotation->property("running").toBool());
        QCOMPARE(refreshIcon->property("name").toString(), QString("verification-spinner"));
        QTRY_COMPARE(refreshIcon->property("status").toInt(), 1);
        QSignalSpy refreshImageStatus(refreshIcon, SIGNAL(statusChanged()));
        QVERIFY(!refresh->isEnabled());
        const auto refreshOpacity = refreshIcon->opacity();
        for (int page = 0; page < 3; ++page) {
            AudioEffectCatalogTestAccess::refreshGeneration(catalog);
            AudioEffectCatalogTestAccess::page(catalog, s.api, {firstRow});
            AudioEffectCatalogTestAccess::pauseAutomaticRequests(catalog);
            QCOMPARE(AudioEffectCatalogTestAccess::request(catalog), quint64(0));
            QVERIFY(catalog.loading());
            QCoreApplication::processEvents();
            QCOMPARE(visual(window->contentItem(), "effectPlazaRefreshIcon"), refreshIcon.data());
            QVERIFY(!refresh->isEnabled());
            QCOMPARE(refreshIcon->opacity(), refreshOpacity);
            QVERIFY(rotation->property("running").toBool());
        }
        const auto refreshFrame = [&] {
            const auto image = window->grabWindow();
            const auto bounds = refresh->mapRectToScene(refresh->boundingRect());
            const auto density = qreal(image.width()) / window->width();
            return image.copy(QRectF(bounds.x() * density, bounds.y() * density,
                                     bounds.width() * density, bounds.height() * density)
                                  .toAlignedRect());
        };
        const auto firstRefreshFrame = refreshFrame();
        QTest::qWait(130);
        const auto secondRefreshFrame = refreshFrame();
        QVERIFY(!firstRefreshFrame.isNull());
        QVERIFY(firstRefreshFrame != secondRefreshFrame);
        QCOMPARE(refreshImageStatus.size(), 0);
        const auto captures = qEnvironmentVariable("ECHO_EFFECT_SEARCH_CAPTURES");
        if (!captures.isEmpty()) {
            QDir().mkpath(captures);
            QVERIFY(firstRefreshFrame.save(captures + "/refresh-first-" + QTest::currentDataTag() +
                                           ".png"));
            QVERIFY(secondRefreshFrame.save(captures + "/refresh-second-" +
                                            QTest::currentDataTag() + ".png"));
        }
        theme.setMotionEnabled(false);
        QTRY_VERIFY(!rotation->property("running").toBool());
        theme.setMotionEnabled(true);
        QTRY_VERIFY(rotation->property("running").toBool());
        window->hide();
        QTRY_VERIFY(!rotation->property("running").toBool());
        window->show();
        QVERIFY(Test::prepareNativeInput(window));
        QTRY_VERIFY(rotation->property("running").toBool());
        // Finish the controlled directory without losing the rows used by search below.
        rows[0] = firstRow;
        AudioEffectCatalogTestAccess::page(catalog, s.api, rows);
        AudioEffectCatalogTestAccess::page(catalog, s.api, {});
        QVERIFY(!catalog.loading());
        QTRY_VERIFY(!rotation->property("running").toBool());
        QVERIFY(refresh->isEnabled());
        QCOMPARE(refreshIcon->property("name").toString(), QString("refresh"));
        catalog.setBrowsingActive(false);
        theme.setMotionEnabled(false);
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
        if (!captures.isEmpty()) {
            QDir().mkpath(captures);
            Test::moveNativePointer(window, QPoint(10, 10));
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
        auto *popup = root->findChild<QObject *>("effectSearchPopup");
        QVERIFY(popup);
        QPointer<QQuickItem> retiredPlaza = visual(window->contentItem(), "effectPlaza");
        QVERIFY(popup->setProperty("tab", 2));
        QTRY_VERIFY(retiredPlaza.isNull());
        QVERIFY(!catalog.browsingActive());
        QQuickItem *mixMode = nullptr;
        QTRY_VERIFY((mixMode = visual(window->contentItem(), "impulseMixMode")));
        QTRY_VERIFY(mixMode->isVisible());
        QVERIFY(mixMode->width() > 0 && mixMode->height() >= 32);
        auto *pageHost = visual(window->contentItem(), "effectPaneHost");
        auto *editor = visual(window->contentItem(), "dspPresetControlsPanel");
        QVERIFY(pageHost && editor);
        QCOMPARE(editor->mapToScene(QPointF()), pageHost->mapToScene(QPointF()));
        QCOMPARE(editor->size(), pageHost->size());
        QCOMPARE(s.playback.audioControls()->impulseMix(), 1.0);
        QCOMPARE(mixMode->property("value").toString(), QString("blend"));
        QVERIFY(
            QMetaObject::invokeMethod(mixMode, "valueEdited", Q_ARG(QVariant, QVariant("reverb"))));
        QCOMPARE(s.playback.audioControls()->impulseMixMode(), QString("reverb"));
        QCOMPARE(mixMode->property("value").toString(), QString("reverb"));
        if (!captures.isEmpty()) {
            Test::moveNativePointer(window, QPoint(10, 10));
            window->update();
            QTest::qWait(80);
            QVERIFY(
                window->grabWindow().save(captures + "/mix-" + QTest::currentDataTag() + ".png"));
        }
        // Changing views must release the entire old page, including hidden
        // controls, while business values and directory position survive.
        const QStringList pages{"effectSongPane", "effectEqualizerPane", "effectLibraryPane",
                                "effectPlaza"};
        for (int cycle = 0; cycle < 3; ++cycle) {
            QVERIFY(popup->setProperty("tab", 2));
            auto *library = visual(window->contentItem(), pages[2]);
            QVERIFY(library);
            QVERIFY(QMetaObject::invokeMethod(library, "sourcePicked", Q_ARG(int, 2)));
            QCOMPARE(library->property("sourceTab").toInt(), 2);
            for (int tab : {0, 1, 3, 2}) {
                QPointer<QQuickItem> retired;
                for (const auto &name : pages)
                    if (auto *item = visual(window->contentItem(), name))
                        retired = item;
                QVERIFY(popup->setProperty("tab", tab));
                QTRY_VERIFY(retired.isNull());
                for (int page = 0; page < pages.size(); ++page)
                    QCOMPARE(visual(window->contentItem(), pages[page]) != nullptr, page == tab);
                if (tab != 2)
                    QVERIFY(!visual(window->contentItem(), "dspPresetControlsPanel"));
                QCOMPARE(catalog.browsingActive(), tab == 3);
                if (tab == 3) {
                    QTRY_COMPARE(catalog.query(), QString("Effect 999"));
                    QTRY_VERIFY((search = visual(window->contentItem(), "effectPlazaSearch")));
                    QCOMPARE(search->property("text").toString(), QString("Effect 999"));
                    QCOMPARE(catalog.rowCount(), 1);
                }
            }
            library = visual(window->contentItem(), pages[2]);
            QVERIFY(library);
            QCOMPARE(library->property("sourceTab").toInt(), 2);
            QCOMPARE(s.playback.audioControls()->impulseMixMode(), QString("reverb"));
        }
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
