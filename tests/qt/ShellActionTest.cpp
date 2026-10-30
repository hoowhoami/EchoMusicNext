#include "ShellActionModel.h"
#include "TaskCenterModel.h"
#include "ShellController.h"
#include "PlaybackService.h"
#include "PlaybackFailureController.h"
#include <QJsonArray>
#include <QScopeGuard>
#include "EchoPopupAnchor.h"
#include <QAbstractItemModelTester>
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
    static void prepare(PlaybackService &player, int count = 1)
    {
        // Observe production final-failure handling independently of device I/O.
        QObject::disconnect(&player.m_engine, &AudioEngine::eventReceived, &player,
                            &PlaybackService::onEvent);
        player.invalidate();
        PlaybackTracks tracks;
        for (int i = 0; i < count; ++i) {
            PlaybackTrack track;
            track.id = QString::number(i + 1);
            track.title = "Notification song";
            track.localPath = QString("/domain-only-not-opened-%1.wav").arg(i);
            tracks.append(track);
        }
        player.m_sourceKey = "notification-test";
        player.m_catalog.activate(player.m_sourceKey, "test", tracks, 0);
        player.m_track = tracks.first();
        player.m_autoplay = true;
        player.m_playing = player.m_loading = false;
        player.m_error.clear();
    }
    static void fail(PlaybackService &player, const QString &reason)
    {
        player.m_error = reason;
        player.m_failure->failed();
    }
    static void sourceOperation(PlaybackService &player)
    {
        player.m_failure->sourceOperation("音质切换失败");
    }
    static void recover(PlaybackService &player)
    {
        player.m_error.clear();
        player.m_failure->succeeded();
    }
};
} // namespace EchoMusic
namespace
{
using M = ShellActionModel;
QStringList keys(QAbstractItemModel &model)
{
    QStringList result;
    for (int row = 0; row < model.rowCount(); ++row)
        result << model.index(row, 0).data(M::KeyRole).toString();
    return result;
}
M::Contribution action(QString key, M::Zone zone = M::Toolbar, int order = 0)
{
    M::Contribution a;
    a.key = key;
    a.id = key;
    a.title = key;
    a.icon = "music";
    a.defaultZone = zone;
    a.order = order;
    a.enabled = true;
    a.invoke = [] {};
    return a;
}
TaskCenterModel::Registration task(QString id, int priority = 0)
{
    TaskCenterModel::Registration registration;
    registration.id = id;
    registration.content.name = id;
    registration.priority = priority;
    return registration;
}
} // namespace
class ShellActionTest : public QObject
{
    Q_OBJECT
private slots:
    void playbackPageActionsDoNotEnterNormalProjections()
    {
        QTemporaryDir directory;
        SettingsStore settings(directory.filePath("settings.ini"));
        M model(settings, M::Playerbar);
        QObject owner;
        ShellActionProjection normal, page, overflow, editor;
        for (auto *projection : {&normal, &page, &overflow, &editor})
            projection->setActions(&model);
        normal.setIncludePlaybackPageActions(false);
        overflow.setIncludePlaybackPageActions(false);
        overflow.setZone(M::More);
        editor.setIncludePlaybackPageActions(false);
        editor.setUsePreferredPlacement(true);
        QAbstractItemModelTester normalContract(&normal), pageContract(&page);
        model.registerAction(owner, action("shared", M::Right));
        auto skin = action("page-skin", M::Right);
        skin.playbackPageOnly = true;
        const auto handle = model.registerAction(owner, skin);
        QCOMPARE(keys(normal), QStringList{"shared"});
        QCOMPARE(keys(overflow), QStringList{"shared"});
        QCOMPARE(keys(editor), QStringList{"shared"});
        QVERIFY(keys(page).contains("page-skin"));
        model.setCapacity(M::Right, 10);
        QVERIFY(model.setPlacement("page-skin", M::Left));
        QCOMPARE(keys(normal), QStringList{"shared"});
        QVERIFY(keys(overflow).isEmpty());
        QCOMPARE(keys(editor), QStringList{"shared"});
        normal.setIncludePlaybackPageActions(true);
        QVERIFY(keys(normal).contains("page-skin"));
        normal.setIncludePlaybackPageActions(false);
        QCOMPARE(keys(normal), QStringList{"shared"});
        QVERIFY(model.unregisterAction(handle));
        QCOMPARE(keys(normal), QStringList{"shared"});

        RouteController routes;
        ShellController controller(settings, routes);
        controller.setLyricPageActive(true);
        int pageOnly = 0;
        for (int i = 0; i < controller.playerbarActions()->rowCount(); ++i)
            pageOnly +=
                controller.playerbarActions()->index(i, 0).data(M::PlaybackPageOnlyRole).toBool();
        QCOMPARE(pageOnly, 2);
        controller.setLyricPageActive(false);
    }
    void dragPreviewCommitsOnceAndDiscardsCatalogChanges()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore preferences(directory.filePath("settings.ini"));
        M model(preferences, M::Titlebar);
        model.setCapacity(M::Toolbar, 3);
        QObject provider;
        auto first = model.registerAction(provider, action("one", M::Toolbar, 0));
        auto second = model.registerAction(provider, action("two", M::Toolbar, 1));
        model.registerAction(provider, action("three", M::Toolbar, 2));
        ShellActionProjection projection;
        projection.setZone(M::Toolbar);
        projection.setActions(&model);
        QAbstractItemModelTester tester(&projection,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy moves(&projection, &QAbstractItemModel::rowsMoved);
        const auto saved = preferences.value("shell/titlebar/order");
        QVERIFY(projection.beginReorder("one"));
        projection.previewReorder(2);
        QCOMPARE(keys(projection), QStringList({"two", "three", "one"}));
        QCOMPARE(keys(model), QStringList({"one", "two", "three"}));
        QCOMPARE(preferences.value("shell/titlebar/order"), saved);
        QVERIFY(!moves.isEmpty());
        projection.previewReorder(0);
        QCOMPARE(keys(projection), keys(model));
        projection.previewReorder(2);
        projection.finishReorder(false);
        QCOMPARE(keys(projection), keys(model));
        QCOMPARE(preferences.value("shell/titlebar/order"), saved);
        QVERIFY(projection.beginReorder("one"));
        projection.previewReorder(2);
        projection.finishReorder(true);
        QCOMPARE(keys(model), QStringList({"two", "three", "one"}));
        QCOMPARE(keys(projection), keys(model));
        const auto committed = preferences.value("shell/titlebar/order");
        QVERIFY(committed != saved);
        QVERIFY(projection.beginReorder("one"));
        projection.previewReorder(0);
        QVERIFY(model.unregisterAction(second));
        projection.finishReorder(true);
        QCOMPARE(keys(projection), keys(model));
        QCOMPARE(keys(model), QStringList({"three", "one"}));
        QCOMPARE(preferences.value("shell/titlebar/order"), committed);
        QVERIFY(projection.beginReorder("one"));
        QVERIFY(model.unregisterAction(first));
        projection.previewReorder(0);
        projection.finishReorder(true);
        QCOMPARE(keys(projection), keys(model));
    }

    void playbackFailuresRespectEpochDeduplicationAndActualSkipScheduling()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService player(api, settings);
        RouteController router;
        ShellController shell(settings, router);
        shell.attachPlayback(player);
        QSignalSpy closed(&player, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        auto cleanup = qScopeGuard([&] {
            shell.shutdown();
            player.shutdown();
            api.shutdown();
            const bool drained =
                QTest::qWaitFor([&] { return closed.size() == 1 && apiClosed.size() == 1; }, 15000);
            QVERIFY(drained);
        });
        auto *toasts = shell.toasts();
        player.preferences()->setAutoNext(true);
        PlaybackServiceTestAccess::prepare(player);
        PlaybackServiceTestAccess::fail(player, "网络连接失败");
        PlaybackServiceTestAccess::fail(player, "网络连接失败");
        QTRY_VERIFY(toasts->visible());
        QCOMPARE(toasts->tone(), ToastService::Danger);
        QCOMPARE(toasts->title(), QString("播放失败 · Notification song"));
        QCOMPARE(toasts->message(), QString("网络连接失败\n请检查网络后重试"));
        QCOMPARE(toasts->repetitions(), 1);
        toasts->dismiss(toasts->noticeId());
        PlaybackServiceTestAccess::fail(player, "网络连接失败");
        QCoreApplication::processEvents();
        QVERIFY(!toasts->visible());

        PlaybackServiceTestAccess::prepare(player, 2);
        PlaybackServiceTestAccess::fail(player, "解码失败");
        QTRY_VERIFY(toasts->visible());
        QVERIFY(toasts->message().contains("秒后尝试下一首"));
        toasts->dismiss(toasts->noticeId());
        PlaybackServiceTestAccess::recover(player);

        PlaybackServiceTestAccess::prepare(player);
        PlaybackServiceTestAccess::fail(player, "当前歌曲需要 VIP 权限");
        QTRY_VERIFY(toasts->visible());
        QCOMPARE(toasts->message(), QString("当前歌曲需要 VIP 权限"));
        toasts->dismiss(toasts->noticeId());
        PlaybackServiceTestAccess::recover(player);
        PlaybackServiceTestAccess::fail(player, "当前歌曲需要 VIP 权限");
        QTRY_VERIFY(toasts->visible()); // Recovery allows a new failure on the same song.
        toasts->dismiss(toasts->noticeId());
        PlaybackServiceTestAccess::prepare(player);
        PlaybackServiceTestAccess::fail(player, "decoder: invalid data");
        QTRY_VERIFY(toasts->visible());
        QCOMPARE(toasts->message(), QString("音频解码失败"));
        toasts->dismiss(toasts->noticeId());
        PlaybackServiceTestAccess::prepare(player);
        PlaybackServiceTestAccess::sourceOperation(player);
        PlaybackServiceTestAccess::fail(player, "所选音质需要 VIP 权限");
        PlaybackServiceTestAccess::recover(player); // Continued audio must not swallow the warning.
        QCOMPARE(toasts->tone(), ToastService::Warning);
        QCOMPARE(toasts->title(), QString("音质切换失败 · Notification song"));
        QCOMPARE(toasts->message(), QString("所选音质需要 VIP 权限"));
        toasts->dismiss(toasts->noticeId());
        PlaybackServiceTestAccess::prepare(player);
        PlaybackServiceTestAccess::fail(player, "obsolete error");
        PlaybackServiceTestAccess::prepare(player); // Switch before post-flush dispatch.
        QCoreApplication::processEvents();
        QVERIFY(!toasts->visible());
        PlaybackServiceTestAccess::fail(player, "transient error");
        PlaybackServiceTestAccess::recover(player);
        QCoreApplication::processEvents();
        QVERIFY(!toasts->visible());
        PlaybackServiceTestAccess::fail(player, "shutdown error");
        player.shutdown();
        QCoreApplication::processEvents();
        QVERIFY(!toasts->visible());
    }
    void sharedPlaybackCommandsPublishFeedbackWithoutPageObservers()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService player(api, settings);
        RouteController router;
        ShellController shell(settings, router);
        shell.attachPlayback(player);
        shell.attachPlayback(player); // Reattachment must not double notifications.
        QSignalSpy closed(&player, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        auto cleanup = qScopeGuard([&] {
            shell.shutdown();
            player.shutdown();
            api.shutdown();
            const bool drained =
                QTest::qWaitFor([&] { return closed.size() == 1 && apiClosed.size() == 1; }, 15000);
            QVERIFY(drained);
        });
        auto *toasts = shell.toasts();
        QVERIFY(player.lyricText()->replace(
            {{"lines", QJsonArray{QJsonObject{{"text", "歌词正文"},
                                              {"start_ms", 0},
                                              {"duration_ms", 1000},
                                              {"words", QJsonArray{}}}}}},
            "track"));
        player.lyricText()->adjustTrackOffset(1);
        QCOMPARE(toasts->message(), QString("单曲歌词偏移: +0.1s"));
        QCOMPARE(toasts->repetitions(), 1);
        toasts->dismiss(toasts->noticeId());
        player.lyricText()->resetTrackOffset();
        QCOMPARE(toasts->message(), QString("单曲歌词偏移已重置"));
        toasts->dismiss(toasts->noticeId());
        player.lyricText()->copyLyrics();
        QCOMPARE(toasts->message(), QString("歌词已复制"));
        toasts->dismiss(toasts->noticeId());
        player.sleepTimer()->executionFinished("系统拒绝关机");
        QCOMPARE(toasts->message(), QString("系统拒绝关机"));
        QCOMPARE(toasts->tone(), ToastService::Danger);
        toasts->dismiss(toasts->noticeId());
        player.effectCatalog()->useDownloaded("removed-effect");
        QCOMPARE(toasts->message(), QString("音效文件已移除，请重新下载"));
        QCOMPARE(toasts->tone(), ToastService::Warning);
    }
    void restoreDefaultsRetainsActionIdentity()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        for (const auto surface : {M::Titlebar, M::Playerbar}) {
            M model(settings, surface);
            QObject owner;
            const auto zone = surface == M::Titlebar ? M::Toolbar : M::Center;
            model.registerAction(owner, action("a", zone, 0));
            model.registerAction(owner, action("b", zone, 1));
            model.registerAction(owner, action("c", M::More, 2));
            model.setCapacity(zone, 3);
            ShellActionProjection all;
            all.setActions(&model);
            QAbstractItemModelTester sourceTester(
                &model, QAbstractItemModelTester::FailureReportingMode::QtTest);
            QAbstractItemModelTester proxyTester(
                &all, QAbstractItemModelTester::FailureReportingMode::QtTest);
            QPersistentModelIndex keptSource(model.index(0, 0)), keptProxy(all.index(0, 0));
            QSignalSpy sourceResets(&model, &QAbstractItemModel::modelReset);
            QSignalSpy proxyResets(&all, &QAbstractItemModel::modelReset);
            QSignalSpy proxyLayouts(&all, &QAbstractItemModel::layoutChanged);
            QSignalSpy proxyMoves(&all, &QAbstractItemModel::rowsMoved);
            QVERIFY(model.reorder({"c", "b", "a"}));
            QVERIFY(model.setPlacement("a", M::More));
            QVERIFY(model.setBadgeVisible("a", false));
            QCOMPARE(keptSource.data(M::KeyRole).toString(), QString("a"));
            QCOMPARE(keptProxy.data(M::KeyRole).toString(), QString("a"));
            model.restoreDefaults();
            QCOMPARE(keys(model), (QStringList{"a", "b", "c"}));
            QCOMPARE(keys(all), keys(model));
            QVERIFY(keptSource.isValid() && keptProxy.isValid());
            QCOMPARE(keptSource.data(M::KeyRole).toString(), QString("a"));
            QCOMPARE(keptProxy.data(M::KeyRole).toString(), QString("a"));
            QCOMPARE(keptSource.data(M::PreferredRole).toInt(), int(zone));
            QVERIFY(keptSource.data(M::BadgePreferredRole).toBool());
            model.restoreDefaults(); // Already-default clicks also keep the view intact.
            QCOMPARE(sourceResets.size(), 0);
            QCOMPARE(proxyResets.size(), 0);
            QCOMPARE(proxyLayouts.size(), 0);
            QVERIFY(proxyMoves.size() > 0);
            const auto prefix = surface == M::Titlebar ? "shell/titlebar/" : "shell/playerbar/";
            QVERIFY(settings.value(QString(prefix) + "order").toStringList().isEmpty());
            QVERIFY(settings.value(QString(prefix) + "placements").toMap().isEmpty());
            QVERIFY(settings.value(QString(prefix) + "badges").toMap().isEmpty());
        }
    }
    void overflowPinOrderAndInactivePreferences()
    {
        QTemporaryDir temp;
        EchoMusic::SettingsStore settings(temp.filePath("settings.ini"));
        const QString inactive = "plugin/unloaded/item";
        settings.setValue("shell/titlebar/order", QStringList{"a", inactive, "b", "c"});
        settings.setValue("shell/titlebar/placements", QVariantMap{{inactive, int(M::Toolbar)}});
        M model(settings, M::Titlebar);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        QObject owner;
        const auto a = model.registerAction(owner, action("a")),
                   b = model.registerAction(owner, action("b"));
        model.registerAction(owner, action("c", M::More));
        ShellActionProjection toolbar, more;
        toolbar.setActions(&model);
        toolbar.setZone(M::Toolbar);
        more.setActions(&model);
        QAbstractItemModelTester projectionTester(
            &toolbar, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setCapacity(M::Toolbar, 1);
        QCOMPARE(keys(toolbar), QStringList{"a"});
        QCOMPARE(keys(more), (QStringList{"a", "b", "c"})); // More also manages pinned actions.
        const auto saved = settings.value("shell/titlebar/placements");
        model.setCapacity(M::Toolbar, 0);
        QCOMPARE(toolbar.rowCount(), 0);
        QCOMPARE(settings.value("shell/titlebar/placements"), saved);
        QVERIFY(model.setPlacement("c", M::Toolbar));
        QVERIFY(model.reorder({"c", "a"}));
        QCOMPARE(settings.value("shell/titlebar/order").toStringList(),
                 (QStringList{"c", inactive, "b", "a"}));
        QVERIFY(!model.reorder({"a", "a"}));
        QVERIFY(!model.reorder({"unknown"}));
        QVERIFY(!model.setPlacement("a", M::Left));
        model.setCapacity(M::Toolbar, 2);
        QCOMPARE(keys(toolbar), (QStringList{"c", "b"}));
        QVERIFY(model.setState(b, true, false)); // A hidden action releases capacity.
        QCOMPARE(keys(toolbar), (QStringList{"c", "a"}));
        QVERIFY(model.unregisterAction(a));
        model.registerAction(owner, action("a"));
        QCOMPARE(keys(toolbar), (QStringList{"c", "a"}));
        settings.sync();
        EchoMusic::SettingsStore reopened(temp.filePath("settings.ini"));
        M recreated(reopened, M::Titlebar);
        recreated.registerAction(owner, action("a"));
        recreated.registerAction(owner, action("c", M::More));
        recreated.setCapacity(M::Toolbar, 2);
        ShellActionProjection nextToolbar;
        nextToolbar.setActions(&recreated);
        nextToolbar.setZone(M::Toolbar);
        QCOMPARE(keys(nextToolbar), (QStringList{"c", "a"}));
        recreated.restoreDefaults();
        QCOMPARE(keys(nextToolbar), QStringList{"a"});
        QVERIFY(reopened.value("shell/titlebar/placements").toMap().isEmpty());
    }
    void actionOwnershipGenerationAndReentrantRemoval()
    {
        QTemporaryDir temp;
        EchoMusic::SettingsStore settings(temp.filePath("settings.ini"));
        M model(settings, M::Titlebar);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto owner = std::make_unique<QObject>();
        QObject other;
        int calls = 0;
        auto entry = action("test");
        entry.invoke = [&] { ++calls; };
        const auto old = model.registerAction(*owner, entry);
        const auto fresh = model.registerAction(*owner, entry);
        QVERIFY(fresh.generation != old.generation);
        QVERIFY(!model.unregisterAction(old));
        QVERIFY(!model.setState(old, false));
        QVERIFY(model.registerAction(other, entry).key.isEmpty());
        QVERIFY(model.activate("test"));
        QCOMPARE(calls, 1);
        QVERIFY(model.setState(fresh, true, true, true));
        QVERIFY(!model.activate("test"));
        QVERIFY(model.setState(fresh, true));
        owner.reset();
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.activate("test"));
        M::Handle self;
        entry.invoke = [&] { model.unregisterAction(self); };
        self = model.registerAction(other, entry);
        QVERIFY(model.activate("test"));
        QCOMPARE(model.rowCount(), 0);
    }
    void playerFourRegionsAndSubsetReordering()
    {
        QTemporaryDir temp;
        EchoMusic::SettingsStore settings(temp.filePath("settings.ini"));
        M model(settings, M::Playerbar);
        QObject owner;
        model.registerAction(owner, action("favorite", M::Left, 8));
        model.registerAction(owner, action("previous", M::Center, 3));
        model.registerAction(owner, action("play", M::Center, 4));
        model.registerAction(owner, action("next", M::Center, 5));
        model.registerAction(owner, action("queue", M::Right, 60));
        ShellActionProjection center, overflow;
        center.setActions(&model);
        center.setZone(M::Center);
        overflow.setActions(&model);
        overflow.setZone(M::More);
        model.setCapacity(M::Center, 2);
        model.setCapacity(M::Left, 1);
        QCOMPARE(keys(center), (QStringList{"previous", "play"}));
        QCOMPARE(keys(overflow), (QStringList{"next", "queue"}));
        QVERIFY(model.shiftAction("play", -1, M::Center));
        QCOMPARE(keys(center), (QStringList{"play", "previous"}));
        QVERIFY(!model.shiftAction("next", -1, M::Center));
        QVERIFY(model.moveAfter("play", "next"));
        QCOMPARE(keys(model), (QStringList{"previous", "next", "play", "favorite", "queue"}));
        QVERIFY(model.setPlacement("queue", M::Left));
        QVERIFY(!model.setPlacement("queue", M::Toolbar));
    }
    void typedPlayerProviderUnloadAndBoardPlacement()
    {
        QTemporaryDir temp;
        EchoMusic::SettingsStore settings(temp.filePath("settings.ini"));
        RouteController router;
        ShellController controller(settings, router);
        auto provider = std::make_unique<QObject>();
        int calls = 0;
        const auto handle =
            controller.bindPlayerAction(*provider, ShellController::Quality, [&] { ++calls; });
        QVERIFY(!handle.key.isEmpty());
        auto *model = controller.playerbarActions();
        QVERIFY(!model->activate(handle.key));
        QVERIFY(model->setState(handle, true, true, false, "HD"));
        QVERIFY(model->activate(handle.key));
        QCOMPARE(calls, 1);
        ShellActionProjection right, more;
        right.setActions(model);
        right.setZone(M::Right);
        right.setUsePreferredPlacement(true);
        more.setActions(model);
        more.setZone(M::More);
        more.setUsePreferredPlacement(true);
        QVERIFY(keys(right).contains(handle.key));
        QVERIFY(model->moveInPlayerZone(handle.key, M::More, "host/cast", false));
        QVERIFY(!keys(right).contains(handle.key));
        QVERIFY(keys(more).contains(handle.key));
        QVERIFY(model->shiftInPlayerZone("host/quality", 1));
        QCOMPARE(keys(more), (QStringList{"host/cast", "host/quality"}));
        QVERIFY(!model->moveInPlayerZone(handle.key, M::Toolbar));
        QVERIFY(!model->moveInPlayerZone(handle.key, M::Center, "host/cast"));
        provider.reset();
        QVERIFY(!model->setState(handle, true));
        QVERIFY(!model->activate(handle.key));
        QVERIFY(keys(more).contains(handle.key)); // Availability does not discard manual placement.
        controller.shutdown();
        QVERIFY(!model->activate(handle.key));
    }
    void taskGenerationsOwnersPriorityAndActions()
    {
        TaskCenterModel model;
        QObject owner, other;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto registration = task("one");
        registration.status = TaskCenterModel::Pending;
        const auto old = model.registerTask(owner, registration);
        const auto current = model.registerTask(owner, registration);
        QVERIFY(!model.start(old));
        QVERIFY(!model.remove(old));
        QVERIFY(!model.updateProgress(old, 99));
        QVERIFY(model.registerTask(other, registration).id.isEmpty());
        QVERIFY(model.start(current));
        QVERIFY(!model.start(current));
        auto content = registration.content;
        int detailCalls = 0;
        content.items = {
            {"detail",
             "文件一",
             "说明",
             {},
             "等待",
             {{"run", "重试", TaskAction::Ghost, true, false, [&] { ++detailCalls; }}}}};
        content.actions = {
            {"result", "查看结果", TaskAction::Ghost, true, true, [&] { model.remove(current); }}};
        QVERIFY(model.update(current, content));
        auto *details =
            model.index(0, 0).data(TaskCenterModel::DetailsRole).value<TaskDetailsModel *>();
        QAbstractItemModelTester detailTester(
            details, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy resets(details, &QAbstractItemModel::modelReset);
        QVERIFY(details->expanded());
        details->setExpanded(false);
        QCOMPARE(details->count(), details->rowCount());
        QVERIFY(model.updateProgress(current, 150, "正在导入"));
        QCOMPARE(model.index(0, 0).data(TaskCenterModel::StatusLabelRole).toString(),
                 QString("进行中"));
        QCOMPARE(model.index(0, 0).data(TaskCenterModel::ProgressLabelRole).toString(),
                 QString("正在导入"));
        QCOMPARE(resets.size(), 0);
        QVERIFY(!details->expanded());
        QCOMPARE(model.index(0, 0).data(TaskCenterModel::PercentRole).toDouble(), 100.0);
        QVERIFY(model.updateProgress(current, std::numeric_limits<double>::quiet_NaN()));
        QCOMPARE(model.index(0, 0).data(TaskCenterModel::PercentRole).toDouble(), 0.0);
        QVERIFY(model.updateProgress(current, std::nullopt));
        QCOMPARE(model.index(0, 0).data(TaskCenterModel::PercentRole).toDouble(), -1.0);
        QVERIFY(model.index(0, 0).data(TaskCenterModel::HasProgressRole).toBool());
        model.resetDetailExpansion();
        QVERIFY(details->expanded());
        QVERIFY(!model.invokeAction(current.id, QString::number(old.generation), "run", "detail"));
        QVERIFY(
            model.invokeAction(current.id, QString::number(current.generation), "run", "detail"));
        QCOMPARE(detailCalls, 1);
        auto higher = task("high", 10);
        model.registerTask(owner, higher);
        model.registerTask(owner, task("new"));
        QCOMPARE(model.index(0, 0).data(TaskCenterModel::IdRole).toString(), "high");
        QCOMPARE(model.index(1, 0).data(TaskCenterModel::IdRole).toString(), "new");
        QSignalSpy closes(&model, &TaskCenterModel::closePanelRequested);
        QVERIFY(model.invokeAction(current.id, QString::number(current.generation), "result"));
        QCOMPARE(closes.size(), 1);
        QCOMPARE(model.count(), 2);
    }
    void retentionExpiryAndOwnerTeardown()
    {
        TaskCenterModel model;
        QObject owner;
        const TaskCenterModel::Policy defaults;
        QCOMPARE(defaults.completed.delayMs, 5000);
        QCOMPARE(defaults.aborted.delayMs, 3000);
        QVERIFY(defaults.error.manual);
        auto registration = task("transient");
        registration.policy.completed.delayMs = 40;
        const auto handle = model.registerTask(owner, registration);
        QVERIFY(!model.dismiss(handle.id, QString::number(handle.generation)));
        QVERIFY(model.finish(handle, TaskCenterModel::Completed, registration.content));
        QVERIFY(!model.finish(handle, TaskCenterModel::Error, registration.content));
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 0, 500);
        registration = task("failure");
        const auto error = model.registerTask(owner, registration);
        QVERIFY(model.finish(error, TaskCenterModel::Error, registration.content));
        QVERIFY(model.dismiss(error.id, QString::number(error.generation)));
        registration = task("result");
        registration.policy = TaskCenterModel::Policy::actionRequired();
        const auto result = model.registerTask(owner, registration);
        QVERIFY(model.finish(result, TaskCenterModel::Completed, registration.content));
        QVERIFY(model.dismiss(result.id, QString::number(result.generation)));
        auto temporaryOwner = std::make_unique<QObject>();
        int cancels = 0;
        registration = task("active");
        registration.cancel = [&] { ++cancels; };
        const auto active = model.registerTask(*temporaryOwner, registration);
        temporaryOwner.reset();
        QCOMPARE(model.count(), 0);
        QCOMPARE(cancels, 0); // No callback into a dead owner.
        QVERIFY(!model.remove(active));
        model.registerTask(owner, registration);
        model.shutdown();
        QCOMPARE(cancels, 1);
        QVERIFY(model.registerTask(owner, registration).id.isEmpty());
    }
    void cancellationMayRegisterAReplacement()
    {
        TaskCenterModel model;
        QObject owner;
        auto original = task("same");
        original.cancel = [&] { model.registerTask(owner, task("same")); };
        model.registerTask(owner, original);
        QVERIFY(model.registerTask(owner, task("same")).id.isEmpty());
        QCOMPARE(model.count(), 1);
    }
    void providerFailureStillClosesAndDrainsTasks()
    {
        TaskCenterModel model;
        QObject owner;
        auto registration = task("failed-provider");
        registration.cancel = [] { throw 1; };
        registration.content.actions.push_back(
            {"throw", "查看结果", TaskAction::Ghost, true, true, [] { throw 1; }});
        const auto handle = model.registerTask(owner, registration);
        QSignalSpy closes(&model, &TaskCenterModel::closePanelRequested);
        QTest::ignoreMessage(QtWarningMsg, "Task action callback failed");
        QVERIFY(model.invokeAction(handle.id, QString::number(handle.generation), "throw"));
        QCOMPARE(closes.size(), 1);
        model.registerTask(owner, task("another"));
        QTest::ignoreMessage(QtWarningMsg, "Task cancellation callback failed");
        model.shutdown();
        QCOMPARE(model.count(), 0);
    }
    void anchorTracksAncestorTransformsReparentingAndDestruction()
    {
        QQuickWindow window;
        QQuickItem left(window.contentItem()), right(window.contentItem());
        left.setX(230);
        left.setScale(1.25);
        right.setX(80);
        auto button = std::make_unique<QQuickItem>(&left);
        button->setX(20);
        button->setY(10);
        button->setSize({34, 34});
        EchoPopupAnchor anchor;
        anchor.setItem(button.get());
        anchor.setCoordinateItem(window.contentItem());
        QVERIFY(anchor.available());
        QCOMPARE(anchor.rect(), QRectF(255, 12.5, 42.5, 42.5));
        QSignalSpy geometry(&anchor, &EchoPopupAnchor::geometryChanged);
        left.setX(80);
        QVERIFY(!geometry.isEmpty());
        QCOMPARE(anchor.rect().x(), 105.0);
        button->setParentItem(&right);
        QCOMPARE(anchor.rect(), QRectF(100, 10, 34, 34));
        right.setVisible(false);
        QVERIFY(!anchor.available());
        right.setVisible(true);
        QVERIFY(anchor.available());
        button.reset();
        QVERIFY(!anchor.available());
        QVERIFY(anchor.rect().isEmpty());
    }
};
QTEST_MAIN(ShellActionTest)
#include "ShellActionTest.moc"
