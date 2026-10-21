#pragma once
#include "EchoUiPartRef.h"
#include "EchoPluginRegistration.h"
#include <QQmlComponent>
#include <QQuickItem>
namespace EchoMusic
{
class EchoPluginUiParts;
class EchoUiPartRegistration;
class EchoUiPartRegistry;
class EchoUiPartMount final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by ui.parts.mount")
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString position READ position CONSTANT)
public:
    EchoUiPartMount(EchoPluginUiParts &, EchoPluginRegistration &, EchoUiPartRegistration &,
                    QString position, QString path, QString id, int priority);
    ~EchoUiPartMount() override;
    bool active() const;
    bool loaded() const
    {
        return !m_item.isNull();
    }
    QString errorMessage() const
    {
        return m_error;
    }
    QString position() const
    {
        return m_position;
    }
    Q_INVOKABLE void dispose();
    void start();
    void stop();
signals:
    void changed();

private:
    void reconcile();
    void construct();
    void release();
    void fail(const QString &);
    void place();
    QPointer<EchoPluginUiParts> m_owner;
    QPointer<EchoPluginRegistration> m_lease;
    QPointer<EchoUiPartRegistration> m_part;
    QPointer<EchoUiPartRegistry> m_registry;
    EchoUiPartRef *m_ref;
    QString m_position, m_path, m_id, m_identity, m_token, m_error;
    int m_priority;
    quint64 m_loadGeneration = 0;
    bool m_started = false, m_constructing = false;
    QPointer<QQmlComponent> m_component;
    QPointer<QQuickItem> m_item;
};
} // namespace EchoMusic
