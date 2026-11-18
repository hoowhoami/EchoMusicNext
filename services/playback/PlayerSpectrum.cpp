#include "PlayerSpectrum.h"
#include "PlaybackService.h"
#include "AudioEngine.h"
#include <QJsonObject>
#include <QDateTime>
#include <cmath>
#include <tuple>
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
    bool fpsValid = false;
    const auto requestedFps = options.value("fps").toDouble(&fpsValid);
    if (fpsValid && requestedFps == 0)
        out["fps"] = 0; // Zero removes pacing; one native snapshot remains in flight.
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
    const auto requestedRanges = options.value("rangesHz");
    if (options.contains("rangesHz") && !requestedRanges.canConvert<QVariantList>())
        return {};
    const auto ranges = requestedRanges.toList();
    if (!ranges.isEmpty()) {
        if (ranges.size() > 32 || out["scale"] != "linear")
            return {};
        for (const auto &range : ranges) {
            const auto pair = range.toList();
            if (pair.size() != 2)
                return {};
            bool a = false, b = false;
            const auto low = pair[0].toDouble(&a), high = pair[1].toDouble(&b);
            if (!a || !b || !std::isfinite(low) || !std::isfinite(high) || low < 0 || high <= low)
                return {};
        }
        out["rangesHz"] = ranges;
    }
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
            m_engine.spectrumSnapshot(m_revision, ++m_request);
        }
    });
    connect(&m_engine, &AudioEngine::spectrumReceived, this, &PlayerSpectrum::accept);
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
    Entry entry{};
    entry.owner = owner;
    entry.options = normalize(options);
    if (entry.options.isEmpty())
        return 0;
    auto id = ++m_next;
    m_entries.insert(id, std::move(entry));
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
            merged["fps"] = merged["fps"].toInt() == 0 || o["fps"].toInt() == 0
                                ? 0
                                : qMax(merged["fps"].toInt(), o["fps"].toInt());
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
        const auto fps = merged["fps"].toInt();
        m_timer.setSingleShot(fps == 0);
        m_timer.setInterval(fps == 0 ? 0 : int(std::ceil(1000. / fps)));
        if (!m_timer.isActive() && (fps != 0 || !m_pending))
            m_timer.start();
    } else
        m_timer.stop();
    emit statusChanged();
    if (changed && !running)
        for (const auto id : m_entries.keys())
            if (m_entries.contains(id) && !m_entries.value(id).paused) {
                SpectrumFrameData idle;
                idle.source = QStringLiteral("silence");
                idle.state =
                    m_playback.hasTrack() ? QStringLiteral("paused") : QStringLiteral("idle");
                idle.timestamp = QDateTime::currentMSecsSinceEpoch();
                idle.includeWaveform = true;
                emit frameReady(id, idle);
            }
}

void PlayerSpectrum::accept(quint64 track, quint64 revision, quint64 request,
                            QSharedPointer<const RawSpectrumFrame> raw)
{
    if (!m_pending || request != m_request)
        return;
    m_pending = false;
    if (!m_stopped && m_running && m_options["fps"].toInt() == 0)
        m_timer.start();
    if (m_stopped || !m_running || track != m_playback.audioSequence() || revision != m_revision ||
        m_source != m_playback.sourceRevision())
        return;
    const QSpan<const double> bins = raw ? raw->bins : QSpan<const double>{};
    const auto now = m_clock.elapsed();
    using Projection = std::tuple<int, QString, double, double>;
    QMap<Projection, QList<double>> projections;
    for (const auto id : m_entries.keys()) {
        if (revision != m_revision || m_stopped)
            break;
        auto it = m_entries.find(id);
        if (it == m_entries.end())
            continue;
        auto &e = it.value();
        const int fps = e.options["fps"].toInt();
        if (!e.owner || e.paused || (fps > 0 && now - e.last < std::ceil(1000. / fps)))
            continue;
        e.last = now;
        const auto count = e.options["binCount"].toInt();
        const double low = e.options["minFrequency"].toDouble(),
                     high = e.options["maxFrequency"].toDouble();
        const auto scale = e.options["scale"].toString();
        if (e.projectionRevision != revision || e.sourceCount != bins.size()) {
            e.projectionRevision = revision;
            e.sourceCount = bins.size();
            e.interpolation.clear();
            e.ranges.clear();
            const double srcLow = m_options["min_frequency"].toDouble(),
                         srcHigh = m_options["max_frequency"].toDouble();
            for (int i = 0; i < count; ++i) {
                const double t = (i + .5) / count;
                const double f =
                    scale == "linear" ? low + (high - low) * t
                    : scale == "mel"
                        ? 700 * (std::pow(1 + low / 700, 1 - t) * std::pow(1 + high / 700, t) - 1)
                        : low * std::pow(high / low, t);
                const double at =
                    bins.empty()
                        ? 0
                        : qBound(0.,
                                 std::log(f / srcLow) / std::log(srcHigh / srcLow) * bins.size() -
                                     .5,
                                 double(bins.size() - 1));
                e.interpolation.append({int(at), at - int(at)});
            }
            const double width = (high - low) / count;
            const auto ranges = e.options.value("rangesHz").toList();
            for (const auto &range : ranges) {
                const auto pair = range.toList();
                QList<QPair<int, double>> weights;
                for (int i = 0; i < count; ++i) {
                    const double overlap = qMin(pair[1].toDouble(), low + (i + 1) * width) -
                                           qMax(pair[0].toDouble(), low + i * width);
                    if (overlap > 0)
                        weights.append({i, overlap});
                }
                e.ranges.append(weights);
            }
        }
        const Projection key{count, scale, low, high};
        auto values = projections.find(key);
        if (values == projections.end()) {
            QList<double> projected(count, 0.);
            if (!bins.empty())
                for (int i = 0; i < count; ++i) {
                    const auto [j, alpha] = e.interpolation[i];
                    projected[i] = bins[j] * (1 - alpha) +
                                   bins[qMin(qsizetype(j + 1), bins.size() - 1)] * alpha;
                }
            values = projections.insert(key, std::move(projected));
        }
        SpectrumFrameData frame;
        frame.state = QStringLiteral("playing");
        frame.source = !raw              ? QStringLiteral("unavailable")
                       : raw->rms > 1e-8 ? QStringLiteral("player")
                                         : QStringLiteral("silence");
        frame.sequence = request;
        frame.timePos = m_playback.positionMs() / 1000.;
        frame.minFrequency = low;
        frame.maxFrequency = high;
        frame.bins = values.value();
        frame.includeWaveform = e.options["includeWaveform"].toBool();
        if (raw) {
            frame.timestamp = raw->timestamp;
            frame.peak = raw->peak;
            frame.rms = raw->rms;
            frame.sampleRate = raw->sampleRate;
            frame.fftSize = raw->fftSize;
            if (frame.includeWaveform)
                frame.waveformOwner = raw;
        }
        for (const auto &weights : std::as_const(e.ranges)) {
            double sum = 0, weight = 0;
            for (auto [i, w] : weights) {
                sum += frame.bins.at(i) * w;
                weight += w;
            }
            frame.bandMeans.append(weight ? sum / weight : 0.);
        }
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
