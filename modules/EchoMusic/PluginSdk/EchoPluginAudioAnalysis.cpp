// SPDX-License-Identifier: GPL-3.0-only
#include "EchoPluginAudioAnalysis.h"
#include "EchoPluginContext.h"
#include "PlaybackService.h"
#include "PlaybackTransitionPresentation.h"
#include "AudioEngine.h"
#include "ApiClient.h"
#include "AnalysisService.h"
#include "AnalysisInstallation.h"
#include "NativePcmReader.h"
#include <QCoreApplication>
#include <QQmlEngine>
#include <algorithm>

namespace EchoMusic
{
namespace
{
class AnalysisLeaseOwner final : public QObject
{
    Q_OBJECT
public:
    AnalysisLeaseOwner(AudioAnalysis::Service &service, bool identityEnabled, QObject *parent)
        : QObject(parent), m_service(&service), m_identityEnabled(identityEnabled)
    {
    }
    QJsonObject sourceIdentity()
    {
        return m_identityEnabled && m_service ? m_service->sourceIdentity(this) : QJsonObject{};
    }

private:
    QPointer<AudioAnalysis::Service> m_service;
    const bool m_identityEnabled;
};
QJsonObject leaseIdentity(const EchoPluginAnalysisLease &lease)
{
    if (!lease.active())
        return {};
    const auto owner = qobject_cast<AnalysisLeaseOwner *>(lease.parent());
    return owner ? owner->sourceIdentity() : QJsonObject{};
}
AudioAnalysis::Config configuration()
{
    AudioAnalysis::Config buildTree;
    QString buildRoot;
#ifdef ECHO_AUDIO_ANALYSIS_BUILD_ROOT
    buildRoot = QString::fromUtf8(ECHO_AUDIO_ANALYSIS_BUILD_ROOT);
#endif
#ifdef ECHO_AUDIO_ANALYSIS_HELPER_PATH
    buildTree.helper = QString::fromUtf8(ECHO_AUDIO_ANALYSIS_HELPER_PATH);
#endif
#ifdef ECHO_AUDIO_ANALYSIS_MODEL_PATH
    buildTree.modelDirectory = QString::fromUtf8(ECHO_AUDIO_ANALYSIS_MODEL_PATH);
#endif
#ifdef ECHO_AUDIO_ANALYSIS_RUNTIME_PATH
    buildTree.runtimeDirectory = QString::fromUtf8(ECHO_AUDIO_ANALYSIS_RUNTIME_PATH);
#endif
    return AudioAnalysis::resolveInstallation(QCoreApplication::applicationDirPath(), buildRoot,
                                              buildTree);
}
AudioAnalysis::Service &service(EchoPluginContext &scope)
{
    auto &playback = scope.host().playback;
    auto *existing = playback.findChild<AudioAnalysis::Service *>("audio-analysis-service",
                                                                  Qt::FindDirectChildrenOnly);
    if (existing)
        return *existing;
    auto *created = new AudioAnalysis::Service(
        configuration(), [&playback] { return playback.analysisSourceFacts(); },
        [](const QJsonObject &facts, AudioAnalysis::Kind kind) {
            const auto captured = AudioEngine::captureAnalysisSource(facts);
            AudioAnalysis::Input input;
            input.code = captured.code;
            input.error = captured.error;
            if (captured.source) {
                input.localIdentity = captured.source->info().value("local_identity").toString();
                input.reader = captured.source->reader(kind);
            }
            return input;
        },
        &playback);
    created->setSourceAuthority(
        [&playback](const QJsonObject &selector) { return playback.analysisSourceFacts(selector); },
        [&playback](const QJsonObject &facts) { return playback.analysisSourceAuthority(facts); });
    created->setObjectName("audio-analysis-service");
    QObject::connect(&playback, &PlaybackService::changed, created,
                     &AudioAnalysis::Service::sourceChanged);
    QObject::connect(&playback.transitionPresentation(), &PlaybackTransitionPresentation::changed,
                     created, &AudioAnalysis::Service::sourceChanged);
    QObject::connect(&scope.host().api, &ApiClient::sessionChanged, created,
                     &AudioAnalysis::Service::sourceChanged);
    return *created;
}
} // namespace
EchoPluginAnalysisLease::EchoPluginAnalysisLease(AudioAnalysis::Job *job, QObject *parent)
    : QObject(parent), m_job(job)
{
    job->setParent(this);
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    connect(job, &AudioAnalysis::Job::changed, this, [this] {
        if (m_active)
            emit changed();
    });
    connect(job, &AudioAnalysis::Job::completed, this, [this] {
        if (m_active)
            emit completed();
    });
    connect(job, &AudioAnalysis::Job::progress, this, [this](int done, int total, double until) {
        if (!m_active)
            return;
        m_progress = {{"done", done}, {"total", total}, {"untilSeconds", until}};
        emit changed();
    });
}
EchoPluginAnalysisLease::~EchoPluginAnalysisLease()
{
    if (m_job) {
        m_job->disconnect(this);
        m_job->cancel();
    }
}
QString EchoPluginAnalysisLease::state() const
{
    if (!m_active || !m_job)
        return "canceled";
    switch (m_job->state()) {
    case AudioAnalysis::Job::State::Pending:
        return "pending";
    case AudioAnalysis::Job::State::Running:
        return "running";
    case AudioAnalysis::Job::State::Succeeded:
        return "succeeded";
    case AudioAnalysis::Job::State::Failed:
        return "failed";
    case AudioAnalysis::Job::State::Canceled:
        return "canceled";
    }
    return "failed";
}
bool EchoPluginAnalysisLease::finished() const
{
    return !m_active || !m_job || m_job->finished();
}
QVariant EchoPluginAnalysisLease::result() const
{
    return m_active && m_job && !m_job->result().isEmpty()
               ? QVariant(m_job->result().toVariantMap())
               : QVariant();
}
QString EchoPluginAnalysisLease::errorCode() const
{
    return m_active && m_job ? m_job->errorCode() : QStringLiteral("Canceled");
}
QString EchoPluginAnalysisLease::errorMessage() const
{
    return m_active && m_job ? m_job->errorMessage() : QStringLiteral("analysis lease retired");
}
QString EchoPluginAnalysisLease::sourceSequence() const
{
    return leaseIdentity(*this).value("sourceSequence").toString();
}
QString EchoPluginAnalysisLease::transitionEpoch() const
{
    return leaseIdentity(*this).value("transitionEpoch").toString();
}
void EchoPluginAnalysisLease::dispose()
{
    retire();
}
void EchoPluginAnalysisLease::retire()
{
    if (!m_active)
        return;
    m_active = false;
    if (m_job)
        m_job->cancel();
    m_progress.clear();
    const QPointer<EchoPluginAnalysisLease> guard(this);
    emit sourceIdentityChanged();
    if (guard)
        emit changed();
}
EchoPluginAudioAnalysis::EchoPluginAudioAnalysis(EchoPluginContext &scope)
    : QObject(&scope), m_scope(scope), m_service(service(scope))
{
    connect(&scope, &EchoPluginContext::activeChanged, this, [this] {
        if (!m_scope.active())
            shutdown();
    });
    connect(&m_service, &AudioAnalysis::Service::authorizationRevoked, this,
            [this](QObject *owner) {
                if (const auto lease = m_owners.value(owner))
                    lease->retire();
            });
    connect(&m_service, &AudioAnalysis::Service::authorizationChanged, this,
            [this](QObject *owner) {
                if (const auto lease = m_owners.value(owner))
                    emit lease->sourceIdentityChanged();
            });
    connect(&m_service, &AudioAnalysis::Service::drainedChanged, this, [this] {
        cleanupOwners();
        if (isDrained())
            emit drained();
    });
}
EchoPluginAudioAnalysis::~EchoPluginAudioAnalysis()
{
    invalidate();
}

QVariantMap EchoPluginAudioAnalysis::status() const
{
    return m_service.status().toVariantMap();
}
EchoPluginAnalysisLease *EchoPluginAudioAnalysis::request(const QVariantMap &options)
{
    using namespace AudioAnalysis;
    Job *job = nullptr;
    const bool transitionSources =
        m_scope.package().features.contains("audio.analysis.transitionSources");
    auto *owner = new AnalysisLeaseOwner(m_service, transitionSources, this);
    QJsonObject selector;
    bool sourceValid = !options.contains("source");
    if (options.contains("source") &&
        options.value("source").metaType() == QMetaType::fromType<QVariantMap>()) {
        const auto source = options.value("source").toMap();
        sourceValid = source.size() == 2;
        for (const auto *key : {"sequence", "transitionEpoch"}) {
            const auto value = source.value(key);
            bool ok = false;
            const auto text = value.toString();
            const auto number = text.toULongLong(&ok);
            sourceValid = sourceValid && value.metaType() == QMetaType::fromType<QString>() && ok &&
                          number > 0 && text == QString::number(number);
        }
        if (sourceValid)
            selector = QJsonObject::fromVariantMap(source);
    }
    if (m_closed || !m_scope.active())
        job = m_service.rejected(owner, "ScopeClosed", "Plugin presentation has ended");
    else if (!m_scope.package().capabilities.contains("audioAnalysis"))
        job = m_service.rejected(owner, "CapabilityDenied", "audioAnalysis capability is required");
    else if (options.contains("source") && !transitionSources)
        job = m_service.rejected(owner, "FeatureRequired",
                                 "audio.analysis.transitionSources feature is required");
    else if (!sourceValid || options.size() != (options.contains("source") ? 3 : 2) ||
             !options.contains("kind") || !options.contains("policy") ||
             options.value("kind").metaType() != QMetaType::fromType<QString>() ||
             options.value("policy").metaType() != QMetaType::fromType<QString>() ||
             (options.value("kind") != "beats" && options.value("kind") != "drumHits") ||
             (options.value("policy") != "storedOnly" && options.value("policy") != "gpuOnly"))
        job = m_service.rejected(owner, "InvalidArgument",
                                 "kind must be beats/drumHits, policy storedOnly/gpuOnly; source "
                                 "requires exact sequence/transitionEpoch decimal strings");
    m_leases.removeIf([](const auto &item) { return !item; });
    if (!job && m_leases.size() >= 8)
        job = m_service.rejected(owner, "Busy",
                                 "At most eight analysis leases may be retained per scope");
    if (!job) {
        job = m_service.request(
            owner, "user:" + QString::number(m_scope.host().api.currentAccountUserId()),
            options.value("kind") == "beats" ? Kind::Beats : Kind::DrumHits,
            options.value("policy") == "storedOnly" ? Policy::StoredOnly : Policy::GpuOnly,
            selector);
    }
    auto *lease = new EchoPluginAnalysisLease(job, owner);
    m_owners.insert(owner, lease);
    m_leases.append(lease);
    connect(lease, &EchoPluginAnalysisLease::changed, this, [this, owner, lease] {
        if (!lease->active()) {
            m_service.retireScope(owner);
            cleanupOwners();
        }
    });
    return lease;
}
void EchoPluginAudioAnalysis::cleanupOwners()
{
    const auto owners = m_owners.keys();
    for (auto *owner : owners) {
        const auto lease = m_owners.value(owner);
        if ((!lease || !lease->active()) && m_service.scopeDrained(owner)) {
            m_owners.remove(owner);
            owner->deleteLater();
        }
    }
}
void EchoPluginAudioAnalysis::invalidate()
{
    const auto owners = m_owners.keys();
    QPointer<EchoPluginAudioAnalysis> guard(this);
    for (auto *owner : owners) {
        if (const auto lease = m_owners.value(owner))
            lease->retire();
        if (!guard)
            return;
        m_service.retireScope(owner);
        if (!guard)
            return;
    }
    cleanupOwners();
}
void EchoPluginAudioAnalysis::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    invalidate();
}
bool EchoPluginAudioAnalysis::isDrained() const
{
    return std::all_of(m_owners.keyBegin(), m_owners.keyEnd(),
                       [this](QObject *owner) { return m_service.scopeDrained(owner); });
}
} // namespace EchoMusic

#include "EchoPluginAudioAnalysis.moc"
