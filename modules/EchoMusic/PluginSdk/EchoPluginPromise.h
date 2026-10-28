#pragma once
#include "EchoPluginOperation.h"
#include <QPointer>
#include <QJSValue>
#include <functional>
namespace EchoMusic
{
/// Private bridge, not an SDK service. Terminal operations release it; the JS
/// adapter catches access to its retired wrapper when a Promise completes late.
class EchoPluginPromise final : public QObject
{
    Q_OBJECT
public:
    EchoPluginPromise(EchoPluginOperation &operation, QObject *parent,
                      std::function<void(const QString &)> report = {})
        : QObject(parent), m_operation(&operation), m_report(std::move(report))
    {
        connect(&operation, &EchoPluginOperation::completed, this, &QObject::deleteLater);
    }
    Q_INVOKABLE void resolve(const QJSValue &value)
    {
        if (m_operation && !m_operation->finished())
            m_operation->succeed(value.toVariant());
    }
    Q_INVOKABLE void reject(const QString &error)
    {
        if (m_operation && !m_operation->finished()) {
            if (m_report)
                m_report(error);
            if (m_operation && !m_operation->finished())
                m_operation->fail("PluginError", error);
        }
    }

private:
    QPointer<EchoPluginOperation> m_operation;
    std::function<void(const QString &)> m_report;
};
} // namespace EchoMusic
