#pragma once
#include "EchoPluginOperation.h"
#include "PlaybackTrack.h"
#include <QVariantMap>
namespace EchoMusic
{
class EchoPluginContext;
/// Commands act on the host's queue catalog; no independent plugin queue store.
/// Read pages are bounded. Mutations carry a queue revision to reject stale rows.
class EchoPluginPlaylist final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
    Q_PROPERTY(QVariantList snapshot READ snapshot NOTIFY changed)
public:
    explicit EchoPluginPlaylist(EchoPluginContext &scope);
    QVariantList snapshot() const;
    Q_INVOKABLE QVariantList tracks(const QString &key, int offset = 0, int limit = 100) const;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *replace(const QString &id, const QString &title,
                                                        const QVariantList &tracks,
                                                        int startIndex = -1);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *play(const QString &key, int index);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *
    append(const QString &key, const QVariantList &tracks, const QString &placement = "last");
    Q_INVOKABLE EchoMusic::EchoPluginOperation *remove(const QString &key, const QString &revision,
                                                       int index);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *move(const QString &key, const QString &revision,
                                                     int from, int to);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *clear(const QString &key, const QString &revision);
signals:
    void changed();

private:
    bool parse(const QVariantList &values, PlaybackTracks &tracks) const;
    EchoPluginOperation *mutate(const QString &key, const QString &revision,
                                std::function<void(class PlaybackQueueModel &)> command);
    EchoPluginContext &m_scope;
};
} // namespace EchoMusic
