#pragma once
#include "ApiClient.h"
#include "LyricModel.h"
#include "PlaybackTrack.h"
#include "SettingsStore.h"
#include <QSet>
#include <optional>
#include <memory>
#include <QSortFilterProxyModel>
#include <QPointer>

namespace EchoMusic
{
class LyricExtensions;
/// Candidate keys remain in this GUI-owned service, never in QML or SQLite.
/// Preview and active lyrics share the typed API but have separate request
/// lifecycles, so browsing a source cannot replace the playing lyric.
class LyricSourceController final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(EchoMusic::LyricModel *preview READ preview CONSTANT)
    Q_PROPERTY(QAbstractItemModel *previewRows READ previewRows CONSTANT)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY changed)
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    Q_PROPERTY(QString currentId READ currentId NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool previewLoading READ previewLoading NOTIFY changed)
    Q_PROPERTY(bool applying READ applying NOTIFY changed)
    Q_PROPERTY(bool manual READ manual NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString selectedTitle READ selectedTitle NOTIFY changed)
    Q_PROPERTY(QString selectedNickname READ selectedNickname NOTIFY changed)
    Q_PROPERTY(QString selectedArtist READ selectedArtist NOTIFY changed)
    Q_PROPERTY(QString selectedSource READ selectedSource NOTIFY changed)
    Q_PROPERTY(qint64 selectedDuration READ selectedDuration NOTIFY changed)
    Q_PROPERTY(QString searchTitle READ searchTitle WRITE setSearchTitle NOTIFY changed)
    Q_PROPERTY(QString searchArtist READ searchArtist WRITE setSearchArtist NOTIFY changed)
    Q_PROPERTY(bool canSearch READ canSearch NOTIFY changed)
public:
    LyricSourceController(ApiClient &, EchoMusic::SettingsStore &, QObject *parent = nullptr);
    ~LyricSourceController() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    LyricModel *preview()
    {
        return &m_preview;
    }
    QAbstractItemModel *previewRows() const
    {
        return m_previewRows.get();
    }
    int selectedIndex() const
    {
        return m_selected;
    }
    QString currentId() const
    {
        return m_manualId.isEmpty() && !m_writes.isEmpty() ? m_writes.last().key : m_currentId;
    }
    QString state() const
    {
        return m_manualId.isEmpty() && !m_writes.isEmpty()
                   ? (m_writes.last().lyrics.value("lines").toArray().isEmpty() ? "empty" : "ready")
                   : m_state;
    }
    bool loading() const
    {
        return m_search != 0 || m_defaultSearch != 0 || !m_extensionJob.isNull();
    }
    bool previewLoading() const
    {
        return m_download != 0 || !m_parseJob.isNull();
    }
    bool applying() const
    {
        return m_applyPending;
    }
    bool manual() const
    {
        return !m_manualId.isEmpty();
    }
    QString errorMessage() const
    {
        return m_error;
    }
    QString selectedTitle() const;
    QString selectedArtist() const;
    QString selectedNickname() const;
    QString selectedSource() const;
    qint64 selectedDuration() const;
    QString searchTitle() const
    {
        return m_searchTitle;
    }
    QString searchArtist() const
    {
        return m_searchArtist;
    }
    bool canSearch() const
    {
        return !m_searchTitle.trimmed().isEmpty() || !m_searchArtist.trimmed().isEmpty();
    }
    void setSearchTitle(const QString &);
    void setSearchArtist(const QString &);
    LyricExtensions *extensions() const
    {
        return m_extensions.get();
    }
    quint64 generation() const
    {
        return m_generation;
    }
    quint64 choiceRevision() const
    {
        return m_choiceRevision;
    }
    void suspend();
    QString activeSource() const;
    void reload(bool preserveCurrent = true);
    bool installWrite(const QString &key, const QString &revision, const QString &source,
                      const QJsonObject &lyrics);
    void removeWrite(const QString &key, const QString &revision);
    void load(const PlaybackTrack &);
    void clear();
    void updateEmbeddedDocument(const QString &trackKey, const QString &title,
                                const QString &artist, const QString &content,
                                const QString &parseError = {});
    void updateEmbedded(const QString &trackKey, const QString &title, const QString &artist,
                        const QJsonObject &lyrics, const QString &parseError = {});
    QString embeddedError() const
    {
        return m_embeddedError;
    }
    void setDuration(qint64 durationMs)
    {
        if (durationMs > 0)
            m_track.durationMs = quint64(durationMs);
    }
    Q_INVOKABLE void openSelection();
    Q_INVOKABLE void closeSelection();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void search();
    Q_INVOKABLE void select(int row);
    Q_INVOKABLE void applySelected();
    Q_INVOKABLE void restoreAuto();
signals:
    void changed();
    void activated(const QJsonObject &lyrics, const QString &trackKey);
    void applied();
    void notice(const QString &message, bool success);

private:
    friend class LyricSourceControllerTestAccess;
    struct Candidate
    {
        QString id, key, title, artist, source, language;
        qint64 duration = 0;
        int score = 0;
        bool wordTimed = false, scrollable = true;
        QString nickname;
        bool hasTranslation = false, hasRomanization = false;
    };
    const Candidate *selected() const;
    QString preferenceKey() const;
    void succeeded(quint64, const QByteArray &);
    void failed(quint64, int, const QByteArray &);
    void previewSelected();
    void cancelParsing();
    void acceptParsed(const QJsonObject &);
    QPointer<QObject> m_parseJob, m_embeddedParse;
    quint64 m_parseRevision = 0;
    void activatePreview();
    void notifyRows();
    void startSearch(bool allCandidates, bool activate, const QString &keywords = {});
    void resetSearchFields();
    QString trackKeywords() const;
    QString nextAutomaticKeywords();
    void includeEmbedded(QList<Candidate> &) const;
    void includePlugin(QList<Candidate> &) const;
    void beginAutomatic(bool force = false, bool preserveCurrent = false);
    void cancelExtension();
    bool hasTrack() const
    {
        return !m_track.id.isEmpty() || !m_track.hash.isEmpty() || !m_track.localPath.isEmpty();
    }
    void extensionsChanged();
    void publishEffective();
    struct Write
    {
        QString key, revision, source;
        QJsonObject lyrics;
    };
    QList<Write> m_writes;
    std::unique_ptr<LyricExtensions> m_extensions;
    QPointer<QObject> m_extensionJob;
    quint64 m_generation = 0, m_choiceRevision = 0;
    std::optional<Candidate> m_pluginCandidate;
    QString m_pluginRevision;
    QJsonObject m_pluginLyrics, m_baseLyrics;

    QJsonObject cachedLyrics(const QString &id) const;
    QString m_embeddedTitle, m_embeddedArtist, m_embeddedError;
    QJsonObject m_embeddedLyrics;
    QSet<QString> m_triedKeywords;
    bool automaticSearch() const
    {
        return m_searchKeywords.isEmpty() || m_keywordFallback;
    }
    void commitCandidates(const QString &recommendedId);
    ApiClient &m_api;
    EchoMusic::SettingsStore &m_settings;
    LyricModel m_preview;
    std::unique_ptr<QSortFilterProxyModel> m_previewRows;
    QList<Candidate> m_candidates, m_pendingCandidates, m_autoCandidates;
    // Parsed contents can survive a session change; candidate access keys cannot.
    QHash<QString, QJsonObject> m_cache;
    QString m_cacheTrack;
    QStringList m_cacheOrder;
    PlaybackTrack m_track;
    quint64 m_revision = 0, m_search = 0, m_download = 0, m_defaultSearch = 0;
    QString m_currentId, m_manualId, m_downloadId, m_state = "empty", m_error;
    QString m_searchTitle, m_searchArtist, m_searchKeywords, m_savedKeywords, m_queuedKeywords,
        m_recommendedId, m_autoKeywords;
    // Remember the current choice only in memory, never its ephemeral access key
    // in preferences. Persisted keyword + source ID re-resolve fresh keys on load.
    std::optional<Candidate> m_manualCandidate;
    int m_selected = -1;
    bool m_allCandidates = false, m_catalogReady = false, m_selectionOpen = false,
         m_openAfterAuto = false;
    bool m_activatePending = false, m_applyPending = false, m_restorePending = false;
    bool m_keywordFallback = false;
};
} // namespace EchoMusic
