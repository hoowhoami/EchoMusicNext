#pragma once
#include <QFont>
#include <QPointer>
#include <QQuickItem>
#include <QQmlParserStatus>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class EchoUiPartRegistry;
// Host declaration only. Public SDK references never expose this object/target.
class EchoUiPartRegistration : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    QML_ELEMENT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(QQuickItem *target READ target WRITE setTarget NOTIFY declarationChanged)
    Q_PROPERTY(QString role READ role WRITE setRole NOTIFY declarationChanged)
    Q_PROPERTY(QString key READ key WRITE setKey NOTIFY declarationChanged)
    Q_PROPERTY(QString surface READ surface WRITE setSurface NOTIFY declarationChanged)
    Q_PROPERTY(QStringList properties READ properties WRITE setProperties NOTIFY declarationChanged)
    Q_PROPERTY(QStringList actions READ actions WRITE setActions NOTIFY declarationChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY declarationChanged)
    Q_PROPERTY(bool replaceable READ replaceable WRITE setReplaceable NOTIFY declarationChanged)
    Q_PROPERTY(QVariantMap overrides READ overrides NOTIFY overridesChanged)
    Q_PROPERTY(QString identity READ identity NOTIFY identityChanged)
    Q_PROPERTY(bool observed READ observed NOTIFY observedChanged)
    Q_PROPERTY(QVariantMap state READ state WRITE setState NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap baseValues READ baseValues WRITE setBaseValues NOTIFY changed)
    Q_PROPERTY(bool replaced READ replaced NOTIFY replacementChanged)
public:
    explicit EchoUiPartRegistration(QObject *parent = nullptr);
    ~EchoUiPartRegistration() override;
    void classBegin() override {}
    void componentComplete() override;
    QQuickItem *target() const
    {
        return m_target;
    }
    void setTarget(QQuickItem *);
    QString role() const
    {
        return m_role;
    }
    void setRole(const QString &);
    QString key() const
    {
        return m_key;
    }
    void setKey(const QString &);
    QString surface() const;
    void setSurface(const QString &);
    QStringList properties() const
    {
        return m_properties;
    }
    void setProperties(const QStringList &);
    QStringList actions() const
    {
        return m_actions;
    }
    void setActions(const QStringList &);
    bool active() const
    {
        return m_active;
    }
    void setActive(bool);
    bool replaceable() const
    {
        return m_replaceable;
    }
    void setReplaceable(bool);
    QVariantMap overrides() const
    {
        return m_overrides;
    }
    QVariantMap baseValues() const
    {
        return m_base;
    }
    void setBaseValues(const QVariantMap &);
    bool observed() const
    {
        return m_readers > 0;
    }
    void acquireState()
    {
        if (++m_readers == 1)
            emit observedChanged();
    }
    void releaseState()
    {
        if (m_readers > 0 && --m_readers == 0)
            emit observedChanged();
    }
    QVariantMap state() const
    {
        return m_state;
    }
    void setState(const QVariantMap &v);
    bool replaced() const
    {
        return !m_replacement.isEmpty();
    }
    QString replacement() const
    {
        return m_replacement;
    }
    QString replacementCandidate() const
    {
        return m_candidate;
    }
    QString identity() const
    {
        return m_identity;
    }
    QString windowId() const;
    QRectF geometry() const;
    bool presented() const;
    void setResolved(const QVariantMap &);
    void setReplacement(const QString &, const QString &candidate = {});
    Q_INVOKABLE QFont resolveFont(const QFont &base, const QVariantMap &values) const;
    Q_INVOKABLE void publish(const QString &event, const QVariantMap &payload = {});
    bool invoke(const QString &action, const QVariantMap &arguments);
signals:
    void stateChanged();
    void identityChanged();
    void observedChanged();
    void declarationChanged();
    void overridesChanged();
    void replacementChanged();
    void mountOrderChanged();
    void changed();
    void eventOccurred(const QString &event, const QVariantMap &payload);
    void actionRequested(const QString &action, const QVariantMap &arguments);

private:
    friend class EchoUiPartRegistry;
    void refresh();
    void connectTarget();
    QPointer<EchoUiPartRegistry> m_registry;
    QPointer<QQuickItem> m_target;
    QString m_role, m_key, m_surface, m_identity, m_replacement, m_candidate;
    QStringList m_properties, m_actions;
    QVariantMap m_overrides, m_base, m_state;
    QList<QMetaObject::Connection> m_connections;
    bool m_stateNoticePending = false;
    int m_readers = 0;
    bool m_active = true, m_complete = false, m_replaceable = false;
};
} // namespace EchoMusic
