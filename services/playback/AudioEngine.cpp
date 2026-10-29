#include "AudioEngine.h"
#include "echo_ffi.h"
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <deque>
#include <utility>
#include <QJsonArray>

namespace EchoMusic
{
struct AudioSink
{
    QMutex mutex;
    std::deque<QJsonObject> events;
    EchoAudio *handle = nullptr; // worker thread only
    quint64 sequence = 0;        // worker thread only
    bool overflow = false;
    void push(QJsonObject event)
    {
        QMutexLocker lock(&mutex);
        const auto kind = event.value("event").toString();
        const bool observation =
            kind == "time-update" || kind.endsWith("stats") || kind == "spectrum";
        if (observation) {
            for (auto it = events.rbegin(); it != events.rend(); ++it)
                if (it->value("event") == event.value("event") &&
                    it->value("track_seq") == event.value("track_seq")) {
                    *it = std::move(event);
                    return;
                }
        }
        if (events.size() >= 256) {
            if (observation)
                return;
            // Critical loss is reported explicitly; never grow with a frozen GUI.
            overflow = true;
            events.pop_front();
        }
        events.push_back(std::move(event));
    }
};
static void audioCallback(void *context, const EchoResult *result)
{
    auto &sink = *static_cast<AudioSink *>(context);
    const auto bytes =
        QByteArray(reinterpret_cast<const char *>(result->payload), qsizetype(result->payload_len));
    const auto event = QJsonDocument::fromJson(bytes).object();
    if (!event.isEmpty())
        sink.push(event);
}
AudioEngine::AudioEngine(QObject *parent, const QJsonObject &options)
    : QObject(parent), m_worker(new QObject), m_sink(std::make_shared<AudioSink>())
{
    m_worker->moveToThread(&m_thread);
    m_thread.setObjectName("echo-audio-bridge");
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
    QMetaObject::invokeMethod(
        m_worker, [sink = m_sink, options = QJsonDocument(options).toJson(QJsonDocument::Compact)] {
            sink->handle = echo_audio_create(audioCallback, sink.get(),
                                             reinterpret_cast<const uint8_t *>(options.constData()),
                                             size_t(options.size()));
            if (!sink->handle)
                sink->push({{"event", "error"}, {"message", "无法初始化音频引擎"}});
        });
    m_drain.setInterval(16);
    connect(&m_drain, &QTimer::timeout, this, [this] {
        std::deque<QJsonObject> batch;
        bool overflow;
        {
            QMutexLocker lock(&m_sink->mutex);
            overflow = std::exchange(m_sink->overflow, false);
            batch.swap(m_sink->events);
        }
        if (overflow)
            emit eventReceived({{"event", "error"}, {"message", "音频事件队列溢出，请重新播放"}});
        for (const auto &event : batch)
            emit eventReceived(event);
    });
    m_drain.start();
}
AudioEngine::~AudioEngine()
{
    shutdown();
    m_thread.wait();
}
void AudioEngine::stallTimeout(double seconds)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker, [sink = m_sink, seconds] {
            if (!echo_audio_stall_timeout(sink->handle, seconds))
                sink->push({{"event", "control-error"}, {"message", "卡死检测设置未被接受"}});
        });
}
void AudioEngine::networkTimeout(double seconds)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker, [sink = m_sink, seconds] {
            if (!echo_audio_network_timeout(sink->handle, seconds))
                sink->push({{"event", "control-error"}, {"message", "网络超时设置未被接受"}});
        });
}
void AudioEngine::load(const QString &source, quint64 sequence, qint64 audioTrack)
{
    if (m_closing)
        return;
    QMetaObject::invokeMethod(
        m_worker, [sink = m_sink, bytes = source.toUtf8(), sequence, audioTrack] {
            sink->sequence = sequence;
            if (!echo_audio_load_track(sink->handle,
                                       reinterpret_cast<const uint8_t *>(bytes.constData()),
                                       size_t(bytes.size()), sequence, audioTrack))
                sink->push({{"event", "error"},
                            {"track_seq", qint64(sequence)},
                            {"message", "音频加载请求未被接受"}});
        });
}
void AudioEngine::switchSource(const QString &source, quint64 sequence, qint64 audioTrack,
                               quint64 sourceRevision)
{
    if (m_closing)
        return;
    QMetaObject::invokeMethod(m_worker, [sink = m_sink, bytes = source.toUtf8(), sequence,
                                         audioTrack, sourceRevision] {
        if (!echo_audio_switch_source(sink->handle,
                                      reinterpret_cast<const uint8_t *>(bytes.constData()),
                                      size_t(bytes.size()), sequence, audioTrack, sourceRevision))
            sink->push({{"event", "error"},
                        {"track_seq", qint64(sequence)},
                        {"source_revision", qint64(sourceRevision)},
                        {"message", "音源切换请求未被接受"}});
    });
}
void AudioEngine::cancelSourceReplacement()
{
    if (m_closing)
        return;
    QMetaObject::invokeMethod(
        m_worker, [sink = m_sink] { echo_audio_cancel_source_replacement(sink->handle); });
}
void AudioEngine::command(Command command, double value)
{
    if (m_closing)
        return;
    QMetaObject::invokeMethod(m_worker, [sink = m_sink, command, value] {
        if (!echo_audio_command(sink->handle, uint32_t(command), value))
            sink->push({{"event", "error"},
                        {"track_seq", qint64(sink->sequence)},
                        {"message", "音频命令未被接受"}});
    });
}
void AudioEngine::beginNext(quint64 revision)
{
    if (m_closing)
        return;
    QMetaObject::invokeMethod(m_worker, [sink = m_sink, revision] {
        const auto requestId = echo_audio_begin_next(sink->handle);
        sink->push({{"event", "next-source-begun"},
                    {"request_id", qint64(requestId)},
                    {"preparation_revision", qint64(revision)}});
    });
}
void AudioEngine::playFaded(double volume, int durationMs)
{
    if (m_closing)
        return;
    QMetaObject::invokeMethod(m_worker, [sink = m_sink, volume, durationMs] {
        if (!echo_audio_play_faded(sink->handle, volume, uint32_t(durationMs)))
            sink->push({{"event", "error"},
                        {"track_seq", qint64(sink->sequence)},
                        {"message", "播放淡入请求未被接受"}});
    });
}
void AudioEngine::prepareNext(const QString &source, qint64 trackId, quint64 sequence,
                              quint64 requestId, quint64 revision, double gain)
{
    if (m_closing)
        return;
    QMetaObject::invokeMethod(m_worker, [sink = m_sink, bytes = source.toUtf8(), trackId, sequence,
                                         requestId, revision, gain] {
        if (!echo_audio_prepare_next(
                sink->handle, reinterpret_cast<const uint8_t *>(bytes.constData()),
                size_t(bytes.size()), trackId, sequence, requestId, revision, gain))
            sink->push({{"event", "next-source-prepared"},
                        {"next_sequence", qint64(sequence)},
                        {"preparation_revision", qint64(revision)},
                        {"prepared", false}});
    });
}
void AudioEngine::clearNext()
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker,
                                  [sink = m_sink] { echo_audio_clear_next(sink->handle); });
}
void AudioEngine::commitNext(quint64 revision)
{
    if (m_closing)
        return;
    QMetaObject::invokeMethod(m_worker, [sink = m_sink, revision] {
        if (!echo_audio_commit_next(sink->handle, revision))
            sink->push({{"event", "next-source-committed"},
                        {"preparation_revision", qint64(revision)},
                        {"committed", false}});
    });
}
void AudioEngine::adoptSequence(quint64 sequence)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker, [sink = m_sink, sequence] {
            sink->sequence = sequence;
            echo_audio_adopt_sequence(sink->handle, sequence);
        });
}
void AudioEngine::transition(int mode, double fadeSeconds, bool matchTempo)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker, [sink = m_sink, mode, fadeSeconds, matchTempo] {
            if (!echo_audio_transition(sink->handle, uint32_t(mode), fadeSeconds, matchTempo))
                sink->push({{"event", "control-error"}, {"message", "歌曲过渡设置未被接受"}});
        });
}
void AudioEngine::mediaMetadata(const QString &title, const QString &artist, const QString &album,
                                qint64 duration, const QByteArray &cover)
{
    if (m_closing || cover.size() > 2 * 1024 * 1024)
        return;
    QMetaObject::invokeMethod(m_worker, [sink = m_sink, title, artist, album, duration, cover] {
        const auto data = QJsonDocument(QJsonObject{{"title", title},
                                                    {"artist", artist},
                                                    {"album", album},
                                                    {"duration_ms", double(duration)}})
                              .toJson(QJsonDocument::Compact);
        echo_audio_media_metadata(
            sink->handle, reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()),
            reinterpret_cast<const uint8_t *>(cover.constData()), size_t(cover.size()));
    });
}
void AudioEngine::equalizer(const QList<double> &gains)
{
    if (m_closing || gains.size() != 10)
        return;
    QMetaObject::invokeMethod(m_worker, [sink = m_sink, gains] {
        if (!echo_audio_equalizer(sink->handle, gains.constData(), size_t(gains.size())))
            sink->push({{"event", "control-error"}, {"message", "均衡器请求未被接受"}});
    });
}
void AudioEngine::output(const QString &name, bool exclusive, quint64 revision)
{
    if (m_closing)
        return;
    QMetaObject::invokeMethod(m_worker, [sink = m_sink, bytes = name.toUtf8(), exclusive,
                                         revision] {
        if (!echo_audio_output(sink->handle, reinterpret_cast<const uint8_t *>(bytes.constData()),
                               size_t(bytes.size()), exclusive, revision))
            sink->push({{"event", "control-error"},
                        {"output_revision", qint64(revision)},
                        {"message", "输出设备请求未被接受"}});
    });
}
void AudioEngine::disconnectPolicy(bool pause)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker, [sink = m_sink, pause] {
            echo_audio_disconnect_policy(sink->handle, pause);
        });
}
void AudioEngine::refreshDevices()
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker, [sink = m_sink] { echo_audio_devices(sink->handle); });
}
void AudioEngine::effect(const QJsonObject &configuration, quint64 revision)
{
    if (m_closing)
        return;
    QMetaObject::invokeMethod(m_worker, [sink = m_sink, configuration, revision] {
        auto payload = configuration;
        payload.insert("effectRevision", qint64(revision));
        const auto bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        if (!echo_audio_effect(sink->handle, reinterpret_cast<const uint8_t *>(bytes.constData()),
                               size_t(bytes.size())))
            sink->push({{"event", "control-error"},
                        {"effect_revision", qint64(revision)},
                        {"message", "音效请求未被接受"}});
    });
}
void AudioEngine::normalization(double gainDb)
{
    if (!m_closing)
        QMetaObject::invokeMethod(
            m_worker, [sink = m_sink, gainDb] { echo_audio_normalization(sink->handle, gainDb); });
}
void AudioEngine::mediaTimeline(qint64 position, qint64 duration, bool playing, double rate)
{
    if (m_closing)
        return;
    QMetaObject::invokeMethod(m_worker, [sink = m_sink, position, duration, playing, rate] {
        echo_audio_media_timeline(sink->handle, double(position), double(duration), playing, rate);
    });
}
void AudioEngine::retryInitialization(const QJsonObject &options)
{
    if (m_closing || m_restartPending)
        return;
    m_restartPending = true;
    const auto retired = std::exchange(m_sink, std::make_shared<AudioSink>());
    QMetaObject::invokeMethod(
        m_worker, [this, retired, sink = m_sink,
                   bytes = QJsonDocument(options).toJson(QJsonDocument::Compact)] {
            echo_audio_destroy(std::exchange(retired->handle, nullptr));
            sink->handle = echo_audio_create(audioCallback, sink.get(),
                                             reinterpret_cast<const uint8_t *>(bytes.constData()),
                                             size_t(bytes.size()));
            if (!sink->handle)
                sink->push({{"event", "error"}, {"message", "无法初始化音频引擎"}});
            QMetaObject::invokeMethod(this, [this] { m_restartPending = false; });
        });
}
void AudioEngine::configureSpectrum(const QJsonObject &options)
{
    if (m_closing)
        return;
    QMetaObject::invokeMethod(
        m_worker, [sink = m_sink, bytes = QJsonDocument(options).toJson(QJsonDocument::Compact)] {
            echo_audio_spectrum_configure(sink->handle,
                                          reinterpret_cast<const uint8_t *>(bytes.constData()),
                                          size_t(bytes.size()));
        });
}
void AudioEngine::spectrumSnapshot(quint64 revision)
{
    if (m_closing)
        return;
    QMetaObject::invokeMethod(m_worker, [sink = m_sink, revision] {
        if (!echo_audio_spectrum_snapshot(sink->handle, revision))
            sink->push({{"event", "spectrum"},
                        {"track_seq", qint64(sink->sequence)},
                        {"spectrum_revision", qint64(revision)},
                        {"error", "Spectrum request rejected"}});
    });
}
void AudioEngine::shutdown()
{
    if (std::exchange(m_closing, true))
        return;
    m_drain.stop();
    QMetaObject::invokeMethod(m_worker, [this, sink = m_sink] {
        echo_audio_destroy(std::exchange(sink->handle, nullptr));
        QMetaObject::invokeMethod(this, [this] { emit closed(); });
        m_thread.quit();
    });
}
} // namespace EchoMusic
