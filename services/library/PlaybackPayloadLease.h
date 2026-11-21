#pragma once
#include <QMetaType>
#include <QStringList>
#include <functional>
#include <memory>
#include <mutex>

namespace EchoMusic
{
class PlaybackStorageWorker;
/// Immutable persistent versions published by one queue/history manifest.
/// Track copies and admitted reads pin keys, never a second copy of song JSON.
class PlaybackPayloadSnapshot final
{
public:
    ~PlaybackPayloadSnapshot();

private:
    friend class PlaybackStorageWorker;
    PlaybackPayloadSnapshot() = default;
    PlaybackPayloadSnapshot(const PlaybackPayloadSnapshot &) = delete;
    PlaybackPayloadSnapshot &operator=(const PlaybackPayloadSnapshot &) = delete;
    struct Retirement
    {
        std::mutex mutex;
        std::function<void()> notify;
    };
    QStringList m_keys;
    std::shared_ptr<Retirement> m_retirement;
};
using PlaybackPayloadLease = std::shared_ptr<const PlaybackPayloadSnapshot>;
} // namespace EchoMusic
Q_DECLARE_METATYPE(EchoMusic::PlaybackPayloadLease)
