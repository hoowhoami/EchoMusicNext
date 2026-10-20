#pragma once
#include <array>
#include <functional>
#include <utility>

namespace EchoMusic
{
/// Explicit asynchronous shutdown dependencies. Completion is idempotent and
/// may be synchronous/reentrant; no counter can mistake duplicates for drains.
class ApplicationDrain final
{
public:
    enum class Service
    {
        Plugins,
        Playback,
        DeviceRegistration,
        Api,
        Accounts,
        Storage,
        Count
    };
    struct Actions
    {
        std::function<void()> stopIngress;
        std::function<void()> stopBusiness;
        std::function<void()> finish;
    };
    explicit ApplicationDrain(Actions actions) : m_actions(std::move(actions)) {}
    void request()
    {
        if (std::exchange(m_requested, true))
            return;
        m_actions.stopIngress();
        advance();
    }
    void completed(Service service)
    {
        m_completed.at(static_cast<size_t>(service)) = true;
        advance();
    }
    bool requested() const
    {
        return m_requested;
    }

private:
    void advance()
    {
        if (!m_requested || !m_completed[static_cast<size_t>(Service::Plugins)])
            return;
        if (!std::exchange(m_businessStopped, true))
            m_actions.stopBusiness();
        for (bool complete : m_completed)
            if (!complete)
                return;
        if (!std::exchange(m_finished, true))
            m_actions.finish();
    }
    Actions m_actions;
    std::array<bool, static_cast<size_t>(Service::Count)> m_completed{};
    bool m_requested = false, m_businessStopped = false, m_finished = false;
};
} // namespace EchoMusic
