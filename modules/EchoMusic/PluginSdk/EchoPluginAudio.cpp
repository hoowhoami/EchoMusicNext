#include "EchoPluginAudio.h"
#include "EchoPluginContext.h"
#include "PlaybackService.h"
#include "PlayerSpectrum.h"
#include <QUuid>
#include <utility>
namespace EchoMusic
{
static QVariantList boxed(QSpan<const double> values)
{
    QVariantList result;
    result.reserve(values.size());
    for (double sample : values)
        result.append(sample);
    return result;
}
EchoPluginSpectrumFrame::EchoPluginSpectrumFrame(EchoPluginContext &scope, QObject *parent)
    : QObject(parent), m_scope(scope)
{
}
void EchoPluginSpectrumFrame::replace(const SpectrumFrameData &frame)
{
    m_data = frame;
    m_legacy.clear();
    emit changed();
}
QVariantList EchoPluginSpectrumFrame::bandMeans() const
{
    return boxed(m_data.bandMeans);
}
QJSValue EchoPluginSpectrumFrame::copy(QSpan<const double> values) const
{
    auto &engine = m_scope.engine();
    const QByteArray bytes(reinterpret_cast<const char *>(values.data()), values.size_bytes());
    return engine.globalObject()
        .property(QStringLiteral("Float64Array"))
        .callAsConstructor({engine.toScriptValue(bytes)});
}
QJSValue EchoPluginSpectrumFrame::copyBins() const
{
    return copy(m_data.bins);
}
QJSValue EchoPluginSpectrumFrame::copyWaveform() const
{
    return copy(m_data.waveform());
}
QVariant EchoPluginSpectrumFrame::legacy() const
{
    if (m_data.state.isEmpty() || m_legacy.isValid())
        return m_legacy;
    QVariantMap frame{{"state", state()}, {"source", source()}, {"timestamp", timestamp()},
                      {"peak", peak()},   {"rms", rms()},       {"bins", boxed(m_data.bins)}};
    if (state() == QStringLiteral("playing")) {
        frame.insert("timePos", timePos());
        frame.insert("sampleRate", sampleRate());
        frame.insert("fftSize", fftSize());
        frame.insert("minFrequency", minFrequency());
        frame.insert("maxFrequency", maxFrequency());
    }
    if (m_data.includeWaveform)
        frame.insert("waveform", boxed(m_data.waveform()));
    m_legacy = frame;
    return m_legacy;
}
EchoPluginAudio::EchoPluginAudio(EchoPluginContext &scope) : QObject(&scope), m_spectrum(scope)
{
    m_spectrum.setParent(this);
}
EchoPluginSpectrumSubscription::EchoPluginSpectrumSubscription(EchoPluginContext &scope,
                                                               EchoPluginRegistration &lease,
                                                               const QVariantMap &options)
    : QObject(&scope), m_broker(*scope.host().playback.spectrum()), m_lease(lease),
      m_frame(scope, this)
{
    connect(&m_broker, &PlayerSpectrum::frameReady, this,
            [this](quint64 id, const SpectrumFrameData &frame) {
                if (id == m_id && m_lease.active() && !m_paused) {
                    m_frame.replace(frame);
                    emit frameChanged();
                }
            });
    connect(&m_broker, &PlayerSpectrum::statusChanged, this,
            &EchoPluginSpectrumSubscription::statusChanged);
    connect(&m_lease, &EchoPluginRegistration::activeChanged, this,
            &EchoPluginSpectrumSubscription::activeChanged);
    lease.stage(
        [this, options] {
            m_id = m_broker.subscribe(this, options);
            if (m_paused)
                m_broker.setPaused(m_id, true);
            return m_id != 0;
        },
        [this] {
            m_broker.remove(std::exchange(m_id, 0));
            m_frame.replace({});
            emit frameChanged();
        });
}
EchoPluginSpectrumSubscription::~EchoPluginSpectrumSubscription()
{
    m_broker.remove(m_id);
}
QVariant EchoPluginSpectrumSubscription::status() const
{
    return m_broker.status();
}
void EchoPluginSpectrumSubscription::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    if (m_id)
        m_broker.setPaused(m_id, paused);
    if (paused) {
        m_frame.replace({});
        emit frameChanged();
    }
    emit pausedChanged();
}
void EchoPluginSpectrumSubscription::dispose()
{
    m_lease.dispose();
}
EchoPluginSpectrum::EchoPluginSpectrum(EchoPluginContext &scope) : QObject(&scope), m_scope(scope)
{
    connect(scope.host().playback.spectrum(), &PlayerSpectrum::statusChanged, this,
            &EchoPluginSpectrum::statusChanged);
}
EchoPluginSpectrumSubscription *EchoPluginSpectrum::subscribe(const QVariantMap &options)
{
    if (!m_scope.package().capabilities.contains("audioSpectrum"))
        return nullptr;
    auto *lease =
        m_scope.registration("spectrum", QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!lease)
        return nullptr;
    auto *sub = new EchoPluginSpectrumSubscription(m_scope, *lease, options);
    QQmlEngine::setObjectOwnership(sub, QQmlEngine::CppOwnership);
    connect(lease, &EchoPluginRegistration::activeChanged, sub, [sub, lease] {
        if (!lease->active()) {
            sub->deleteLater();
            lease->deleteLater();
        }
    });
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return sub;
}
QVariant EchoPluginSpectrum::getStatus() const
{
    return m_scope.host().playback.spectrum()->status();
}
} // namespace EchoMusic
