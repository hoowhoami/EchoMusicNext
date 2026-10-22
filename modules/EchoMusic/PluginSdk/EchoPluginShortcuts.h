#pragma once
#include "EchoPluginRegistration.h"
#include <QJSValue>
#include <QPointer>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginShortcut final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by EchoPluginShortcuts")
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool registered READ registered NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    bool active() const;
    bool registered() const
    {
        return active() && m_registered;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    Q_INVOKABLE void dispose();
signals:
    void changed();

private:
    friend class EchoPluginShortcuts;
    explicit EchoPluginShortcut(EchoPluginRegistration *lease);
    QPointer<EchoPluginRegistration> m_lease;
    bool m_registered = false;
    QString m_error;
};
class EchoPluginShortcuts final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
public:
    explicit EchoPluginShortcuts(EchoPluginContext &scope);
    Q_INVOKABLE EchoMusic::EchoPluginShortcut *registerShortcut(const QString &id,
                                                                const QString &sequence,
                                                                const QJSValue &callback,
                                                                bool global = false);

private:
    EchoPluginContext &m_scope;
};
} // namespace EchoMusic
