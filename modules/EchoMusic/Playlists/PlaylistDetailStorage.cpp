#include "PlaylistDetailContext.h"
#include "CloudAudioIndex.h"
#include <QTimer>
#include <algorithm>
namespace EchoMusic
{
void PlaylistDetailContext::clearStoredRows()
{
    ++m_sourceEpoch;
    delete m_sourceRead.data();
    delete m_hintRead.data();
    delete m_coverRead.data();
    m_storingPage = m_enriching = false;
    m_cachedRows.clear();
    m_sourceRows.clear();
    m_sparseIds.clear();
    m_hintAttempted.clear();
}
void PlaylistDetailContext::consumeStoredBatch()
{
    if (m_closed || !active() || m_storingPage)
        return;
    if (m_pageErrors.contains(m_nextPage)) {
        failMessage(m_pageErrors.take(m_nextPage));
        return;
    }
    if (!m_pages.contains(m_nextPage)) {
        const auto first = m_nextPage;
        for (quint32 page = first; page < first + 3; ++page)
            requestPage(page);
        return;
    }
    const auto page = m_pages.take(m_nextPage);
    const auto items = page.value("items").toArray();
    QJsonArray songs;
    QList<QJsonObject> entries;
    QSet<QString> accepted;
    quint64 filtered = 0;
    for (const auto &value : items) {
        auto entry = value.toObject();
        auto song = entry.value("song").toObject();
        const auto hash = song.value("hash").toString().trimmed().toLower();
        if (hash.isEmpty()) {
            ++filtered;
            continue;
        }
        if (m_cloud) {
            if (!song.value("id").toString().toULongLong())
                song.insert("id", "cloud:" + hash);
            const auto id = song.value("id").toString();
            if (m_cloudEntryIds.contains(id) || accepted.contains(id))
                continue;
            accepted.insert(id);
            entry.insert("song", song);
            const auto facts = m_playback.playbackFacts(hash);
            if (!facts.isEmpty())
                applyFactsToEntry(entry, facts);
            song = entry.value("song").toObject();
        }
        song.insert("metadata", entry.value("metadata"));
        songs.append(song);
        entries.append(entry);
    }
    const auto generation = m_generation, epoch = m_sourceEpoch;
    m_storingPage = true;
    auto *request = new QObject(this);
    m_sourceRead = request;
    const QPointer<QObject> guard(request);
    m_sourceCache->cache(
        std::move(songs), request,
        [this, generation, epoch, guard, entries, items, page, filtered](auto rows, auto error) {
            if (!guard)
                return;
            guard->deleteLater();
            if (m_closed || generation != m_generation || epoch != m_sourceEpoch)
                return;
            m_storingPage = false;
            if (!error.isEmpty()) {
                failMessage(error);
                return;
            }
            m_received += quint64(items.size());
            m_filteredInvalidCount += filtered;
            m_total = quint64(page.value("total").toInteger());
            if (m_cloud) {
                for (int i = 0; i < entries.size(); ++i) {
                    const auto &entry = entries[i];
                    const auto song = entry.value("song").toObject();
                    const auto id = song.value("id").toString();
                    m_cloudEntryIds.insert(id);
                    m_sourceRows.insert(id, rows[i]);
                    m_cloudEntries.append({{"song", QJsonObject{{"id", song.value("id")},
                                                                {"hash", song.value("hash")}}},
                                           {"file_id", entry.value("file_id")},
                                           {"sort_index", entry.value("sort_index")}});
                    // A hint may fill artist/album/duration/artwork; it never
                    // becomes another complete source snapshot in the page.
                    bool artistKnown = false;
                    for (const auto &artist : song.value("artists").toArray()) {
                        const auto name = artist.toObject().value("name").toString().trimmed();
                        artistKnown |= !name.isEmpty() && !SongMetadataService::maskedName(name);
                    }
                    if (!artistKnown || song.value("duration_ms").toInteger() <= 0 ||
                        song.value("cover_url").toString().trimmed().isEmpty() ||
                        song.value("album_title").toString().trimmed().isEmpty())
                        m_sparseIds.insert(id);
                }
                std::stable_sort(m_cloudEntries.begin(), m_cloudEntries.end(),
                                 [](const auto &a, const auto &b) {
                                     return a.value("sort_index").toInteger() <
                                            b.value("sort_index").toInteger();
                                 });
                m_cachedRows.clear();
                m_cloudEntryRows.clear();
                for (int i = 0; i < m_cloudEntries.size(); ++i) {
                    const auto id =
                        m_cloudEntries[i].value("song").toObject().value("id").toString();
                    m_cloudEntryRows.insert(id, i);
                    m_cachedRows.append(m_sourceRows.value(id));
                }
            } else
                m_cachedRows.append(rows);
            const bool finished = items.isEmpty() || (m_total > 0 && m_received >= m_total);
            if (!m_reconcilingTracks || finished)
                m_songs.replaceCachedRows(m_cachedRows, true);
            ++m_nextPage;
            if (finished) {
                m_complete = true;
                m_reconcilingTracks = false;
                cancelPageRequests();
                publishCover();
            } else
                consumeStoredBatch();
            if (m_network && !m_sparseIds.isEmpty() && !m_playback.cloudSources()->complete())
                m_playback.cloudSources()->ensure();
            enrichStoredEntries();
            emit changed();
        });
}
void PlaylistDetailContext::enrichStoredEntries()
{
    if (!m_sourceCache || m_closed || !active() || !m_complete || m_reconcilingTracks ||
        m_enriching || !m_songs.presentationsReady() || !m_playback.cloudSources()->complete())
        return;
    QStringList ids, hashes;
    for (const auto &id : m_sparseIds)
        if (!m_hintAttempted.contains(id) && m_cloudEntryRows.contains(id)) {
            ids.append(id);
            hashes.append(m_cloudEntries[m_cloudEntryRows.value(id)]
                              .value("song")
                              .toObject()
                              .value("hash")
                              .toString());
            m_hintAttempted.insert(id);
            if (ids.size() == 128)
                break;
        }
    if (ids.isEmpty())
        return;
    m_enriching = true;
    auto *request = new QObject(this);
    m_hintRead = request;
    const QPointer<QObject> guard(request);
    const auto epoch = m_sourceEpoch, generation = m_generation;
    m_playback.cloudSources()->requestHints(
        hashes, request, [this, ids, epoch, generation, guard](auto hints, auto error) {
            if (!guard)
                return;
            if (m_closed || epoch != m_sourceEpoch || generation != m_generation) {
                guard->deleteLater();
                return;
            }
            if (!error.isEmpty()) {
                m_enriching = false;
                guard->deleteLater();
                QTimer::singleShot(0, this, &PlaylistDetailContext::enrichStoredEntries);
                return;
            }
            m_songs.requestPresentations(
                ids, guard, [this, epoch, generation, guard, hints](QJsonArray values) {
                    if (!guard)
                        return;
                    guard->deleteLater();
                    if (m_closed || epoch != m_sourceEpoch || generation != m_generation)
                        return;
                    m_enriching = false;
                    QJsonArray projected;
                    for (const auto &value : values) {
                        auto song = value.toObject();
                        const auto metadata = song.take("metadata");
                        QJsonObject entry{{"song", song}, {"metadata", metadata}};
                        const auto hash = song.value("hash").toString().trimmed().toLower();
                        if (enrichEntry(entry, hints.value(hash))) {
                            auto updated = entry.value("song").toObject();
                            updated.insert("metadata", entry.value("metadata"));
                            projected.append(updated);
                        }
                    }
                    if (!projected.isEmpty())
                        m_songs.applyPresentations(projected);
                    enrichStoredEntries();
                    if (m_complete)
                        publishCover();
                });
        });
}
void PlaylistDetailContext::publishStoredCover()
{
    delete m_coverRead.data();
    auto *request = new QObject(this);
    m_coverRead = request;
    const QPointer<QObject> guard(request);
    const auto generation = m_generation, session = m_api.sessionRevision(), cloud = m_cloud->value,
               revision = *m_revision;
    m_songs.requestCover(request,
                         [this, guard, generation, session, cloud, revision](QString cover) {
                             if (!guard)
                                 return;
                             guard->deleteLater();
                             if (m_closed || !active() || generation != m_generation ||
                                 session != m_api.sessionRevision() || !m_cloud ||
                                 m_cloud->value != cloud || !m_revision || *m_revision != revision)
                                 return;
                             m_library.publishAutoCover({cloud}, revision, cover, session);
                         });
}
} // namespace EchoMusic
