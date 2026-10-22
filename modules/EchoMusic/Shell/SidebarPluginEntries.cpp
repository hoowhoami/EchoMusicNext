#include "SidebarLayoutModel.h"
#include <QTimer>
#include <algorithm>
namespace EchoMusic
{
quint64 SidebarLayoutModel::registerPluginEntry(PluginEntry entry)
{
    if (!entry.owner || entry.key.isEmpty() ||
        (!m_plugins.contains(entry.key) && m_plugins.size() >= 256))
        return 0;
    entry.generation = ++m_pluginGeneration;
    const auto key = entry.key;
    const auto generation = entry.generation;
    connect(entry.owner, &QObject::destroyed, this,
            [this, key, generation] { unregisterPluginEntry(key, generation); });
    m_plugins.insert(key, std::move(entry));
    refreshPlugins();
    return generation;
}
void SidebarLayoutModel::unregisterPluginEntry(const QString &key, quint64 generation)
{
    if (m_plugins.value(key).generation == generation) {
        endShortcutDrag(false);
        m_plugins.remove(key);
        refreshPlugins();
    }
}
void SidebarLayoutModel::refreshPlugins()
{
    if (m_pluginRefreshPending)
        return;
    m_pluginRefreshPending = true;
    QTimer::singleShot(0, this, [this] {
        m_pluginRefreshPending = false;
        emit changed();
    });
}
bool SidebarLayoutModel::isPlugin(const QString &key) const
{
    return m_plugins.contains(key);
}
bool SidebarLayoutModel::entryEnabled(const QString &key) const
{
    const auto e = m_plugins.value(key);
    return e.owner && (!e.visible || e.visible()) && (!e.enabled || e.enabled());
}
bool SidebarLayoutModel::activate(const QString &key)
{
    if (!isPlugin(key))
        return false;
    const auto e = m_plugins.value(key);
    if (entryEnabled(key) && e.invoke)
        e.invoke();
    return true;
}
void SidebarLayoutModel::toggleCollapsed(const QString &key)
{
    for (const auto &s : sections())
        if (s.toMap().value("key") == key && s.toMap().value("collapsible", true).toBool()) {
            if (!m_collapsedGroups.removeOne(key))
                m_collapsedGroups.append(key);
            save();
            return;
        }
}
QVariantMap SidebarLayoutModel::describePlugin(const PluginEntry &entry) const
{
    return {{"key", entry.key},
            {"title", entry.values.value("title")},
            {"glyph", entry.values.value("icon", "puzzle")},
            {"route", entry.values.value("route", QString())},
            {"hidden",
             m_hidden.contains(entry.key) || !entry.owner || (entry.visible && !entry.visible())},
            {"enabled", entryEnabled(entry.key)},
            {"plugin", true},
            {"section", entry.values.value("section", "plugins")},
            {"order", entry.values.value("order", 0)},
            {"before", entry.values.value("before", QString())},
            {"after", entry.values.value("after", QString())}};
}
QVariantList SidebarLayoutModel::pluginEntries(bool shortcut) const
{
    QList<PluginEntry> sorted;
    for (const auto &e : m_plugins)
        if (e.owner && e.shortcut == shortcut)
            sorted.append(e);
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        const auto ao = a.values.value("order", 0).toDouble(),
                   bo = b.values.value("order", 0).toDouble();
        return ao == bo ? a.key < b.key : ao < bo;
    });
    QVariantList result;
    for (const auto &e : sorted)
        result.append(describePlugin(e));
    return result;
}
void SidebarLayoutModel::addPluginSections(QVariantList &sections) const
{
    const auto entries = pluginEntries(false);
    for (const auto &item : entries) {
        const auto e = item.toMap();
        const auto key = e.value("section").toString();
        int index = -1;
        for (int i = 0; i < sections.size(); ++i)
            if (sections[i].toMap().value("key") == key) {
                index = i;
                break;
            }
        if (index < 0) {
            const auto def = m_plugins.value(e.value("key").toString()).values;
            index = sections.size();
            sections.append(QVariantMap{{"key", key},
                                        {"title", def.value("sectionTitle", tr("插件"))},
                                        {"hidden", m_hiddenGroups.contains(key)},
                                        {"collapsed", m_collapsedGroups.contains(key)},
                                        {"collapsible", def.value("collapsible", true)},
                                        {"sectionOrder", def.value("sectionOrder", 100)}});
        }
        auto section = sections[index].toMap();
        auto rows = section.value("items").toList();
        rows.append(e);
        section["items"] = rows;
        sections[index] = section;
    }
    for (auto &value : sections) {
        auto section = value.toMap();
        auto rows = section.value("items").toList();
        std::stable_sort(rows.begin(), rows.end(), [this](const auto &a, const auto &b) {
            const auto ai = m_pluginOrder.indexOf(a.toMap().value("key").toString()),
                       bi = m_pluginOrder.indexOf(b.toMap().value("key").toString());
            return (ai < 0 ? m_pluginOrder.size() : ai) < (bi < 0 ? m_pluginOrder.size() : bi);
        });
        for (const auto &e : entries) {
            const auto map = e.toMap();
            if (m_pluginOrder.contains(map.value("key").toString()))
                continue;
            for (const auto &side : {"before", "after"}) {
                const auto target = map.value(side).toString();
                if (target.isEmpty())
                    continue;
                int from = -1, to = -1;
                for (int i = 0; i < rows.size(); ++i) {
                    if (rows[i].toMap().value("key") == map.value("key"))
                        from = i;
                    if (rows[i].toMap().value("key") == target)
                        to = i;
                }
                if (from >= 0 && to >= 0 && from != to) {
                    auto row = rows.takeAt(from);
                    rows.insert(
                        qBound(0, to + (QString(side) == "after" ? 1 : 0) - (from < to ? 1 : 0),
                               rows.size()),
                        row);
                }
            }
        }
        section["items"] = rows;
        value = section;
    }
    std::stable_sort(sections.begin(), sections.end(), [](const auto &a, const auto &b) {
        return a.toMap().value("sectionOrder", 0).toDouble() <
               b.toMap().value("sectionOrder", 0).toDouble();
    });
}
} // namespace EchoMusic
