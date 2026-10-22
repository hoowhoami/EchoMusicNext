#pragma once
#include <QObject>
#include "EchoPluginOperation.h"
#include <QPointer>
#include <QRectF>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class EchoUiPartRegistration;
class EchoUiPartRegistry;
class EchoPluginContext;
class EchoPluginOperation;
class EchoUiPartRef final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by ui.parts")
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString revision READ id CONSTANT)
    Q_PROPERTY(QString role READ role CONSTANT)
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(QString surface READ surface CONSTANT)
    Q_PROPERTY(QString window READ window CONSTANT)
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(QRectF geometry READ geometry NOTIFY changed)
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QVariantMap visuals READ visuals NOTIFY changed)
    Q_PROPERTY(QStringList properties READ properties CONSTANT)
    Q_PROPERTY(QStringList actions READ actions CONSTANT)
    Q_PROPERTY(bool replaceable READ replaceable CONSTANT)
public:
    EchoUiPartRef(EchoPluginContext &, EchoUiPartRegistration &, QObject *parent = nullptr);
    ~EchoUiPartRef() override;
    QString id() const
    {
        return m_id;
    }
    QString role() const
    {
        return m_role;
    }
    QString key() const
    {
        return m_key;
    }
    QString surface() const
    {
        return m_surface;
    }
    QString window() const
    {
        return m_window;
    }
    bool valid() const;
    bool visible() const;
    QRectF geometry() const;
    QVariantMap visuals() const;
    QVariantMap state() const;
    QStringList properties() const
    {
        return m_properties;
    }
    QStringList actions() const
    {
        return m_actions;
    }
    bool replaceable() const
    {
        return m_replaceable;
    }
    bool belongsTo(const EchoPluginContext *) const;
    EchoUiPartRegistration *registration() const;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *invoke(const QString &action,
                                                       const QVariantMap &arguments = {});
signals:
    void changed();
    void invalidated();
    void eventOccurred(const QString &event, const QVariantMap &payload);

private:
    void watchAncestors();
    void notify();
    QPointer<EchoPluginContext> m_scope;
    QPointer<EchoUiPartRegistry> m_registry;
    QPointer<EchoUiPartRegistration> m_part;
    QString m_id, m_role, m_key, m_surface, m_window;
    QStringList m_properties, m_actions;
    QList<QMetaObject::Connection> m_ancestors;
    quint64 m_epoch = 0;
    bool m_replaceable = false, m_invalidated = false;
};
} // namespace EchoMusic
