#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QNetworkReply>
#include <QThreadPool>
#include <QFutureWatcher>
#include <QColor>
namespace EchoMusic
{
struct ArtworkResult
{
    QByteArray png;
    QColor dominant;
};
/// Ephemeral artwork for Now Playing and lyric background. Requests are bounded
/// and cancelled on track change. Image decode/quantization never runs on GUI.
class PlaybackArtwork final : public QObject
{
    Q_OBJECT
public:
    explicit PlaybackArtwork(QObject *parent = nullptr);
    ~PlaybackArtwork() override;
    void load(const QString &url, quint64 sequence);
    void cancel();
signals:
    // Terminal result of the current request, including missing/failed artwork.
    // Empty PNG/color denote failure; cancelled or superseded requests never emit.
    void ready(quint64 sequence, const QByteArray &png, const QColor &dominant);

private:
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QThreadPool m_pool;
    quint64 m_sequence = 0, m_generation = 0;
};
} // namespace EchoMusic
