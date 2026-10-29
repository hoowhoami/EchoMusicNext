// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

namespace EchoMusic::AudioAnalysis
{
enum class Kind
{
    Beats,
    DrumHits
};
enum class Policy
{
    StoredOnly,
    GpuOnly,
    CpuOffline
};
struct Source
{
    QString accountNamespace, trackKey, representationKey;
    quint64 revision = 0, generation = 0;
    bool persistent = true;
    bool operator==(const Source &) const = default;
};
struct Pcm
{
    QByteArray left, right;
    int sampleRate = 0;
    bool wholeTrack = false;
    QString error, code;
};
using Reader = std::function<Pcm(const std::shared_ptr<std::atomic_bool> &cancelled)>;
struct Config
{
    QString helper, modelDirectory, runtimeDirectory, cacheDirectory, scratchDirectory;
};
class Job final : public QObject
{
    Q_OBJECT
public:
    enum class State
    {
        Pending,
        Running,
        Succeeded,
        Failed,
        Canceled
    };
    Q_ENUM(State)
    explicit Job(QObject *parent);
    ~Job() override;
    State state() const
    {
        return m_state;
    }
    bool finished() const
    {
        return m_state >= State::Succeeded;
    }
    QJsonObject result() const
    {
        return m_result;
    }
    QString errorCode() const
    {
        return m_code;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    void cancel();
signals:
    void changed();
    void completed();
    void progress(int done, int total, double untilSeconds);

private:
    friend class Broker;
    friend class Service;
    void finish(State, QJsonObject = {}, QString = {}, QString = {}, bool notify = true);
    State m_state = State::Pending;
    QJsonObject m_result;
    QString m_code, m_error;
    std::function<void()> m_cancel;
};
// Internal host service, intentionally not registered as an SDK/QML type yet.
// Reader must capture immutable authorized decode state, never a GUI QObject/raw plugin path.
class Broker final : public QObject
{
    Q_OBJECT
public:
    explicit Broker(Config, QObject *parent = nullptr);
    ~Broker() override;
    void setSource(QObject *scope, Source);
    void retireScope(QObject *scope);
    Job *request(QObject *scope, Source, Kind, Policy, Reader = {});
    void shutdown();
    bool drained() const;
    int pendingCount() const;
    bool scopeDrained(QObject *) const;
signals:
    void drainedChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
};
} // namespace EchoMusic::AudioAnalysis
