#include "AccountUiFixture.h"
#include "SearchModel.h"
#include "SearchResultModel.h"
#include "SearchRouteCoordinator.h"
#include "RouteController.h"
#include "ShellSettings.h"
#include "ShellController.h"
#include "ThemeStore.h"
#include "NativeWindow.h"
#include "RenderedInk.h"
#include "UiTypography.h"
#include "WindowController.h"
#include <QAbstractItemModelTester>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlIncubationController>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QQuickStyle>
#include <QSGRendererInterface>
#include <QPropertyAnimation>
#include <QEventLoop>
#include <QTimer>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTextLayout>
#include <QtTest>
#include <algorithm>
#include <functional>
#include <limits>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#endif

using namespace EchoMusic;
namespace EchoMusic
{
class SearchModelTestAccess
{
public:
    /// Canonical display-only load. This is not a fabricated provider fixture;
    /// production API conversion and network latency are outside this benchmark.
    static void populate(SearchModel &model, SearchModel::Kind kind, int count,
                         const QString &coverDirectory = {},
                         const QString &keyword = QStringLiteral("列表验证"))
    {
        model.cancelRequests();
        model.beginResetModel();
        model.m_kind = kind;
        model.m_keyword = keyword;
        auto &bucket = model.active();
        bucket = {};
        bucket.rows.reserve(count);
        for (int i = 0; i < count; ++i)
            bucket.rows.append(
                {QStringLiteral("歌曲 %1：长标题与中文排版验证").arg(i), QStringLiteral("歌手"),
                 QStringLiteral("专辑"),
                 coverDirectory.isEmpty()
                     ? QString{}
                     : QUrl::fromLocalFile(coverDirectory + QStringLiteral("/%1.png").arg(i % 128))
                           .toString(),
                 QStringLiteral("3:05"), QStringLiteral("歌手 • 12 首歌曲"), QString::number(i + 1),
                 185000});
        bucket.state = SearchModel::State::Ready;
        bucket.loaded = true;
        bucket.total = quint64(count);
        model.endResetModel();
        emit model.changed();
    }
    static void originalFirst(SearchModel &model)
    {
        model.active().rows[0].original = true;
        emit model.dataChanged(model.index(0), model.index(0), {SearchModel::Original});
    }
    static void showLoading(SearchModel &model)
    {
        populate(model, SearchModel::Kind::Song, 0);
        model.active().state = SearchModel::State::Loading;
        emit model.changed();
    }
    static void populateDiscovery(SearchModel &model)
    {
        // Canonical UI data only; no upstream API conversion is inferred.
        QJsonArray keywords;
        for (const auto *keyword : {"周杰伦", "陈奕迅", "林俊杰", "五月天", "邓紫棋", "薛之谦"})
            keywords.append(QJsonObject{{"keyword", QString::fromUtf8(keyword)}, {"reason", ""}});
        model.m_hotRevision = model.m_api.sessionRevision();
        model.m_hotRequest = model.m_api.capabilities(); // Local identity seam, no HTTP.
        model.succeeded(model.m_hotRequest,
                        QJsonDocument(QJsonArray{QJsonObject{{"title", QStringLiteral("热门搜索")},
                                                             {"keywords", keywords}}})
                            .toJson());
    }
};
} // namespace EchoMusic
static QQuickItem *findVisual(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *item : root->childItems())
        if (auto *found = findVisual(item, name))
            return found;
    return nullptr;
}
static int countVisual(QQuickItem *root, const QString &name)
{
    int count = root->objectName() == name ? 1 : 0;
    for (auto *item : root->childItems())
        count += countVisual(item, name);
    return count;
}
static bool visibleIconsReady(QQuickItem *root)
{
    if (!root->isVisible())
        return true;
    if (root->objectName() == "iconImage" && !root->property("source").toUrl().isEmpty() &&
        root->property("status").toInt() != 1)
        return false; // Image.Ready
    for (auto *item : root->childItems())
        if (!visibleIconsReady(item))
            return false;
    return true;
}
static qint64 residentBytes()
{
#ifdef Q_OS_MACOS
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t size = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                  &size) == KERN_SUCCESS)
        return qint64(info.resident_size);
#endif
    return -1;
}
struct Session
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings{directory.filePath("settings.ini")};
    ThemeStore theme{settings};
    ShellSettings shell{settings};
    WindowController windows{settings, false};
    ApiClient api{DeviceIdentity::temporary()};
    Test::AccountUiFixture accountUi{api};
    SearchModel search{api};
    RouteController routes;
    ShellController shellController{settings, routes};
    SearchRouteCoordinator coordinator{search, routes, false};
    QQmlEngine engine;
    QScopedPointer<QObject> root;
    QQuickWindow *window = nullptr;
    Session()
    {
        theme.attachToEngine(engine);
        // Same feature registration as Application, before the first route.
        routes.registerPage({"themes",
                             "/main/themes",
                             QUrl("qrc:/qt/qml/EchoMusic/Themes/ThemeCenterPage.qml"),
                             {},
                             false,
                             false,
                             false,
                             {}});
        routes.navigate("search", {{"q", "列表验证"}});
    }
    bool create()
    {
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/App/Main.qml"));
        root.reset(component.createWithInitialProperties(
            {{"libraryController", QVariant::fromValue(&accountUi.library)},
             {"accountController", QVariant::fromValue(&accountUi.controller)},
             {"shellController", QVariant::fromValue(&shellController)},
             {"router", QVariant::fromValue(&routes)},
             {"shellSettings", QVariant::fromValue(&shell)},
             {"windowService", QVariant::fromValue(&windows)},
             {"searchService", QVariant::fromValue(&search)}}));
        if (!root) {
            qWarning().noquote() << component.errorString();
            return false;
        }
        window = qobject_cast<QQuickWindow *>(root.data());
        windows.attach(window);
        window->resize(1150, 750);
        return window;
    }
    ~Session()
    {
        routes.shutdown();
        search.shutdown();
        windows.shutdown();
        api.shutdown();
        if (window)
            window->close();
    }
    QQuickItem *item(const QString &name)
    {
        return findVisual(window->contentItem(), name);
    }
};
class SearchUiTest : public QObject
{
    Q_OBJECT
private slots:
    void skeletonLayoutsAcrossThemesAndWidths()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ThemeStore theme(prefs);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList errors;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &warnings) {
            for (const auto &warning : warnings)
                errors.append(warning.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Music
import EchoMusic.Search
Window {
 id: window
 width:1000; height:800; visible:true
 property int mode:0
 property real uiScale:1
 Rectangle {anchors.fill:parent;color:Theme.main}
 Item {
  width:parent.width/window.uiScale;height:parent.height/window.uiScale
  scale:window.uiScale;transformOrigin:Item.TopLeft
  SongListGeometry {id: skeletonGeometry}
  SearchResultsSkeleton {
   x:40;y:16;width:parent.width-80;height:implicitHeight
   visible:window.mode<3;songs:window.mode===0;video:window.mode===2;geometry:skeletonGeometry
  }
  Column {
   x:24;y:20;width:parent.width-48;spacing:20;visible:window.mode===3
   ContentSkeleton {width:parent.width;compact:true}
   Row {
    width:parent.width;spacing:20
    Repeater {model:3;ContentSkeleton {width:(parent.width-40)/3}}
   }
  }
 }
})",
                          QUrl("qrc:/skeleton-layouts.qml"));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->raise();
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(200);
        const auto artifacts = qEnvironmentVariable("ECHO_SKELETON_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        for (bool dark : {false, true})
            for (double scale : {1., 1.25})
                for (int mode = 0; mode < 4; ++mode) {
                    theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                    root->setProperty("uiScale", scale);
                    root->setProperty("mode", mode);
                    QTest::qWait(80);
                    if (mode == 0) {
                        window->grabWindow();
                        QList<QRectF> rows;
                        const auto collect = [&](auto &&self, QQuickItem *item) -> void {
                            if (item->objectName() == "songSkeletonRow" && item->isVisible())
                                rows.append(item->mapRectToScene(item->boundingRect()));
                            for (auto *child : item->childItems())
                                self(self, child);
                        };
                        collect(collect, window->contentItem());
                        QCOMPARE(rows.size(), 10);
                        std::sort(rows.begin(), rows.end(),
                                  [](const auto &a, const auto &b) { return a.top() < b.top(); });
                        for (int r = 0; r < rows.size(); ++r) {
                            QVERIFY(qAbs(rows[r].height() - 60 * scale) < .5);
                            if (r)
                                QVERIFY(rows[r].top() >= rows[r - 1].bottom() - .5);
                        }
                    }
                    if (!dark && scale == 1. && mode == 0) {
                        window->grabWindow(); // Complete initial layout/polish.
                        QTest::qWait(100);
                        const auto first = window->grabWindow();
                        QTest::qWait(600);
                        const auto second = window->grabWindow();
                        if (window->rendererInterface()->graphicsApi() !=
                            QSGRendererInterface::Software)
                            QVERIFY2(first != second, "Visible shimmer must advance actual pixels");
                        if (!artifacts.isEmpty()) {
                            first.save(artifacts + "/phase-a.png");
                            second.save(artifacts + "/phase-b.png");
                        }
                    }

                    if (!artifacts.isEmpty())
                        QVERIFY(
                            window->grabWindow().save(artifacts + QString("/skeleton-%1-%2-%3.png")
                                                                      .arg(mode)
                                                                      .arg(dark ? "dark" : "light")
                                                                      .arg(scale)));
                }
        window->resize(650, 800);
        for (int mode = 0; mode < 4; ++mode) {
            root->setProperty("mode", mode);
            QTest::qWait(40);
        }
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
    void songOrdinalGeometry()
    {
        Session session;
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QQmlComponent component(
            &session.engine, QUrl("qrc:/qt/qml/EchoMusic/SearchUiTests/SongGeometryHarness.qml"));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        auto *row = findVisual(window->contentItem(), "songDelegate");
        auto *number = findVisual(row, "songIndexText");
        auto *play = findVisual(row, "songIndexPlay");
        auto *column = findVisual(row, "songIndexColumn");
        auto *headerColumn = findVisual(window->contentItem(), "songHeaderIndexColumn");
        auto *skeletonColumn = findVisual(window->contentItem(), "songSkeletonIndexColumn");
        auto *cover = findVisual(row, "songCover");
        auto *geometry = qvariant_cast<QObject *>(row->property("geometry"));
        QVERIFY(number && play && column && headerColumn && skeletonColumn && cover);
        // Reflow the same scene through all boundaries. Recreating fourteen
        // native windows tests Cocoa activation churn, not ordinal rendering.
        for (int ordinal : {1, 9, 10, 99, 100, 999, 1000, 9999, 10000, 99999, 100000, 999999,
                            1000000, std::numeric_limits<int>::max()}) {
            root->setProperty("ordinal", ordinal);
            for (qreal scale : {1.0, 1.25, 2.0}) {
                root->setProperty("uiScale", scale);
                QTRY_COMPARE(column->width(), geometry->property("indexWidth").toReal());
                QTRY_VERIFY(column->width() >= 40);
                QTRY_COMPARE(headerColumn->width(), column->width());
                QTRY_COMPARE(skeletonColumn->width(), column->width());
                QCOMPARE(number->property("text").toString(), QString::number(ordinal));
                QCOMPARE(number->property("font").value<QFont>().pixelSize(), 12);
                QVERIFY(!number->property("truncated").toBool());
                QVERIFY(number->x() >= 0);
                QVERIFY(number->x() + number->width() <= column->width() - 8);
                QTRY_COMPARE(cover->mapToItem(row, QPointF()).x(), column->width());
                QCOMPARE(play->mapToItem(row, QPointF(play->width() / 2, 0)).x(), qreal(16));
                if (ordinal < 10000) {
                    QCOMPARE(column->width(), qreal(40));
                    QVERIFY(qAbs(number->x() + number->width() / 2 - 16) < 0.01);
                }
            }
        }
        if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
            QVERIFY(Test::prepareNativeInput(window));
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
            window->update();
            QTRY_VERIFY(!frames.isEmpty());
            const QPoint outsideRow(window->width() / 2, window->height() - 20);
            // QtGui discards moves whose global position equals the previous
            // window's last synthetic move. Establish two distinct positions
            // after Cocoa's Enter event before asserting the new scene state.
            Test::moveNativePointer(window, outsideRow + QPoint(2, 0));
            Test::moveNativePointer(window, outsideRow);
            QTRY_VERIFY(!row->property("hovered").toBool());
            Test::moveNativePointer(window, row->mapToScene(QPointF(16, 30)).toPoint());
            QTRY_VERIFY(play->isVisible());
            QVERIFY(!number->isVisible());
            QTRY_VERIFY(play->property("hovered").toBool());
            auto *playBackground = play->property("background").value<QQuickItem *>();
            QVERIFY(playBackground);
            QCOMPARE(playBackground->property("color").value<QColor>().alpha(), 0);
            Test::moveNativePointer(window, outsideRow + QPoint(2, 0));
            Test::moveNativePointer(window, outsideRow);
            QTRY_VERIFY(number->isVisible());
        }
        QCOMPARE(warnings.count(), 0);
    }
    void songOrdinalFontChangeReflowsSharedColumns()
    {
        Session session;
        QQmlComponent component(
            &session.engine, QUrl("qrc:/qt/qml/EchoMusic/SearchUiTests/SongGeometryHarness.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties({{"ordinal", 100000}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        auto *row = findVisual(window->contentItem(), "songDelegate");
        auto *column = findVisual(row, "songIndexColumn");
        auto *geometry = qvariant_cast<QObject *>(row->property("geometry"));
        auto *metrics = qvariant_cast<QObject *>(geometry->property("digitMetrics"));
        QVERIFY(metrics);
        QTRY_VERIFY(column->width() > 40);
        const auto before = column->width();
        // A typography notification must reflow the shared geometry, rather
        // than merely updating delegate text and leaving a stale column width.
        auto font = metrics->property("font").value<QFont>();
        font.setPixelSize(24);
        QVERIFY(metrics->setProperty("font", font));
        QTRY_VERIFY(column->width() > before);
        QTRY_COMPARE(findVisual(window->contentItem(), "songHeaderIndexColumn")->width(),
                     column->width());
        QTRY_COMPARE(findVisual(window->contentItem(), "songSkeletonIndexColumn")->width(),
                     column->width());
        auto *number = findVisual(row, "songIndexText");
        QCOMPARE(number->property("font").value<QFont>().pixelSize(), 24);
        QVERIFY(number->x() + number->width() <= column->width() - 8);
    }
    void originalLabelsAreOptInAndSearchEnablesThem()
    {
        Session session;
        QVERIFY(session.create());
        SearchModelTestAccess::populate(session.search, SearchModel::Kind::Song, 2);
        SearchModelTestAccess::originalFirst(session.search);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = session.item("searchResults")));
        QTRY_COMPARE(view->property("count").toInt(), 2);
        QQuickItem *tag = nullptr;
        QTRY_VERIFY((tag = findVisual(view, "songOriginalTag")));
        QTRY_VERIFY(tag->isVisible());
        QCOMPARE(tag->property("text").toString(), QString("原唱"));
        QVERIFY(view->setProperty("showOriginal", false));
        QTRY_VERIFY(!findVisual(view, "songOriginalTag"));
        QVERIFY(view->setProperty("showOriginal", true));
        QTRY_VERIFY((tag = findVisual(view, "songOriginalTag")));
        QTRY_VERIFY(tag->isVisible());
    }
    void productionOrdinalsSurviveFilterSortAndReuse_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("songs") << int(SearchModel::Kind::Song);
        QTest::newRow("lyrics") << int(SearchModel::Kind::Lyric);
    }
    void productionOrdinalsSurviveFilterSortAndReuse()
    {
        QFETCH(int, kind);
        Session session;
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QVERIFY(session.create());
        SearchModelTestAccess::populate(session.search, SearchModel::Kind(kind), 100000);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = session.item("searchResults")));
        QTRY_COMPARE(view->property("count").toInt(), 100000);
        auto *geometry = qvariant_cast<QObject *>(view->property("geometry"));
        QVERIFY(geometry);
        QTRY_VERIFY(geometry->property("indexWidth").toReal() > 40);
        const auto verifyRows = [&]() {
            int checked = 0;
            bool ready = true;
            const auto visit = [&](auto &&self, QQuickItem *item) -> void {
                if (item->objectName() == "songDelegate" && !item->property("pooled").toBool()) {
                    const int index = item->property("index").toInt();
                    if (index >= 0 && index < session.search.results()->rowCount()) {
                        auto *number = findVisual(item, "songIndexText");
                        ready &= number &&
                                 number->property("text").toString() == QString::number(index + 1);
                        ready &= item->property("resourceId").toString() ==
                                 session.search.results()
                                     ->index(index, 0)
                                     .data(SearchModel::ResourceId)
                                     .toString();
                        ++checked;
                    }
                }
                for (auto *child : item->childItems())
                    self(self, child);
            };
            visit(visit, view);
            auto *headerColumn = session.item("songHeaderIndexColumn");
            return ready && checked > 0 && countVisual(view, "songDelegate") < 70 && headerColumn &&
                   headerColumn->width() == geometry->property("indexWidth").toReal();
        };
        QTRY_VERIFY(countVisual(view, "songDelegate") > 0);
        // Incubation assigns required roles before nested text bindings settle.
        // Re-snapshot the live tree each poll; never retain raw delegate pointers
        // across an event-loop wait while ListView may pool/destroy them.
        QTRY_VERIFY_WITH_TIMEOUT(verifyRows(), 3000);
        // Jump far enough to pool/reuse delegates and display six-digit ordinals.
        QVERIFY(QMetaObject::invokeMethod(view, "positionViewAtIndex", Q_ARG(int, 99999),
                                          Q_ARG(int, 2)));
        QTRY_VERIFY(view->property("contentY").toReal() > 99980 * 60.0);
        QTRY_VERIFY(session.item("songIndexText"));
        // Incubation assigns required roles before nested text bindings settle.
        // Re-snapshot the live tree each poll; never retain raw delegate pointers
        // across an event-loop wait while ListView may pool/destroy them.
        QTRY_VERIFY_WITH_TIMEOUT(verifyRows(), 3000);
        bool largeOrdinal = false;
        const auto findLarge = [&](auto &&self, QQuickItem *item) -> void {
            if (item->objectName() == "songIndexText" &&
                item->property("text").toString() == "100000")
                largeOrdinal = true;
            for (auto *child : item->childItems())
                self(self, child);
        };
        QTRY_VERIFY(([&] {
            largeOrdinal = false;
            findLarge(findLarge, view);
            return largeOrdinal;
        })());
        session.search.results()->setQuery(QStringLiteral("歌曲 999"));
        QTRY_COMPARE(view->property("count").toInt(), 111);
        QVERIFY(
            QMetaObject::invokeMethod(view, "positionViewAtIndex", Q_ARG(int, 0), Q_ARG(int, 0)));
        QTRY_COMPARE(geometry->property("indexWidth").toReal(), qreal(40));
        QTRY_VERIFY(countVisual(view, "songDelegate") > 0);
        // Incubation assigns required roles before nested text bindings settle.
        // Re-snapshot the live tree each poll; never retain raw delegate pointers
        // across an event-loop wait while ListView may pool/destroy them.
        QTRY_VERIFY_WITH_TIMEOUT(verifyRows(), 3000);
        for (int direction = 0; direction < 3; ++direction) {
            session.search.results()->toggleSort(SearchResultModel::Title);
            QTest::qWait(30); // Allow ListView's scheduled model layout.
            // Incubation assigns required roles before nested text bindings settle.
            // Re-snapshot the live tree each poll; never retain raw delegate pointers
            // across an event-loop wait while ListView may pool/destroy them.
            QTRY_VERIFY_WITH_TIMEOUT(verifyRows(), 3000);
        }
        session.search.results()->setQuery({});
        QTRY_COMPARE(view->property("count").toInt(), 100000);
        QTRY_VERIFY(geometry->property("indexWidth").toReal() > 40);
        // Incubation assigns required roles before nested text bindings settle.
        // Re-snapshot the live tree each poll; never retain raw delegate pointers
        // across an event-loop wait while ListView may pool/destroy them.
        QTRY_VERIFY_WITH_TIMEOUT(verifyRows(), 3000);
        const auto output = qEnvironmentVariable("ECHO_ORDINAL_REPORT_DIR");
        if (!output.isEmpty() && QGuiApplication::platformName() != QStringLiteral("offscreen")) {
            QVERIFY(QTest::qWaitForWindowExposed(session.window));
            QVERIFY(QMetaObject::invokeMethod(view, "positionViewAtIndex", Q_ARG(int, 99999),
                                              Q_ARG(int, 2)));
            QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
            session.window->update();
            QTRY_VERIFY(!frames.isEmpty());
            QTest::mouseMove(session.window, QPoint(2, 2));
            QVERIFY(QDir().mkpath(output));
            const auto capture = session.window->grabWindow();
            QVERIFY(!capture.isNull());
            const auto name =
                kind == int(SearchModel::Kind::Song) ? "/songs-100000.png" : "/lyrics-100000.png";
            QVERIFY(capture.save(output + name));
        }
        QCOMPARE(warnings.count(), 0);
    }
    void productionButtonContentCentersAndSortIconsFollowForeground()
    {
        Session session;
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QVERIFY(session.create());
        SearchModelTestAccess::populate(session.search, SearchModel::Kind::Song, 1000);
        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = session.item("searchPage")));
        QTRY_VERIFY(findVisual(page, "buttonContentRow"));
        QList<QQuickItem *> rows;
        const auto collect = [&rows](auto &&self, QQuickItem *root) -> void {
            if (root->objectName() == "buttonContentRow" && root->isVisible())
                rows.append(root);
            for (auto *child : root->childItems())
                self(self, child);
        };
        collect(collect, session.window->contentItem());
        QVERIFY(rows.size() >= 7); // Shared buttons in toolbar, sort header and sidebar.
        QQuickItem *songLabel = nullptr, *songGlyph = nullptr;
        for (auto *row : rows) {
            auto *content = row->parentItem();
            QTRY_VERIFY(qAbs(row->mapToItem(content, QPointF(0, row->height() / 2)).y() -
                             content->height() / 2) < 0.6);
            auto *label = findVisual(row, "buttonLabel");
            auto *glyph = findVisual(row, "buttonGlyph");
            QVERIFY(label);
            if (content->property("glyph").toString().isEmpty()) {
                QVERIFY(!glyph); // Text-only buttons do not retain a hidden icon.
                continue;
            }
            QTRY_VERIFY((glyph = findVisual(row, "buttonGlyph")));
            if (!label->isVisible() || !glyph->isVisible())
                continue;
            QTRY_VERIFY(qAbs(label->mapToItem(content, QPointF(0, label->height() / 2)).y() -
                             glyph->mapToItem(content, QPointF(0, glyph->height() / 2)).y()) < 0.6);
            if (label->property("text").toString() == QStringLiteral("歌曲")) {
                songLabel = label;
                songGlyph = glyph;
            }
        }
        QVERIFY(songLabel && songGlyph);
        session.search.results()->toggleSort(SearchResultModel::SortField::Title);
        for (auto mode : {ThemeStore::Mode::Light, ThemeStore::Mode::Dark}) {
            session.theme.setMode(mode);
            session.theme.setAccent(QColor("#bc4477"));
            QTRY_COMPARE(songGlyph->property("color").value<QColor>(), session.theme.accentText());
            QCOMPARE(songLabel->property("color").value<QColor>(), session.theme.accentText());
        }
        QCOMPARE(warnings.count(), 0);
    }
    void sharedPageScrollLifecycleClippingAndWheel()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        ThemeStore theme(settings);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine,
                                QUrl("qrc:/qt/qml/EchoMusic/SearchUiTests/PageScrollHarness.qml"));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        auto *page = findVisual(window->contentItem(), "harnessPage");
        auto *list = findVisual(window->contentItem(), "harnessList");
        auto *grid = findVisual(window->contentItem(), "harnessGrid");
        QVERIFY(page && list && grid);
        QTRY_COMPARE(list->property("count").toInt(), 1000);
        list->setProperty("contentY", 950.0 - list->property("topMargin").toReal());
        QTRY_COMPARE(page->property("scrollTop").toReal(), qreal(950));
        // Alternate virtual views share the page offset, not a per-category cache.
        root->setProperty("useGrid", true);
        QTRY_COMPARE(page->property("scrollTop").toReal(), qreal(950));
        QTRY_COMPARE(grid->property("contentY").toReal(), qreal(890));
        root->setProperty("useGrid", false);
        QTRY_COMPARE(list->property("contentY").toReal(), qreal(890));
        root->setProperty("useAuxiliary", true);
        QTRY_COMPARE(page->property("scrollTop").toReal(), qreal(0));
        auto *auxiliary = findVisual(page, "harnessAuxiliary");
        QVERIFY(auxiliary);
        auxiliary->setProperty("contentY", 300);
        root->setProperty("useAuxiliary", false);
        QTRY_COMPARE(page->property("scrollTop").toReal(), qreal(950));
        QVERIFY(QMetaObject::invokeMethod(root.data(), "resizeHeader", Q_ARG(int, 100)));
        QTRY_COMPARE(page->property("scrollTop").toReal(), qreal(950));
        QTRY_COMPARE(page->property("occlusionHeight").toReal(), qreal(100));
        auto *clip = findVisual(page, "pageContentClip");
        QVERIFY(clip && clip->clip());
        QCOMPARE(list->mapToItem(page, QPointF()), QPointF());
        QCOMPARE(list->height(), page->height()); // Clipping must not shrink the view.
        QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(200, 20));
        QCOMPARE(root->property("rowClicks").toInt(), 0);
        QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(50, 20));
        QCOMPARE(root->property("headerClicks").toInt(), 1);
        ulong timestamp = 1000;
        auto sendWheel = [window, &timestamp](QPoint pixel, QPoint angle,
                                              Qt::KeyboardModifiers modifiers = {},
                                              Qt::ScrollPhase phase = Qt::NoScrollPhase) {
            const QPointF position(200, 20);
            QWheelEvent event(position, window->mapToGlobal(position.toPoint()), pixel, angle,
                              Qt::NoButton, modifiers, phase, false,
                              Qt::MouseEventSynthesizedBySystem);
            event.setTimestamp(timestamp += 20);
            QCoreApplication::sendEvent(window, &event);
        };
        QSignalSpy gesture(list, SIGNAL(movementStarted()));
        sendWheel({}, {}, {}, Qt::ScrollBegin);
        for (int i = 0; i < 3; ++i)
            sendWheel(QPoint(0, -40), QPoint(0, -80), {}, Qt::ScrollUpdate);
        sendWheel({}, {}, {}, Qt::ScrollEnd);
        QTRY_VERIFY(page->property("scrollTop").toReal() > 950);
        QVERIFY(!gesture.isEmpty()); // Header gestures retain native Flickable state.
        const auto gesturePosition = page->property("scrollTop").toReal();
        sendWheel({}, QPoint(0, -120), Qt::ControlModifier);
        QCOMPARE(page->property("scrollTop").toReal(), gesturePosition);
        sendWheel(QPoint(-80, 0), QPoint(-120, 0));
        QCOMPARE(page->property("scrollTop").toReal(), gesturePosition);
        sendWheel({}, QPoint(0, -120));
        QTRY_VERIFY(page->property("scrollTop").toReal() > gesturePosition);
        const auto saved = page->property("scrollTop").toReal();
        page->setProperty("pageActive", false);
        QTRY_COMPARE(list->property("count").toInt(), 0);
        list->setProperty("contentY", 0); // Offscreen/reset notifications cannot replace the cache.
        window->resize(550, 450);
        page->setProperty("pageActive", true);
        QTRY_COMPARE(list->property("count").toInt(), 1000);
        // Recreating the virtual model runs Qt's fixup(), which snaps an
        // interrupted fractional animation position to a logical pixel.
        QTRY_COMPARE(page->property("scrollTop").toReal(), qreal(qRound(saved)));
        root->setProperty("rowCount", 1);
        QTRY_COMPARE(list->property("count").toInt(), 1);
        QTRY_COMPARE(page->property("scrollTop").toReal(), qreal(0));
        QVERIFY(QMetaObject::invokeMethod(page, "resetPosition"));
        QCOMPARE(auxiliary->property("contentY").toReal(), qreal(0));
        QCOMPARE(warnings.count(), 0);
        window->close();
    }
    void sharedPageSwapLaysOutHiddenTargetBeforeClamping()
    {
        QTemporaryDir directory;
        SettingsStore settings(directory.filePath("settings.ini"));
        ThemeStore theme(settings);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine,
                                QUrl("qrc:/qt/qml/EchoMusic/SearchUiTests/PageScrollHarness.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties({{"rowCount", 3}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        auto *page = findVisual(window->contentItem(), "harnessPage");
        auto *list = findVisual(window->contentItem(), "harnessList");
        QVERIFY(page && list);
        QTest::qWait(40);
        root->setProperty("rowCount", 1000);
        QVERIFY(QMetaObject::invokeMethod(list, "forceLayout"));
        QVERIFY(QMetaObject::invokeMethod(page, "scrollTo", Q_ARG(double, 120.)));
        // The hidden grid still has its previous short extent until layout.
        root->setProperty("useGrid", true);
        QTest::qWait(40);
        QTRY_COMPARE_WITH_TIMEOUT(page->property("scrollTop").toReal(), 120., 1000);
        root->setProperty("useGrid", false);
        QTest::qWait(40);
        QTRY_COMPARE_WITH_TIMEOUT(page->property("scrollTop").toReal(), 120., 1000);
        root->setProperty("useGrid", true);
        QVERIFY(QMetaObject::invokeMethod(page, "scrollTo", Q_ARG(double, 40.)));
        QTest::qWait(40);
        QTRY_COMPARE(page->property("scrollTop").toReal(), 40.);
        root->setProperty("useGrid", false);
        QVERIFY(QMetaObject::invokeMethod(root.data(), "locate", Q_ARG(int, 100)));
        QTest::qWait(40);
        QTRY_VERIFY(page->property("scrollTop").toReal() > 4000.);
    }
    void sharedPageContentWheelIsNotBlockedByStickyLayer()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        ThemeStore theme(settings);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine,
                                QUrl("qrc:/qt/qml/EchoMusic/SearchUiTests/PageScrollHarness.qml"));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *page = findVisual(window->contentItem(), "harnessPage");
        QVERIFY(page);
        for (int presentation = 0; presentation < 3; ++presentation) {
            root->setProperty("useGrid", presentation == 1);
            root->setProperty("useAuxiliary", presentation == 2);
            const auto name = presentation == 0   ? "harnessList"
                              : presentation == 1 ? "harnessGrid"
                                                  : "harnessAuxiliary";
            auto *view = findVisual(page, name);
            QVERIFY(view);
            // Visibility/model notifications precede scene-graph hit-test
            // updates. Dispatch only after the new presentation is polished.
            QTest::qWait(50);
            QVERIFY(QMetaObject::invokeMethod(page, "scrollTo", Q_ARG(double, 1000.0)));
            QTRY_VERIFY(view->property("contentY").toReal() > 800);
            QTest::qWait(50);
            const auto before = view->property("contentY").toReal();
            QObject *angleWheel = nullptr;
            for (auto *child : view->children())
                if (QByteArray(child->metaObject()->className())
                        .endsWith("EchoWheelScrollAttached"))
                    angleWheel = child;
            QVERIFY(angleWheel && angleWheel->property("enabled").toBool());
            const auto beforeOffset = page->property("scrollTop").toReal();
            QSignalSpy movement(view, SIGNAL(movementStarted()));
            QVERIFY(movement.isValid());
            // Exercise the actual QQuickWindow dispatch below the sticky header.
            // Assigning contentY or calling scrollBy would bypass the regression.
            const QPointF position(200, window->height() - 60);
            QWheelEvent event(position, window->mapToGlobal(position.toPoint()), {},
                              QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                              false);
            QCoreApplication::sendEvent(window, &event);
            QVERIFY(angleWheel->property("running").toBool());
            // A queued no-op layout restore must not cancel the native wheel
            // animation before its first frame changes contentY.
            QVERIFY(QMetaObject::invokeMethod(page, "scheduleRestore"));
            QTRY_VERIFY2_WITH_TIMEOUT(
                page->property("scrollTop").toReal() > beforeOffset,
                qPrintable(QStringLiteral("presentation=%1, before=%2, after=%3, viewport=%4x%5")
                               .arg(presentation)
                               .arg(before)
                               .arg(view->property("contentY").toReal())
                               .arg(view->width())
                               .arg(view->height())),
                1000);
            QTRY_VERIFY2(qAbs(page->property("scrollTop").toReal() - beforeOffset -
                              QGuiApplication::styleHints()->wheelScrollLines() * 24) < 1.1,
                         qPrintable(QStringLiteral(
                                        "presentation=%1 before=%2 after=%3 enabled=%4 running=%5")
                                        .arg(presentation)
                                        .arg(before)
                                        .arg(view->property("contentY").toReal())
                                        .arg(angleWheel->property("enabled").toBool())
                                        .arg(angleWheel->property("running").toBool())));
            view->setProperty("contentY", before);
            QVERIFY(QMetaObject::invokeMethod(view, "cancelFlick"));
        }
        QCOMPARE(warnings.count(), 0);
    }
    void productionBackToTopKeepsAllSearchKindsVirtual()
    {
        Session session;
        session.theme.setMotionEnabled(false);
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QVERIFY(session.create());
        QVERIFY(Test::prepareNativeInput(session.window));
        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = session.item("searchPage")));
        for (auto kind :
             {SearchModel::Kind::Song, SearchModel::Kind::Lyric, SearchModel::Kind::Playlist,
              SearchModel::Kind::Album, SearchModel::Kind::Artist, SearchModel::Kind::Mv}) {
            SearchModelTestAccess::populate(session.search, kind, 10000);
            const bool songs = kind == SearchModel::Kind::Song || kind == SearchModel::Kind::Lyric;
            auto *view = session.item(songs ? "searchResults" : "searchResultGrid");
            QVERIFY(view);
            QTRY_VERIFY(view->isVisible());
            QTRY_COMPARE(view->property("count").toInt(), 10000);
            QVERIFY(QMetaObject::invokeMethod(view, "forceLayout"));
            QVERIFY(session.engine.incubationController());
            QTRY_COMPARE(session.engine.incubationController()->incubatingObjectCount(), 0);
            QVERIFY(QMetaObject::invokeMethod(page, "scrollTo", Q_ARG(double, 2000.0)));
            QQuickItem *button = nullptr;
            QTRY_VERIFY((button = session.item("backToTopButton")));
            auto *owner = session.item("pageBackToTop");
            QVERIFY(owner);
            QPointer<QQuickItem> control = owner->property("control").value<QQuickItem *>();
            QVERIFY(control);
            const QPoint point =
                button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint();
            QTest::mouseClick(session.window, Qt::LeftButton, {}, point);
            QTRY_VERIFY(control.isNull());
            QTRY_VERIFY(qAbs(page->property("scrollTop").toReal()) < 0.01);
            QCOMPARE(session.search.rowCount(), 10000);
            QVERIFY(!view->property("moving").toBool());
            QVERIFY(countVisual(view, songs ? "songDelegate" : "contentDelegate") <
                    (songs ? 70 : 128));
        }
        // Include the last view's asynchronous delegates in the warning check
        // and finish their creation before destroying the window/engine.
        QVERIFY(session.engine.incubationController());
        QTRY_COMPARE(session.engine.incubationController()->incubatingObjectCount(), 0);
        QCOMPARE(warnings.count(), 0);
    }
    void productionPageWheelReachesContentAndGutters()
    {
        Session session;
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QVERIFY(session.create());
        QVERIFY(Test::prepareNativeInput(session.window));
        QVERIFY(QTest::qWaitForWindowExposed(session.window));
        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = session.item("searchPage")));
        ulong timestamp = 10000;
        auto sendWheel = [&](QPointF local, QPoint pixel, QPoint angle, Qt::ScrollPhase phase,
                             Qt::MouseEventSource source = Qt::MouseEventNotSynthesized) {
            const auto position = page->mapToScene(local);
            QWheelEvent event(position, session.window->mapToGlobal(position.toPoint()), pixel,
                              angle, Qt::NoButton, Qt::NoModifier, phase, false, source);
            // Native input carries a monotonic timestamp; default zero cannot
            // reliably drive consecutive Flickable velocity calculations.
            event.setTimestamp(timestamp += 20);
            QCoreApplication::sendEvent(session.window, &event);
        };
        for (auto kind :
             {SearchModel::Kind::Song, SearchModel::Kind::Lyric, SearchModel::Kind::Playlist,
              SearchModel::Kind::Album, SearchModel::Kind::Artist, SearchModel::Kind::Mv}) {
            SearchModelTestAccess::populate(session.search, kind, 10000);
            const bool songs = kind == SearchModel::Kind::Song || kind == SearchModel::Kind::Lyric;
            auto *view = session.item(songs ? "searchResults" : "searchResultGrid");
            QVERIFY(view);
            QTest::qWait(50); // Polish the changed presentation before hit testing.
            QSignalSpy movement(view, SIGNAL(movementStarted()));
            QVERIFY(movement.isValid());
            for (int region = 0; region < 5; ++region) {
                for (int direction : {1, -1}) {
                    QVERIFY(QMetaObject::invokeMethod(page, "scrollTo", Q_ARG(double, 1000.0)));
                    QTRY_COMPARE(page->property("pinned").toBool(),
                                 kind != SearchModel::Kind::Song);
                    QTest::qWait(50);
                    movement.clear();
                    const auto before = page->property("scrollTop").toReal();
                    const auto occlusion = page->property("occlusionHeight").toReal();
                    const QPointF positions[] = {{120, occlusion + 60},
                                                 {120, page->height() - 60},
                                                 {page->width() / 2, 20},
                                                 {16, page->height() - 60},
                                                 {page->width() - 16, page->height() - 60}};
                    sendWheel(positions[region], {}, QPoint(0, -120 * direction),
                              Qt::NoScrollPhase);
                    QTRY_VERIFY2_WITH_TIMEOUT(
                        (page->property("scrollTop").toReal() - before) * direction > 0,
                        qPrintable(QStringLiteral("kind=%1 region=%2 direction=%3")
                                       .arg(int(kind))
                                       .arg(region)
                                       .arg(direction)),
                        1000);
                    // Angle wheels preserve each tick's distance in every hit
                    // region. Pixel gestures below must still use native movement.
                    QTRY_VERIFY(qAbs(page->property("scrollTop").toReal() - before -
                                     direction * QGuiApplication::styleHints()->wheelScrollLines() *
                                         24) < 1.1);
                }
            }
            QVERIFY(QMetaObject::invokeMethod(page, "scrollTo", Q_ARG(double, 1000.0)));
            QTest::qWait(50);
            movement.clear();
            const QPointF position(120, page->height() - 60);
            const auto before = page->property("scrollTop").toReal();
            sendWheel(position, {}, {}, Qt::ScrollBegin, Qt::MouseEventSynthesizedBySystem);
            // Cocoa supplies angle deltas alongside pixel deltas. A zero angle
            // ScrollUpdate after an accepted wheel is Qt's compatibility event
            // and is intentionally suppressed by the delivery agent.
            for (int update = 0; update < 3; ++update)
                sendWheel(position, QPoint(0, -40), QPoint(0, -80), Qt::ScrollUpdate,
                          Qt::MouseEventSynthesizedBySystem);
            QTRY_VERIFY2_WITH_TIMEOUT(
                page->property("scrollTop").toReal() > before,
                qPrintable(QStringLiteral("pixel gesture kind=%1").arg(int(kind))), 1000);
            QVERIFY(movement.count() > 0);
            sendWheel(position, QPoint(0, -10), QPoint(0, -20), Qt::ScrollMomentum,
                      Qt::MouseEventSynthesizedBySystem);
            sendWheel(position, {}, {}, Qt::ScrollEnd, Qt::MouseEventSynthesizedBySystem);
            QVERIFY(countVisual(view, songs ? "songDelegate" : "contentDelegate") <
                    (songs ? 70 : 128));
        }
        QCOMPARE(warnings.count(), 0);
    }
    void productionThemeViewportWheelWithoutStickyHeader()
    {
        Session session;
        session.routes.navigate("themes");
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QVERIFY(session.create());
        QVERIFY(Test::prepareNativeInput(session.window));
        QVERIFY(QTest::qWaitForWindowExposed(session.window));
        // The real catalog/palette contain few items. Use the supported UI zoom
        // to make both scrollable, without injecting a theme model or changing
        // a user's saved theme preferences.
        for (int step = 0; step < 16; ++step)
            session.shell.zoomIn();
        QQuickItem *themePage = nullptr;
        QTRY_VERIFY((themePage = session.item("themeCenterPage")));
        // Wheel input belongs to the outer Flickable, not the virtual grid's
        // bounded projection (which also exposes a scrollView property).
        QQuickItem *catalog = nullptr;
        QTRY_VERIFY((catalog = session.item("themeCatalogScroll")));
        auto *viewport = catalog->parentItem();
        while (viewport && !viewport->property("scrollView").isValid())
            viewport = viewport->parentItem();
        QVERIFY(viewport);
        QCOMPARE(viewport->property("occlusionHeight").toReal(), qreal(0));
        auto *frame = session.item("shellFrame");
        QVERIFY(frame);
        QTRY_VERIFY(frame->property("narrow").toBool());
        // Finish the real 230->80 sidebar animation before wheel hit testing:
        // GridView cell resizing itself can cancel a wheel animation. Keep the
        // product animation enabled and wait for its actual final geometry.
        QTRY_VERIFY(std::abs(viewport->width() - (frame->width() - 80 - 4)) <
                    1.0); // Layout pixel rounding at 180%.
        ulong timestamp = 20000;
        for (int tab : {0, 1}) {
            themePage->setProperty("tab", tab);
            QTest::qWait(50);
            auto *view =
                qobject_cast<QQuickItem *>(viewport->property("scrollView").value<QObject *>());
            QVERIFY(view);
            QTRY_VERIFY2_WITH_TIMEOUT(
                view->property("contentHeight").toReal() > view->height() + 20,
                qPrintable(QStringLiteral("tab=%1 content=%2 height=%3 width=%4 scale=%5")
                               .arg(tab)
                               .arg(view->property("contentHeight").toReal())
                               .arg(view->height())
                               .arg(view->width())
                               .arg(session.shell.uiScale())),
                1000);
            for (int region = 0; region < 3; ++region) {
                QVERIFY(QMetaObject::invokeMethod(viewport, "scrollTo", Q_ARG(double, 0.0)));
                QTest::qWait(50);
                QTRY_VERIFY(view->property("atYBeginning").toBool());
                const auto before = viewport->property("scrollTop").toReal();
                const QPointF local(region == 0   ? viewport->width() / 2
                                    : region == 1 ? 16
                                                  : viewport->width() - 16,
                                    viewport->height() * 0.7);
                const auto position = viewport->mapToScene(local);
                QSignalSpy movement(view, SIGNAL(movementStarted()));
                QWheelEvent event(position, session.window->mapToGlobal(position.toPoint()), {},
                                  QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                                  false);
                event.setTimestamp(timestamp += 100);
                QCoreApplication::sendEvent(session.window, &event);
                QTRY_VERIFY2_WITH_TIMEOUT(
                    viewport->property("scrollTop").toReal() > before,
                    qPrintable(QStringLiteral("theme tab=%1 region=%2 viewport=%3x%4 view=%5x%6 "
                                              "before=%7 after=%8 point=%9,%10")
                                   .arg(tab)
                                   .arg(region)
                                   .arg(viewport->width())
                                   .arg(viewport->height())
                                   .arg(view->width())
                                   .arg(view->height())
                                   .arg(before)
                                   .arg(viewport->property("scrollTop").toReal())
                                   .arg(position.x())
                                   .arg(position.y())),
                    1000);
                QTRY_VERIFY(qAbs(viewport->property("scrollTop").toReal() - before -
                                 QGuiApplication::styleHints()->wheelScrollLines() * 24) < 1.1);
            }
        }
        QCOMPARE(warnings.count(), 0);
    }
    void productionSearchShortcutEscapeAndNavigationDismissal()
    {
        Session session;
        session.routes.navigate("search"); // Empty query: no suggestion HTTP timer.
        SearchModelTestAccess::populateDiscovery(session.search);
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QVERIFY(session.create());
        QQuickItem *input = nullptr;
        QTRY_VERIFY((input = session.item("searchInput")));
        QPointer<QObject> popup;
        QVERIFY(!session.root->findChild<QObject *>("searchSuggestionsPopup"));
        const auto openBusinessPopup = [&](const QString &name) -> QObject * {
            auto *owner = session.root->findChild<QObject *>(name + "Owner");
            if (!owner || !QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})))
                return nullptr;
            return owner->property("item").value<QObject *>();
        };
        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = session.item("searchPage")));
        // Recommendations belong exclusively to the titlebar popup, even when
        // the results route has no submitted keyword.
        QVERIFY(!findVisual(page, "searchDiscoveryPanel"));
        QVERIFY(Test::prepareNativeInput(session.window));
        const auto bindings = QKeySequence::keyBindings(QKeySequence::Find);
        QVERIFY(!bindings.isEmpty());
        const auto findKey = bindings.first()[0];
        QTest::keyClick(session.window, findKey.key(), findKey.keyboardModifiers());
        QTRY_VERIFY(input->hasActiveFocus());
        QTRY_VERIFY((popup = session.root->findChild<QObject *>("searchSuggestionsPopup")));
        QTRY_VERIFY(popup->property("opened").toBool());
        auto *content = popup->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        QVERIFY(findVisual(content, "searchDiscoveryPanel"));
        const auto insideWindow = [&] {
            const auto topLeft = content->mapToScene(QPointF(0, 0));
            const auto bottomRight =
                content->mapToScene(QPointF(content->width(), content->height()));
            return topLeft.x() >= 11 && topLeft.y() >= 0 &&
                   bottomRight.x() <= session.window->width() - 11 &&
                   bottomRight.y() <= session.window->height() - 11;
        };
        QTRY_VERIFY2(
            insideWindow(),
            qPrintable(
                QStringLiteral(
                    "content bounds (%1,%2)-(%3,%4), window %5x%6, popup (%7,%8) scale %9")
                    .arg(content->mapToScene(QPointF(0, 0)).x())
                    .arg(content->mapToScene(QPointF(0, 0)).y())
                    .arg(content->mapToScene(QPointF(content->width(), content->height())).x())
                    .arg(content->mapToScene(QPointF(content->width(), content->height())).y())
                    .arg(session.window->width())
                    .arg(session.window->height())
                    .arg(popup->property("x").toReal())
                    .arg(popup->property("y").toReal())
                    .arg(popup->property("scale").toReal())));
        for (int step = 0; step < 5; ++step)
            session.shell.zoomIn();
        QTRY_VERIFY(insideWindow());
        session.window->resize(1250, 800);
        QTRY_VERIFY(insideWindow());
        // Query suggestions use the smaller trigger-relative popup in the same overlay.
        input->setProperty("text", QStringLiteral("音乐"));
        QTRY_VERIFY(popup->property("hasQuery").toBool());
        QTRY_VERIFY(insideWindow());
        input->setProperty("text", QString{});
        session.shell.resetZoom();
        session.window->resize(1150, 750);
        QTRY_VERIFY(!popup->property("hasQuery").toBool());
        QTRY_VERIFY(insideWindow());
        const auto popupArtifacts = qEnvironmentVariable("ECHO_SEARCH_POPUP_REPORT_DIR");
        if (!popupArtifacts.isEmpty() && QGuiApplication::platformName() != "offscreen") {
            QVERIFY(QDir().mkpath(popupArtifacts));
            for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
                session.theme.setMode(mode);
                QTRY_VERIFY(visibleIconsReady(session.window->contentItem()));
                QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
                session.window->update();
                QTRY_VERIFY(!frames.isEmpty());
                const auto capture = session.window->grabWindow();
                QVERIFY(!capture.isNull());
                QVERIFY(capture.save(popupArtifacts + (mode == ThemeStore::Light
                                                           ? "/search-popup-light.png"
                                                           : "/search-popup-dark.png")));
            }
        }
        auto *hotKeyword = findVisual(content, "searchHotKeyword");
        QVERIFY(hotKeyword);
        hotKeyword->forceActiveFocus(Qt::MouseFocusReason);
        QTest::qWait(80);
        QVERIFY(popup->property("opened").toBool());
        input->forceActiveFocus(Qt::OtherFocusReason);
        QTest::keyClick(session.window, Qt::Key_Escape);
        QTRY_VERIFY(popup.isNull());
        // Dismissal releases the editor; clicking must explicitly open search
        // again without requiring a focus restoration from a different popup.
        QTRY_VERIFY(!input->hasActiveFocus());
        const auto inputPoint =
            input->mapToScene(QPointF(input->width() / 2, input->height() / 2)).toPoint();
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, inputPoint);
        QTRY_VERIFY((popup = session.root->findChild<QObject *>("searchSuggestionsPopup")));
        QTRY_VERIFY(popup->property("opened").toBool());
        QTest::keyClick(session.window, Qt::Key_Escape);
        QTRY_VERIFY(popup.isNull());
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, inputPoint);
        QTRY_VERIFY((popup = session.root->findChild<QObject *>("searchSuggestionsPopup")));
        QTRY_VERIFY(popup->property("opened").toBool());
        // Foreign surfaces must dismiss search before taking focus. Restoring
        // focus is not a search command, and must not recreate its popup.
        QPointer<QObject> foreign = openBusinessPopup("sidebarShortcutPopup");
        QVERIFY(foreign);
        QVERIFY(QMetaObject::invokeMethod(foreign, "open"));
        QTRY_VERIFY(foreign->property("opened").toBool());
        QTRY_VERIFY(popup.isNull());
        QVERIFY(!input->hasActiveFocus());
        QVERIFY(QMetaObject::invokeMethod(foreign, "close"));
        QTRY_VERIFY(foreign.isNull());
        input->forceActiveFocus(Qt::PopupFocusReason);
        QTest::qWait(80);
        QVERIFY(popup.isNull());
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, inputPoint);
        QTRY_VERIFY((popup = session.root->findChild<QObject *>("searchSuggestionsPopup")));
        QTRY_VERIFY(popup->property("opened").toBool());
        QPointer<QObject> parentDialog = openBusinessPopup("accountSwitcherDialog");
        QVERIFY(parentDialog);
        QVERIFY(QMetaObject::invokeMethod(parentDialog, "open"));
        QTRY_VERIFY(parentDialog->property("opened").toBool());
        QTRY_VERIFY(popup.isNull());
        QPointer<QObject> childDialog = openBusinessPopup("logoutConfirmation");
        QVERIFY(childDialog);
        QTRY_VERIFY(childDialog->property("opened").toBool());
        QTest::keyClick(session.window, Qt::Key_Escape);
        QTRY_VERIFY(childDialog.isNull());
        QVERIFY(parentDialog->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(parentDialog, "close"));
        QTRY_VERIFY(parentDialog.isNull());
        QVERIFY(popup.isNull());
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, inputPoint);
        QTRY_VERIFY(input->hasActiveFocus());
        QTRY_VERIFY((popup = session.root->findChild<QObject *>("searchSuggestionsPopup")));
        QTRY_VERIFY(popup->property("opened").toBool());
        // Full-page login does not inherit Popup; disabling the titlebar must
        // still release its editor, and closing login must not restore it.
        session.accountUi.controller.openLogin();
        QTRY_VERIFY(!input->isEnabled());
        QTRY_VERIFY(popup.isNull());
        QTRY_VERIFY(!input->hasActiveFocus());
        session.accountUi.controller.closeLogin();
        QTRY_VERIFY(input->isEnabled());
        QTRY_VERIFY(!input->hasActiveFocus());
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, inputPoint);
        QTRY_VERIFY((popup = session.root->findChild<QObject *>("searchSuggestionsPopup")));
        QTRY_VERIFY(popup->property("opened").toBool());
        for (const auto key : {Qt::Key_E, Qt::Key_C, Qt::Key_H, Qt::Key_O})
            QTest::keyClick(session.window, key);
        QTRY_COMPARE(input->property("text").toString(), QStringLiteral("echo"));
        // Clear the query before outside dismissal to avoid a live HTTP timer.
        QTest::keyClick(session.window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(session.window, Qt::Key_Backspace);
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, QPoint(1140, 740));
        QTRY_VERIFY(popup.isNull());
        QTRY_VERIFY(!input->hasActiveFocus());
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, inputPoint);
        QTRY_VERIFY((popup = session.root->findChild<QObject *>("searchSuggestionsPopup")));
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(session.routes.navigate("themes"));
        QTRY_VERIFY(popup.isNull());
        session.shell.zoomIn();
        QVERIFY(session.shell.uiScale() > 1.0);
        QTest::keyClick(session.window, Qt::Key_0, Qt::ControlModifier);
        QTRY_COMPARE(session.shell.uiScale(), 1.0);
        QCOMPARE(warnings.count(), 0);
    }
    void productionSearchOpensOnKeyboardEntry()
    {
        // Keep keyboard entry independent of synthetic modal focus restoration;
        // the preceding test covers the complete nested-popup workflow.
        Session session;
        session.routes.navigate("search");
        SearchModelTestAccess::populateDiscovery(session.search);
        QVERIFY(session.create());
        auto *input = session.item("searchInput");
        QPointer<QObject> popup;
        QVERIFY(input);
        QVERIFY(!session.root->findChild<QObject *>("searchSuggestionsPopup"));
        QVERIFY(Test::prepareNativeInput(session.window));
        input->setFocus(false, Qt::OtherFocusReason);
        session.window->contentItem()->forceActiveFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(!input->hasActiveFocus());
        for (int step = 0; step < 64 && !input->hasActiveFocus(); ++step) {
            QTest::keyClick(session.window, Qt::Key_Tab);
            QTest::qWait(10);
        }
        QTRY_VERIFY(input->hasActiveFocus());
        QTRY_VERIFY((popup = session.root->findChild<QObject *>("searchSuggestionsPopup")));
        QTRY_VERIFY(popup->property("opened").toBool());
    }
    void productionSongToolbarRulesAcrossKindsThemesAndZoom()
    {
        Session session;
        SearchModelTestAccess::populate(session.search, SearchModel::Kind::Song, 30);
        QVERIFY(session.create());
        session.window->show();
        const auto artifacts = qEnvironmentVariable("ECHO_SEARCH_TAB_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        for (const auto kind : {SearchModel::Kind::Song, SearchModel::Kind::Lyric}) {
            SearchModelTestAccess::populate(session.search, kind, 30);
            for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
                session.theme.setMode(mode);
                for (const auto zoom : {1., 1.25}) {
                    session.shell.resetZoom();
                    if (zoom > 1.)
                        for (int step = 0; step < 5; ++step)
                            session.shell.zoomIn();
                    for (const auto width : {1100, 1600}) {
                        session.window->resize(width, 800);
                        QQuickItem *rule = nullptr;
                        QTRY_VERIFY((rule = session.item("searchSongTabIndicator")));
                        auto *tab = session.item("searchSongTabSlot");
                        auto *label = session.item("searchSongCountRow");
                        auto *divider = session.item("songListHeaderDivider");
                        auto *tabRow = session.item("searchSongTabRow");
                        auto *tabDivider = session.item("searchSongTabDivider");
                        QTRY_VERIFY(rule && tab && label && divider && tabRow && tabDivider);
                        QTRY_VERIFY(rule->width() > 30);
                        QTRY_COMPARE(tab->height(), 36.);
                        QCOMPARE(label->y() + label->height(), 30.);
                        QTest::qWait(100);
                        if (QGuiApplication::platformName() != "offscreen") {
                            QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
                            session.window->update();
                            QTRY_VERIFY(!frames.isEmpty());
                        }
                        QQuickItem *typeTabs = nullptr;
                        if (QGuiApplication::platformName() != "offscreen") {
                            // A production page is asynchronously incubated;
                            // wait for all six actual tab labels before capturing.
                            QTRY_VERIFY(([&] {
                                QList<QQuickItem *> pending{session.window->contentItem()};
                                while (!pending.isEmpty()) {
                                    auto *candidate = pending.takeFirst();
                                    if (!candidate->isVisible())
                                        continue;
                                    if (candidate->objectName() == "searchTypeTabs" ||
                                        candidate->objectName() == "pinnedSearchTypeTabs") {
                                        typeTabs = candidate;
                                        break;
                                    }
                                    pending.append(candidate->childItems());
                                }
                                if (!typeTabs)
                                    return false;
                                for (int n = 0; n < 6; ++n)
                                    if (!findVisual(typeTabs, QString("tabLabel-%1").arg(n)))
                                        return false;
                                return true;
                            })());
                        }
                        const auto capture = session.window->grabWindow();
                        QVERIFY(!capture.isNull());
                        const auto ratio = qreal(capture.width()) / session.window->width();
                        QCOMPARE(tabDivider->height(), 1.);
                        QCOMPARE(tabDivider->width(), tabRow->width());
                        QCOMPARE(tabDivider->mapToScene({0, 1}).y(),
                                 tabRow->mapToScene({0, tabRow->height()}).y());
                        QVERIFY(tabDivider->mapToScene({0, 1}).y() <
                                divider->mapToScene({0, 0}).y());
                        auto *countBadge = session.item("searchSongCountBadge");
                        auto *textBox = label->property("textBox").value<QQuickItem *>();
                        QVERIFY(countBadge && textBox);
                        QTRY_VERIFY(
                            qAbs(textBox->mapToScene({0, textBox->height() / 2}).y() -
                                 countBadge->mapToScene({0, countBadge->height() / 2}).y()) < zoom);
                        if (QGuiApplication::platformName() != "offscreen") {
                            const auto titleInk = Test::renderedInkCenter(
                                capture, session.window, textBox, session.theme.text());
                            const auto countInk =
                                Test::renderedInkCenter(capture, session.window, countBadge,
                                                        session.theme.text(), {3, 3, 3, 3});
                            QVERIFY(titleInk && countInk);
                            QVERIFY(qAbs(*titleInk - *countInk) <= zoom);
                        }
                        const auto pixelAt = [&](const QPointF &point) {
                            return capture.pixelColor(
                                qBound(0, qRound(point.x() * ratio), capture.width() - 1),
                                qBound(0, qRound(point.y() * ratio), capture.height() - 1));
                        };
                        // Pixel acceptance uses the native renderer; the offscreen
                        // software backend still exercises all geometry above.
                        for (auto *line : QGuiApplication::platformName() == "offscreen"
                                              ? QList<QQuickItem *>{}
                                              : QList<QQuickItem *>{rule, tabDivider, divider}) {
                            const auto center =
                                line->mapToScene(QPointF(line->width() / 2, line->height() / 2));
                            const auto ink = pixelAt(center);
                            const auto below = pixelAt(center + QPointF(0, 4 * zoom));
                            const auto contrast = qAbs(ink.red() - below.red()) +
                                                  qAbs(ink.green() - below.green()) +
                                                  qAbs(ink.blue() - below.blue());
                            QVERIFY2(contrast > 6,
                                     qPrintable(line->objectName() + " has no rendered rule"));
                        }
                        if (QGuiApplication::platformName() != "offscreen")
                            for (const auto x : {4., tabDivider->width() - 4}) {
                                const auto point = tabDivider->mapToScene({x, .5});
                                const auto ink = pixelAt(point);
                                const auto below = pixelAt(point + QPointF(0, 4 * zoom));
                                QVERIFY(qAbs(ink.red() - below.red()) +
                                            qAbs(ink.green() - below.green()) +
                                            qAbs(ink.blue() - below.blue()) >
                                        6);
                            }
                        if (QGuiApplication::platformName() != "offscreen") {
                            // Loading and pinned headers also exist in this page;
                            // inspect the one actually presented in this frame.
                            QVERIFY(typeTabs);
                            for (int n = 0; n < 6; ++n) {
                                auto *labelItem =
                                    findVisual(typeTabs, QString("tabLabel-%1").arg(n));
                                QVERIFY(labelItem);
                                auto *textItem =
                                    labelItem->property("textItem").value<QQuickItem *>();
                                QVERIFY(textItem && textItem->isVisible());
                                QCOMPARE(textItem->property("text").toString(),
                                         labelItem->property("text").toString());
                                const auto inkColor = textItem->property("color").value<QColor>();
                                const auto area =
                                    textItem->mapRectToScene(QRectF(QPointF(), textItem->size()));
                                int inkPixels = 0;
                                for (int y = qCeil(area.top() * ratio); y < area.bottom() * ratio;
                                     ++y)
                                    for (int x = qCeil(area.left() * ratio);
                                         x < area.right() * ratio; ++x) {
                                        const auto pixel = capture.pixelColor(x, y);
                                        if (qAbs(pixel.red() - inkColor.red()) +
                                                qAbs(pixel.green() - inkColor.green()) +
                                                qAbs(pixel.blue() - inkColor.blue()) <
                                            90)
                                            ++inkPixels;
                                    }
                                QVERIFY2(inkPixels > 3,
                                         qPrintable(labelItem->property("text").toString() +
                                                    " has no rendered glyphs"));
                            }
                        }
                        if (!artifacts.isEmpty())
                            QVERIFY(capture.save(
                                artifacts +
                                QString("/%1-%2-%3-%4.png")
                                    .arg(kind == SearchModel::Kind::Song ? "song" : "lyric")
                                    .arg(mode == ThemeStore::Light ? "light" : "dark")
                                    .arg(zoom)
                                    .arg(width)));
                    }
                }
            }
        }
    }
    void productionSearchFilteringStickyAndBoundedDelegates()
    {
        Session session;
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QVERIFY(session.create());
        SearchModelTestAccess::populate(session.search, SearchModel::Kind::Song, 10000);
        QQuickItem *page = nullptr, *list = nullptr, *toolbar = nullptr;
        QTRY_VERIFY((page = session.item("searchPage")));
        // The lower toolbar has not pinned yet. It must not clip the normal
        // search heading which precedes it in the same scrolling page.
        QTRY_COMPARE(page->property("pinnedOcclusionHeight").toReal(), 0.);
        auto *flowClip = findVisual(page, "pageFlowHeaderClip");
        QVERIFY(flowClip);
        QCOMPARE(flowClip->y(), 0.);
        QTRY_VERIFY((list = session.item("searchResults")));
        auto *songTitle = findVisual(list, "songTitleText");
        auto *songArtist = findVisual(list, "songArtistText");
        QTRY_VERIFY((songTitle = findVisual(list, "songTitleText")));
        QTRY_VERIFY((songArtist = findVisual(list, "songArtistText")));
        QCOMPARE(songTitle->property("font").value<QFont>().weight(), QFont::DemiBold);
        QCOMPARE(songArtist->property("font").value<QFont>().weight(), QFont::Medium);
        QCOMPARE(session.window->property("font").value<QFont>().family(),
                 QGuiApplication::font().family());
        QCOMPARE(session.window->property("font").value<QFont>().weight(), QFont::Normal);
#ifdef Q_OS_MACOS
        if (QFontDatabase::hasFamily(QStringLiteral("PingFang SC"))) {
            const auto titleFont = songTitle->property("font").value<QFont>();
            // Weight metadata alone missed CoreText's Regular CJK fallback.
            // Exercise the production Text's resolved glyphs, not just weight=600.
            for (const auto &text :
                 {QStringLiteral("Y"), QStringLiteral("茶"), QStringLiteral("《")}) {
                auto regular = titleFont;
                regular.setWeight(QFont::Normal);
                QTextLayout regularLayout(text, regular), titleLayout(text, titleFont);
                for (auto *layout : {&regularLayout, &titleLayout}) {
                    layout->beginLayout();
                    layout->createLine();
                    layout->endLayout();
                }
                const auto regularRuns = regularLayout.glyphRuns();
                const auto titleRuns = titleLayout.glyphRuns();
                QCOMPARE(regularRuns.size(), 1);
                QCOMPARE(titleRuns.size(), 1);
                const auto &regularRun = regularRuns.first();
                const auto &titleRun = titleRuns.first();
                QCOMPARE(regularRun.glyphIndexes().size(), 1);
                QCOMPARE(titleRun.glyphIndexes().size(), 1);
                QVERIFY(titleRun.glyphIndexes().first() != 0);
                const auto titlePath =
                    titleRun.rawFont().pathForGlyph(titleRun.glyphIndexes().first());
                QVERIFY(!titlePath.isEmpty());
                QVERIFY2(titlePath !=
                             regularRun.rawFont().pathForGlyph(regularRun.glyphIndexes().first()),
                         qPrintable(text + " still resolves to the Regular glyph outline"));
            }
        }
#endif
        auto *tabSlot = session.item("searchSongTabSlot");
        auto *countLabel = session.item("searchSongCountRow");
        auto *tabIndicator = session.item("searchSongTabIndicator");
        auto *tableHeader = session.item("songListHeader");
        auto *tableDivider = session.item("songListHeaderDivider");
        QVERIFY(tabSlot && countLabel && tabIndicator && tableHeader && tableDivider);
        QTRY_COMPARE(tabSlot->height(), 36.);
        QCOMPARE(tabIndicator->width(), tabSlot->width());
        QCOMPARE(tabIndicator->y() + tabIndicator->height(), tabSlot->height());
        QCOMPARE(countLabel->y() + countLabel->height(), tabSlot->height() - 6);
        QCOMPARE(tableDivider->height(), 1.);
        QCOMPARE(tableDivider->mapToScene(QPointF(0, 1)).y(),
                 tableHeader->mapToScene(QPointF(0, tableHeader->height())).y());
        const auto typographyArtifacts = qEnvironmentVariable("ECHO_SEARCH_TYPOGRAPHY_REPORT_DIR");
        if (!typographyArtifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(typographyArtifacts));
            for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
                session.theme.setMode(mode);
                QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
                session.window->update();
                QTRY_VERIFY(!frames.isEmpty());
                const auto capture = session.window->grabWindow();
                QVERIFY(!capture.isNull());
                QVERIFY(capture.save(typographyArtifacts + (mode == ThemeStore::Light
                                                                ? "/song-titles-light.png"
                                                                : "/song-titles-dark.png")));
            }
        }
        QTRY_VERIFY((toolbar = session.item("searchSongToolbar")));
        QTRY_COMPARE(list->property("count").toInt(), 10000);
        QTRY_VERIFY(countVisual(list, "songDelegate") > 0);
        QVERIFY(countVisual(list, "songDelegate") < 70);
        const auto margin = list->property("topMargin").toReal();
        QTRY_VERIFY(qAbs(list->property("contentY").toReal() + margin) < 2);
        list->setProperty("contentY", 1000.0);
        QTRY_COMPARE(toolbar->y(), qreal(0));
        QVERIFY(!page->property("pinned").toBool());
        QTRY_VERIFY(countVisual(list, "songDelegate") < 70);
        session.search.results()->setQuery(QStringLiteral("歌曲 9999："));
        QTRY_COMPARE(list->property("count").toInt(), 1);
        QCOMPARE(session.search.count(), 10000);
        QTRY_VERIFY(list->property("contentY").toReal() <=
                    qMax(-margin, list->property("contentHeight").toReal() - list->height()) + 2);
        session.search.results()->setQuery({});
        QTRY_COMPARE(list->property("count").toInt(), 10000);
        SearchModelTestAccess::populate(session.search, SearchModel::Kind::Album, 10000);
        QQuickItem *grid = nullptr;
        QTRY_VERIFY((grid = session.item("searchResultGrid")));
        QTRY_COMPARE(grid->property("count").toInt(), 10000);
        QTRY_VERIFY(grid->property("columns").toInt() >= 3);
        QTRY_VERIFY(countVisual(grid, "contentDelegate") > 0);
        QVERIFY(countVisual(grid, "contentDelegate") < 128);
        grid->setProperty("contentY", -grid->property("topMargin").toReal());
        QTRY_VERIFY(!page->property("pinned").toBool());
        // Cross the real 80px threshold repeatedly, including Qt's synchronous
        // contentY adjustment when the inline heading disappears.
        for (int i = 0; i < 5; ++i) {
            grid->setProperty("contentY", 81.0 - grid->property("topMargin").toReal());
            QTRY_VERIFY(page->property("pinned").toBool());
            grid->setProperty("contentY", 79.0 - grid->property("topMargin").toReal());
            QTRY_VERIFY(!page->property("pinned").toBool());
        }
        grid->setProperty("contentY", 1000.0);
        QTRY_VERIFY(page->property("pinned").toBool());
        QTRY_VERIFY(countVisual(grid, "contentDelegate") < 128);
        QVERIFY(session.engine.incubationController());
        QTRY_COMPARE(session.engine.incubationController()->incubatingObjectCount(), 0);
        QCOMPARE(warnings.count(), 0);
    }
    void productionCategoryChangesKeepThePageOffset()
    {
        Session session;
        QVERIFY(session.create());
        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = session.item("searchPage")));
        for (int count : {0, 3, 100}) {
            for (const auto kind :
                 {SearchModel::Kind::Song, SearchModel::Kind::Playlist, SearchModel::Kind::Album,
                  SearchModel::Kind::Artist, SearchModel::Kind::Lyric, SearchModel::Kind::Mv})
                SearchModelTestAccess::populate(session.search, kind, count);
            QVERIFY(QMetaObject::invokeMethod(page, "scrollTo", Q_ARG(double, 0.)));
            for (const auto kind :
                 {SearchModel::Kind::Song, SearchModel::Kind::Playlist, SearchModel::Kind::Album,
                  SearchModel::Kind::Artist, SearchModel::Kind::Lyric, SearchModel::Kind::Mv,
                  SearchModel::Kind::Song}) {
                session.search.setKind(kind);
                QTest::qWait(80);
                QTRY_COMPARE_WITH_TIMEOUT(page->property("scrollTop").toReal(), 0., 1000);
            }
        }
        QVERIFY(QMetaObject::invokeMethod(page, "scrollTo", Q_ARG(double, 120.)));
        QTRY_COMPARE(page->property("scrollTop").toReal(), 120.);
        for (const auto kind :
             {SearchModel::Kind::Album, SearchModel::Kind::Lyric, SearchModel::Kind::Artist,
              SearchModel::Kind::Playlist, SearchModel::Kind::Mv, SearchModel::Kind::Song}) {
            session.search.setKind(kind);
            QTest::qWait(80);
            QTRY_VERIFY2_WITH_TIMEOUT(qAbs(page->property("scrollTop").toReal() - 120.) < .01,
                                      qPrintable(QString::number(int(kind))), 1000);
        }
        session.search.setKind(SearchModel::Kind::Album);
        auto *grid = session.item("searchResultGrid");
        QVERIFY(grid);
        QVERIFY(QMetaObject::invokeMethod(grid, "forceLayout"));
        QVERIFY(QMetaObject::invokeMethod(page, "scrollTo", Q_ARG(double, 40.)));
        QTest::qWait(80);
        QTRY_COMPARE(page->property("scrollTop").toReal(), 40.);
        SearchModelTestAccess::populate(session.search, SearchModel::Kind::Song, 100, {},
                                        QStringLiteral("新的关键词"));
        QTest::qWait(80);
        QTRY_COMPARE_WITH_TIMEOUT(page->property("scrollTop").toReal(), 0., 1000);
    }
    void productionScrollbarsUsePageEdgeAndRemainInteractiveAboveStickyLayers()
    {
        Session session;
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QVERIFY(session.create());
        QVERIFY(Test::prepareNativeInput(session.window));
        QTest::mouseMove(session.window, QPoint(5, 5));
        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = session.item("searchPage")));
        for (auto kind :
             {SearchModel::Kind::Song, SearchModel::Kind::Lyric, SearchModel::Kind::Playlist,
              SearchModel::Kind::Album, SearchModel::Kind::Artist, SearchModel::Kind::Mv}) {
            SearchModelTestAccess::populate(session.search, kind, 10000);
            const bool songs = kind == SearchModel::Kind::Song || kind == SearchModel::Kind::Lyric;
            auto *view = session.item(songs ? "searchResults" : "searchResultGrid");
            auto *bar = session.item(songs ? "songScrollbar" : "gridScrollbar");
            QVERIFY(view && bar);
            QTRY_VERIFY(bar->isVisible());
            // The asynchronous page Loader can expose the attached bar before
            // its pageContainer binding moves it to the overlay layer.
            QTRY_COMPARE(bar->parentItem(), findVisual(page, "pageScrollbarLayer"));
            QCOMPARE(view->x(), qreal(40));
            QCOMPARE(view->width(), page->width() - (songs ? 80 : 60));
            QCOMPARE(bar->x() + bar->width(), page->width());
            QCOMPARE(bar->y(), qreal(0));
            QCOMPARE(bar->height(), page->height());
            // Pinning occludes list content, never the page-height track.
            view->setProperty("contentY", 1000.0 - view->property("topMargin").toReal());
            QTRY_COMPARE(page->property("pinned").toBool(), kind != SearchModel::Kind::Song);
            QCOMPARE(bar->height(), page->height());
            auto *thumb = findVisual(bar, "scrollThumb");
            QVERIFY(thumb);
            QTRY_VERIFY(thumb->height() >= 29.9);
            QCOMPARE(thumb->mapToItem(page, QPointF(thumb->width(), 0)).x(), page->width() - 2);
        }
        SearchModelTestAccess::showLoading(session.search);
        auto *staticBar = session.item("discoveryScrollbar");
        QVERIFY(staticBar);
        QTRY_VERIFY(staticBar->isVisible());
        QCOMPARE(staticBar->parentItem(), findVisual(page, "pageScrollbarLayer"));
        QCOMPARE(staticBar->x() + staticBar->width(), page->width());
        QCOMPARE(staticBar->height(), page->height());
        SearchModelTestAccess::populate(session.search, SearchModel::Kind::Song, 10000);
        session.search.results()->setQuery(QStringLiteral("歌曲 9999："));
        QTRY_COMPARE(session.search.results()->rowCount(), 1);
        QTRY_VERIFY(!session.item("songScrollbar")->isVisible());
        session.search.results()->setQuery({});
        QTRY_VERIFY(session.item("songScrollbar")->isVisible());
        // The lyric thumb is in the sticky header's top region. Exercise the
        // real mouse path, so geometry-only checks cannot hide an occluding layer.
        SearchModelTestAccess::populate(session.search, SearchModel::Kind::Lyric, 10000);
        auto *view = session.item("searchResults");
        auto *bar = session.item("songScrollbar");
        auto *thumb = findVisual(bar, "scrollThumb");
        QVERIFY(view && bar && thumb);
        view->setProperty("contentY", 1000.0 - view->property("topMargin").toReal());
        QTRY_VERIFY(page->property("pinned").toBool());
        if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
            // Cocoa rendering/polish follows the model/margin notifications.
            // Hit testing must use the thumb's rendered geometry, not the
            // previous frame's extent after changing from a one-row filter.
            QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
            session.window->update();
            QTRY_VERIFY(!frames.isEmpty());
        }
        QTest::mouseMove(session.window,
                         page->mapToScene(QPointF(16, page->height() / 2)).toPoint());
        QTRY_VERIFY(bar->property("revealed").toBool());
        QTRY_COMPARE(thumb->width(), qreal(4));
        QTest::mouseMove(session.window,
                         page->mapToScene(QPointF(100, page->height() / 2)).toPoint());
        QTest::qWait(700); // A hovered row must keep the track shown after the 600ms delay.
        QVERIFY(bar->property("revealed").toBool());
        QTRY_VERIFY(thumb->mapToItem(page, QPointF()).y() + thumb->height() / 2 < 56);
        QTest::mouseMove(session.window,
                         bar->mapToScene(QPointF(bar->width() - 4, bar->height() / 2)).toPoint());
        QTRY_VERIFY(bar->property("hovered").toBool());
        QCOMPARE(thumb->width(), qreal(4)); // Track hover alone must not expand the thumb.
        const auto handlePoint =
            thumb->mapToScene(QPointF(thumb->width() / 2, thumb->height() / 2)).toPoint();
        QTest::mouseMove(session.window, handlePoint);
        QTRY_VERIFY(bar->property("hovered").toBool());
        QTRY_COMPARE(thumb->width(), qreal(8));
        const auto artifactDirectory = qEnvironmentVariable("ECHO_SCROLLBAR_REPORT_DIR");
        if (!artifactDirectory.isEmpty() &&
            QGuiApplication::platformName() != QStringLiteral("offscreen")) {
            QVERIFY(QDir().mkpath(artifactDirectory));
            const auto image = session.window->grabWindow();
            QVERIFY(!image.isNull());
            QVERIFY(image.save(artifactDirectory + QStringLiteral("/lyric-sticky-scrollbar.png")));
        }
        const auto before = view->property("contentY").toReal();
        QTest::mousePress(session.window, Qt::LeftButton, {}, handlePoint);
        QTRY_VERIFY(bar->property("pressed").toBool());
        QTest::mouseMove(session.window, handlePoint + QPoint(0, 100));
        QTest::mouseRelease(session.window, Qt::LeftButton, {}, handlePoint + QPoint(0, 100));
        QTRY_VERIFY(view->property("contentY").toReal() > before);
        QTest::mouseMove(session.window, QPoint(5, 5));
        QVERIFY(bar->property("revealed").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!bar->property("revealed").toBool(), 1500);
        const auto contentWidth = view->width();
        session.window->resize(1250, 800);
        QTRY_VERIFY(view->width() > contentWidth);
        QCOMPARE(bar->x() + bar->width(), page->width());
        QCOMPARE(bar->height(), page->height());
        const auto beforeZoom = page->width();
        session.shell.zoomIn();
        QTRY_VERIFY(page->width() < beforeZoom);
        QCOMPARE(bar->x() + bar->width(), page->width());
        QCOMPARE(bar->height(), page->height());
        QVERIFY(session.routes.navigate("themes"));
        QTRY_VERIFY(!bar->property("resourceActive").toBool());
        QVERIFY(!bar->property("revealed").toBool());
        QCOMPARE(warnings.count(), 0);
    }
    void nativeReleaseScrollMeasurement()
    {
        if (qEnvironmentVariable("ECHO_RUN_UI_PERF") != "1")
            QSKIP("Opt-in real-window Release rendering measurement");
#ifndef QT_NO_DEBUG
        QSKIP("Rendering measurements require a Release application target");
#endif
        if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
            QSKIP("Offscreen is not a native rendering acceptance environment");
        QTest::failOnWarning(); // Scene-graph/texture warnings also fail this run.
        Session session;
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QVERIFY(session.create());
        QVERIFY(QTest::qWaitForWindowExposed(session.window));
        const auto coverDirectory = session.directory.filePath(QStringLiteral("covers"));
        QVERIFY(QDir().mkpath(coverDirectory));
        for (int i = 0; i < 128; ++i) {
            QImage cover(640, 640, QImage::Format_RGB32);
            QPainter painter(&cover);
            QLinearGradient gradient(0, 0, 640, 640);
            gradient.setColorAt(0, QColor::fromHsv((i * 19) % 360, 120, 220));
            gradient.setColorAt(1, QColor::fromHsv((i * 31) % 360, 220, 120));
            painter.fillRect(cover.rect(), gradient);
            painter.end();
            QVERIFY(cover.save(coverDirectory + QStringLiteral("/%1.png").arg(i)));
        }
        QJsonObject report{
            {"qt", QT_VERSION_STR},
            {"platform", QGuiApplication::platformName()},
            {"graphics_api", int(session.window->rendererInterface()->graphicsApi())},
            {"window_width", session.window->width()},
            {"window_height", session.window->height()},
            {"dpr", session.window->devicePixelRatio()},
            {"cover_workload", "128 distinct local 640x640 PNGs, async sized decode; no HTTP"},
            {"screen_refresh_hz", session.window->screen()->refreshRate()}};
        for (auto kind : {SearchModel::Kind::Song, SearchModel::Kind::Album}) {
            SearchModelTestAccess::populate(session.search, kind, 10000, coverDirectory);
            QQuickItem *view = nullptr;
            const auto viewName =
                kind == SearchModel::Kind::Song ? "searchResults" : "searchResultGrid";
            QTRY_VERIFY((view = session.item(viewName)));
            QTRY_COMPARE(view->property("count").toInt(), 10000);
            QTest::qWait(300);
            const auto *cover = findVisual(view, "coverImage");
            QVERIFY(cover);
            QTRY_COMPARE(cover->property("status").toInt(), 1); // Image.Ready.
            QVERIFY(!cover->property("source").toUrl().isEmpty());
            const qint64 before = residentBytes();
            QElapsedTimer clock;
            clock.start();
            QList<double> frames;
            qint64 previous = 0;
            bool recording = true;
            // Render-thread signals can already be queued when disconnect()
            // returns. A receiver scoped to this iteration removes its queued
            // deliveries before the captured measurement variables die.
            QObject frameContext;
            const auto connection =
                connect(session.window, &QQuickWindow::frameSwapped, &frameContext, [&] {
                    if (!recording)
                        return;
                    const auto now = clock.nsecsElapsed();
                    if (previous)
                        frames.append(double(now - previous) / 1e6);
                    previous = now;
                });
            int maxDelegates = 0, maxObservedInstances = 0;
            QList<QPointer<QQuickItem>> observedDelegates;
            const QString delegateName = kind == SearchModel::Kind::Song
                                             ? QStringLiteral("songDelegate")
                                             : QStringLiteral("contentDelegate");
            std::function<void(QQuickItem *)> observeDelegates = [&](QQuickItem *item) {
                if (item->objectName() == delegateName && !observedDelegates.contains(item))
                    observedDelegates.append(item);
                for (auto *child : item->childItems())
                    observeDelegates(child);
            };
            observeDelegates(view);
            QPropertyAnimation scroll(view, "contentY");
            scroll.setDuration(4000);
            scroll.setStartValue(0.0);
            scroll.setEndValue(7680.0);
            scroll.setEasingCurve(QEasingCurve::Linear);
            connect(&scroll, &QVariantAnimation::valueChanged, this, [&] {
                maxDelegates = qMax(maxDelegates, countVisual(view, delegateName));
                observeDelegates(view);
                // Keep weak references after a delegate leaves the visual tree:
                // observed pooled instances remain counted until destruction.
                observedDelegates.removeIf([](const auto &item) { return item.isNull(); });
                maxObservedInstances = qMax(maxObservedInstances, int(observedDelegates.size()));
            });
            QEventLoop loop;
            connect(&scroll, &QAbstractAnimation::finished, &loop, &QEventLoop::quit);
            QTimer watchdog;
            watchdog.setSingleShot(true);
            connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
            watchdog.start(7000);
            scroll.start();
            loop.exec();
            QCOMPARE(scroll.state(), QAbstractAnimation::Stopped);
            recording = false;
            disconnect(connection);
            QVERIFY(frames.size() > 30);
            std::sort(frames.begin(), frames.end());
            QJsonObject metrics{
                {"rows", 10000},
                {"max_live_visual_delegates", maxDelegates},
                {"max_live_observed_delegates_including_pool", maxObservedInstances},
                {"frame_count", frames.size()},
                {"frame_interval_median_ms", frames.at(frames.size() / 2)},
                {"frame_interval_p95_ms",
                 frames.at(qMin(frames.size() - 1, frames.size() * 95 / 100))},
                {"frame_interval_max_ms", frames.last()},
                {"elapsed_ms", clock.elapsed()},
                {"rss_before_scroll_bytes", before},
                {"rss_after_scroll_bytes", residentBytes()}};
            report.insert(kind == SearchModel::Kind::Song ? "songs" : "albums", metrics);
            QVERIFY(maxDelegates < (kind == SearchModel::Kind::Song ? 70 : 128));
            QVERIFY(maxObservedInstances < (kind == SearchModel::Kind::Song ? 70 : 128));
            const auto capture = session.window->grabWindow();
            QVERIFY(!capture.isNull());
            const qreal imageScale = qreal(capture.width()) / session.window->width();
            bool coloredCover = false;
            std::function<void(QQuickItem *)> checkCover = [&](QQuickItem *item) {
                if (item->objectName() == QStringLiteral("coverImage")) {
                    const auto center =
                        item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
                    const auto viewTop = view->mapToScene(QPointF(0, 0)).y();
                    if (center.y() > viewTop + 160 && center.y() < viewTop + view->height() - 20 &&
                        item->property("status").toInt() == 1) {
                        const QPoint pixel = qRound(center.x() * imageScale) >= 0
                                                 ? QPoint(qRound(center.x() * imageScale),
                                                          qRound(center.y() * imageScale))
                                                 : QPoint(-1, -1);
                        if (capture.rect().contains(pixel) &&
                            capture.pixelColor(pixel).hsvSaturationF() > 0.15)
                            coloredCover = true;
                    }
                }
                for (auto *child : item->childItems())
                    checkCover(child);
            };
            checkCover(view);
            QVERIFY2(coloredCover,
                     "Decoded cover must actually be rendered through the production rounded mask");
            const auto output = qEnvironmentVariable("ECHO_UI_REPORT_DIR");
            if (!output.isEmpty()) {
                QDir().mkpath(output);
                QVERIFY(capture.save(output + "/" + viewName + ".png"));
            }
        }
        // Exercise the same pooled cover component in a dark circular artist
        // card, including actual pixels rather than Image.Ready alone.
        session.theme.setMode(ThemeStore::Dark);
        SearchModelTestAccess::populate(session.search, SearchModel::Kind::Artist, 200,
                                        coverDirectory);
        auto *artistView = session.item("searchResultGrid");
        QVERIFY(artistView);
        artistView->setProperty("contentY", -artistView->property("topMargin").toReal());
        QTest::qWait(400);
        const auto artistCapture = session.window->grabWindow();
        QVERIFY(!artistCapture.isNull());
        const qreal artistScale = qreal(artistCapture.width()) / session.window->width();
        bool circularCover = false;
        std::function<void(QQuickItem *)> checkCircle = [&](QQuickItem *item) {
            if (item->objectName() == QStringLiteral("coverImage") &&
                item->property("status").toInt() == 1) {
                const auto center =
                    item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
                const auto corner = item->mapToScene(QPointF(2, 2));
                if (center.y() > 160 && center.y() < session.window->height() - 130) {
                    const QPoint middle(qRound(center.x() * artistScale),
                                        qRound(center.y() * artistScale));
                    const QPoint edge(qRound(corner.x() * artistScale),
                                      qRound(corner.y() * artistScale));
                    if (artistCapture.rect().contains(middle) &&
                        artistCapture.rect().contains(edge) &&
                        artistCapture.pixelColor(middle).hsvSaturationF() > 0.15 &&
                        artistCapture.pixelColor(edge).hsvSaturationF() < 0.1)
                        circularCover = true;
                }
            }
            for (auto *child : item->childItems())
                checkCircle(child);
        };
        checkCircle(artistView);
        QVERIFY2(circularCover,
                 "Dark artist card must render a colored circular cover with masked corners");
        const auto artistOutput = qEnvironmentVariable("ECHO_UI_REPORT_DIR");
        if (!artistOutput.isEmpty())
            QVERIFY(artistCapture.save(artistOutput + "/artists-dark.png"));
        QCOMPARE(warnings.count(), 0);
        const auto output = qEnvironmentVariable("ECHO_UI_REPORT_DIR");
        if (!output.isEmpty()) {
            QFile file(output + "/rendering.json");
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QJsonDocument(report).toJson());
        }
        qInfo().noquote() << QJsonDocument(report).toJson(QJsonDocument::Compact);
    }
};
int main(int argc, char **argv)
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication app(argc, argv);
    configureUiTypography();
    SearchUiTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "SearchUiTest.moc"
