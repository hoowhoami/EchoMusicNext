#include "CloudAudioIndex.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <utility>
namespace EchoMusic
{
namespace
{
QString idKey(const QString &prefix, const QJsonValue &id)
{
    const auto text = id.toVariant().toString();
    bool ok = false;
    const auto n = text.toULongLong(&ok);
    return ok && n ? prefix + QString::number(n) : QString();
}
QString hashKey(const QString &prefix, const QString &hash)
{
    return hash.isEmpty() ? QString() : prefix + hash.trimmed().toLower();
}
} // namespace
CloudAudioIndex::CloudAudioIndex(ApiClient &api, QObject *parent) : QObject(parent), m_api(api)
{
    connect(&api, &ApiClient::sessionChanged, this, &CloudAudioIndex::invalidate);
    connect(&api, &ApiClient::succeeded, this, &CloudAudioIndex::accept);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &) {
        if (id && id == m_request) {
            m_request = 0;
            m_pending.clear();
            emit settled();
        }
    });
}
void CloudAudioIndex::invalidate()
{
    const auto id = std::exchange(m_request, 0);
    if (id)
        m_api.cancel(id);
    m_ready = false;
    m_index.clear();
    m_pending.clear();
    m_page = 0;
    m_loaded = 0;
    m_revision = m_api.sessionRevision();
    emit settled();
}
void CloudAudioIndex::ensure()
{
    if (m_ready || m_request)
        return;
    m_revision = m_api.sessionRevision();
    if (!m_api.currentAccountUserId()) {
        m_ready = true;
        emit settled();
        return;
    }
    m_pending.clear();
    m_page = 0;
    m_loaded = 0;
    next();
}
void CloudAudioIndex::next()
{
    m_request = m_api.userCloud(++m_page, 100);
    if (!m_request) {
        m_pending.clear();
        emit settled();
    }
}
QJsonObject CloudAudioIndex::fromRow(const QJsonObject &row)
{
    const auto song = row.value("song").toObject();
    return {{"hash", song.value("hash")},
            {"hash_std", row.value("hash_std")},
            {"size_bytes", row.value("size_bytes")},
            {"song_id", song.value("id").toVariant().toLongLong()},
            {"audio_id", row.value("audio_id")},
            {"file_id", row.value("file_id")},
            {"name", row.value("source_file_name")},
            // Presentation facts for surfaces whose provider snapshot stripped
            // them (cloud playlist entries keep only title + hash).
            {"title", song.value("title")},
            {"artists", song.value("artists")},
            {"duration_ms", song.value("duration_ms")},
            {"album", song.value("album_title")},
            {"cover_url", song.value("cover_url")}};
}
QJsonObject CloudAudioIndex::byHash(const QString &hash) const
{
    if (!m_ready)
        return {};
    return m_index.value(hashKey("hash:", hash));
}
void CloudAudioIndex::accept(quint64 id, const QByteArray &bytes)
{
    if (!id || id != m_request || m_revision != m_api.sessionRevision())
        return;
    m_request = 0;
    const auto page = QJsonDocument::fromJson(bytes).object().value("page").toObject();
    const auto rows = page.value("items").toArray();
    for (const auto &value : rows) {
        const auto s = fromRow(value.toObject());
        if (s.value("hash").toString().isEmpty())
            continue;
        const QStringList keys{idKey("song:", s.value("song_id")),
                               idKey("audio:", s.value("audio_id")),
                               hashKey("std:", s.value("hash_std").toString()),
                               hashKey("hash:", s.value("hash").toString())};
        for (const auto &key : keys)
            if (!key.isEmpty() && !m_pending.contains(key))
                m_pending.insert(key, s);
    }
    m_loaded += rows.size();
    const auto total = page.value("total");
    if (!rows.isEmpty() &&
        (total.isDouble() ? m_loaded < quint64(total.toInteger()) : rows.size() == 100)) {
        next();
        return;
    }
    m_index = std::move(m_pending);
    m_ready = true;
    emit settled();
}
QJsonObject CloudAudioIndex::source(const PlaybackTrack &track) const
{
    const auto embedded = track.catalogMetadata.value("cloud_source").toObject();
    if (!embedded.value("hash").toString().isEmpty())
        return embedded;
    const auto metadata = track.catalogMetadata;
    const QStringList keys{
        idKey("song:", qint64(track.songId)), idKey("audio:", metadata.value("audio_id")),
        hashKey("std:", metadata.value("hash_std").toString()), hashKey("hash:", track.hash)};
    for (const auto &key : keys)
        if (!key.isEmpty() && m_index.contains(key))
            return m_index.value(key);
    return {};
}
} // namespace EchoMusic

EchoMusic::CloudAudioIndex::~CloudAudioIndex()
{
    if (m_request)
        m_api.cancel(m_request);
}
