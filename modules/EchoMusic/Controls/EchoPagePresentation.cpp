#include "EchoPagePresentation.h"
#include <utility>

namespace EchoMusic
{
void EchoPagePresentation::begin(bool suppressInitialEntry)
{
    for (auto *viewport : std::as_const(m_waiters))
        disconnect(viewport, nullptr, this, nullptr);
    m_waiters.clear();
    m_loaded = false;
    m_suppressInitialEntry = suppressInitialEntry;
    m_presented = false;
    if (m_ready) {
        m_ready = false;
        emit readyChanged();
    }
    emit stateChanged();
}
void EchoPagePresentation::finishLoading()
{
    m_loaded = true;
    emit restoreRequested();
    emit stateChanged();
    updateReady();
}
void EchoPagePresentation::cancel()
{
    begin(false);
}
bool EchoPagePresentation::waitFor(QObject *viewport)
{
    // Readiness is monotonic within this visual instance. A later user-opened
    // tab or an asynchronous image must not hide an already presented page.
    if (!viewport || m_ready)
        return false;
    if (!m_waiters.contains(viewport)) {
        m_waiters.insert(viewport);
        connect(viewport, &QObject::destroyed, this, [this, viewport] { release(viewport); });
    }
    return true;
}
void EchoPagePresentation::release(QObject *viewport)
{
    if (!m_waiters.remove(viewport))
        return;
    disconnect(viewport, nullptr, this, nullptr);
    updateReady();
}
void EchoPagePresentation::updateReady()
{
    if (!m_ready && m_loaded && m_waiters.isEmpty()) {
        m_ready = true;
        emit readyChanged();
    }
}
void EchoPagePresentation::presented()
{
    if (m_presented)
        return;
    m_presented = true;
    emit stateChanged();
}
} // namespace EchoMusic
