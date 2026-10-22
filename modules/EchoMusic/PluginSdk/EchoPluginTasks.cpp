#include "EchoPluginTasks.h"
#include "EchoPluginContext.h"
#include "ShellController.h"
namespace EchoMusic
{
EchoPluginTasks::EchoPluginTasks(EchoPluginContext &scope) : QObject(&scope), m_scope(scope) {}
EchoPluginTask *EchoPluginTasks::registerTask(const QString &id, const QJSValue &definition)
{
    m_tasks.removeIf([](const auto &t) { return !t || !t->active(); });
    if (!m_scope.active() || m_tasks.size() >= 128 || !definition.isObject() ||
        definition.isArray())
        return nullptr;
    // Validate before replacing the current contribution or touching the model.
    auto *probe = new EchoPluginRegistration({}, 0, this);
    auto *task = new EchoPluginTask(m_scope, *probe);
    if (!task->configure(definition)) {
        delete probe;
        return nullptr;
    }
    auto *lease = m_scope.registration("task", id);
    if (!lease) {
        delete probe;
        return nullptr;
    }
    task->setParent(lease);
    task->m_lease = lease;
    delete probe;
    QQmlEngine::setObjectOwnership(task, QQmlEngine::CppOwnership);
    connect(lease, &EchoPluginRegistration::activeChanged, task, [task] {
        if (!task->active())
            task->retire();
    });
    lease->stage([task] { return task->publish(); },
                 [task] {
                     if (task->m_scope)
                         task->m_scope->host().shell.tasks()->remove(task->m_handle);
                     task->retire();
                 });
    m_tasks.append(task);
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return task;
}
} // namespace EchoMusic
