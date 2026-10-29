#include "PlayerSpectrum.h"
#include "PlaybackService.h"
#include "AudioEngine.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <cmath>
namespace EchoMusic
{
QVariantMap PlayerSpectrum::normalize(const QVariantMap &options)
{
    QVariantMap out;
    const auto number = [&](const char *key, double fallback, double low, double high) {
        bool ok = false;
        const double v = options.value(key, fallback).toDouble(&ok);
        return ok && std::isfinite(v) ? qBound(low, v, high) : fallback;
    };
    out["fps"] = int(number("fps", 30, 1, 60));
    out["binCount"] = int(number("binCount", 128, 8, 512));
    int fft = int(number("fftSize", 2048, 256, 8192));
    if ((fft & (fft - 1)) != 0)
        fft = 2048;
    out["fftSize"] = fft;
    out["smoothing"] = number("smoothing", .65, 0, .95);
    out["minFrequency"] = number("minFrequency", 20, 1, 20000);
    out["maxFrequency"] =
        qMax(out["minFrequency"].toDouble() + 1, number("maxFrequency", 20000, 2, 24000));
    const auto scale = options.value("scale", "log").toString();
    out["scale"] = QStringList{"log", "linear", "mel"}.contains(scale) ? scale : "log";
    out["includeWaveform"] = options.value("includeWaveform", false).toBool();
    return out;
}
PlayerSpectrum::PlayerSpectrum(PlaybackService &playback, AudioEngine &engine)
    : m_playback(playback), m_engine(engine)
{
    m_clock.start();
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (!m_pending && m_running) {
            m_pending = true;
            m_engine.spectrumSnapshot(m_revision);
        }
    });
    connect(&m_engine, &AudioEngine::eventReceived, this, &PlayerSpectrum::accept);
    connect(&playback, &PlaybackService::changed, this, &PlayerSpectrum::reconcile);
    connect(&playback, &PlaybackService::engineInitializationChanged, this,
            &PlayerSpectrum::reconcile);
}
PlayerSpectrum::~PlayerSpectrum()
{
    stop();
}
quint64 PlayerSpectrum::subscribe(QObject *owner, const QVariantMap &options)
{
    if (m_stopped || !owner || m_entries.size() >= 128)
        return 0;
    auto id = ++m_next;
    m_entries.insert(id, {owner, normalize(options), false, -10000});
    connect(owner, &QObject::destroyed, this, [this, id] { remove(id); });
    reconcile();
    return id;
}
void PlayerSpectrum::setPaused(quint64 id, bool paused)
{
    auto it = m_entries.find(id);
    if (it != m_entries.end() && it->paused != paused) {
        it->paused = paused;
        it->last = -10000;
        reconcile();
    }
}
void PlayerSpectrum::remove(quint64 id)
{
    if (m_entries.remove(id))
        reconcile();
}
QVariantMap PlayerSpectrum::status() const
{
    return {{"available", m_playback.engineReady()},
            {"running", m_running},
            {"provider", m_playback.engineReady() ? "player" : "unavailable"},
            {"reason", m_running                   ? QString()
                       : m_entries.isEmpty()       ? QString()
                       : !m_playback.engineReady() ? tr("音频引擎未就绪")
                       : !m_playback.hasTrack()    ? tr("暂无播放音频")
                       : !m_playback.playing()     ? tr("播放已暂停")
                                                   : tr("等待可见的频谱区域")},
            {"subscriberCount", m_entries.size()}};
}
void PlayerSpectrum::reconcile()
{
    if (m_stopped)
        return;
    QVariantMap merged{
        {"enabled", false},         {"bands", 8},          {"fps", 1},         {"fft_size", 256},
        {"min_frequency", 20000.},  {"max_frequency", 2.}, {"smoothing", .95}, {"scale", "log"},
        {"include_waveform", false}};
    bool any = false;
    for (const auto &e : std::as_const(m_entries))
        if (e.owner && !e.paused) {
            any = true;
            const auto &o = e.options;
            merged["bands"] = qMax(merged["bands"].toInt(), o["binCount"].toInt());
            merged["fps"] = qMax(merged["fps"].toInt(), o["fps"].toInt());
            merged["fft_size"] = qMax(merged["fft_size"].toInt(), o["fftSize"].toInt());
            merged["min_frequency"] =
                qMin(merged["min_frequency"].toDouble(), o["minFrequency"].toDouble());
            merged["max_frequency"] =
                qMax(merged["max_frequency"].toDouble(), o["maxFrequency"].toDouble());
            merged["smoothing"] = qMin(merged["smoothing"].toDouble(), o["smoothing"].toDouble());
            merged["include_waveform"] =
                merged["include_waveform"].toBool() || o["includeWaveform"].toBool();
        }
    bool running = any && m_playback.engineReady() && m_playback.hasTrack() &&
                   m_playback.playing() && !m_playback.loading();
    merged["enabled"] = running;
    if (!running)
        merged = {{"enabled", false}};
    const bool sourceChanged = m_source != m_playback.sourceRevision();
    m_source = m_playback.sourceRevision();
    if (merged != m_options || sourceChanged) {
        m_options = merged;
        ++m_revision;
        if (m_playback.engineReady())
            m_engine.configureSpectrum(QJsonObject::fromVariantMap(merged));
    }
    if (!m_playback.engineReady())
        m_pending = false; // A restarted engine retires its previous event sink.
    const bool changed = m_running != running;
    m_running = running;
    if (running) {
        m_timer.setInterval(int(std::ceil(1000. / merged["fps"].toInt())));
        if (!m_timer.isActive())
            m_timer.start();
    } else
        m_timer.stop();
    emit statusChanged();
    if (changed && !running)
        for (const auto id : m_entries.keys())
            if (m_entries.contains(id) && !m_entries.value(id).paused)
                emit frameReady(id, {{"source", "silence"},
                                     {"state", m_playback.hasTrack() ? "paused" : "idle"},
                                     {"bins", QVariantList()},
                                     {"waveform", QVariantList()},
                                     {"timestamp", QDateTime::currentMSecsSinceEpoch()},
                                     {"peak", 0.},
                                     {"rms", 0.}});
}
void PlayerSpectrum::accept(const QJsonObject &event)
{
    if (event.value("event") != "spectrum")
        return;
    m_pending = false;
    if (m_stopped || !m_running ||
        quint64(event.value("track_seq").toDouble()) != m_playback.audioSequence() ||
        quint64(event.value("spectrum_revision").toDouble()) != m_revision ||
        m_source != m_playback.sourceRevision())
        return;
    const auto raw = event.value("frame").toObject();
    const auto bins = raw.value("bins").toArray();
    const auto now = m_clock.elapsed();
    for (const auto id : m_entries.keys()) {
        auto it = m_entries.find(id);
        if (it == m_entries.end())
            continue;
        auto &e = it.value();
        if (!e.owner || e.paused || now - e.last < std::ceil(1000. / e.options["fps"].toInt()))
            continue;
        e.last = now;
        QVariantMap frame{{"source", raw.isEmpty()                        ? "unavailable"
                                     : raw.value("rms").toDouble() > 1e-8 ? "player"
                                                                          : "silence"},
                          {"state", "playing"},
                          {"timestamp", raw.value("timestamp").toDouble() * 1000},
                          {"timePos", m_playback.positionMs() / 1000.},
                          {"sampleRate", raw.value("sample_rate").toInt()},
                          {"fftSize", raw.value("fft_size").toInt()},
                          {"peak", raw.value("peak").toDouble()},
                          {"rms", raw.value("rms").toDouble()},
                          {"minFrequency", e.options["minFrequency"]},
                          {"maxFrequency", e.options["maxFrequency"]}};
        QVariantList values;
        const int count = e.options["binCount"].toInt();
        values.reserve(count);
        const double low = e.options["minFrequency"].toDouble(),
                     high = e.options["maxFrequency"].toDouble(),
                     srcLow = m_options["min_frequency"].toDouble(),
                     srcHigh = m_options["max_frequency"].toDouble();
        for (int i = 0; i < count; ++i) {
            double t = (i + .5) / count;
            const auto scale = e.options["scale"].toString();
            double f =
                scale == "linear" ? low + (high - low) * t
                : scale == "mel"
                    ? 700 * (std::pow(1 + low / 700, 1 - t) * std::pow(1 + high / 700, t) - 1)
                    : low * std::pow(high / low, t);
            double at =
                bins.isEmpty()
                    ? 0
                    : qBound(0.,
                             std::log(f / srcLow) / std::log(srcHigh / srcLow) * bins.size() - .5,
                             double(bins.size() - 1));
            int j = int(at);
            values.append(bins.isEmpty()
                              ? 0.
                              : bins[j].toDouble() * (1 - (at - j)) +
                                    bins[qMin(j + 1, int(bins.size()) - 1)].toDouble() * (at - j));
        }
        frame["bins"] = values;
        if (e.options["includeWaveform"].toBool())
            frame["waveform"] = raw.value("waveform").toArray().toVariantList();
        emit frameReady(id, frame);
    }
}
void PlayerSpectrum::stop()
{
    if (m_stopped)
        return;
    m_stopped = true;
    m_timer.stop();
    m_entries.clear();
    m_running = false;
    ++m_revision;
    m_engine.configureSpectrum({{"enabled", false}});
}
} // namespace EchoMusic
