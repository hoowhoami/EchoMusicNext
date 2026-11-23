#pragma once
#include "SessionDatabase.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <functional>
#include <memory>

namespace EchoMusic
{
/// Immutable, disposable non-song records. Domain owners retain order, identity
/// and mutation semantics; only this store reads/writes JSON on the SQL worker.
class JsonRecordStore final : public QObject
{
    Q_OBJECT
public:
    struct State;
    using Snapshot = std::shared_ptr<State>;
    struct Reference
    {
        Snapshot snapshot;
        int row = 0;
    };
    using Completion = std::function<void(QJsonArray, QString)>;
    explicit JsonRecordStore(SessionDatabase &, QObject *parent = nullptr);
    Snapshot create(QJsonArray);
    int count(const Snapshot &) const;
    void read(Reference, QObject *consumer, Completion, int limit = 1);
    void readAll(Snapshot, QObject *consumer, Completion);
    bool persisted(const Snapshot &) const;
signals:
    void invalidated();
    void cacheFailed(const QString &);
    void stored();

private:
    void flush();
    void readRange(Snapshot, int, int, QObject *, Completion);
    SessionDatabase &m_database;
    QList<std::weak_ptr<State>> m_pending;
    bool m_writing = false;
};
} // namespace EchoMusic
