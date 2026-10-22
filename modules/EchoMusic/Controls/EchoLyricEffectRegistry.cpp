#include "EchoLyricEffectRegistry.h"
#include <algorithm>
namespace EchoMusic
{
EchoLyricEffectRegistry *EchoLyricEffectRegistry::forEngine(QQmlEngine &e)
{
    return e.singletonInstance<EchoLyricEffectRegistry *>("EchoMusic.Controls",
                                                          "EchoLyricEffectRegistry");
}
quint64 EchoLyricEffectRegistry::put(ScrollHandler h)
{
    if (!h.owner || (!m_handlers.contains(h.key) && m_handlers.size() >= 128))
        return 0;
    h.generation = ++m_generation;
    const auto key = h.key;
    const auto generation = h.generation;
    connect(h.owner, &QObject::destroyed, this,
            [this, key, generation] { remove(key, generation); });
    m_handlers.insert(key, std::move(h));
    emit changed();
    return generation;
}
void EchoLyricEffectRegistry::remove(const QString &key, quint64 generation)
{
    if (m_handlers.contains(key) && m_handlers.value(key).generation == generation) {
        m_handlers.remove(key);
        emit changed();
    }
}
bool EchoLyricEffectRegistry::hasHandlers(const QString &scope) const
{
    for (const auto &h : m_handlers)
        if (h.owner && h.scopes.contains(scope))
            return true;
    return false;
}
bool EchoLyricEffectRegistry::requestScroll(const QString &host, const QVariantMap &request)
{
    if (request.value("browsing").toBool())
        return false;
    auto entries = m_handlers.values();
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return a.order == b.order ? a.key < b.key : a.order > b.order;
    });
    for (const auto &e : entries)
        if (e.owner && e.scopes.contains(request.value("scope").toString()) && e.invoke &&
            e.invoke(host, request))
            return true;
    return false;
}
} // namespace EchoMusic
