#pragma once
#include "PlaybackTrack.h"
#include <QHash>
#include <QSet>

namespace EchoMusic
{
/// Ephemeral incoming-song evidence. Consumes native domain values only;
/// lyric labels never create catalog identities or playback authorization.
class ShareSongRecovery
{
public:
    enum class PageResult
    {
        More,
        Complete,
        Incomplete
    };
    PlaybackTrack track;
    bool removed = false;
    quint32 page = 0;
    static constexpr quint32 PageSize = 30;
    void readCandidates(const QJsonArray &);
    PageResult readPage(const QJsonObject &);
    void applyMatch();

private:
    QJsonArray m_hints;
    QHash<quint64, QJsonObject> m_exact, m_matches;
    QSet<QByteArray> m_pages;
    quint64 m_received = 0;
};
} // namespace EchoMusic
