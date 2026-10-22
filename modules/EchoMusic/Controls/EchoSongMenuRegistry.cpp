#include "EchoSongMenuRegistry.h"
#include <algorithm>
namespace EchoMusic
{
EchoSongMenuRegistry *EchoSongMenuRegistry::forEngine(QQmlEngine &e)
{
    return e.singletonInstance<EchoSongMenuRegistry *>("EchoMusic.Controls",
                                                       "EchoSongMenuRegistry");
}
quint64 EchoSongMenuRegistry::put(Entry e)
{
    if (!e.owner || (!m_entries.contains(e.key) && m_entries.size() >= 256))
        return 0;
    e.generation = ++m_generation;
    const auto key = e.key;
    const auto generation = e.generation;
    connect(e.owner, &QObject::destroyed, this,
            [this, key, generation] { remove(key, generation); });
    m_entries.insert(key, std::move(e));
    emit changed();
    return generation;
}
void EchoSongMenuRegistry::remove(const QString &key, quint64 generation)
{
    if (m_entries.value(key).generation == generation) {
        m_entries.remove(key);
        emit changed();
    }
}
QVariantList EchoSongMenuRegistry::entries(const QVariantMap &context) const
{
    if (context.value("track").toMap().isEmpty())
        return {};
    auto values = m_entries.values();
    std::sort(values.begin(), values.end(), [](const auto &a, const auto &b) {
        return a.order == b.order ? a.key < b.key : a.order < b.order;
    });
    QVariantList result;
    for (const auto &e : values)
        if (e.owner && (!e.visible || e.visible(context)))
            result.append(QVariantMap{{"key", e.key},
                                      {"text", e.label},
                                      {"danger", e.danger},
                                      {"enabled", !e.enabled || e.enabled(context)},
                                      {"token", QString::number(e.generation) + ":" +
                                                    (e.epoch ? e.epoch() : QString())}});
    return result;
}
bool EchoSongMenuRegistry::invoke(const QString &key, const QString &token,
                                  const QVariantMap &context)
{
    const auto e = m_entries.value(key);
    if (!e.owner ||
        token != QString::number(e.generation) + ":" + (e.epoch ? e.epoch() : QString()) ||
        (e.visible && !e.visible(context)) || (e.enabled && !e.enabled(context)) || !e.invoke)
        return false;
    e.invoke(context);
    return true;
}
} // namespace EchoMusic
