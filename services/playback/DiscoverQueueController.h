#pragma once
#include "ApiClient.h"
#include <QJsonArray>
#include <QPointer>
#include <functional>
namespace EchoMusic
{
class PlaybackService;
class PlaybackQueueModel;
/// Playback-owned DiscoverFlow replenishment. Page browsing and refresh never
/// own the lifetime of an active recommendation queue.
class DiscoverQueueController final : public QObject
{
    Q_OBJECT
public:
    DiscoverQueueController(PlaybackService &, ApiClient &);
    ~DiscoverQueueController() override;
    static QString queueKey()
    {
        return QStringLiteral("queue:home-discover");
    }
    static QJsonArray songs(const QJsonObject &domain);
    /// An application-owned recommendation source can supply the same tail
    /// handoff. The weak owner retires callbacks without retaining a page.
    void bindFeed(const QString &key, QObject *owner, std::function<bool()> request);
    void unbindFeed(QObject *owner);
    void completeFeed(QObject *owner, bool added, const QString &error = {});
    bool handlesCurrent() const;
    bool replenish();
    bool busy() const
    {
        return m_request != 0 || m_feedBusy;
    }
    void advance(bool automatic, bool preserveFailureChain = false);
    void cancelWait()
    {
        m_waiting = false;
    }
    void shutdown();
signals:
    void completed(bool added);
    void notice(const QString &message);

private:
    friend class DiscoverQueueControllerTestAccess;
    void changed();
    void request();
    void succeeded(quint64 id, const QByteArray &bytes);
    void finish(bool added, const QString &error = {});
    void cancel();
    PlaybackService &m_playback;
    ApiClient &m_api;
    quint64 m_request = 0, m_session = 0, m_revision = 0, m_seenRevision = 0, m_waitSequence = 0;
    QPointer<PlaybackQueueModel> m_queue;
    QString m_seenTrack, m_feedKey;
    QPointer<QObject> m_feedOwner;
    std::function<bool()> m_feedRequest;
    bool m_feedBusy = false;
    int m_attempt = 0;
    bool m_waiting = false, m_automatic = false, m_preserveFailure = false, m_closed = false,
         m_merging = false;
};
} // namespace EchoMusic
