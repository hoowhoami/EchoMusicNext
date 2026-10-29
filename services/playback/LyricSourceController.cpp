#include "SettingsKeys.h"
#include "LyricSourceController.h"
#include "LyricExtensions.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <algorithm>
#include <utility>
#include <QRegularExpression>
#include <QFileInfo>
namespace
{
class PreviewRows final : public QSortFilterProxyModel
{
    EchoMusic::LyricModel &m_lyrics;

public:
    explicit PreviewRows(EchoMusic::LyricModel &lyrics) : m_lyrics(lyrics)
    {
        setSourceModel(&lyrics);
        connect(&lyrics, &EchoMusic::LyricModel::changed, this, [this] {
            beginFilterChange();
            endFilterChange(Direction::Rows);
        });
    }
    bool filterAcceptsRow(int row, const QModelIndex &) const override
    {
        return m_lyrics.scrollable() || row < 18;
    }
};
} // namespace
namespace EchoMusic
{
LyricSourceController::LyricSourceController(ApiClient &api, QSettings &settings, QObject *parent)
    : QAbstractListModel(parent), m_api(api), m_settings(settings), m_preview(this),
      m_previewRows(std::make_unique<PreviewRows>(m_preview))
{
    m_extensions = std::make_unique<LyricExtensions>(this);
    connect(m_extensions.get(), &LyricExtensions::changed, this,
            &LyricSourceController::extensionsChanged);
    connect(&api, &ApiClient::succeeded, this, &LyricSourceController::succeeded);
    connect(&api, &ApiClient::failed, this, &LyricSourceController::failed);
    connect(&api, &ApiClient::sessionChanged, this, &LyricSourceController::clear);
    connect(&api, &ApiClient::closed, this, [this] {
        clear();
        m_cache.clear();
        m_cacheOrder.clear();
    });
}
LyricSourceController::~LyricSourceController()
{
    cancelExtension();
    if (m_search)
        m_api.cancel(m_search);
    if (m_download)
        m_api.cancel(m_download);
    if (m_defaultSearch)
        m_api.cancel(m_defaultSearch);
}
int LyricSourceController::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_candidates.size();
}
QHash<int, QByteArray> LyricSourceController::roleNames() const
{
    return {{Qt::UserRole + 1, "sourceId"},        {Qt::UserRole + 2, "sourceTitle"},
            {Qt::UserRole + 3, "sourceArtist"},    {Qt::UserRole + 4, "durationMs"},
            {Qt::UserRole + 5, "sourceLabel"},     {Qt::UserRole + 6, "language"},
            {Qt::UserRole + 7, "wordTimed"},       {Qt::UserRole + 8, "score"},
            {Qt::UserRole + 9, "scrollable"},      {Qt::UserRole + 10, "current"},
            {Qt::UserRole + 11, "recommended"},    {Qt::UserRole + 12, "sourceNickname"},
            {Qt::UserRole + 13, "hasTranslation"}, {Qt::UserRole + 14, "hasRomanization"}};
}
QVariant LyricSourceController::data(const QModelIndex &i, int role) const
{
    if (!i.isValid() || i.row() < 0 || i.row() >= m_candidates.size())
        return {};
    const auto &c = m_candidates[i.row()];
    switch (role) {
    case Qt::UserRole + 1:
        return c.id;
    case Qt::UserRole + 2:
        return c.title;
    case Qt::UserRole + 3:
        return c.artist;
    case Qt::UserRole + 4:
        return c.duration;
    case Qt::UserRole + 5:
        return c.source;
    case Qt::UserRole + 6:
        return c.language;
    case Qt::UserRole + 7:
        return c.wordTimed;
    case Qt::UserRole + 8:
        return c.score;
    case Qt::UserRole + 9:
        return c.scrollable;
    case Qt::UserRole + 10:
        return c.id == currentId();
    case Qt::UserRole + 11:
        return automaticSearch() && c.id == m_recommendedId;
    case Qt::UserRole + 12:
        return c.nickname;
    case Qt::UserRole + 13:
        return c.hasTranslation;
    case Qt::UserRole + 14:
        return c.hasRomanization;
    default:
        return {};
    }
}
const LyricSourceController::Candidate *LyricSourceController::selected() const
{
    return m_selected >= 0 && m_selected < m_candidates.size() ? &m_candidates[m_selected]
                                                               : nullptr;
}
QString LyricSourceController::selectedTitle() const
{
    const auto *c = selected();
    return c ? c->title : QString();
}
QString LyricSourceController::selectedArtist() const
{
    const auto *c = selected();
    return c ? c->artist : QString();
}
QString LyricSourceController::selectedNickname() const
{
    const auto *c = selected();
    return c ? c->nickname : QString();
}
QString LyricSourceController::selectedSource() const
{
    const auto *c = selected();
    return c ? c->source : QString();
}
qint64 LyricSourceController::selectedDuration() const
{
    const auto *c = selected();
    return c ? c->duration : 0;
}
QString LyricSourceController::preferenceKey() const
{
    return EchoMusic::SettingsKeys::Lyrics::SourcesPrefix +
           QString::fromLatin1(
               QCryptographicHash::hash(m_track.stableKey().toUtf8(), QCryptographicHash::Sha256)
                   .toHex());
}
void LyricSourceController::notifyRows()
{
    if (rowCount())
        emit dataChanged(index(0), index(rowCount() - 1));
    emit changed();
}
void LyricSourceController::clear()
{
    ++m_choiceRevision;
    cancelExtension();
    m_writes.clear();
    m_pluginCandidate.reset();
    m_pluginLyrics = m_baseLyrics = {};
    m_pluginRevision.clear();
    if (m_search)
        m_api.cancel(m_search);
    if (m_download)
        m_api.cancel(m_download);
    if (m_defaultSearch)
        m_api.cancel(m_defaultSearch);
    m_search = m_download = m_defaultSearch = 0;
    m_pendingCandidates.clear();
    m_autoCandidates.clear();
    m_catalogReady = m_allCandidates = m_selectionOpen = m_openAfterAuto = false;
    m_revision = m_api.sessionRevision();
    beginResetModel();
    m_candidates.clear();
    m_selected = -1;
    endResetModel();
    m_preview.clear();
    m_track = {};
    m_embeddedTitle.clear();
    m_embeddedArtist.clear();
    m_embeddedError.clear();
    m_embeddedLyrics = {};
    m_triedKeywords.clear();
    m_currentId.clear();
    m_manualId.clear();
    m_manualCandidate.reset();
    m_searchTitle.clear();
    m_searchArtist.clear();
    m_searchKeywords.clear();
    m_autoKeywords.clear();
    m_keywordFallback = false;
    m_savedKeywords.clear();
    m_queuedKeywords.clear();
    m_recommendedId.clear();
    m_downloadId.clear();
    m_error.clear();
    m_state = "empty";
    m_activatePending = m_applyPending = m_restorePending = false;
    emit changed();
}
void LyricSourceController::load(const PlaybackTrack &track)
{
    // Preserve a live plugin contribution across reader/view refreshes. Normal
    // source loads still re-resolve remembered IDs with fresh access keys.
    if (m_extensions &&
        (m_extensions->hasResolvers() || m_pluginCandidate || !m_writes.isEmpty()) && hasTrack() &&
        m_track.stableKey() == track.stableKey()) {
        m_track = track;
        beginAutomatic(false, true);
        return;
    }
    const bool reopen = m_selectionOpen;
    clear();
    m_selectionOpen = m_openAfterAuto = reopen;
    m_track = track;
    // Cache and update guards key on the stable identity so a mid-play view-id
    // upgrade (legacy "0" snapshot adopting "cloud:<hash>") cannot orphan the
    // already-fetched embedded lyrics.
    if (m_cacheTrack != track.stableKey()) {
        m_cache.clear();
        m_cacheOrder.clear();
        m_cacheTrack = track.stableKey();
    }
    resetSearchFields();
    if (hasTrack()) {
        m_manualId = m_settings.value(preferenceKey()).toString();
        m_savedKeywords = m_manualId.isEmpty()
                              ? QString()
                              : m_settings.value(preferenceKey() + "/keywords").toString();
        if (m_manualId != "embedded")
            beginAutomatic();
    }
}
void LyricSourceController::openSelection()
{
    m_selectionOpen = true;
    resetSearchFields();
    // Opening a dialog must not cancel the initial automatic lyric activation.
    if ((loading() || previewLoading()) && m_activatePending && !m_applyPending) {
        m_openAfterAuto = true;
        return;
    }
    // Keyword browsing does not replace the full automatic catalog.
    // Reopening uses the existing in-memory hash cache.
    if (!m_catalogReady && !m_autoCandidates.isEmpty()) {
        if (m_search)
            m_api.cancel(m_search);
        if (m_defaultSearch)
            m_api.cancel(m_defaultSearch);
        if (m_download)
            m_api.cancel(m_download);
        ++m_parseRevision;
        delete m_parseJob.data();
        m_parseJob = nullptr;
        m_search = m_defaultSearch = m_download = 0;
        m_pendingCandidates.clear();
        m_searchKeywords = m_autoKeywords;
        m_keywordFallback = !m_autoKeywords.isEmpty();
        beginResetModel();
        m_candidates = m_autoCandidates;
        includeEmbedded(m_candidates);
        if (m_manualCandidate &&
            std::none_of(m_candidates.cbegin(), m_candidates.cend(),
                         [this](const Candidate &c) { return c.id == m_manualId; }))
            m_candidates.prepend(*m_manualCandidate);
        m_selected = -1;
        endResetModel();
        m_catalogReady = true;
    }
    if (!automaticSearch() || !m_catalogReady || m_candidates.isEmpty()) {
        refresh();
        return;
    }
    m_activatePending = false;
    m_selected = m_candidates.isEmpty() ? -1 : 0;
    for (int i = 0; i < m_candidates.size(); ++i)
        if (m_candidates[i].id == m_currentId) {
            m_selected = i;
            break;
        }
    previewSelected();
}
void LyricSourceController::closeSelection()
{
    m_selectionOpen = m_openAfterAuto = false;
    m_queuedKeywords.clear();
    // Automatic loading is independent of the dialog's resource lifetime.
    if (m_activatePending && !m_applyPending)
        return;
    if (m_search)
        m_api.cancel(m_search);
    if (m_defaultSearch)
        m_api.cancel(m_defaultSearch);
    if (m_download)
        m_api.cancel(m_download);
    ++m_parseRevision;
    delete m_parseJob.data();
    m_parseJob = nullptr;
    m_search = m_defaultSearch = m_download = 0;
    m_applyPending = m_activatePending = m_restorePending = false;
    m_pendingCandidates.clear();
    m_preview.clear();
    emit changed();
}
void LyricSourceController::refresh()
{
    if (!hasTrack() || m_applyPending)
        return;
    m_cache.clear();
    m_cacheOrder.clear();
    resetSearchFields();
    m_queuedKeywords.clear();
    startSearch(true, m_currentId.isEmpty());
}
void LyricSourceController::setSearchTitle(const QString &value)
{
    if (m_searchTitle == value)
        return;
    m_searchTitle = value;
    emit changed();
}
void LyricSourceController::setSearchArtist(const QString &value)
{
    if (m_searchArtist == value)
        return;
    m_searchArtist = value;
    emit changed();
}
namespace
{
QString unmask(const QString &value, const QString &embedded)
{
    static const QRegularExpression masked("^\\*+$");
    return masked.match(value.trimmed()).hasMatch() && !embedded.trimmed().isEmpty() &&
                   !masked.match(embedded.trimmed()).hasMatch()
               ? embedded.trimmed()
               : value;
}
QString keywordKey(const QString &value)
{
    return value.simplified().toCaseFolded();
}
} // namespace
void LyricSourceController::resetSearchFields()
{
    m_searchTitle = unmask(m_track.title, m_embeddedTitle);
    m_searchArtist = unmask(m_track.artist, m_embeddedArtist);
    emit changed();
}
QString LyricSourceController::trackKeywords() const
{
    // Automatic matching requires a song title; artist-only is manual browsing.
    const auto title = unmask(m_track.title, m_embeddedTitle).trimmed();
    if (title.isEmpty())
        return {};
    const auto artist = unmask(m_track.artist, m_embeddedArtist).trimmed();
    return artist.isEmpty() ? title : artist + " - " + title;
}
QString LyricSourceController::nextAutomaticKeywords()
{
    const auto title = m_embeddedTitle.isEmpty() ? m_track.title : m_embeddedTitle;
    const auto artist = m_embeddedArtist.isEmpty() ? m_track.artist : m_embeddedArtist;
    const QString embedded =
        title.trimmed().isEmpty()
            ? QString()
            : (artist.trimmed().isEmpty() ? title.trimmed()
                                          : artist.trimmed() + " - " + title.trimmed());
    for (const auto &query : {trackKeywords(), embedded}) {
        const auto key = keywordKey(query);
        if (!key.isEmpty() && !m_triedKeywords.contains(key)) {
            m_triedKeywords.insert(key);
            return query;
        }
    }
    return {};
}
void LyricSourceController::includeEmbedded(QList<Candidate> &list) const
{
    includePlugin(list);
    if (m_embeddedLyrics.value("lines").toArray().isEmpty() ||
        std::any_of(list.cbegin(), list.cend(),
                    [](const Candidate &c) { return c.id == "embedded"; }))
        return;
    Candidate c;
    c.id = "embedded";
    c.title = m_embeddedTitle.isEmpty() ? m_track.title : m_embeddedTitle;
    c.artist = m_embeddedArtist.isEmpty() ? m_track.artist : m_embeddedArtist;
    c.source = tr("内嵌歌词");
    c.duration = qint64(m_track.durationMs);
    c.scrollable = m_embeddedLyrics.value("scrollable").toBool();
    list.prepend(c);
}
QJsonObject LyricSourceController::cachedLyrics(const QString &id) const
{
    return id == "embedded"                                   ? m_embeddedLyrics
           : m_pluginCandidate && m_pluginCandidate->id == id ? m_pluginLyrics
                                                              : m_cache.value(id);
}
void LyricSourceController::cancelParsing()
{
    ++m_parseRevision;
    delete m_parseJob.data();
    m_parseJob = nullptr;
    delete m_embeddedParse.data();
    m_embeddedParse = nullptr;
}
void LyricSourceController::updateEmbeddedDocument(const QString &trackKey, const QString &title,
                                                   const QString &artist, const QString &content,
                                                   const QString &parseError)
{
    if (trackKey != m_track.stableKey() || m_revision != m_api.sessionRevision())
        return;
    delete m_embeddedParse.data();
    m_embeddedParse = nullptr;
    if (!parseError.isEmpty()) {
        updateEmbedded(trackKey, title, artist, {}, parseError);
        return;
    }
    const auto generation = m_generation, session = m_api.sessionRevision();
    m_embeddedParse = m_extensions->parseEmbedded(
        content, this,
        [this, generation, session, trackKey, title, artist](QJsonObject value, QString error) {
            if (generation != m_generation || session != m_api.sessionRevision() ||
                trackKey != m_track.stableKey())
                return;
            m_embeddedParse = nullptr;
            updateEmbedded(trackKey, title, artist, value, error);
        });
}
void LyricSourceController::updateEmbedded(const QString &trackKey, const QString &title,
                                           const QString &artist, const QJsonObject &lyrics,
                                           const QString &parseError)
{
    if (trackKey != m_track.stableKey() || m_revision != m_api.sessionRevision())
        return;
    const bool pristine = m_searchTitle == unmask(m_track.title, m_embeddedTitle) &&
                          m_searchArtist == unmask(m_track.artist, m_embeddedArtist);
    const auto missing = [](const QString &value, const QStringList &unknown) {
        return value.trimmed().isEmpty() || unknown.contains(value.trimmed().toLower());
    };
    if (!title.isEmpty() && (missing(m_track.title, {"未知歌曲", "unknown title", "unknown"}) ||
                             (!m_track.localPath.isEmpty() &&
                              m_track.title == QFileInfo(m_track.localPath).completeBaseName())))
        m_track.title = title;
    if (!artist.isEmpty() &&
        missing(m_track.artist, {"未知歌手", "未知艺术家", "unknown artist", "unknown"}))
        m_track.artist = artist;
    m_embeddedTitle = title;
    m_embeddedArtist = artist;
    m_embeddedLyrics = lyrics;
    m_embeddedError = parseError.left(512);
    if (pristine)
        resetSearchFields();
    if (!lyrics.value("lines").toArray().isEmpty() &&
        std::none_of(m_candidates.cbegin(), m_candidates.cend(),
                     [](const Candidate &c) { return c.id == "embedded"; })) {
        QList<Candidate> embedded;
        includeEmbedded(embedded);
        beginInsertRows({}, 0, 0);
        m_candidates.prepend(embedded.first());
        if (m_selected >= 0)
            ++m_selected;
        endInsertRows();
    }
    if (!lyrics.value("lines").toArray().isEmpty()) {
        QList<Candidate> snapshot;
        includeEmbedded(snapshot);
        for (auto &candidate : m_candidates)
            if (candidate.id == "embedded")
                candidate = snapshot.first();
    }
    if (m_manualId == "embedded" && lyrics.value("lines").toArray().isEmpty() &&
        m_currentId.isEmpty()) {
        m_state = "error";
        m_error = m_embeddedError.isEmpty() ? tr("已选内嵌歌词暂不可用，请重新选择或恢复智能推荐")
                                            : m_embeddedError;
    }
    if (m_currentId == "embedded" && !lyrics.value("lines").toArray().isEmpty()) {
        // A source/quality replacement may update the active reader without
        // changing the song. Refresh its lyrics without cancelling dialog browsing.
        if (selected() && selected()->id == "embedded")
            m_preview.replace(lyrics);
        m_baseLyrics = lyrics;
        publishEffective();
    }
    if (m_manualId == "embedded" && m_currentId.isEmpty() &&
        !lyrics.value("lines").toArray().isEmpty()) {
        if (m_search)
            m_api.cancel(m_search);
        if (m_defaultSearch)
            m_api.cancel(m_defaultSearch);
        if (m_download)
            m_api.cancel(m_download);
        ++m_parseRevision;
        delete m_parseJob.data();
        m_parseJob = nullptr;
        m_search = m_defaultSearch = m_download = 0;
        m_selected = 0;
        m_activatePending = true;
        previewSelected();
    } else if (m_currentId.isEmpty() && m_manualId.isEmpty() && !loading() && !previewLoading()) {
        startSearch(false, true);
    }
    notifyRows();
}
void LyricSourceController::search()
{
    if (!m_selectionOpen || !hasTrack() || m_applyPending || !canSearch())
        return;
    QStringList words;
    if (!m_searchArtist.trimmed().isEmpty())
        words.append(m_searchArtist.trimmed());
    if (!m_searchTitle.trimmed().isEmpty())
        words.append(m_searchTitle.trimmed());
    const auto keywords = words.join(" - ");
    // The initial active lyric must finish independently of dialog browsing.
    if (m_activatePending && (loading() || previewLoading())) {
        m_queuedKeywords = keywords;
        return;
    }
    startSearch(true, false, keywords);
}
void LyricSourceController::startSearch(bool allCandidates, bool activate, const QString &keywords)
{
    cancelExtension();
    if (m_search)
        m_api.cancel(m_search);
    if (m_defaultSearch)
        m_api.cancel(m_defaultSearch);
    if (m_download)
        m_api.cancel(m_download);
    ++m_parseRevision;
    delete m_parseJob.data();
    m_parseJob = nullptr;
    m_search = m_defaultSearch = m_download = 0;
    m_allCandidates = allCandidates;
    m_searchKeywords = keywords;
    m_keywordFallback = false;
    m_activatePending = activate;
    m_catalogReady = false;
    m_pendingCandidates.clear();
    m_triedKeywords.clear();
    if (!keywords.isEmpty())
        m_triedKeywords.insert(keywordKey(keywords));
    // Incremental removal lets the native view retire pending/reused delegates
    // before the replacement result arrives, without an intermediate model reset.
    if (!m_candidates.isEmpty()) {
        beginRemoveRows({}, 0, m_candidates.size() - 1);
        m_candidates.clear();
        endRemoveRows();
    }
    m_selected = -1;
    m_preview.clear();
    m_revision = m_api.sessionRevision();
    if (m_track.hash.isEmpty() && m_searchKeywords.isEmpty()) {
        m_searchKeywords = nextAutomaticKeywords();
        m_keywordFallback = true;
    }
    QList<Candidate> embeddedRows;
    includeEmbedded(embeddedRows);
    if (!embeddedRows.isEmpty()) {
        beginInsertRows({}, 0, embeddedRows.size() - 1);
        m_candidates = embeddedRows;
        if (m_currentId == "embedded" || m_manualId == "embedded")
            m_selected = 0;
        endInsertRows();
        if (m_selected == 0)
            m_preview.replace(m_embeddedLyrics);
    }
    m_search =
        m_searchKeywords.isEmpty() && !m_track.hash.isEmpty()
            ? m_api.lyricCandidates(m_track.hash, m_track.songId, m_track.durationMs, allCandidates)
            : (m_searchKeywords.isEmpty()
                   ? 0
                   : m_api.lyricCandidatesByKeywords(m_searchKeywords, m_track.durationMs,
                                                     allCandidates));
    m_error.clear();
    if (m_currentId.isEmpty())
        m_state = m_search ? "loading" : "error";
    if (!m_search) {
        if (m_track.hash.isEmpty()) {
            commitCandidates({});
            return;
        }
        m_error = tr("歌词候选加载失败");
    }
    emit changed();
}
void LyricSourceController::commitCandidates(const QString &recommendedId)
{
    if (automaticSearch()) {
        m_recommendedId = recommendedId.isEmpty() && !m_pendingCandidates.isEmpty()
                              ? m_pendingCandidates.first().id
                              : recommendedId;
    }
    // Request man=yes candidates, then man=no best match. Move only that match
    // to the front; access keys belong to their fresh list response.
    for (int i = 0; i < m_pendingCandidates.size(); ++i)
        if (m_pendingCandidates[i].id == recommendedId) {
            m_pendingCandidates.move(i, 0);
            break;
        }
    if (automaticSearch()) {
        if (m_allCandidates) {
            m_autoCandidates = m_pendingCandidates;
            m_autoKeywords = m_searchKeywords;
        }
        if (!m_restorePending && m_manualCandidate && !m_manualId.isEmpty() &&
            std::none_of(m_pendingCandidates.cbegin(), m_pendingCandidates.cend(),
                         [this](const Candidate &c) { return c.id == m_manualId; }))
            m_pendingCandidates.prepend(*m_manualCandidate);
    }
    const bool hasOnline = !m_pendingCandidates.isEmpty();
    includeEmbedded(m_pendingCandidates);
    const auto wanted =
        m_restorePending ? (hasOnline ? m_recommendedId : QString("embedded"))
                         : (m_currentId.isEmpty()
                                ? (m_manualId.isEmpty() && hasOnline ? m_recommendedId : m_manualId)
                                : m_currentId);
    beginResetModel();
    m_candidates = std::move(m_pendingCandidates);
    m_selected = m_candidates.isEmpty() ? -1 : 0;
    for (int i = 0; i < m_candidates.size(); ++i)
        if (m_candidates[i].id == wanted) {
            m_selected = i;
            break;
        }
    const bool manualUnavailable =
        m_activatePending && !m_restorePending && !m_manualId.isEmpty() &&
        std::none_of(m_candidates.cbegin(), m_candidates.cend(),
                     [this](const Candidate &c) { return c.id == m_manualId; });
    if (manualUnavailable)
        m_selected = -1;
    endResetModel();
    m_catalogReady = m_allCandidates && automaticSearch();
    // A missing remembered source is not permission to activate another version.
    // Keep its preference; only an explicit restore/apply can replace it.
    if (manualUnavailable) {
        m_state = "error";
        m_error = m_manualId == "embedded" && !m_embeddedError.isEmpty()
                      ? m_embeddedError
                      : tr("已选歌词版本暂不可用，请重新选择或使用智能推荐");
        m_activatePending = m_openAfterAuto = false;
        emit changed();
        return;
    }
    if (m_candidates.isEmpty()) {
        m_state = m_currentId.isEmpty() ? "empty" : m_state;
        if (m_currentId.isEmpty() && !m_embeddedError.isEmpty() &&
            (m_manualId.isEmpty() || m_manualId == "embedded")) {
            m_error = m_embeddedError;
            m_state = "error";
        }
        m_activatePending = false;
        emit changed();
        if (!m_queuedKeywords.isEmpty() && m_selectionOpen) {
            const auto keywords = std::exchange(m_queuedKeywords, QString());
            m_openAfterAuto = false;
            startSearch(true, false, keywords);
        } else if (m_openAfterAuto && m_selectionOpen) {
            // A default lookup can be empty while the full catalog still has
            // versions to preview. Opening the dialog must finish its own path,
            // without silently applying one after automatic matching was empty.
            m_openAfterAuto = false;
            startSearch(true, false);
        }
        return;
    }
    previewSelected();
}
void LyricSourceController::select(int row)
{
    if (row < 0 || row >= rowCount() || m_applyPending ||
        (loading() && cachedLyrics(m_candidates[row].id).isEmpty()))
        return;
    m_selected = row;
    m_activatePending = false;
    previewSelected();
}
void LyricSourceController::previewSelected()
{
    ++m_parseRevision;
    delete m_parseJob.data();
    m_parseJob = nullptr;
    if (m_download)
        m_api.cancel(m_download);
    m_download = 0;
    m_preview.clear();
    m_error.clear();
    const auto *c = selected();
    if (!c) {
        emit changed();
        return;
    }
    const auto cached = cachedLyrics(c->id);
    if (!cached.isEmpty()) {
        m_preview.replace(cached);
        if (m_activatePending)
            activatePreview();
    } else {
        m_downloadId = c->id;
        m_download = m_api.lyric(c->id, c->key);
        if (!m_download) {
            m_error = tr("歌词预览加载失败");
            m_activatePending = m_applyPending = m_restorePending = false;
        }
    }
    emit changed();
}
void LyricSourceController::activatePreview()
{
    const auto *c = selected();
    if (!c || cachedLyrics(c->id).isEmpty())
        return;
    const bool explicitApply = m_applyPending;
    const bool restored = m_restorePending;
    if (explicitApply) {
        if (m_restorePending) {
            m_settings.remove(preferenceKey());
            m_manualId.clear();
            m_savedKeywords.clear();
            m_manualCandidate.reset();
        } else {
            const auto keywords =
                c->id == "embedded" ? QString()
                                    : (c->id == m_manualId && automaticSearch() ? m_savedKeywords
                                                                                : m_searchKeywords);
            m_manualId = c->id;
            m_settings.setValue(preferenceKey(), m_manualId);
            m_savedKeywords = keywords;
            if (m_savedKeywords.isEmpty())
                m_settings.remove(preferenceKey() + "/keywords");
            else
                m_settings.setValue(preferenceKey() + "/keywords", m_savedKeywords);
        }
    }
    if (c->id == m_manualId)
        m_manualCandidate = *c;
    m_currentId = c->id;
    m_state = m_preview.rowCount() ? "ready" : "empty";
    m_activatePending = m_applyPending = m_restorePending = false;
    m_baseLyrics = cachedLyrics(c->id);
    publishEffective();
    notifyRows();
    if (explicitApply) {
        emit notice(restored ? tr("已改为智能推荐") : tr("歌词版本已应用"), true);
        emit applied();
    }
    if (!m_queuedKeywords.isEmpty() && m_selectionOpen) {
        const auto keywords = std::exchange(m_queuedKeywords, QString());
        m_openAfterAuto = false;
        startSearch(true, false, keywords);
    } else if (m_openAfterAuto && m_selectionOpen) {
        m_openAfterAuto = false;
        openSelection();
    }
}
void LyricSourceController::applySelected()
{
    if (!selected() || m_applyPending ||
        ((m_search || m_defaultSearch) && selected()->id != "embedded" &&
         !cachedLyrics(selected()->id).contains("lines")))
        return;
    ++m_choiceRevision;
    cancelExtension();
    if (!m_writes.isEmpty()) {
        m_writes.clear();
        publishEffective();
    }
    if (!cachedLyrics(selected()->id).isEmpty()) {
        if (m_search)
            m_api.cancel(m_search);
        if (m_defaultSearch)
            m_api.cancel(m_defaultSearch);
        m_search = m_defaultSearch = 0;
        m_pendingCandidates.clear();
    }
    m_applyPending = m_activatePending = true;
    if (!cachedLyrics(selected()->id).isEmpty())
        activatePreview();
    else if (!m_download)
        previewSelected();
    emit changed();
}
void LyricSourceController::restoreAuto()
{
    if (!hasTrack() || m_applyPending)
        return;
    ++m_choiceRevision;
    m_restorePending = m_applyPending = true;
    if (!m_writes.isEmpty()) {
        m_writes.clear();
        publishEffective();
    }
    beginAutomatic(true, true);
}
void LyricSourceController::succeeded(quint64 id, const QByteArray &bytes)
{
    if (!id || m_revision != m_api.sessionRevision())
        return;
    if (id == m_defaultSearch) {
        const auto document = QJsonDocument::fromJson(bytes);
        if (!document.isArray()) {
            failed(id, 0, R"({"message":"歌词默认版本数据无效"})");
            return;
        }
        m_defaultSearch = 0;
        QString recommended;
        for (const auto &value : document.array()) {
            const auto c = value.toObject();
            if (!c.value("source_id").toString().isEmpty() &&
                !c.value("access_key").toString().isEmpty()) {
                recommended = c.value("source_id").toString();
                break;
            }
        }
        commitCandidates(recommended);
        return;
    }
    if (id == m_search) {
        const auto document = QJsonDocument::fromJson(bytes);
        if (!document.isArray()) {
            failed(id, 0, R"({"message":"歌词候选数据无效"})");
            return;
        }
        QList<Candidate> candidates;
        for (const auto &value : document.array()) {
            const auto c = value.toObject();
            if (c.value("source_id").toString().isEmpty() ||
                c.value("access_key").toString().isEmpty())
                continue;
            candidates.append({c.value("source_id").toString(), c.value("access_key").toString(),
                               c.value("title").toString(), c.value("artist").toString(),
                               c.value("source_label").toString(), c.value("language").toString(),
                               c.value("duration_ms").toInteger(), c.value("score").toInt(),
                               c.value("word_timed").toBool(), c.value("scrollable").toBool(),
                               c.value("nickname").toString()});
        }
        m_search = 0;
        m_pendingCandidates = std::move(candidates);
        // Only a successful, empty hash result can change the automatic query.
        // Preserve man=yes/no, duration and original track identity across it.
        if (m_pendingCandidates.isEmpty() && automaticSearch()) {
            const auto keywords = nextAutomaticKeywords();
            if (!keywords.isEmpty()) {
                m_searchKeywords = keywords;
                m_keywordFallback = true;
                m_search =
                    m_api.lyricCandidatesByKeywords(keywords, m_track.durationMs, m_allCandidates);
                if (!m_search) {
                    m_error = tr("歌词候选加载失败");
                    m_activatePending = m_applyPending = m_restorePending = false;
                }
                emit changed();
                return;
            }
        }
        if (m_allCandidates && automaticSearch() && !m_pendingCandidates.isEmpty()) {
            m_defaultSearch =
                m_searchKeywords.isEmpty()
                    ? m_api.lyricCandidates(m_track.hash, m_track.songId, m_track.durationMs, false)
                    : m_api.lyricCandidatesByKeywords(m_searchKeywords, m_track.durationMs, false);
            if (!m_defaultSearch) {
                m_error = tr("默认歌词加载失败");
                m_activatePending = false;
            }
            emit changed();
        } else {
            commitCandidates({});
        }
        return;
    }
    if (id != m_download)
        return;
    m_download = 0;
    if (!selected() || selected()->id != m_downloadId)
        return;
    const auto revision = ++m_parseRevision;
    const auto generation = m_generation, session = m_api.sessionRevision();
    m_parseJob = m_extensions->parseDownloaded(
        QJsonDocument::fromJson(bytes).object().toVariantMap(), this,
        [this, revision, generation, session](QJsonObject object, QString error) {
            if (revision != m_parseRevision || generation != m_generation ||
                session != m_api.sessionRevision())
                return;
            m_parseJob = nullptr;
            if (!selected() || selected()->id != m_downloadId)
                return;
            if (!error.isEmpty()) {
                m_error = error;
                if (m_currentId.isEmpty())
                    m_state = "error";
                m_activatePending = m_applyPending = m_restorePending = false;
                emit changed();
                return;
            }
            acceptParsed(object);
        });
    emit changed();
}
void LyricSourceController::acceptParsed(const QJsonObject &object)
{
    if (!m_preview.replace(object)) {
        m_error = tr("歌词预览数据无效");
        m_activatePending = m_applyPending = m_restorePending = false;
        emit changed();
        return;
    }
    m_candidates[m_selected].hasTranslation = m_preview.hasTranslation();
    m_candidates[m_selected].hasRomanization = m_preview.hasRomanization();
    m_cache.insert(m_downloadId, object);
    m_cacheOrder.removeAll(m_downloadId);
    m_cacheOrder.append(m_downloadId);
    while (m_cacheOrder.size() > 4)
        m_cache.remove(m_cacheOrder.takeFirst());
    if (m_activatePending)
        activatePreview();
    notifyRows();
}
void LyricSourceController::failed(quint64 id, int, const QByteArray &bytes)
{
    if (!id || m_revision != m_api.sessionRevision() ||
        (id != m_search && id != m_download && id != m_defaultSearch))
        return;
    const bool keywordSearchFailed = id == m_search && !m_searchKeywords.isEmpty();
    if (id == m_search)
        m_search = 0;
    else if (id == m_defaultSearch)
        m_defaultSearch = 0;
    else
        m_download = 0;
    if (m_activatePending && !m_applyPending && m_manualId.isEmpty() &&
        !m_embeddedLyrics.value("lines").toArray().isEmpty() && m_currentId.isEmpty()) {
        m_pendingCandidates.clear();
        commitCandidates({});
        return;
    }
    const bool userAction = m_selectionOpen || m_applyPending;
    m_error =
        keywordSearchFailed
            ? tr("搜索失败，请重试")
            : QJsonDocument::fromJson(bytes).object().value("message").toString(tr("歌词加载失败"));
    if (m_currentId.isEmpty() && !m_embeddedError.isEmpty() &&
        (m_manualId.isEmpty() || m_manualId == "embedded"))
        m_error = m_embeddedError;
    if (m_currentId.isEmpty())
        m_state = "error";
    m_activatePending = m_applyPending = m_restorePending = false;
    if (userAction)
        emit notice(m_error, false);
    emit changed();
    if (!m_queuedKeywords.isEmpty() && m_selectionOpen) {
        const auto keywords = std::exchange(m_queuedKeywords, QString());
        m_openAfterAuto = false;
        startSearch(true, false, keywords);
    } else if (m_openAfterAuto && m_selectionOpen) {
        m_openAfterAuto = false;
        refresh();
    }
}
} // namespace EchoMusic
