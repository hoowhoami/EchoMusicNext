#include "AppearanceOverrides.h"
#include <algorithm>
namespace EchoMusic
{
bool AppearanceOverrides::put(const QString &token, QVariantMap definition, int priority,
                              QVariantMap owner)
{
    if (!m_entries.contains(token) && m_entries.size() >= 128)
        return false;
    m_entries.insert(token, {std::move(definition), std::move(owner), priority, ++m_order});
    emit changed();
    return true;
}
void AppearanceOverrides::remove(const QString &token)
{
    if (m_entries.remove(token))
        emit changed();
}
QList<AppearanceOverrides::Entry> AppearanceOverrides::ordered() const
{
    auto entries = m_entries.values();
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.priority == b.priority ? a.order < b.order : a.priority < b.priority;
    });
    return entries;
}
QVariantMap AppearanceOverrides::resolve(QVariantMap base, bool dark) const
{
    for (const auto &entry : ordered()) {
        const auto values = entry.definition.value(dark ? "dark" : "light").toMap();
        for (auto group = values.begin(); group != values.end(); ++group) {
            auto fields = base.value(group.key()).toMap();
            const auto patch = group.value().toMap();
            for (auto field = patch.begin(); field != patch.end(); ++field)
                fields[field.key()] = field.value();
            base[group.key()] = fields;
        }
    }
    return base;
}
QVariantList AppearanceOverrides::records() const
{
    QVariantList out;
    for (const auto &entry : ordered()) {
        auto row = entry.owner;
        row["priority"] = entry.priority;
        out.append(row);
    }
    return out;
}
} // namespace EchoMusic
