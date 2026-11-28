#pragma once
#include "EchoPluginRegistration.h"
#include <QJSValue>
#include <QPointer>

namespace EchoMusic
{
class EchoPluginContext;
class PlaybackFavoriteExtensions;
/// Revision-bound favorite provider handle; invalidation never publishes state
/// supplied by a page and is restricted to this provider's own namespace.
class EchoPluginFavoriteProvider final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by favorite provider registration")
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(QString revision READ revision CONSTANT)
public:
    EchoPluginFavoriteProvider(EchoPluginRegistration &, PlaybackFavoriteExtensions &, QObject *);
    bool active() const;
    QString key() const;
    QString revision() const;
    Q_INVOKABLE bool invalidate(const QString &sourceId = {});
    Q_INVOKABLE void dispose();
signals:
    void activeChanged();

private:
    QPointer<EchoPluginRegistration> m_lease;
    PlaybackFavoriteExtensions &m_registry;
};
class EchoPluginFavorites final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginPlayer")
public:
    explicit EchoPluginFavorites(EchoPluginContext &);
    Q_INVOKABLE EchoMusic::EchoPluginFavoriteProvider *registerProvider(const QString &id,
                                                                        const QJSValue &definition);

private:
    EchoPluginContext &m_scope;
};
} // namespace EchoMusic
