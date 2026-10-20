#include "SettingsKeys.h"
#include "PluginRuntime.h"
#include "PluginDataTransaction.h"
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
    QStringList normalizedImports;
    for (const auto &source : imports) {
        const auto canonical = QFileInfo(source).canonicalFilePath();
        const auto identity =
            canonical.isEmpty() ? QFileInfo(source).absoluteFilePath() : canonical;
        if (!normalizedImports.contains(identity))
            normalizedImports.append(identity);
    }
    const auto catalog =
        m_preferences.value(EchoMusic::SettingsKeys::Plugins::Installations).toList();
    for (const auto &value : catalog) {
        const auto record = value.toMap();
        const auto source = record.value("source").toString();
        if (normalizedImports.contains(source))
            continue;
        importResolved(record.value("root").toString(), source,
                       record.value("installation").toString(), record.value("enabled").toBool());
        if (auto *entry = find(record.value("installation").toString()))
            entry->origin = record.value("origin").toMap();
    }
    for (const auto &source : normalizedImports)
        importDirectory(source, true);
    m_startScanPending = true;
    QTimer::singleShot(0, this, &PluginRuntime::advanceRefresh);
}
void PluginRuntime::importDirectory(const QString &source, bool enabled)
{
    if (m_closing || m_maintenance)
        return;
    const auto canonical = QFileInfo(source).canonicalFilePath();
    const auto identity = canonical.isEmpty() ? QFileInfo(source).absoluteFilePath() : canonical;
    const auto settingsKey =
        EchoMusic::SettingsKeys::Plugins::SourcePrefix +
        QString::fromLatin1(
            QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
    auto installation = m_preferences.value(settingsKey).toString();
    if (installation.isEmpty()) {
        installation = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_preferences.setValue(settingsKey, installation);
    }
    importResolved(identity, identity, installation, enabled);
    if (auto *entry = find(installation)) {
        if (const auto owned = rootSource(source, m_root); !owned.isEmpty())
            entry->origin = {{"kind", "local"},
                             {QFileInfo(owned).isFile() ? "file" : "directory", owned}};
    }
}
void PluginRuntime::importResolved(const QString &directory, const QString &source,
                                   const QString &installation, bool enabled)
{
    if (m_closing || installation.isEmpty())
        return;
    if (find(installation))
        return;
    const int row = rowCount();
    beginInsertRows({}, row, row);
    auto entry = std::make_unique<Entry>();
    entry->source = source;
    entry->failure =
        m_preferences.value(EchoMusic::SettingsKeys::Plugins::FailuresPrefix + installation)
            .toMap();
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
    auto *target = &entry;
    const auto installation = entry.package.installationId;
    const bool recover =
        m_preferences.value(EchoMusic::SettingsKeys::Plugins::RollbackPrefix + installation, false)
            .toBool();
    ++m_imports;
    auto *watcher = new QFutureWatcher<PluginPackage>(this);
    connect(watcher, &QFutureWatcher<PluginPackage>::finished, this,
            [this, watcher, target, recover] {
                --m_imports;
                auto package = watcher->result();
                // Startup restoration retains the original installation identity.
                package.installationId = target->package.installationId;
                target->package = std::move(package);
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
                            other->state != Invalid && other->state != Incompatible) {
                            target->state = Incompatible;
                            target->error = "Another installation owns this plugin ID";
                        }
                }
                if (recover && target->error.isEmpty()) {
                    m_preferences.remove(EchoMusic::SettingsKeys::Plugins::RollbackPrefix +
                                         target->package.installationId);
                }
                persist();
                if (recover && target->error.isEmpty()) {
                    const auto backup = m_root + "/transactions/" + target->package.installationId;
                    discardSnapshot(backup);
                }
                changed(*target);
                queueReconcile();
                checkClosed();
            });
    const auto packages = m_root + "/packages";
    watcher->setFuture(
        QtConcurrent::run([directory, packages, installation, recover, root = m_root] {
            if (recover) {
                const auto error = PluginDataTransaction::restore(
                    root + "/data/" + installation, root + "/transactions/" + installation);
                if (!error.isEmpty()) {
                    PluginPackage failed;
                    failed.installationId = installation;
                    failed.error = error;
                    return failed;
                }
            }
            return PluginPackage::importDirectory(directory, packages, installation);
        }));
}
void PluginRuntime::refresh()
{
    if (m_closing || m_maintenance || loading())
        return;
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
        m_refreshSources = watcher->result();
        watcher->deleteLater();
        if (!reloadInstalled) {
            for (const auto &entry : m_entries)
                m_refreshSources.removeAll(
                    entry->origin.value("file", entry->origin.value("directory")).toString());
            const auto sources = std::exchange(m_refreshSources, {});
            m_refreshing = false;
            for (const auto &path : sources)
                importDirectory(path);
            queueReconcile();
        }
        if (reloadInstalled) {
            for (const auto &entry : m_entries) {
                // A root-folder update already reloads this installation through
                // the data snapshot/activation rollback transaction.
                if (!m_refreshSources.contains(
                        entry->origin.value("file", entry->origin.value("directory")).toString()))
                    m_refreshInstallations.append(entry->package.installationId);
            }
        }
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
        if (installDirectory(path, {{"kind", "local"}, {"directory", path}}))
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
}
} // namespace EchoMusic
