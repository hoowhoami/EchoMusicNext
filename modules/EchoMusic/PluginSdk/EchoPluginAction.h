#pragma once
#include "EchoPluginOperation.h"
#include "EchoPluginRegistration.h"
#include "ShellActionModel.h"
#include <QJSValue>
#include <QPointer>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginAction final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by ui action registration")
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(QString revision READ revision CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
public:
    EchoPluginAction(EchoPluginContext &, EchoPluginRegistration &, ShellActionModel &, QJSValue);
    QString key() const
    {
        return m_key;
    }
    QString revision() const
    {
        return m_revision;
    }
    bool active() const;
    bool busy() const;
    QVariantMap state() const
    {
        return m_state;
    }
    Q_INVOKABLE bool update(const QJSValue &patch);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *invoke();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void dispose();
    bool publish(bool playerbar, const QString &id);
    EchoPluginRegistration *lease() const
    {
        return m_lease;
    }
    static bool valid(const QJSValue &, bool playerbar, bool patch = false);
signals:
    void changed();

private:
    friend class EchoPluginUi;
    void reconcile();
    QJSValue evaluate(const QString &) const;
    QPointer<EchoPluginContext> m_scope;
    QPointer<EchoPluginRegistration> m_lease;
    QPointer<ShellActionModel> m_model;
    ShellActionModel::Handle m_handle;
    QJSValue m_definition;
    QString m_key, m_revision;
    QVariantMap m_state;
    QPointer<EchoPluginOperation> m_running;
    bool m_queued = false;
};
} // namespace EchoMusic
