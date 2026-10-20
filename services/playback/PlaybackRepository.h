#pragma once
#include "PlaybackQueueCatalog.h"
#include <QJsonArray>
#include <QThread>
namespace EchoMusic
{
class PlaybackStorageWorker;

class PlaybackRepository final : public QObject
{
    Q_OBJECT
public:
    // Queue state is shared; history and rooms retain their real account scopes.
    static constexpr quint64 SharedQueueScope = 0;
    explicit PlaybackRepository(const QString &path, QObject *parent = nullptr);
    ~PlaybackRepository() override;
    void load(quint64 account, quint64 revision);
    void save(quint64 account, const PlaybackQueueStates &states, const QString &active,
              const QString &lastLocal = {});
    void record(quint64 account, const PlaybackTrack &track, qint64 listenedMs);
    void loadHistory(quint64 account, quint64 revision);
    void removeHistory(quint64 account, const QStringList &keys);
    void clearHistory(quint64 account);
    void loadRooms(quint64 account, quint64 revision);
    void saveRooms(quint64 account, const QJsonArray &rooms, const QStringList &dissolved);
    void shutdown();
signals:
    void loaded(quint64 account, quint64 revision, const EchoMusic::PlaybackQueueStates &states,
                const QString &active, const QString &lastLocal);
    void historyLoaded(quint64 account, quint64 revision, const QJsonArray &entries);
    void historyFailed(const QString &message);
    void historyChanged(quint64 account);
    void roomsLoaded(quint64 account, quint64 revision, const QJsonArray &rooms,
                     const QStringList &dissolved);
    void failed(const QString &message);
    void closed();

private:
    QThread m_thread;
    PlaybackStorageWorker *m_worker;
    bool m_closing = false;
};
} // namespace EchoMusic
