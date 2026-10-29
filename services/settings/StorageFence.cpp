#include "SettingsStore.h"
#include "StorageFence.h"
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QMap>
#include <QSet>
#include <QElapsedTimer>
namespace EchoMusic
{
namespace
{
QMutex mutex;
QWaitCondition changed;
QMap<QString, int> writers;
QSet<QString> snapshots;
bool overlaps(const QString &a, const QString &b)
{
    return a == b || a.startsWith(b + '/') || b.startsWith(a + '/');
}
} // namespace
std::shared_ptr<StorageFence> StorageFence::write(const QString &path)
{
    if (path.isEmpty())
        return {};
    const auto root = canonicalStoragePath(path);
    QMutexLocker lock(&mutex);
    for (const auto &snapshot : snapshots)
        if (overlaps(root, snapshot))
            return {};
    ++writers[root];
    return std::shared_ptr<StorageFence>(new StorageFence(root, false));
}
std::shared_ptr<StorageFence> StorageFence::freeze(const QString &path)
{
    if (path.isEmpty())
        return {};
    const auto root = canonicalStoragePath(path);
    QMutexLocker lock(&mutex);
    for (const auto &snapshot : snapshots)
        if (overlaps(root, snapshot))
            return {};
    snapshots.insert(root);
    return std::shared_ptr<StorageFence>(new StorageFence(root, true));
}
StorageFence::~StorageFence()
{
    QMutexLocker lock(&mutex);
    if (m_snapshot)
        snapshots.remove(m_path);
    else if (--writers[m_path] == 0)
        writers.remove(m_path);
    changed.wakeAll();
}
bool StorageFence::waitForWriters(const std::atomic_bool &cancelled) const
{
    QElapsedTimer deadline;
    deadline.start();
    QMutexLocker lock(&mutex);
    while (!cancelled && deadline.elapsed() < 5000) {
        bool busy = false;
        for (auto it = writers.begin(); it != writers.end(); ++it)
            busy |= it.value() > 0 && overlaps(m_path, it.key());
        if (!busy)
            return true;
        changed.wait(&mutex, 50);
    }
    return false;
}
} // namespace EchoMusic
