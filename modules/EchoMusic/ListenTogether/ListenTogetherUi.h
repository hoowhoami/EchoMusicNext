#pragma once
#include "ListenTogetherController.h"
#include "PlaybackService.h"
#include "SongCatalogModel.h"
#include "UserPlaylistModel.h"
#include "SongMetadataService.h"
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class PlaybackService;
class RouteController;
/// Lives for one picker popup, including its requests and selected song facts.
class ListenSongPicker final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created only by the actual room song-picker trigger")
    Q_PROPERTY(EchoMusic::SongCatalogModel *songs READ songs CONSTANT)
    Q_PROPERTY(EchoMusic::ListenTogetherListModel *playlists READ playlists CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(int source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(QString playlistId READ playlistId NOTIFY changed)
public:
    ListenSongPicker(ApiClient &, PlaybackService &, UserPlaylistModel &, SongMetadataService &,
                     ListenTogetherController &, QObject *);
    ~ListenSongPicker() override;
    SongCatalogModel *songs()
    {
        return &m_songs;
    }
    ListenTogetherListModel *playlists()
    {
        return &m_playlists;
    }
    bool loading() const
    {
        return m_loading || m_catalogLoading;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    int source() const
    {
        return m_source;
    }
    QString playlistId() const
    {
        return m_playlistId;
    }
    void setSource(int);
    Q_INVOKABLE void selectPlaylist(const QString &);
    Q_INVOKABLE void search(const QString &);
    Q_INVOKABLE void invalidateSearch();
    Q_INVOKABLE void submit();
    Q_INVOKABLE void chooseSong(const QString &);
signals:
    void changed();
    void submitted();

private:
    void cancel();
    void projectPlaylists();
    void ensureLibrary();
    void completeSongs();
    void page();
    void accept(quint64, const QByteArray &);
    ApiClient &m_api;
    PlaybackService &m_playback;
    UserPlaylistModel &m_library;
    SongMetadataService &m_metadata;
    QSet<quint64> m_metadataRequests;
    ListenTogetherController &m_room;
    SongCatalogModel m_songs;
    ListenTogetherListModel m_playlists;
    QJsonArray m_entries;
    QString m_error, m_playlistId;
    quint64 m_request = 0, m_session = 0;
    quint32 m_page = 1, m_pageSize = 300;
    int m_source = -1, m_libraryRow = -1;
    bool m_loading = false, m_catalogLoading = false, m_submitting = false;
};
class ListenTogetherUi final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Application-owned together route")
    Q_PROPERTY(EchoMusic::ListenTogetherController *room READ room CONSTANT)
    Q_PROPERTY(EchoMusic::PlaybackService *playback READ playback CONSTANT)
public:
    /// Explicit UI composition; construction alone does not register pages.
    void registerRoutes(RouteController &router);

    ListenTogetherUi(ApiClient &, PlaybackService &, UserPlaylistModel &, SongMetadataService &,
                     RouteController &, bool networkEnabled, QObject *parent = nullptr);
    ListenTogetherController *room();
    PlaybackService *playback()
    {
        return &m_playback;
    }
    Q_INVOKABLE int creationSongCount() const;
    Q_INVOKABLE EchoMusic::ListenSongPicker *createPicker(QObject *owner);

private:
    ApiClient &m_api;
    PlaybackService &m_playback;
    UserPlaylistModel &m_library;
    SongMetadataService &m_metadata;
};
} // namespace EchoMusic
