#pragma once
#include "EchoPluginOperation.h"
#include <QPointer>
#include <QRect>
class QQuickWindow;
class QQmlComponent;
namespace EchoMusic
{
class EchoPluginContext;
/// A stable descriptor/command handle. Hidden windows retain no QML or native surface.
class EchoPluginWindow final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by plugin windows")
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
public:
    EchoPluginWindow(EchoPluginContext &, QVariantMap definition);
    ~EchoPluginWindow() override;
    QString id() const
    {
        return m_definition.value("id").toString();
    }
    QVariantMap snapshot() const;
    EchoPluginOperation *show(const QVariantMap &options = {});
    Q_INVOKABLE void hide();
    Q_INVOKABLE void close();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *setBounds(const QVariantMap &bounds);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *setIgnoreMouseEvents(bool ignore);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *showOnTop(bool focus = true);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *startMove();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *startResize(const QString &direction);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *beginMove(double globalX, double globalY);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *beginResize(const QString &direction,
                                                            double globalX, double globalY);
    Q_INVOKABLE void updatePointer(double globalX, double globalY);
    Q_INVOKABLE void endPointer();
signals:
    void changed();

private:
    bool eventFilter(QObject *, QEvent *) override;
    void release();
    void saveBounds();
    void create(EchoPluginOperation *, QVariantMap options);
    QRect bounded(QRect) const;
    Qt::Edges edges(const QString &) const;
    EchoPluginContext &m_scope;
    QVariantMap m_definition;
    QPointer<QQuickWindow> m_window;
    QPointer<QQmlComponent> m_component;
    QPointer<QObject> m_owner;
    QPointer<EchoPluginContext> m_presentation;
    QPointer<EchoPluginOperation> m_opening;
    QString m_error;
    QRect m_bounds, m_dragBounds;
    QPoint m_pointer;
    Qt::Edges m_edges;
    bool m_reconfiguring = false;
    bool m_creating = false, m_releasePending = false;
    bool m_dragging = false, m_ignore = false;
    quint64 m_revision = 0;
};
} // namespace EchoMusic
