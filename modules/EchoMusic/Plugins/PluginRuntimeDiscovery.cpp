#include "SettingsKeys.h"
#include "PluginRuntime.h"
#include "StorageSnapshot.h"
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QFileInfo>
#include <QDir>
#include <QUuid>
#include <QThread>
#include <QDateTime>
#include <utility>
#include "PluginRuntimeHelpers_p.h"

namespace EchoMusic
{
using namespace PluginRuntimeDetail;

void PluginRuntime::start(const QStringList &imports)
{
    if (std::exchange(m_started, true))
        return;
    if (!m_engine.property("echoPluginSdkRegistered").toBool()) {
        // importModule accepts filesystem/QResource names, not qrc: URL text.
        // Do not silently run plugins with an unavailable native SDK module.
        const auto sdk = m_engine.importModule(":/echo/plugins/sdk.mjs");
        if (sdk.isError() || !sdk.property("awaitOperation").isCallable())
            m_sdkError = "Cannot load PluginSdk Promise adapter";
        else {
            auto exports = m_engine.newObject();
            exports.setProperty("awaitOperation", sdk.property("awaitOperation"));
            exports.setProperty("pluginService", sdk.property("pluginService"));
            if (!m_engine.registerModule("echo:plugin-sdk", exports))
                m_sdkError = "Cannot register PluginSdk module";
            else
                m_engine.setProperty("echoPluginSdkRegistered", true);
        }
        if (m_engine.hasError()) {
            m_sdkError += ": " + m_engine.catchError().toString();
        }
    }
    ++m_imports;
    emit loadingChanged();
    auto *watcher = new QFutureWatcher<QPair<QVariantList, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, imports] {
        --m_imports;
        const auto result = watcher->result();
        watcher->deleteLater();
        if (m_closing) {
            checkClosed();
            return;
        }
        if (!result.second.isEmpty()) {
            m_sdkError = result.second;
            emit notice(result.second, true);
            emit loadingChanged();
            return;
        }
        for (const auto &value : result.first) {
            const auto record = value.toMap();
            importResolved(record.value("root").toString(), record.value("source").toString(),
                           record.value("installation").toString(),
                           record.value("enabled").toBool());
            if (auto *entry = find(record.value("installation").toString())) {
                entry->origin = record.value("origin").toMap();
                entry->transaction = record.value("transaction").toString();
            }
        }
        m_explicitImports = imports;
        m_startScanPending = true;
        QTimer::singleShot(0, this, &PluginRuntime::advanceRefresh);
    });
    watcher->setFuture(QtConcurrent::run([root = m_root] {
        auto error = PluginInstallTransaction::recover(root);
        auto records =
            error.isEmpty() ? PluginInstallationStore(root).records(&error) : QVariantList{};
        return qMakePair(records, error);
    }));
}
void PluginRuntime::importDirectory(const QString &source, bool enabled)
{
    if (m_closing || m_maintenance)
        return;
    const auto canonical = QFileInfo(source).canonicalFilePath();
    if (canonical.isEmpty())
        return;
    m_importEnabled[canonical] = enabled;
    if (!m_refreshSources.contains(canonical))
        m_refreshSources.append(canonical);
    m_refreshing = true;
    QTimer::singleShot(0, this, &PluginRuntime::advanceRefresh);
}
void PluginRuntime::importResolved(const QString &directory, const QString &source,
                                   const QString &installation, bool enabled)
{
    if (m_closing || installation.isEmpty() || find(installation))
        return;
    const int row = rowCount();
    beginInsertRows({}, row, row);
    auto entry = std::make_unique<Entry>();
    entry->source = source;
    entry->failure =
        m_preferences.value(SettingsKeys::Plugins::FailuresPrefix + installation).toMap();
    entry->enabled = enabled;
    entry->package.installationId = installation;
    auto *target = entry.get();
    m_entries.push_back(std::move(entry));
    endInsertRows();
    load(*target, directory);
}
void PluginRuntime::load(Entry &entry, const QString &directory)
{
    entry.state = Discovered;
    changed(entry);
    ++m_imports;
    auto *watcher = new QFutureWatcher<PluginPackage>(this);
    connect(watcher, &QFutureWatcher<PluginPackage>::finished, this,
            [this, watcher, target = &entry] {
                --m_imports;
                target->package = watcher->result();
                watcher->deleteLater();
                target->error = target->package.error;
                if (target->error.isEmpty() && !m_sdkError.isEmpty())
                    target->error = m_sdkError;
                target->state = target->error.isEmpty()        ? Disabled
                                : target->package.incompatible ? Incompatible
                                                               : Invalid;
                if (target->error.isEmpty()) {
                    for (const auto &other : m_entries)
                        if (other.get() != target && other->package.id == target->package.id &&
                            other->state != Invalid) {
                            target->state = Incompatible;
                            target->error = "Another installation owns this plugin ID";
                        }
                }
                if (target->error.isEmpty())
                    cleanImages(target->package.id);
                changed(*target);
                queueReconcile();
                checkClosed();
            });
    watcher->setFuture(QtConcurrent::run([directory, images = PluginPaths(m_root).images(),
                                          installation = entry.package.installationId] {
        return PluginPackage::importDirectory(directory, images, installation);
    }));
}
void PluginRuntime::refresh()
{
    if (m_closing || m_maintenance || loading())
        return;
    m_refreshNotice = true;
    discoverRoot(true);
}
void PluginRuntime::discoverRoot(bool reloadInstalled)
{
    m_startScanPending = false;
    m_refreshing = true;
    ++m_imports;
    emit loadingChanged();
    auto *watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, reloadInstalled] {
        --m_imports;
        watcher->deleteLater();
        auto discovered = watcher->result();
        for (const auto &entry : m_entries)
            discovered.removeAll(PluginPaths(m_root).code(entry->package.id));
        discovered.append(std::exchange(m_explicitImports, {}));
        for (const auto &path : discovered)
            if (!m_refreshSources.contains(path))
                m_refreshSources.append(path);
        if (reloadInstalled)
            for (const auto &entry : m_entries)
                m_refreshInstallations.append(entry->package.installationId);
        advanceRefresh();
        checkClosed();
    });
    watcher->setFuture(QtConcurrent::run([root = m_root] {
        QStringList sources;
        QDir directory(root);
        if (!directory.mkpath("."))
            return sources;
        for (const auto &name :
             directory.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
            const auto path = rootSource(directory.filePath(name), root);
            if (!path.isEmpty() &&
                (QFileInfo(path).isFile() || QFileInfo(path + "/manifest.json").isFile()))
                sources.append(path);
        }
        return sources;
    }));
}
void PluginRuntime::advanceRefresh()
{
    if (m_maintenance)
        return;
    if (m_closing) {
        m_startScanPending = m_refreshing = false;
        m_refreshSources.clear();
        m_refreshInstallations.clear();
        return;
    }
    if (m_imports)
        return;
    for (const auto &entry : m_entries)
        if (entry->state == Discovered || entry->state == Activating ||
            entry->state == Deactivating || entry->mutation != Entry::None)
            return;
    if (m_startScanPending) {
        discoverRoot(false);
        return;
    }
    if (!m_refreshing)
        return;
    while (!m_refreshSources.isEmpty()) {
        const auto path = m_refreshSources.takeFirst();
        if (installDirectory(
                path, {{"kind", "local"}, {QFileInfo(path).isFile() ? "file" : "directory", path}}))
            return;
        emit managementFinished({}, false, tr("无法导入插件目录：%1").arg(path));
    }
    while (!m_refreshInstallations.isEmpty()) {
        if (reload(m_refreshInstallations.takeFirst()))
            return;
    }
    m_refreshing = false;
    queueReconcile();
    emit loadingChanged();
    if (std::exchange(m_refreshNotice, false))
        emit notice(tr("插件已刷新"), false);
}
} // namespace EchoMusic
