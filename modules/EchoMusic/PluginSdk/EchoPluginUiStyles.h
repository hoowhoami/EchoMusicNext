#pragma once
#include "EchoPluginRegistration.h"
#include <QPointer>
#include <QVariantMap>
namespace EchoMusic
{
class EchoPluginContext;
class EchoUiPartRegistry;
class EchoPluginUiStyles;
class EchoUiStyleRule final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by ui.styles.registerRule")
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(QString revision READ revision CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(int priority READ priority NOTIFY changed)
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
public:
    EchoUiStyleRule(EchoPluginUiStyles &, EchoPluginRegistration &, QString token, QString id,
                    QVariantMap selector, QVariantMap values, int priority);
    QString key() const;
    QString revision() const;
    bool active() const;
    int priority() const
    {
        return m_priority;
    }
    QVariantMap values() const
    {
        return m_values;
    }
    Q_INVOKABLE bool update(const QVariantMap &values);
    Q_INVOKABLE bool setPriority(int priority);
    Q_INVOKABLE void dispose();
    bool publish();
signals:
    void changed();

private:
    QPointer<EchoPluginUiStyles> m_owner;
    QPointer<EchoPluginRegistration> m_lease;
    QString m_token, m_id, m_key, m_revision;
    QVariantMap m_selector, m_values;
    int m_priority;
};
class EchoPluginUiStyles final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ui")
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
public:
    explicit EchoPluginUiStyles(EchoPluginContext &);
    Q_INVOKABLE EchoMusic::EchoUiStyleRule *registerRule(const QString &id,
                                                         const QVariantMap &selector,
                                                         const QVariantMap &values,
                                                         int priority = 0);
    QString errorMessage() const
    {
        return m_error;
    }
    EchoPluginContext &scope() const
    {
        return m_scope;
    }
    EchoUiPartRegistry *registry() const;
    bool validate(QVariantMap &, int priority);
    void error(const QString &);
signals:
    void errorChanged();

private:
    EchoPluginContext &m_scope;
    QString m_error;
};
} // namespace EchoMusic
