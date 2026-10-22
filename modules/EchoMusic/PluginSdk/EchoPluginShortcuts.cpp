#include "EchoPluginShortcuts.h"
#include "EchoPluginContext.h"
#include "ShortcutService.h"
#include <QTimer>
namespace EchoMusic
{
EchoPluginShortcut::EchoPluginShortcut(EchoPluginRegistration *lease)
    : QObject(lease), m_lease(lease)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    connect(lease, &EchoPluginRegistration::activeChanged, this, [this] {
        emit changed();
        if (!active()) {
            QTimer::singleShot(0, this, [this] {
                auto lease = m_lease;
                setParent(nullptr);
                QQmlEngine::setObjectOwnership(this, QQmlEngine::JavaScriptOwnership);
                if (lease)
                    lease->deleteLater();
            });
        }
    });
}
bool EchoPluginShortcut::active() const
{
    return m_lease && m_lease->active();
}
void EchoPluginShortcut::dispose()
{
    if (m_lease)
        m_lease->dispose();
}
EchoPluginShortcuts::EchoPluginShortcuts(EchoPluginContext &scope) : QObject(&scope), m_scope(scope)
{
}
EchoPluginShortcut *EchoPluginShortcuts::registerShortcut(const QString &id,
                                                          const QString &sequence,
                                                          const QJSValue &callback, bool global)
{
    if (m_scope.activationContext() != &m_scope)
        return nullptr;
    if (!callback.isCallable() || sequence.size() > 128)
        return nullptr;
    auto *lease = m_scope.registration("keyboardShortcut", id);
    if (!lease)
        return nullptr;
    auto *handle = new EchoPluginShortcut(lease);
    auto *service = m_scope.host().shortcuts;
    if (!service) {
        handle->m_error = tr("宿主快捷键服务不可用");
        lease->dispose();
        return handle;
    }
    auto generation = std::make_shared<quint64>(0);
    auto guard = QPointer(lease);
    auto object = QPointer(handle);
    connect(service, &ShortcutService::pluginRegistrationChanged, lease,
            [object, lease](const QString &key, quint64, bool registered, const QString &error) {
                if (object && lease->active() && key == lease->key()) {
                    object->m_registered = registered;
                    object->m_error = error;
                    emit object->changed();
                }
            });
    lease->stage(
        [this, service, lease, object, guard, sequence, callback, global, generation] {
            ShortcutService::PluginBinding binding;
            binding.key = lease->key();
            binding.owner = lease;
            binding.sequence = QKeySequence(sequence, QKeySequence::PortableText);
            binding.global = global;
            binding.invoke = [this, guard, callback] {
                if (guard && guard->active() && m_scope.active())
                    m_scope.invokeRegistered(callback, guard, {});
            };
            QString error;
            *generation = service->registerPlugin(binding, error);
            if (!object)
                return false;
            if (!error.isEmpty())
                object->m_error = error;
            if (*generation && !global)
                object->m_registered = true;
            emit object->changed();
            return *generation != 0;
        },
        [service, lease, generation, object] {
            service->unregisterPlugin(lease->key(), *generation);
            if (object) {
                object->m_registered = false;
                emit object->changed();
            }
        });
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return handle;
}
} // namespace EchoMusic
