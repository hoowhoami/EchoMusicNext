#pragma once
#include "ApiClient.h"
#include "SongCatalogModel.h"
#include "SongCatalogActions.h"
#include "RouteController.h"
#include <array>
namespace EchoMusic
{
class ExploreServiceTestAccess;
/// Owns the five discovery catalogs and session-scoped request lifetimes.
/// QML receives domain projections, never upstream JSON or arbitrary routes.
class ExploreService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(int tab READ tab NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(EchoMusic::SongCatalogModel *cards READ cards CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *newSongs READ newSongs CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *rankSongs READ rankSongs CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogActions *newActions READ newActions CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogActions *rankActions READ rankActions CONSTANT)
    Q_PROPERTY(EchoMusic::PlaybackService *playback READ playback CONSTANT)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY changed)
    Q_PROPERTY(QVariantList ranks READ ranks NOTIFY changed)
    Q_PROPERTY(QVariantList sexFilters READ sexFilters NOTIFY changed)
    Q_PROPERTY(QVariantList typeFilters READ typeFilters NOTIFY changed)
    Q_PROPERTY(QStringList artistLetters READ artistLetters NOTIFY changed)
    Q_PROPERTY(QString artistLetter READ artistLetter NOTIFY changed)
    Q_PROPERTY(QString categoryLabel READ categoryLabel NOTIFY changed)
    Q_PROPERTY(QString category READ category NOTIFY changed)
    Q_PROPERTY(QString rankId READ rankId NOTIFY changed)
    Q_PROPERTY(QString rankLabel READ rankLabel NOTIFY changed)
    Q_PROPERTY(QString region READ region NOTIFY changed)
    Q_PROPERTY(QString sex READ sex NOTIFY changed)
    Q_PROPERTY(QString artistType READ artistType NOTIFY changed)
public:
    /// Explicit UI composition; construction alone does not register pages.
    void registerRoutes(RouteController &router);
    enum Part
    {
        Categories,
        Playlists,
        RankCatalog,
        RankFallback,
        RankSongs,
        Albums,
        NewSongs,
        Artists,
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
    ExploreService(ApiClient &, PlaybackService &, RouteController &, bool networkEnabled,
                   QObject *parent = nullptr);
    ~ExploreService() override;
    int tab() const
    {
        return m_tab;
    }
    bool loading() const;
    QString errorMessage() const;
    SongCatalogModel *cards()
    {
        return &m_cards;
    }
    SongCatalogModel *newSongs()
    {
        return &m_newSongs;
    }
    SongCatalogModel *rankSongs()
    {
        return &m_rankSongs;
    }
    SongCatalogActions *newActions()
    {
        return &m_newActions;
    }
    SongCatalogActions *rankActions()
    {
        return &m_rankActions;
    }
    PlaybackService *playback()
    {
        return &m_playback;
    }
    QVariantList categories() const
    {
        return m_categories.toVariantList();
    }
    QVariantList ranks() const
    {
        return m_ranks.toVariantList();
    }
    QVariantList sexFilters() const
    {
        return m_sexFilters.toVariantList();
    }
    QVariantList typeFilters() const
    {
        return m_typeFilters.toVariantList();
    }
    QStringList artistLetters() const;
    QString artistLetter() const
    {
        return m_letter;
    }
    QString categoryLabel() const
    {
        return m_categoryLabel;
    }
    QString category() const
    {
        return m_category;
    }
    QString rankId() const
    {
        return QString::number(m_rank);
    }
    QString rankLabel() const;
    QString region() const
    {
        return m_region;
    }
    QString sex() const
    {
        return m_sex;
    }
    QString artistType() const
    {
        return m_artistType;
    }
    Q_INVOKABLE void selectTab(int);
    Q_INVOKABLE void selectCategory(const QString &);
    Q_INVOKABLE void selectRank(const QString &);
    Q_INVOKABLE void selectRegion(const QString &);
    Q_INVOKABLE void selectSex(const QString &);
    Q_INVOKABLE void selectArtistType(const QString &);
    Q_INVOKABLE void selectLetter(const QString &);
    Q_INVOKABLE void retry();
    void shutdown();
signals:
    void changed();

private:
    friend class ExploreServiceTestAccess;
    struct Pending
    {
        Part part;
        quint64 session;
    };
    Part activePart() const;
    void activate(RouteContext *);
    void request(Part);
    void cancel(Part);
    void cancelAll();
    void reset();
    void accept(quint64, const QByteArray &);
    void fail(quint64, const QByteArray &);
    void projectCards();
    void loadCurrent();
    ApiClient &m_api;
    PlaybackService &m_playback;
    RouteController &m_router;
    SongCatalogModel m_cards, m_newSongs, m_rankSongs;
    SongCatalogActions m_newActions, m_rankActions;
    QHash<quint64, Pending> m_pending;
    std::array<State, PartCount> m_states{};
    std::array<QString, PartCount> m_errors{};
    QJsonArray m_categories, m_playlistCards, m_ranks, m_artistGroups, m_sexFilters, m_typeFilters;
    QJsonObject m_albumRegions;
    QString m_instance, m_category = "0", m_categoryLabel, m_region = "all", m_sex = "0",
                        m_artistType = "0:0", m_letter;
    quint64 m_rank = 0;
    int m_tab = 0;
    bool m_network = false, m_active = false, m_closed = false;
};
} // namespace EchoMusic
