#pragma once
#include "PlaybackRepository.h"
#include <QSqlDatabase>
#include <QTimer>

namespace EchoMusic
{
/// Independent new-project schema. Queues and track identities are structured
/// rows with canonical catalog facts; signed URLs, credentials and raw provider
/// DTOs never enter this database. Own schema upgrades preserve those facts.
class PlaybackStorageWorker final : public QObject
{
    Q_OBJECT
public:
    explicit PlaybackStorageWorker(QObject *parent = nullptr);
    ~PlaybackStorageWorker() override;
    void initialize(const QString &path);
    void load(quint64 account, quint64 revision, bool projectionsOnly = false);
    std::pair<PlaybackTrack, QString> hydrate(PlaybackTrack);
    void collectPayloads();
    void save(quint64 account, const PlaybackQueueStates &states, const QString &active,
              const QString &lastLocal = {});
    void record(quint64 account, const PlaybackTrack &track, qint64 listenedMs);
    void addHistoryTime(quint64 account, const PlaybackTrack &track, qint64 listenedMs);
    void loadHistory(quint64 account, quint64 revision, bool projectionsOnly = false);
    std::pair<QList<QJsonObject>, QString> historyTracks(quint64 account, const QStringList &);
    /// Unmatched cloud uploads converge their per-file identities on hash.
    QString historyKey(const PlaybackTrack &track);
    void removeHistory(quint64 account, const QStringList &keys);
    void clearHistory(quint64 account);
    void loadRooms(quint64 account, quint64 revision);
    void saveRooms(quint64 account, const QJsonArray &rooms, const QStringList &dissolved);
    void close();
signals:
    void loaded(quint64 account, quint64 revision, const EchoMusic::PlaybackQueueStates &states,
                const QString &active, const QString &lastLocal);
    void historyLoaded(quint64 account, quint64 revision, const QJsonArray &entries,
                       EchoMusic::HistoryPayloadLease lease);
    void saved(const QStringList &payloadKeys, EchoMusic::PlaybackPayloadLease lease);
    void historyFailed(const QString &message);
    void historyChanged(quint64 account);
    void roomsLoaded(quint64 account, quint64 revision, const QJsonArray &rooms,
                     const QStringList &dissolved);
    void failed(const QString &message);
    void drained();

private:
    PlaybackPayloadLease queueLease(const QStringList &keys);
    void collectQueuePayloads();
    void scheduleQueueCollection();
    QTimer m_queueCollection{this};
    qint64 m_queueCollectionCursor = 0;
    QList<std::weak_ptr<const PlaybackPayloadSnapshot>> m_queueLeases;
    std::shared_ptr<PlaybackPayloadSnapshot::Retirement> m_queueRetirement;
    void collectHistoryPayloads();
    void scheduleHistoryCollection();
    QTimer m_historyCollection{this};
    QList<std::weak_ptr<const HistoryPayloadSnapshot>> m_historyLeases;
    std::shared_ptr<HistoryPayloadSnapshot::Retirement> m_retirement;
    QSqlDatabase m_database;
    bool m_valid = false;
};
} // namespace EchoMusic
