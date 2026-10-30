#pragma once
#include "EchoPluginContext.h"
#include <QTimer>
namespace EchoMusic
{
// State observation coalesces synchronous owner notifications and always reads
// the latest snapshot. Event streams (e.g. playback seek) keep their own API.
template <class Owner, class Reader>
EchoPluginRegistration *observePluginSnapshot(EchoPluginContext &scope, Owner *source,
                                              void (Owner::*signal)(), const QString &kind,
                                              const QString &id, const QJSValue &callback,
                                              bool immediate, Reader reader)
{
    if (!callback.isCallable())
        return nullptr;
    auto *lease = scope.registration(kind, id);
    if (!lease)
        return nullptr;
    auto pending = std::make_shared<bool>(false);
    auto connection = std::make_shared<QMetaObject::Connection>();
    const auto schedule = [owner = QPointer<Owner>(source), context = QPointer(&scope),
                           lease = QPointer(lease), callback, reader, pending] {
        if (!owner || !context || !lease || !lease->active() || std::exchange(*pending, true))
            return;
        QTimer::singleShot(0, lease, [owner, context, lease, callback, reader, pending] {
            *pending = false;
            if (owner && context && context->active() && lease && lease->active())
                context->call(callback, {context->engine().toScriptValue(reader(owner.data()))});
        });
    };
    lease->stage(
        [source, signal, lease, connection, schedule, immediate] {
            *connection = QObject::connect(source, signal, lease, schedule);
            if (immediate)
                schedule();
            return true;
        },
        [connection] { QObject::disconnect(*connection); });
    if (scope.committed() && !lease->commit())
        lease->dispose();
    return lease;
}
} // namespace EchoMusic
