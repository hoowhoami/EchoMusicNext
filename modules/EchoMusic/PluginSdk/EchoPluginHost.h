#pragma once
#include "EchoPluginOperation.h"
#include "EchoPluginRegistration.h"
#include <QJSValue>
#include <QVariantMap>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginNavigation final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ui")
public:
    explicit EchoPluginNavigation(EchoPluginContext &);
    Q_INVOKABLE QStringList available() const;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *read();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *navigate(const QVariantMap &target);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *history(const QString &command);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *observe(const QString &id,
                                                        const QJSValue &callback);

private:
    QVariantMap snapshot() const;
    EchoPluginContext &m_scope;
};
class EchoPluginSettings final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by context")
public:
    explicit EchoPluginSettings(EchoPluginContext &);
    Q_INVOKABLE QVariantList catalog() const;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *read(const QString &domain);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *update(const QString &domain,
                                                       const QVariantMap &patch);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *observe(const QString &id, const QString &domain,
                                                        const QJSValue &callback);
signals:
    void sourceChanged();

private:
    QObject *owner(const QString &) const;
    QVariantMap snapshot(const QString &) const;
    EchoPluginContext &m_scope;
};
} // namespace EchoMusic
