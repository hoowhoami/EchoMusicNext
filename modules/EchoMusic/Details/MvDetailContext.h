#pragma once
#include "ApiClient.h"
#include "RouteController.h"
#include "PlaybackService.h"
#include "VideoCollectionService.h"
#include "BarrageController.h"
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QPointer>
#include <QVideoSink>
namespace EchoMusic
{
class NetworkPreferences;
class MvDetailContext final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by an MV route")
    Q_PROPERTY(EchoMusic::BarrageController *barrage READ barrage CONSTANT)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY changed)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(int versionIndex READ versionIndex NOTIFY changed)
    Q_PROPERTY(int versionCount READ versionCount NOTIFY changed)
    Q_PROPERTY(QString sourceHash READ sourceHash NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(EchoMusic::VideoCollectionService *collection READ collection CONSTANT)
    Q_PROPERTY(QMediaPlayer *player READ player NOTIFY playerChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY playbackChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY playbackChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY playbackChanged)
public:
    MvDetailContext(ApiClient &, PlaybackService &, VideoCollectionService &, RouteContext &, bool);
    VideoCollectionService *collection()
    {
        return &m_collection;
    }
    ~MvDetailContext() override;
    BarrageController *barrage()
    {
        return &m_barrage;
    }
    QVariantMap detail() const
    {
        return m_detail.toVariantMap();
    }
    QVariantList sources() const
    {
        return m_sources.toVariantList();
    }
    int versionIndex() const
    {
        return m_version;
    }
    int versionCount() const
    {
        return m_versions.size();
    }
    QString sourceHash() const
    {
        return m_hash;
    }
    bool loading() const
    {
        return !m_pending.isEmpty();
    }
    QString errorMessage() const
    {
        return m_error;
    }
    QMediaPlayer *player() const
    {
        return m_player;
    }
    bool playing() const
    {
        return m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
    }
    qint64 position() const
    {
        return m_player ? m_player->position() : 0;
    }
    qint64 duration() const
    {
        return m_player ? m_player->duration() : 0;
    }
    double volume() const
    {
        return m_volume;
    }
    void setVolume(double);
    void setNetworkPreferences(NetworkPreferences *);
    Q_INVOKABLE void setPresented(bool);
    Q_INVOKABLE void setVideoSink(QObject *);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void selectVersion(int);
    Q_INVOKABLE void selectSource(const QString &);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void seek(qint64);
    void shutdown();
signals:
    void changed();
    void playerChanged();
    void playbackChanged();
    void notice(const QString &, bool);

private:
    friend class DetailsTestAccess;
    enum Kind
    {
        Versions,
        Detail,
        Privilege,
        Source
    };
    struct Pending
    {
        Kind kind;
        quint64 epoch, session, selection;
    };
    void activate();
    void request(quint64, Kind);
    void cancel();
    void releasePlayer();
    void openPlayer(const QString &url);
    void applyVersion();
    void requestSource(const QString &hash);
    void accept(quint64, const QByteArray &);
    void fail(quint64, const QByteArray &);
    ApiClient &m_api;
    PlaybackService &m_playback;
    VideoCollectionService &m_collection;
    RouteContext &m_route;
    BarrageController m_barrage;
    QJsonArray m_versions, m_sources;
    QJsonObject m_detail, m_fallback;
    QHash<quint64, Pending> m_pending;
    QPointer<NetworkPreferences> m_networkPreferences;
    QUrl m_mediaResource;
    QPointer<QMediaPlayer> m_player;
    QPointer<QAudioOutput> m_audio;
    QPointer<QVideoSink> m_sink;
    QString m_hash, m_error;
    quint64 m_epoch = 0, m_selection = 0;
    int m_version = 0;
    double m_volume = .5;
    bool m_network, m_closed = false, m_presented = false, m_sourceChosen = false;
};
} // namespace EchoMusic
