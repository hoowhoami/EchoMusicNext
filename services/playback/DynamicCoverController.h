#pragma once
#include "ApiClient.h"
#include "PlaybackTrack.h"
#include "ValueCache.h"
#include <QUrl>
#include <QHash>
#include <QPointer>
namespace EchoMusic
{
class NetworkPreferences;
/// Optional artwork, GUI-thread authority. Signed video URLs are memory-only;
/// visibility/track/session changes cancel pending work and stale deliveries.
class DynamicCoverController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source NOTIFY changed)
public:
    explicit DynamicCoverController(ApiClient &api, QObject *parent = nullptr);
    ~DynamicCoverController() override;
    QUrl source() const;
    void setNetworkPreferences(NetworkPreferences *network);
    void setTrack(const PlaybackTrack &track);
    Q_INVOKABLE void setActive(bool active);
    Q_INVOKABLE void nextSource(const QUrl &failedSource);
signals:
    void changed();

private:
    friend class DynamicCoverControllerTestAccess;
    void cancel();
    void publish();
    void request();
    void succeeded(quint64 id, const QByteArray &bytes);
    void failed(quint64 id, int, const QByteArray &);
    ApiClient &m_api;
    PlaybackTrack m_track;
    QStringList m_urls;
    ValueCache<QString, QStringList> m_cache{64};
    QString cacheKey() const;

    quint64 m_request = 0, m_revision = 0;
    int m_index = 0;
    bool m_active = false;
    QPointer<NetworkPreferences> m_network;
    QUrl m_resource, m_resourceOrigin;
};
} // namespace EchoMusic
