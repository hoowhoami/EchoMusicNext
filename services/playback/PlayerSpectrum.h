#pragma once
#include "SpectrumFrame.h"
#include <QObject>
#include <QJsonObject>
#include <QPointer>
#include <QVariantMap>
#include <QTimer>
#include <QElapsedTimer>
#include <QMap>
namespace EchoMusic
{
class AudioEngine;
class PlaybackService;
/// One GUI broker for the engine's bounded post-output PCM tap. Subscribers
/// contribute only while visible; one native FFT request can be in flight.
class PlayerSpectrum final : public QObject
{
    Q_OBJECT
public:
    PlayerSpectrum(PlaybackService &, AudioEngine &);
    ~PlayerSpectrum() override;
    quint64 subscribe(QObject *owner, const QVariantMap &options);
    void setPaused(quint64 id, bool paused);
    void remove(quint64 id);
    QVariantMap status() const;
    static QVariantMap normalize(const QVariantMap &options);
    void stop();
    int subscriberCount() const
    {
        return m_entries.size();
    }
signals:
    void frameReady(quint64 id, const SpectrumFrameData &frame);
    void statusChanged();

private:
    struct Entry
    {
        QPointer<QObject> owner;
        QVariantMap options;
        QList<QPair<int, double>> interpolation;
        QList<QList<QPair<int, double>>> ranges;
        quint64 projectionRevision = 0;
        qsizetype sourceCount = -1;
        bool paused = false;
        qint64 last = -10000;
    };
    void reconcile();
    void accept(quint64 track, quint64 revision, quint64 request,
                QSharedPointer<const RawSpectrumFrame>);
    quint64 m_request = 0;
    PlaybackService &m_playback;
    AudioEngine &m_engine;
    QMap<quint64, Entry> m_entries;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QVariantMap m_options;
    quint64 m_next = 0, m_revision = 0, m_source = 0;
    bool m_pending = false, m_stopped = false, m_running = false;
};
} // namespace EchoMusic
