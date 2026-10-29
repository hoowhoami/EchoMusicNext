#include "SongCatalogActions.h"
#include "SongCommands.h"
#include "SearchModel.h"
#include "SearchResultModel.h"
#include "ThemeStore.h"
#include "UiTypography.h"
#include "NativeWindow.h"
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace EchoMusic;
namespace EchoMusic
{
class SearchModelTestAccess
{
public:
    static void populate(SearchModel &model, const PlaybackTracks &tracks, bool lyric)
    {
        model.cancelRequests();
        model.beginResetModel();
        model.m_kind = lyric ? SearchModel::Kind::Lyric : SearchModel::Kind::Song;
        model.m_keyword = QStringLiteral("播放状态");
        auto &bucket = model.active();
        bucket = {};
        for (const auto &track : tracks)
            bucket.rows.append({track.title,
                                track.artist,
                                track.album,
                                {},
                                "0:20",
                                QStringLiteral("匹配歌词 Lyric"),
                                track.id,
                                track.durationMs,
                                track,
                                false});
        bucket.state = SearchModel::State::Ready;
        bucket.loaded = true;
        bucket.total = tracks.size();
        model.endResetModel();
        emit model.changed();
    }
};
} // namespace EchoMusic
namespace
{
QQuickItem *find(QQuickItem *root, const QString &name, const QString &track = {})
{
    if (root->isVisible() && root->objectName() == name &&
        (track.isEmpty() || root->property("resourceId").toString() == track))
        return root;
    for (auto *child : root->childItems())
        if (auto *item = find(child, name, track))
            return item;
    return nullptr;
}
bool containsGlyphColor(const QImage &frame, QQuickItem *item, const QColor &color)
{
    const auto scene = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    const qreal ratio = frame.devicePixelRatio();
    const auto pixels =
        QRectF(scene.x() * ratio, scene.y() * ratio, scene.width() * ratio, scene.height() * ratio)
            .toAlignedRect()
            .intersected(frame.rect());
    int matches = 0;
    for (int y = pixels.top(); y <= pixels.bottom(); ++y)
        for (int x = pixels.left(); x <= pixels.right(); ++x) {
            const auto p = frame.pixelColor(x, y);
            if (qAbs(p.red() - color.red()) < 14 && qAbs(p.green() - color.green()) < 14 &&
                qAbs(p.blue() - color.blue()) < 14 && ++matches >= 3)
                return true;
        }
    return false;
}
} // namespace
class SongPlaybackUiTest final : public QObject
{
    Q_OBJECT
private slots:
    void batchDrawerSelectionLayoutAndLifetime()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, settings, nullptr, directory.filePath("playback.sqlite"));
        SongCatalogModel catalog;
        QJsonArray songs;
        for (int i = 0; i < 200; ++i)
            songs.append(
                QJsonObject{{"id", QString::number(i + 1)},
                            {"title", QString("歌曲 Song %1 长标题布局测试").arg(i + 1)},
                            {"artists", QJsonArray{QJsonObject{{"name", "歌手 Artist"}}}}});
        catalog.replaceSongs(songs);
        SongCatalogActions actions(catalog, playback);
        ThemeStore theme(settings);
        theme.setMotionEnabled(false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import EchoMusic.Music
            import EchoMusic.Controls
            import EchoMusic.Theme
            Window {
                id: window
                required property SongCatalogModel catalog
                required property SongCatalogActions actions
                property real uiScale: 1
                property alias drawer: owner.item
                function openBatch(): void { owner.open(); }
                width: 1100; height: 800; visible: true; color: Theme.main
                EchoBackdropRoot { anchors.fill: parent }
                Item {
                    id: host
                    anchors.fill: parent
                    EchoPopupLoader {
                        id: owner
                        hostItem: host
                        sourceComponent: SongCatalogBatchDrawer {
                            collection: window.catalog
                            actions: window.actions
                            uiScale: window.uiScale
                            enableRemove: true
                        }
                    }
                }
            }
        )",
                          QUrl());
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"catalog", QVariant::fromValue(&catalog)},
                                                   {"actions", QVariant::fromValue(&actions)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        const auto captures = qEnvironmentVariable("ECHO_PANEL_CAPTURES");
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (const qreal scale : {1., 1.25}) {
                root->setProperty("uiScale", scale);
                for (const int width : {1100, 390}) {
                    window->resize(width, 800);
                    QVERIFY(Test::prepareNativeInput(window));
                    QTest::qWait(80);
                    QVERIFY(!root->property("drawer").value<QObject *>());
                    QVERIFY(QMetaObject::invokeMethod(root.data(), "openBatch"));
                    QPointer<QObject> drawer = root->property("drawer").value<QObject *>();
                    QVERIFY(drawer);
                    QTRY_VERIFY(drawer->property("opened").toBool());
                    auto *surface = drawer->property("background").value<QQuickItem *>();
                    QVERIFY(surface);
                    QCOMPARE(QQmlProperty(surface, "border.width").read().toReal(), 1.);
                    QPointer<QQuickItem> roundedMask;
                    QTRY_VERIFY(
                        (roundedMask = drawer->findChild<QQuickItem *>("popupContentRoundedMask")));
                    auto *list = find(window->contentItem(), "discoveryBatchSongs");
                    auto *selection = find(window->contentItem(), "batchSelectAll");
                    auto *footer = find(window->contentItem(), "batchFooter");
                    QVERIFY(list && selection && footer);
                    QTRY_VERIFY(find(window->contentItem(), "batchSongRow"));
                    QCOMPARE(catalog.selectedCount(), 0);
                    const auto point = selection->mapToScene({9, 9}).toPoint();
                    qInfo() << "Batch viewport/scale/point:" << window->size() << scale << point
                            << drawer->property("x") << drawer->property("width")
                            << drawer->property("parent").value<QQuickItem *>()->size();
                    QVERIFY(window->contentItem()->boundingRect().contains(point));
                    Test::moveNativePointer(window, point);
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
                    QTRY_COMPARE(catalog.selectedCount(), 200);
                    auto *row = find(window->contentItem(), "batchSongRow");
                    auto *check = find(row, "batchSongCheck");
                    QVERIFY(check);
                    const auto firstCheck = check->mapToScene({9, 9}).toPoint();
                    Test::moveNativePointer(window, firstCheck);
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, firstCheck);
                    QTRY_COMPARE(catalog.selectedCount(), 199);
                    QCOMPARE(selection->property("checkState").toInt(), int(Qt::PartiallyChecked));
                    QVERIFY(qAbs(check->mapToScene({0, 0}).x() -
                                 selection->mapToScene({0, 0}).x()) < .6);
                    const auto footerRect = footer->mapRectToScene(footer->boundingRect());
                    const auto listRect = list->mapRectToScene(list->boundingRect());
                    QVERIFY(listRect.bottom() <= footerRect.top() + .5);
                    const auto footerButtons = footer->findChildren<QQuickItem *>();
                    for (auto *button : footerButtons) {
                        if (!button->isVisible() || !button->property("glyph").isValid())
                            continue;
                        const auto rect = button->mapRectToScene(button->boundingRect());
                        QVERIFY2(footerRect.adjusted(-.5, -.5, .5, .5).contains(rect),
                                 qPrintable(button->property("text").toString()));
                    }
                    int instantiated = 0;
                    for (auto *item : list->findChildren<QQuickItem *>())
                        instantiated += item->objectName() == "batchSongRow";
                    QVERIFY(instantiated < 40);
                    QTest::qWait(80);
                    QVERIFY(drawer && drawer->property("opened").toBool());
                    if (!captures.isEmpty()) {
                        QVERIFY(QDir().mkpath(captures));
                        QVERIFY(window->grabWindow().save(
                            captures +
                            QString("/batch-%1-%2-%3.png").arg(int(mode)).arg(scale).arg(width)));
                    }
                    if (mode == ThemeStore::Light && scale == 1. && width == 1100) {
                        auto *remove = find(window->contentItem(), "batchRemove");
                        QVERIFY(remove);
                        const auto removePoint =
                            remove->mapToScene({remove->width() / 2, remove->height() / 2})
                                .toPoint();
                        Test::moveNativePointer(window, removePoint);
                        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, removePoint);
                        auto *owner = drawer->findChild<QObject *>("batchRemoveConfirmOwner");
                        QVERIFY(owner);
                        QPointer<QObject> confirmation;
                        QTRY_VERIFY((confirmation = owner->property("item").value<QObject *>()));
                        QTRY_VERIFY(confirmation->property("opened").toBool());
                        QVERIFY(QMetaObject::invokeMethod(confirmation, "close"));
                        QTRY_VERIFY(confirmation.isNull());
                        QVERIFY(drawer && drawer->property("opened").toBool());
                    }
                    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
                    QTRY_VERIFY(drawer.isNull());
                    QVERIFY(roundedMask.isNull());
                }
            }
        }
        root.reset();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    }
    void currentRowFollowsTheNativePlayer_data()
    {
        QTest::addColumn<bool>("searchMode");
        QTest::addColumn<bool>("lyric");
        QTest::addColumn<bool>("dark");
        QTest::addColumn<qreal>("scale");
        for (const int mode : {0, 1, 2})
            for (const bool dark : {false, true})
                for (const qreal scale : {1.0, 1.25})
                    QTest::newRow(qPrintable(QString("%1-%2-%3")
                                                 .arg(mode == 0   ? "catalog"
                                                      : mode == 1 ? "search"
                                                                  : "lyric",
                                                      dark ? "dark" : "light")
                                                 .arg(scale)))
                        << (mode != 0) << (mode == 2) << dark << scale;
    }
    void currentRowFollowsTheNativePlayer()
    {
        QFETCH(bool, searchMode);
        QFETCH(bool, lyric);
        QFETCH(bool, dark);
        QFETCH(qreal, scale);
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, settings, nullptr, directory.filePath("playback.sqlite"));
        playback.setVolume(0);
        const auto coverPath = directory.filePath("cover.png");
        QImage coverArt(120, 120, QImage::Format_RGB32);
        coverArt.fill(QColor("#14a6b8"));
        QVERIFY(coverArt.save(coverPath));
        PlaybackTracks tracks;
        QJsonArray songs;
        for (int i = 0; i < 2; ++i) {
            const auto path = directory.filePath(QString("silence-%1.wav").arg(i));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QDataStream out(&file);
            out.setByteOrder(QDataStream::LittleEndian);
            constexpr quint32 bytes = 48000 * 20 * 4;
            out.writeRawData("RIFF", 4);
            out << quint32(bytes + 36);
            out.writeRawData("WAVEfmt ", 8);
            out << quint32(16) << quint16(1) << quint16(2) << quint32(48000) << quint32(192000)
                << quint16(4) << quint16(16);
            out.writeRawData("data", 4);
            out << bytes;
            const auto silence = QByteArray(bytes, 0);
            QCOMPARE(out.writeRawData(silence.constData(), silence.size()), silence.size());
            file.close();
            PlaybackTrack track;
            track.id = QString::number(i + 1);
            track.localPath = path;
            track.title = i ? QStringLiteral("第二首 Other") : QStringLiteral("当前播放 中文 Song");
            track.cover = QUrl::fromLocalFile(coverPath).toString();
            track.artist = QStringLiteral("歌手 Artist");
            track.album = i ? QString() : QStringLiteral("专辑 Album");
            track.albumId = i ? 0 : 12345;
            track.catalogMetadata.insert(
                "artist_refs", QJsonArray{QJsonObject{{"id", "456"}, {"name", track.artist}},
                                          QJsonObject{{"name", QStringLiteral("无ID Guest")}}});
            track.durationMs = 20000;
            tracks.append(track);
            songs.append(QJsonObject{{"id", track.id},
                                     {"hash", QString(32, u'a')},
                                     {"title", track.title},
                                     {"cover_url", track.cover},
                                     {"album_title", track.album},
                                     {"album_id", QString::number(track.albumId)},
                                     {"duration_ms", 20000},
                                     {"privilege", 0},
                                     {"artists", track.catalogMetadata.value("artist_refs")}});
        }
        SongCatalogModel catalog;
        catalog.replaceSongs(songs);
        SongCatalogActions actions(catalog, playback);
        SearchModel search(api);
        search.attachPlayback(playback);
        SearchModelTestAccess::populate(search, tracks, lyric);
        connect(&search, &SearchModel::tracksPlayRequested, &playback,
                &PlaybackService::playFromSelection);
        ThemeStore theme(settings);
        theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.Music
import EchoMusic.Search
import EchoMusic.Theme
Window {
 id: window
 required property SongCatalogModel catalog
 required property SongCatalogActions actions
 required property SearchModel search
 required property bool searchMode
 required property real uiScale
 visible: true; width: 1100; height: 800
 color: Theme.main
 Item {
  width: window.width / window.uiScale; height: window.height / window.uiScale
  scale: window.uiScale; transformOrigin: Item.TopLeft
  SongCatalogView {
   anchors.fill: parent; visible: !window.searchMode; resourceActive: visible
   collection: window.catalog; actions: window.actions
   headerComponent: Component { SongDetailHeader { heading: "播放状态" } }
  }
  SearchPage {
   anchors.fill: parent; visible: window.searchMode; pageActive: visible
   service: window.search; viewportWidth: width
  }
 }
})",
                          QUrl("qrc:/song-playback-state-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"catalog", QVariant::fromValue(&catalog)},
                                                   {"actions", QVariant::fromValue(&actions)},
                                                   {"search", QVariant::fromValue(&search)},
                                                   {"searchMode", searchMode},
                                                   {"uiScale", scale}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        Test::moveNativePointer(window, QPoint(window->width() - 4, window->height() - 4));
        playback.playTracks(tracks, 0, "Original queue", "song-row-test");
        QTRY_VERIFY2_WITH_TIMEOUT(playback.playing() && playback.positionMs() > 500,
                                  qPrintable(QString("playing=%1 loading=%2 error=%3")
                                                 .arg(playback.playing())
                                                 .arg(playback.loading())
                                                 .arg(playback.errorMessage())),
                                  15000);
        auto *row = find(window->contentItem(), "songDelegate", "1");
        QTRY_VERIFY((row = find(window->contentItem(), "songDelegate", "1")));
        const auto clickMetadata = [&](QQuickItem *item) {
            QVERIFY(item);
            const auto point = item->mapToScene({item->width() / 2, item->height() / 2}).toPoint();
            Test::moveNativePointer(window, point);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        };
        QSignalSpy artists(playback.songCommands(), &SongCommands::artistRequested);
        QSignalSpy albums(playback.songCommands(), &SongCommands::albumRequested);
        auto *firstArtist = find(row, "artistLink-0");
        auto *guestArtist = find(row, "artistLink-1");
        QVERIFY(firstArtist && guestArtist);
        QVERIFY(firstArtist->isEnabled());
        QVERIFY(!guestArtist->isEnabled());
        const auto queueSize = playback.queue()->rowCount();
        clickMetadata(firstArtist);
        QCOMPARE(artists.size(), 1);
        QCOMPARE(artists.first().first().toString(), QString("456"));
        firstArtist->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QCOMPARE(artists.size(), 2);
        clickMetadata(guestArtist);
        QCOMPARE(artists.size(), 2);
        auto *album = find(row, "songAlbumLink");
        QVERIFY(album);
        QCOMPARE(album->isEnabled(), !lyric);
        clickMetadata(album);
        QCOMPARE(albums.size(), lyric ? 0 : 1);
        if (!lyric)
            QCOMPARE(albums.first().first().toString(), QString("12345"));
        QCOMPARE(playback.queue()->rowCount(), queueSize);
        QVERIFY(playback.playing());
        auto *otherRow = find(window->contentItem(), "songDelegate", "2");
        QVERIFY(otherRow);
        auto *missingAlbum = find(otherRow, "songAlbumText");
        QVERIFY(missingAlbum);
        QCOMPARE(missingAlbum->property("text").toString(),
                 lyric ? QStringLiteral("匹配歌词 Lyric") : QString("-"));
        QVERIFY(!find(otherRow, "songAlbumLink")->isEnabled());
        Test::moveNativePointer(window, QPoint(window->width() - 4, window->height() - 4));
        QTRY_VERIFY(!row->property("hovered").toBool());
        auto *play = find(row, "songIndexPlay");
        QVERIFY(play);
        QTRY_VERIFY(row->property("active").toBool());
        QVERIFY(row->property("highlighted").toBool());
        QVERIFY(!row->property("hovered").toBool());
        Test::moveNativePointer(window, row->mapToScene({row->width() / 2, 30}).toPoint());
        QTRY_VERIFY(row->property("hovered").toBool());
        for (auto *item : row->findChildren<QQuickItem *>())
            QVERIFY2(item->property("glyph").toString() != "dots",
                     "Song rows expose their context actions through right click and keyboard");
        Test::moveNativePointer(window, QPoint(window->width() - 4, window->height() - 4));
        QTRY_VERIFY(!row->property("hovered").toBool());
        // Transparent row actions keep their native button identity and tab
        // target, but not an icon tree. Keyboard focus recreates it immediately.
        auto *mv = find(row, "songMvAction");
        QVERIFY(mv);
        QTRY_VERIFY(!mv->property("visualContentActive").toBool());
        QVERIFY(!find(mv, "iconImage"));
        mv->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(mv->hasActiveFocus());
        QTRY_VERIFY(mv->property("visualContentActive").toBool());
        QTRY_VERIFY(find(mv, "iconImage"));
        QPointer<QObject> retiredIcon = find(mv, "iconImage");
        otherRow->forceActiveFocus(Qt::MouseFocusReason);
        QTRY_VERIFY(!mv->hasActiveFocus());
        QTRY_VERIFY(!mv->property("visualContentActive").toBool());
        QTRY_VERIFY(retiredIcon.isNull());
        auto *inactivePlay = otherRow->findChild<QQuickItem *>("songIndexPlay");
        QVERIFY(inactivePlay);
        QVERIFY(!inactivePlay->property("visualContentActive").toBool());
        QVERIFY(play->isVisible());
        QCOMPARE(play->property("glyph").toString(), QString("pause"));
        QCOMPARE(play->property("text").toString(), QString("暂停"));
        const auto accent = theme.property("accentText").value<QColor>();
        window->update();
        QTest::qWait(100);
        const auto frame = window->grabWindow();
        QVERIFY(!frame.isNull());
        const auto capture = qEnvironmentVariable("ECHO_SONG_PLAYBACK_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(
                frame.save(capture + '/' + QString::fromLatin1(QTest::currentDataTag()) + ".png"));
        }
        for (const auto &name :
             {"songTitleText", "songArtistText", "songAlbumText", "songDurationText"}) {
            auto *text = find(row, QString::fromLatin1(name));
            QVERIFY(text);
            QCOMPARE(text->property("color").value<QColor>(), accent);
            if (!containsGlyphColor(frame, text, accent))
                qWarning() << name << text->property("text")
                           << text->mapRectToScene(QRectF(0, 0, text->width(), text->height()))
                           << frame.size() << frame.devicePixelRatio();
            QVERIFY2(containsGlyphColor(frame, text, accent), name);
        }
        const auto source = playback.sourceRevision();
        const auto position = playback.positionMs();
        const auto click = [&] {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              play->mapToScene({play->width() / 2, play->height() / 2}).toPoint());
        };
        click();
        QTRY_VERIFY(!playback.playing());
        QCOMPARE(play->property("glyph").toString(), QString("play"));
        QVERIFY(row->property("active").toBool());
        QCOMPARE(playback.sourceRevision(), source);
        QVERIFY(playback.positionMs() >= position - 50);
        click();
        QTRY_VERIFY(playback.playing());
        QCOMPARE(play->property("glyph").toString(), QString("pause"));
        QCOMPARE(playback.sourceRevision(), source);
        QCOMPARE(playback.queue()->rowCount(), 2);
        QCOMPARE(playback.queueTitle(), QString("Original queue"));
        // A real engine track change updates both bound row state and colors.
        playback.next();
        QTRY_COMPARE_WITH_TIMEOUT(playback.trackId(), QString("2"), 10000);
        QTRY_VERIFY(playback.playing());
        QTRY_VERIFY(!row->property("active").toBool());
        auto *next = find(window->contentItem(), "songDelegate", "2");
        QVERIFY(next);
        QTRY_VERIFY(next->property("active").toBool());
        QCOMPARE(find(next, "songIndexPlay")->property("glyph").toString(), QString("pause"));
        // Batch selection remains an explicit command, independent of current-row emphasis.
        QCOMPARE(catalog.selectedCount(), 0);
        root.reset();
        search.shutdown();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    }
};
int main(int argc, char **argv)
{
    QQuickStyle::setStyle("Basic");
    QGuiApplication app(argc, argv);
    configureUiTypography();
    SongPlaybackUiTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "SongPlaybackUiTest.moc"
