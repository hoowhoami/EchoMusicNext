#include "PlaybackDiscussion.h"
#include "DetailsService.h"
#include "ShellController.h"
#include "ShellSettings.h"
#include "ThemeStore.h"
#include "NetworkPreferences.h"
#include "RenderedInk.h"
#include "NativeWindow.h"
#include <QDir>
#include <QJsonDocument>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static void track(PlaybackService &p, bool next = false)
    {
        ++p.m_sequence;
        p.m_track =
            PlaybackTrack::fromDomain({{"id", next ? "266705785" : "32042818"},
                                       {"hash", next ? "AA5B528D84DDDC91C702FBBD1932B1C8"
                                                     : "105A2705BEEE36A8BED029A8E1A6311D"},
                                       {"title", next ? "春庭雪" : "龙战骑士"},
                                       {"artists", QJsonArray{QJsonObject{{"name", "周杰伦"}}}}});
        emit p.changed();
    }
    static void playing(PlaybackService &p, bool value)
    {
        p.m_playing = value;
        emit p.changed();
    }
};
class DetailsTestAccess
{
public:
    static QString resource(CommentsController &c)
    {
        return c.m_id;
    }
    static void loading(CommentsController &c, bool value)
    {
        if (value)
            c.m_pending.insert(33333, {CommentsController::Operation::List,
                                       "all",
                                       {},
                                       c.m_epoch,
                                       0,
                                       c.m_api.sessionRevision(),
                                       1});
        else
            c.m_pending.remove(33333);
        emit c.changed();
    }
    static void readFailure(CommentsController &c)
    {
        c.fail(33333, QJsonDocument(QJsonObject{{"message", "offline read failure"}}).toJson());
    }
    static void seed(CommentsController &c)
    {
        auto &s = c.stream("all");
        s.error.clear();
        for (int i = 1; i <= 30; ++i)
            s.rows.append(
                QJsonObject{{"id", QString::number(i)},
                            {"content", QString("评论 中文 Artist\n").repeated(1 + i % 4)},
                            {"user_name", "歌迷 Artist"},
                            {"user_id", "77"},
                            {"likes", QJsonObject{{"count", 4344}}},
                            {"created_at", "2026-10-19 12:30:00"}});
        s.singerRows = QJsonArray{QJsonObject{{"id", "singer-1"},
                                              {"content", "歌手说 · 原生评论抽屉"},
                                              {"user_name", "歌手 Artist"},
                                              {"user_id", "77"},
                                              {"reply_count", 8},
                                              {"likes", QJsonObject{{"count", 4344}}}}};
        s.total = 30;
        s.loaded = true;
        s.more = false;
        c.rebuild();
        emit c.changed();
    }
    static void barrageResponse(BarrageController &c, const QJsonArray &rows, bool preserve)
    {
        c.m_read = 88888;
        c.m_session = c.m_api.sessionRevision();
        c.m_preserve = preserve;
        c.accept(88888, QJsonDocument(QJsonObject{{"comments", rows}}).toJson());
    }
    static void verification(BarrageController &c)
    {
        c.m_write = 77777;
        c.m_session = c.m_api.sessionRevision();
        c.m_sentDraft = c.m_draft;
        emit c.changed();
        emit c.verificationStarted();
    }
    static void verificationFailure(BarrageController &c)
    {
        c.fail(77777, QJsonDocument(QJsonObject{{"message", "离线验证取消"}}).toJson());
    }
    static int poolSize(BarrageController &c)
    {
        return c.m_pool.size();
    }
    static void barrage(BarrageController &c)
    {
        c.m_pool =
            QJsonArray{QJsonObject{{"content", "弹幕 中文 <b>Artist</b>\n🎵"}, {"user_id", "77"}}};
        emit c.changed();
    }
};
} // namespace EchoMusic
namespace
{
QQuickItem *visual(QQuickItem *item, const QString &name)
{
    if (!item || !item->isVisible())
        return nullptr;
    if (item->objectName() == name)
        return item;
    for (auto *child : item->childItems())
        if (auto *found = visual(child, name))
            return found;
    return nullptr;
}
void click(QQuickWindow *window, QQuickItem *item)
{
    QVERIFY(item);
    if (item->window())
        window = item->window();
    const bool ready = Test::prepareNativeInput(window);
    if (!ready)
        for (auto *candidate : QGuiApplication::allWindows())
            qInfo() << "input_window" << candidate->metaObject()->className()
                    << candidate->visibility() << candidate->isActive();
    QVERIFY(ready);
    QPointer<QQuickItem> target = item;
    QSignalSpy frame(window, &QQuickWindow::frameSwapped);
    window->update();
    QTRY_VERIFY(!frame.isEmpty());
    QVERIFY(target);
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    Test::moveNativePointer(window, point);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
}
void activate(QQuickWindow *window, const QString &id)
{
    if (auto *button = visual(window->contentItem(), "playerbar-action-" + id)) {
        click(window, button);
        return;
    }
    click(window, visual(window->contentItem(), "playerbarMoreAction"));
    QQuickItem *button = nullptr;
    QTRY_VERIFY(button = visual(window->contentItem(), "playerbarMenuAction-host/" + id));
    click(window, button);
}
QJsonObject snapshot()
{
    QJsonObject result;
    const auto name = QStringLiteral("playback-discussion-snapshot");
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(QDir::homePath() +
                           "/Library/Application Support/echo-music/echomusic.sqlite");
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (db.open()) {
            db.transaction();
            QSqlQuery q(db);
            q.exec("SELECT key,value_json FROM app_kv WHERE key IN ('pinia:user','pinia:device')");
            while (q.next())
                result.insert(q.value(0).toString(),
                              QJsonDocument::fromJson(q.value(1).toByteArray()).object());
            q.finish();
            db.rollback();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return result;
}
} // namespace
class PlaybackDiscussionTest : public QObject
{
    Q_OBJECT
private slots:
    void barragePoolAndResourceIsolation()
    {
        ApiClient api(DeviceIdentity::temporary());
        BarrageController controller(api, nullptr, false, nullptr, true);
        controller.setResource("105A2705BEEE36A8BED029A8E1A6311D", "歌曲");
        controller.setPresented(true);
        controller.setEnabled(true);
        const auto row = [](const QString &user, const QString &text) {
            return QJsonObject{{"content", text}, {"user_id", user}};
        };
        DetailsTestAccess::barrageResponse(
            controller, {row("1", "same"), row("2", "same"), row("3", "other")}, false);
        QCOMPARE(controller.next().value("user_id").toString(), QString("1"));
        DetailsTestAccess::barrageResponse(
            controller, {row("1", "same"), row("3", "other"), row("2", "same")}, true);
        QCOMPARE(controller.next().value("user_id").toString(), QString("2"));
        DetailsTestAccess::barrageResponse(controller, {}, true);
        QVERIFY(!controller.empty());
        QJsonArray rows;
        for (int i = 0; i < 20; ++i)
            rows.append(row("1", "  "));
        for (int i = 0; i < 120; ++i)
            rows.append(row(QString::number(i + 1), "content"));
        DetailsTestAccess::barrageResponse(controller, rows, false);
        QCOMPARE(DetailsTestAccess::poolSize(controller), 100);
        controller.setDraft("旧歌曲草稿");
        controller.setResource("AA5B528D84DDDC91C702FBBD1932B1C8", "下一首");
        QVERIFY(controller.empty());
        QVERIFY(controller.draft().isEmpty());
        controller.shutdown();
        api.shutdown();
    }
    void productionEntry_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<bool>("zoomed");
        for (bool dark : {false, true})
            for (bool zoomed : {false, true})
                QTest::newRow(qPrintable(
                    QString("%1-%2").arg(dark ? "dark" : "light").arg(zoomed ? 125 : 100)))
                    << dark << zoomed;
    }
    void productionEntry()
    {
        QFETCH(bool, dark);
        QFETCH(bool, zoomed);
        QTest::failOnWarning(QRegularExpression(
            ".*(TypeError|ReferenceError|TableView::forceLayout|recursive rearrange).*"));
        const bool live = qEnvironmentVariableIsSet("ECHO_PLAYBACK_DISCUSSION_LIVE");
        const auto auth = live ? snapshot() : QJsonObject{};
        const auto user = auth.value("pinia:user").toObject().value("info").toObject();
        const auto device = auth.value("pinia:device").toObject().value("info").toObject();
        DeviceIdentity identity = DeviceIdentity::temporary();
        if (live)
            identity = {device.value("mid").toVariant().toString(),
                        device.value("dfid").toString(),
                        device.value("guid").toString(),
                        device.value("mac").toString(),
                        "EchoMusicNext",
                        device.value("uuid").toString(),
                        device.value("renderFingerprint").toString()};
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        prefs.setValue("playback/volume", 0.);
        ApiClient api(identity);
        if (live)
            QVERIFY(api.setAccount(user.value("userid").toVariant().toULongLong(),
                                   user.value("token").toString(), user.value("t1").toString()));
        NetworkPreferences network(prefs, api);
        if (live)
            network.installApplicationTransport();
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        PlaybackServiceTestAccess::track(playback);
        RouteController router;
        CommentAuthorService authors(api, live);
        DetailsService details(api, playback, router, live, nullptr, live ? &authors : nullptr);
        details.registerRoutes(router);
        auto *discussion = details.playbackDiscussion();
        ShellSettings settings(prefs);
        ShellController commands(prefs, router);
        commands.attachPlayback(playback);
        commands.bindBarrageSettings(settings);
        ThemeStore theme(prefs);
        theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
        if (zoomed)
            for (int i = 0; i < 5; ++i)
                settings.zoomIn();
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Window
import EchoMusic.Controls
import EchoMusic.Playback
import EchoMusic.Details
import EchoMusic.Shell
Window {
 id: window
 required property PlaybackService playback
 required property PlaybackDiscussion discussion
 required property ShellSettings settings
 required property ShellController commands
 required property RouteController router
 property Item lastAnchor: null
 visible: true; width: 1150; height: 800
 EchoPopupLoader {
  id: owner; objectName: "testPlaybackOwner"; hostItem: window.contentItem
  sourceComponent: PlaybackPage {
   id: page
   playback: window.playback; uiScale: window.settings.uiScale
   controlsComponent: Component {
    PlayerBar {
     controller: window.commands; router: window.router; uiScale: window.settings.uiScale
     lyricStyle: true; colors: page.colors
     onLastActionAnchorChanged: window.lastAnchor = lastActionAnchor
    }
   }
   discussionComponent: Component {
    PlaybackDiscussionHost {
     service: window.discussion; playback: window.playback; settings: window.settings
     resourceActive: page.discussionActive; uiScale: page.uiScale; actionAnchor: window.lastAnchor
    }
   }
  }
 }
 Connections {
  target: window.commands
  function onPlaybackPopupRequested(kind: string): void {
   const page = owner.item as PlaybackPage;
   const host = page.discussionHost as PlaybackDiscussionHost;
   if (kind === "comments") host.openComments();
   else if (kind === "barrage") host.openBarrage();
  }
 }
})",
                          QUrl("qrc:/playback-discussion-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"playback", QVariant::fromValue(&playback)},
                                                   {"discussion", QVariant::fromValue(discussion)},
                                                   {"settings", QVariant::fromValue(&settings)},
                                                   {"commands", QVariant::fromValue(&commands)},
                                                   {"router", QVariant::fromValue(&router)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        if (qEnvironmentVariableIsSet("ECHO_DISCUSSION_NARROW"))
            window->resize(660, 800);
        auto *owner = root->findChild<QObject *>("testPlaybackOwner");
        QVERIFY(owner);
        QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
        QTRY_VERIFY(owner->property("opened").toBool());
        PlaybackServiceTestAccess::track(playback);
        QVERIFY(playback.hasTrack());
        Test::moveNativePointer(window, QPoint(qMin(800, window->width() - 30), 100));
        const auto popupCount = [&] {
            int count = 0;
            for (auto *object : root->findChildren<QObject *>())
                if (object->inherits("QQuickPopup"))
                    ++count;
            return count;
        };
        const int baseline = popupCount();
        QCOMPARE(baseline, 1);
        QVERIFY(!root->findChild<QObject *>("playbackCommentDrawer"));
        activate(window, "comments");
        QPointer<QObject> drawer;
        QTRY_VERIFY(drawer = root->findChild<QObject *>("playbackCommentDrawer"));
        QTRY_COMPARE(popupCount(), baseline + 1);
        if (live)
            QTRY_VERIFY_WITH_TIMEOUT(!discussion->comments()->loading() &&
                                         discussion->comments()->model()->rowCount() > 0,
                                     30000);
        else {
            DetailsTestAccess::loading(*discussion->comments(), true);
            QPointer<QQuickItem> skeleton;
            QTRY_VERIFY(skeleton = visual(window->contentItem(), "echoSkeleton"));
            QTRY_COMPARE(skeleton->width(), 36.);
            QCOMPARE(skeleton->property("variant").toInt(), 2);
            auto *likeSkeleton = visual(window->contentItem(), "commentSkeletonLike");
            QVERIFY(likeSkeleton);
            QTRY_VERIFY(qAbs(likeSkeleton->x() + likeSkeleton->width() -
                             likeSkeleton->parentItem()->width()) < .01);
            const auto artifacts = qEnvironmentVariable("ECHO_DETAILS_ARTIFACTS");
            if (!artifacts.isEmpty()) {
                QDir().mkpath(artifacts);
                QTest::qWait(100);
                QVERIFY(window->grabWindow().save(artifacts + QString("/playback-loading-%1-%2.png")
                                                                  .arg(dark ? "dark" : "light")
                                                                  .arg(zoomed ? 125 : 100)));
            }
            QSignalSpy notices(discussion, &PlaybackDiscussion::notice);
            DetailsTestAccess::readFailure(*discussion->comments());
            QCOMPARE(notices.size(), 1);
            QTRY_VERIFY(skeleton.isNull());
            QTRY_VERIFY(visual(window->contentItem(), "commentEmpty"));
            DetailsTestAccess::seed(*discussion->comments());
        }
        QTRY_VERIFY(visual(window->contentItem(), "commentContent"));
        QCOMPARE(visual(window->contentItem(), "commentContent")
                     ->property("font")
                     .value<QFont>()
                     .pixelSize(),
                 12);
        QCOMPARE(DetailsTestAccess::resource(*discussion->comments()), QString("32042818"));
        const auto artifacts = qEnvironmentVariable("ECHO_DETAILS_ARTIFACTS");
        const auto capture = [&](const QString &kind) {
            if (artifacts.isEmpty())
                return true;
            QDir().mkpath(artifacts);
            QTest::qWait(250);
            return window->grabWindow().save(artifacts + QString("/playback-%1-%2-%3.png")
                                                             .arg(kind, dark ? "dark" : "light")
                                                             .arg(zoomed ? 125 : 100));
        };
        QVERIFY(capture("comments"));
        if (!live) {
            click(window, visual(window->contentItem(), "commentReply"));
            QQuickItem *reply = nullptr;
            QTRY_VERIFY(reply = visual(window->contentItem(), "replyDraft"));
            QVERIFY(reply->setProperty("text", "行内回复草稿 🎵"));
            QCOMPARE(discussion->comments()->replyDraft(), QString("行内回复草稿 🎵"));
            click(window, visual(window->contentItem(), "cancelFloorReply"));
            QTRY_VERIFY(!visual(window->contentItem(), "floorReplyComposer"));
        }

        if (!live) {
            PlaybackServiceTestAccess::track(playback, true);
            QCOMPARE(DetailsTestAccess::resource(*discussion->comments()), QString("32042818"));
        }
        qInfo() << "comment_drawer_before_close" << drawer->property("visible") << "native_popups"
                << popupCount();
        click(window, visual(window->contentItem(), "playbackCommentClose"));
        QTRY_VERIFY(drawer.isNull());
        QCOMPARE(popupCount(), baseline);
        if (!live) {
            activate(window, "comments");
            QTRY_VERIFY(drawer = root->findChild<QObject *>("playbackCommentDrawer"));
            QCOMPARE(DetailsTestAccess::resource(*discussion->comments()), QString("266705785"));
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(drawer.isNull());
            QCOMPARE(popupCount(), baseline);
        }
        activate(window, "barrage");
        QPointer<QObject> panel;
        QTRY_VERIFY(panel = root->findChild<QObject *>("lyricBarragePanel"));
        const auto mvConfig = settings.mvBarrageConfig();
        click(window, visual(window->contentItem(), "barrageEnabledSwitch"));
        QTRY_VERIFY(settings.lyricBarrageEnabled());
        settings.setLyricBarrageOption(ShellSettings::BarrageSpeed, 1.5);
        QCOMPARE(settings.mvBarrageConfig(), mvConfig);
        QCOMPARE(settings.mvBarrageEnabled(), false);
        ShellSettings restored(prefs);
        QVERIFY(restored.lyricBarrageEnabled());
        QCOMPARE(restored.lyricBarrageConfig().value("speed").toDouble(), 1.5);
        discussion->barrage()->setDraft("弹幕草稿 🎵");
        if (!live) {
            DetailsTestAccess::verification(*discussion->barrage());
            QTRY_VERIFY(panel.isNull());
            QVERIFY(discussion->barrage()->busy());
            QCOMPARE(discussion->barrage()->draft(), QString("弹幕草稿 🎵"));
            DetailsTestAccess::verificationFailure(*discussion->barrage());
            QTRY_VERIFY(panel = root->findChild<QObject *>("lyricBarragePanel"));
            QVERIFY(!discussion->barrage()->busy());
            QCOMPARE(discussion->barrage()->draft(), QString("弹幕草稿 🎵"));
        }

        QVERIFY(capture("barrage"));
        click(window, visual(window->contentItem(), "barrageClose"));
        QTRY_VERIFY(panel.isNull());
        QCOMPARE(popupCount(), baseline);
        QPointer<QQuickItem> layer;
        QTRY_VERIFY(layer = visual(window->contentItem(), "barrageLayer"));
        if (live)
            QTRY_VERIFY_WITH_TIMEOUT(
                !discussion->barrage()->loading() && !discussion->barrage()->empty(), 25000);
        else
            DetailsTestAccess::barrage(*discussion->barrage());
        PlaybackServiceTestAccess::playing(playback, true);
        if (!live)
            QVERIFY(QMetaObject::invokeMethod(layer, "launch", Q_ARG(bool, false)));
        QQuickItem *flight = nullptr;
        QTRY_VERIFY(flight = visual(window->contentItem(), "barrageFlight"));
        QTest::qWait(120);
        auto *barrageText = visual(window->contentItem(), "barrageText");
        QVERIFY(barrageText);
        QCOMPARE(barrageText->property("textFormat").toInt(), 0);
        QCOMPARE(barrageText->property("lineCount").toInt(), 1);
        PlaybackServiceTestAccess::playing(playback, false);
        QTest::qWait(35);
        const auto pausedX = flight->x();
        QTest::qWait(100);
        QVERIFY(qAbs(flight->x() - pausedX) < .1);
        PlaybackServiceTestAccess::playing(playback, true);
        QTRY_VERIFY(flight->x() < pausedX - 2);
        QVERIFY(capture("flights"));
        PlaybackServiceTestAccess::playing(playback, false);
        window->setVisibility(QWindow::Minimized);
        QTRY_VERIFY(layer.isNull());
        QVERIFY(discussion->barrage()->empty());
        window->setVisibility(QWindow::Windowed);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(owner->property("item").isNull());
        QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
        QTRY_VERIFY(owner->property("opened").toBool());
        QTRY_VERIFY(layer = visual(window->contentItem(), "barrageLayer"));
        QCOMPARE(discussion->barrage()->draft(), QString("弹幕草稿 🎵"));
        if (!live) {
            DetailsTestAccess::barrage(*discussion->barrage());
            PlaybackServiceTestAccess::track(playback, false);
            QVERIFY(discussion->barrage()->empty());
            QCOMPARE(layer->property("flightCount").toInt(), 0);
        }
        settings.setLyricBarrageEnabled(false);
        QTRY_VERIFY(layer.isNull());
        QVERIFY(discussion->barrage()->empty());
        click(window, visual(window->contentItem(), "playbackCollapseAction"));
        QTRY_VERIFY(owner->property("item").isNull());
        QCOMPARE(popupCount(), 0);
        QVERIFY(!visual(window->contentItem(), "playerbar-action-barrage"));
        details.shutdown();
        commands.shutdown();
        router.shutdown();
        authors.shutdown();
        root.reset();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
        api.shutdown();
    }
};
QTEST_MAIN(PlaybackDiscussionTest)
#include "PlaybackDiscussionTest.moc"
