#pragma once
#include "EchoPagePresentation.h"
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
/// Small value-only UI snapshots. The route owns this object independently of
/// its QML instance; it never owns widgets, business data, timers or requests.
class EchoPageStateStore final : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by a route")
public:
    explicit EchoPageStateStore(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE QVariantMap load(const QString &key) const;
    Q_INVOKABLE bool save(const QString &key, const QVariantMap &value);
    void setWritable(bool writable)
    {
        m_writable = writable;
    }

private:
    QHash<QString, QVariantMap> m_values;
    QHash<QString, qsizetype> m_costs;
    qsizetype m_bytes = 0;
    bool m_writable = true;
};

/// RouteHost installs a scope on each Loader. Shared controls resolve their
/// nearest visual ancestor's scope without depending on Shell or a page type.
class EchoPageState final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Attached to a page or its controls")
    QML_ATTACHED(EchoPageState)
    Q_PROPERTY(EchoMusic::EchoPageStateStore *scope READ scope WRITE setScope NOTIFY scopeChanged)
    Q_PROPERTY(EchoMusic::EchoPagePresentation *presentation READ presentation WRITE setPresentation
                   NOTIFY presentationChanged)
    Q_PROPERTY(QStringList properties READ properties WRITE setProperties NOTIFY propertiesChanged)
public:
    explicit EchoPageState(QObject *parent) : QObject(parent) {}
    static EchoPageState *qmlAttachedProperties(QObject *object);
    EchoPageStateStore *scope() const;
    void setScope(EchoPageStateStore *scope);
    EchoPagePresentation *presentation() const;
    void setPresentation(EchoPagePresentation *presentation);
    QStringList properties() const
    {
        return m_properties;
    }
    void setProperties(const QStringList &properties);
    /// Call from Component.onCompleted before page-specific initialization.
    /// Only explicitly declared writable UI values are restored/observed.
    Q_INVOKABLE void restore();
    Q_INVOKABLE QVariantMap load(const QString &key) const;
    Q_INVOKABLE bool save(const QString &key, const QVariantMap &value);
signals:
    void scopeChanged();
    void presentationChanged();
    void propertiesChanged();

private slots:
    void capture();

private:
    QPointer<EchoPageStateStore> m_scope;
    QPointer<EchoPagePresentation> m_presentation;
    QStringList m_properties;
    bool m_ready = false;
};
} // namespace EchoMusic
