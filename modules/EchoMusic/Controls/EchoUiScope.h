#pragma once
#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <QtQml/qqml.h>
class QQuickItem;
namespace EchoMusic
{
// Presentation/resource metadata. It owns no business state or visual objects.
class EchoUiScope final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Attached to a presentation or delegate root")
    QML_ATTACHED(EchoUiScope)
    Q_PROPERTY(QString owner READ owner WRITE setOwner NOTIFY changed)
    Q_PROPERTY(QString surface READ surface WRITE setSurface NOTIFY changed)
    Q_PROPERTY(QString key READ key WRITE setKey NOTIFY changed)
    Q_PROPERTY(QString resourceKey READ resourceKey WRITE setResourceKey NOTIFY changed)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY changed)
    Q_PROPERTY(qulonglong generation READ generation NOTIFY changed)
public:
    explicit EchoUiScope(QObject *);
    ~EchoUiScope() override;
    static EchoUiScope *qmlAttachedProperties(QObject *);
    static EchoUiScope *get(QObject *, bool create = false);
    static void mark(QQuickItem *, const QString &owner, const QString &surface,
                     const QString &key);
    static bool accepts(QQuickItem *);
    QString owner() const
    {
        return m_owner;
    }
    QString surface() const
    {
        return m_surface;
    }
    QString key() const
    {
        return m_key;
    }
    QString resourceKey() const
    {
        return m_resource;
    }
    bool active() const
    {
        return m_active;
    }
    qulonglong generation() const
    {
        return m_generation;
    }
    void setOwner(const QString &);
    void setSurface(const QString &);
    void setKey(const QString &);
    void setResourceKey(const QString &);
    void setActive(bool);
    Q_INVOKABLE void configure(const QString &owner, const QString &surface, const QString &key);
    void renew();
signals:
    void changed();

private:
    void invalidate();
    QPointer<QQuickItem> m_root;
    QString m_owner, m_surface, m_key, m_resource;
    qulonglong m_generation = 0;
    bool m_active = true;
};
} // namespace EchoMusic
QML_DECLARE_TYPEINFO(EchoMusic::EchoUiScope, QML_HAS_ATTACHED_PROPERTIES)
