#pragma once
#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class EchoUiPartRegistry;
class EchoUiPartRegistration;
class EchoUiPresentation;
// Inspector-owned native pointer probe. Neither the overlay nor the application
// event filter exists outside this diagnostic presentation's lifetime.
class EchoUiInspectorProbe : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY changed)
    Q_PROPERTY(QString selectedId READ selectedId WRITE setSelectedId NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
public:
    explicit EchoUiInspectorProbe(QObject *parent = nullptr);
    ~EchoUiInspectorProbe() override;
    bool active() const
    {
        return m_active;
    }
    QString selectedId() const
    {
        return m_selectedId;
    }
    QColor color() const
    {
        return m_color;
    }
    void setActive(bool);
    void setSelectedId(const QString &);
    void setColor(const QColor &);
signals:
    void changed();
    void picked(const QString &identity);

protected:
    bool eventFilter(QObject *, QEvent *) override;

private:
    EchoUiPartRegistry *registry() const;
    void highlight();
    void clearHighlight();
    QString at(QQuickWindow *, const QPointF &) const;
    bool m_active = false, m_highlighting = false;
    QString m_selectedId;
    QColor m_color = QColor("#00b96b");
    QPointer<QQuickItem> m_overlay;
    QPointer<EchoUiPresentation> m_observer;
    QPointer<EchoUiPartRegistration> m_part;
    QList<QMetaObject::Connection> m_connections;
};
} // namespace EchoMusic
