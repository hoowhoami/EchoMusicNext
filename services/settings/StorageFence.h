#pragma once
#include <QString>
#include <memory>
#include <atomic>
#include <utility>
namespace EchoMusic
{
/// File owners acquire a write lease before dispatching work, including helper
/// processes. A snapshot closes admission first, then drains accepted workers.
class StorageFence final
{
public:
    static std::shared_ptr<StorageFence> write(const QString &path);
    static std::shared_ptr<StorageFence> freeze(const QString &path);
    ~StorageFence();
    bool waitForWriters(const std::atomic_bool &cancelled) const;

private:
    StorageFence(QString path, bool snapshot) : m_path(std::move(path)), m_snapshot(snapshot) {}
    QString m_path;
    bool m_snapshot;
};
} // namespace EchoMusic
