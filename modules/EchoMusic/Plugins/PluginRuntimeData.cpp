#include "PluginRuntime.h"
#include "PluginRuntimeHelpers_p.h"
#include <QDesktopServices>
namespace EchoMusic
{
using namespace PluginRuntimeDetail;
QVariantList PluginRuntime::dataEntries() const
{
    QVariantList result;
    const PluginPaths paths(m_root);
    for (const auto &entry : m_entries)
        if (PluginPaths::validId(entry->package.id))
            result.append(QVariantMap{
                {"key", entry->package.installationId},
                {"id", entry->package.id},
                {"name", entry->package.name},
                {"retained", false},
                {"path", paths.data(entry->package.id)},
                {"source",
                 entry->origin.value("repository", entry->origin.value("name", tr("本地安装")))}});
    for (const auto &value : PluginInstallationStore(m_root).retained()) {
        auto record = value.toMap();
        const auto id = record.value("id").toString(), relative = record.value("path").toString();
        if (!PluginPaths::validId(id) || (relative.isEmpty() && !installationForId(id).isEmpty()))
            continue;
        if (!relative.isEmpty() &&
            relative != ".retained/" + id + '/' + QFileInfo(relative).fileName())
            continue;
        if (!relative.isEmpty() &&
            !PluginInstallationStore::validToken(QFileInfo(relative).fileName()))
            continue;
        record["key"] = id + ':' + relative;
        record["retained"] = true;
        record["path"] = relative.isEmpty() ? paths.data(id) : m_root + "/.data/" + relative;
        record["source"] = record.value("origin").toMap().value("repository", tr("本地安装"));
        result.append(record);
    }
    return result;
}
bool PluginRuntime::openPluginDirectory(const QString &installation, bool data)
{
    auto *entry = find(installation);
    if (!entry || !PluginPaths::validId(entry->package.id))
        return false;
    const auto path = data ? PluginPaths(m_root).data(entry->package.id)
                           : PluginPaths(m_root).code(entry->package.id);
    return QDir().mkpath(path) && QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
bool PluginRuntime::openDataDirectory(const QString &key)
{
    for (const auto &value : dataEntries()) {
        const auto row = value.toMap();
        if (row.value("key") == key) {
            const auto path = row.value("path").toString();
            return QDir().mkpath(path) && QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        }
    }
    return false;
}
bool PluginRuntime::clearPluginData(const QString &key)
{
    if (m_closing || m_maintenance || loading())
        return false;
    if (auto *entry = find(key)) {
        if (!PluginPaths::validId(entry->package.id))
            return false;
        if (entry->mutation != Entry::None)
            return false;
        entry->mutation = Entry::Clearing;
        entry->applying = false;
        if (entry->context)
            retire(*entry, Disabled);
        else
            applyMutation(*entry);
        return true;
    }
    for (const auto &value : dataEntries()) {
        const auto record = value.toMap();
        if (record.value("key") != key || !record.value("retained").toBool())
            continue;
        ++m_imports;
        emit loadingChanged();
        auto *watcher = new QFutureWatcher<bool>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, key] {
            --m_imports;
            const auto ok = watcher->result();
            watcher->deleteLater();
            const auto error =
                ok ? PluginInstallationStore(m_root).removeRetained(key) : tr("无法删除保留数据");
            emit notice(error.isEmpty() ? tr("插件数据已删除") : error, !error.isEmpty());
            emit catalogChanged();
            emit loadingChanged();
            queueReconcile();
            checkClosed();
        });
        watcher->setFuture(QtConcurrent::run([path = record.value("path").toString()] {
            return !QFileInfo(path).isSymLink() && QDir(path).removeRecursively();
        }));
        return true;
    }
    return false;
}
} // namespace EchoMusic
