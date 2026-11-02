#include "EchoPluginLyrics.h"
#include "EchoPluginContext.h"
#include "EchoPluginAudioSource.h"
#include "PlaybackService.h"
#include "LyricExtensions.h"
#include <QJSValueIterator>
#include <QTimer>
#include <QJsonDocument>
#include <cmath>
#include <limits>
namespace EchoMusic
{
EchoPluginLyrics::EchoPluginLyrics(EchoPluginContext &scope) : QObject(&scope), m_scope(scope)
{
    connect(scope.host().playback.lyrics(), &LyricModel::changed, this,
            [this] { ++m_documentRevision; });
    connect(scope.player(), &EchoPluginPlayer::lyricChanged, this, &EchoPluginLyrics::changed);
    connect(scope.player(), &EchoPluginPlayer::changed, this, &EchoPluginLyrics::changed);
    connect(scope.host().playback.lyricSource(), &LyricSourceController::changed, this,
            &EchoPluginLyrics::changed);
}
QVariantMap EchoPluginLyrics::snapshot() const
{
    auto result = information();
    result["lines"] = m_scope.player()->lyricSnapshot().value("lines");
    return result;
}
QVariantMap EchoPluginLyrics::information() const
{
    auto result = m_scope.player()->lyricsInfo();
    result["revision"] = m_scope.player()->revision();
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
    auto documentRevision = std::make_shared<quint64>(std::numeric_limits<quint64>::max());
    auto lines = std::make_shared<QJSValue>();
    const auto deliver = [this, lease, callback, pending, previous, documentRevision, lines] {
        if (*pending || !lease->active() || !m_scope.active())
            return;
        *pending = true;
        QTimer::singleShot(
            0, lease, [this, lease, callback, pending, previous, documentRevision, lines] {
                *pending = false;
                if (!lease->active() || !m_scope.active())
                    return;
                const auto value = information();
                const bool documentChanged = *documentRevision != m_documentRevision;
                if (!documentChanged && *previous == value)
                    return;
                if (documentChanged) {
                    // Keep the immutable document outside the line-change payload.
                    // The cache belongs to this subscription, not a global store;
                    // disposing its lease releases the JS graph and old documents.
                    auto prepareDocument = m_scope.engine().evaluate(QStringLiteral(
                        "(function(json){"
                        "var mutations={copyWithin:true,fill:true,pop:true,push:true,reverse:true,"
                        "shift:true,sort:true,splice:true,unshift:true};"
                        "var deny=function(){throw new TypeError('Lyric document is read-only');};"
                        "function readonly(v){if(v===null||typeof v!=='object')return v;"
                        "Object.keys(v).forEach(function(k){v[k]=readonly(v[k]);});"
                        "Object.freeze(v);var array=Array.isArray(v);"
                        "return new Proxy(v,{get:function(t,k,r){"
                        "if(array&&mutations[k]===true)return deny;return Reflect.get(t,k,r);},"
                        "set:deny,defineProperty:deny,deleteProperty:deny,setPrototypeOf:deny});}"
                        "return readonly(JSON.parse(json));})"));
                    // Materialize plain JS data once. Qt 6.12 array mutators can
                    // bypass Object.freeze; proxies also reject borrowed writes.
                    const auto encoded =
                        QJsonDocument::fromVariant(m_scope.player()->lyricSnapshot().value("lines"))
                            .toJson(QJsonDocument::Compact);
                    *lines = prepareDocument.call({QJSValue(QString::fromUtf8(encoded))});
                    if (lines->isError()) {
                        emit m_scope.callbackFailed("Unable to prepare read-only lyric snapshot");
                        return;
                    }
                    *documentRevision = m_documentRevision;
                }
                *previous = value;
                // A native QVariantMap wrapper would convert assigned arrays
                // back to QVariant, losing their JS identity and immutability.
                auto argument = m_scope.engine().newObject();
                for (auto it = value.cbegin(); it != value.cend(); ++it)
                    argument.setProperty(it.key(), m_scope.engine().toScriptValue(it.value()));
                argument.setProperty("lines", *lines);
                m_scope.call(callback, {argument});
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
