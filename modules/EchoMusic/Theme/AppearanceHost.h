#pragma once
#include "AppearanceCatalog.h"
#include <QQuickItem>
#include <QQmlComponent>
namespace EchoMusic
{
/// Native demand-loaded presentation tree. No dependency on the plugin runtime
/// or business services; the registered factory injects a scoped typed context.
class AppearanceHost : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EchoMusic::AppearanceEntry *entry READ entry WRITE setEntry NOTIFY entryChanged)
    Q_PROPERTY(QString placement READ placement WRITE setPlacement NOTIFY placementChanged)
    Q_PROPERTY(bool resourceActive READ resourceActive WRITE setResourceActive NOTIFY
                   resourceActiveChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool available READ available NOTIFY entryChanged)
public:
    explicit AppearanceHost(QQuickItem *parent = nullptr);
    ~AppearanceHost() override;
    AppearanceEntry *entry() const
    {
        return m_entry;
    }
    QString placement() const
    {
        return m_placement;
    }
    bool resourceActive() const
    {
        return m_active;
    }
    bool available() const
    {
        return m_entry && m_entry->components.contains(m_placement);
    }
    bool loaded() const
    {
        return !m_item.isNull();
    }
    void setEntry(AppearanceEntry *);
    void setPlacement(const QString &);
    void setResourceActive(bool);

protected:
    void componentComplete() override;
    void geometryChange(const QRectF &, const QRectF &) override;
signals:
    void entryChanged();
    void placementChanged();
    void resourceActiveChanged();
    void loadedChanged();
    /// Synchronous finalization before the old owner's command lease is revoked.
    /// Receivers must not retain the opaque owner or defer work through this signal.
    void presentationClosing(QObject *owner);
    void actionRequested(const QString &action, const QVariantMap &arguments);

private:
    void reconcile();
    void release();
    void finish(quint64);
    bool m_active = true, m_complete = false, m_loading = false;
    quint64 m_generation = 0;
    QString m_placement;
    QPointer<AppearanceEntry> m_entry;
    QPointer<QQuickItem> m_item;
    QPointer<QQmlComponent> m_component;
    QPointer<QObject> m_context;
    QMetaObject::Connection m_windowConnection, m_catalogConnection, m_warningConnection;
    QObject *m_visibility = nullptr;
};
} // namespace EchoMusic
