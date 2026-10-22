#pragma once
#include "EchoPluginTask.h"
#include <QJSValue>
namespace EchoMusic
{
class EchoPluginTasks final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
public:
    explicit EchoPluginTasks(EchoPluginContext &scope);
    Q_INVOKABLE EchoMusic::EchoPluginTask *registerTask(const QString &id,
                                                        const QJSValue &definition);

private:
    EchoPluginContext &m_scope;
    QList<QPointer<EchoPluginTask>> m_tasks;
};
} // namespace EchoMusic
