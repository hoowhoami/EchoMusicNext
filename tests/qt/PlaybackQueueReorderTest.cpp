#include "PlaybackService.h"
#include "PlaylistOrderModel.h"
#include "PlaybackRepository.h"
#include "ThemeStore.h"
#include "SettingsStore.h"
#include "NativeWindow.h"
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QQmlComponent>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QtTest>

using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static bool restoring(const PlaybackService &p)
    {
        return p.m_queueRestorePending;
    }
    static quint64 sequence(const PlaybackService &p)
    {
        return p.m_sequence;
    }
    static PlaybackRepository *repository(PlaybackService &p)
    {
        return p.m_repository.get();
    }
    static bool savePending(const PlaybackService &p)
    {
        return p.m_saveTimer.isActive();
    }
    static void persist(PlaybackService &p)
    {
        p.persist();
    }
};
class PlaylistOrderModelTestAccess
{
public:
    // Seed domain rows only: this UI fixture does not invent provider payloads
    // or exercise cloud writes. Existing model tests own revision/commit rules.
    static void seed(PlaylistOrderModel &model, bool tracks, int count = 1000,
                     bool longFirst = false)
    {
        if (model.m_request)
            model.m_api.cancel(model.m_request);
        model.beginResetModel();
        model.m_request = 0;
        model.m_saving = false;
        model.m_open = true;
        model.m_mode = tracks ? 1 : 0;
        model.m_selected = -1;
        model.m_error.clear();
        model.m_snapshot.clear();
        model.m_order.clear();
        for (int i = 0; i < count; ++i) {
            model.m_snapshot.append(
                {quint64(i + 1), 0, i,
                 QString(tracks ? "歌曲 %1 · Track" : "歌单 %1 · Playlist").arg(i),
                 tracks ? QStringLiteral("歌手 Artist · 专辑 Album") : QStringLiteral("32 首歌曲"),
                 32});
            model.m_order.append(i);
        }
        if (longFirst && count > 0) {
            auto &row = model.m_snapshot.first();
            row.title = QStringLiteral("很长的名称 Long title ").repeated(12);
            row.subtitle = tracks ? QStringLiteral("很长的歌手 Artist ").repeated(12)
                                  : QStringLiteral("611 首歌曲");
            row.songs = 611;
        }
        model.m_original = model.m_order;
        model.endResetModel();
        emit model.changed();
    }
};
} // namespace EchoMusic
namespace
{
QQuickItem *item(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = item(child, name))
            return found;
    return nullptr;
}
bool metadataOnRight(QQuickItem *row)
{
    if (!row)
        return false;
    auto *ordinal = item(row, "playlistOrderIndex");
    auto *title = item(row, "playlistOrderTitle");
    auto *subtitle = item(row, "playlistOrderSubtitle");
    if (!ordinal || !title || !subtitle)
        return false;
    const auto titleRight = title->mapToItem(row, {title->width(), 0}).x();
    const auto metadataLeft = subtitle->mapToItem(row, {0, 0}).x();
    const auto metadataRight = subtitle->mapToItem(row, {subtitle->width(), 0}).x();
    const auto ordinalBaseline = ordinal->mapToItem(row, {0, ordinal->baselineOffset()}).y();
    const auto titleBaseline = title->mapToItem(row, {0, title->baselineOffset()}).y();
    const auto metadataBaseline = subtitle->mapToItem(row, {0, subtitle->baselineOffset()}).y();
    return metadataLeft >= titleRight + 9.5 &&
           qAbs(metadataRight - (row->width() - row->property("rightPadding").toReal())) < 0.51 &&
           subtitle->width() <= row->width() * 0.4 + 0.51 &&
           qAbs(ordinalBaseline - titleBaseline) < 0.51 &&
           qAbs(titleBaseline - metadataBaseline) < 0.51 && row->height() == 52;
}
int rowCount(QQuickItem *root)
{
    int count = root->objectName() == "queueRow" || root->objectName() == "playlistOrderRow";
    for (auto *child : root->childItems())
        count += rowCount(child);
    return count;
}
PlaybackTracks songs(const QString &prefix)
{
    PlaybackTracks result;
    for (int i = 0; i < 1000; ++i) {
        PlaybackTrack song;
        song.id = prefix + QString::number(i);
        song.title = "Queue song " + song.id;
        song.artist = QStringLiteral("Artist 歌手");
        song.localPath = "/queue-ui-test/" + song.id + ".wav";
        result.append(song);
    }
    return result;
}
struct Fixture
{
    QTemporaryDir dir;
    EchoMusic::SettingsStore settings{dir.filePath("prefs.ini")};
    ApiClient api{DeviceIdentity::temporary()};
    PlaybackService playback{api, settings, nullptr, dir.filePath("queue.sqlite")};
    PlaylistMutationService mutations{api};
    PlaylistOrderModel order{api, mutations};
    ThemeStore theme{settings};
    QQmlEngine engine;
    QScopedPointer<QObject> root;
    QQuickWindow *window = nullptr;
    ~Fixture()
    {
        if (window && (QGuiApplication::mouseButtons() & Qt::LeftButton))
            QTest::mouseRelease(window, Qt::LeftButton);
    }
    bool create(bool dark = false, qreal scale = 1.)
    {
        api.setNetworkTransport(1);
        theme.attachToEngine(engine);
        theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls.Basic
            import EchoMusic.Library
            import EchoMusic.Playback
            import EchoMusic.Controls
            ApplicationWindow {
                id: host
                required property PlaybackService playback
                required property PlaylistOrderModel orderEditor
                property bool trackOrdering: false
                property alias orderDialog: orderOwner.item
                function openOrder(): void { orderOwner.open(); }
                property real panelScale: 1
                property alias drawer: owner.item
                width: 900; height: 820; visible: true
                function openQueue(): void { owner.open(); }
                EchoPopupLoader {
                    id: orderOwner
                    hostItem: host.contentItem
                    sourceComponent: PlaylistOrderDialog {
                        editor: host.orderEditor
                        tracksMode: host.trackOrdering
                        tracksListId: 7
                        tracksTitle: "测试歌单"
                        uiScale: host.panelScale
                    }
                }
                EchoPopupLoader {
                    id: owner
                    hostItem: host.contentItem
                    sourceComponent: PlaybackQueueDrawer {
                        playback: host.playback
                        uiScale: host.panelScale
                    }
                }
            }
        )",
                          QUrl());
        root.reset(
            component.createWithInitialProperties({{"playback", QVariant::fromValue(&playback)},
                                                   {"orderEditor", QVariant::fromValue(&order)},
                                                   {"panelScale", scale}}));
        if (!root) {
            qWarning() << component.errorString();
            return false;
        }
        window = qobject_cast<QQuickWindow *>(root.data());
        return window && Test::prepareNativeInput(window);
    }
    QObject *open()
    {
        QMetaObject::invokeMethod(root.data(), "openQueue");
        return root->property("drawer").value<QObject *>();
    }
    QObject *openOrder(bool tracks)
    {
        root->setProperty("trackOrdering", tracks);
        QMetaObject::invokeMethod(root.data(), "openOrder");
        return root->property("orderDialog").value<QObject *>();
    }
    QQuickItem *find(const QString &name)
    {
        return item(window->contentItem(), name);
    }
    QPoint point(QQuickItem *view, qreal x, qreal y)
    {
        return view->mapToScene({x, y}).toPoint();
    }
    void begin(QQuickItem *view, qreal y)
    {
        auto start = point(view, 160, y);
        Test::moveNativePointer(window, start);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(window, start + QPoint(0, 24), 20);
    }
};
} // namespace
class PlaybackQueueReorderTest final : public QObject
{
    Q_OBJECT
private slots:
    void dragCommitsOnceAndPersists_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<qreal>("scale");
        for (bool dark : {false, true})
            for (qreal scale : {1., 1.25})
                QTest::newRow(qPrintable(QString("%1-%2").arg(dark ? "dark" : "light").arg(scale)))
                    << dark << scale;
    }
    void dragCommitsOnceAndPersists()
    {
        QFETCH(bool, dark);
        QFETCH(qreal, scale);
        Fixture f;
        QSignalSpy warnings(&f.engine, &QQmlEngine::warnings);
        QVERIFY(f.create(dark, scale));
        QTRY_VERIFY(!PlaybackServiceTestAccess::restoring(f.playback));
        auto *catalog = f.playback.queueCatalog();
        auto *active = catalog->activate("active", "Active", songs("a"), 204);
        auto *history = catalog->retain("history", "History", songs("h"));
        history->setCurrent(204);
        const auto sequence = PlaybackServiceTestAccess::sequence(f.playback);
        for (auto *model : {active, history}) {
            f.theme.setMotionEnabled(model == active);
            QPointer<QObject> drawer = f.open();
            QVERIFY(drawer);
            QTRY_VERIFY(drawer->property("opened").toBool());
            if (model == history)
                catalog->setViewIndex(catalog->rowCount() - 1);
            QTRY_COMPARE(catalog->viewModel(), model);
            auto *view = f.find("playbackQueueList");
            auto *control = f.find("playbackQueueReorder");
            QVERIFY(view && control);
            // EchoViewPoolGuard restores a switched model asynchronously.
            // Wait before setting scroll or retaining any delegate pointer.
            QTRY_COMPARE(view->property("model").value<QObject *>(), model);
            auto *guard = view->findChild<QObject *>("viewPoolGuard");
            QVERIFY(guard);
            auto *reset = guard->property("resetTimer").value<QObject *>();
            QVERIFY(reset);
            QTRY_VERIFY(!reset->property("running").toBool());
            QVERIFY(QMetaObject::invokeMethod(view, "forceLayout"));
            view->setProperty("contentY", 200. * 56);
            QTest::qWait(50);
            model->restoreNext({model->at(300)->key()});
            const auto next = model->nextKeys();
            const auto id = model->at(204)->id;
            QSignalSpy moves(model, &QAbstractItemModel::rowsMoved);
            f.begin(view, 4 * 56 + 28);
            QTRY_VERIFY(control->property("dragging").toBool());
            QCOMPARE(control->property("fromIndex").toInt(), 204);
            const auto end = f.point(view, 160, 56 + 28);
            QTest::mouseMove(f.window, end, 20);
            QTRY_COMPARE(control->property("toIndex").toInt(), 201);
            QPointer<QQuickItem> ghost = f.find("queueDragGhost");
            QVERIFY(ghost && ghost->isVisible());
            QCOMPARE(ghost->property("title").toString(), model->at(204)->title);
            QCOMPARE(moves.size(), 0); // Preview does not write the queue.
            QCOMPARE(model->at(204)->id, id);
            QQuickItem *displaced = nullptr;
            for (auto *row : view->property("contentItem").value<QQuickItem *>()->childItems())
                if (row->objectName() == "queueRow" && row->property("index").toInt() == 202)
                    displaced = item(row, "reorderRowBody");
            QVERIFY(displaced);
            QTRY_VERIFY(qAbs(displaced->mapToItem(displaced->parentItem(), {0, 0}).y() - 56) < 1);
            const auto captures = qEnvironmentVariable("ECHO_QUEUE_REORDER_CAPTURE_DIR");
            if (!captures.isEmpty()) {
                QVERIFY(QDir().mkpath(captures));
                QVERIFY(f.window->grabWindow().save(
                    captures + QString("/%1-%2-%3-drag.png")
                                   .arg(dark ? "dark" : "light")
                                   .arg(scale)
                                   .arg(model == active ? "active" : "history")));
            }
            QTest::mouseRelease(f.window, Qt::LeftButton, Qt::NoModifier, end);
            QTRY_COMPARE(moves.size(), 1);
            QCOMPARE(model->at(201)->id, id);
            QCOMPARE(model->currentIndex(), 201);
            QCOMPARE(model->nextKeys(), next);
            QCOMPARE(catalog->activeModel(), active);
            QCOMPARE(PlaybackServiceTestAccess::sequence(f.playback), sequence);
            QVERIFY(!f.playback.playing());
            QTRY_VERIFY(ghost.isNull());
            QVERIFY(rowCount(view) < 80);
            QVERIFY(PlaybackServiceTestAccess::savePending(f.playback));
            PlaybackServiceTestAccess::persist(f.playback);
            auto *repository = PlaybackServiceTestAccess::repository(f.playback);
            QSignalSpy loaded(repository, &PlaybackRepository::loaded);
            repository->load(PlaybackRepository::SharedQueueScope, 999);
            QTRY_COMPARE(loaded.size(), 1);
            bool found = false;
            for (const auto &state : qvariant_cast<PlaybackQueueStates>(loaded[0][2]))
                if (state.key == (model == active ? "active" : "history")) {
                    QCOMPARE(state.tracks[201].id, id);
                    QCOMPARE(state.current, 201);
                    QCOMPARE(state.next, next);
                    found = true;
                }
            QVERIFY(found);
            QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
            QTRY_VERIFY(drawer.isNull());
        }
        QCOMPARE(warnings.size(), 0);
    }
    void autoScrollSurvivesRecyclingAndCloseCancels()
    {
        Fixture f;
        QVERIFY(f.create());
        QTRY_VERIFY(!PlaybackServiceTestAccess::restoring(f.playback));
        auto *model = f.playback.queueCatalog()->activate("active", "Active", songs("a"), -1);
        QPointer<QObject> drawer = f.open();
        QTRY_VERIFY(drawer->property("opened").toBool());
        auto *view = f.find("playbackQueueList");
        QPointer<QQuickItem> control = f.find("playbackQueueReorder");
        QVERIFY(view && control);
        QSignalSpy moves(model, &QAbstractItemModel::rowsMoved);
        f.begin(view, 2 * 56 + 28);
        QTRY_VERIFY(control->property("dragging").toBool());
        const auto id = model->at(2)->id;
        const auto end = f.point(view, 160, view->height() - 2);
        QTest::mouseMove(f.window, end, 20);
        QTRY_VERIFY(control->property("autoScrolling").toBool());
        QTRY_VERIFY(view->property("contentY").toReal() > 20);
        // Cross the cache boundary without tying this lifecycle test to a fast
        // auto-scroll rate. The independent edge test measures the speed cap.
        view->setProperty("contentY", 60. * 56);
        QVERIFY(QMetaObject::invokeMethod(view, "forceLayout"));
        QTRY_VERIFY(control->property("toIndex").toInt() >= 60);
        const qreal recycled = view->property("contentY").toReal();
        QTRY_VERIFY(view->property("contentY").toReal() > recycled + 10);
        auto *ghost = f.find("queueDragGhost");
        QVERIFY(ghost);
        QVERIFY(qAbs(ghost->mapToItem(view, {0, 28}).y() - (view->height() - 28)) < 1);
        QCOMPARE(ghost->property("title").toString(), model->at(2)->title);
        QVERIFY(rowCount(view) < 80);
        const int target = control->property("toIndex").toInt();
        QTest::mouseRelease(f.window, Qt::LeftButton, Qt::NoModifier, end);
        QTRY_COMPARE(moves.size(), 1);
        QCOMPARE(model->at(target)->id, id);
        moves.clear();
        f.begin(view, 56 + 28);
        QTRY_VERIFY(control->property("dragging").toBool());
        QTest::mouseMove(f.window, f.point(view, 160, 5), 20);
        QTest::keyClick(f.window, Qt::Key_Escape);
        QTRY_VERIFY(drawer.isNull());
        QTRY_VERIFY(control.isNull());
        QTest::mouseRelease(f.window, Qt::LeftButton, Qt::NoModifier, end);
        QTest::qWait(50);
        QCOMPARE(moves.size(), 0);
    }
    void queueMutationCancelsAndButtonsRemainIndependent()
    {
        Fixture f;
        QVERIFY(f.create());
        QTRY_VERIFY(!PlaybackServiceTestAccess::restoring(f.playback));
        auto *catalog = f.playback.queueCatalog();
        auto *model = catalog->activate("active", "Active", songs("a"), -1);
        QPointer<QObject> drawer = f.open();
        QTRY_VERIFY(drawer->property("opened").toBool());
        auto *view = f.find("playbackQueueList");
        auto *control = f.find("playbackQueueReorder");
        QVERIFY(view && control);
        QSignalSpy moves(model, &QAbstractItemModel::rowsMoved);
        f.begin(view, 2 * 56 + 28);
        QTRY_VERIFY(control->property("dragging").toBool());
        model->remove(900);
        QTRY_VERIFY(!control->property("dragging").toBool());
        QTest::mouseRelease(f.window, Qt::LeftButton, Qt::NoModifier, f.point(view, 160, 28));
        QTest::qWait(50);
        QCOMPARE(moves.size(), 0);
        f.begin(view, 2 * 56 + 28);
        QTRY_VERIFY(control->property("dragging").toBool());
        catalog->retain("history", "History", songs("h"));
        catalog->setViewIndex(catalog->rowCount() - 1);
        QTRY_VERIFY(!control->property("dragging").toBool());
        QTest::mouseRelease(f.window, Qt::LeftButton, Qt::NoModifier, f.point(view, 160, 28));
        QCOMPARE(moves.size(), 0);
        catalog->resetPreview();
        QTRY_COMPARE(view->property("model").value<QObject *>(), model);
        view->setProperty("contentY", 0.);
        QVERIFY(QMetaObject::invokeMethod(view, "forceLayout"));
        // Leaving history removes its resume toolbar. Wait for the ancestor
        // ColumnLayout's frame before using the first row's scene geometry.
        QSignalSpy rendered(f.window, &QQuickWindow::frameSwapped);
        f.window->requestUpdate();
        QVERIFY(rendered.wait(1000));
        QVERIFY(QMetaObject::invokeMethod(view, "positionViewAtBeginning"));
        QQuickItem *firstRow = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(
                        view, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, firstRow), Q_ARG(int, 0)) &&
                    firstRow);
        auto *remove = item(firstRow, "queueRemoveButton");
        QVERIFY(remove);
        QTRY_VERIFY2(
            ([&] {
                Test::moveNativePointer(
                    f.window,
                    remove->mapToScene({remove->width() / 2, remove->height() / 2}).toPoint());
                return remove->isVisible();
            })(),
            qPrintable(QString("hover=%1 active=%2 pooled=%3 from=%4 drag=%5 y=%6 contentY=%7")
                           .arg(firstRow->property("hovered").toBool())
                           .arg(catalog->viewActive())
                           .arg(firstRow->property("pooled").toBool())
                           .arg(control->property("fromIndex").toInt())
                           .arg(control->property("dragging").toBool())
                           .arg(firstRow->y())
                           .arg(view->property("contentY").toReal())));
        const auto button =
            remove->mapToScene({remove->width() / 2, remove->height() / 2}).toPoint();
        const int count = model->rowCount();
        QTest::mouseClick(f.window, Qt::LeftButton, Qt::NoModifier, button);
        QTRY_COMPARE(model->rowCount(), count - 1);
        QCOMPARE(moves.size(), 0);
    }
    void edgeScrollStopsAndLandingCommits_data()
    {
        QTest::addColumn<int>("kind"); // 0 queue, 1 playlists, 2 playlist tracks
        QTest::addColumn<bool>("dark");
        QTest::addColumn<qreal>("scale");
        QTest::newRow("queue") << 0 << false << 1.;
        QTest::newRow("playlists-light") << 1 << false << 1.;
        QTest::newRow("playlists-dark-125") << 1 << true << 1.25;
        QTest::newRow("tracks-light-125") << 2 << false << 1.25;
        QTest::newRow("tracks-dark") << 2 << true << 1.;
    }
    void edgeScrollStopsAndLandingCommits()
    {
        QFETCH(int, kind);
        QFETCH(bool, dark);
        QFETCH(qreal, scale);
        Fixture f;
        QSignalSpy warnings(&f.engine, &QQmlEngine::warnings);
        QVERIFY(f.create(dark, scale));
        QTRY_VERIFY(!PlaybackServiceTestAccess::restoring(f.playback));
        auto *queue = f.playback.queueCatalog()->activate("active", "Active", songs("a"), -1);
        QPointer<QObject> popup = kind == 0 ? f.open() : f.openOrder(kind == 2);
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        if (kind)
            PlaylistOrderModelTestAccess::seed(f.order, kind == 2, 1000, dark);
        auto *view = f.find(kind ? "playlistOrderList" : "playbackQueueList");
        QPointer<QQuickItem> control =
            f.find(kind ? "playlistOrderReorder" : "playbackQueueReorder");
        QVERIFY(view && control);
        QAbstractItemModel *model = kind ? static_cast<QAbstractItemModel *>(&f.order) : queue;
        QSignalSpy moves(model, &QAbstractItemModel::rowsMoved);
        QSignalSpy writes(&f.mutations, &PlaylistMutationService::changed);
        const int height = kind ? 52 : 56;
        QTRY_VERIFY(view->height() > height);
        QVERIFY(QMetaObject::invokeMethod(view, "forceLayout"));
        if (kind) {
            QQuickItem *row = nullptr;
            QTRY_VERIFY(QMetaObject::invokeMethod(view, "itemAtIndex",
                                                  Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, 0)) &&
                        row);
            auto *visual = item(row, "playlistOrderRowVisual");
            QTRY_VERIFY(metadataOnRight(visual));
            if (dark) {
                QTRY_VERIFY(item(visual, "playlistOrderTitle")->property("truncated").toBool());
                if (kind == 2)
                    QTRY_VERIFY(
                        item(visual, "playlistOrderSubtitle")->property("truncated").toBool());
            }
        }
        f.begin(view, height / 2.);
        QTRY_VERIFY(control->property("dragging").toBool());
        const auto edge = f.point(view, 160, view->height() - 2);
        const qreal initial = view->property("contentY").toReal();
        QTest::mouseMove(f.window, edge, 20);
        QTest::qWait(65);
        QVERIFY(!control->property("autoScrolling").toBool());
        QCOMPARE(view->property("contentY").toReal(), initial); // dwell before scrolling
        QTRY_VERIFY(control->property("autoScrolling").toBool());
        const qreal before = view->property("contentY").toReal();
        QElapsedTimer elapsed;
        elapsed.start();
        QTest::qWait(420);
        const qreal delta = view->property("contentY").toReal() - before;
        QVERIFY(delta > 10);
        QVERIFY2(delta <= 180 * elapsed.elapsed() / 1000. + 3, qPrintable(QString::number(delta)));
        QTest::mouseMove(f.window, f.point(view, 160, view->height() / 2), 20);
        QVERIFY(!control->property("autoScrolling").toBool());
        const qreal middle = view->property("contentY").toReal();
        QTest::qWait(240);
        QCOMPARE(view->property("contentY").toReal(), middle);
        QTest::mouseMove(f.window, edge, 20);
        QTRY_VERIFY(control->property("autoScrolling").toBool());
        QTest::mouseMove(f.window, f.point(view, 160, view->height() + 16), 20);
        QVERIFY(!control->property("autoScrolling").toBool());
        const qreal outside = view->property("contentY").toReal();
        QTest::qWait(240);
        QCOMPARE(view->property("contentY").toReal(), outside);
        QTest::mouseMove(f.window, edge, 20);
        QTRY_VERIFY(control->property("autoScrolling").toBool());
        QCOMPARE(moves.size(), 0);
        QPointer<QQuickItem> ghost = f.find(kind ? "playlistOrderGhost" : "queueDragGhost");
        QVERIFY(ghost);
        if (kind)
            QTRY_VERIFY(metadataOnRight(ghost));
        const auto captures = qEnvironmentVariable("ECHO_QUEUE_REORDER_CAPTURE_DIR");
        if (kind && !captures.isEmpty()) {
            QVERIFY(QDir().mkpath(captures));
            QVERIFY(f.window->grabWindow().save(
                captures + QString("/order-%1-%2-%3-drag.png").arg(kind).arg(dark).arg(scale)));
        }
        const qreal released = view->property("contentY").toReal();
        QTest::mouseRelease(f.window, Qt::LeftButton, Qt::NoModifier, edge);
        QVERIFY(!control->property("autoScrolling").toBool());
        QVERIFY(control->property("settling").toBool());
        QCOMPARE(moves.size(), 0); // Landing still presents the original snapshot.
        QTRY_COMPARE(moves.size(), 1);
        QVERIFY(!control->property("busy").toBool());
        QTRY_VERIFY(ghost.isNull());
        for (auto *row : view->property("contentItem").value<QQuickItem *>()->childItems()) {
            if (auto *body = item(row, "reorderRowBody"))
                QTRY_VERIFY(qAbs(body->mapToItem(row, {0, 0}).y()) < 0.5);
        }
        QTest::qWait(240);
        // Flickable may align a fractional offset to its nearest logical pixel
        // when interaction resumes. There must be no continuing edge scroll.
        QVERIFY(qAbs(view->property("contentY").toReal() - released) <= 0.51);
        const qreal aligned = view->property("contentY").toReal();
        QTest::qWait(160);
        QCOMPARE(view->property("contentY").toReal(), aligned);
        QCOMPARE(writes.size(), 0); // No implicit cloud save.
        if (kind)
            QVERIFY(f.order.dirty());
        QVERIFY(rowCount(view) < 80);
        f.begin(view, height / 2.);
        QTRY_VERIFY(control->property("dragging").toBool());
        QTest::mouseMove(f.window, edge, 20);
        QTRY_VERIFY(control->property("autoScrolling").toBool());
        QTest::keyClick(f.window, Qt::Key_Escape);
        QTRY_VERIFY(popup.isNull());
        QTRY_VERIFY(control.isNull());
        QTest::mouseRelease(f.window, Qt::LeftButton, Qt::NoModifier, edge);
        QCOMPARE(moves.size(), 1); // Escape never commits a second move.
        if (kind)
            f.openOrder(kind == 2);
        else
            f.open();
        // Reopening during the owner's queued teardown is intentionally deferred.
        QTRY_VERIFY((popup = f.root->property(kind ? "orderDialog" : "drawer").value<QObject *>()));
        QTRY_VERIFY(popup->property("opened").toBool());
        if (kind)
            PlaylistOrderModelTestAccess::seed(f.order, kind == 2);
        view = f.find(kind ? "playlistOrderList" : "playbackQueueList");
        control = f.find(kind ? "playlistOrderReorder" : "playbackQueueReorder");
        QVERIFY(view && control);
        f.begin(view, height / 2.);
        QTRY_VERIFY(control->property("dragging").toBool());
        const auto drop = f.point(view, 160, height * 1.5);
        QTest::mouseMove(f.window, drop, 20);
        QTest::mouseRelease(f.window, Qt::LeftButton, Qt::NoModifier, drop);
        QVERIFY(control->property("settling").toBool());
        QTest::keyClick(f.window, Qt::Key_Escape);
        QVERIFY(control.isNull() || !control->property("busy").toBool());
        QTRY_VERIFY(popup.isNull());
        QCOMPARE(moves.size(), 1); // Closing during landing cancels that move too.
        QCOMPARE(warnings.size(), 0);
    }
    void noOverflowDoesNotScroll_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("queue") << 0;
        QTest::newRow("playlists") << 1;
        QTest::newRow("tracks") << 2;
    }
    void noOverflowDoesNotScroll()
    {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.create());
        QTRY_VERIFY(!PlaybackServiceTestAccess::restoring(f.playback));
        auto shortQueue = songs("a");
        shortQueue.resize(2);
        f.playback.queueCatalog()->activate("active", "Active", shortQueue, -1);
        QPointer<QObject> popup = kind ? f.openOrder(kind == 2) : f.open();
        QTRY_VERIFY(popup->property("opened").toBool());
        if (kind)
            PlaylistOrderModelTestAccess::seed(f.order, kind == 2, 2);
        auto *view = f.find(kind ? "playlistOrderList" : "playbackQueueList");
        auto *control = f.find(kind ? "playlistOrderReorder" : "playbackQueueReorder");
        QVERIFY(view && control);
        QTRY_VERIFY(view->property("contentHeight").toReal() <= view->height());
        f.begin(view, kind ? 26 : 28);
        QTRY_VERIFY(control->property("dragging").toBool());
        const auto edge = f.point(view, 160, view->height() - 2);
        QTest::mouseMove(f.window, edge, 20);
        QTest::qWait(300);
        QCOMPARE(control->property("toIndex").toInt(), 1); // Empty tail is the last slot.
        QVERIFY(!control->property("autoScrolling").toBool());
        QCOMPARE(view->property("contentY").toReal(), 0.);
        f.window->hide();
        QTRY_VERIFY(!control->property("busy").toBool());
        QTest::mouseRelease(f.window, Qt::LeftButton, Qt::NoModifier, edge);
        if (kind)
            QVERIFY(!f.order.dirty());
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QTRY_VERIFY(popup.isNull());
    }
    void compactHeaderMetadata_data()
    {
        dragCommitsOnceAndPersists_data();
    }
    void compactHeaderMetadata()
    {
        QFETCH(bool, dark);
        QFETCH(qreal, scale);
        Fixture f;
        QSignalSpy warnings(&f.engine, &QQmlEngine::warnings);
        QVERIFY(f.create(dark, scale));
        QTRY_VERIFY(!PlaybackServiceTestAccess::restoring(f.playback));
        auto *catalog = f.playback.queueCatalog();
        catalog->activate("active", "我最喜爱", songs("a"), -1);
        QPointer<QObject> popup = f.open();
        QTRY_VERIFY(popup->property("opened").toBool());
        auto *header = f.find("queueHeader");
        auto *subtitle = f.find("queueSubtitle");
        auto *title = f.find("queueTitle");
        auto *view = f.find("playbackQueueList");
        auto *close = f.find("queueCloseButton");
        auto *actions = f.find("queueHeaderActions");
        QVERIFY(header && subtitle && title && view && close && actions);
        QTRY_COMPARE(header->height(), 78.);
        QVERIFY(!subtitle->isVisible());
        const qreal compactHeight = header->height();
        const qreal viewportTop = view->mapToItem(header, {0, 0}).y();
        QCOMPARE(close->width(), 28.);
        QCOMPARE(close->height(), 28.);
        QCOMPARE(actions->width(), 4 * 28. + 3 * 4);
        for (const auto *name :
             {"queueScrollTopButton", "queueLocateButton", "queueAddButton", "queueClearButton"}) {
            auto *button = f.find(name);
            QVERIFY(button);
            QCOMPARE(button->width(), 28.);
            QCOMPARE(button->height(), 28.);
        }
        for (const auto *name : {"queueCloseButton", "queueScrollTopButton", "queueLocateButton",
                                 "queueAddButton", "queueClearButton"}) {
            auto *button = f.find(name);
            QTRY_VERIFY(([&] {
                auto *icon = item(button, "iconImage");
                return icon && icon->property("status").toInt() == 1;
            })());
        }
        const auto captures = qEnvironmentVariable("ECHO_QUEUE_REORDER_CAPTURE_DIR");
        const auto capture = [&](const QString &state) {
            if (captures.isEmpty())
                return true;
            QSignalSpy frames(f.window, &QQuickWindow::frameSwapped);
            f.window->requestUpdate();
            if (!frames.wait(1000))
                return false;
            return QDir().mkpath(captures) &&
                   f.window->grabWindow().save(
                       captures + QString("/header-%1-%2-%3.png").arg(dark).arg(scale).arg(state));
        };
        QVERIFY(capture("ordinary"));
        catalog->activate("search/周杰伦 Jay Chou", "搜索结果", songs("search"), -1);
        catalog->setViewIndex(catalog->rowCount() - 1);
        QTRY_VERIFY(subtitle->isVisible());
        QCOMPARE(subtitle->property("text").toString(), QStringLiteral("周杰伦 Jay Chou"));
        QTRY_COMPARE(header->height(), compactHeight);
        QTRY_COMPARE(view->mapToItem(header, {0, 0}).y(), viewportTop);
        QTRY_VERIFY(subtitle->mapToItem(header, {0, 0}).x() >=
                    title->mapToItem(header, {title->width(), 0}).x() + 7.5);
        QTRY_VERIFY(qAbs(subtitle->mapToItem(header, {0, subtitle->baselineOffset()}).y() -
                         title->mapToItem(header, {0, title->baselineOffset()}).y()) < 0.51);
        QVERIFY(capture("search"));
        const QString longQuery = QStringLiteral("周杰伦 Jay Chou 很长的搜索说明 ").repeated(20);
        catalog->activate("search/" + longQuery, "搜索结果", songs("long"), -1);
        catalog->resetPreview();
        QTRY_COMPARE(subtitle->property("text").toString(), longQuery);
        QTRY_VERIFY(subtitle->property("truncated").toBool());
        QVERIFY(!title->property("truncated").toBool());
        QTRY_COMPARE(header->height(), compactHeight);
        QTRY_COMPARE(view->mapToItem(header, {0, 0}).y(), viewportTop);
        QVERIFY(subtitle->mapToItem(header, {subtitle->width(), 0}).x() <=
                close->mapToItem(header, {0, 0}).x() - 7.5);
        QVERIFY(capture("search-long"));
        const qreal wideSubtitleWidth = subtitle->width();
        f.window->setWidth(420);
        QTRY_COMPARE(popup->property("width").toReal(), qMin(380., 420. / scale - 16));
        QTRY_VERIFY(subtitle->width() <= wideSubtitleWidth);
        QTRY_VERIFY(subtitle->property("truncated").toBool());
        QTRY_COMPARE(header->height(), compactHeight);
        QVERIFY(capture("search-long-narrow"));
        catalog->setViewIndex(0);
        QTRY_VERIFY(!subtitle->isVisible());
        QTRY_COMPARE(header->height(), compactHeight);
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QTRY_VERIFY(popup.isNull());
        QCOMPARE(warnings.size(), 0);
    }
    void resumePreviewPresentation_data()
    {
        dragCommitsOnceAndPersists_data();
    }
    void resumePreviewPresentation()
    {
        QFETCH(bool, dark);
        QFETCH(qreal, scale);
        Fixture f;
        QSignalSpy warnings(&f.engine, &QQmlEngine::warnings);
        QVERIFY(f.create(dark, scale));
        f.theme.setMotionEnabled(false);
        QTRY_VERIFY(!PlaybackServiceTestAccess::restoring(f.playback));
        auto *catalog = f.playback.queueCatalog();
        auto tracks = songs("h");
        tracks.resize(3);
        tracks[0].title = QStringLiteral("Dirty Kiss");
        tracks[1].title.clear();
        tracks[2].title = QStringLiteral("很长的续播歌名 Long resume title ").repeated(12);
        catalog->activate("active", "当前队列", songs("a"), -1);
        auto *history = catalog->retain("history", "我最喜爱", tracks);
        QPointer<QObject> popup = f.open();
        QTRY_VERIFY(popup->property("opened").toBool());
        auto *toolbar = f.find("queueResumeToolbar");
        auto *button = f.find("queueResumeButton");
        auto *title = f.find("queueResumeSongTitle");
        auto *view = f.find("playbackQueueList");
        auto *header = f.find("queueHeader");
        QVERIFY(toolbar && button && title && view && header);
        QVERIFY(!toolbar->isVisible());
        const auto sequence = PlaybackServiceTestAccess::sequence(f.playback);
        catalog->switchPreview(1);
        QTRY_VERIFY(toolbar->isVisible());
        QTRY_COMPARE(title->property("text").toString(), tracks[0].title);
        QCOMPARE(catalog->activeKey(), QString("active"));
        QCOMPARE(PlaybackServiceTestAccess::sequence(f.playback), sequence);
        QTRY_COMPARE(button->height(), 36.);
        QCOMPARE(button->x(), 14.);
        QCOMPARE(button->y(), 8.);
        QCOMPARE(toolbar->height(), 50.);
        QCOMPARE(button->property("glyphSize").toReal(), 14.);
        QCOMPARE(header->height(), 78.);
        QTRY_COMPARE(view->mapToItem(header, {0, 0}).y(), 128.);
        const auto captures = qEnvironmentVariable("ECHO_QUEUE_REORDER_CAPTURE_DIR");
        const auto capture = [&](const QString &state) {
            if (captures.isEmpty())
                return true;
            QSignalSpy frames(f.window, &QQuickWindow::frameSwapped);
            f.window->requestUpdate();
            if (!frames.wait(1000))
                return false;
            return QDir().mkpath(captures) &&
                   f.window->grabWindow().save(
                       captures + QString("/resume-%1-%2-%3.png").arg(dark).arg(scale).arg(state));
        };
        QVERIFY(capture("first"));
        history->setCurrent(1);
        QTRY_COMPARE(title->property("text").toString(), QStringLiteral("这首歌"));
        // A late metadata title arrives through dataChanged, without queue replacement.
        history->updateFacts("h1", {}, QStringLiteral("如愿"), {}, {});
        QTRY_COMPARE(title->property("text").toString(), QStringLiteral("如愿"));
        QVERIFY(capture("metadata"));
        history->setCurrent(2);
        QTRY_COMPARE(title->property("text").toString(), tracks[2].title);
        QTRY_COMPARE(button->width(), 220.);
        auto *renderedTitle = title->property("textItem").value<QQuickItem *>();
        QVERIFY(renderedTitle);
        QTRY_VERIFY(renderedTitle->property("text").toString().endsWith(QChar(0x2026)));
        QVERIFY(capture("long"));
        f.window->setWidth(300);
        QTRY_VERIFY(button->width() < 220.);
        QTRY_VERIFY(button->width() <= (toolbar->width() - 28) * .7 + .01);
        QVERIFY(capture("narrow"));
        catalog->resetPreview();
        QTRY_VERIFY(!toolbar->isVisible());
        QTRY_COMPARE(view->mapToItem(header, {0, 0}).y(), 78.);
        catalog->switchPreview(1);
        QTRY_VERIFY(toolbar->isVisible());
        QTRY_COMPARE(view->mapToItem(header, {0, 0}).y(), 128.);
        const auto point = f.point(button, button->width() / 2, button->height() / 2);
        Test::moveNativePointer(f.window, point);
        QTest::mouseClick(f.window, Qt::LeftButton, Qt::NoModifier, point);
        QTRY_COMPARE(catalog->activeKey(), QString("history"));
        QTRY_COMPARE(f.playback.trackId(), QString("h2"));
        QTRY_VERIFY(!toolbar->isVisible());
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QTRY_VERIFY(popup.isNull());
        QCOMPARE(warnings.size(), 0);
    }
    void touchRequiresHoldAndThenReorders()
    {
        Fixture f;
        QVERIFY(f.create());
        QTRY_VERIFY(!PlaybackServiceTestAccess::restoring(f.playback));
        auto *model = f.playback.queueCatalog()->activate("active", "Active", songs("a"), -1);
        QPointer<QObject> drawer = f.open();
        QTRY_VERIFY(drawer->property("opened").toBool());
        auto *view = f.find("playbackQueueList");
        auto *control = f.find("playbackQueueReorder");
        QVERIFY(view && control);
        auto *device = QTest::createTouchDevice();
        const auto start = f.point(view, 160, 2 * 56 + 28);
        QTest::touchEvent(f.window, device).press(0, start, f.window);
        QTest::qWait(220);
        QTRY_VERIFY(control->property("touchReady").toBool());
        QTest::touchEvent(f.window, device).move(0, start + QPoint(0, 24), f.window);
        QTRY_VERIFY(control->property("dragging").toBool());
        const auto end = f.point(view, 160, 4 * 56 + 28);
        QTest::touchEvent(f.window, device).move(0, end, f.window);
        QTRY_COMPARE(control->property("toIndex").toInt(), 4);
        QTest::touchEvent(f.window, device).release(0, end, f.window);
        QTRY_COMPARE(model->at(4)->id, QString("a2"));
        QVERIFY(!control->property("dragging").toBool());
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const auto cursorPosition = QCursor::pos();
    const auto restoreCursor = qScopeGuard([cursorPosition] { QCursor::setPos(cursorPosition); });
    QQuickStyle::setStyle("Basic");
    PlaybackQueueReorderTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "PlaybackQueueReorderTest.moc"
