#pragma once
#include "ApiClient.h"
#include "PlaybackTrack.h"
#include "LyricPortraitAppearance.h"
#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>
namespace EchoMusic
{
/// Visible portrait gallery. Only URL metadata is cached; images belong to the
/// visible QML surface. Requests and carousel stop when that surface retires.
class PortraitController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int index READ index NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
public:
    PortraitController(ApiClient &api, LyricPortraitAppearance &appearance,
                       QObject *parent = nullptr);
    ~PortraitController() override;
    QUrl source() const
    {
        return m_active ? QUrl(m_urls.value(m_index)) : QUrl();
    }
    int count() const
    {
        return m_urls.size();
    }
    int index() const
    {
        return m_index;
    }
    bool loading() const
    {
        return m_request != 0;
    }
    void setTrack(const PlaybackTrack &track);
    Q_INVOKABLE void setActive(bool active);
    Q_INVOKABLE void previous();
    Q_INVOKABLE void next();
    Q_INVOKABLE void imageFailed(const QUrl &url);
signals:
    void changed();
    void trackChanged();

private:
    friend class PortraitControllerTestAccess;
    void request();
    void cancel();
    void publish();
    void succeeded(quint64 id, const QByteArray &bytes);
    void failed(quint64 id, int, const QByteArray &);
    QString cacheKey() const;
    ApiClient &m_api;
    LyricPortraitAppearance &m_appearance;
    PlaybackTrack m_track;
    QStringList m_urls, m_cacheOrder;
    struct CacheEntry
    {
        QStringList urls;
        qint64 expires;
    };
    QHash<QString, CacheEntry> m_cache;
    QElapsedTimer m_clock;
    QTimer m_carousel;
    quint64 m_request = 0, m_revision = 0;
    int m_index = 0;
    bool m_active = false;
};
} // namespace EchoMusic
