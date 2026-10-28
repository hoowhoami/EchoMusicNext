#include "DetailsService.h"
#include "CommentExtent.h"
#include "ArtistDetailService.h"
#include "AlbumDetailService.h"
#include "PlaylistDetailService.h"
#include "ShellController.h"
#include "ShellSettings.h"
#include "ThemeStore.h"
#include "NetworkPreferences.h"
#include "WindowController.h"
#include "NativeWindow.h"
#include "RenderedInk.h"
#include <QFileInfo>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QVideoFrame>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTimer>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QAbstractItemModelTester>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQmlIncubationController>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWheelEvent>
#include <QPersistentModelIndex>
#include <QSettings>
#include <QStyleHints>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <ctime>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#endif
using namespace EchoMusic;
namespace EchoMusic
{
class AlbumDetailContextTestAccess
{
public:
    static void ready(AlbumDetailContext &context)
    {
        context.m_detail = {{"id", "960399"}, {"title", "专辑 Album"}};
        context.m_complete = true;
        emit context.changed();
    }
};
class PlaylistDetailContextTestAccess
{
public:
    static void ready(PlaylistDetailContext &context)
    {
        context.m_detail = {{"global_id", "141134598"}, {"title", "歌单 Playlist"}};
        context.m_complete = true;
        emit context.changed();
    }
};
class ApiClientTestAccess
{
public:
    static QMap<quint64, QString> operations(ApiClient &api)
    {
        QMap<quint64, QString> result;
        for (auto it = api.m_pending.cbegin(); it != api.m_pending.cend(); ++it)
            result.insert(it.key(), it->request.value("operation").toString());
        return result;
    }
};
class PlaybackServiceTestAccess
{
public:
    static void current(PlaybackService &p)
    {
        p.m_track =
            PlaybackTrack::fromDomain({{"id", "32042818"},
                                       {"hash", "105A2705BEEE36A8BED029A8E1A6311D"},
                                       {"title", "龙战骑士"},
                                       {"album_id", "960399"},
                                       {"artists", QJsonArray{QJsonObject{{"name", "周杰伦"}}}}});
        emit p.changed();
    }
};
class DetailsTestAccess
{
public:
    static QJsonObject row(int id, bool own = false)
    {
        return {{"id", QString::number(id)},
                {"resource_id", "154262"},
                {"category_id", "2364"},
                {"resource_name", "开发验证歌曲"},
                {"content",
                 id == 1 ? QStringLiteral("中文评论与 Latin glyph alignment。\n第二行包含表情 "
                                          "🎵，保留换行与完整内容。")
                         : QStringLiteral("评论 %1 · Native virtual list").arg(id)},
                {"user_name", QStringLiteral("用户 Artist %1").arg(id)},
                {"user_id", own ? "42" : "77"},
                {"created_at", "2026-10-18 12:30:00"},
                {"location", "北京"},
                {"reply_count", id == 1 ? 10 : 0},
                {"reply_code", 0},
                {"likes", QJsonObject{{"count", id * 3}}},
                {"images", QJsonArray{}},
                {"membership", QJsonObject{}},
                {"identity", QJsonObject{}}};
    }
    static void comments(CommentsController &c, int count = 1000, const QString &key = "all")
    {
        QJsonArray rows;
        for (int i = 1; i <= count; ++i)
            rows.append(row(i, i == 1));
        auto &s = c.stream(key);
        s.rows = rows;
        s.loaded = true;
        s.more = false;
        s.total = count;
        c.m_categories = {QJsonObject{{"id", 1}, {"label", "音乐故事"}, {"cnt", 72}}};
        c.m_words = {QJsonObject{{"content", "思绪"}, {"count", 300}}};
        c.m_category = "1";
        c.m_word = "思绪";
        c.rebuild();
        emit c.changed();
    }
    static void styledThread(CommentsController &c)
    {
        comments(c, 0);
        auto root = row(1);
        root["user_name"] = QStringLiteral("竖");
        root["content"] = QStringLiteral("没看懂的是搞笑图片，看懂了就是童年");
        root["created_at"] = "2021-06-12 22:32:48";
        root["location"] = "";
        root["reply_count"] = 2;
        root["likes"] = QJsonObject{{"count", 1307}};
        auto first = row(2);
        first["user_name"] = QStringLiteral("没有尽头");
        first["content"] =
            QStringLiteral("小时候的你我他[微笑][微笑]//@清风:没看懂的是搞笑图片，看懂了就是童年");
        first["parent_id"] = "1";
        first["reply_code"] = 1;
        first["created_at"] = "2021-06-30 10:45:03";
        first["location"] = "";
        first["likes"] = QJsonObject{{"count", 67}};
        auto second = row(3);
        second["content"] = QStringLiteral("保留换行与 <b>原文</b> & 空格  ").repeated(10);
        second["parent_id"] = "1";
        second["reply_code"] = 1;
        second["created_at"] = "2021-07-21 20:46:06";
        auto &stream = c.stream("all");
        stream.rows = {root, row(4)};
        stream.total = 2;
        auto &floor = c.stream("thread:1");
        floor.rows = {first, second};
        floor.loaded = floor.expanded = true;
        floor.more = false;
        floor.total = 2;
        c.rebuild();
        emit c.changed();
    }
    static void seed(SongDetailContext &c)
    {
        c.m_mix = 32042818;
        c.m_hash = "105A2705BEEE36A8BED029A8E1A6311D";
        c.m_detail = {{"title", "无奈的思绪 · Song Details"},
                      {"artist", "歌手 Artist"},
                      {"artist_id", 3520},
                      {"album", "专辑 Album"},
                      {"album_id", "960399"},
                      {"favorite_count", 42561},
                      {"comment_count", 12345},
                      {"ranking_summary", "见证歌曲的每一次登榜"}};
        c.m_qualities = {"HQ", "SQ", "Hi-Res"};
        c.m_ranking = {{"info", QJsonArray{QJsonObject{{"platform", 1},
                                                       {"platform_name", "酷狗音乐榜"},
                                                       {"ranking_num", 3},
                                                       {"ranking_times", 12},
                                                       {"last_time", "2026-10-18"}}}}};
        c.m_rankingDetails = {{"list", QJsonArray{QJsonObject{{"platform", 1},
                                                              {"name", "飙升榜"},
                                                              {"platform_name", "酷狗音乐"},
                                                              {"count", 12},
                                                              {"top_ranking", 1}}}}};
        c.m_track = {{"id", "32042818"},
                     {"title", "无奈的思绪"},
                     {"hash", c.m_hash},
                     {"artists", QJsonArray{QJsonObject{{"name", "歌手 Artist"}}}},
                     {"album_id", "960399"}};
        c.updateSong();
        c.m_loaded = true;
        comments(c.m_comments);
    }
    static void variedComments(CommentsController &c, int count)
    {
        comments(c, count);
        auto &s = c.stream("all");
        for (int i = 0; i < s.rows.size(); ++i) {
            auto entry = s.rows[i].toObject();
            entry["content"] = QString("短句中文 Artist\n").repeated(1 + i % 9);
            s.rows[i] = entry;
        }
        singers(c);
        c.rebuild();
    }
    static void navigation(SongDetailContext &c, const QJsonValue &artist, const QJsonValue &album)
    {
        c.m_detail["artist_id"] = artist;
        c.m_detail["album_id"] = album;
        emit c.changed();
    }
    static void artists(SongDetailContext &c, const QJsonArray &artists)
    {
        c.m_detail["artists"] = artists;
        emit c.changed();
    }
    static void metadata(SongDetailContext &c, const QJsonObject &value)
    {
        constexpr quint64 id = 999001;
        c.m_pending.insert(id, {SongDetailContext::Metadata, c.m_api.sessionRevision(), c.m_epoch});
        c.accept(id, QJsonDocument(QJsonArray{value}).toJson());
    }
    static void seed(MvDetailContext &c)
    {
        QJsonArray sources{QJsonObject{{"hash", "00000000000000000000000000000001"},
                                       {"quality", "sd"},
                                       {"codec", "H.264"},
                                       {"width", 768},
                                       {"height", 432},
                                       {"size_bytes", 21549465},
                                       {"bit_rate", 637865}},
                           QJsonObject{{"hash", "00000000000000000000000000000002"},
                                       {"quality", "sd"},
                                       {"codec", "H.265"},
                                       {"size_bytes", 13098344},
                                       {"bit_rate", 388093}}};
        c.m_versions = {QJsonObject{{"id", 33958},
                                    {"title", "无奈的思绪 · MV"},
                                    {"artist", "歌手 Artist"},
                                    {"duration_ms", 270003},
                                    {"published_at", "2008-10-15"},
                                    {"play_count", 1240542},
                                    {"collection_count", 4357},
                                    {"download_count", 81197},
                                    {"recommendation_code", 1},
                                    {"sources", sources},
                                    {"tags", QJsonArray{QJsonObject{{"tag_name", "流行"}}}}},
                        QJsonObject{{"id", 756652}, {"title", "第二个版本"}, {"sources", sources}}};
        c.m_version = 0;
        c.applyVersion();
    }
    static void pending(CommentsController &c, quint64 id, const QString &key = "all",
                        quint32 page = 1)
    {
        auto &s = c.stream(key);
        c.m_pending.insert(id, {CommentsController::Operation::List,
                                key,
                                {},
                                c.m_epoch,
                                s.revision,
                                c.m_api.sessionRevision(),
                                page});
    }
    static void singers(CommentsController &c)
    {
        auto entry = row(9001);
        entry["user_name"] = "歌手 Artist";
        entry["content"] = "歌手说 · 与每一位听众分享音乐。";
        entry["reply_count"] = 1;
        c.stream("all").singerRows = {entry};
        c.rebuild();
        emit c.changed();
    }
    static void accept(CommentsController &c, quint64 id, const QJsonObject &v)
    {
        c.accept(id, QJsonDocument(v).toJson());
    }
    static void feedMutation(CommentsController &c, quint64 id, const QString &draft)
    {
        c.m_pending.insert(id, {CommentsController::Operation::Send,
                                {},
                                draft,
                                c.m_epoch,
                                0,
                                c.m_api.sessionRevision(),
                                0});
    }
    static void release(MvDetailContext &c)
    {
        c.releasePlayer();
    }
    static void choices(MvDetailContext &c)
    {
        c.m_versions = {QJsonObject{{"sources", QJsonArray{QJsonObject{{"hash", "high-hevc"},
                                                                       {"codec", "H.265"},
                                                                       {"quality", "fhd"},
                                                                       {"width", 3840},
                                                                       {"height", 2160}},
                                                           QJsonObject{{"hash", "compatible-h264"},
                                                                       {"codec", "H.264"},
                                                                       {"quality", "hd"},
                                                                       {"width", 1920},
                                                                       {"height", 1080},
                                                                       {"bit_rate", 3000000}},
                                                           QJsonObject{{"hash", "COMPATIBLE-H264"},
                                                                       {"codec", "MKV"},
                                                                       {"quality", "hd"},
                                                                       {"size_bytes", 123}}}}}};
        c.m_version = 0;
        c.applyVersion();
    }
    static void open(MvDetailContext &c, const QString &url)
    {
        c.openPlayer(url);
    }
};
} // namespace EchoMusic
namespace
{
QQuickItem *visual(QQuickItem *item, const QString &name, bool includeHidden = false)
{
    if (item->objectName() == name && (includeHidden || item->isVisible()))
        return item;
    for (auto *child : item->childItems())
        if (auto *found = visual(child, name, includeHidden))
            return found;
    return nullptr;
}
int visualCount(QQuickItem *item, const QString &name)
{
    int count = item->objectName() == name && item->isVisible() ? 1 : 0;
    for (auto *child : item->childItems())
        count += visualCount(child, name);
    return count;
}
QQuickItem *visualInBounds(QQuickItem *item, const QString &name, const QRectF &sceneBounds)
{
    if (item->objectName() == name && item->isVisible() &&
        sceneBounds.contains(item->mapToScene(QPointF(item->width() / 2, item->height() / 2))))
        return item;
    for (auto *child : item->childItems())
        if (auto *found = visualInBounds(child, name, sceneBounds))
            return found;
    return nullptr;
}
void click(QQuickWindow *window, QQuickItem *item)
{
    QPointer<QQuickItem> target(item);
    QVERIFY2(target, "Native click target must be mounted");
    QVERIFY(Test::prepareNativeInput(window));
    QSignalSpy frames(window, &QQuickWindow::frameSwapped);
    window->requestUpdate();
    QTRY_VERIFY(!frames.isEmpty());
    QVERIFY2(target, "Native click target was retired during window activation");
    QTRY_VERIFY(target && target->width() > 0 && target->height() > 0);
    const auto point =
        target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
    Test::moveNativePointer(window, point);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
}
QList<QObject *> popups(QObject *host)
{
    QSet<QObject *> visited;
    QList<QObject *> found;
    const auto visit = [&](auto &&self, QObject *object) -> void {
        if (!object || visited.contains(object))
            return;
        visited.insert(object);
        if (object->inherits("QQuickPopup"))
            found.append(object);
        for (auto *child : object->children())
            self(self, child);
        if (auto *item = qobject_cast<QQuickItem *>(object))
            for (auto *child : item->childItems())
                self(self, child);
    };
    visit(visit, host);
    return found;
}
void scrollTo(QQuickWindow *window, QQuickItem *scroll, QQuickItem *item)
{
    const auto center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    const auto top = scroll->mapToScene(QPointF());
    const auto y = scroll->property("contentY").toReal() +
                   (center.y() - top.y()) / scroll->scale() - scroll->height() / 2;
    scroll->setProperty(
        "contentY",
        qBound(0., y, qMax(0., scroll->property("contentHeight").toReal() - scroll->height())));
    window->update();
    QTest::qWait(60);
}
// Development-only, explicitly authorized current-account sampling. Never
// copies credentials to preferences, fixtures, logs or the application's store.
QJsonObject samplingSnapshot()
{
    QJsonObject result;
    const auto name = QStringLiteral("details-read-only-oracle");
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(QDir::homePath() +
                           "/Library/Application Support/echo-music/echomusic.sqlite");
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (db.open() && db.transaction()) {
            QSqlQuery query(db);
            query.prepare("SELECT value_json FROM app_kv WHERE key=?");
            for (const auto &key : {"pinia:user", "pinia:device"}) {
                query.bindValue(0, key);
                if (query.exec() && query.next())
                    result.insert(key,
                                  QJsonDocument::fromJson(query.value(0).toByteArray()).object());
                query.finish();
            }
            db.rollback();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return result;
}
} // namespace
class DetailsTest : public QObject
{
    Q_OBJECT
private slots:
    void commentThreadsMatchSourceLayout()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        DetailsService service(api, playback, router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("song-detail", {{"tab", "comment"}}, "32042818"));
        auto *controller = service.songFor(router.current())->comments();
        DetailsTestAccess::styledThread(*controller);
        ShellSettings shell(prefs);
        ThemeStore theme(prefs);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.Details
import EchoMusic.Shell
import EchoMusic.Theme
Window {
 id: window
 required property CommentsController controller
 required property ShellSettings settings
 property bool compact: false
 visible: true; width: compact ? 520 : 1000; height: 900
 Rectangle { anchors.fill: parent; color: Theme.main }
 Item {
  width: parent.width / window.settings.uiScale
  height: parent.height / window.settings.uiScale
  scale: window.settings.uiScale; transformOrigin: Item.TopLeft
  CommentsView {
   anchors.fill: parent; controller: window.controller
   compact: window.compact; showSections: false
   contentInset: compact ? 16 : 24; uiScale: window.settings.uiScale
  }
 }
})",
                          QUrl("qrc:/comment-style-parity-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"controller", QVariant::fromValue(controller)},
                                                   {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window && Test::prepareNativeInput(window));
        auto *list = visual(window->contentItem(), "commentsList");
        QVERIFY(list);
        const auto rowAt = [&](int index) -> QQuickItem * {
            QQuickItem *loader = nullptr;
            QMetaObject::invokeMethod(list, "rowItemAt", Q_RETURN_ARG(QQuickItem *, loader),
                                      Q_ARG(int, index));
            return loader ? loader->property("item").value<QQuickItem *>() : nullptr;
        };
        const auto captures = qEnvironmentVariable("ECHO_COMMENT_PARITY_CAPTURES");
        if (!captures.isEmpty())
            QVERIFY(QDir().mkpath(captures));
        for (bool compact : {false, true})
            for (bool dark : {false, true})
                for (bool zoomed : {false, true}) {
                    root->setProperty("compact", compact);
                    theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                    shell.resetZoom();
                    if (zoomed)
                        for (int step = 0; step < 5; ++step)
                            shell.zoomIn();
                    QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtBeginning"));
                    QQuickItem *main = nullptr, *heading = nullptr, *reply = nullptr,
                               *longReply = nullptr;
                    QTRY_VERIFY((main = rowAt(0)) && (heading = rowAt(1)) && (reply = rowAt(2)) &&
                                (longReply = rowAt(3)));
                    const auto replyReady = [](QQuickItem *item) {
                        return visual(item, "commentAvatar", true) &&
                               visual(item, "commentLikeCount", true) &&
                               visual(item, "commentRootTime", true) &&
                               visual(item, "commentReplyTime", true) &&
                               visual(item, "commentUserLine", true) &&
                               visual(item, "commentContent", true) &&
                               visual(item, "commentReplyDivider", true);
                    };
                    QTRY_VERIFY(replyReady(reply) && replyReady(longReply));
                    QTRY_VERIFY(qAbs(reply->height() - reply->implicitHeight()) < 1. &&
                                qAbs(longReply->height() - longReply->implicitHeight()) < 1.);
                    QTRY_VERIFY(visual(heading, "commentFloorCount"));
                    QTRY_COMPARE(list->property("rowSpacing").toReal(), 0.);
                    auto *mainSurface = visual(main, "commentCardSurface");
                    auto *headingSurface = visual(heading, "commentCardSurface");
                    QVERIFY(mainSurface && headingSurface);
                    QTRY_VERIFY(qAbs(mainSurface->mapToScene({0, mainSurface->height()}).y() -
                                     headingSurface->mapToScene({0, 0}).y()) < 1.);
                    QCOMPARE(visual(reply, "commentAvatar")->width(), 30.);
                    QCOMPARE(visual(reply, "commentAvatar")->height(), 30.);
                    QVERIFY(!visual(reply, "commentLikeCount", true)->isVisible());
                    QVERIFY(!visual(reply, "commentRootTime", true)->isVisible());
                    QVERIFY(visual(reply, "commentReplyTime")->isVisible());
                    QCOMPARE(visual(reply, "commentUserLine")
                                 ->property("font")
                                 .value<QFont>()
                                 .pixelSize(),
                             compact ? 11 : 12);
                    QCOMPARE(visual(reply, "commentContent")
                                 ->property("font")
                                 .value<QFont>()
                                 .pixelSize(),
                             compact ? 12 : 13);
                    QVERIFY(!visual(reply, "commentReplyDivider", true)->isVisible());
                    QVERIFY(visual(longReply, "commentReplyDivider", true)->isVisible());
                    QCOMPARE(visual(heading, "commentFloorCount")->property("text").toString(),
                             "2");
                    auto *body = visual(reply, "commentContent");
                    auto *time = visual(reply, "commentReplyTime");
                    QVERIFY(time->mapToItem(reply, {0, 0}).y() >
                            body->mapToItem(reply, {0, body->height()}).y());
                    auto *longBody = visual(longReply, "commentContent");
                    auto *document =
                        longBody->property("textDocument").value<QQuickTextDocument *>();
                    QVERIFY(document);
                    QVERIFY(document->textDocument()->toPlainText().contains("<b>原文</b>"));
                    QVERIFY(
                        document->textDocument()->toPlainText().endsWith(QStringLiteral("展开")));
                    if (!captures.isEmpty()) {
                        QSignalSpy frames(window, &QQuickWindow::frameSwapped);
                        window->update();
                        QTRY_VERIFY(!frames.isEmpty());
                        QVERIFY(window->grabWindow().save(captures +
                                                          QString("/%1-%2-%3.png")
                                                              .arg(compact ? "playback" : "detail")
                                                              .arg(dark ? "dark" : "light")
                                                              .arg(zoomed ? 125 : 100)));
                    }
                    QVERIFY(QMetaObject::invokeMethod(longBody, "linkActivated",
                                                      Q_ARG(QString, "comment:toggle")));
                    QTRY_VERIFY(controller->model()
                                    ->data(controller->model()->index(3), CommentsModel::Entry)
                                    .toMap()
                                    .value("content_expanded")
                                    .toBool());
                    QTRY_VERIFY(
                        document->textDocument()->toPlainText().endsWith(QStringLiteral("收起")));
                    controller->toggleContent("3");
                    QTRY_VERIFY(
                        document->textDocument()->toPlainText().endsWith(QStringLiteral("展开")));
                    const auto target =
                        controller->model()
                            ->data(controller->model()->index(2), CommentsModel::Entry)
                            .toMap();
                    controller->beginReply(target, "1");
                    QQuickItem *form = nullptr;
                    QTRY_VERIFY((form = visual(window->contentItem(), "floorReplyComposer")));
                    QVERIFY(form->property("nested").toBool());
                    auto *replyBody = visual(reply, "commentContent");
                    QVERIFY(replyBody);
                    QTRY_VERIFY(qAbs(form->mapToScene({0, 0}).x() -
                                     replyBody->mapToScene({0, 0}).x()) < 1.);
                    QTRY_VERIFY(qAbs(form->mapToScene({form->width(), 0}).x() -
                                     replyBody->mapToScene({replyBody->width(), 0}).x()) < 1.);
                    auto *cancel = visual(form, "cancelFloorReply");
                    QVERIFY(cancel);
                    QPointer<QQuickItem> retiringForm = form;
                    click(window, cancel);
                    QTRY_VERIFY(!retiringForm);
                }
        root.reset();
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void singerCommentsStaySeparateFromPagination()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        DetailsService service(api, playback, router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("song-detail", {}, "32042818"));
        auto *c = service.songFor(router.current())->comments();
        QAbstractItemModelTester tester(c->model(),
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        DetailsTestAccess::pending(*c, 2001);
        DetailsTestAccess::accept(*c, 2001,
                                  {{"comments", QJsonArray{DetailsTestAccess::row(1)}},
                                   {"singer_comments", QJsonArray{DetailsTestAccess::row(9001)}},
                                   {"total", 2}});
        auto kind = [c](int index) {
            return c->model()->data(c->model()->index(index), CommentsModel::Kind).toString();
        };
        QCOMPARE(c->model()->rowCount(), 4);
        QCOMPARE(kind(0), "singerHeading");
        QCOMPARE(kind(1), "singerComment");
        QCOMPARE(kind(2), "singerDivider");
        QCOMPARE(kind(3), "comment");
        QCOMPARE(c->comment("9001").value("id").toString(), "9001");
        QVERIFY(c->hasMore());
        DetailsTestAccess::pending(*c, 2002, "all", 2);
        DetailsTestAccess::accept(
            *c, 2002, {{"comments", QJsonArray{DetailsTestAccess::row(2)}}, {"total", 2}});
        QCOMPARE(c->model()->rowCount(), 5);
        QVERIFY(!c->hasMore());
        QCOMPARE(c->totalText(), "2");
        c->setTab(1);
        DetailsTestAccess::comments(*c, 2, "category:1");
        QCOMPARE(c->model()->rowCount(), 2);
        QCOMPARE(kind(0), "comment");
        c->setTab(0);
        QCOMPARE(kind(0), "singerHeading");
        c->toggleThread("9001");
        QCOMPARE(kind(2), "threadHeading");
        api.advanceSession();
        QCOMPARE(c->model()->rowCount(), 0);
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void incrementalCommentsAndStaleSession()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        DetailsService service(api, playback, router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("song-detail", {{"mixSongId", "32042818"}}, "32042818"));
        auto *c = service.songFor(router.current());
        QVERIFY(c);
        auto *comments = c->comments();
        QAbstractItemModelTester tester(comments->model(),
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        DetailsTestAccess::pending(*comments, 1001);
        DetailsTestAccess::accept(
            *comments, 1001,
            {{"comments", QJsonArray{DetailsTestAccess::row(1), DetailsTestAccess::row(2)}},
             {"total", 72}});
        QCOMPARE(comments->model()->rowCount(), 2);
        QVERIFY(comments->hasMore());
        QSignalSpy reset(comments->model(), &QAbstractItemModel::modelReset);
        QSignalSpy inserted(comments->model(), &QAbstractItemModel::rowsInserted);
        DetailsTestAccess::pending(*comments, 1002, "all", 2);
        DetailsTestAccess::accept(
            *comments, 1002, {{"comments", QJsonArray{DetailsTestAccess::row(3)}}, {"total", 72}});
        QCOMPARE(comments->model()->rowCount(), 3);
        QCOMPARE(reset.size(), 0);
        QCOMPARE(inserted.size(), 1);
        DetailsTestAccess::pending(*comments, 1005, "all", 3);
        auto updated = DetailsTestAccess::row(3);
        updated["content"] = "同一评论更新资料";
        DetailsTestAccess::accept(*comments, 1005,
                                  {{"comments", QJsonArray{updated}}, {"total", 72}});
        QCOMPARE(comments->model()->rowCount(), 3);
        QVERIFY(!comments->hasMore());
        QCOMPARE(comments->model()
                     ->data(comments->model()->index(2), CommentsModel::Entry)
                     .toMap()
                     .value("content")
                     .toString(),
                 QString("同一评论更新资料"));
        QCOMPARE(reset.size(), 0);
        DetailsTestAccess::pending(*comments, 1003);
        api.advanceSession();
        DetailsTestAccess::accept(*comments, 1003,
                                  {{"comments", QJsonArray{DetailsTestAccess::row(4)}}});
        QCOMPARE(comments->model()->rowCount(), 0);
        comments->setDraft(QString::fromUtf8("保留草稿🎵"));
        DetailsTestAccess::feedMutation(*comments, 1004, comments->draft());
        comments->setDraft("用户已继续编辑");
        DetailsTestAccess::accept(*comments, 1004, {{"accepted", true}});
        QCOMPARE(comments->draft(), QString("用户已继续编辑"));
        QVERIFY(router.navigate("mv-detail", {}, "33958"));
        auto *mv = service.mvFor(router.current());
        DetailsTestAccess::choices(*mv);
        QCOMPARE(mv->sources().size(), 2);
        QCOMPARE(mv->sources().first().toMap().value("codec").toString(), QString("H.265"));
        QCOMPARE(mv->sources().last().toMap().value("codec").toString(), QString("H.264"));
        QCOMPARE(mv->sources().last().toMap().value("width").toInt(), 1920);
        QCOMPARE(mv->sources().last().toMap().value("size_bytes").toInt(), 123);
        QCOMPARE(mv->sourceHash(), QString("COMPATIBLE-H264"));
        DetailsTestAccess::open(*mv, "https://example.invalid/unpresented-video.mp4");
        QVERIFY(!mv->player());
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void detailNavigationUsesTypeAndAssociatedIdentities()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        DetailsService service(api, playback, router, false);
        service.registerRoutes(router);
        ShellController commands(prefs, router);
        auto track = PlaybackTrack::fromDomain(
            {{"id", "1"},
             {"title", "两位歌手"},
             {"artists", QJsonArray{QJsonObject{{"id", 3520}, {"name", "歌手 A"}},
                                    QJsonObject{{"id", 6743}, {"name", "歌手 B"}}}},
             {"album_id", "960399"}});
        commands.attachPlayback(playback);
        service.attachShell(commands);
        playback.songCommands()->openDetails(track);
        QVERIFY(router.current());
        QCOMPARE(router.current()->name(), QString("song-detail"));
        QVERIFY(!router.current()->query().contains("artists"));
        auto *context = service.songFor(router.current());
        QVERIFY(context);
        QCOMPARE(context->artistLinks().size(), 2);
        QCOMPARE(context->artistLinks()[0].toMap()["id"].toString(), QString("3520"));
        QCOMPARE(context->artistLinks()[1].toMap()["id"].toString(), QString("6743"));
        DetailsTestAccess::metadata(
            *context, {{"id", "1"},
                       {"album_id", "2603117"},
                       {"artists", QJsonArray{QJsonObject{{"id", 6743}, {"name", "云朵"}}}}});
        QCOMPARE(context->artistLinks()[0].toMap()["id"].toString(), QString("6743"));
        QCOMPARE(context->albumNavigationId(), QString("960399"));
        QVERIFY(router.navigate("song-detail", {}, "2"));
        context = service.songFor(router.current());
        for (const auto &invalid : {QJsonValue("0"), QJsonValue(0), QJsonValue(-1),
                                    QJsonValue("invalid"), QJsonValue()}) {
            DetailsTestAccess::navigation(*context, invalid, invalid);
            QVERIFY(context->artistNavigationId().isEmpty());
            QVERIFY(context->albumNavigationId().isEmpty());
        }
        DetailsTestAccess::navigation(*context, 6743, "2603117");
        QCOMPARE(context->artistNavigationId(), QString("6743"));
        QCOMPARE(context->albumNavigationId(), QString("2603117"));
        DetailsTestAccess::artists(*context, {QJsonObject{{"id", 6743}, {"name", "云朵"}},
                                              QJsonObject{{"name", "仅名称歌手"}}});
        QCOMPARE(context->artistLinks().size(), 2);
        QCOMPARE(context->artistLinks()[0].toMap()["id"].toString(), QString("6743"));
        QVERIFY(context->artistLinks()[1].toMap()["id"].toString().isEmpty());
        // Unlike list/player links, a detail album ID needs no album display name.
        QVERIFY(context->detail()["album"].toString().isEmpty());
        for (const auto &type : {QString("album"), QString("playlist")}) {
            QVERIFY(router.navigate("song-detail",
                                    {{"type", type}, {"artistId", "3520"}, {"albumId", "960399"}},
                                    type));
            context = service.songFor(router.current());
            QVERIFY(context->artistNavigationId().isEmpty());
            QVERIFY(context->albumNavigationId().isEmpty());
            DetailsTestAccess::artists(*context, {QJsonObject{{"id", 6743}, {"name", "云朵"}}});
            QVERIFY(context->artistLinks()[0].toMap()["id"].toString().isEmpty());
        }
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void nativeMvControlAlignment()
    {
        const auto clip = qEnvironmentVariable("ECHO_DETAILS_TEST_VIDEO");
        if (clip.isEmpty())
            QSKIP("Opt-in real local video decoder and native MV control pixels");
        QVERIFY(QFileInfo::exists(clip));
        QTemporaryDir directory;
        QSettings preferences(directory.filePath("settings.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, preferences, nullptr, directory.filePath("playback.sqlite"));
        RouteController router;
        DetailsService service(api, playback, router, false);
        service.registerRoutes(router);
        ShellSettings settings(preferences);
        settings.setMvBarrageEnabled(false);
        ThemeStore theme(preferences);
        theme.setMotionEnabled(false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &items) {
            for (const auto &item : items)
                warnings.append(item.toString());
        });
        QVERIFY(router.navigate("mv-detail", {}, "33958"));
        auto *mv = service.mvFor(router.current());
        DetailsTestAccess::seed(*mv);
        mv->setPresented(true);
        mv->setVolume(0);
        mv->setNetworkPreferences(nullptr);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.Details
import EchoMusic.Shell
import EchoMusic.Theme
Window {
    id: window
    required property MvDetailContext detail
    required property ShellSettings settings
    property real uiScale: 1
    property bool full: false
    visible: true; width: 1150; height: 800; color: Theme.main
    Item {
        width: window.width / window.uiScale; height: window.height / window.uiScale
        scale: window.uiScale; transformOrigin: Item.TopLeft
        MvVideoSurface {
            x: window.full ? 0 : 32; y: window.full ? 0 : 32
            width: parent.width - (window.full ? 0 : 64)
            height: window.full ? parent.height : width * 9 / 16
            detail: window.detail; settings: window.settings; fullScreen: window.full
        }
    }
})",
                          QUrl("qrc:/mv-control-alignment.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"detail", QVariant::fromValue(mv)}, {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        DetailsTestAccess::open(*mv,
                                QUrl::fromLocalFile(QFileInfo(clip).absoluteFilePath()).toString());
        QTRY_VERIFY_WITH_TIMEOUT(mv->player() && mv->duration() > 10000, 15000);
        if (mv->playing())
            mv->toggle();
        QTRY_VERIFY(!mv->playing());
        mv->seek(5000);
        QTRY_VERIFY(qAbs(mv->position() - 5000) < 250);
        const auto path = qEnvironmentVariable("ECHO_MV_CONTROL_ARTIFACTS");
        if (!path.isEmpty())
            QVERIFY(QDir().mkpath(path));
        for (bool dark : {false, true})
            for (qreal scale : {1., 1.25})
                for (bool full : {false, true}) {
                    theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                    root->setProperty("uiScale", scale);
                    root->setProperty("full", full);
                    QTest::qWait(150);
                    auto *seek = visual(window->contentItem(), "mvSeek");
                    auto *volume = visual(window->contentItem(), "mvVolume");
                    QVERIFY(seek && volume);
                    auto *track = seek->property("background").value<QQuickItem *>();
                    auto *volumeTrack = volume->property("background").value<QQuickItem *>();
                    QVERIFY(track && volumeTrack);
                    const qreal center = track->mapToScene(track->boundingRect().center()).y();
                    QCOMPARE(volumeTrack->mapToScene(volumeTrack->boundingRect().center()).y(),
                             center);
                    auto frame = window->grabWindow();
                    QVERIFY(!frame.isNull());
                    const qreal pixel = qreal(window->width()) / frame.width();
                    for (const auto &name : {"mvPlayPause", "mvPositionText", "mvDurationText",
                                             "mvMute", "mvFullscreen"}) {
                        auto *control = visual(window->contentItem(), name);
                        QVERIFY(control);
                        const auto ink = Test::renderedInkCenter(frame, window, control, Qt::white);
                        QVERIFY2(ink, name);
                        QVERIFY2(qAbs(*ink - center) <= pixel,
                                 qPrintable(QString("%1 ink=%2 track=%3 physical delta=%4")
                                                .arg(name)
                                                .arg(*ink)
                                                .arg(center)
                                                .arg(qAbs(*ink - center) / pixel)));
                    }
                    const auto progress =
                        Test::renderedInkCenter(frame, window, seek, theme.accent());
                    QVERIFY(progress);
                    QVERIFY(qAbs(*progress - center) <= pixel);
                    if (!path.isEmpty())
                        QVERIFY(frame.save(path + QString("/mv-controls-%1-%2-%3.png")
                                                      .arg(dark ? "dark" : "light")
                                                      .arg(int(scale * 100))
                                                      .arg(full ? "full" : "normal")));
                }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        mv->setPresented(false);
        root.reset();
        service.shutdown();
        router.shutdown();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
    void commentRowsKeepIdentityAcrossMetadataAndInsertions()
    {
        CommentsModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        const auto row = [](int id, int likes = 0) {
            return QVariantMap{
                {"entry", QVariantMap{{"id", QString::number(id)}, {"likes", likes}}},
                {"kind", "comment"},
                {"rootId", QString::number(id)}};
        };
        model.replace({row(1), row(2), row(3)});
        QPersistentModelIndex first(model.index(0)), last(model.index(2));
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        model.replace({row(1, 10), row(2), row(3), row(4)});
        QCOMPARE(removed.size(), 0);
        QVERIFY(first.isValid() && last.isValid());
        QCOMPARE(first.data(CommentsModel::Entry).toMap()["likes"].toInt(), 10);
        model.replace({row(1, 20), row(9), row(2), row(3, 30), row(4)});
        QCOMPARE(removed.size(), 0);
        QCOMPARE(last.row(), 3);
        QCOMPARE(last.data(CommentsModel::Entry).toMap()["likes"].toInt(), 30);
        model.replace({row(1, 21), row(2), row(3, 31), row(4)});
        QCOMPARE(removed.size(), 1);
        QVERIFY(first.isValid() && last.isValid());
        QCOMPARE(last.row(), 2);
    }
    void commentExtentWorkloadCosts()
    {
        if (!qEnvironmentVariableIsSet("ECHO_COMMENTS_PERF"))
            QSKIP("Opt-in Release workload measurement");
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        DetailsService service(api, playback, router, false);
        service.registerRoutes(router);
        ThemeStore theme(prefs);
        QVERIFY(router.navigate("song-detail", {{"tab", "comment"}}, "32042818"));
        auto *song = service.songFor(router.current());
        DetailsTestAccess::seed(*song);
        for (int count : {30, 400, 2000}) {
            DetailsTestAccess::variedComments(*song->comments(), count);
            QList<double> cold, reflow;
            for (int repeat = 0; repeat < 5; ++repeat) {
                CommentExtent extent;
                extent.setBodyFont(
                    theme.styledFont(theme.applicationFont(), 14, 400, 0, false, false));
                extent.setTimeFont(
                    theme.styledFont(theme.applicationFont(), 10, 400, 0, false, false));
                extent.setQuoteFont(
                    theme.styledFont(theme.applicationFont(), 12, 400, 0, false, false));
                extent.setLayoutWidth(760);
                QSignalSpy ready(&extent, &CommentExtent::changed);
                QElapsedTimer elapsed;
                elapsed.start();
                extent.setController(song->comments());
                while (ready.isEmpty() && elapsed.elapsed() < 10000)
                    QCoreApplication::processEvents();
                QVERIFY(!ready.isEmpty());
                cold.append(elapsed.nsecsElapsed() / 1e6);
                QVERIFY(extent.totalHeight() > count * 100);
                ready.clear();
                // Font invalidation exercises the same uncached text layout as
                // settled width reflow, without counting its 40ms debounce.
                elapsed.restart();
                extent.setBodyFont(
                    theme.styledFont(theme.applicationFont(), 15, 400, 0, false, false));
                while (ready.isEmpty() && elapsed.elapsed() < 10000)
                    QCoreApplication::processEvents();
                QVERIFY(!ready.isEmpty());
                reflow.append(elapsed.nsecsElapsed() / 1e6);
            }
            std::sort(cold.begin(), cold.end());
            std::sort(reflow.begin(), reflow.end());
            qInfo() << "comment_extent_perf" << count << "cold_median_ms" << cold[2]
                    << "cold_max_ms" << cold.last() << "reflow_median_ms" << reflow[2]
                    << "reflow_max_ms" << reflow.last();
        }
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void commentScrollKeepsVisibleRowsStable_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<bool>("zoomed");
        QTest::addColumn<int>("rowCount");
        for (bool dark : {false, true})
            for (bool zoomed : {false, true})
                for (int count : {30, 400})
                    QTest::newRow(qPrintable(QString("%1-%2-%3")
                                                 .arg(dark ? "dark" : "light")
                                                 .arg(zoomed ? 125 : 100)
                                                 .arg(count)))
                        << dark << zoomed << count;
    }
    void commentScrollKeepsVisibleRowsStable()
    {
        QTest::failOnWarning(QRegularExpression(".*TableView::forceLayout.*"));
        QFETCH(bool, dark);
        QFETCH(bool, zoomed);
        QFETCH(int, rowCount);
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        DetailsService service(api, playback, router, false);
        service.registerRoutes(router);
        ShellSettings shell(prefs);
        ThemeStore theme(prefs);
        theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
        if (zoomed)
            for (int i = 0; i < 5; ++i)
                shell.zoomIn();
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Window
import EchoMusic.Shell
import EchoMusic.Theme
Window {
 id: window
 required property RouteController router
 required property ShellSettings settings
 visible: true; width: 840; height: 740
 Rectangle { anchors.fill: parent; color: Theme.main }
 Item {
  width: window.width / window.settings.uiScale
  height: window.height / window.settings.uiScale
  scale: window.settings.uiScale; transformOrigin: Item.TopLeft
  RouteHost { anchors.fill: parent; router: window.router; settings: window.settings }
 }
})",
                          QUrl("qrc:/comment-scroll-test.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        QVERIFY(router.navigate("song-detail", {{"tab", "comment"}}, "32042818"));
        auto *song = service.songFor(router.current());
        DetailsTestAccess::seed(*song);
        DetailsTestAccess::variedComments(*song->comments(), rowCount);
        song->comments()->toggleContent("9");
        QTRY_VERIFY(visual(window->contentItem(), "commentContent"));
        auto *list = visual(window->contentItem(), "commentsList");
        QVERIFY(list);
        QTRY_VERIFY(list->property("contentHeight").toReal() > rowCount * 100);
        QTest::qWait(60);
        QVERIFY2(qAbs(list->property("contentY").toReal() + list->property("topMargin").toReal()) <
                     1.,
                 "Fresh comments page must start above its complete flow header");
        QVERIFY(Test::prepareNativeInput(window));
        QElapsedTimer clock;
        clock.start();
        const auto positions = [&] {
            QMap<QString, qreal> result;
            const auto visit = [&](auto &&self, QQuickItem *item) -> void {
                if (!item || !item->isVisible())
                    return;
                if (item->objectName() == "commentContent") {
                    const auto y = item->mapToScene(QPointF()).y();
                    if (y > 70 && y < window->height() - 120) {
                        auto *owner = item->parentItem();
                        while (owner && !owner->property("rootId").isValid())
                            owner = owner->parentItem();
                        if (owner)
                            result.insert(owner->property("rootId").toString(), y);
                    }
                }
                for (auto *child : item->childItems())
                    self(self, child);
            };
            visit(visit, list);
            return result;
        };
        const bool measure = qEnvironmentVariableIsSet("ECHO_COMMENTS_PERF");
        const auto footprint = []() -> qint64 {
#ifdef Q_OS_MACOS
            task_vm_info_data_t info{};
            mach_msg_type_number_t size = TASK_VM_INFO_COUNT;
            if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info),
                          &size) == KERN_SUCCESS)
                return static_cast<qint64>(info.phys_footprint);
#endif
            return -1;
        };
        if (measure) {
            const auto idleCpu = std::clock();
            QElapsedTimer idleWall;
            idleWall.start();
            QTest::qWait(1000);
            qInfo() << "comment_perf_idle_cpu_percent"
                    << 100. * (std::clock() - idleCpu) / CLOCKS_PER_SEC /
                           (idleWall.nsecsElapsed() / 1e9);
        }
        const auto scrollCpu = std::clock();
        const auto initialFootprint = footprint();
        QElapsedTimer scrollWall;
        scrollWall.start();
        qreal worstBackstep = 0;
        qreal extentMin = list->property("contentHeight").toReal(), extentMax = extentMin;
        for (int direction : {-1, 1}) {
            auto previous = positions();
            for (int frame = 0; frame < 150; ++frame) {
                if (frame % 6 == 0) {
                    const QPointF point(window->width() / 2., window->height() * .7);
                    QWheelEvent wheel(point, window->mapToGlobal(point.toPoint()), {},
                                      QPoint(0, direction * 120), Qt::NoButton, Qt::NoModifier,
                                      Qt::NoScrollPhase, false);
                    wheel.setTimestamp(clock.elapsed() + 1);
                    QCoreApplication::sendEvent(window, &wheel);
                }
                QTest::qWait(16);
                const auto current = positions();
                extentMin = qMin(extentMin, list->property("contentHeight").toReal());
                extentMax = qMax(extentMax, list->property("contentHeight").toReal());
                for (const auto &[id, y] : current.asKeyValueRange())
                    if (previous.contains(id)) {
                        const qreal backstep = (y - previous[id]) * -direction;
                        if (backstep > worstBackstep) {
                            worstBackstep = backstep;
                            qInfo() << "comment_scroll_backstep" << worstBackstep << "frame"
                                    << frame << "direction" << direction << "contentY"
                                    << list->property("contentY") << "originY"
                                    << list->property("originY");
                        }
                    }
                previous = current;
            }
            QTRY_VERIFY(!list->property("moving").toBool());
            if (direction == -1) {
                const auto anchored = positions();
                QVERIFY(!anchored.isEmpty());
                QVERIFY(list->property("contentY").toReal() > 500);
                auto updated = DetailsTestAccess::row(1);
                updated["content"] = "短句中文 Artist\n";
                updated["likes"] = QJsonObject{{"count", 1234}};
                DetailsTestAccess::pending(*song->comments(), 900001, "all", 2);
                DetailsTestAccess::accept(
                    *song->comments(), 900001,
                    {{"comments", QJsonArray{updated, DetailsTestAccess::row(rowCount + 1)}},
                     {"total", rowCount + 1}});
                QTest::qWait(120);
                const auto after = positions();
                int retained = 0;
                for (const auto &[id, y] : anchored.asKeyValueRange())
                    if (after.contains(id)) {
                        ++retained;
                        QVERIFY2(qAbs(after[id] - y) <= 1.,
                                 "Pagination displaced an existing visible comment");
                    }
                qInfo() << "comment_pagination_retained_anchors" << retained << "of"
                        << anchored.size();
                QVERIFY2(retained > 0,
                         "Pagination removed every visible comment and lost the scroll anchor");
            }
        }
        if (measure)
            qInfo() << "comment_perf_scroll" << rowCount << "cpu_percent_one_core"
                    << 100. * (std::clock() - scrollCpu) / CLOCKS_PER_SEC /
                           (scrollWall.nsecsElapsed() / 1e9)
                    << "wall_ms" << scrollWall.elapsed() << "footprint_before_bytes"
                    << initialFootprint << "footprint_after_bytes" << footprint()
                    << "comment_bodies" << visualCount(window->contentItem(), "commentContent");
        qInfo() << "comment_scroll_worst_backstep" << worstBackstep;
        qInfo() << "comment_scroll_estimated_extent" << extentMin << extentMax;
        QVERIFY2(extentMax / extentMin < 1.10,
                 "Scrolling changed the whole-page height estimate by more than 10 percent");
        QVERIFY2(worstBackstep <= 1.,
                 qPrintable(QString("Visible comment moved against wheel direction by %1px")
                                .arg(worstBackstep)));
        QCOMPARE(visualCount(window->contentItem(), "commentContent") < 30, true);
        bool expanded = false;
        auto *model = song->comments()->model();
        for (int i = 0; i < model->rowCount(); ++i) {
            const auto entry = model->data(model->index(i), CommentsModel::Entry).toMap();
            if (entry.value("id").toString() == "9")
                expanded = entry.value("content_expanded").toBool();
        }
        QVERIFY(expanded);
        const QQmlProperty wheelRunning(list, "EchoWheelScroll.running", qmlContext(list));
        QVERIFY(wheelRunning.isValid());
        QTRY_VERIFY(!wheelRunning.read().toBool());
        QTRY_VERIFY(!list->property("moving").toBool());
        QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtEnd"));
        QQuickItem *lastRow = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(list, "rowItemAt",
                                              Q_RETURN_ARG(QQuickItem *, lastRow),
                                              Q_ARG(int, model->rowCount() - 1)) &&
                    lastRow);
        QTest::qWait(80);
        const auto lastBottom = lastRow->mapToScene(QPointF(0, lastRow->height())).y();
        qInfo() << "comment_scroll_last_bottom" << lastBottom;
        QVERIFY(lastBottom > window->height() - 350 && lastBottom < window->height() - 40);
        service.shutdown();
        router.shutdown();
        root.reset();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void nativeProductionRoutes()
    {
        QTest::failOnWarning(QRegularExpression(".*TableView::forceLayout.*"));
        QTest::failOnWarning(QRegularExpression(".*DelegateModel::cancel.*"));
        const auto tabPolicy = QGuiApplication::styleHints()->tabFocusBehavior();
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
        const auto restoreTabPolicy = qScopeGuard(
            [tabPolicy] { QGuiApplication::styleHints()->setTabFocusBehavior(tabPolicy); });
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(42, "fixture-only-never-sent"));
        NetworkPreferences network(prefs, api);
        network.installApplicationTransport();
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        RouteController router;
        WindowController windows(prefs, false);
        DetailsService service(api, playback, router, false, nullptr, nullptr, &windows);
        service.registerRoutes(router);
        service.setNetworkPreferences(&network);
        AlbumDetailService albums(api, playback, router, false);
        albums.registerRoutes(router);
        ShellSettings shell(prefs);
        ArtistDetailService artistCatalog(api, playback, router, shell, false);
        artistCatalog.registerRoutes(router);
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        PlaylistOrderModel order(api, mutations);
        SongMetadataService songMetadata(api);
        PlaylistDetailService playlists(api, playback, library, mutations, order, songMetadata,
                                        router, false);
        playlists.registerRoutes(router);
        ShellController commands(prefs, router);
        commands.attachPlayback(playback);
        ThemeStore theme(prefs);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList errors;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &w) {
            for (const auto &e : w)
                errors.append(e.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Window
import EchoMusic.Shell
import EchoMusic.Theme
import EchoMusic.Details
Window {
 id: window
 required property RouteController router
 required property ShellSettings settings
 required property WindowController windows
 Shortcut { sequence: "F11"; autoRepeat: false; onActivated: window.windows.toggleFullscreen() }
 visible: true; width: 1150; height: 800
 Rectangle {anchors.fill:parent;color:Theme.main}
 Item {width:parent.width/window.settings.uiScale;height:parent.height/window.settings.uiScale;scale:window.settings.uiScale;transformOrigin:Item.TopLeft
 RouteHost {anchors.fill:parent;router:window.router;settings:window.settings}}
})",
                          QUrl("qrc:/details-native-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&shell)},
                                                   {"windows", QVariant::fromValue(&windows)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        windows.attach(window);
        window->requestActivate();
        QVERIFY(Test::prepareNativeInput(window));
        QVERIFY(router.navigate(
            "song-detail",
            {{"mixSongId", "32042818"}, {"hash", "105A2705BEEE36A8BED029A8E1A6311D"}}, "32042818"));
        auto *song = service.songFor(router.current());
        QElapsedTimer projectionTime;
        projectionTime.start();
        DetailsTestAccess::seed(*song);
        const auto projectionMs = projectionTime.elapsed();
        QTRY_VERIFY_WITH_TIMEOUT(visual(window->contentItem(), "songDetailPage"), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(visual(window->contentItem(), "songRankingToggle"), 4000);
        QTRY_VERIFY2(visual(window->contentItem(), "songArtistLink") &&
                         visual(window->contentItem(), "songAlbumLink"),
                     qPrintable(errors.join('\n')));
        QSignalSpy headerFrame(window, &QQuickWindow::frameSwapped);
        window->update();
        QTRY_VERIFY(headerFrame.size() > 0);
        auto *hiddenChips = visual(window->contentItem(), "commentFilterChips", true);
        QVERIFY(hiddenChips);
        QCOMPARE(hiddenChips->property("count").toInt(), 0);
        auto *artistLink = visual(window->contentItem(), "songArtistLink");
        auto *albumLink = visual(window->contentItem(), "songAlbumLink");
        QVERIFY(artistLink && albumLink);
        DetailsTestAccess::navigation(*song, "0", "0");
        QTRY_VERIFY(visual(artistLink, "artistLink-0"));
        QTRY_VERIFY(!visual(artistLink, "artistLink-0")->isEnabled() && !albumLink->isEnabled());
        QPointer<RouteContext> originalRoute = router.current();
        click(window, visual(artistLink, "artistLink-0"));
        click(window, albumLink);
        QCOMPARE(router.current(), originalRoute.data());
        DetailsTestAccess::navigation(*song, "3520", "960399");
        QTRY_VERIFY(visual(artistLink, "artistLink-0")->isEnabled() && albumLink->isEnabled());
        click(window, albumLink);
        QTRY_COMPARE(router.current()->name(), QString("album-detail"));
        QCOMPARE(router.current()->resourceId(), QString("960399"));
        QTRY_VERIFY(!originalRoute);
        router.back();
        QTRY_COMPARE(router.current()->name(), QString("song-detail"));
        QCOMPARE(router.current()->resourceId(), QString("32042818"));
        originalRoute = router.current();
        song = service.songFor(router.current());
        QVERIFY(song);
        DetailsTestAccess::seed(*song);
        QTRY_VERIFY(visual(window->contentItem(), "songRankingToggle"));
        const QJsonArray artistRows{
            QJsonObject{{"id", 3520}, {"name", "歌手 Artist"}},
            QJsonObject{{"id", 6743}, {"name", "云朵"}},
            QJsonObject{{"id", 12345}, {"name", QString("长歌手 Long Artist ").repeated(12)}},
            QJsonObject{{"id", 12346}, {"name", "屏外歌手"}}};
        DetailsTestAccess::artists(*song, artistRows);
        auto *ranking = visual(window->contentItem(), "songRankingToggle");
        QVERIFY2(ranking, qPrintable(errors.join('\n')));
        click(window, ranking);
        const auto artifacts = qEnvironmentVariable("ECHO_DETAILS_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        for (bool dark : {false, true})
            for (double scale : {1., 1.25}) {
                window->setWidth(scale > 1. ? 850 : 1150);
                theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                shell.resetZoom();
                if (scale > 1.)
                    for (int i = 0; i < 5; ++i)
                        shell.zoomIn();
                QTest::qWait(150);
                artistLink = visual(window->contentItem(), "songArtistLink");
                auto *firstArtist = visual(artistLink, "artistLink-0");
                auto *secondArtist = visual(artistLink, "artistLink-1");
                auto *clippedArtist = visual(artistLink, "artistLink-2");
                auto *hiddenArtist = visual(artistLink, "artistLink-3");
                QVERIFY(firstArtist && secondArtist && clippedArtist && hiddenArtist);
                QVERIFY(firstArtist->isEnabled() && secondArtist->isEnabled());
                QVERIFY(clippedArtist->width() > 0 &&
                        clippedArtist->width() < clippedArtist->implicitWidth());
                QVERIFY(!clippedArtist->isEnabled() && !hiddenArtist->isEnabled());
                QCOMPARE(hiddenArtist->width(), 0.);
                QVERIFY(Test::prepareNativeInput(window));
                firstArtist->forceActiveFocus(Qt::TabFocusReason);
                QTRY_VERIFY(firstArtist->hasActiveFocus());
                QTest::keyClick(window, Qt::Key_Tab);
                QTRY_VERIFY(secondArtist->hasActiveFocus());
                QTest::keyClick(window, Qt::Key_Tab);
                QVERIFY(!clippedArtist->hasActiveFocus() && !hiddenArtist->hasActiveFocus());
                click(window, clippedArtist);
                QCOMPARE(router.current(), originalRoute.data());
                for (const auto &target : {QString("3520"), QString("6743")}) {
                    QSignalSpy readyFrame(window, &QQuickWindow::frameSwapped);
                    window->update();
                    QTRY_VERIFY(readyFrame.size() > 0);
                    const auto artistRowReady = [&] {
                        auto *row = visual(window->contentItem(), "songArtistLink");
                        auto *first = row ? visual(row, "artistLink-0") : nullptr;
                        auto *second = row ? visual(row, "artistLink-1") : nullptr;
                        if (!first || !second)
                            return false;
                        const auto a = first->mapToScene(QPointF());
                        const auto b = second->mapToScene(QPointF());
                        return b.x() > a.x() + first->width() && qAbs(a.y() - b.y()) < 1.;
                    };
                    // A newly incubated page can exist before its nested Row
                    // positioner has polished, even after an earlier frame.
                    QTRY_VERIFY(artistRowReady());
                    auto *targetLink = visual(visual(window->contentItem(), "songArtistLink"),
                                              target == "3520" ? "artistLink-0" : "artistLink-1");
                    qInfo()
                        << "artist_target" << target << "enabled" << targetLink->isEnabled()
                        << "visible" << targetLink->isVisible() << "scene"
                        << targetLink->mapToScene(QPointF()) << "progress"
                        << visual(window->contentItem(), "songDetailHeader")->property("progress");
                    click(window, targetLink);
                    QTRY_COMPARE(router.current()->name(), QString("artist-detail"));
                    QCOMPARE(router.current()->resourceId(), target);
                    QTRY_VERIFY(!originalRoute);
                    router.back();
                    QTRY_COMPARE(router.current()->name(), QString("song-detail"));
                    QCOMPARE(router.current()->resourceId(), QString("32042818"));
                    originalRoute = router.current();
                    song = service.songFor(router.current());
                    QVERIFY(song);
                    DetailsTestAccess::seed(*song);
                    DetailsTestAccess::artists(*song, artistRows);
                    QTRY_VERIFY(visual(window->contentItem(), "songArtistLink"));
                    QTRY_VERIFY(
                        visual(visual(window->contentItem(), "songArtistLink"), "artistLink-1"));
                }
                window->update();
                QSignalSpy swapped(window, &QQuickWindow::frameSwapped);
                QTRY_VERIFY_WITH_TIMEOUT(swapped.size() > 0, 3000);
                auto frame = window->grabWindow();
                QVERIFY(!frame.isNull());
                if (!artifacts.isEmpty())
                    QVERIFY(frame.save(
                        artifacts +
                        QString("/song-detail-%1-%2.png").arg(dark ? "dark" : "light").arg(scale)));
            }
        shell.resetZoom();
        window->setWidth(1150);
        theme.setMode(ThemeStore::Light);
        QVariantMap commentQuery = router.current()->query();
        commentQuery.insert("tab", "comment");
        router.replaceQuery(commentQuery);
        QTRY_VERIFY(visual(window->contentItem(), "commentDraft"));
        QCOMPARE(service.songFor(router.current()), song);
        QTRY_VERIFY(visual(window->contentItem(), "commentReply"));
        auto *sections = visual(window->contentItem(), "commentSections");
        QVERIFY(sections);
        QVERIFY(sections->mapToScene(QPointF()).y() < 300);
        auto *artist = visual(window->contentItem(), "songArtistLink");
        QVERIFY(artist);
        QVERIFY(artist->mapToScene(QPointF()).x() < 300);
        QVERIFY(visualCount(window->contentItem(), "commentContent") < 50);
        qInfo() << "fixture_rows=1000 model_prepare_ms=" << projectionMs
                << "visible_comment_delegates="
                << visualCount(window->contentItem(), "commentContent");
        auto *body = visual(window->contentItem(), "commentContent");
        QVERIFY(body);
        auto *document = body->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(document && document->textDocument());
        QCOMPARE(document->textDocument()->begin().blockFormat().lineHeight(), 24.5);
        QCOMPARE(document->textDocument()->begin().blockFormat().lineHeightType(),
                 QTextBlockFormat::FixedHeight);
        QCOMPARE(popups(window).size(), 0);
        // Switch before buffer incubation settles, as the live category
        // response can arrive immediately after entering the comments tab.
        for (int cycle = 0; cycle < 3; ++cycle) {
            song->comments()->setTab(1);
            DetailsTestAccess::comments(*song->comments(), 20, "category:1");
            song->comments()->setTab(2);
            DetailsTestAccess::comments(*song->comments(), 15, "word:思绪");
            song->comments()->setTab(0);
            QTest::qWait(1);
        }
        QTRY_VERIFY(visual(window->contentItem(), "commentReply"));
        auto *reply = visual(window->contentItem(), "commentReply");
        auto *page = visual(window->contentItem(), "songDetailPage");
        QVERIFY(page);
        QVERIFY(!visual(window->contentItem(), "floorReplyComposer"));
        auto *commentList = visual(window->contentItem(), "commentsList");
        QVERIFY(commentList);
        const auto commentControl = [&](const QString &name) {
            auto *container = qobject_cast<QQuickItem *>(
                commentList->parentItem()->property("pageContainer").value<QObject *>());
            const auto top =
                container
                    ? container
                          ->mapToScene(QPointF(0, container->property("occlusionHeight").toReal()))
                          .y()
                    : 0.;
            return visualInBounds(window->contentItem(), name,
                                  QRectF(0, top, window->width(), window->height() - top - 100));
        };
        for (int cycle = 0; cycle < 3; ++cycle) {
            // Category replacement can preserve a viewport offset while the
            // virtual rows are reflowing. Use an on-screen native target.
            QVERIFY(QMetaObject::invokeMethod(commentList, "positionViewAtBeginning"));
            QSignalSpy commentFrame(window, &QQuickWindow::frameSwapped);
            window->update();
            QTRY_VERIFY(commentFrame.size() > 0);
            QTRY_VERIFY(reply = commentControl("commentReply"));
            click(window, reply);
            QTRY_VERIFY(visual(window->contentItem(), "floorReplyComposer"));
            QPointer<QQuickItem> editor = visual(window->contentItem(), "floorReplyComposer");
            auto *cancel = visual(window->contentItem(), "cancelFloorReply");
            QVERIFY(cancel);
            click(window, cancel);
            QTRY_VERIFY(!editor);
            QTRY_VERIFY(!visual(window->contentItem(), "floorReplyComposer"));
        }
        auto *remove = visual(window->contentItem(), "commentDelete");
        QVERIFY(remove);
        for (int cycle = 0; cycle < 3; ++cycle) {
            QVERIFY(QMetaObject::invokeMethod(commentList, "positionViewAtBeginning"));
            QTRY_VERIFY(remove = commentControl("commentDelete"));
            click(window, remove);
            QTRY_COMPARE(popups(window).size(), 1);
            QPointer<QObject> popup = popups(window).first();
            QTRY_VERIFY(visual(window->contentItem(), "popconfirmCancel"));
            click(window, visual(window->contentItem(), "popconfirmCancel"));
            QTRY_VERIFY(!popup);
            QTRY_COMPARE(popups(window).size(), 0);
        }
        for (bool dark : {false, true})
            for (double scale : {1., 1.25}) {
                theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                shell.resetZoom();
                if (scale > 1.)
                    for (int i = 0; i < 5; ++i)
                        shell.zoomIn();
                for (int tab : {0, 1, 2}) {
                    song->comments()->setTab(tab);
                    // Reset through the page owner, which also preserves the
                    // hero/main-tabs inset above the ListView's own header.
                    auto *container =
                        commentList->parentItem()->property("pageContainer").value<QObject *>();
                    QVERIFY(container);
                    QVERIFY(QMetaObject::invokeMethod(container, "resetPosition"));
                    QSignalSpy commentFrame(window, &QQuickWindow::frameSwapped);
                    window->update();
                    QTRY_VERIFY(commentFrame.size() > 0);
                    auto *mainTabs = visual(window->contentItem(), "songDetailMainTabs");
                    auto *subTabs = visual(window->contentItem(), "commentSections");
                    QVERIFY(mainTabs && subTabs);
                    QTRY_VERIFY(qAbs(subTabs->mapToScene(QPointF()).y() -
                                     mainTabs->mapToScene(QPointF(0, mainTabs->height())).y() -
                                     22 * scale) < 1.);
                    QQuickItem *firstRow = nullptr;
                    QTRY_VERIFY(QMetaObject::invokeMethod(commentList, "rowItemAt",
                                                          Q_RETURN_ARG(QQuickItem *, firstRow),
                                                          Q_ARG(int, 0)) &&
                                firstRow);
                    QTRY_VERIFY(qAbs(firstRow->mapToScene(QPointF()).y() -
                                     subTabs->mapToScene(QPointF(0, subTabs->height())).y() -
                                     (16 + (tab ? 54 : 0)) * scale) < 1.);
                    qInfo() << "comment_spacing" << tab << scale << "main_bottom"
                            << mainTabs->mapToScene(QPointF(0, mainTabs->height())).y()
                            << "sections_top" << subTabs->mapToScene(QPointF()).y()
                            << "first_row_top" << firstRow->mapToScene(QPointF()).y();
                    auto *selection = visual(subTabs, "tabSelection");
                    auto *selectedTab = visual(subTabs, "tab-" + QString::number(tab));
                    QVERIFY(selection && selectedTab);
                    QTRY_VERIFY(qAbs(selection->x() - selectedTab->x()) < .01);
                    QSignalSpy settledFrame(window, &QQuickWindow::frameSwapped);
                    window->update();
                    QTRY_VERIFY(settledFrame.size() > 0);
                    auto frame = window->grabWindow();
                    QVERIFY(!frame.isNull());
                    if (!artifacts.isEmpty())
                        QVERIFY(frame.save(artifacts + QString("/song-comments-%1-%2-tab%3.png")
                                                           .arg(dark ? "dark" : "light")
                                                           .arg(scale)
                                                           .arg(tab)));
                }
            }
        DetailsTestAccess::singers(*song->comments());
        song->comments()->setTab(0);
        for (bool dark : {false, true})
            for (const auto scale : {1., 1.25}) {
                theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                shell.resetZoom();
                if (scale > 1.)
                    for (int i = 0; i < 5; ++i)
                        shell.zoomIn();
                auto *container =
                    commentList->parentItem()->property("pageContainer").value<QObject *>();
                QVERIFY(QMetaObject::invokeMethod(container, "resetPosition"));
                QTRY_VERIFY(visual(window->contentItem(), "commentSingerHeading"));
                auto *content = visual(window->contentItem(), "commentContent");
                auto *toggle = visual(window->contentItem(), "commentThreadToggle");
                QVERIFY(content && toggle);
                auto *label = toggle->property("contentItem").value<QQuickItem *>();
                QVERIFY(label);
                QTRY_VERIFY(qAbs(label->mapToScene(QPointF()).x() -
                                 content->mapToScene(QPointF()).x()) < .01);
                QCOMPARE(toggle->property("leftPadding").toReal(), 0.);
                QSignalSpy frame(window, &QQuickWindow::frameSwapped);
                window->update();
                QTRY_VERIFY(frame.size() > 0);
                if (!artifacts.isEmpty())
                    QVERIFY(
                        window->grabWindow().save(artifacts + QString("/singer-comments-%1-%2.png")
                                                                  .arg(dark ? "dark" : "light")
                                                                  .arg(scale)));
            }
        shell.resetZoom();
        for (const auto &route : {QString("album-detail"), QString("playlist-detail")}) {
            const auto resource = route == "album-detail" ? "960399" : "141134598";
            QVERIFY(router.navigate(route, {{"tab", "comments"}}, resource));
            if (route == "album-detail")
                AlbumDetailContextTestAccess::ready(*albums.contextFor(router.current()));
            else
                PlaylistDetailContextTestAccess::ready(*playlists.contextFor(router.current()));
            auto *controller = route == "album-detail"
                                   ? albums.contextFor(router.current())->comments()
                                   : playlists.contextFor(router.current())->comments();
            DetailsTestAccess::comments(*controller, 30);
            // RouteHost retains a bounded LRU. Wait for this active context's
            // page and its incubated tab delegates, not a retired page's ids.
            QQuickItem *activePage = nullptr;
            QTRY_VERIFY((activePage = visual(window->contentItem(), route == "album-detail"
                                                                        ? "albumDetailPage"
                                                                        : "playlistDetailPage")) &&
                        activePage->property("context").value<RouteContext *>() ==
                            router.current());
            QQuickItem *catalog = nullptr;
            QTRY_VERIFY((catalog = visual(activePage, "songCatalogView")));
            QTRY_VERIFY(visual(catalog, "commentDraft"));
            QCOMPARE(catalog->property("toolbarHeight").toReal(), 56.);
            QTRY_VERIFY(visual(window->contentItem(), "detailTabIndicator"));
            QTRY_VERIFY(visual(window->contentItem(), "detailTabIndicator")->width() > 0);
            QPointer<QQuickItem> editor = visual(catalog, "commentDraft");
            QQuickItem *songsTab = nullptr;
            QTRY_VERIFY((songsTab = visual(catalog, "detailTab-0")));
            QTRY_VERIFY(songsTab->isEnabled());
            QSignalSpy tabClicked(songsTab, SIGNAL(clicked()));
            click(window, songsTab);
            QTRY_COMPARE(tabClicked.size(), 1);
            QTRY_COMPARE(router.current()->query().value("tab").toString(), QString("songs"));
            QTRY_VERIFY(!editor);
            auto *songs = route == "album-detail" ? albums.contextFor(router.current())->songs()
                                                  : playlists.contextFor(router.current())->songs();
            songs->replaceSongs({QJsonObject{
                {"id", "32042818"},
                {"hash", "105A2705BEEE36A8BED029A8E1A6311D"},
                {"title", "行内入口曲目"},
                {"album_id", "960399"},
                {"artists", QJsonArray{QJsonObject{{"id", 3520}, {"name", "Artist"}}}}}});
            for (const auto &entry : {QString("songDetailsAction"), QString("songMvAction")}) {
                QTRY_VERIFY(visual(window->contentItem(), entry));
                auto *button = visual(window->contentItem(), entry);
                Test::moveNativePointer(
                    window, button->mapToScene(QPointF(button->width() / 2, button->height() / 2))
                                .toPoint());
                QTRY_VERIFY(button->opacity() > .9);
                QCOMPARE(button->property("hoverBackground").toBool(), false);
                click(window, button);
                QCOMPARE(router.current()->name(), entry == "songDetailsAction"
                                                       ? QString("song-detail")
                                                       : QString("mv-detail"));
                if (entry == "songMvAction") {
                    QCOMPARE(router.current()->query().value("albumAudioId").toString(),
                             QString("32042818"));
                    QVERIFY(!router.current()->query().contains("hash"));
                    QTRY_VERIFY(visual(window->contentItem(), "mvDetailPage"));
                } else {
                    QCOMPARE(router.current()->query().value("artistId").toString(),
                             QString("3520"));
                    QTRY_VERIFY(visual(window->contentItem(), "songDetailPage"));
                }
                QVERIFY(playback.currentTrack()
                            .id.isEmpty()); // Row action never changes the playing song.
                router.back();
                QTRY_COMPARE(router.current()->name(), route);
            }
            click(window, visual(window->contentItem(), "detailTab-1"));
            QTRY_COMPARE(router.current()->query().value("tab").toString(), QString("comments"));
            QTRY_VERIFY(visual(window->contentItem(), "commentDraft"));
            for (bool dark : {false, true})
                for (double scale : {1., 1.25}) {
                    theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                    shell.resetZoom();
                    if (scale > 1.)
                        for (int i = 0; i < 5; ++i)
                            shell.zoomIn();
                    QTest::qWait(100);
                    if (!artifacts.isEmpty())
                        QVERIFY(
                            window->grabWindow().save(artifacts + QString("/%1-comments-%2-%3.png")
                                                                      .arg(route)
                                                                      .arg(dark ? "dark" : "light")
                                                                      .arg(scale)));
                }
            shell.resetZoom();
        }
        QVERIFY(router.navigate("mv-detail", {}, "33958"));
        auto *mv = service.mvFor(router.current());
        DetailsTestAccess::seed(*mv);
        QTRY_VERIFY_WITH_TIMEOUT(visual(window->contentItem(), "mvDetailPage"), 4000);
        auto *next = visual(window->contentItem(), "mvNextVersion");
        QVERIFY2(next, qPrintable(errors.join('\n')));
        auto *mvScroll = visual(window->contentItem(), "mvPageScroll");
        QVERIFY(mvScroll);
        scrollTo(window, mvScroll, next);
        click(window, next);
        QCOMPARE(mv->versionIndex(), 1);
        auto *prev = visual(window->contentItem(), "mvPreviousVersion");
        click(window, prev);
        QCOMPARE(mv->versionIndex(), 0);
        for (int cycle = 0; cycle < 3; ++cycle) {
            auto *barrage = visual(window->contentItem(), "mvBarrageButton");
            QVERIFY(barrage);
            scrollTo(window, mvScroll, barrage);
            click(window, barrage);
            QTRY_COMPARE(popups(window).size(), 1);
            QPointer<QObject> popup = popups(window).first();
            QTRY_VERIFY(visual(window->contentItem(), "mvBarrageDraft"));
            QTRY_VERIFY(popup->property("opened").toBool());
            mv->barrage()->setDraft("草稿 🎵");
            QVERIFY(Test::prepareNativeInput(window));
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(!popup);
            QTRY_COMPARE(popups(window).size(), 0);
            QCOMPARE(mv->barrage()->draft(), QString("草稿 🎵"));
        }
        const auto clip = qEnvironmentVariable("ECHO_DETAILS_TEST_VIDEO");
        if (!clip.isEmpty()) {
            QVERIFY(QFileInfo::exists(clip));
            mv->setVolume(0);
            // Local generated decoder fixture. Online Range/proxy playback is
            // exercised independently by liveProductionChains below.
            mv->setNetworkPreferences(nullptr);
            DetailsTestAccess::open(
                *mv, QUrl::fromLocalFile(QFileInfo(clip).absoluteFilePath()).toString());
            QTRY_VERIFY_WITH_TIMEOUT(mv->player() && mv->duration() > 10000, 15000);
            QTRY_VERIFY_WITH_TIMEOUT(mv->position() > 150, 10000);
            QPointer<QMediaPlayer> player = mv->player();
            QPointer<QAudioOutput> audio = player->audioOutput();
            auto *output = visual(window->contentItem(), "mvVideoOutput");
            QVERIFY(output);
            auto *sink = output->property("videoSink").value<QVideoSink *>();
            QVERIFY(sink);
            QTRY_VERIFY_WITH_TIMEOUT(sink->videoFrame().isValid(), 10000);
            mvScroll->setProperty("contentY", 0);
            click(window, visual(window->contentItem(), "mvPlayPause"));
            QTRY_VERIFY(!mv->playing());
            const auto paused = mv->position();
            QTest::qWait(200);
            QVERIFY(qAbs(mv->position() - paused) < 60);
            mv->seek(5000);
            QTRY_VERIFY(qAbs(mv->position() - 5000) < 200);
            for (int cycle = 0; cycle < 9; ++cycle) {
                const int entry = cycle / 3;
                qInfo() << "mv_fullscreen_cycle" << cycle << "entry" << entry << "exit"
                        << cycle % 3;
                if (windows.fullscreen() || windows.maximized())
                    window->showNormal();
                if (entry == 1)
                    window->showMaximized();
                else if (entry == 2)
                    windows.toggleFullscreen();
                QTRY_COMPARE(windows.fullscreen(), entry == 2);
                QTRY_COMPARE(windows.maximized(), entry == 1);
                // Let the test's prior host-state setup reach the platform
                // before injecting a separate user click into the MV surface.
                QTest::qWait(100);
                auto *fullscreen = visual(window->contentItem(), "mvFullscreen");
                QVERIFY(fullscreen);
                click(window, fullscreen);
                QTRY_COMPARE(popups(window).size(), 1);
                QPointer<QObject> popup = popups(window).first();
                QTRY_VERIFY(windows.fullscreen());
                QTRY_VERIFY(popup && popup->property("opened").toBool());
                QCOMPARE(mv->player(), player.data());
                QVERIFY(Test::prepareNativeInput(window));
                if (cycle % 3 == 0)
                    QTest::keyClick(window, Qt::Key_Escape);
                else if (cycle % 3 == 1)
                    QTest::keyClick(window, Qt::Key_F11);
                else {
                    auto *video = popup->property("contentItem").value<QQuickItem *>();
                    QVERIFY(video);
                    click(window, visual(video, "mvFullscreen"));
                }
                QTRY_VERIFY(!popup);
                QTRY_COMPARE(windows.fullscreen(), entry == 2);
                QTRY_COMPARE(windows.maximized(), entry == 1);
                QTRY_COMPARE(popups(window).size(), 0);
                QCOMPARE(mv->player(), player.data());
                if (entry == 2)
                    windows.toggleFullscreen();
            }
            window->hide();
            QTRY_VERIFY(!player && !audio);
            QVERIFY(!sink->videoFrame().isValid());
            window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
        }
        for (bool dark : {false, true})
            for (double scale : {1., 1.25}) {
                theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                shell.resetZoom();
                if (scale > 1.)
                    for (int i = 0; i < 5; ++i)
                        shell.zoomIn();
                QTest::qWait(150);
                auto frame = window->grabWindow();
                QVERIFY(!frame.isNull());
                if (!artifacts.isEmpty())
                    QVERIFY(frame.save(
                        artifacts +
                        QString("/mv-detail-%1-%2.png").arg(dark ? "dark" : "light").arg(scale)));
                mvScroll->setProperty(
                    "contentY",
                    qMax(0., mvScroll->property("contentHeight").toReal() - mvScroll->height()));
                QTest::qWait(80);
                if (!artifacts.isEmpty())
                    QVERIFY(window->grabWindow().save(
                        artifacts +
                        QString("/mv-sources-%1-%2.png").arg(dark ? "dark" : "light").arg(scale)));
                mvScroll->setProperty("contentY", 0);
            }
        // The final scroll schedules asynchronous view delegates. Let the
        // native window's controller finish them before destroying the test
        // window and its incubation controller ahead of the engine.
        const auto settled = [&] {
            auto *controller = engine.incubationController();
            if (!controller)
                return true;
            controller->incubateFor(10);
            return controller->incubatingObjectCount() == 0;
        };
        QTRY_VERIFY(settled());
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        artistCatalog.shutdown();
        service.shutdown();
        commands.shutdown();
        albums.shutdown();
        playlists.shutdown();
        order.close();
        mutations.cancel();
        router.shutdown();
        // Retiring the routes can itself queue final view work. Drain it
        // while the window-owned incubation controller still exists.
        QTRY_VERIFY(settled());
        root.reset();
        windows.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void liveProductionChains()
    {
        QTest::failOnWarning(QRegularExpression(".*DelegateModel::cancel.*"));
        if (!qEnvironmentVariableIsSet("ECHO_DETAILS_LIVE"))
            QSKIP("Opt-in current-account read-only native production routes/media");
        const auto snapshot = samplingSnapshot();
        const auto user = snapshot.value("pinia:user").toObject().value("info").toObject();
        const auto device = snapshot.value("pinia:device").toObject().value("info").toObject();
        QVERIFY(!user.value("token").toString().isEmpty());
        DeviceIdentity identity{device.value("mid").toVariant().toString(),
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
        QVERIFY(api.setAccount(user.value("userid").toVariant().toULongLong(),
                               user.value("token").toString(), user.value("t1").toString()));
        NetworkPreferences network(prefs, api);
        network.installApplicationTransport();
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        PlaybackServiceTestAccess::current(playback);
        RouteController router;
        WindowController windows(prefs, false);
        CommentAuthorService authors(api, true);
        DetailsService service(api, playback, router, true, nullptr, &authors, &windows);
        service.registerRoutes(router);
        service.setNetworkPreferences(&network);
        ShellController commands(prefs, router);
        commands.attachPlayback(playback);
        ThemeStore theme(prefs);
        ShellSettings shell(prefs);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &list) {
            for (const auto &warning : list)
                warnings.append(warning.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Window
import EchoMusic.Shell
import EchoMusic.Theme
import EchoMusic.Details
Window {
 id: window
 required property RouteController router
 required property ShellSettings settings
 visible:true; width:1150; height:800
 Item {
   width: window.width / window.settings.uiScale
   height: window.height / window.settings.uiScale
   scale: window.settings.uiScale
   transformOrigin: Item.TopLeft
   Rectangle {anchors.fill:parent;color:Theme.main}
   RouteHost {anchors.fill:parent;router:window.router;settings:window.settings}
 }
})",
                          QUrl("qrc:/details-live-test.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        windows.attach(window);
        QMap<quint64, QString> seen;
        QStringList trace;
        const auto record = [&] {
            const auto operations = ApiClientTestAccess::operations(api);
            for (const auto &[id, operation] : operations.asKeyValueRange())
                if (!seen.contains(id)) {
                    seen.insert(id, operation);
                    trace.append(operation);
                    qInfo().noquote() << "LIVE operation=" << operation;
                }
        };
        QTimer poll;
        connect(&poll, &QTimer::timeout, this, record);
        poll.start(5);
        connect(&api, &ApiClient::succeeded, &poll, [&](quint64, const QByteArray &) { record(); });
        connect(&api, &ApiClient::failed, &poll, [&](quint64 id, int, const QByteArray &b) {
            const auto error = QJsonDocument::fromJson(b).object();
            qInfo().noquote() << "LIVE failure operation=" << seen.value(id)
                              << "kind=" << error.value("kind").toString()
                              << "code=" << error.value("code").toInt();
            record();
        });
        if (qEnvironmentVariableIsSet("ECHO_DETAILS_SINGER_ONLY")) {
            QVERIFY(router.navigate("song-detail", {{"mixSongId", "266705785"}, {"tab", "comment"}},
                                    "266705785"));
            auto *song = service.songFor(router.current());
            QVERIFY(song);
            QTRY_VERIFY_WITH_TIMEOUT(!song->loading() && !song->comments()->loading() &&
                                         visual(window->contentItem(), "commentSingerHeading"),
                                     45000);
            auto *comments = song->comments();
            QCOMPARE(comments->model()
                         ->data(comments->model()->index(1), CommentsModel::Kind)
                         .toString(),
                     "singerComment");
            const auto singer =
                comments->model()->data(comments->model()->index(1), CommentsModel::Entry).toMap();
            QVERIFY(!singer.value("content").toString().isEmpty());
            QVERIFY(singer.value("reply_count").toInt() > 0);
            const auto artifacts = qEnvironmentVariable("ECHO_DETAILS_ARTIFACTS");
            if (!artifacts.isEmpty())
                QVERIFY(QDir().mkpath(artifacts));
            for (bool dark : {false, true})
                for (double scale : {1., 1.25}) {
                    theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                    shell.resetZoom();
                    if (scale > 1.)
                        for (int i = 0; i < 5; ++i)
                            shell.zoomIn();
                    auto *list = visual(window->contentItem(), "commentsList");
                    QVERIFY(list);
                    auto *container =
                        list->parentItem()->property("pageContainer").value<QObject *>();
                    QVERIFY(QMetaObject::invokeMethod(container, "resetPosition"));
                    QSignalSpy frame(window, &QQuickWindow::frameSwapped);
                    window->update();
                    QTRY_VERIFY(frame.size() > 0);
                    if (!artifacts.isEmpty())
                        QVERIFY(
                            window->grabWindow().save(artifacts + QString("/live-singer-%1-%2.png")
                                                                      .arg(dark ? "dark" : "light")
                                                                      .arg(scale)));
                }
            auto *toggle = visual(window->contentItem(), "commentThreadToggle");
            QVERIFY(toggle);
            click(window, toggle);
            QTRY_VERIFY_WITH_TIMEOUT(trace.contains("comment_floor") &&
                                         visual(window->contentItem(), "commentThreadToggle")
                                                 ->property("text")
                                                 .toString() == "收起回复",
                                     25000);
            QTRY_VERIFY_WITH_TIMEOUT(
                comments->model()->rowCount() > 4 &&
                    comments->model()
                            ->data(comments->model()->index(3), CommentsModel::Kind)
                            .toString() == "reply",
                25000);
            qInfo() << "LIVE singer_comment=" << singer.value("id").toString()
                    << "reply_count=" << singer.value("reply_count").toInt()
                    << "expanded_model_rows=" << comments->model()->rowCount();
            if (!artifacts.isEmpty())
                QVERIFY(window->grabWindow().save(artifacts + "/live-singer-expanded.png"));
            click(window, visual(window->contentItem(), "commentThreadToggle"));
            QTRY_VERIFY(visual(window->contentItem(), "commentThreadToggle")
                            ->property("text")
                            .toString()
                            .startsWith("查看"));
            record();
            QVERIFY(trace.indexOf("comment_music") >= 0);
            QVERIFY(trace.indexOf("user_batch_union_vipinfo") > trace.indexOf("comment_music"));
            QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
            poll.stop();
            commands.shutdown();
            service.shutdown();
            authors.shutdown();
            router.shutdown();
            root.reset();
            windows.shutdown();
            QSignalSpy drain(&playback, &PlaybackService::closed);
            playback.shutdown();
            QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
            api.shutdown();
            return;
        }
        QVERIFY(commands.invokePlayerCommand(ShellController::Comments));
        QCOMPARE(router.current()->name(), QString("song-detail"));
        QCOMPARE(router.current()->query().value("mixSongId").toString(), QString("32042818"));
        auto *song = service.songFor(router.current());
        QVERIFY(song);
        record();
        QTRY_VERIFY_WITH_TIMEOUT(!song->loading() && !song->comments()->loading() &&
                                     song->comments()->model()->rowCount() > 0,
                                 45000);
        QTRY_VERIFY_WITH_TIMEOUT(song->detail().contains("comment_count"), 30000);
        QVERIFY2(song->errorMessage().isEmpty(), qPrintable(song->errorMessage()));
        QCOMPARE(song->detail().value("artist").toString(), QString("周杰伦"));
        qInfo() << "LIVE comments rows=" << song->comments()->model()->rowCount();
        for (const auto &operation :
             {"privilege_lite", "krm_audio", "song_ranking", "song_ranking_filter", "comment_music",
              "user_batch_union_vipinfo", "favorite_count", "comment_count"})
            QVERIFY2(trace.contains(operation), operation);
        QVERIFY(trace.indexOf("privilege_lite") < trace.indexOf("krm_audio"));
        QVERIFY(trace.indexOf("krm_audio") < trace.indexOf("song_ranking"));
        QVERIFY(trace.indexOf("song_ranking_filter") < trace.indexOf("comment_music"));
        QVERIFY(trace.indexOf("comment_music") < trace.indexOf("user_batch_union_vipinfo"));
        QVERIFY(trace.indexOf("user_batch_union_vipinfo") < trace.indexOf("comment_count"));
        auto query = router.current()->query();
        query.insert("tab", "comment");
        router.replaceQuery(query);
        QTRY_VERIFY(visual(window->contentItem(), "commentContent"));
        auto *commentTab = visual(window->contentItem(), "detailTab-1");
        QVERIFY(commentTab);
        QTest::qWait(250); // Let the source tab underline transition finish before capture.
        const auto artifacts = qEnvironmentVariable("ECHO_DETAILS_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(window->grabWindow().save(artifacts + "/live-song-comments.png"));
        }
        // Real category, hot-word and floor commands share the same native UI
        // model. These are reads; publishing awaits an actual human proof.
        auto *comments = song->comments();
        comments->setTab(1);
        QTRY_VERIFY_WITH_TIMEOUT(!comments->loading() && comments->model()->rowCount() > 0, 25000);
        comments->setTab(2);
        QTRY_VERIFY_WITH_TIMEOUT(!comments->loading() && comments->model()->rowCount() > 0, 25000);
        comments->setTab(0);
        const auto first =
            comments->model()->data(comments->model()->index(0), CommentsModel::Entry).toMap();
        comments->toggleThread(first.value("id").toString());
        QTRY_VERIFY_WITH_TIMEOUT(trace.contains("comment_floor"), 25000);
        QVERIFY(commands.invokePlayerCommand(ShellController::Mv));
        QCOMPARE(router.current()->name(), QString("mv-detail"));
        QCOMPARE(router.current()->query().value("albumAudioId").toString(), QString("32042818"));
        QVERIFY(!router.current()->query().contains("hash"));
        auto *mv = service.mvFor(router.current());
        mv->setVolume(0);
        record();
        QTRY_VERIFY_WITH_TIMEOUT(mv->player() && mv->duration() > 0, 45000);
        QTRY_VERIFY_WITH_TIMEOUT(mv->position() > 200, 15000);
        QCOMPARE(mv->versionCount(), 2);
        QCOMPARE(mv->sources().size(), 4);
        auto *output = visual(window->contentItem(), "mvVideoOutput");
        QVERIFY(output);
        QPointer<QVideoSink> sink = output->property("videoSink").value<QVideoSink *>();
        QVERIFY(sink);
        QTRY_VERIFY_WITH_TIMEOUT(sink->videoFrame().isValid(), 15000);
        qInfo() << "LIVE native_video_frame=" << sink->videoFrame().size()
                << "duration_ms=" << mv->duration();
        if (qEnvironmentVariableIsSet("ECHO_DETAILS_COLLECTION_WRITE")) {
            auto *collection = mv->collection();
            const auto videoId = mv->detail().value("id").toString();
            QTRY_VERIFY_WITH_TIMEOUT(collection->loaded(), 25000);
            const auto initiallyCollected = collection->contains(videoId);
            auto restore = qScopeGuard([&] {
                // Undo only a known accepted mutation; an uncertain write is
                // observed separately, never automatically resent.
                if (!collection->pending(videoId) &&
                    collection->contains(videoId) != initiallyCollected) {
                    collection->toggle(videoId);
                    QElapsedTimer timeout;
                    timeout.start();
                    while (collection->pending(videoId) && timeout.elapsed() < 25000)
                        QTest::qWait(25);
                }
                qInfo() << "LIVE collection_cache_restored="
                        << (!collection->pending(videoId) &&
                            collection->contains(videoId) == initiallyCollected);
            });
            QSignalSpy mutations(collection, &VideoCollectionService::mutated);
            auto *button = visual(window->contentItem(), "mvCollection");
            QVERIFY(button);
            scrollTo(window, visual(window->contentItem(), "mvPageScroll"), button);
            click(window, button);
            QTRY_COMPARE_WITH_TIMEOUT(mutations.size(), 1, 25000);
            QCOMPARE(collection->contains(videoId), !initiallyCollected);
            click(window, button);
            QTRY_COMPARE_WITH_TIMEOUT(mutations.size(), 2, 25000);
            QCOMPARE(collection->contains(videoId), initiallyCollected);
        }
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/live-mv.png"));
        shell.setMvBarrageEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(!mv->barrage()->loading() && !mv->barrage()->empty(), 25000);
        QPointer<QMediaPlayer> oldPlayer = mv->player();
        mv->selectSource(mv->sources().last().toMap().value("hash").toString());
        QTRY_VERIFY(!oldPlayer);
        QTRY_VERIFY_WITH_TIMEOUT(mv->player() && mv->position() > 100, 45000);
        oldPlayer = mv->player();
        QPointer<QAudioOutput> oldAudio = oldPlayer->audioOutput();
        router.back();
        QTRY_VERIFY(!oldPlayer && !oldAudio);
        QVERIFY(!sink || !sink->videoFrame().isValid());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        poll.stop();
        commands.shutdown();
        service.shutdown();
        authors.shutdown();
        router.shutdown();
        root.reset();
        windows.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
};
QTEST_MAIN(DetailsTest)
#include "DetailsTest.moc"
