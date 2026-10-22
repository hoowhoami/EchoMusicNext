#include "LyricSourceController.h"
#include "LyricExtensions.h"
#include <QTimer>
#include <utility>
#include <algorithm>
namespace EchoMusic
{
void LyricSourceController::cancelExtension()
{
    ++m_generation;
    delete m_extensionJob.data();
    m_extensionJob = nullptr;
}
void LyricSourceController::suspend()
{
    cancelExtension();
    for (auto *id : {&m_search, &m_defaultSearch, &m_download}) {
        const auto pending = std::exchange(*id, 0);
        if (pending)
            m_api.cancel(pending);
    }
    m_activatePending = m_applyPending = m_restorePending = false;
    m_selectionOpen = m_openAfterAuto = false;
    if (m_state == "loading")
        m_state = m_baseLyrics.value("lines").toArray().isEmpty() ? "empty" : "ready";
    emit changed();
}
QString LyricSourceController::activeSource() const
{
    if (m_manualId.isEmpty() && !m_writes.isEmpty())
        return m_writes.last().source;
    for (const auto &candidate : m_candidates)
        if (candidate.id == m_currentId)
            return candidate.source;
    return {};
}
void LyricSourceController::includePlugin(QList<Candidate> &list) const
{
    if (m_pluginCandidate && m_extensions &&
        m_extensions->contains(m_pluginCandidate->id, m_pluginRevision) &&
        std::none_of(list.cbegin(), list.cend(),
                     [this](const auto &c) { return c.id == m_pluginCandidate->id; }))
        list.prepend(*m_pluginCandidate);
}
void LyricSourceController::publishEffective()
{
    const auto empty =
        QJsonObject{{"scrollable", false}, {"offset_ms", 0}, {"lines", QJsonArray{}}};
    const auto lyrics =
        m_manualId.isEmpty() && !m_writes.isEmpty() ? m_writes.last().lyrics : m_baseLyrics;
    emit activated(lyrics.isEmpty() ? empty : lyrics, m_track.stableKey());
}
bool LyricSourceController::installWrite(const QString &key, const QString &revision,
                                         const QString &source, const QJsonObject &lyrics)
{
    if (!hasTrack() || !m_manualId.isEmpty() || m_applyPending || !lyrics.contains("lines"))
        return false;
    m_writes.removeIf([&](const auto &write) { return write.key == key; });
    if (m_writes.size() >= 128)
        return false;
    m_writes.append({key, revision, source, lyrics});
    publishEffective();
    notifyRows();
    return true;
}
void LyricSourceController::removeWrite(const QString &key, const QString &revision)
{
    if (m_writes.removeIf(
            [&](const auto &write) { return write.key == key && write.revision == revision; })) {
        publishEffective();
        notifyRows();
    }
}
void LyricSourceController::reload(bool preserveCurrent)
{
    if (!hasTrack())
        return;
    ++m_choiceRevision;
    m_cache.clear();
    m_cacheOrder.clear();
    beginAutomatic(true, preserveCurrent);
}
void LyricSourceController::beginAutomatic(bool force, bool preserveCurrent)
{
    cancelExtension();
    if (!m_extensions ||
        (!m_restorePending && !m_manualId.isEmpty() && !m_manualId.startsWith("plugin/"))) {
        if (m_manualId != "embedded" || m_restorePending)
            startSearch(!m_restorePending && !m_manualId.isEmpty(), true,
                        m_restorePending ? QString() : m_savedKeywords);
        return;
    }
    for (auto *id : {&m_search, &m_defaultSearch, &m_download}) {
        const auto pending = std::exchange(*id, 0);
        if (pending)
            m_api.cancel(pending);
    }
    m_activatePending = true;
    m_state = preserveCurrent && !m_baseLyrics.isEmpty() ? m_state : "loading";
    m_error.clear();
    if (!preserveCurrent) {
        m_baseLyrics = {};
        m_currentId.clear();
        publishEffective();
    }
    const auto generation = m_generation;
    const auto key = m_track.stableKey();
    const auto only = m_restorePending ? QString() : m_manualId;
    m_extensionJob = m_extensions->resolve(
        m_track, force, preserveCurrent, only, this,
        [this, generation, key, only, preserveCurrent](LyricExtensions::Result result) {
            if (generation != m_generation || key != m_track.stableKey() ||
                m_revision != m_api.sessionRevision())
                return;
            m_extensionJob = nullptr;
            if (result.lyrics.isEmpty()) {
                const auto oldKey = m_pluginCandidate ? m_pluginCandidate->id : QString();
                m_pluginCandidate.reset();
                m_pluginLyrics = {};
                m_pluginRevision.clear();
                if (m_manualCandidate && m_manualCandidate->id == oldKey)
                    m_manualCandidate.reset();
                if (m_currentId == oldKey)
                    m_currentId.clear();
                beginResetModel();
                m_candidates.removeIf([&](const auto &c) { return c.id == oldKey; });
                m_autoCandidates.removeIf([&](const auto &c) { return c.id == oldKey; });
                m_selected = -1;
                endResetModel();
                m_preview.clear();
                if (!only.isEmpty()) {
                    m_activatePending = m_applyPending = false;
                    m_state = "error";
                    m_error = tr("已选插件歌词暂不可用，请重新选择或使用智能推荐");
                    if (!preserveCurrent)
                        publishEffective();
                    notifyRows();
                } else
                    startSearch(false, true);
                return;
            }
            Candidate candidate;
            candidate.id = result.key;
            candidate.title = m_track.title;
            candidate.artist = m_track.artist;
            candidate.source = result.source;
            candidate.duration = qint64(m_track.durationMs);
            candidate.scrollable = result.lyrics.value("scrollable").toBool();
            for (const auto &value : result.lyrics.value("lines").toArray()) {
                const auto line = value.toObject();
                candidate.hasTranslation |= !line.value("translation").toString().isEmpty();
                candidate.hasRomanization |= !line.value("romanization").toString().isEmpty();
                for (const auto &word : line.value("words").toArray())
                    candidate.wordTimed |= word.toObject().value("duration_ms").toInteger() > 0;
            }
            m_pluginCandidate = candidate;
            m_pluginRevision = result.revision;
            m_pluginLyrics = result.lyrics;
            m_pendingCandidates = {candidate};
            m_allCandidates = false;
            m_searchKeywords.clear();
            m_keywordFallback = false;
            if (m_manualId.isEmpty() || m_restorePending)
                m_currentId.clear();
            commitCandidates(candidate.id);
        });
    emit changed();
}
void LyricSourceController::extensionsChanged()
{
    if (!m_pluginCandidate || m_extensions->contains(m_pluginCandidate->id, m_pluginRevision)) {
        if (hasTrack() && m_manualId.isEmpty()) {
            const auto generation = m_generation;
            QTimer::singleShot(0, this, [this, generation] {
                if (generation == m_generation)
                    beginAutomatic(true, true);
            });
        }
        return;
    }
    const auto key = m_pluginCandidate->id;
    cancelExtension();
    const bool wasCurrent = m_currentId == key;
    m_pluginCandidate.reset();
    m_pluginLyrics = {};
    m_pluginRevision.clear();
    if (m_manualCandidate && m_manualCandidate->id == key)
        m_manualCandidate.reset();
    beginResetModel();
    m_candidates.removeIf([&](const auto &c) { return c.id == key; });
    m_autoCandidates.removeIf([&](const auto &c) { return c.id == key; });
    m_pendingCandidates.removeIf([&](const auto &c) { return c.id == key; });
    m_selected = -1;
    endResetModel();
    m_preview.clear();
    if (wasCurrent) {
        m_currentId.clear();
        m_baseLyrics = {};
        m_state = "empty";
        publishEffective();
        if (m_manualId == key) {
            m_state = "error";
            m_error = tr("已选插件歌词暂不可用，请重新选择或使用智能推荐");
        } else {
            // Never restart resolution reentrantly from registration teardown.
            const auto generation = m_generation;
            QTimer::singleShot(0, this, [this, generation] {
                if (generation == m_generation && hasTrack())
                    beginAutomatic(true);
            });
        }
    }
    notifyRows();
}
} // namespace EchoMusic
