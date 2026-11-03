#include "AccountService.h"
#include "DiscoveryService.h"
#include "NativeWindow.h"
#include "SearchModel.h"
#include "SettingsStore.h"
#include "SongCatalogActions.h"
#include "ThemeStore.h"
#include "UiTypography.h"
#include <QDir>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static bool ready(const PlaybackService &p)
    {
        return p.m_ready && !p.m_queueRestorePending;
    }
    static void hold(PlaybackService &p)
    {
        p.m_ready = false;
    }
    static quint64 sequence(const PlaybackService &p)
    {
        return p.m_sequence;
    }
    static bool autoplay(const PlaybackService &p)
    {
        return p.m_autoplay;
    }
    // Complete the controlled pre-resolution boundary; real media transport is
    // covered by SongPlaybackUiTest's WAV playback cases.
    static void finish(PlaybackService &p, bool failed = false)
    {
        p.m_loading = false;
        p.m_loaded = !failed;
        p.m_playing = !failed;
        p.m_error = failed ? QStringLiteral("fixture failure") : QString();
        emit p.changed();
    }
};
class SearchModelTestAccess
{
public:
    static void populate(SearchModel &m, const PlaybackTracks &tracks)
    {
        m.beginResetModel();
        m.m_kind = SearchModel::Kind::Song;
        m.m_keyword = "fixture";
        auto &b = m.active();
        b = {};
        for (const auto &t : tracks)
            b.rows.append({t.title, t.artist, t.album, {}, "0:20", {}, t.id, 20000, t, false});
        b.loaded = true;
        b.state = SearchModel::State::Ready;
        b.total = tracks.size();
        m.endResetModel();
        emit m.changed();
    }
};
} // namespace EchoMusic
static QQuickItem *find(QQuickItem *root, const QString &name, const QString &id = {})
{
    if (root->isVisible() && root->objectName() == name &&
        (id.isEmpty() || root->property("resourceId").toString() == id))
        return root;
    for (auto *child : root->childItems())
        if (auto *result = find(child, name, id))
            return result;
    return nullptr;
}
class PlaybackButtonUiTest : public QObject
{
    Q_OBJECT
private slots:
    void loadingFollowsOnlyTheCurrentTrack_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<qreal>("zoom");
        for (bool dark : {false, true})
            for (qreal zoom : {1., 1.25})
                QTest::newRow(qPrintable(QString("%1-%2").arg(dark).arg(zoom))) << dark << zoom;
    }
    void loadingFollowsOnlyTheCurrentTrack()
    {
        QFETCH(bool, dark);
        QFETCH(qreal, zoom);
        QTemporaryDir dir;
        SettingsStore settings(dir.filePath("settings.ini"));
        ApiClient api(DeviceIdentity::temporary());
        api.addInterceptor(100, [](const QJsonObject &, ApiInterceptNext, ApiInterceptDone) {
            return std::function<void()>([] {});
        });
        AccountService account(api);
        PlaybackService playback(api, settings, nullptr, dir.filePath("queue.sqlite"));
        QTRY_VERIFY_WITH_TIMEOUT(PlaybackServiceTestAccess::ready(playback), 15000);
        PlaybackServiceTestAccess::hold(playback);
        DiscoveryService discovery(api, playback, account);
        SongCatalogModel catalog;
        const QJsonArray songs{
            QJsonObject{{"id", "1"},
                        {"hash", QString(32, 'a')},
                        {"title", "Song A"},
                        {"artists", QJsonArray{QJsonObject{{"name", "Artist A"}}}}},
            QJsonObject{{"id", "2"},
                        {"hash", QString(32, 'b')},
                        {"title", "Song B"},
                        {"artists", QJsonArray{QJsonObject{{"name", "Artist B"}}}}}};
        catalog.replaceSongs(songs);
        discovery.styleSongs()->replaceSongs(songs);
        SongCatalogActions actions(catalog, playback);
        actions.setQueueContext("Fixture catalog", "fixture/catalog");
        SearchModel search(api);
        search.attachPlayback(playback);
        SearchModelTestAccess::populate(search, catalog.tracks());
        connect(&search, &SearchModel::tracksPlayRequested, &playback,
                &PlaybackService::playFromSelection);
        ThemeStore theme(settings);
        theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Search
import EchoMusic.Playback
import EchoMusic.Discovery
import EchoMusic.Theme
Window {
 id: host; width: 1100; height: 780; visible: true; color: Theme.main
 required property SongCatalogModel catalog
 required property SongCatalogActions actions
 required property SearchModel search
 required property DiscoveryService discovery
 required property PlaybackService playback
 required property real zoom
 property bool searchMode: false
 Item {
  width: host.width / host.zoom; height: host.height / host.zoom
  scale: host.zoom; transformOrigin: Item.TopLeft
  SongCatalogView {
   anchors.fill: parent; anchors.bottomMargin: 110
   visible: !host.searchMode; collection: host.catalog; actions: host.actions
   headerComponent: Component { Item { implicitHeight: 0 } }
  }
  SearchPage {
   anchors.fill: parent; anchors.bottomMargin: 110
   visible: host.searchMode; pageActive: visible; service: host.search; viewportWidth: width
  }
  HomeSongCard {
   id: home; objectName: "homeFixture"; anchors.bottom: parent.bottom
   width: 400; service: host.discovery; entry: host.catalog.sourceItem(0)
  }
 }
 PlaybackQueueDrawer { objectName: "queueFixture"; playback: host.playback; uiScale: host.zoom }
})",
                          QUrl("qrc:/playback-buttons.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"catalog", QVariant::fromValue(&catalog)},
                                                   {"actions", QVariant::fromValue(&actions)},
                                                   {"search", QVariant::fromValue(&search)},
                                                   {"discovery", QVariant::fromValue(&discovery)},
                                                   {"playback", QVariant::fromValue(&playback)},
                                                   {"zoom", zoom}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window && Test::prepareNativeInput(window));
        const auto click = [&](QQuickItem *item) {
            const auto point = item->mapToScene({item->width() / 2, item->height() / 2}).toPoint();
            Test::moveNativePointer(window, point);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        };
        const auto captures = qEnvironmentVariable("ECHO_PLAYBACK_BUTTON_CAPTURES");
        if (!captures.isEmpty())
            QVERIFY(QDir().mkpath(captures));
        for (bool searchMode : {false, true}) {
            root->setProperty("searchMode", searchMode);
            QQuickItem *row = nullptr;
            QTRY_VERIFY(row = find(window->contentItem(), "songDelegate", "2"));
            Test::moveNativePointer(window, row->mapToScene({100, 30}).toPoint());
            QQuickItem *button = nullptr;
            QTRY_VERIFY(button = find(row, "songIndexPlay"));
            click(button);
            QTRY_VERIFY(playback.loading());
            QCOMPARE(playback.trackId(), QString("2"));
            QTRY_VERIFY(find(button, "playbackButtonSpinner"));
            QVERIFY(!button->isEnabled());
            const auto sequence = PlaybackServiceTestAccess::sequence(playback);
            actions.playOrToggle("2");
            search.playSong("2");
            playback.playViewedIndex(playback.queue()->currentIndex());
            QCOMPARE(PlaybackServiceTestAccess::sequence(playback), sequence);
            QVERIFY(PlaybackServiceTestAccess::autoplay(playback));
            const auto queueSize = playback.queue()->rowCount();
            Test::moveNativePointer(window, {window->width() - 4, window->height() - 4});
            QVERIFY(button->isVisible());
            const auto frame = window->grabWindow();
            QVERIFY(!frame.isNull());
            if (!captures.isEmpty())
                QVERIFY(frame.save(
                    captures + QString("/%1-%2-list%3.png").arg(dark).arg(zoom).arg(searchMode)));
            QPointer<QObject> spinner = find(button, "playbackButtonSpinner");
            PlaybackServiceTestAccess::finish(playback);
            QTRY_VERIFY(spinner.isNull());
            QVERIFY(button->isEnabled());
            QTRY_COMPARE(button->property("glyph").toString(),
                         playback.playing() ? QString("pause") : QString("play"));
            QCOMPARE(playback.queue()->rowCount(), queueSize);
            playback.stop();
        }
        auto *home = find(window->contentItem(), "homeFixture");
        QVERIFY(home);
        Test::moveNativePointer(window, home->mapToScene({20, 20}).toPoint());
        QQuickItem *homePlay = nullptr;
        QTRY_VERIFY(homePlay = find(home, "homeSongPlay"));
        QCOMPARE(homePlay->width() - homePlay->property("leftPadding").toReal() -
                     homePlay->property("rightPadding").toReal(),
                 14.);
        Test::moveNativePointer(
            window,
            homePlay->mapToScene({homePlay->width() / 2, homePlay->height() / 2}).toPoint());
        QTRY_COMPARE(homePlay->property("visualScale").toReal(), 1.1);
        QCOMPARE(homePlay->property("foreground").value<QColor>(),
                 theme.property("accentText").value<QColor>());
        click(homePlay);
        QTRY_COMPARE(playback.trackId(), QString("1"));
        QTRY_VERIFY(find(homePlay, "playbackButtonSpinner"));
        PlaybackServiceTestAccess::finish(playback);
        Test::moveNativePointer(window, {window->width() - 4, 4});
        QTRY_VERIFY(!homePlay->property("hovered").toBool());
        QTRY_COMPARE(homePlay->property("foreground").value<QColor>(),
                     theme.property("accentText").value<QColor>());
        QTRY_COMPARE(homePlay->property("playing").toBool(), playback.playing());
        if (!captures.isEmpty())
            QVERIFY(window->grabWindow().save(
                captures + QString("/%1-%2-home-playing.png").arg(dark).arg(zoom)));
        playback.stop();
        // Start another real request before exercising resource retirement.
        playback.play();
        QTRY_VERIFY(find(homePlay, "playbackButtonSpinner"));
        QVERIFY(!homePlay->isEnabled());
        Test::moveNativePointer(window, {window->width() - 4, 4});
        QVERIFY(homePlay->isVisible());
        QPointer<QObject> homeSpinner = find(homePlay, "playbackButtonSpinner");
        home->setProperty("resourceActive", false);
        QTRY_VERIFY(homeSpinner.isNull());
        home->setProperty("resourceActive", true);
        QTRY_VERIFY(find(homePlay, "playbackButtonSpinner"));
        QPointer<QObject> hiddenSpinner = find(homePlay, "playbackButtonSpinner");
        window->hide();
        QTRY_VERIFY(hiddenSpinner.isNull());
        window->show();
        QVERIFY(Test::prepareNativeInput(window));
        QTRY_VERIFY(find(homePlay, "playbackButtonSpinner"));
        PlaybackServiceTestAccess::finish(playback, true);
        QTRY_VERIFY(!find(homePlay, "playbackButtonSpinner"));
        QVERIFY(homePlay->isEnabled());
        auto *drawer = root->findChild<QObject *>("queueFixture");
        QVERIFY(drawer && QMetaObject::invokeMethod(drawer, "open"));
        QTRY_VERIFY(drawer->property("opened").toBool());
        for (const auto &name : {"queueLocateButton", "queueCloseButton"}) {
            auto *tool = find(window->contentItem(), name);
            QVERIFY(tool);
            Test::moveNativePointer(
                window, tool->mapToScene({tool->width() / 2, tool->height() / 2}).toPoint());
            QTRY_COMPARE(tool->property("visualScale").toReal(), 1.1);
            QCOMPARE(tool->property("foreground").value<QColor>(),
                     theme.property("floatingAccentText").value<QColor>());
        }
        QQuickItem *queuePlay = nullptr;
        QTRY_VERIFY(queuePlay = find(window->contentItem(), "queuePlayButton"));
        QCOMPARE(queuePlay->width() - queuePlay->property("leftPadding").toReal() -
                     queuePlay->property("rightPadding").toReal(),
                 14.);
        auto *queueRow = queuePlay->parentItem()->parentItem()->parentItem();
        Test::moveNativePointer(
            window,
            queuePlay->mapToScene({queuePlay->width() / 2, queuePlay->height() / 2}).toPoint());
        QTRY_COMPARE(queuePlay->property("visualScale").toReal(), 1.1);
        QQuickItem *remove = nullptr;
        QTRY_VERIFY(remove = find(queueRow, "queueRemoveButton"));
        Test::moveNativePointer(
            window, remove->mapToScene({remove->width() / 2, remove->height() / 2}).toPoint());
        QTRY_COMPARE(remove->property("visualScale").toReal(), 1.1);
        QCOMPARE(remove->property("foreground").value<QColor>(),
                 theme.property("floatingAccentText").value<QColor>());
        click(queuePlay);
        QTRY_VERIFY(find(queuePlay, "playbackButtonSpinner"));
        QVERIFY(!queuePlay->isEnabled());
        auto *artist = find(queueRow, "queueArtistText");
        QVERIFY(artist);
        QCOMPARE(artist->property("color").value<QColor>(),
                 theme.property("floatingAccentText").value<QColor>());
        QPointer<QObject> queueSpinner = find(queuePlay, "playbackButtonSpinner");
        playback.stop();
        QTRY_VERIFY(queueSpinner.isNull());
        QVERIFY(queuePlay->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
        QCOMPARE(warnings.size(), 0);
        root.reset();
        search.shutdown();
        discovery.shutdown();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
};
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    configureUiTypography();
    PlaybackButtonUiTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "PlaybackButtonUiTest.moc"
