#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QThread>
#include <QList>
#include <functional>
#include <memory>
#include <utility>

namespace EchoMusic
{
/// One disposable database/worker per application. Domain stores own schemas,
/// query semantics and generations; this owner provides bounded work admission,
/// typed GUI delivery and shutdown. No business credentials or durable facts.
class SessionDatabase final : public QObject
{
    Q_OBJECT
public:
    struct Limits
    {
        quint64 pendingBytes = 8 * 1024 * 1024;
        int pendingJobs = 32;
    };
    explicit SessionDatabase(QString directory, QObject *parent = nullptr);
    SessionDatabase(QString directory, Limits limits, QObject *parent = nullptr);
    ~SessionDatabase() override;
    /// work(db, initializationError) executes on the database thread; done(result)
    /// executes on this owner thread only while consumer is alive. A false return
    /// leaves the input with its producer; resume on capacityAvailable.
    template <typename Work, typename Done>
    bool submit(QObject *consumer, Work work, Done done, quint64 cost = 0)
    {
        return enqueue(consumer, cost,
                       [work = std::move(work), done = std::move(done)](
                           QSqlDatabase &db, const QString &error) mutable -> Completion {
                           auto value =
                               std::make_shared<decltype(work(db, error))>(work(db, error));
                           return [value = std::move(value), done = std::move(done)]() mutable {
                               done(std::move(*value));
                           };
                       });
    }
    /// Small domain cleanup commands are retained until admitted, then drained.
    void discard(std::function<void(QSqlDatabase &)>);
    void shutdown();
    quint64 pendingBytes() const
    {
        return m_pendingBytes;
    }
    int pendingJobs() const
    {
        return m_pendingJobs;
    }
signals:
    void capacityAvailable();
    void closed();

private:
    using Completion = std::function<void()>;
    using Job = std::function<Completion(QSqlDatabase &, const QString &)>;
    bool enqueue(QObject *, quint64, Job);
    struct State;
    std::shared_ptr<State> m_state;
    QThread m_thread;
    QObject *m_worker;
    Limits m_limits;
    quint64 m_pendingBytes = 0;
    int m_pendingJobs = 0;
    QList<std::function<void(QSqlDatabase &)>> m_retired;
    bool m_closing = false;
};
} // namespace EchoMusic
