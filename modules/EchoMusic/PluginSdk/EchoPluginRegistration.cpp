#include "EchoPluginRegistration.h"
#include <utility>
namespace EchoMusic
{
EchoPluginRegistration::EchoPluginRegistration(QString key, quint64 revision, QObject *parent)
    : QObject(parent), m_key(std::move(key)), m_revision(revision)
{
}
void EchoPluginRegistration::stage(std::function<bool()> publish, std::function<void()> remove)
{
    if (!m_active)
        return;
    m_publish = std::move(publish);
    m_remove = std::move(remove);
}
bool EchoPluginRegistration::commit()
{
    if (!m_active)
        return true;
    if (m_published)
        return true;
    // Publication emits host signals. A listener may synchronously retire this
    // scope; make revocation effective during that call and retain its callable.
    const auto publish = m_publish;
    m_published = true;
    const bool success = !publish || publish();
    if (!success)
        m_published = false;
    return success;
}
void EchoPluginRegistration::dispose()
{
    if (!std::exchange(m_active, false))
        return;
    if (m_published && m_remove)
        m_remove();
    m_publish = {};
    m_remove = {};
    emit activeChanged();
}
} // namespace EchoMusic
