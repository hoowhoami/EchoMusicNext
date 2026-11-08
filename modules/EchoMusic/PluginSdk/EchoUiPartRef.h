#pragma once
#include <QObject>
#include "EchoUiContentFactory.h"
#include "EchoPluginOperation.h"
#include <QPointer>
#include <QRectF>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class EchoUiPartRegistration;
class EchoUiContentFactory;
class EchoUiPresentation;
class EchoUiPartRegistry;
class EchoPluginContext;
class EchoPluginOperation;
class EchoUiPartRef final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by ui.parts")
    Q_PROPERTY(QString parentId READ parentId NOTIFY changed)
    Q_PROPERTY(QString address READ address NOTIFY changed)
    Q_PROPERTY(QString scope READ scope NOTIFY changed)
    Q_PROPERTY(QString resourceKey READ resourceKey NOTIFY changed)
    Q_PROPERTY(bool persistent READ persistent NOTIFY changed)
    Q_PROPERTY(QString layoutKind READ layoutKind NOTIFY changed)
    Q_PROPERTY(QString layoutEngine READ layoutEngine NOTIFY changed)
    Q_PROPERTY(QStringList insertionPositions READ insertionPositions NOTIFY changed)
    Q_PROPERTY(bool automatic READ automatic NOTIFY changed)
    Q_PROPERTY(QString owner READ owner CONSTANT)
    Q_PROPERTY(bool composable READ composable NOTIFY changed)
    Q_PROPERTY(int contractVersion READ contractVersion CONSTANT)
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString revision READ id CONSTANT)
    Q_PROPERTY(QString role READ role CONSTANT)
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(QString surface READ surface NOTIFY changed)
    Q_PROPERTY(QString window READ window CONSTANT)
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(QRectF geometry READ geometry NOTIFY changed)
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QVariantMap modelContext READ modelContext NOTIFY changed)
    Q_PROPERTY(QVariantMap stateSchema READ stateSchema NOTIFY changed)
    Q_PROPERTY(QVariantMap actionSchema READ actionSchema NOTIFY changed)
    Q_PROPERTY(QVariantMap visuals READ visuals NOTIFY changed)
    Q_PROPERTY(QVariantMap visualSources READ visualSources NOTIFY changed)
    Q_PROPERTY(QVariantMap palette READ palette NOTIFY changed)
    Q_PROPERTY(QStringList properties READ properties NOTIFY changed)
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
    QString surface() const;
    QString window() const
    {
        return m_window;
    }
    QString parentId() const;
    QString address() const;
    QString scope() const;
    QString resourceKey() const;
    bool persistent() const;
    QString layoutKind() const;
    QString layoutEngine() const;
    QStringList insertionPositions() const;
    bool automatic() const;
    QString owner() const
    {
        return m_owner;
    }
    bool composable() const;
    int contractVersion() const
    {
        return 1;
    }
    EchoUiContentFactory *hostContent() const
    {
        return m_content;
    }
    void setHostContent(EchoUiContentFactory *factory)
    {
        m_content = factory;
    }
    bool valid() const;
    bool visible() const;
    QRectF geometry() const;
    QVariantMap visuals() const;
    QVariantMap visualSources() const;
    QVariantMap palette() const;
    QVariantMap state() const;
    QVariantMap modelContext() const;
    QVariantMap stateSchema() const;
    QVariantMap actionSchema() const;
    Q_INVOKABLE QVariantMap mapPoint(const QVariantMap &point,
                                     const QString &space = "window") const;
    QStringList properties() const;
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

private slots:
    void paletteChanged();

private:
    void watchAncestors();
    void notify();
    QPointer<EchoPluginContext> m_scope;
    QPointer<EchoUiPartRegistry> m_registry;
    QPointer<EchoUiPartRegistration> m_part;
    QPointer<EchoUiContentFactory> m_content;
    QPointer<EchoUiPresentation> m_presentation;
    QString m_owner;
    QString m_id, m_role, m_key, m_surface, m_window;
    QStringList m_properties, m_actions;
    QList<QMetaObject::Connection> m_ancestors;
    quint64 m_epoch = 0;
    bool m_replaceable = false, m_invalidated = false;
};
} // namespace EchoMusic
