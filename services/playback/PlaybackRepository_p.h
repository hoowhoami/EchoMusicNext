#pragma once
#include "PlaybackRepository.h"
#include <QSqlDatabase>

namespace EchoMusic
{
/// Independent new-project schema. Queues and track identities are structured
/// rows with canonical catalog facts; signed URLs, credentials and raw provider
/// DTOs never enter this database. Own schema upgrades preserve those facts.
class PlaybackStorageWorker final : public QObject
{
    Q_OBJECT
public:
    explicit PlaybackStorageWorker(QObject *parent = nullptr) : QObject(parent) {}
    ~PlaybackStorageWorker() override;
    void initialize(const QString &path);
    void load(quint64 account, quint64 revision);
    void save(quint64 account, const PlaybackQueueStates &states, const QString &active,
              const QString &lastLocal = {});
    void record(quint64 account, const PlaybackTrack &track, qint64 listenedMs);
    void loadHistory(quint64 account, quint64 revision);
    void removeHistory(quint64 account, const QStringList &keys);
    void clearHistory(quint64 account);
    void loadRooms(quint64 account, quint64 revision);
    void saveRooms(quint64 account, const QJsonArray &rooms, const QStringList &dissolved);
    void close();
signals:
    void loaded(quint64 account, quint64 revision, const EchoMusic::PlaybackQueueStates &states,
                const QString &active, const QString &lastLocal);
    void historyLoaded(quint64 account, quint64 revision, const QJsonArray &entries);
    void historyFailed(const QString &message);
    void historyChanged(quint64 account);
    void roomsLoaded(quint64 account, quint64 revision, const QJsonArray &rooms,
                     const QStringList &dissolved);
    void failed(const QString &message);
    void drained();

private:
    QSqlDatabase m_database;
    bool m_valid = false;
};
} // namespace EchoMusic
