#include "ShareSongRecovery.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>

namespace EchoMusic
{
namespace
{
QString normalized(const QString &text)
{
    QString result;
    for (const auto c : text.normalized(QString::NormalizationForm_KC).toCaseFolded())
        if (c.isLetterOrNumber())
            result.append(c);
    return result;
}
QStringList artists(const QString &text)
{
    QStringList result;
    const auto names = text.split(QRegularExpression("[,，/、&;；]"), Qt::SkipEmptyParts);
    for (const auto &name : names)
        if (const auto key = normalized(name); !key.isEmpty())
            result.append(key);
    result.removeDuplicates();
    result.sort();
    return result;
}
QString titleKey(QString title, const QString &artist)
{
    // Mediastore display names can contain the explicit singer prefix. Keep
    // separators inside the real title and never strip an unrelated prefix.
    const auto separator = title.indexOf(" - ");
    if (separator > 0 && !artists(artist).isEmpty() &&
        artists(title.left(separator)) == artists(artist))
        title = title.mid(separator + 3);
    return normalized(title);
}
bool closeDuration(quint64 first, quint64 second)
{
    // Observed lyric durations differ from album audio by up to 2257 ms.
    // Unknown durations cannot support an identity match.
    return first && second && (std::max(first, second) - std::min(first, second) <= 3000);
}
bool sameHash(const QString &first, const QString &second)
{
    return !first.isEmpty() && first.compare(second, Qt::CaseInsensitive) == 0;
}
} // namespace
void ShareSongRecovery::readCandidates(const QJsonArray &values)
{
    if (!track.title.isEmpty())
        m_hints.append(QJsonObject{{"title", track.title},
                                   {"artist", track.artist},
                                   {"duration_ms", qint64(track.durationMs)}});
    QSet<QString> labels;
    QJsonObject display;
    for (const auto &value : values) {
        const auto candidate = value.toObject();
        const auto title = candidate.value("title").toString().trimmed();
        const auto artist = candidate.value("artist").toString().trimmed();
        const auto duration =
            quint64(std::max(qint64(0), candidate.value("duration_ms").toInteger()));
        if (normalized(title).isEmpty() ||
            (!track.title.isEmpty() &&
             titleKey(title, artist) !=
                 titleKey(track.title, track.artist.isEmpty() ? artist : track.artist)) ||
            (!track.artist.isEmpty() && artists(artist) != artists(track.artist)) ||
            (track.durationMs && duration && !closeDuration(track.durationMs, duration)))
            continue;
        // Discard access keys and attribution; only bounded matching evidence
        // lives until this share resolves or is cancelled.
        const QJsonObject hint{
            {"title", title}, {"artist", artist}, {"duration_ms", qint64(duration)}};
        if (!m_hints.contains(hint))
            m_hints.append(hint);
        labels.insert(normalized(title) + "\n" + artists(artist).join('/'));
        display = hint;
    }
    if (labels.size() == 1) {
        if (track.title.isEmpty())
            track.title = display.value("title").toString();
        if (track.artist.isEmpty())
            track.artist = display.value("artist").toString();
    }
}
ShareSongRecovery::PageResult ShareSongRecovery::readPage(const QJsonObject &response)
{
    const auto data = response.value("page").toObject();
    const auto items = data.value("items").toArray();
    if (items.isEmpty())
        return PageResult::Complete;
    const auto fingerprint = QCryptographicHash::hash(
        QJsonDocument(items).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
    if (m_pages.contains(fingerprint))
        return PageResult::Incomplete;
    m_pages.insert(fingerprint);
    m_received += quint64(items.size());
    for (const auto &value : items) {
        auto song = value.toObject().value("song").toObject();
        const auto metadata = value.toObject().value("metadata").toObject();
        const auto id = song.value("id").toString().toULongLong();
        const auto album = song.value("album_id").toString().toULongLong();
        if (!id || (album && album != track.albumId))
            continue;
        bool exact = sameHash(song.value("hash").toString(), track.hash);
        for (const auto &resource : metadata.value("resources").toArray())
            exact |= sameHash(resource.toObject().value("hash").toString(), track.hash);
        if (exact)
            m_exact.insert(id, song);
        QStringList names;
        for (const auto &author : song.value("artists").toArray())
            names.append(author.toObject().value("name").toString());
        const auto artist = artists(names.join('/'));
        const auto title = titleKey(song.value("title").toString(), names.join('/'));
        const auto duration = quint64(std::max(qint64(0), song.value("duration_ms").toInteger()));
        for (const auto &hintValue : m_hints) {
            const auto hint = hintValue.toObject();
            if (!title.isEmpty() && !artist.isEmpty() &&
                title ==
                    titleKey(hint.value("title").toString(), hint.value("artist").toString()) &&
                artist == artists(hint.value("artist").toString()) &&
                closeDuration(duration, quint64(hint.value("duration_ms").toInteger())))
                m_matches.insert(id, song);
        }
    }
    const auto total = data.value("total");
    if ((!total.isNull() && !total.isUndefined() &&
         m_received >= quint64(std::max(qint64(0), total.toInteger()))) ||
        ((total.isNull() || total.isUndefined()) && items.size() < PageSize))
        return PageResult::Complete;
    // Sequential pages, bounded evidence and no background continuation.
    return page >= 100 ? PageResult::Incomplete : PageResult::More;
}
void ShareSongRecovery::applyMatch()
{
    const auto &matches = m_exact.isEmpty() ? m_matches : m_exact;
    if (matches.size() != 1)
        return;
    const auto song = matches.cbegin().value();
    const auto matched = PlaybackTrack::fromDomain(song);
    track.songId = matches.cbegin().key();
    track.id = QString::number(track.songId);
    if (track.title.isEmpty())
        track.title = matched.title;
    if (track.artist.isEmpty())
        track.artist = matched.artist;
    if (track.album.isEmpty())
        track.album = matched.album;
    if (track.cover.isEmpty())
        track.cover = matched.cover;
    if (!track.durationMs) {
        track.durationMs = matched.durationMs;
        if (matched.durationMs)
            track.catalogMetadata.insert("catalog_duration_ms", qint64(matched.durationMs));
    }
    track.catalogMetadata.insert("artist_refs", song.value("artists"));
}
} // namespace EchoMusic
