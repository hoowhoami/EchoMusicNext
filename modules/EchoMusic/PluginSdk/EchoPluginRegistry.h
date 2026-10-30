#pragma once
#include "EchoPluginOperation.h"
#include "EchoPluginRegistration.h"
#include <QJSValue>
#include <QHash>
#include <QPointer>
#include <functional>

namespace EchoMusic
{
class EchoPluginContext;
/// Scope-owned, versioned service calls between explicitly declared plugin
/// dependencies. No provider QObject, writable store or QJSValue escapes.
class EchoPluginRegistry final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY availabilityChanged)
public:
    explicit EchoPluginRegistry(EchoPluginContext &scope);
    QVariantMap snapshot() const;
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *
    onChanged(const QString &id, const QJSValue &callback, bool immediate = true);
    Q_INVOKABLE bool isAvailable(const QString &pluginId) const;
    Q_INVOKABLE QString version(const QString &pluginId) const;
    Q_INVOKABLE QString serviceVersion(const QString &pluginId, const QString &serviceId) const;
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *
    registerService(const QString &id, const QString &version, const QJSValue &methods);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *
    registerCommand(const QString &id, const QString &version, const QJSValue &callback,
                    const QVariantMap &metadata = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *callCommand(const QString &pluginId,
                                                            const QString &commandId,
                                                            const QString &versionRange,
                                                            const QVariantMap &arguments = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *
    call(const QString &pluginId, const QString &serviceId, const QString &versionRange,
         const QString &method, const QVariantMap &arguments = {});
    void setResolver(std::function<EchoPluginContext *(const QString &)> resolver);
    void refresh();
signals:
    void availabilityChanged();
    void exportsChanged();

private:
    struct Service
    {
        QString version;
        QHash<QString, QJSValue> methods;
        QPointer<EchoPluginRegistration> lease;
    };
    struct Command
    {
        QString version, title, description;
        QJSValue callback;
        QPointer<EchoPluginRegistration> lease;
    };
    EchoPluginContext *provider(const QString &pluginId) const;
    QVariantMap describe(const EchoPluginContext *provider) const;
    void scheduleRefresh();
    void refreshNow();
    EchoPluginOperation *forward(EchoPluginContext &provider, const QJSValue &callback,
                                 QPointer<EchoPluginRegistration> lease,
                                 const QVariantMap &arguments);
    EchoPluginContext &m_scope;
    QHash<QString, Service> m_services;
    QHash<QString, Command> m_commands;
    QList<QMetaObject::Connection> m_providerConnections;
    QVariantMap m_lastSnapshot;
    bool m_refreshQueued = false;
    std::function<EchoPluginContext *(const QString &)> m_resolver;
};
} // namespace EchoMusic
