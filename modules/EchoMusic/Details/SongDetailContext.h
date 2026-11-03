#pragma once
#include "CommentsController.h"
#include "SongCatalogActions.h"
#include "SongCatalogModel.h"
#include <optional>
namespace EchoMusic
{
class SongDetailContext final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by DetailsService or a detail route")
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY changed)
    Q_PROPERTY(QVariantMap information READ information NOTIFY changed)
    Q_PROPERTY(QVariantList rankings READ rankings NOTIFY changed)
    Q_PROPERTY(QVariantList rankingDetails READ rankingDetails NOTIFY changed)
    Q_PROPERTY(QStringList qualities READ qualities NOTIFY changed)
    Q_PROPERTY(QStringList effects READ effects NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString songId READ songId NOTIFY changed)
    Q_PROPERTY(QVariantList artistLinks READ artistLinks NOTIFY changed)
    Q_PROPERTY(QString albumNavigationId READ albumNavigationId NOTIFY changed)
    Q_PROPERTY(EchoMusic::SongCatalogActions *actions READ actions CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *songs READ songs CONSTANT)
    Q_PROPERTY(EchoMusic::CommentsController *comments READ comments CONSTANT)
public:
    SongDetailContext(ApiClient &, PlaybackService &, RouteContext &, bool,
                      CommentAuthorService *authors = nullptr);
    SongDetailContext(ApiClient &, PlaybackService &, const PlaybackTrack &, bool, QObject *parent,
                      CommentAuthorService *authors = nullptr);
    ~SongDetailContext() override;
    QVariantMap detail() const
    {
        return m_detail.toVariantMap();
    }
    QVariantMap information() const;
    QVariantList rankings() const
    {
        return m_ranking.value("info").toArray().toVariantList();
    }
    QVariantList rankingDetails() const
    {
        return m_rankingDetails.value("list").toArray().toVariantList();
    }
    QStringList qualities() const
    {
        return m_qualities;
    }
    QStringList effects() const
    {
        return m_effects;
    }
    bool loading() const;
    QString artistNavigationId() const;
    QVariantList artistLinks() const;
    QString albumNavigationId() const;
    QString songId() const
    {
        return m_mix ? QString::number(m_mix) : QString();
    }
    QString errorMessage() const
    {
        return m_error;
    }
    SongCatalogActions *actions()
    {
        return &m_actions;
    }
    SongCatalogModel *songs()
    {
        return &m_songs;
    }
    CommentsController *comments()
    {
        return &m_comments;
    }
    Q_INVOKABLE void reload();
    Q_INVOKABLE void setPresented(bool);
    void shutdown();
signals:
    void changed();
    void notice(const QString &, bool);

private:
    friend class DetailsTestAccess;
    friend class DetailsService;
    SongDetailContext(ApiClient &, PlaybackService &, RouteContext *, std::optional<PlaybackTrack>,
                      bool, QObject *, CommentAuthorService *);
    QVariantMap query() const;
    QString resourceId() const;
    bool active() const;
    void adoptEntryTrack(const PlaybackTrack &);
    enum Kind
    {
        Privilege,
        Metadata,
        Ranking,
        RankingDetails,
        FavoriteCount,
        CommentCount
    };
    struct Pending
    {
        Kind kind;
        quint64 session, epoch;
    };
    void activate();
    void cancel();
    void hydrate();
    void requestMetadata();
    void rankingsAndComments();
    bool commentsPage() const;
    void requestHeaderStats();
    void startComments();
    void updateSong();
    void applyMetadata(const QJsonArray &values);
    void request(quint64, Kind);
    void accept(quint64, const QByteArray &);
    void fail(quint64, const QByteArray &);
    ApiClient &m_api;
    PlaybackService &m_playback;
    QPointer<RouteContext> m_route;
    std::optional<PlaybackTrack> m_entryTrack;
    QVariantMap m_entryQuery;
    SongCatalogModel m_songs;
    SongCatalogActions m_actions;
    CommentsController m_comments;
    QJsonObject m_detail, m_track, m_ranking, m_rankingDetails;
    QHash<quint64, Pending> m_pending;
    quint64 m_metadataTicket = 0;
    quint64 m_mix = 0, m_album = 0, m_epoch = 0;
    QString m_hash, m_error;
    QStringList m_qualities, m_effects;
    bool m_network, m_presented = true, m_closed = false, m_loaded = false,
                    m_rankingsStarted = false, m_commentsStarted = false,
                    m_headerStatsRequested = false;
};
} // namespace EchoMusic
