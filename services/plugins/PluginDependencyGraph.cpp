#include "PluginDependencyGraph.h"
#include <QObject>
#include <algorithm>

namespace EchoMusic
{
PluginDependencyGraph PluginDependencyGraph::resolve(const QList<PluginPackage> &packages,
                                                     const QSet<QString> &enabled)
{
    PluginDependencyGraph result;
    QMap<QString, PluginPackage> nodes;
    QMap<QString, PluginPackage> known;
    QMap<QString, QSet<QString>> predecessors;
    QMap<QString, QSet<QString>> requirements;
    for (const auto &package : packages) {
        if (!package.id.isEmpty())
            known.insert(package.id, package);
        if (package.id.isEmpty() || !enabled.contains(package.id))
            continue;
        if (nodes.contains(package.id))
            result.errors[package.id] = QObject::tr("重复的插件 ID：%1").arg(package.id);
        nodes.insert(package.id, package);
    }
    for (auto it = nodes.begin(); it != nodes.end(); ++it) {
        const auto &package = it.value();
        if (!package.error.isEmpty()) {
            result.errors[it.key()] = package.error;
            continue;
        }
        auto dependencies = package.dependencies;
        for (auto optional = package.optionalDependencies.begin();
             optional != package.optionalDependencies.end(); ++optional)
            if (enabled.contains(optional.key()))
                dependencies.insert(optional.key(), optional.value());
        for (auto dependency = dependencies.begin(); dependency != dependencies.end();
             ++dependency) {
            if (!nodes.contains(dependency.key())) {
                const auto provider = known.constFind(dependency.key());
                result.errors[it.key()] =
                    provider != known.cend() && provider->incompatible
                        ? QObject::tr("依赖 %1 不可用：%2").arg(dependency.key(), provider->error)
                        : QObject::tr("需要启用依赖插件 %1（%2）")
                              .arg(dependency.key(), dependency.value());
            } else if (!PluginPackage::versionMatches(dependency.value(),
                                                      nodes[dependency.key()].version))
                result.errors[it.key()] =
                    QObject::tr("依赖 %1 要求 %2，已安装 %3")
                        .arg(dependency.key(), dependency.value(), nodes[dependency.key()].version);
            predecessors[it.key()].insert(dependency.key());
            requirements[it.key()].insert(dependency.key());
        }
        for (auto conflict = package.conflicts.begin(); conflict != package.conflicts.end();
             ++conflict)
            if (nodes.contains(conflict.key()) &&
                PluginPackage::versionMatches(conflict.value(), nodes[conflict.key()].version)) {
                const auto message =
                    QObject::tr("插件冲突：%1 与 %2").arg(it.key(), conflict.key());
                result.errors[it.key()] = message;
                result.errors[conflict.key()] = message;
            }
        for (const auto &id : package.loadAfter)
            if (nodes.contains(id))
                predecessors[it.key()].insert(id);
        for (const auto &id : package.loadBefore)
            if (nodes.contains(id))
                predecessors[id].insert(it.key());
    }
    QSet<QString> remaining;
    for (auto it = nodes.begin(); it != nodes.end(); ++it)
        remaining.insert(it.key());
    while (!remaining.isEmpty()) {
        auto candidates = remaining.values();
        std::sort(candidates.begin(), candidates.end());
        bool progressed = false;
        for (const auto &id : candidates) {
            auto pending = predecessors[id];
            pending.intersect(remaining);
            if (!pending.isEmpty())
                continue;
            for (const auto &dependency : requirements[id])
                if (result.errors.contains(dependency))
                    result.errors[id] = QObject::tr("依赖 %1 不可用：%2")
                                            .arg(dependency, result.errors[dependency]);
            remaining.remove(id);
            result.order.append(id);
            progressed = true;
        }
        if (!progressed) {
            auto cycle = remaining.values();
            std::sort(cycle.begin(), cycle.end());
            for (const auto &id : cycle)
                result.errors[id] =
                    QObject::tr("插件依赖或加载顺序存在循环：%1").arg(cycle.join(" → "));
            break;
        }
    }
    return result;
}
} // namespace EchoMusic
