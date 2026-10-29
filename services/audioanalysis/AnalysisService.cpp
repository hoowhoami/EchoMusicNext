// SPDX-License-Identifier: GPL-3.0-only
#include "AnalysisService.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

namespace EchoMusic::AudioAnalysis
{
namespace
{
QString digest(const QJsonArray &value)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(value).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256)
            .toHex());
}
quint64 revision(const QJsonObject &facts, const char *key)
{
    bool valid = false;
    const auto value = facts.value(key).toString().toULongLong(&valid);
    return valid ? value : 0;
}
Source identity(const QJsonObject &facts, const QString &account, const Input &input)
{
    const auto hash = facts.value("resolvedHash").toString();
    const bool trustedHash =
        QRegularExpression("^(?:[a-fA-F0-9]{32}|[a-fA-F0-9]{64})$").match(hash).hasMatch();
    const bool local = !input.localIdentity.isEmpty();
    Source out;
    out.accountNamespace = account;
    out.trackKey = facts.value("trackKey").toString();
    out.revision = revision(facts, "sourceRevision");
    out.generation = revision(facts, "trackSequence");
    out.persistent = local || trustedHash;
    // Never persist a locator or infer a representation from a queue item/URL.
    // FFmpeg SWR is an explicit decoder profile; bump this version when its contract changes.
    out.representationKey =
        digest({"ffmpeg-swr-v1",
                local ? input.localIdentity : (trustedHash ? hash.toLower() : "current-epoch"),
                facts.value("sourceKind"), facts.value("resolvedQuality"),
                facts.value("appliedEffect"), facts.value("audioStreamOrdinal"),
                out.persistent ? QJsonValue() : facts.value("accountRevision")});
    return out;
}
} // namespace
struct Service::Private
{
    struct Control
    {
        std::shared_ptr<std::atomic_bool> canceled = std::make_shared<std::atomic_bool>(false);
        QPointer<Job> front, upstream;
        bool captureRetried = false;
    };
    struct Scope
    {
        QJsonObject facts;
        QString account, authority;
        quint64 serial;
    };
    Service *q;
    Config config;
    Facts facts;
    Resolve resolve;
    Authorize authorize;
    Capture capture;
    Broker broker;
    QHash<QObject *, Scope> scopes;
    QHash<QObject *, int> pending;
    QHash<QObject *, QList<std::weak_ptr<Control>>> controls;
    QSet<QObject *> observed;
    quint64 serial = 0;
    int captures = 0;
    Private(Service *owner, Config cfg, Facts read, Capture take)
        : q(owner), config(std::move(cfg)), facts(std::move(read)), capture(std::move(take)),
          broker(config, owner)
    {
    }
    QString permission(const QJsonObject &snapshot) const
    {
        return authorize ? authorize(snapshot)
                         : (facts() == snapshot ? QStringLiteral("current") : QString());
    }
    bool active(QObject *scope, quint64 epoch, bool queueIdentity = true)
    {
        auto it = scopes.find(scope);
        if (it == scopes.end() || it->serial != epoch)
            return false;
        const auto now = permission(it->facts);
        if (now.isEmpty() || (it->authority != "current" && it->authority != now))
            return false;
        // A current lease may enter one real outgoing epoch, never a later one
        // or current again after cancellation/seek of that epoch.
        if (it->authority == "current" && now != "current") {
            it->authority = now;
            // Capture/result completion may observe this before sourceChanged.
            // Never re-enter that caller; publish once only while this same
            // owner, serial and exact authority still exist.
            if (queueIdentity)
                QMetaObject::invokeMethod(
                    q,
                    [guard = QPointer<Service>(q), subject = QPointer<QObject>(scope), epoch,
                     authority = now] {
                        if (!guard || !subject)
                            return;
                        const auto entry = guard->d->scopes.constFind(subject.data());
                        if (entry != guard->d->scopes.cend() && entry->serial == epoch &&
                            entry->authority == authority &&
                            guard->d->permission(entry->facts) == authority)
                            emit guard->authorizationChanged(subject.data());
                    },
                    Qt::QueuedConnection);
        }
        return true;
    }
};
Service::Service(Config config, Facts facts, Capture capture, QObject *parent)
    : QObject(parent),
      d(std::make_unique<Private>(this, std::move(config), std::move(facts), std::move(capture)))
{
    connect(&d->broker, &Broker::drainedChanged, this, &Service::drainedChanged);
}
Service::~Service()
{
    for (const auto &items : d->controls)
        for (const auto &weak : items)
            if (const auto state = weak.lock())
                state->canceled->store(true);
    d->broker.shutdown();
}
Job *Service::rejected(QObject *scope, const QString &code, const QString &message)
{
    auto *job = new Job(scope ? scope : this);
    QTimer::singleShot(
        0, job, [job, code, message] { job->finish(Job::State::Failed, {}, code, message); });
    return job;
}
void Service::setSourceAuthority(Resolve resolve, Authorize authorize)
{
    d->resolve = std::move(resolve);
    d->authorize = std::move(authorize);
    sourceChanged();
}
Job *Service::request(QObject *scope, const QString &account, Kind kind, Policy policy,
                      const QJsonObject &selector)
{
    auto *job = new Job(scope ? scope : this);
    const auto facts = d->resolve           ? d->resolve(selector)
                       : selector.isEmpty() ? d->facts()
                                            : QJsonObject{};
    const auto reject = [job](QString code, QString message) {
        QTimer::singleShot(
            0, job, [job, code, message] { job->finish(Job::State::Failed, {}, code, message); });
        return job;
    };
    if (!scope || facts.isEmpty() || account.isEmpty() || account.size() > 4096 ||
        facts.value("trackKey").toString().isEmpty() ||
        facts.value("trackKey").toString().size() > 4096 || !revision(facts, "trackSequence"))
        return reject("Unavailable", "no acknowledged current source is available");
    if (facts.value("preview").toBool())
        return reject("IncompleteSource", "preview audio cannot produce whole-track analysis");
    const auto authority = d->permission(facts);
    if (authority.isEmpty())
        return reject("StaleSource", "source authorization is no longer active");
    if (d->captures >= 2)
        return reject("Busy", "two source captures are already pending");
    if (d->scopes.contains(scope) &&
        (d->scopes.value(scope).facts != facts || d->scopes.value(scope).account != account))
        retireScope(scope);
    if (!d->scopes.contains(scope))
        d->scopes.insert(scope, {facts, account, authority, ++d->serial});
    if (!d->observed.contains(scope)) {
        d->observed.insert(scope);
        connect(scope, &QObject::destroyed, this, [this, scope] {
            retireScope(scope);
            d->observed.remove(scope);
        });
    }
    const auto epoch = d->scopes.value(scope).serial;
    auto state = std::make_shared<Private::Control>();
    state->front = job;
    d->controls[scope].removeIf([](const auto &item) { return item.expired(); });
    d->controls[scope].append(state);
    job->m_cancel = [state] {
        state->canceled->store(true);
        if (state->upstream)
            state->upstream->cancel();
    };
    ++d->captures;
    ++d->pending[scope];
    auto *watcher = new QFutureWatcher<Input>(this);
    connect(watcher, &QFutureWatcher<Input>::finished, this,
            [this, watcher, scope, epoch, state, facts, account, kind, policy] {
                const auto input = watcher->result();
                // Capture may have started while A was current and run only after
                // the native boundary adopted B. Retry once using the now explicit,
                // exact outgoing epoch; never reopen an arbitrary retired source.
                const auto authority = d->permission(facts);
                if (!state->captureRetried && input.code == "StaleSource" &&
                    !state->canceled->load() && d->active(scope, epoch) &&
                    authority.startsWith("epoch:")) {
                    state->captureRetried = true;
                    auto outgoingFacts = facts;
                    outgoingFacts.insert("analysisTransitionEpoch", authority.mid(6));
                    const auto capture = d->capture;
                    watcher->setFuture(QtConcurrent::run([capture, outgoingFacts, kind, state] {
                        if (state->canceled->load())
                            return Input{};
                        try {
                            return capture(outgoingFacts, kind);
                        } catch (...) {
                            Input out;
                            out.code = "DecodeError";
                            out.error = "native source capture failed";
                            return out;
                        }
                    }));
                    return;
                }
                watcher->deleteLater();
                --d->captures;
                if (--d->pending[scope] == 0)
                    d->pending.remove(scope);
                QPointer<Service> guard(this);
                if (!state->front || state->canceled->load() || !d->active(scope, epoch) ||
                    d->permission(facts).isEmpty()) {
                    if (state->front)
                        state->front->finish(Job::State::Canceled, {}, "Canceled",
                                             "source/scope retired", false);
                } else if (!input.error.isEmpty()) {
                    state->front->finish(Job::State::Failed, {},
                                         input.code.isEmpty() ? QStringLiteral("Unavailable")
                                                              : input.code,
                                         input.error);
                } else {
                    const auto source = identity(facts, account, input);
                    d->broker.setSource(scope, source);
                    state->upstream = d->broker.request(scope, source, kind, policy, input.reader);
                    auto *upstream = state->upstream.data();
                    const auto synchronize = [guard, state, scope, epoch] {
                        if (!guard || !state->front || !state->upstream ||
                            state->canceled->load() || !guard->d->active(scope, epoch))
                            return;
                        const auto upstream = state->upstream;
                        if (upstream->finished())
                            state->front->finish(upstream->state(), upstream->result(),
                                                 upstream->errorCode(), upstream->errorMessage());
                        else {
                            state->front->m_state = upstream->state();
                            state->front->m_result = upstream->result();
                            emit state->front->changed();
                        }
                    };
                    connect(upstream, &Job::changed, state->front, synchronize);
                    connect(upstream, &Job::progress, state->front,
                            [guard, state, scope, epoch](int done, int total, double until) {
                                if (guard && guard->d->active(scope, epoch) && state->front &&
                                    !state->front->finished())
                                    emit state->front->progress(done, total, until);
                            });
                    synchronize();
                }
                if (guard)
                    emit drainedChanged();
            });
    const auto capture = d->capture;
    auto captureFacts = facts;
    if (authority.startsWith("epoch:"))
        captureFacts.insert("analysisTransitionEpoch", authority.mid(6));
    watcher->setFuture(QtConcurrent::run([capture, captureFacts, kind, state] {
        if (state->canceled->load())
            return Input{};
        try {
            return capture(captureFacts, kind);
        } catch (...) {
            Input out;
            out.code = "DecodeError";
            out.error = "native source capture failed";
            return out;
        }
    }));
    return job;
}
void Service::retireScope(QObject *scope)
{
    const bool hadAuthority = d->scopes.remove(scope) > 0;
    const auto items = d->controls.take(scope);
    QPointer<Service> guard(this);
    if (hadAuthority)
        emit authorizationRevoked(scope);
    if (!guard)
        return;
    for (const auto &weak : items)
        if (const auto state = weak.lock()) {
            state->canceled->store(true);
            if (state->front)
                state->front->finish(Job::State::Canceled, {}, "Canceled", "analysis scope retired",
                                     false);
            if (state->upstream)
                state->upstream->cancel();
        }
    d->broker.retireScope(scope);
}
bool Service::scopeDrained(QObject *scope) const
{
    return d->pending.value(scope) == 0 && d->broker.scopeDrained(scope);
}
QJsonObject Service::sourceIdentity(QObject *scope) const
{
    const auto it = d->scopes.constFind(scope);
    if (it == d->scopes.cend() || it->authority != d->permission(it->facts))
        return {};
    const auto sequence = it->facts.value("trackSequence").toString();
    const auto sequenceNumber = revision(it->facts, "trackSequence");
    if (!sequenceNumber || sequence != QString::number(sequenceNumber))
        return {};
    QString epoch;
    if (it->authority != "current") {
        if (!it->authority.startsWith("epoch:"))
            return {};
        epoch = it->authority.mid(6);
        bool ok = false;
        const auto number = epoch.toULongLong(&ok);
        if (!ok || !number || epoch != QString::number(number))
            return {};
    }
    return {{"sourceSequence", sequence}, {"transitionEpoch", epoch}};
}
void Service::sourceChanged()
{
    const auto scopes = d->scopes.keys();
    QPointer<Service> guard(this);
    for (auto *scope : scopes) {
        const auto entry = d->scopes.constFind(scope);
        if (entry == d->scopes.cend())
            continue;
        const auto authority = entry->authority;
        const auto serial = entry->serial;
        if (!d->active(scope, serial, false))
            retireScope(scope);
        else if (authority != d->scopes.value(scope).authority)
            emit authorizationChanged(scope);
        if (!guard)
            return;
    }
}
QJsonObject Service::status() const
{
    return {{"backend", "native-webgpu"},
            {"gpuVerified", false},
            {"cpuAvailable", false},
            {"helperInstalled", QFileInfo(d->config.helper).isExecutable()},
            {"beatsModelInstalled",
             QFileInfo(d->config.modelDirectory + "/beat_this-wgsl.bin").size() == 41102271},
            {"drumModelInstalled",
             QFileInfo(d->config.modelDirectory + "/htdemucs-wgsl.bin").size() == 91838176}};
}
} // namespace EchoMusic::AudioAnalysis
