#include "EchoPluginLyrics.h"
#include "EchoPluginContext.h"
#include "EchoPluginAudioSource.h"
#include "PlaybackService.h"
#include "LyricExtensions.h"
#include <QJSValueIterator>
#include <QTimer>
#include <cmath>
namespace EchoMusic
{
EchoPluginLyrics::EchoPluginLyrics(EchoPluginContext &scope) : QObject(&scope), m_scope(scope)
{
    connect(scope.player(), &EchoPluginPlayer::lyricChanged, this, &EchoPluginLyrics::changed);
    connect(scope.player(), &EchoPluginPlayer::changed, this, &EchoPluginLyrics::changed);
    connect(scope.host().playback.lyricSource(), &LyricSourceController::changed, this,
            &EchoPluginLyrics::changed);
}
QVariantMap EchoPluginLyrics::snapshot() const
{
    auto result = m_scope.player()->lyricSnapshot();
    const auto *source = m_scope.host().playback.lyricSource();
    result["sourceId"] = source->currentId();
    result["source"] = source->activeSource();
    result["manual"] = source->manual();
    result["loading"] = source->loading();
    result["errorMessage"] = source->errorMessage();
    return result;
}
EchoPluginReadModel *EchoPluginLyrics::model() const
{
    return m_scope.player()->lyrics();
}
QString EchoPluginLyrics::writeKey() const
{
    return m_scope.registrationKey("lyricWrite", "current");
}
EchoPluginRegistration *EchoPluginLyrics::registerResolver(const QString &id,
                                                           const QJSValue &definition)
{
    if (!m_scope.package().capabilities.contains("lyrics") || m_scope.contextId() != "main" ||
        !definition.isObject() || definition.isArray() ||
        !definition.property("resolve").isCallable())
        return nullptr;
    QJSValueIterator iterator(definition);
    while (iterator.hasNext()) {
        iterator.next();
        if (!QStringList{"order", "match", "resolve"}.contains(iterator.name()))
            return nullptr;
    }
    const auto order = definition.property("order");
    if (!order.isUndefined() &&
        (!order.isNumber() || !std::isfinite(order.toNumber()) ||
         std::floor(order.toNumber()) != order.toNumber() || std::abs(order.toNumber()) > 1000000))
        return nullptr;
    const auto match = definition.property("match"), resolve = definition.property("resolve");
    if (!match.isUndefined() && !match.isCallable())
        return nullptr;
    auto *registry = m_scope.host().playback.lyricSource()->extensions();
    if (!registry)
        return nullptr;
    auto *lease = m_scope.registration("lyricResolver", id);
    if (!lease)
        return nullptr;
    const QPointer<EchoPluginContext> scope(&m_scope);
    const QPointer<EchoPluginRegistration> guard(lease);
    LyricExtensions::Resolver resolver;
    resolver.key = "plugin/" + m_scope.id() + '/' + id;
    resolver.revision = lease->revision();
    resolver.order = order.isUndefined() ? 1000 : order.toInt();
    resolver.match = match.isCallable();
    resolver.valid = [scope, guard] {
        return scope && scope->active() && scope->committed() && guard && guard->active();
    };
    resolver.report = [scope, id](const QString &message) {
        if (scope && scope->active())
            emit scope->callbackFailed("Lyric resolver " + id + ": " + message);
    };
    resolver.invoke = [scope, guard, match,
                       resolve](const QString &method, const QVariantMap &arguments,
                                QObject *lifetime, LyricExtensions::Reply reply) {
        if (!scope || !guard || !guard->active() || !scope->active()) {
            reply(false, {});
            return;
        }
        auto *request = new EchoPluginSourceRequest(lifetime);
        QQmlEngine::setObjectOwnership(request, QQmlEngine::CppOwnership);
        connect(lifetime, &QObject::destroyed, request, [request] { request->cancel(); });
        auto values = arguments;
        values.insert("request", QVariant::fromValue(request));
        auto *operation =
            scope->invokeRegistered(method == "match" ? match : resolve, guard, values);
        connect(lifetime, &QObject::destroyed, operation, &EchoPluginOperation::cancel);
        connect(guard, &EchoPluginRegistration::activeChanged, operation, [guard, operation] {
            if (!guard || !guard->active())
                operation->cancel();
        });
        const auto complete = [scope, operation, reply] {
            if (operation->state() == EchoPluginOperation::Failed && scope && scope->active())
                emit scope->callbackFailed(operation->errorMessage());
            reply(operation->state() == EchoPluginOperation::Succeeded, operation->result());
        };
        connect(operation, &EchoPluginOperation::completed, lifetime, complete);
        if (operation->finished())
            complete();
    };
    lease->stage([registry, resolver] { return registry->add(resolver); },
                 [registry, key = resolver.key, revision = resolver.revision] {
                     registry->remove(key, revision);
                 });
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return lease;
}
EchoPluginRegistration *EchoPluginLyrics::onSnapshot(const QString &id, const QJSValue &callback,
                                                     bool immediate)
{
    if (!callback.isCallable())
        return nullptr;
    auto *lease = m_scope.registration("lyricSnapshot", id);
    if (!lease)
        return nullptr;
    auto connection = std::make_shared<QMetaObject::Connection>();
    auto pending = std::make_shared<bool>(false);
    auto previous = std::make_shared<QVariantMap>();
    const auto deliver = [this, lease, callback, pending, previous] {
        if (*pending || !lease->active() || !m_scope.active())
            return;
        *pending = true;
        QTimer::singleShot(0, lease, [this, lease, callback, pending, previous] {
            *pending = false;
            if (!lease->active() || !m_scope.active())
                return;
            const auto value = snapshot();
            if (*previous == value)
                return;
            *previous = value;
            m_scope.call(callback, {m_scope.engine().toScriptValue(value)});
        });
    };
    lease->stage(
        [this, lease, immediate, connection, deliver] {
            *connection = connect(this, &EchoPluginLyrics::changed, lease, deliver);
            if (immediate)
                deliver();
            return true;
        },
        [connection] { QObject::disconnect(*connection); });
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return lease;
}
EchoPluginOperation *EchoPluginLyrics::write(const QString &revision, const QVariantMap &document)
{
    auto &host = m_scope.host().playback;
    auto *controller = host.lyricSource();
    if (!m_scope.package().capabilities.contains("lyrics"))
        return m_scope.rejected("CapabilityRequired", "Declare lyrics capability");
    if (!m_scope.committed())
        return m_scope.rejected("NotReady", "Writes require a committed activation");
    if (!host.hasTrack() || revision != QString::number(host.sourceRevision()))
        return m_scope.rejected("StaleTrack", "Playback track changed");
    host.ensureLyrics();
    if (controller->manual() || controller->applying())
        return m_scope.rejected("ManualSelection", "A manually selected lyric source has priority");
    const auto error = LyricExtensions::validate(document);
    if (!error.isEmpty())
        return m_scope.rejected("InvalidArgument", error);
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    const auto generation = controller->choiceRevision(), epoch = m_scope.accountEpoch();
    auto job = std::make_shared<QPointer<QObject>>();
    op->start([job] {
        delete job->data();
        *job = nullptr;
    });
    connect(&host, &PlaybackService::changed, op, [op, &host, revision] {
        if (!op->finished() && revision != QString::number(host.sourceRevision()))
            op->cancel();
    });
    *job = controller->extensions()->parse(
        document, op,
        [this, op, revision, generation, epoch, document](QJsonObject lyrics, QString error) {
            auto &host = m_scope.host().playback;
            auto *controller = host.lyricSource();
            if (op->finished())
                return;
            if (!m_scope.active() || epoch != m_scope.accountEpoch() ||
                revision != QString::number(host.sourceRevision()) ||
                generation != controller->choiceRevision()) {
                op->cancel();
                return;
            }
            if (!error.isEmpty()) {
                op->fail("InvalidLyrics", error);
                return;
            }
            if (controller->manual()) {
                op->fail("ManualSelection", "A manual source was selected while parsing");
                return;
            }
            auto *lease = m_scope.registration("lyricWrite", "current");
            if (!lease) {
                op->cancel();
                return;
            }
            const auto key = writeKey();
            const auto leaseRevision = lease->revision();
            lease->stage(
                [controller, key, leaseRevision, lyrics, document] {
                    return controller->installWrite(
                        key, leaseRevision, document.value("source", key).toString(), lyrics);
                },
                [controller, key, leaseRevision] { controller->removeWrite(key, leaseRevision); });
            if (!lease->commit()) {
                lease->dispose();
                op->fail("Unavailable", "Lyric write could not be applied");
            } else {
                m_write = lease;
                op->succeed();
            }
        });
    return op;
}
EchoPluginOperation *EchoPluginLyrics::clear(const QString &revision)
{
    if (revision != m_scope.player()->revision())
        return m_scope.rejected("StaleTrack", "Playback track changed");
    return m_scope.execute(
        [this] {
            if (m_write)
                m_write->dispose();
        },
        true);
}
EchoPluginOperation *EchoPluginLyrics::reload(const QString &revision, bool preserveCurrent)
{
    if (revision != m_scope.player()->revision())
        return m_scope.rejected("StaleTrack", "Playback track changed");
    return m_scope.execute(
        [this, preserveCurrent] { m_scope.host().playback.lyricSource()->reload(preserveCurrent); },
        true);
}
EchoPluginOperation *EchoPluginLyrics::command(const QString &action, const QVariantMap &arguments)
{
    if (!QStringList{"translation", "romanization", "conversion", "offset", "resetOffset",
                     "globalOffset", "offsetStep", "copyLyrics", "seek"}
             .contains(action))
        return m_scope.rejected("InvalidArgument", "Unknown lyric command");
    return m_scope.player()->control(action, arguments);
}
} // namespace EchoMusic
