// SPDX-License-Identifier: GPL-3.0-only
#include "AnalysisBroker.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QSet>
#include <QTimer>
#include <QSaveFile>
#include <QtEndian>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace EchoMusic::AudioAnalysis
{
namespace
{
QString kindName(Kind kind)
{
    return kind == Kind::Beats ? "beats" : "drumHits";
}
QString digest(const QByteArray &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}
bool finiteList(const QJsonValue &value, int maximum, double low, double high)
{
    if (!value.isArray() || value.toArray().size() > maximum)
        return false;
    for (const auto entry : value.toArray()) {
        const double number = entry.toDouble(std::nan(""));
        if (!std::isfinite(number) || number < low || number > high)
            return false;
    }
    return true;
}
bool validResult(const QJsonObject &value, Kind kind, bool partial = false)
{
    const auto seconds = value.value("durationSeconds").toDouble(-1);
    if (!value.value("complete").isBool() || value.value("complete").toBool() == partial ||
        !std::isfinite(seconds) || seconds <= 0 || seconds > 600 || value.value("version") != 1 ||
        value.value("source") != (kind == Kind::Beats ? "beat_this" : "htdemucs") ||
        (value.value("backend") != "gpu" && value.value("backend") != "cpu"))
        return false;
    const auto ordered = [](const QJsonArray &times) {
        double previous = -1;
        for (const auto time : times) {
            const double current = time.toDouble(-1);
            if (current <= previous)
                return false;
            previous = current;
        }
        return true;
    };
    if (kind == Kind::Beats) {
        if (partial)
            return false;
        if (!finiteList(value.value("beats"), 30001, 0, seconds + .021) ||
            !finiteList(value.value("downbeats"), 30001, 0, seconds + .021) ||
            !ordered(value.value("beats").toArray()) ||
            !ordered(value.value("downbeats").toArray()))
            return false;
        const auto beats = value.value("beats").toArray();
        for (const auto time : value.value("downbeats").toArray())
            if (!beats.contains(time))
                return false;
        const auto accents = value.value("accents");
        if (!accents.isNull() && !accents.isUndefined()) {
            if (!accents.isObject())
                return false;
            const auto object = accents.toObject();
            const auto count = value.value("beats").toArray().size();
            for (const auto &key : {"impact", "low", "mid", "high", "rhythmic", "pulse"})
                if (!finiteList(object.value(key), int(count), 0, 1) ||
                    object.value(key).toArray().size() != count)
                    return false;
            const double scale = object.value("trackScale").toDouble(-1);
            if (!std::isfinite(scale) || scale < 0 || scale > 1)
                return false;
        }
        return true;
    }
    const auto hits = value.value("hits").toObject();
    const auto count = hits.value("times").toArray().size();
    const auto coverage = value.value("coverage").toDouble(-1);
    for (const auto entry : hits.value("kinds").toArray())
        if (entry.toDouble(-1) != entry.toInt(-2))
            return false;
    // Different bands may produce simultaneous hits, so drum times are non-decreasing.
    double previous = -1;
    for (const auto entry : hits.value("times").toArray()) {
        if (entry.toDouble(-2) < previous)
            return false;
        previous = entry.toDouble(-2);
    }
    const auto until = hits.value("until").toDouble(-1);
    const bool energies =
        partial ? std::isfinite(until) && until > 0 && until <= seconds && previous < until
                : finiteList(value.value("drumEnergy"), 60001, 0, 1e9) &&
                      finiteList(value.value("mixEnergy"), 60001, 0, 1e9) &&
                      !value.value("drumEnergy").toArray().isEmpty() &&
                      value.value("drumEnergy").toArray().size() ==
                          value.value("mixEnergy").toArray().size();
    return std::isfinite(coverage) && coverage >= 0 && coverage <= 1 &&
           value.value("drumless").isBool() &&
           value.value("drumless").toBool() == (coverage < .08) &&
           finiteList(hits.value("times"), 30001, 0, seconds + .021) &&
           finiteList(hits.value("strengths"), 30001, 0, 1) &&
           hits.value("strengths").toArray().size() == count &&
           finiteList(hits.value("kinds"), 30001, 0, 2) &&
           hits.value("kinds").toArray().size() == count && energies;
}
bool replaceAtomic(const QString &from, const QString &to)
{
#if defined(Q_OS_WIN)
    return MoveFileExW(reinterpret_cast<LPCWSTR>(from.utf16()),
                       reinterpret_cast<LPCWSTR>(to.utf16()),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    return ::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
#endif
}
bool reserveCache(const QString &directory, const QString &target, qint64 nextBytes)
{
    // Only this service's fixed SHA256 filenames are eviction candidates.
    QList<QFileInfo> entries;
    qint64 used = 0;
    int retained = 0;
    QDirIterator iterator(directory, {"*.json"}, QDir::Files | QDir::NoSymLinks);
    while (iterator.hasNext()) {
        iterator.next();
        const auto info = iterator.fileInfo();
        const auto name = info.fileName();
        if (name.size() != 69)
            continue;
        bool valid = true;
        for (int index = 0; index < 64; ++index)
            if (!((name[index] >= '0' && name[index] <= '9') ||
                  (name[index] >= 'a' && name[index] <= 'f')))
                valid = false;
        if (!valid || info.absoluteFilePath() == target)
            continue;
        if (entries.size() >= 512)
            return false;
        entries.append(info);
        used += info.size();
        ++retained;
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto &a, const auto &b) { return a.lastModified() < b.lastModified(); });
    for (const auto &entry : entries) {
        if (retained < 128 && used + nextBytes <= 64 * 1024 * 1024)
            break;
        if (!QFile::remove(entry.absoluteFilePath()))
            return false;
        used -= entry.size();
        --retained;
    }
    return retained < 128 && used + nextBytes <= 64 * 1024 * 1024;
}
constexpr qsizetype ResumeLimit = 2 * 1024 * 1024 + 140;
struct Resume
{
    QByteArray bytes;
    QJsonObject partial;
};
struct CollectedResume
{
    QByteArray bytes;
    QString warning;
    bool partialMatches = false;
};
// Generic private envelope validation. The AGPL signal codec remains in the helper process.
QByteArray checkedResume(const QString &name, const QString &key, QString *error)
{
    const QFileInfo info(name);
    if (!info.exists() && !info.isSymLink())
        return {};
    QFile file(name);
    if (info.isSymLink() || !info.isFile() || !file.open(QIODevice::ReadOnly) ||
        file.size() < 204 || file.size() > ResumeLimit) {
        *error = "checkpoint file type/byte budget";
        return {};
    }
    const auto bytes = file.read(ResumeLimit + 1);
    if (bytes.size() < 204 || bytes.size() > ResumeLimit || bytes.first(8) != "AUDIORS1" ||
        bytes.mid(8, 32) != QCryptographicHash::hash(key.toLatin1(), QCryptographicHash::Sha256) ||
        qFromBigEndian<quint32>(bytes.constData() + 136) != quint32(bytes.size() - 140) ||
        bytes.mid(104, 32) != QCryptographicHash::hash(QByteArrayView(bytes).sliced(140),
                                                       QCryptographicHash::Sha256)) {
        *error = "checkpoint envelope identity/length/SHA256";
        return {};
    }
    return bytes;
}
bool writeResume(const QString &name, const QByteArray &bytes)
{
    QSaveFile file(name);
    file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) &&
           file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) &&
           file.write(bytes) == bytes.size() && file.commit();
}
std::shared_ptr<QTemporaryDir> privateDirectory(const QString &pattern,
                                                const std::shared_ptr<std::atomic_int> &quota,
                                                bool drum, bool *allowed)
{
    bool reserved = false;
    if (drum) {
        reserved = quota->fetch_add(1) < 2;
        if (!reserved) {
            --*quota;
            qWarning("Audio analysis resume staging quota busy; optimization disabled");
        }
    }
    *allowed = reserved;
    // Reserve two bounded checkpoint files until their directory has actually been removed.
    // An asynchronous cleanup backlog cannot exceed the disk budget or block valid analysis.
    return {new QTemporaryDir(pattern), [quota, reserved](QTemporaryDir *directory) {
                (void)QtConcurrent::run([directory, quota, reserved] {
                    delete directory;
                    if (reserved)
                        --*quota;
                });
            }};
}
struct Prepared
{
    QString code, error, left, right;
    QJsonObject cached;
    bool checkpointAllowed = false;
    std::shared_ptr<QTemporaryDir> directory;
};
} // namespace
Job::Job(QObject *parent) : QObject(parent) {}
Job::~Job()
{
    auto callback = std::move(m_cancel);
    if (callback)
        callback();
}
void Job::finish(State state, QJsonObject result, QString code, QString error, bool notify)
{
    if (finished())
        return;
    m_state = state;
    m_result = std::move(result);
    m_code = std::move(code);
    m_error = std::move(error);
    m_cancel = {};
    if (!notify)
        return;
    QPointer<Job> guard(this);
    emit changed();
    if (guard)
        emit completed();
}
void Job::cancel()
{
    if (finished())
        return;
    auto callback = std::move(m_cancel);
    QPointer<Job> guard(this);
    finish(State::Canceled, {}, "Canceled", "analysis canceled");
    if (callback)
        callback();
}
struct Broker::Private
{
    struct Lease
    {
        QPointer<Job> job;
        QObject *scope = nullptr;
        Source source;
    };
    struct Task
    {
        QString key, id, cachePath;
        Source source;
        Kind kind;
        Policy policy;
        Reader reader;
        std::shared_ptr<std::atomic_bool> canceled = std::make_shared<std::atomic_bool>(false);
        std::vector<Lease> leases;
        Prepared prepared;
        QPointer<QProcess> process;
        QByteArray output, diagnostics;
        QJsonObject result, partial, remembered;
        QString failureCode, failureMessage;
        bool ready = false, done = false, publishing = false;
        bool resumeStaged = false, resumeInput = false, resumeRetried = false;
        bool stagingResume = false, collectingResume = false;
        QByteArray checkpointSha;
        QJsonObject checkpointPartial;
        QElapsedTimer runtime;
    };
    Broker *q;
    Config config;
    QString fingerprint;
    QHash<QObject *, Source> scopes;
    QSet<QObject *> observedScopes;
    QHash<QString, std::shared_ptr<Task>> tasks;
    QHash<QString, QJsonObject> ephemeral;
    QList<QString> order;
    QHash<QString, Resume> resumes;
    QList<QString> resumeOrder;
    std::shared_ptr<std::atomic_int> checkpointSlots = std::make_shared<std::atomic_int>(0);
    bool stopped = false;
    quint64 serial = 0;
    explicit Private(Broker *owner, Config value) : q(owner), config(std::move(value))
    {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        for (const auto &name : {config.helper, config.runtimeDirectory + "/graph-runtime.js",
                                 config.runtimeDirectory + "/signal-runtime.js"}) {
            QFile file(name);
            if (file.open(QIODevice::ReadOnly))
                hash.addData(&file);
        }
        fingerprint = QString::fromLatin1(hash.result().toHex());
    }
    bool active(const Lease &lease) const
    {
        return lease.job && !lease.job->finished() && scopes.contains(lease.scope) &&
               scopes.value(lease.scope) == lease.source;
    }
    bool wanted(const std::shared_ptr<Task> &task) const
    {
        if (stopped)
            return false;
        for (const auto &lease : task->leases)
            if (active(lease))
                return true;
        return false;
    }
    QString key(const Source &source, Kind kind) const
    {
        return digest(
            QJsonDocument(QJsonArray{source.accountNamespace, source.trackKey,
                                     source.representationKey, kindName(kind), fingerprint,
                                     source.persistent ? QString()
                                                       : QString::number(source.revision) + ':' +
                                                             QString::number(source.generation)})
                .toJson(QJsonDocument::Compact));
    }
    void end(const std::shared_ptr<Task> &task, QString code = {}, QString error = {},
             QJsonObject result = {})
    {
        if (task->done)
            return;
        task->done = true;
        if (code.isEmpty() && !result.isEmpty()) {
            resumes.remove(task->key);
            resumeOrder.removeAll(task->key);
        }
        const auto leases = task->leases;
        const bool deliver = !stopped;
        if (code.isEmpty() && !result.isEmpty() && !task->source.persistent && wanted(task)) {
            if (ephemeral.size() >= 2 && !ephemeral.contains(task->key))
                ephemeral.erase(ephemeral.begin());
            ephemeral.insert(task->key, result);
        }
        tasks.remove(task->id);
        order.removeAll(task->id);
        if (task->process) {
            task->process->disconnect(q);
            task->process->deleteLater();
            task->process = nullptr;
        }
        QPointer<Broker> guard(q);
        for (const auto &lease : leases) {
            if (!guard)
                return;
            if (deliver && active(lease))
                lease.job->finish(code.isEmpty() ? Job::State::Succeeded : Job::State::Failed,
                                  result, code, error);
        }
        if (!guard)
            return;
        emit q->drainedChanged();
        if (guard)
            startNext();
    }
    void cancelLease(const std::shared_ptr<Task> &task, Job *job, bool silent)
    {
        for (const auto &lease : task->leases)
            if (lease.job == job && lease.job && !lease.job->finished())
                lease.job->finish(Job::State::Canceled, {}, "Canceled", "analysis retired",
                                  !silent);
        if (wanted(task))
            return;
        task->canceled->store(true);
        if (task->process)
            task->process->kill();
        else if (task->ready)
            end(task, "Canceled", "analysis retired");
    }
    void prepare(const std::shared_ptr<Task> &task)
    {
        const auto cfg = config;
        const auto quota = checkpointSlots;
        auto *watcher = new QFutureWatcher<Prepared>(q);
        QObject::connect(watcher, &QFutureWatcher<Prepared>::finished, q, [this, watcher, task] {
            auto prepared = watcher->result();
            watcher->deleteLater();
            task->prepared = std::move(prepared);
            if (!wanted(task)) {
                end(task, "Canceled", "analysis retired");
                return;
            }
            if (!task->prepared.code.isEmpty()) {
                end(task, task->prepared.code, task->prepared.error);
                return;
            }
            if (!task->prepared.cached.isEmpty()) {
                end(task, {}, {}, task->prepared.cached);
                return;
            }
            task->ready = true;
            startNext();
        });
        watcher->setFuture(QtConcurrent::run([task, cfg, quota] {
            Prepared out;
            const auto reject = [&out](QString code, QString error) {
                out.code = std::move(code);
                out.error = std::move(error);
                return out;
            };
            QFile cache(task->cachePath);
            if (!task->remembered.isEmpty()) {
                out.cached = task->remembered;
                return out;
            }
            if (task->source.persistent && cache.open(QIODevice::ReadOnly) &&
                cache.size() <= 8 * 1024 * 1024 + 65536) {
                const auto entry = QJsonDocument::fromJson(cache.readAll()).object();
                const auto result = entry.value("result").toObject();
                if (entry.value("key") == task->key && validResult(result, task->kind)) {
                    out.cached = result;
                    return out;
                }
            }
            if (task->policy == Policy::StoredOnly)
                return reject("CacheMiss",
                              "no complete analysis is cached for this source/profile");
            if (task->policy == Policy::CpuOffline || !QFileInfo(cfg.helper).isExecutable())
                return reject("Unavailable", "requested native analysis backend is unavailable");
            if (task->canceled->load())
                return reject("Canceled", "analysis retired");
            if (!task->reader)
                return reject("Unavailable", "authorized whole-track PCM reader unavailable");
            // Validate the selected immutable model and a real hardware adapter before any
            // whole-track read. This process is owned by the background preparation job.
            QProcess probe;
            probe.setProgram(cfg.helper);
            probe.setArguments({"--probe", "--model",
                                task->kind == Kind::Beats ? "beat_this" : "htdemucs", "--models",
                                cfg.modelDirectory, "--runtime", cfg.runtimeDirectory});
            probe.start();
            if (!probe.waitForStarted(5000))
                return reject("Unavailable", "native GPU probe failed to start");
            QElapsedTimer deadline;
            deadline.start();
            QByteArray probeOutput, probeError;
            QString probeCode, probeMessage;
            while (true) {
                probe.waitForFinished(20);
                probeOutput += probe.readAllStandardOutput();
                probeError += probe.readAllStandardError();
                if (task->canceled->load()) {
                    probeCode = "Canceled";
                    probeMessage = "analysis retired";
                } else if (deadline.elapsed() > 30000) {
                    probeCode = "Timeout";
                    probeMessage = "native GPU probe absolute deadline";
                } else if (probeOutput.size() > 65536 || probeError.size() > 65536) {
                    probeCode = "QuotaExceeded";
                    probeMessage = "native GPU probe protocol budget";
                }
                if (!probeCode.isEmpty()) {
                    probe.kill();
                    probe.waitForFinished(1000);
                    return reject(probeCode, probeMessage);
                }
                if (probe.state() == QProcess::NotRunning)
                    break;
            }
            const auto capability = QJsonDocument::fromJson(probeOutput).object();
            if (probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0 ||
                !capability.value("available").toBool() ||
                capability.value("cpuFallback").toBool(true)) {
                const auto message =
                    capability.value("error").toString("native hardware GPU unavailable");
                const auto colon = message.indexOf(':');
                const auto code = colon > 0 ? message.left(colon) : QStringLiteral("Unavailable");
                return reject(code, message);
            }
            Pcm pcm;
            try {
                pcm = task->reader(task->canceled);
            } catch (const std::exception &error) {
                return reject("DecodeError", QString::fromUtf8(error.what()));
            } catch (...) {
                return reject("DecodeError", "PCM reader failed");
            }
            if (task->canceled->load())
                return reject("Canceled", "analysis retired");
            if (!pcm.error.isEmpty())
                return reject(pcm.code.isEmpty() ? QStringLiteral("DecodeError") : pcm.code,
                              pcm.error);
            const int rate = task->kind == Kind::Beats ? 22050 : 44100;
            if (!pcm.wholeTrack)
                return reject("IncompleteSource",
                              "partial/preview audio cannot produce whole-track cache");
            if (pcm.sampleRate != rate || pcm.left.size() < 4096 || pcm.left.size() % 4 ||
                pcm.left.size() > rate * 600 * 4 ||
                (task->kind == Kind::DrumHits && pcm.right.size() != pcm.left.size()))
                return reject("InvalidPcm",
                              "PCM rate/channel/duration budget differs from the selected profile");
            QDir().mkpath(cfg.scratchDirectory);
            out.directory = privateDirectory(cfg.scratchDirectory + "/job-XXXXXX", quota,
                                             task->kind == Kind::DrumHits, &out.checkpointAllowed);
            if (!out.directory->isValid())
                return reject("IoError", "private analysis staging unavailable");
            out.left = out.directory->filePath("left.f32");
            out.right = out.directory->filePath("right.f32");
            const auto write = [](const QString &name, const QByteArray &bytes) {
                QFile file(name);
                return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                       file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) &&
                       file.write(bytes) == bytes.size();
            };
            if (!write(out.left, pcm.left) ||
                (task->kind == Kind::DrumHits && !write(out.right, pcm.right)))
                return reject("IoError", "PCM staging write failed");
            return out;
        }));
    }
    void publish(const std::shared_ptr<Task> &task)
    {
        if (task->publishing)
            return;
        task->publishing = true;
        if (!task->source.persistent) {
            end(task, {}, {}, task->result);
            return;
        }
        if (!wanted(task)) {
            end(task, "Canceled", "analysis retired");
            return;
        }
        const auto json = QJsonDocument(QJsonObject{{"key", task->key}, {"result", task->result}})
                              .toJson(QJsonDocument::Compact);
        const auto temporary = task->prepared.directory->filePath("cache.json");
        auto *watcher = new QFutureWatcher<bool>(q);
        QObject::connect(watcher, &QFutureWatcher<bool>::finished, q,
                         [this, watcher, task, temporary] {
                             const bool saved = watcher->result();
                             watcher->deleteLater();
                             if (!wanted(task)) {
                                 end(task, "Canceled", "analysis retired");
                                 return;
                             }
                             if (!saved || !QDir().mkpath(config.cacheDirectory) ||
                                 !reserveCache(config.cacheDirectory,
                                               QFileInfo(task->cachePath).absoluteFilePath(),
                                               QFileInfo(temporary).size()) ||
                                 !replaceAtomic(temporary, task->cachePath)) {
                                 end(task, "IoError", "analysis cache quota/publication failed");
                                 return;
                             }
                             end(task, {}, {}, task->result);
                         });
        watcher->setFuture(QtConcurrent::run([json, temporary] {
            QFile file(temporary);
            if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
                !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
                file.write(json) != json.size() || !file.flush())
                return false;
#if !defined(Q_OS_WIN)
            if (::fsync(file.handle()) != 0)
                return false;
#endif
            return true;
        }));
    }
    void rememberResume(const std::shared_ptr<Task> &task, QString code, QString message)
    {
        if (task->kind != Kind::DrumHits || !task->prepared.directory ||
            !task->prepared.checkpointAllowed || stopped) {
            end(task, std::move(code), std::move(message));
            return;
        }
        task->collectingResume = true;
        auto *watcher = new QFutureWatcher<CollectedResume>(q);
        QObject::connect(
            watcher, &QFutureWatcherBase::finished, q, [this, watcher, task, code, message] {
                const auto saved = watcher->result();
                watcher->deleteLater();
                task->collectingResume = false;
                if (!saved.warning.isEmpty())
                    qWarning().noquote() << "Audio analysis resume ignored:" << saved.warning;
                if (!stopped && !task->done && !saved.bytes.isEmpty()) {
                    // Four entries, each <=2MiB+140B plus <=1MiB partial: bounded to 12MiB+560B.
                    auto partial = task->checkpointPartial;
                    if (!saved.partialMatches)
                        partial = {};
                    resumes.insert(task->key, {saved.bytes, partial});
                    resumeOrder.removeAll(task->key);
                    resumeOrder.append(task->key);
                    while (resumeOrder.size() > 4)
                        resumes.remove(resumeOrder.takeFirst());
                }
                end(task, code, message);
            });
        const auto checkpointSha = task->checkpointSha;
        watcher->setFuture(QtConcurrent::run([task, checkpointSha] {
            CollectedResume out;
            out.bytes = checkedResume(task->prepared.directory->filePath("drum-resume-output.bin"),
                                      task->key, &out.warning);
            out.partialMatches =
                !out.bytes.isEmpty() && digest(out.bytes).toLatin1() == checkpointSha;
            return out;
        }));
    }
    bool stageResume(const std::shared_ptr<Task> &task)
    {
        if (task->kind != Kind::DrumHits || task->resumeStaged || !task->prepared.checkpointAllowed)
            return false;
        task->resumeStaged = true;
        const auto found = resumes.constFind(task->key);
        if (found == resumes.cend())
            return false;
        const auto bytes = found->bytes;
        task->stagingResume = true;
        auto *watcher = new QFutureWatcher<bool>(q);
        QObject::connect(watcher, &QFutureWatcherBase::finished, q, [this, watcher, task] {
            const bool saved = watcher->result();
            watcher->deleteLater();
            task->stagingResume = false;
            task->resumeInput = saved;
            if (!saved) {
                resumes.remove(task->key);
                resumeOrder.removeAll(task->key);
                qWarning("Audio analysis resume ignored: private staging write failed");
            }
            if (!wanted(task))
                end(task, "Canceled", "analysis retired");
            else
                startNext();
        });
        watcher->setFuture(QtConcurrent::run([task, bytes] {
            return writeResume(task->prepared.directory->filePath("drum-resume-input.bin"), bytes);
        }));
        return true;
    }
    bool retryWithoutResume(const std::shared_ptr<Task> &task, const QString &message)
    {
        if (!task->resumeInput || task->resumeRetried || !message.startsWith("ResumeInvalid:") ||
            !wanted(task))
            return false;
        task->resumeRetried = true;
        task->resumeInput = false;
        resumes.remove(task->key);
        resumeOrder.removeAll(task->key);
        qWarning().noquote() << "Audio analysis resume discarded; restarting real analysis:"
                             << message;
        if (task->process) {
            for (auto *timer : task->process->findChildren<QTimer *>())
                timer->stop();
            task->process->disconnect(q);
            task->process->deleteLater();
            task->process = nullptr;
        }
        task->output.clear();
        task->result = {};
        task->partial = {};
        task->checkpointPartial = {};
        task->checkpointSha.clear();
        task->stagingResume = true;
        auto *watcher = new QFutureWatcher<bool>(q);
        QObject::connect(watcher, &QFutureWatcherBase::finished, q, [this, watcher, task] {
            const bool removed = watcher->result();
            watcher->deleteLater();
            task->stagingResume = false;
            if (!wanted(task)) {
                end(task, "Canceled", "analysis retired");
                return;
            }
            if (!removed) {
                end(task, "IoError", "invalid private checkpoint could not be removed");
                return;
            }
            startNext();
        });
        watcher->setFuture(QtConcurrent::run([task] {
            const auto input = task->prepared.directory->filePath("drum-resume-input.bin");
            const auto output = task->prepared.directory->filePath("drum-resume-output.bin");
            const bool removed = !QFileInfo::exists(input) || QFile::remove(input);
            if (QFileInfo::exists(output))
                QFile::remove(output);
            return removed;
        }));
        return true;
    }
    void consume(const std::shared_ptr<Task> &task)
    {
        if (!task->process || task->done)
            return;
        task->diagnostics += task->process->readAllStandardError();
        if (task->diagnostics.size() > 65536)
            task->diagnostics = task->diagnostics.last(65536);
        task->output += task->process->readAllStandardOutput();
        if (task->output.size() > 8 * 1024 * 1024 + 65536) {
            task->failureCode = "QuotaExceeded";
            task->failureMessage = "analysis protocol byte budget";
            task->canceled->store(true);
            task->process->kill();
            return;
        }
        while (true) {
            const auto index = task->output.indexOf('\n');
            if (index < 0)
                break;
            const auto data = task->output.first(index);
            task->output.remove(0, index + 1);
            const auto object = QJsonDocument::fromJson(data).object();
            if (object.value("type") == "result")
                task->result = object;
            if (object.value("type") == "checkpoint" && task->kind == Kind::DrumHits) {
                const auto partial = object.value("result").toObject();
                const auto bytes = object.value("bytes").toDouble(-1);
                const auto sha = object.value("sha256").toString().toLatin1();
                if (bytes >= 204 && bytes <= ResumeLimit && std::floor(bytes) == bytes &&
                    sha.size() == 64 && validResult(partial, Kind::DrumHits, true) &&
                    data.size() <= 1024 * 1024 + 512) {
                    task->checkpointPartial = partial;
                    task->checkpointSha = sha;
                } else {
                    qWarning("Audio analysis checkpoint ignored: bounded partial contract");
                }
            }
            if (object.value("type") == "partial") {
                const auto result = object.value("result").toObject();
                if (!validResult(result, task->kind, true)) {
                    task->failureCode = "InvalidResult";
                    task->failureMessage = "partial analysis contract rejected";
                    task->canceled->store(true);
                    task->process->kill();
                    return;
                }
                task->partial = result;
                const auto leases = task->leases;
                QPointer<Broker> guard(q);
                for (const auto &lease : leases) {
                    if (!guard)
                        return;
                    if (active(lease)) {
                        lease.job->m_result = result;
                        emit lease.job->changed();
                    }
                }
            }
            // Progress is consumed by the owner only while its exact source generation is active.
            if (object.value("type") == "progress") {
                const int done = object.value("done").toInt(-1),
                          total = object.value("total").toInt(-1);
                const double until = object.value("until").toDouble(-1);
                if (done < 0 || total < done || total > 1000 || !std::isfinite(until) ||
                    until < 0 || until > 600)
                    continue;
                const auto leases = task->leases;
                QPointer<Broker> guard(q);
                for (const auto &lease : leases) {
                    if (!guard)
                        return;
                    if (active(lease))
                        emit lease.job->progress(done, total, until);
                }
            }
        }
    }
    void startNext()
    {
        if (stopped)
            return;
        for (const auto &task : tasks)
            if (task->collectingResume || task->stagingResume ||
                (task->process && task->process->state() != QProcess::NotRunning))
                return;
        for (const auto &key : order) {
            const auto task = tasks.value(key);
            if (!task || !task->ready || task->process || task->done || task->publishing)
                continue;
            if (!wanted(task)) {
                end(task, "Canceled", "analysis retired");
                return;
            }
            if (stageResume(task))
                return;
            auto *process = new QProcess(q);
            task->process = process;
            QStringList args{
                "--kind",    kindName(task->kind),    "--models", config.modelDirectory,
                "--runtime", config.runtimeDirectory, "--input",  task->prepared.left};
            if (task->kind == Kind::DrumHits) {
                args << "--right" << task->prepared.right;
                if (task->prepared.checkpointAllowed)
                    args << "--checkpoint-key" << task->key;
            }
            QObject::connect(process, &QProcess::readyReadStandardOutput, q,
                             [this, task] { consume(task); });
            QObject::connect(process, &QProcess::readyReadStandardError, q,
                             [this, task] { consume(task); });
            QObject::connect(
                process, &QProcess::finished, q,
                [this, task](int code, QProcess::ExitStatus status) {
                    QPointer<Broker> guard(q);
                    consume(task);
                    if (!guard)
                        return;
                    if (!wanted(task)) {
                        rememberResume(task, "Canceled", "analysis retired");
                        return;
                    }
                    if (!task->failureCode.isEmpty()) {
                        end(task, task->failureCode, task->failureMessage);
                        return;
                    }
                    if (task->canceled->load() || code || status != QProcess::NormalExit ||
                        !task->result.value("ok").toBool()) {
                        const auto message =
                            task->result.value("error").toString("native analysis process failed");
                        if (retryWithoutResume(task, message))
                            return;
                        auto failure = message.section(':', 0, 0);
                        if (!QStringList{"Unavailable", "ModelUnavailable", "ModelInvalid",
                                         "RuntimeUnavailable", "Timeout", "QuotaExceeded",
                                         "InvalidArgument", "IoError"}
                                 .contains(failure))
                            failure = "AnalysisError";
                        rememberResume(task, failure, message);
                        return;
                    }
                    const auto result = task->result.value("result").toObject();
                    if (!validResult(result, task->kind)) {
                        end(task, "InvalidResult", "native analysis result contract rejected");
                        return;
                    }
                    task->result = result;
                    publish(task);
                });
            QObject::connect(
                process, &QProcess::errorOccurred, q, [this, task](QProcess::ProcessError error) {
                    if (error == QProcess::FailedToStart)
                        end(task, "Unavailable", "native analysis helper failed to start");
                });
            auto *deadline = new QTimer(process);
            deadline->setSingleShot(true);
            if (!task->runtime.isValid())
                task->runtime.start();
            deadline->setInterval(
                int(std::max<qint64>(1, 5 * 60 * 1000 - task->runtime.elapsed())));
            QObject::connect(deadline, &QTimer::timeout, process, [task] {
                task->failureCode = "Timeout";
                task->failureMessage = "analysis absolute deadline exceeded";
                task->canceled->store(true);
                if (task->process)
                    task->process->kill();
            });
            deadline->start();
            process->setProgram(config.helper);
            process->setArguments(args);
            process->start();
            const auto leases = task->leases;
            QPointer<Broker> guard(q);
            for (const auto &lease : leases) {
                if (!guard)
                    return;
                if (active(lease)) {
                    lease.job->m_state = Job::State::Running;
                    emit lease.job->changed();
                }
            }
            return;
        }
    }
};
Broker::Broker(Config config, QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(this, std::move(config)))
{
}
Broker::~Broker()
{
    d->stopped = true;
    for (const auto &task : d->tasks) {
        task->canceled->store(true);
        for (const auto &lease : task->leases)
            if (lease.job)
                lease.job->finish(Job::State::Canceled, {}, "Canceled", "analysis broker retired",
                                  false);
        if (task->process) {
            auto *process = task->process.data();
            process->disconnect(this);
            process->setParent(QCoreApplication::instance());
            QObject::connect(process, &QProcess::finished, process, &QObject::deleteLater);
            if (process->state() == QProcess::NotRunning)
                process->deleteLater();
            else
                process->kill();
        }
    }
}
void Broker::setSource(QObject *scope, Source source)
{
    if (!scope || d->stopped)
        return;
    if (d->scopes.contains(scope) && d->scopes.value(scope) == source)
        return;
    const bool known = d->observedScopes.contains(scope);
    QPointer<Broker> guard(this);
    retireScope(scope);
    if (!guard)
        return;
    d->scopes.insert(scope, std::move(source));
    if (!known) {
        d->observedScopes.insert(scope);
        QObject::connect(scope, &QObject::destroyed, this, [this, scope] {
            d->observedScopes.remove(scope);
            retireScope(scope);
        });
    }
}
void Broker::retireScope(QObject *scope)
{
    d->scopes.remove(scope);
    const auto tasks = d->tasks.values();
    QPointer<Broker> guard(this);
    for (const auto &task : tasks)
        for (const auto &lease : task->leases)
            if (lease.scope == scope && lease.job) {
                d->cancelLease(task, lease.job, true);
                if (!guard)
                    return;
            }
}
Job *Broker::request(QObject *scope, Source source, Kind kind, Policy policy, Reader reader)
{
    auto *job = new Job(scope ? scope : this);
    QPointer<Broker> guard(this);
    const auto current = d->scopes.value(scope);
    const auto reject = [job, guard, scope, current](QString code, QString message) {
        QTimer::singleShot(0, job, [job, guard, scope, current, code, message] {
            if (!guard || guard->d->stopped || !guard->d->scopes.contains(scope) ||
                guard->d->scopes.value(scope) != current)
                job->finish(Job::State::Canceled, {}, "Canceled", "analysis scope retired", false);
            else
                job->finish(Job::State::Failed, {}, code, message);
        });
        return job;
    };
    if (!scope || d->stopped || !d->scopes.contains(scope) || d->scopes.value(scope) != source)
        return reject("StaleSource", "analysis scope/source generation is inactive");
    if (source.accountNamespace.isEmpty() || source.trackKey.isEmpty() ||
        source.representationKey.isEmpty() || source.accountNamespace.size() > 4096 ||
        source.trackKey.size() > 4096 || source.representationKey.size() > 4096)
        return reject("InvalidArgument", "bounded stable source identity is required");
    const auto key = d->key(source, kind);
    std::shared_ptr<Private::Task> task;
    for (const auto &candidate : d->tasks)
        if (candidate->key == key && !candidate->canceled->load()) {
            task = candidate;
            break;
        }
    // Source cached beats may observe an already-started grid; cached drum stems never join.
    const bool mayObserve = task && kind == Kind::Beats && policy == Policy::StoredOnly &&
                            task->policy == Policy::GpuOnly;
    if (task && policy == Policy::StoredOnly && !mayObserve)
        return reject("CacheMiss", "analysis is not yet completely cached");
    if (task && task->policy != policy && !mayObserve)
        return reject("Unavailable",
                      "active analysis uses a different explicitly requested backend policy");
    if (!task) {
        if (d->tasks.size() >= 2)
            return reject("Busy", "two analysis jobs are already pending");
        task = std::make_shared<Private::Task>();
        task->key = key;
        task->cachePath = d->config.cacheDirectory + '/' + key + ".json";
        task->id = QString::number(++d->serial);
        task->source = source;
        task->remembered = d->ephemeral.value(key);
        task->kind = kind;
        task->policy = policy;
        task->reader = std::move(reader);
        d->tasks.insert(task->id, task);
        d->order.append(task->id);
    }
    task->leases.push_back({job, scope, source});
    if (task->process) {
        job->m_state = Job::State::Running;
        job->m_result = task->partial;
    }
    std::weak_ptr<Private::Task> weak = task;
    job->m_cancel = [guard, weak, job] {
        if (guard)
            if (const auto task = weak.lock())
                guard->d->cancelLease(task, job, true);
    };
    if (task->leases.size() == 1)
        d->prepare(task);
    emit drainedChanged();
    return job;
}
void Broker::shutdown()
{
    if (d->stopped)
        return;
    d->stopped = true;
    d->resumes.clear();
    d->resumeOrder.clear();
    const auto scopes = d->scopes.keys();
    QPointer<Broker> guard(this);
    for (auto *scope : scopes) {
        retireScope(scope);
        if (!guard)
            return;
    }
}
bool Broker::drained() const
{
    return d->tasks.isEmpty();
}
int Broker::pendingCount() const
{
    return d->tasks.size();
}
bool Broker::scopeDrained(QObject *scope) const
{
    for (const auto &task : d->tasks) {
        for (const auto &lease : task->leases)
            if (lease.scope == scope) {
                if (d->scopes.contains(scope) || task->canceled->load())
                    return false;
            }
    }
    return true;
}
} // namespace EchoMusic::AudioAnalysis
