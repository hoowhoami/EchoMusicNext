#pragma once
#include "SongDatasetStore.h"
#include <QJsonArray>
namespace EchoMusic
{
/// Bounded asynchronous page ingestion shared by account-owned song sources.
/// The caller owns membership/version semantics; the returned leases own records.
class SongSourceCache final : public QObject
{
    Q_OBJECT
public:
    explicit SongSourceCache(SessionDatabase &, QObject *parent = nullptr);
    using Completion = std::function<void(QList<SongDatasetStore::CachedRow>, QString)>;
    void cache(QJsonArray songs, QObject *consumer, Completion, SongDatasetStore::Loader = {},
               QList<QJsonObject> presentations = {});
    SessionDatabase &database()
    {
        return m_database;
    }
    SongDatasetStore &store()
    {
        return m_store;
    }

private:
    SessionDatabase &m_database;
    SongDatasetStore m_store;
};
} // namespace EchoMusic
