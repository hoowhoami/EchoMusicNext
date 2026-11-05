#include "DynamicCoverController.h"
#include "NetworkPreferences.h"
#include <QJsonDocument>
#include <QJsonArray>
namespace EchoMusic
{
DynamicCoverController::DynamicCoverController(ApiClient &api, QObject *parent)
    : QObject(parent), m_api(api)
{
    m_clock.start();
    connect(&api, &ApiClient::succeeded, this, &DynamicCoverController::succeeded);
    connect(&api, &ApiClient::failed, this, &DynamicCoverController::failed);
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        cancel();
        m_urls.clear();

        m_cache.clear();
        m_cacheOrder.clear();
        publish();
        if (m_active)
            request();
    });
    connect(&api, &ApiClient::closed, this, [this] {
        cancel();
        m_urls.clear();
        m_active = false;
        publish();
    });
}
DynamicCoverController::~DynamicCoverController()
{
    cancel();
    if (m_network && !m_resource.isEmpty())
        m_network->releaseMedia(m_resource);
}
QUrl DynamicCoverController::source() const
{
    if (m_network)
        return m_resource;
    return m_index >= 0 && m_index < m_urls.size() ? QUrl(m_urls[m_index]) : QUrl();
}
void DynamicCoverController::setNetworkPreferences(NetworkPreferences *network)
{
    if (m_network && !m_resource.isEmpty())
        m_network->releaseMedia(m_resource);
    m_network = network;
    m_resource = QUrl();
    m_resourceOrigin = QUrl();
    publish();
}
void DynamicCoverController::publish()
{
    if (m_network) {
        const QUrl target =
            m_active && m_index >= 0 && m_index < m_urls.size() ? QUrl(m_urls[m_index]) : QUrl();
        if (target != m_resourceOrigin) {
            if (!m_resource.isEmpty())
                m_network->releaseMedia(m_resource);
            m_resourceOrigin = target;
            m_resource = target.isEmpty() ? QUrl() : m_network->registerMedia(target);
        }
    }
    emit changed();
}
QString DynamicCoverController::cacheKey() const
{
    return QString::number(m_track.songId) + "/" + QString::number(m_track.albumId);
}
void DynamicCoverController::cancel()
{
    const auto pending = m_request;
    m_request = 0;
    if (pending)
        m_api.cancel(pending);
}
void DynamicCoverController::setTrack(const PlaybackTrack &track)
{
    if (track.key() == m_track.key() && track.songId == m_track.songId &&
        track.albumId == m_track.albumId)
        return;
    cancel();
    m_track = track;
    m_urls.clear();
    m_index = 0;

    publish();
    request();
}
void DynamicCoverController::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (active) {
        request();
        publish();
    } else {
        cancel();
        publish();
    }
}
void DynamicCoverController::request()
{
    if (!m_active || m_request || !m_track.localPath.isEmpty() || !m_track.songId)
        return;
    // Cache up to 64 artwork resources for five minutes, including misses.
    // Credentials and signed artwork URLs stay out of preferences and queue SQL.
    const auto key = cacheKey();
    const auto found = m_cache.constFind(key);
    if (found != m_cache.cend() && found->expires > m_clock.elapsed()) {
        m_urls = found->urls;
        m_index = 0;

        m_cacheOrder.removeAll(key);
        m_cacheOrder.append(key);
        publish();
        return;
    }
    m_cache.remove(key);
    m_cacheOrder.removeAll(key);
    m_urls.clear();
    m_index = 0;

    m_revision = m_api.sessionRevision();
    m_request = m_api.albumDynamicCover(m_track.songId, m_track.albumId);
    publish();
}
void DynamicCoverController::succeeded(quint64 id, const QByteArray &bytes)
{
    if (!id || id != m_request || m_revision != m_api.sessionRevision() || !m_active)
        return;
    m_request = 0;
    const auto document = QJsonDocument::fromJson(bytes);
    // Qt consumes only the Rust domain result, never upstream dycover fields.
    if (document.isNull() && bytes.trimmed() != "null")
        return;
    if (document.isObject()) {
        const auto domain = document.object();
        for (const auto &entry : domain.value("urls").toArray()) {
            if (!entry.toString().isEmpty())
                m_urls.append(entry.toString());
        }
    } else if (!bytes.trimmed().startsWith("null"))
        return;

    const auto key = cacheKey();
    m_cache.insert(key, {m_urls, m_clock.elapsed() + 300000});
    m_cacheOrder.removeAll(key);
    m_cacheOrder.append(key);
    while (m_cacheOrder.size() > 64)
        m_cache.remove(m_cacheOrder.takeFirst());
    m_index = 0;
    publish();
}
void DynamicCoverController::failed(quint64 id, int, const QByteArray &)
{
    if (!id || id != m_request || m_revision != m_api.sessionRevision())
        return;
    m_request = 0;
    // Optional artwork failure falls back to the static cover, no playback error.
    m_urls.clear();

    publish();
}
void DynamicCoverController::nextSource(const QUrl &failedSource)
{
    // A retiring decoder may report an error after a track/backup change.
    // Only the surface which owns the current URL may advance its fallback.
    if (m_active && !failedSource.isEmpty() && failedSource == source() &&
        m_index < m_urls.size()) {
        ++m_index;
        publish();
    }
}
} // namespace EchoMusic
