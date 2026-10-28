#include "PortraitController.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <utility>
namespace EchoMusic
{
PortraitController::PortraitController(ApiClient &api, LyricPortraitAppearance &appearance,
                                       QObject *parent)
    : QObject(parent), m_api(api), m_appearance(appearance)
{
    m_clock.start();
    connect(&api, &ApiClient::succeeded, this, &PortraitController::succeeded);
    connect(&api, &ApiClient::failed, this, &PortraitController::failed);
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        cancel();
        m_urls.clear();
        m_index = 0;
        publish();
        request();
    });
    connect(&api, &ApiClient::closed, this, [this] { setActive(false); });
    connect(&appearance, &LyricPortraitAppearance::changed, this, &PortraitController::publish);
    connect(&m_carousel, &QTimer::timeout, this, &PortraitController::next);
}
PortraitController::~PortraitController()
{
    cancel();
}
QString PortraitController::cacheKey() const
{
    return m_track.hash.toLower() + "/" + QString::number(m_track.songId);
}
void PortraitController::cancel()
{
    const auto pending = std::exchange(m_request, 0);
    if (pending)
        m_api.cancel(pending);
}
void PortraitController::publish()
{
    m_carousel.stop();
    if (m_active && m_urls.size() > 1 && m_appearance.carouselEnabled())
        m_carousel.start(m_appearance.carouselInterval() * 1000);
    emit changed();
}
void PortraitController::setTrack(const PlaybackTrack &track)
{
    if (track.hash.compare(m_track.hash, Qt::CaseInsensitive) == 0 &&
        track.songId == m_track.songId && track.localPath == m_track.localPath)
        return;
    cancel();
    m_track = track;
    m_urls.clear();
    m_index = 0;
    // Clear retiring decoders before publishing a new gallery, even on a cache hit.
    emit trackChanged();
    publish();
    request();
}
void PortraitController::setActive(bool active)
{
    setSurfaceActive(QStringLiteral("playback"), active);
}
void PortraitController::setSurfaceActive(const QString &surface, bool active)
{
    if (active)
        m_surfaces.insert(surface);
    else
        m_surfaces.remove(surface);
    const bool now = !m_surfaces.isEmpty();
    if (m_active == now)
        return;
    m_active = now;
    if (!now) {
        cancel();
        m_urls.clear();
        m_index = 0;
    }
    publish();
    if (now)
        request();
}
void PortraitController::request()
{
    if (!m_active || m_request || m_track.hash.size() != 32)
        return;
    const auto key = cacheKey();
    const auto cached = m_cache.constFind(key);
    if (cached != m_cache.cend() && cached->expires > m_clock.elapsed()) {
        m_urls = cached->urls;
        m_index = 0;
        m_cacheOrder.removeAll(key);
        m_cacheOrder.append(key);
        publish();
        return;
    }
    m_cache.remove(key);
    m_cacheOrder.removeAll(key);
    m_revision = m_api.sessionRevision();
    m_request = m_api.audioImages({{m_track.hash, 0, m_track.songId, m_track.title}}, 5);
    publish();
}
void PortraitController::succeeded(quint64 id, const QByteArray &bytes)
{
    if (!id || id != m_request || !m_active || m_revision != m_api.sessionRevision())
        return;
    m_request = 0;
    const auto groups = QJsonDocument::fromJson(bytes).array();
    QStringList urls;
    // This owner requests one song. The Rust result already owns wire mapping;
    // image category preference belongs to this presentation, not the API.
    const auto artists = groups.isEmpty() ? QJsonArray() : groups[0].toArray();
    for (const auto &kind : {"3", "4", "2"}) {
        for (const auto &artist : artists) {
            const auto images = artist.toObject().value("images").toObject().value(kind).toArray();
            for (const auto &image : images) {
                const auto url = image.toObject().value("url").toString();
                if (!url.isEmpty() && !urls.contains(url))
                    urls.append(url);
            }
        }
        if (!urls.isEmpty())
            break;
    }
    m_urls = urls.mid(0, 5);
    m_index = 0;
    const auto key = cacheKey();
    m_cache.insert(key, {m_urls, m_clock.elapsed() + (m_urls.isEmpty() ? 300000 : 1800000)});
    m_cacheOrder.removeAll(key);
    m_cacheOrder.append(key);
    while (m_cacheOrder.size() > 50)
        m_cache.remove(m_cacheOrder.takeFirst());
    publish();
}
void PortraitController::failed(quint64 id, int, const QByteArray &)
{
    if (!id || id != m_request)
        return;
    m_request = 0;
    m_urls.clear();
    m_index = 0;
    // Failures aren't cached or retried on a timer. Opening again can retry.
    publish();
}
void PortraitController::previous()
{
    if (!m_active || m_urls.size() < 2)
        return;
    m_index = (m_index + m_urls.size() - 1) % m_urls.size();
    publish();
}
void PortraitController::next()
{
    if (!m_active || m_urls.size() < 2)
        return;
    m_index = (m_index + 1) % m_urls.size();
    publish();
}
void PortraitController::imageFailed(const QUrl &url)
{
    if (!m_active || url.isEmpty() || url != source())
        return;
    m_urls.removeAt(m_index);
    m_index = m_urls.isEmpty() ? 0 : m_index % m_urls.size();
    if (m_urls.isEmpty()) {
        // A decoder/network failure is not an upstream empty gallery. Reopen
        // may retry rather than retaining a failed gallery for the hit TTL.
        m_cache.remove(cacheKey());
        m_cacheOrder.removeAll(cacheKey());
    } else if (auto cached = m_cache.find(cacheKey()); cached != m_cache.end()) {
        cached->urls = m_urls;
    }
    publish();
}
} // namespace EchoMusic
