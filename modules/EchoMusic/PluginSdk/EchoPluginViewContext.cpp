#include "EchoPluginViewContext.h"
#include "EchoPluginContext.h"
#include "EchoPluginPlaybackTransition.h"
#include "ThemeStore.h"
#include "AppearanceHost.h"
#include "PlaybackService.h"
#include "ShellController.h"
#include "FloatingPlayerController.h"
#include "WindowController.h"
#include <QMetaProperty>
#include <QTimer>
#include <QUuid>
#include <limits>
#include <cmath>
namespace EchoMusic
{
EchoPluginViewContext::EchoPluginViewContext(EchoPluginContext &s, QString surface, QObject *owner)
    : QObject(owner ? owner : &s), m_scope(&s), m_owner(owner), m_surface(std::move(surface)),
      m_epoch(s.accountEpoch()), m_visualScoped(owner != nullptr)
{
    connect(s.player(), &EchoPluginPlayer::changed, this, &EchoPluginViewContext::changed);
    connect(s.player(), &EchoPluginPlayer::positionChanged, this, &EchoPluginViewContext::changed);
    connect(&s, &EchoPluginContext::activeChanged, this, &EchoPluginViewContext::changed);
    if (owner)
        if (auto *host = qobject_cast<AppearanceHost *>(owner->parent()))
            connect(host, &AppearanceHost::presentationClosing, this,
                    [this, owner](QObject *closing) {
                        if (closing == owner && active())
                            emit aboutToClose();
                    });
}
bool EchoPluginViewContext::active() const
{
    return m_scope && m_scope->active() && m_scope->accountEpoch() == m_epoch &&
           (!m_visualScoped || (m_owner && m_owner->property("active").toBool()));
}
EchoPluginPlayer *EchoPluginViewContext::player() const
{
    return m_scope ? m_scope->player() : nullptr;
}
QVariantMap EchoPluginViewContext::snapshot() const
{
    return active() ? m_scope->player()->snapshot() : QVariantMap();
}
EchoPluginOperation *EchoPluginViewContext::closeSkinSettings()
{
    return command("closeSkinSettings");
}
EchoPluginOperation *EchoPluginViewContext::closeSongWall()
{
    return command("closeSongWall");
}
EchoPluginOperation *EchoPluginViewContext::command(const QString &action, const QVariantMap &args)
{
    if (!m_scope)
        return nullptr;
    if (!active())
        return m_scope->rejected("Unavailable", "Presentation has expired");
    if (action == "closeSongWall" && m_surface != "queue-wall")
        return m_scope->rejected("Unavailable", "Only a song wall may close its presentation");
    if (action == "selectTransitionSource") {
        static const QStringList fields{"epoch", "sequence", "positionMs", "playing"};
        if (args.size() != fields.size() ||
            std::any_of(fields.cbegin(), fields.cend(),
                        [&args](const auto &field) { return !args.contains(field); }))
            return m_scope->rejected("InvalidArgument", "Invalid transition transport arguments");
        const auto decimal = [](const QVariant &value) {
            if (value.metaType().id() != QMetaType::QString)
                return false;
            const auto text = value.toString();
            if (text.isEmpty() || text.size() > 20)
                return false;
            bool valid = false;
            const auto number = text.toULongLong(&valid);
            return valid && QString::number(number) == text;
        };
        const auto value = args.value("positionMs");
        const auto type = value.metaType().id();
        const double position = value.toDouble();
        if (!decimal(args.value("epoch")) || !decimal(args.value("sequence")) ||
            args.value("playing").metaType().id() != QMetaType::Bool ||
            (type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::LongLong &&
             type != QMetaType::ULongLong && type != QMetaType::Double) ||
            !std::isfinite(position) || position < 0 || position > 1e12 ||
            position != std::floor(position))
            return m_scope->rejected("InvalidArgument", "Invalid transition source or transport");
        const auto epoch = args.value("epoch").toString();
        const auto sequence = args.value("sequence").toString();
        const auto account = m_scope->accountEpoch();
        const bool playing = args.value("playing").toBool();
        auto *op = m_scope->operation();
        if (op->finished())
            return op;
        op->start();
        connect(this, &QObject::destroyed, op, &EchoPluginOperation::cancel);
        QTimer::singleShot(
            0, op,
            [view = QPointer(this), op, epoch, sequence, account, position = qint64(position),
             playing] {
                if (op->finished())
                    return;
                if (!view || !view->active() || view->m_scope->accountEpoch() != account) {
                    op->cancel();
                    return;
                }
                auto *projection = view->player()->transition();
                // Source revision may advance when native B adopts within this
                // same transition. Only the explicit epoch/sequence selects A/B.
                if (!projection->valid() || projection->epoch() != epoch ||
                    (projection->outgoing()->sequence() != sequence &&
                     projection->incoming()->sequence() != sequence) ||
                    !view->m_scope->host().playback.selectTransitionSource(epoch, sequence,
                                                                           position, playing)) {
                    op->fail("StaleTransition", "Transition source is no longer available");
                    return;
                }
                op->succeed();
            });
        return op;
    }
    if (action == "queueNext" || action == "saveQueue") {
        const bool next = action == "queueNext";
        const auto field = next ? QStringLiteral("index") : QStringLiteral("title");
        if (args.size() != 1 || !args.contains(field))
            return m_scope->rejected("InvalidArgument", "Invalid native queue command arguments");
        int row = -1;
        QString title;
        if (next) {
            const auto value = args.value(field);
            const auto type = value.metaType().id();
            const double index = value.toDouble();
            if ((type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::LongLong &&
                 type != QMetaType::ULongLong && type != QMetaType::Double) ||
                !std::isfinite(index) || index < 0 || index != std::floor(index) ||
                index > std::numeric_limits<int>::max())
                return m_scope->rejected("InvalidArgument", "Queue index must be an integer");
            row = int(index);
        } else {
            const auto value = args.value(field);
            const auto input = value.toString();
            if (value.metaType().id() != QMetaType::QString || input.size() > 128 ||
                std::any_of(input.cbegin(), input.cend(),
                            [](QChar character) {
                                return character.unicode() < 0x20 || character.unicode() == 0x7f;
                            }) ||
                (title = input.trimmed()).isEmpty())
                return m_scope->rejected("InvalidArgument",
                                         "Queue title must be 1..128 characters");
        }
        auto &playback = m_scope->host().playback;
        auto *queue = playback.queue();
        const auto key = playback.queueCatalog()->activeKey();
        if (!queue || (next && !queue->at(row)))
            return m_scope->rejected("InvalidArgument", "Native queue row does not exist");
        if (!next && !queue->rowCount())
            return m_scope->rejected("Unavailable", "The active queue is empty");
        // Native append redirects dynamic sources to another ordinary queue.
        // A row-scoped action must never silently mutate that other source.
        if (next && (playback.personalFm()->active() || key.startsWith("fm/") ||
                     key == "queue:home-discover" || key == "queue:listen-together"))
            return m_scope->rejected("Unavailable", "This queue cannot accept next priorities");
        const auto revision = queue->revision();
        const auto epoch = m_scope->accountEpoch();
        const QPointer<PlaybackQueueModel> guard(queue);
        // The host's original records preserve local files without hashes and
        // provider identities. No projected SDK map is reparsed as authority.
        const PlaybackTracks tracks = next ? PlaybackTracks{*queue->at(row)} : queue->tracks();
        auto *op = m_scope->operation();
        if (op->finished())
            return op;
        op->start();
        connect(this, &QObject::destroyed, op, &EchoPluginOperation::cancel);
        QTimer::singleShot(
            0, op,
            [view = QPointer(this), op, guard, key, revision, epoch, tracks, row, next, title] {
                if (op->finished())
                    return;
                if (!view || !view->active() || view->m_scope->accountEpoch() != epoch) {
                    op->cancel();
                    return;
                }
                auto &playback = view->m_scope->host().playback;
                auto *catalog = playback.queueCatalog();
                if (!guard || playback.queue() != guard || catalog->activeKey() != key ||
                    guard->revision() != revision ||
                    (!next && guard->rowCount() != tracks.size())) {
                    op->fail("StaleQueue", "Queue changed before the command ran");
                    return;
                }
                for (int index = 0; index < tracks.size(); ++index) {
                    const auto *current = guard->at(next ? row : index);
                    if (!current || current->key() != tracks[index].key() ||
                        current->presentationIdentity() != tracks[index].presentationIdentity()) {
                        op->fail("StaleQueue", "Queue identity changed before the command ran");
                        return;
                    }
                }
                if (next) {
                    if (playback.personalFm()->active() || key.startsWith("fm/") ||
                        key == "queue:home-discover" || key == "queue:listen-together") {
                        op->fail("Unavailable", "This queue cannot accept next priorities");
                        return;
                    }
                    playback.appendTracks(tracks, true);
                    op->succeed();
                } else {
                    const auto saved = "saved/" + QUuid::createUuid().toString(QUuid::Id128);
                    // Retain does not activate or browse the new queue. Existing
                    // catalog capacity/eviction and repository persistence apply.
                    catalog->retain(saved, title, tracks);
                    op->succeed(QVariantMap{
                        {"queueKey", saved}, {"title", title}, {"count", tracks.size()}});
                }
            });
        return op;
    }
    if (args.contains("revision") && args.value("revision").toString() !=
                                         QString::number(m_scope->host().playback.sourceRevision()))
        return m_scope->rejected("Unavailable", "Track generation has changed");
    const auto actions = QStringList{"play",
                                     "pause",
                                     "toggle",
                                     "stop",
                                     "previous",
                                     "next",
                                     "seek",
                                     "volume",
                                     "volumePreview",
                                     "volumeCommit",
                                     "volumeCancel",
                                     "shuffleQueue",
                                     "songWall",
                                     "locateTrack",
                                     "sleepTimerStart",
                                     "sleepTimerCancel",
                                     "speed",
                                     "mode",
                                     "quality",
                                     "effect",
                                     "favorite",
                                     "dislikeFm",
                                     "translation",
                                     "romanization",
                                     "conversion",
                                     "offset",
                                     "resetOffset",
                                     "copyLyrics",
                                     "themes",
                                     "source",
                                     "settings",
                                     "skins",
                                     "closePlayback",
                                     "playback",
                                     "comments",
                                     "barrage",
                                     "queue",
                                     "addToPlaylist",
                                     "share",
                                     "fmPlay",
                                     "fmMode",
                                     "fmPool",
                                     "globalOffset",
                                     "offsetStep",
                                     "closeSkinSettings",
                                     "closeSongWall"};
    if (!actions.contains(action))
        return m_scope->rejected("InvalidArgument", "Unknown presentation command");
    const auto valueType = args.value("value").metaType().id();
    const bool numericValue = valueType == QMetaType::Int || valueType == QMetaType::UInt ||
                              valueType == QMetaType::LongLong ||
                              valueType == QMetaType::ULongLong || valueType == QMetaType::Double ||
                              valueType == QMetaType::Float;
    const auto number = args.value("value").toDouble();
    const auto ranges = QHash<QString, QPair<double, double>>{{"seek", {0, 1e12}},
                                                              {"volume", {0, 150}},
                                                              {"volumePreview", {0, 150}},
                                                              {"volumeCommit", {0, 150}},
                                                              {"sleepTimerStart", {1, 59999}},
                                                              {"speed", {.1, 5}},
                                                              {"offset", {-1, 1}},
                                                              {"mode", {0, 3}},
                                                              {"fmMode", {0, 3}},
                                                              {"fmPool", {0, 2}},
                                                              {"globalOffset", {-20000, 20000}},
                                                              {"offsetStep", {.1, 5}}};
    if (ranges.contains(action) &&
        (!numericValue || !std::isfinite(number) || number < ranges[action].first ||
         number > ranges[action].second))
        return m_scope->rejected("InvalidArgument", "Command value is outside its supported range");
    if ((action == "mode" || action == "fmMode" || action == "fmPool" || action == "offset" ||
         action == "sleepTimerStart") &&
        number != std::floor(number))
        return m_scope->rejected("InvalidArgument", "Command requires an integer");
    if (action == "sleepTimerStart" &&
        (args.value("action").metaType().id() != QMetaType::QString ||
         !QStringList{"pause", "quit", "shutdown"}.contains(args.value("action").toString())))
        return m_scope->rejected("InvalidArgument", "Unknown sleep timer action");
    auto *op = m_scope->operation();
    if (op->finished())
        return op;
    op->start();
    connect(this, &QObject::destroyed, op, &EchoPluginOperation::cancel);
    const auto revision = m_scope->host().playback.sourceRevision();
    const bool trackScoped =
        args.contains("revision") ||
        QStringList{"seek",   "favorite", "dislikeFm", "copyLyrics",    "offset", "resetOffset",
                    "source", "comments", "barrage",   "addToPlaylist", "share",  "locateTrack"}
            .contains(action);
    QTimer::singleShot(
        0, op, [view = QPointer(this), op, action, args, revision, number, trackScoped] {
            if (op->finished())
                return;
            if (!view || !view->active() ||
                (trackScoped && revision != view->m_scope->host().playback.sourceRevision())) {
                op->cancel();
                return;
            }
            auto &p = view->m_scope->host().playback;
            auto *catalog = view->m_scope->host().theme.extensions();
            if (action == "closeSongWall" ||
                (action == "playback" && view->m_surface == "queue-wall")) {
                const auto owner = view->m_owner;
                const auto host = owner ? QPointer(qobject_cast<AppearanceHost *>(owner->parent()))
                                        : QPointer<AppearanceHost>();
                if (!host) {
                    op->fail("Unavailable", "Song wall owner has expired");
                    return;
                }
                // Completing may reenter QML and replace the wall. Never let
                // the old generation's close dismiss that replacement.
                op->succeed();
                if (!owner || !owner->property("active").toBool() || !host)
                    return;
                if (action == "playback")
                    catalog->request(action);
                if (owner && owner->property("active").toBool() && host)
                    emit host->actionRequested("close", {});
                return;
            }
            if (action == "play")
                p.play();
            else if (action == "pause")
                p.pause();
            else if (action == "toggle")
                p.toggle();
            else if (action == "stop")
                p.stop();
            else if (action == "next")
                p.next();
            else if (action == "previous")
                p.previous();
            else if (action == "seek")
                p.seek(qint64(number));
            else if (action == "volume")
                p.setVolume(number);
            else if (action == "songWall") {
                if (!view->m_scope->host().shell.showQueueWall()) {
                    op->fail("Unavailable", "Song wall needs a non-FM playback queue");
                    return;
                }
            } else if (action == "locateTrack") {
                if (!view->m_scope->host().shell.locatePlayingTrack()) {
                    op->fail("Unavailable", "The current queue has no available source collection");
                    return;
                }
                // The accepted route exists before the lyric overlay releases
                // its own presentation, as with closeSkinSettings below.
                op->succeed();
                catalog->request("closePlayback");
                return;
            } else if (action == "volumePreview") {
                if (!p.previewVolume(view->m_scope->player(), number)) {
                    op->fail("Unavailable", "Volume preview is owned by another control or output");
                    return;
                }
            } else if (action == "volumeCommit") {
                if (!p.commitVolumePreview(view->m_scope->player(), number)) {
                    op->fail("Unavailable", "Volume preview has expired");
                    return;
                }
            } else if (action == "volumeCancel")
                p.cancelVolumePreview(view->m_scope->player());
            else if (action == "shuffleQueue") {
                if (!p.shuffleQueue()) {
                    op->fail("Unavailable", "This playback queue cannot be shuffled");
                    return;
                }
            } else if (action == "sleepTimerStart") {
                if (!p.sleepTimer()->start(qint64(number), args.value("action").toString())) {
                    op->fail("Unavailable", "Sleep timer is executing or unavailable");
                    return;
                }
            } else if (action == "sleepTimerCancel") {
                if (p.sleepTimer()->executing()) {
                    op->fail("Unavailable", "Sleep timer action is already executing");
                    return;
                }
                p.sleepTimer()->cancel();
            } else if (action == "speed")
                p.setSpeed(number);
            else if (action == "mode")
                p.setPlayMode(PlaybackService::PlayMode(int(number)));
            else if (action == "quality")
                p.setQuality(args.value("value").toString());
            else if (action == "effect")
                p.setSongEffect(args.value("value").toString());
            else if (action == "favorite") {
                if (!p.libraryActions()) {
                    op->fail("Unavailable", "Library is unavailable");
                    return;
                }
                p.libraryActions()->toggleFavorite();
            } else if (action == "fmPlay")
                p.personalFm()->play(args.value("value").toString());
            else if (action == "fmMode")
                p.personalFm()->setMode(int(number));
            else if (action == "fmPool")
                p.personalFm()->setSongPool(int(number));
            else if (action == "globalOffset")
                p.lyricText()->setGlobalOffsetMs(int(number));
            else if (action == "offsetStep")
                p.lyricText()->setOffsetStep(number);
            else if (action == "dislikeFm")
                p.personalFm()->dislike();
            else if (action == "translation")
                p.lyricText()->setWantTranslation(args.value("value").toBool());
            else if (action == "romanization")
                p.lyricText()->setWantRomanization(args.value("value").toBool());
            else if (action == "conversion")
                p.lyricText()->setConversion(args.value("value").toString());
            else if (action == "offset")
                p.lyricText()->adjustTrackOffset(int(number));
            else if (action == "resetOffset")
                p.lyricText()->resetTrackOffset();
            else if (action == "copyLyrics")
                p.lyricText()->copyLyrics();
            else if (action == "queue")
                view->m_scope->host().shell.invokePlayerCommand(ShellController::Queue);
            else if (action == "addToPlaylist")
                view->m_scope->host().shell.invokePlayerCommand(ShellController::AddToPlaylist);
            else if (action == "share")
                view->m_scope->host().shell.invokePlayerCommand(ShellController::Share);
            else if (action == "closeSkinSettings") {
                // Closing a zero-duration drawer synchronously revokes this
                // presentation. Accept its UI request before that self-removal.
                op->succeed();
                catalog->request(action);
                return;
            } else
                catalog->request(action);
            op->succeed();
        });
    return op;
}
EchoPluginWindows::EchoPluginWindows(EchoPluginContext &s) : QObject(&s), m_scope(s)
{
    if (auto f = s.host().floating) {
        connect(f, &FloatingPlayerController::changed, this, &EchoPluginWindows::changed);
        connect(f->mini(), &FloatingLyricPreferences::changed, this, &EchoPluginWindows::changed);
        connect(f->desktop(), &FloatingLyricPreferences::changed, this,
                &EchoPluginWindows::changed);
    }
}
namespace
{
QVariantMap preferences(QObject *object)
{
    QVariantMap result;
    if (!object)
        return result;
    const auto meta = object->metaObject();
    for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
        auto p = meta->property(i);
        if (p.isReadable())
            result.insert(p.name(), p.read(object));
    }
    return result;
}
bool patchPreferences(QObject *object, const QVariantMap &values)
{
    if (!object)
        return false;
    const auto meta = object->metaObject();
    for (auto it = values.begin(); it != values.end(); ++it) {
        auto index = meta->indexOfProperty(it.key().toUtf8());
        if (index < meta->propertyOffset() || !meta->property(index).isWritable() ||
            (it.value().metaType() != meta->property(index).metaType() &&
             !(meta->property(index).metaType().id() == QMetaType::Int &&
               it.value().metaType().id() == QMetaType::Double &&
               std::isfinite(it.value().toDouble()) &&
               it.value().toDouble() == std::floor(it.value().toDouble()) &&
               it.value().toDouble() >= -2147483648. && it.value().toDouble() <= 2147483647.)))
            return false;
    }
    for (auto it = values.begin(); it != values.end(); ++it)
        if (!object->setProperty(it.key().toUtf8(), it.value()))
            return false;
    return true;
}
} // namespace
QVariantList EchoPluginWindows::list() const
{
    return m_scope.package().manifest.value("windows").toList();
}
EchoPluginWindow *EchoPluginWindows::get(const QString &id)
{
    if (!m_scope.active())
        return nullptr;
    if (m_scope.activationContext() != &m_scope)
        return m_scope.activationContext()->windows()->get(id);
    if (auto window = m_windows.value(id))
        return window;
    for (const auto &value : list()) {
        auto definition = value.toMap();
        if (definition.value("id").toString() == id) {
            auto *window = new EchoPluginWindow(m_scope, definition);
            m_windows.insert(id, window);
            return window;
        }
    }
    return nullptr;
}
EchoPluginOperation *EchoPluginWindows::show(const QString &id, const QVariantMap &options)
{
    auto *window = get(id);
    if (!window)
        return m_scope.rejected("Unavailable", "Plugin window is not declared");
    if (m_scope.activationContext() == &m_scope)
        return window->show(options);
    auto *caller = m_scope.operation();
    if (caller->finished())
        return caller;
    const QPointer<EchoPluginOperation> opened = window->show(options);
    caller->start([opened] {
        if (opened)
            opened->cancel();
    });
    const auto settled = [caller, opened] {
        if (caller->finished() || !opened)
            return;
        if (opened->state() == EchoPluginOperation::Succeeded)
            caller->succeed(opened->result());
        else if (opened->state() == EchoPluginOperation::Canceled)
            caller->cancel();
        else
            caller->fail(opened->errorCode(), opened->errorMessage());
    };
    if (opened->finished())
        settled();
    else
        connect(opened, &EchoPluginOperation::completed, caller, settled);
    return caller;
}
QVariantMap EchoPluginWindows::snapshot() const
{
    auto f = m_scope.host().floating;
    if (!f)
        return {{"available", false}};
    return {{"available", true},
            {"miniActive", f->miniActive()},
            {"desktopEnabled", f->desktopEnabled()},
            {"locked", f->locked()},
            {"panel", f->panel()},
            {"mini", preferences(f->mini())},
            {"desktop", preferences(f->desktop())},
            {"miniBounds", f->miniBounds()},
            {"desktopBounds", f->desktopBounds()}};
}
EchoPluginOperation *EchoPluginWindows::command(const QString &target, const QString &action,
                                                const QVariantMap &args)
{
    auto f = m_scope.host().floating;
    if (!f)
        return m_scope.rejected("Unavailable", "Floating windows are unavailable");
    if (target != "mini" && target != "desktop")
        return m_scope.rejected("InvalidArgument", "Unknown floating target");
    if (!QStringList{"show", "hide", "toggle", "showMain", "panel", "lock", "settings", "bounds",
                     "move"}
             .contains(action))
        return m_scope.rejected("InvalidArgument", "Unknown floating command");
    if (action == "panel" && (target != "mini" || !QStringList{"", "lyric", "queue"}.contains(
                                                      args.value("value").toString())))
        return m_scope.rejected("InvalidArgument", "Invalid Mini panel");
    if (action == "lock" &&
        (target != "desktop" || args.value("value").metaType().id() != QMetaType::Bool))
        return m_scope.rejected("InvalidArgument", "Invalid desktop lock state");
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    op->start();
    const auto epoch = m_scope.accountEpoch();
    QTimer::singleShot(0, op, [this, op, f, target, action, args, epoch] {
        if (op->finished())
            return;
        if (!m_scope.active() || epoch != m_scope.accountEpoch()) {
            op->cancel();
            return;
        }
        if (action == "showMain")
            f->showMain();
        else if (action == "show") {
            if (target == "mini")
                f->showMini();
            else
                f->setDesktopEnabled(true);
        } else if (action == "hide") {
            if (target == "mini")
                f->closeMini();
            else
                f->setDesktopEnabled(false);
        } else if (action == "toggle") {
            if (target == "mini")
                f->toggleMini();
            else
                f->toggleDesktop();
        } else if (action == "panel" && target == "mini")
            f->setPanel(args.value("value").toString());
        else if (action == "lock" && target == "desktop")
            f->setLocked(args.value("value").toBool());
        else if (action == "settings") {
            if (!patchPreferences(target == "mini" ? f->mini() : f->desktop(), args)) {
                op->fail("InvalidArgument", "Unknown or readonly window setting");
                return;
            }
        } else if (action == "bounds") {
            if (!f->setPluginBounds(target, args)) {
                op->fail("Unavailable", "Window must be open and bounds valid");
                return;
            }
        } else if (action == "move") {
            if (target == "mini")
                f->moveMini();
            else
                f->moveDesktop();
        } else {
            op->fail("InvalidArgument", "Command is not available for target");
            return;
        }
        op->succeed();
    });
    return op;
}
} // namespace EchoMusic
