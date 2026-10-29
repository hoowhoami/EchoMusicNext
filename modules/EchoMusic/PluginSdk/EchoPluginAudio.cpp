#include "EchoPluginAudio.h"
#include "EchoPluginContext.h"
#include "PlaybackService.h"
#include "PlayerSpectrum.h"
#include <QUuid>
#include <utility>
namespace EchoMusic
{
EchoPluginAudio::EchoPluginAudio(EchoPluginContext &scope) : QObject(&scope), m_spectrum(scope)
{
    m_spectrum.setParent(this);
}
EchoPluginSpectrumSubscription::EchoPluginSpectrumSubscription(EchoPluginContext &scope,
                                                               EchoPluginRegistration &lease,
                                                               const QVariantMap &options)
    : QObject(&scope), m_broker(*scope.host().playback.spectrum()), m_lease(lease)
{
    connect(&m_broker, &PlayerSpectrum::frameReady, this,
            [this](quint64 id, const QVariantMap &frame) {
                if (id == m_id && m_lease.active() && !m_paused) {
                    m_frame = frame;
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
            m_frame.clear();
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
