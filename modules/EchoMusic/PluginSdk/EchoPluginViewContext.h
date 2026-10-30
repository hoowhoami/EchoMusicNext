#pragma once
#include "EchoPluginPlayer.h"
#include "EchoPluginWindow.h"
#include <QPointer>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginWindows;
/// One visible component's command scope. Models remain host-owned projections;
/// queued commands are cancelled on hide, replacement or account change.
/// Track commands also reject a different track generation.
class EchoPluginViewContext final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Injected by the appearance host")
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString surface READ surface CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginPlayer *player READ player CONSTANT)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
public:
    EchoPluginViewContext(EchoPluginContext &, QString surface, QObject *owner);
    bool active() const;
    QString surface() const
    {
        return m_surface;
    }
    EchoPluginPlayer *player() const;
    QVariantMap snapshot() const;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *command(const QString &action,
                                                        const QVariantMap &arguments = {});
signals:
    void changed();

private:
    QPointer<EchoPluginContext> m_scope;
    QPointer<QObject> m_owner;
    QString m_surface;
    quint64 m_epoch;
    bool m_visualScoped = false;
};
class EchoPluginWindows final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin context")
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
public:
    explicit EchoPluginWindows(EchoPluginContext &);
    Q_INVOKABLE QVariantList list() const;
    Q_INVOKABLE EchoMusic::EchoPluginWindow *get(const QString &id);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *show(const QString &id,
                                                     const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *
    onSnapshot(const QString &id, const QJSValue &callback, bool immediate = true);
    QVariantMap snapshot() const;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *
    command(const QString &target, const QString &action, const QVariantMap &arguments = {});
signals:
    void changed();

private:
    EchoPluginContext &m_scope;
    QHash<QString, QPointer<EchoPluginWindow>> m_windows;
};
} // namespace EchoMusic
