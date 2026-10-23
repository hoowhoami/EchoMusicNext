#include "EchoPluginViewContext.h"
#include "EchoPluginContext.h"
#include "ThemeStore.h"
#include "PlaybackService.h"
#include "ShellController.h"
#include "FloatingPlayerController.h"
#include "WindowController.h"
#include <QMetaProperty>
#include <QTimer>
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
EchoPluginOperation *EchoPluginViewContext::command(const QString &action, const QVariantMap &args)
{
    if (!m_scope)
        return nullptr;
    if (!active())
        return m_scope->rejected("Unavailable", "Presentation has expired");
    if (args.contains("revision") && args.value("revision").toString() !=
                                         QString::number(m_scope->host().playback.sourceRevision()))
        return m_scope->rejected("Unavailable", "Track generation has changed");
    const auto actions =
        QStringList{"play",         "pause",      "toggle",   "stop",        "previous",
                    "next",         "seek",       "volume",   "speed",       "mode",
                    "quality",      "effect",     "favorite", "dislikeFm",   "translation",
                    "romanization", "conversion", "offset",   "resetOffset", "copyLyrics",
                    "themes",       "source",     "settings", "skins",       "closePlayback",
                    "playback",     "comments",   "barrage",  "queue",       "addToPlaylist",
                    "share",        "fmPlay",     "fmMode",   "fmPool",      "globalOffset",
                    "offsetStep"};
    if (!actions.contains(action))
        return m_scope->rejected("InvalidArgument", "Unknown presentation command");
    const auto number = args.value("value").toDouble();
    const auto ranges = QHash<QString, QPair<double, double>>{
        {"seek", {0, 1e12}},    {"volume", {0, 150}},
        {"speed", {.1, 5}},     {"offset", {-1, 1}},
        {"mode", {0, 3}},       {"fmMode", {0, 3}},
        {"fmPool", {0, 2}},     {"globalOffset", {-20000, 20000}},
        {"offsetStep", {.1, 5}}};
    if (ranges.contains(action) &&
        (!args.contains("value") || args.value("value").metaType().id() == QMetaType::Bool ||
         args.value("value").metaType().id() == QMetaType::QString || !std::isfinite(number) ||
         number < ranges[action].first || number > ranges[action].second))
        return m_scope->rejected("InvalidArgument", "Command value is outside its supported range");
    if ((action == "mode" || action == "fmMode" || action == "fmPool" || action == "offset") &&
        number != std::floor(number))
        return m_scope->rejected("InvalidArgument", "Command requires an integer");
    auto *op = m_scope->operation();
    if (op->finished())
        return op;
    op->start();
    connect(this, &QObject::destroyed, op, &EchoPluginOperation::cancel);
    const auto revision = m_scope->host().playback.sourceRevision();
    const bool trackScoped =
        args.contains("revision") ||
        QStringList{"seek",   "favorite", "dislikeFm", "copyLyrics",    "offset", "resetOffset",
                    "source", "comments", "barrage",   "addToPlaylist", "share"}
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
            else if (action == "speed")
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
            else
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
