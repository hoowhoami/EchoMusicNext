#pragma once
#include "SongCatalogModel.h"
#include "RecommendationCards.h"
#include "RecommendationFeed.h"
#include "SongCatalogActions.h"
#include "ApiClient.h"
#include "PlaybackService.h"
#include <array>
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class PlaybackService;
class AccountService;
class DiscoveryServiceTestAccess;
/// GUI-thread owner of discovery catalogs and request lifetimes. There is one
/// writable copy per catalog; hidden route objects only retain presentation.
class DiscoveryService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(
        EchoMusic::RecommendationCards *recommendationCards READ recommendationCards CONSTANT)
    Q_PROPERTY(EchoMusic::RecommendationCard *detailCard READ detailCard NOTIFY changed)
    Q_PROPERTY(EchoMusic::RecommendationCard *newSongsCard READ newSongsCard CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *styleSongs READ styleSongs CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *playlists READ playlists CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *editors READ editors CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *dailySongs READ dailySongs CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *rankingSongs READ rankingSongs CONSTANT)
    /// The service owns the free-listening catalog; pages only present it.
    Q_PROPERTY(EchoMusic::SongCatalogModel *freeListen READ freeListen CONSTANT)
    /// Owns the recommendation list and follows the shared discovery playback queue.
    /// discoverFlowActive reports whether that queue is the current playback source.
    Q_PROPERTY(EchoMusic::SongCatalogModel *discoverFlow READ discoverFlow CONSTANT)
    Q_PROPERTY(bool discoverFlowActive READ discoverFlowActive NOTIFY changed)
    Q_PROPERTY(QVariantList states READ states NOTIFY changed)
    Q_PROPERTY(QStringList errors READ errors NOTIFY changed)
    Q_PROPERTY(quint64 favoriteRevision READ favoriteRevision NOTIFY favoritesChanged)
    Q_PROPERTY(QString greeting READ greeting NOTIFY changed)
    Q_PROPERTY(QString todayLabel READ todayLabel NOTIFY changed)
    Q_PROPERTY(QString category READ category NOTIFY changed)
    Q_PROPERTY(QVariantList styleGroups READ styleGroups NOTIFY changed)
    Q_PROPERTY(int activeStyleGroup READ activeStyleGroup NOTIFY changed)
    Q_PROPERTY(QStringList selectedTags READ selectedTags NOTIFY changed)
    Q_PROPERTY(QString styleSummary READ styleSummary NOTIFY changed)
    Q_PROPERTY(QVariantList ranks READ ranks NOTIFY changed)
    Q_PROPERTY(QVariantList recommendedRanks READ recommendedRanks NOTIFY changed)
    Q_PROPERTY(QVariantMap selectedRank READ selectedRank NOTIFY changed)
    Q_PROPERTY(QString dailyDate READ dailyDate NOTIFY changed)
    Q_PROPERTY(QString dailySubtitle READ dailySubtitle NOTIFY changed)
    Q_PROPERTY(quint64 rankTotal READ rankTotal NOTIFY changed)
    Q_PROPERTY(QString activePage READ activePage NOTIFY changed)
    Q_PROPERTY(QString preparingFeed READ preparingFeed NOTIFY changed)
    Q_PROPERTY(EchoMusic::PlaybackService *playback READ playback CONSTANT)
public:
    enum Part
    {
        Style,
        Playlists,
        Editors,
        Daily,
        RankTop,
        RankCatalog,
        RankSongs,
        FreeListen,
        DiscoverFlow,
        PartCount
    };
    Q_ENUM(Part)
    enum State
    {
        Idle,
        Loading,
        Ready,
        Error
    };
    Q_ENUM(State)
    DiscoveryService(ApiClient &, PlaybackService &, AccountService &, QObject *parent = nullptr);
    ~DiscoveryService() override;
    SongCatalogModel *styleSongs()
    {
        return &m_styleSongs;
    }
    SongCatalogModel *playlists()
    {
        return &m_playlists;
    }
    SongCatalogModel *editors()
    {
        return &m_editors;
    }
    SongCatalogModel *dailySongs()
    {
        return &m_dailySongs;
    }
    SongCatalogModel *rankingSongs()
    {
        return &m_rankSongs;
    }
    SongCatalogModel *freeListen()
    {
        return &m_freeListen;
    }
    SongCatalogModel *discoverFlow()
    {
        return &m_discoverFlow;
    }
    /// True while the shared discovery queue is the current playback source;
    /// DiscoverFlow pages use it to follow player-originated track changes.
    bool discoverFlowActive() const;
    PlaybackService *playback()
    {
        return &m_playback;
    }
    QVariantList states() const;
    QStringList errors() const;
    quint64 favoriteRevision() const
    {
        return m_favoriteRevision;
    }
    QString preparingFeed() const;
    Q_INVOKABLE QString feedQueueKey(quint32 card, const QString &tag = {}) const;
    Q_INVOKABLE EchoMusic::RecommendationFeed *recommendationFeed(QObject *context);
    Q_INVOKABLE void playFeed(quint32 card, const QString &tag = {}, const QString &title = {},
                              const QString &description = {}, int first = 0, int count = 6,
                              int songOffset = -1);
    QString greeting() const;
    QString todayLabel() const;
    QString category() const
    {
        return m_category;
    }
    QVariantList styleGroups() const
    {
        return m_styleGroups;
    }
    int activeStyleGroup() const
    {
        return m_activeGroup;
    }
    QStringList selectedTags() const
    {
        return m_selectedTags;
    }
    QString styleSummary() const;
    QVariantList ranks() const
    {
        return m_ranks;
    }
    QVariantList recommendedRanks() const;
    QVariantMap selectedRank() const;
    QString dailyDate() const
    {
        return m_dailyDate;
    }
    QString dailySubtitle() const
    {
        return m_dailySubtitle;
    }
    quint64 rankTotal() const
    {
        return m_rankTotal;
    }
    QString activePage() const
    {
        return m_activePage;
    }
    RecommendationCards *recommendationCards()
    {
        return &m_recommendationCards;
    }
    RecommendationCard *detailCard() const
    {
        return m_detailCard;
    }
    RecommendationCard *newSongsCard() const
    {
        return m_recommendationCards.find("new");
    }
    void setPreferences(SettingsStore *preferences)
    {
        m_recommendationCards.setPreferences(preferences);
    }
    void setDetailCard(const QString &key);
    Q_INVOKABLE EchoMusic::RecommendationCard *recommendationCard(const QString &key) const
    {
        return m_recommendationCards.find(key);
    }
    Q_INVOKABLE void setFmPreviewActive(QObject *consumer, bool active);
    Q_INVOKABLE void activateCard(const QString &key, bool active);
    Q_INVOKABLE void refreshCard(const QString &key);
    void setPage(const QString &name, const QString &instance, bool networkEnabled);
    void shutdown();
    Q_INVOKABLE EchoMusic::SongCatalogActions *actions(EchoMusic::SongCatalogModel *model);
    Q_INVOKABLE int state(Part part) const;
    Q_INVOKABLE QString error(Part part) const;
    Q_INVOKABLE void retry(Part part);
    /// Free listening loads once and supports refresh. DiscoverFlow can append
    /// a bounded recommendation batch for preloading.
    Q_INVOKABLE void loadFreeListen();
    Q_INVOKABLE void loadDiscoverFlow(bool append = false);
    /// Page-level feedback (DiscoverFlow's "no new songs" / "unavailable").
    /// Routed pages cannot reach ToastService, so they raise it here and the
    /// application forwards the notice to the shared toast surface.
    Q_INVOKABLE void notify(const QString &message, int tone = 0);
    Q_INVOKABLE void selectCategory(const QString &category);
    Q_INVOKABLE void selectStyleGroup(int group);
    Q_INVOKABLE void toggleTag(const QString &id);
    Q_INVOKABLE void selectRank(quint64 id);
    Q_INVOKABLE void play(EchoMusic::SongCatalogModel *model, const QString &id = {},
                          bool replaceContext = true, bool selectedOnly = false);
    Q_INVOKABLE void appendSelected(EchoMusic::SongCatalogModel *model);
    Q_INVOKABLE void addSelection(EchoMusic::SongCatalogModel *model);
    Q_INVOKABLE bool isFavorite(EchoMusic::SongCatalogModel *model, const QString &id) const;
    Q_INVOKABLE void toggleFavorite(EchoMusic::SongCatalogModel *model, const QString &id);
    Q_INVOKABLE void addSong(EchoMusic::SongCatalogModel *model, const QString &id);
    Q_INVOKABLE void appendSong(EchoMusic::SongCatalogModel *model, const QString &id,
                                bool next = false);
    Q_INVOKABLE void copySong(EchoMusic::SongCatalogModel *model, const QString &id);
    Q_INVOKABLE void playDiscover(const QString &id);
signals:
    void changed();
    void favoritesChanged();
    /// tone follows EchoMusic::ToastService::Tone (0 Info, 1 Success, 2 Warning, 3 Danger).
    void notice(const QString &message, int tone);

private:
    void initializeCards();
    void syncFmPreview();
    void requestCard(RecommendationCard *);
    void cancelCardRequests();
    bool acceptCard(quint64 id, const QByteArray &);
    bool failCard(quint64 id, const QString &);
    RecommendationCards m_recommendationCards;
    RecommendationCard *m_detailCard = nullptr;
    QHash<quint64, QPair<RecommendationCard *, quint64>> m_cardPending;
    QSet<QString> m_cardDemand;
    QHash<QObject *, QMetaObject::Connection> m_fmPreviews;
    friend class DiscoveryServiceTestAccess;
    struct Pending
    {
        Part part;
        quint64 session;
        /// DiscoverFlow keeps append vs. replace distinct across the async
        /// boundary: an append that lands after a refresh must not resurrect it.
        bool append = false;
    };
    void request(Part part, bool append = false);
    void mergeRanks();
    /// Deduplicate DiscoverFlow batches against served keys.
    /// Reset the key set before replacing the catalog.
    QJsonArray discoverRows(const QJsonObject &payload);
    void cancel(Part part);
    void cancelAll();
    void reset();
    void succeeded(quint64 id, const QByteArray &bytes);
    void failed(quint64 id, const QString &error);
    void syncDiscoverQueue();
    QString queueKey(const SongCatalogModel *model) const;
    QString queueTitle(const SongCatalogModel *model) const;
    RecommendationFeed *feed(quint32 card, const QString &tag, const QString &title,
                             const QString &description);
    QHash<QString, RecommendationFeed *> m_feeds;
    QHash<QString, quint64> m_feedUsage;
    quint64 m_feedClock = 0;
    ApiClient &m_api;
    PlaybackService &m_playback;
    AccountService &m_account;
    SongCatalogModel m_styleSongs, m_playlists, m_editors, m_dailySongs, m_rankSongs;
    SongCatalogModel m_freeListen, m_discoverFlow;
    SongCatalogActions m_styleActions, m_dailyActions, m_rankActions;
    SongCatalogActions m_freeActions, m_discoverActions;
    QHash<quint64, Pending> m_pending;
    QHash<QString, QString> m_instances;
    std::array<State, PartCount> m_states{};
    std::array<QString, PartCount> m_errors{};
    QVariantList m_styleGroups, m_ranks;
    QJsonArray m_rankTop, m_rankCatalog;
    QStringList m_selectedTags;
    /// Source keys already served by DiscoverFlow, so a later batch cannot add
    /// a row the user has already scrolled past (SongCatalogModel renames
    /// duplicate ids instead of dropping them).
    QSet<QString> m_discoverKeys;
    QString m_category = "0", m_activePage, m_dailyDate, m_dailySubtitle;
    quint64 m_favoriteRevision = 0;
    quint64 m_selectedRank = 0, m_rankTotal = 0;
    int m_activeGroup = 0;
    bool m_networkEnabled = false, m_closed = false;
    bool m_discoverFollowQueue = true;
    bool m_discoverAppendWait = false;
    QPointer<PlaybackQueueModel> m_discoverMirror;
    quint64 m_discoverMirrorRevision = 0;
};
} // namespace EchoMusic
