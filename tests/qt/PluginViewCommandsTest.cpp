#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(EchoMusic_ThemesPlugin)
#include "EchoPluginViewContext.h"
#include "plugins/PluginRuntimeTestFixture.h"
#include "PlaybackRepository.h"
#include <limits>

class PluginViewCommandsTest : public QObject
{
    Q_OBJECT
private slots:
    void closeSettingsUsesActivePresentation();
    void volumeCommandsRejectNonNumericValues();
    void shuffleReordersTheQueueInsteadOfChangingPlayMode();
    void queueProjectionFollowsTheActualActiveSource();
    void queueShuffleMatchesTheSeededFisherYatesAndRejectsDynamicSources();
    void deferredCommandsCannotWriteAfterCancellationOrScopeExit();
    void previewsAreExclusiveAndAccountChangeRestoresCommittedVolume();
    void sleepDeadlineUses64BitMillisecondsAndRejectsOverflow();
    void navigationCommandsUseTheActualQueueAndSource();
    void volumePreviewRestoresOnRetireAndCommitPersists();
    void volumePreviewCannotOverrideAnExternalChange();
    void sleepTimerAcceptsTheFullSourceDurationAndRejectsInvalidValues();
    void closeSettingsCompletesBeforeItsOwnPresentationRetires();
    void queueNextUsesNativeLocalRowAndExistingPriority();
    void saveQueueCopiesTheActiveNativeQueueAndRestoresFromSqlite();
    void nativeQueueCommandsRejectInvalidArguments();
    void nativeQueueCommandsRejectLateAuthority_data();
    void nativeQueueCommandsRejectLateAuthority();
    void queueNextCannotRedirectDynamicSourceIntoAnotherQueue();
    void savedLocalQueuesKeepTheExistingFourQueueCapacity();
    void transitionSourceCommandRejectsInvalidArgumentsAndExpiredProjection();
};
void PluginViewCommandsTest::closeSettingsUsesActivePresentation()
{
    Host host;
    host.runtime.importDirectory(
        package(host.temporary.path(), "export function activate(ctx) {}\n"));
    QString installation;
    QTRY_VERIFY(!(installation = host.runtime.installationForId("test.runtime")).isEmpty());
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *scope = host.runtime.context(installation);
    QObject owner;
    owner.setProperty("active", true);
    auto *presentation = scope->createPresentation(&owner, "skin.settings");
    QVERIFY(presentation);
    EchoPluginViewContext page(*presentation, "skin.settings", &owner);
    QSignalSpy requests(host.theme.extensions(), &AppearanceCatalog::requestRaised);
    QPointer<EchoPluginOperation> operation = page.closeSkinSettings();
    QJSValue handle = host.engine.newQObject(operation);
    QTRY_COMPARE(operation->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(requests.count(), 1);
    QCOMPARE(requests.at(0).at(0).toString(), QString("closeSkinSettings"));
    owner.setProperty("active", false);
    auto *rejected = page.closeSkinSettings();
    const auto rejectedHandle = host.engine.newQObject(rejected);
    QCOMPARE(rejected->state(), EchoPluginOperation::Failed);
    QCOMPARE(rejected->errorCode(), QString("ScopeClosed"));
    QCOMPARE(requests.count(), 1);
}
void PluginViewCommandsTest::closeSettingsCompletesBeforeItsOwnPresentationRetires()
{
    Host host;
    host.runtime.importDirectory(
        package(host.temporary.path(), "export function activate(ctx) {}\n"));
    QString installation;
    QTRY_VERIFY(!(installation = host.runtime.installationForId("test.runtime")).isEmpty());
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *scope = host.runtime.context(installation);
    QObject owner;
    owner.setProperty("active", true);
    auto *presentation = scope->createPresentation(&owner, "skin.settings");
    QVERIFY(presentation);
    EchoPluginViewContext page(*presentation, "skin.settings", &owner);
    // A zero-duration drawer closes and revokes its content in requestRaised.
    connect(host.theme.extensions(), &AppearanceCatalog::requestRaised, &owner,
            [&owner](const QString &action) {
                if (action == "closeSkinSettings")
                    owner.setProperty("active", false);
            });
    auto *operation = page.closeSkinSettings();
    const auto handle = host.engine.newQObject(operation);
    int deliveredState = -1;
    connect(operation, &EchoPluginOperation::completed, &owner,
            [operation, &deliveredState] { deliveredState = operation->state(); });
    QTRY_VERIFY(deliveredState >= 0);
    QCOMPARE(deliveredState, int(EchoPluginOperation::Succeeded));
    QVERIFY(!page.active());
}

namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static PlaybackRepository *persistThenRead(PlaybackService &playback, quint64 revision)
    {
        playback.m_saveTimer.stop();
        playback.persist();
        playback.m_repository->load(PlaybackRepository::SharedQueueScope, revision);
        return playback.m_repository.get();
    }
    static void seed(PlaybackService &playback, int current = 2, const QString &source = "default")
    {
        PlaybackTracks tracks;
        for (int i = 0; i < 12; ++i) {
            PlaybackTrack track;
            track.id = track.title = QString::number(i + 1);
            track.localPath = "/fixture/" + track.id;
            tracks.append(track);
        }
        playback.m_catalog.activate(source, "Fixture", tracks, current);
        playback.m_track = tracks[current];
        playback.m_sourceKey = source;
        playback.m_ready = false;
    }
};
} // namespace EchoMusic
namespace
{
struct CommandsFixture
{
    Host host;
    QObject owner;
    EchoPluginContext *scope = nullptr;
    std::unique_ptr<EchoPluginViewContext> page;
    QJSValueList retained;
    bool activate()
    {
        host.runtime.importDirectory(
            package(host.temporary.path(), "export function activate(ctx) {}\n"));
        QElapsedTimer deadline;
        deadline.start();
        while (deadline.elapsed() < 7000) {
            const auto installation = host.runtime.installationForId("test.runtime");
            if (!installation.isEmpty() &&
                host.runtime.state(installation) == PluginRuntime::Active) {
                owner.setProperty("active", true);
                scope =
                    host.runtime.context(installation)->createPresentation(&owner, "lyricsPage");
                page = std::make_unique<EchoPluginViewContext>(*scope, "lyricsPage", &owner);
                return true;
            }
            QTest::qWait(1);
        }
        return false;
    }
    EchoPluginOperation *issue(const QString &action, const QVariantMap &arguments = {})
    {
        auto *operation = page->command(action, arguments);
        retained.append(host.engine.newQObject(operation));
        return operation;
    }
};
QStringList queueIds(PlaybackQueueModel &queue)
{
    QStringList ids;
    for (const auto &track : queue.tracks())
        ids.append(track.id);
    return ids;
}
} // namespace
void PluginViewCommandsTest::shuffleReordersTheQueueInsteadOfChangingPlayMode()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    PlaybackServiceTestAccess::seed(f.host.playback);
    auto *queue = f.host.playback.queue();
    const auto current = f.host.playback.trackId();
    const auto before = queueIds(*queue);
    queue->restoreNext({queue->at(7)->key(), queue->at(8)->key()});
    const auto mode = f.host.playback.playMode();
    const auto revision = f.host.playback.sourceRevision();
    auto *operation = f.issue("shuffleQueue");
    QTRY_VERIFY(operation->finished());
    QCOMPARE(operation->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(queue->at(0)->id, current);
    QCOMPARE(queue->currentIndex(), 0);
    auto expected = before;
    auto actual = queueIds(*queue);
    expected.sort();
    actual.sort();
    QCOMPARE(actual, expected);
    QVERIFY(queue->nextKeys().isEmpty());
    QCOMPARE(f.host.playback.trackId(), current);
    QCOMPARE(f.host.playback.sourceRevision(), revision);
    QCOMPARE(f.host.playback.playMode(), mode);
}
void PluginViewCommandsTest::queueProjectionFollowsTheActualActiveSource()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    QCOMPARE(f.page->player()->queue()->rowCount(), 0);
    PlaybackServiceTestAccess::seed(f.host.playback, 3, "queue:album:12");
    QCOMPARE(f.page->player()->queue()->rowCount(), 12);
    QCOMPARE(f.page->player()->queue()->row(3).value("title").toString(), QString("4"));
    QVERIFY(f.page->player()->queue()->row(3).value("isCurrent").toBool());
}
void PluginViewCommandsTest::queueNextUsesNativeLocalRowAndExistingPriority()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    PlaybackServiceTestAccess::seed(f.host.playback);
    QVERIFY(!f.scope->package().capabilities.contains("audioSource"));
    auto *queue = f.host.playback.queue();
    const auto selected = *queue->at(7);
    QVERIFY(selected.hash.isEmpty());
    QVERIFY(!selected.localPath.isEmpty());
    const auto current = f.host.playback.currentTrack();
    const auto revision = f.host.playback.sourceRevision();
    auto *operation = f.issue("queueNext", {{"index", 7}});
    QTRY_VERIFY(operation->finished());
    QCOMPARE(operation->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(queue->rowCount(), 12);
    QCOMPARE(queue->at(queue->currentIndex() + 1)->key(), selected.key());
    QCOMPARE(queue->nextKeys(), QStringList{selected.key()});
    QCOMPARE(queue->at(queue->peekNextPriority())->key(), selected.key());
    QVERIFY(f.page->player()->queue()->row(queue->currentIndex() + 1).value("queuedNext").toBool());
    QCOMPARE(f.host.playback.currentTrack(), current);
    QCOMPARE(f.host.playback.sourceRevision(), revision);
    QVERIFY(!f.host.playback.loading());
    QVERIFY(operation->result().isNull());
    auto *repeat = f.scope->player()->control("queueNext", {{"index", queue->currentIndex() + 1}});
    f.retained.append(f.host.engine.newQObject(repeat));
    QTRY_VERIFY(repeat->finished());
    QCOMPARE(repeat->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(queue->nextKeys(), QStringList{selected.key()});
}
void PluginViewCommandsTest::saveQueueCopiesTheActiveNativeQueueAndRestoresFromSqlite()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    PlaybackServiceTestAccess::seed(f.host.playback, 2, "default");
    auto *catalog = f.host.playback.queueCatalog();
    auto *source = f.host.playback.queue();
    const auto tracks = source->tracks();
    QVERIFY(tracks.first().hash.isEmpty());
    QVERIFY(!tracks.first().localPath.isEmpty());
    catalog->retain("preview-fixture", "Preview, not active", {tracks.last()});
    catalog->setViewIndex(catalog->rowCount() - 1);
    QVERIFY(catalog->viewModel() != source);
    const auto active = catalog->activeKey();
    auto *operation = f.issue("saveQueue", {{"title", QString("  My local queue  ")}});
    QTRY_VERIFY(operation->finished());
    QCOMPARE(operation->state(), EchoPluginOperation::Succeeded);
    const auto result = operation->result().toMap();
    const auto key = result.value("queueKey").toString();
    QVERIFY(!key.isEmpty());
    QVERIFY(key != active);
    QVERIFY(!key.contains(tracks.first().localPath));
    QCOMPARE(result.value("title").toString(), QString("My local queue"));
    QCOMPARE(result.value("count").toInt(), tracks.size());
    auto *saved = catalog->modelForKey(key);
    QVERIFY(saved);
    QVERIFY(saved != source);
    QCOMPARE(saved->tracks(), tracks);
    QCOMPARE(catalog->titleForKey(key), QString("My local queue"));
    QCOMPARE(catalog->activeKey(), active);
    QCOMPARE(f.host.playback.queue(), source);
    QCOMPARE(catalog->viewTitle(), QString("Preview, not active"));
    QVERIFY(!f.host.playback.playing());
    auto *repository = PlaybackServiceTestAccess::persistThenRead(f.host.playback, 4321);
    QSignalSpy loaded(repository, &PlaybackRepository::loaded);
    QTRY_VERIFY(!loaded.isEmpty());
    const auto states = qvariant_cast<PlaybackQueueStates>(loaded.last().at(2));
    const auto found = std::find_if(states.cbegin(), states.cend(),
                                    [&key](const auto &state) { return state.key == key; });
    QVERIFY(found != states.cend());
    QCOMPARE(found->title, QString("My local queue"));
    QCOMPARE(found->tracks, tracks);
    PlaybackQueueCatalog restored(nullptr);
    restored.restore(states, loaded.last().at(3).toString(), loaded.last().at(4).toString());
    QVERIFY(restored.modelForKey(key));
    QCOMPARE(restored.modelForKey(key)->tracks(), tracks);
    QCOMPARE(restored.titleForKey(key), QString("My local queue"));
}
void PluginViewCommandsTest::nativeQueueCommandsRejectInvalidArguments()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    PlaybackServiceTestAccess::seed(f.host.playback);
    const QVariantList invalidIndices{QVariant{},
                                      true,
                                      QString("1"),
                                      -1,
                                      12,
                                      1.5,
                                      std::numeric_limits<double>::infinity(),
                                      std::numeric_limits<double>::quiet_NaN(),
                                      QVariantList{1}};
    for (const auto &index : invalidIndices) {
        auto *op = f.issue("queueNext", {{"index", index}});
        QTRY_VERIFY(op->finished());
        QCOMPARE(op->state(), EchoPluginOperation::Failed);
        QCOMPARE(op->errorCode(), QString("InvalidArgument"));
    }
    const QVariantList invalidTitles{
        QVariant{}, true, 1, QString(), QString(" \t "), QString("a\nb"), QString(129, 'x')};
    for (const auto &title : invalidTitles) {
        auto *op = f.issue("saveQueue", {{"title", title}});
        QTRY_VERIFY(op->finished());
        QCOMPARE(op->state(), EchoPluginOperation::Failed);
        QCOMPARE(op->errorCode(), QString("InvalidArgument"));
    }
    for (const auto &action : {QString("queueNext"), QString("saveQueue")}) {
        const auto key = action == "queueNext" ? "index" : "title";
        const auto value = action == "queueNext" ? QVariant(1) : QVariant("Valid");
        auto *op = f.issue(action, {{key, value}, {"tracks", QVariantList{}}});
        QTRY_VERIFY(op->finished());
        QCOMPARE(op->state(), EchoPluginOperation::Failed);
        QCOMPARE(op->errorCode(), QString("InvalidArgument"));
    }
    QCOMPARE(f.host.playback.queueCatalog()->snapshot().size(), 1);
    QVERIFY(f.host.playback.queue()->nextKeys().isEmpty());
}
void PluginViewCommandsTest::nativeQueueCommandsRejectLateAuthority_data()
{
    QTest::addColumn<QString>("action");
    QTest::addColumn<QString>("change");
    for (const auto &action : {QString("queueNext"), QString("saveQueue")})
        for (const auto &change : {QString("cancel"), QString("owner"), QString("account"),
                                   QString("reorder"), QString("source")})
            QTest::newRow(qPrintable(action + '/' + change)) << action << change;
}
void PluginViewCommandsTest::nativeQueueCommandsRejectLateAuthority()
{
    QFETCH(QString, action);
    QFETCH(QString, change);
    CommandsFixture f;
    QVERIFY(f.activate());
    PlaybackServiceTestAccess::seed(f.host.playback);
    auto *source = f.host.playback.queue();
    const auto tracks = source->tracks();
    auto *op = f.issue(action, action == "queueNext" ? QVariantMap{{"index", 7}}
                                                     : QVariantMap{{"title", "Must not save"}});
    if (change == "cancel")
        op->cancel();
    else if (change == "owner")
        f.owner.setProperty("active", false);
    else if (change == "account")
        f.host.api.advanceSession();
    else if (change == "reorder")
        source->move(0, 1);
    else
        f.host.playback.queueCatalog()->activate("alternate", "Alternate", tracks, 2);
    const auto count = f.host.playback.queueCatalog()->rowCount();
    QTRY_VERIFY(op->finished());
    if (change == "reorder" || change == "source") {
        QCOMPARE(op->state(), EchoPluginOperation::Failed);
        QCOMPARE(op->errorCode(), QString("StaleQueue"));
    } else
        QCOMPARE(op->state(), EchoPluginOperation::Canceled);
    QCOMPARE(f.host.playback.queueCatalog()->rowCount(), count);
    QVERIFY(source->nextKeys().isEmpty());
    QVERIFY(f.host.playback.queue()->nextKeys().isEmpty());
}
void PluginViewCommandsTest::queueNextCannotRedirectDynamicSourceIntoAnotherQueue()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    for (const auto &key : {PersonalFmController::queueKey(), DiscoverQueueController::queueKey(),
                            QString("queue:listen-together")}) {
        PlaybackServiceTestAccess::seed(f.host.playback, 2, key);
        auto *op = f.issue("queueNext", {{"index", 7}});
        QTRY_VERIFY(op->finished());
        QCOMPARE(op->state(), EchoPluginOperation::Failed);
        QCOMPARE(op->errorCode(), QString("Unavailable"));
        for (const auto &state : f.host.playback.queueCatalog()->snapshot())
            QVERIFY(state.next.isEmpty());
    }
}
void PluginViewCommandsTest::savedLocalQueuesKeepTheExistingFourQueueCapacity()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    PlaybackServiceTestAccess::seed(f.host.playback);
    auto *source = f.host.playback.queue();
    QString first;
    for (int index = 0; index < 6; ++index) {
        auto *op = f.issue("saveQueue", {{"title", "Duplicate title"}});
        QTRY_COMPARE(op->state(), EchoPluginOperation::Succeeded);
        const auto key = op->result().toMap().value("queueKey").toString();
        QVERIFY(!key.isEmpty());
        QVERIFY(key != first);
        if (index == 0)
            first = key;
        QCOMPARE(f.host.playback.queue(), source);
        QVERIFY(f.host.playback.queueCatalog()->snapshot().size() <= 4);
    }
    QVERIFY(!f.host.playback.queueCatalog()->modelForKey(first));
}
void PluginViewCommandsTest::transitionSourceCommandRejectsInvalidArgumentsAndExpiredProjection()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    PlaybackServiceTestAccess::seed(f.host.playback);
    const QVariantMap valid{
        {"epoch", "1"}, {"sequence", "2"}, {"positionMs", 0}, {"playing", false}};
    const auto rejected = [&f](const QVariantMap &args) {
        auto *op = f.issue("selectTransitionSource", args);
        QTRY_VERIFY(op->finished());
        QCOMPARE(op->state(), EchoPluginOperation::Failed);
        QCOMPARE(op->errorCode(), QString("InvalidArgument"));
    };
    for (const auto &field : {QString("epoch"), QString("sequence")})
        for (const auto &value :
             QVariantList{QVariant{}, true, 1, QString(), QString("01"), QString("-1"),
                          QString("1.0"), QString("18446744073709551616")}) {
            auto args = valid;
            args[field] = value;
            rejected(args);
        }
    for (const auto &value : QVariantList{QVariant{}, true, QString("10"), -1, 1.5, 1000000000001.,
                                          std::numeric_limits<double>::infinity(),
                                          std::numeric_limits<double>::quiet_NaN()}) {
        auto args = valid;
        args["positionMs"] = value;
        rejected(args);
    }
    for (const auto &value : QVariantList{QVariant{}, 0, QString("false")}) {
        auto args = valid;
        args["playing"] = value;
        rejected(args);
    }
    auto extra = valid;
    extra["revision"] = "Not a transition authority";
    rejected(extra);
    auto missing = valid;
    missing.remove("sequence");
    rejected(missing);
    auto *expired = f.issue("selectTransitionSource", valid);
    QTRY_VERIFY(expired->finished());
    QCOMPARE(expired->state(), EchoPluginOperation::Failed);
    QCOMPARE(expired->errorCode(), QString("StaleTransition"));
    auto *cancel = f.issue("selectTransitionSource", valid);
    cancel->cancel();
    QTest::qWait(10);
    QCOMPARE(cancel->state(), EchoPluginOperation::Canceled);
    QCOMPARE(f.host.playback.currentTrack().title, QString("3"));
    QVERIFY(!f.host.playback.loading());
}
void PluginViewCommandsTest::navigationCommandsUseTheActualQueueAndSource()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    f.host.shell.attachPlayback(f.host.playback);
    PlaybackServiceTestAccess::seed(f.host.playback);
    QSignalSpy popups(&f.host.shell, &ShellController::playbackPopupRequested);
    auto *wall = f.issue("songWall");
    QTRY_VERIFY(wall->finished());
    QCOMPARE(wall->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(popups.size(), 1);
    QCOMPARE(popups.first().first().toString(), QString("song-wall"));
    auto *unavailable = f.issue("locateTrack");
    QTRY_VERIFY(unavailable->finished());
    QCOMPARE(unavailable->state(), EchoPluginOperation::Failed);
    QCOMPARE(unavailable->errorCode(), QString("Unavailable"));
    f.host.router.registerPage(
        {"album-detail", "/album/:id", QUrl("qrc:/album.qml"), {}, false, true, false, {}});
    PlaybackServiceTestAccess::seed(f.host.playback, 2, "queue:album:12");
    auto *locate = f.issue("locateTrack");
    QTRY_VERIFY(locate->finished());
    QCOMPARE(locate->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(f.host.router.current()->name(), QString("album-detail"));
    QCOMPARE(f.host.router.current()->resourceId(), QString("12"));
    QCOMPARE(f.host.router.current()->query().value("locateTrack").toString(),
             f.host.playback.trackId());
    QVERIFY(!f.host.router.current()->query().value("locateToken").toString().isEmpty());
}
void PluginViewCommandsTest::queueShuffleMatchesTheSeededFisherYatesAndRejectsDynamicSources()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    PlaybackServiceTestAccess::seed(f.host.playback);
    auto *queue = f.host.playback.queue();
    auto expected = queue->tracks();
    const auto current = expected.takeAt(queue->currentIndex());
    QRandomGenerator sourceRandom(42), nativeRandom(42);
    // Independent source oracle: shuffle the remainder, then prepend current.
    for (int i = expected.size() - 1; i > 0; --i) {
        const int random = sourceRandom.bounded(quint32(i + 1));
        std::swap(expected[i], expected[random]);
    }
    expected.prepend(current);
    QVERIFY(queue->shuffle(nativeRandom));
    QCOMPARE(queue->tracks(), expected);
    for (const auto &source :
         {PersonalFmController::queueKey(), DiscoverQueueController::queueKey(),
          QString("queue:listen-together")}) {
        PlaybackServiceTestAccess::seed(f.host.playback, 2, source);
        const auto before = queueIds(*f.host.playback.queue());
        auto *operation = f.issue("shuffleQueue");
        QTRY_VERIFY(operation->finished());
        QCOMPARE(operation->state(), EchoPluginOperation::Failed);
        QCOMPARE(operation->errorCode(), QString("Unavailable"));
        QCOMPARE(queueIds(*f.host.playback.queue()), before);
    }
}
void PluginViewCommandsTest::deferredCommandsCannotWriteAfterCancellationOrScopeExit()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    f.host.playback.setVolume(37);
    auto *preview = f.issue("volumePreview", {{"value", 14.}});
    preview->cancel();
    QTest::qWait(20);
    QCOMPARE(f.host.playback.volume(), 37.);
    QCOMPARE(f.host.settings.value("playback/volume").toDouble(), 37.);
    preview = f.issue("volumePreview", {{"value", 14.}});
    f.owner.setProperty("active", false);
    QTest::qWait(20);
    QCOMPARE(preview->state(), EchoPluginOperation::Canceled);
    QCOMPARE(f.host.playback.volume(), 37.);
}
void PluginViewCommandsTest::previewsAreExclusiveAndAccountChangeRestoresCommittedVolume()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    f.host.playback.setVolume(37);
    auto *preview = f.issue("volumePreview", {{"value", 14.}});
    QTRY_COMPARE(preview->state(), EchoPluginOperation::Succeeded);
    QObject other;
    QVERIFY(!f.host.playback.previewVolume(&other, 62));
    QCOMPARE(f.host.playback.volume(), 14.);
    f.host.api.advanceSession();
    QCOMPARE(f.host.playback.volume(), 37.);
    QCOMPARE(f.host.settings.value("playback/volume").toDouble(), 37.);
    // An activation-scope control is also retired by account epoch, not only a drawer.
    auto *activation = f.host.runtime.context(f.host.installation());
    QVERIFY(activation);
    preview = activation->player()->control("volumePreview", {{"value", 21.}});
    f.retained.append(f.host.engine.newQObject(preview));
    QTRY_COMPARE(preview->state(), EchoPluginOperation::Succeeded);
    f.host.api.advanceSession();
    QCOMPARE(f.host.playback.volume(), 37.);
    QCOMPARE(f.host.settings.value("playback/volume").toDouble(), 37.);
}
void PluginViewCommandsTest::sleepDeadlineUses64BitMillisecondsAndRejectsOverflow()
{
    qint64 now = 100000;
    SleepTimer timer(nullptr, [&now] { return now; });
    QVERIFY(timer.start(59999, "pause"));
    QCOMPARE(timer.remainingSeconds(), 3599940);
    now += qint64(59999) * 60000 - 1;
    QVERIFY(!timer.tick("fixture", true));
    QCOMPARE(timer.remainingSeconds(), 1);
    ++now;
    QVERIFY(timer.tick("fixture", true));
    timer.executionFinished();
    now = std::numeric_limits<qint64>::max() - 100;
    QVERIFY(!timer.start(1, "pause"));
    QVERIFY(!timer.active());
}
void PluginViewCommandsTest::volumeCommandsRejectNonNumericValues()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    f.host.playback.setVolume(37);
    const QVariantList invalid{
        QVariant{},      true, QString("14"), QByteArray("14"), QVariantMap{{"value", 14}},
        QVariantList{14}};
    for (const auto &action :
         {QString("volumePreview"), QString("volumeCommit"), QString("volume")})
        for (const auto &value : invalid) {
            auto *operation = f.issue(action, {{"value", value}});
            QTRY_VERIFY(operation->finished());
            QCOMPARE(operation->state(), EchoPluginOperation::Failed);
            QCOMPARE(operation->errorCode(), QString("InvalidArgument"));
            QCOMPARE(f.host.playback.volume(), 37.);
            QCOMPARE(f.host.settings.value("playback/volume").toDouble(), 37.);
            auto *control = f.scope->player()->control(action, {{"value", value}});
            f.retained.append(f.host.engine.newQObject(control));
            QTRY_VERIFY(control->finished());
            QCOMPARE(control->state(), EchoPluginOperation::Failed);
            QCOMPARE(control->errorCode(), QString("InvalidArgument"));
            QCOMPARE(f.host.playback.volume(), 37.);
        }
}
void PluginViewCommandsTest::volumePreviewRestoresOnRetireAndCommitPersists()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    f.host.playback.setVolume(37);
    auto *preview = f.issue("volumePreview", {{"value", 14.}});
    QTRY_VERIFY(preview->finished());
    QCOMPARE(preview->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(f.host.playback.volume(), 14.);
    QCOMPARE(f.host.settings.value("playback/volume").toDouble(), 37.);
    auto *commit = f.issue("volumeCommit", {{"value", 14.}});
    QTRY_VERIFY(commit->finished());
    QCOMPARE(commit->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(f.host.settings.value("playback/volume").toDouble(), 14.);
    preview = f.issue("volumePreview", {{"value", 62.}});
    QTRY_COMPARE(preview->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(f.host.playback.volume(), 62.);
    f.owner.setProperty("active", false);
    QCOMPARE(f.host.playback.volume(), 14.);
    QCOMPARE(f.host.settings.value("playback/volume").toDouble(), 14.);
}
void PluginViewCommandsTest::volumePreviewCannotOverrideAnExternalChange()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    f.host.playback.setVolume(37);
    auto *preview = f.issue("volumePreview", {{"value", 14.}});
    QTRY_VERIFY(preview->finished());
    QCOMPARE(preview->state(), EchoPluginOperation::Succeeded);
    f.host.playback.setVolume(71);
    auto *commit = f.issue("volumeCommit", {{"value", 14.}});
    QTRY_VERIFY(commit->finished());
    QCOMPARE(commit->state(), EchoPluginOperation::Failed);
    QCOMPARE(commit->errorCode(), QString("Unavailable"));
    f.owner.setProperty("active", false);
    QCOMPARE(f.host.playback.volume(), 71.);
    QCOMPARE(f.host.settings.value("playback/volume").toDouble(), 71.);
}
void PluginViewCommandsTest::sleepTimerAcceptsTheFullSourceDurationAndRejectsInvalidValues()
{
    CommandsFixture f;
    QVERIFY(f.activate());
    auto *operation = f.issue("sleepTimerStart", {{"value", 59999.}, {"action", "pause"}});
    QTRY_VERIFY(operation->finished());
    QCOMPARE(operation->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(f.host.playback.sleepTimer()->durationMinutes(), 59999);
    QVERIFY(f.host.playback.sleepTimer()->remainingSeconds() >= 3599939);
    const auto snapshot = f.page->snapshot().value("sleepTimer").toMap();
    QCOMPARE(snapshot.value("durationMinutes").toLongLong(), qint64(59999));
    QVERIFY(snapshot.value("active").toBool());
    for (const auto &value : QVariantList{0, -1, 60000, 1.25, QString("30"), true}) {
        auto *invalid = f.issue("sleepTimerStart", {{"value", value}, {"action", "pause"}});
        QTRY_VERIFY(invalid->finished());
        QCOMPARE(invalid->state(), EchoPluginOperation::Failed);
        QCOMPARE(invalid->errorCode(), QString("InvalidArgument"));
        QCOMPARE(f.host.playback.sleepTimer()->durationMinutes(), 59999);
    }
    operation = f.issue("sleepTimerCancel");
    QTRY_COMPARE(operation->state(), EchoPluginOperation::Succeeded);
    QVERIFY(!f.host.playback.sleepTimer()->active());
}

QTEST_MAIN(PluginViewCommandsTest)
#include "PluginViewCommandsTest.moc"
