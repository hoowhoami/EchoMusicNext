#pragma once
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QJsonObject>
#include <memory>
namespace EchoMusic
{
struct AudioSink;
/// GUI-facing engine bridge. The worker alone owns the Rust handle. Callback
/// context survives drain; high-rate observations coalesce in a bounded mailbox.
class AudioEngine final : public QObject
{
    Q_OBJECT
public:
    enum Command
    {
        Play,
        Pause,
        Stop,
        Seek,
        Volume,
        Speed,
        LoopFile
    };
    explicit AudioEngine(QObject *parent = nullptr, const QJsonObject &options = {});
    void stallTimeout(double seconds);
    void networkTimeout(double seconds);
    ~AudioEngine() override;
    void load(const QString &source, quint64 sequence, qint64 audioTrack = 0);
    void switchSource(const QString &source, quint64 sequence, qint64 audioTrack = 0,
                      quint64 sourceRevision = 0);
    void cancelSourceReplacement();
    void beginNext(quint64 revision);
    void prepareNext(const QString &source, qint64 trackId, quint64 sequence, quint64 requestId,
                     quint64 revision, double normalizationGainDb);
    void clearNext();
    void commitNext(quint64 revision);
    void adoptSequence(quint64 sequence);
    void transition(int mode, double fadeSeconds, bool matchTempo);
    void command(Command command, double value = 0);
    void playFaded(double volume, int durationMs);
    void equalizer(const QList<double> &gains);
    void output(const QString &name, bool exclusive, quint64 revision = 0);
    void refreshDevices();
    void configureSpectrum(const QJsonObject &options);
    void spectrumSnapshot(quint64 revision);
    void disconnectPolicy(bool pause);
    void effect(const QJsonObject &configuration, quint64 revision = 0);
    void normalization(double gainDb);
    void mediaMetadata(const QString &title, const QString &artist, const QString &album,
                       qint64 durationMs, const QByteArray &cover = {});
    void mediaTimeline(qint64 positionMs, qint64 durationMs, bool playing, double rate);
    void retryInitialization(const QJsonObject &options);
    void shutdown();
signals:
    void eventReceived(const QJsonObject &event);
    void closed();

private:
    QThread m_thread;
    QObject *m_worker;
    QTimer m_drain;
    std::shared_ptr<AudioSink> m_sink;
    bool m_closing = false, m_restartPending = false;
};
} // namespace EchoMusic
