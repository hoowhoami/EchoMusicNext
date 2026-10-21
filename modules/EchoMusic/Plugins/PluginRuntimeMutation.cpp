#include "SettingsKeys.h"
#include "PluginRuntime.h"
#include "ThemeStore.h"
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

bool PluginRuntime::installDirectory(const QString &directory, const QVariantMap &origin,
                                     const QVariantMap &expected,
                                     QSharedPointer<QTemporaryDir> sourceLease)
{
    if (m_closing || m_maintenance || m_imports)
        return false;
    if (QFileInfo(directory).isFile()) {
        if (!directory.endsWith(".echomod", Qt::CaseInsensitive))
            return false;
        const auto stage = QSharedPointer<QTemporaryDir>::create();
        ++m_imports;
        emit loadingChanged();
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcherBase::finished, this,
                [this, watcher, stage, origin, expected] {
                    --m_imports;
                    queueReconcile();
                    const auto error = watcher->result();
                    watcher->deleteLater();
                    if (m_closing || !error.isEmpty() ||
                        !installDirectory(stage->path(), origin, expected, stage))
                        emit managementFinished(
                            {}, false, error.isEmpty() ? tr("无法安装 .echomod 插件包") : error);
                    emit loadingChanged();
                    checkClosed();
                });
        watcher->setFuture(QtConcurrent::run([directory, stage] {
            return stage->isValid() ? PluginPackage::extractArchive(directory, stage->path())
                                    : QStringLiteral("Cannot create plugin staging directory");
        }));
        return true;
    }
    const auto sourcePath = QFileInfo(directory).canonicalFilePath();
    const auto rootPath = QFileInfo(m_root).canonicalFilePath();
    if (!rootPath.isEmpty() && (sourcePath == rootPath || sourcePath.startsWith(rootPath + '/')) &&
        rootSource(directory, m_root).isEmpty())
        return false;
    auto installationOrigin = origin;
    if (const auto owned = rootSource(directory, m_root); !owned.isEmpty()) {
        installationOrigin = {{"kind", "local"}, {"directory", owned}};
    }
    QFile manifestFile(directory + "/manifest.json");
    if (!manifestFile.open(QIODevice::ReadOnly) || manifestFile.size() > 256 * 1024)
        return false;
    const auto manifestBytes = manifestFile.readAll();
    const auto descriptor = PluginPackage::inspectManifest(manifestBytes);
    if (!descriptor.error.isEmpty() || (!expected.isEmpty() && descriptor.manifest != expected))
        return false;
    const auto existing = installationForId(descriptor.id);
    auto *target = find(existing);
    const auto plan = dependencyPlan(&descriptor);
    if (target && plan.errors.contains(descriptor.id))
        return false;
    for (const auto &other : m_entries)
        if (other->context && plan.errors.contains(other->package.id))
            return false; // A provider update cannot silently break live consumers.
    if (target && (identityKey(descriptor.id, target->origin) !=
                   identityKey(descriptor.id, installationOrigin)))
        return false;
    if (target && (target->mutation != Entry::None || target->state == Discovered ||
                   target->state == Activating || target->state == Deactivating))
        return false;
    auto installation =
        existing.isEmpty()
            ? m_preferences.value(identityKey(descriptor.id, installationOrigin)).toString()
            : existing;
    if (installation.isEmpty())
        installation = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ++m_imports;
    emit managementStarted(installation, descriptor.id);
    emit loadingChanged();
    auto *watcher = new QFutureWatcher<PluginPackage>(this);
    connect(watcher, &QFutureWatcher<PluginPackage>::finished, this,
            [this, watcher, existing, installation, installationOrigin, expected] {
                --m_imports;
                queueReconcile();
                auto package = watcher->result();
                watcher->deleteLater();
                if (m_closing || !package.error.isEmpty() ||
                    (!expected.isEmpty() && package.manifest != expected)) {
                    emit managementFinished(installation, false,
                                            m_closing                 ? tr("应用正在关闭")
                                            : package.error.isEmpty() ? tr("安装过程中清单已变更")
                                                                      : package.error);
                    emit loadingChanged();
                    checkClosed();
                    return;
                }
                auto *entry = find(existing);
                if (!entry) {
                    beginInsertRows({}, rowCount(), rowCount());
                    auto created = std::make_unique<Entry>();
                    created->package.installationId = installation;
                    created->enabled = true;
                    created->newInstall = true;
                    entry = created.get();
                    m_entries.push_back(std::move(created));
                    endInsertRows();
                }
                entry->previousPackage = entry->package;
                entry->previousOrigin = entry->origin;
                entry->previousSource = entry->source;
                entry->pendingPackage = std::move(package);
                entry->origin = installationOrigin;
                entry->mutation = Entry::Updating;
                entry->applying = false;
                if (entry->context)
                    retire(*entry, Disabled);
                else
                    applyMutation(*entry);
            });
    watcher->setFuture(
        QtConcurrent::run([directory, packages = m_root + "/packages", installation, sourceLease] {
            // Keep downloaded input alive even if its manager is destroyed while copying.
            Q_UNUSED(sourceLease)
            return PluginPackage::importDirectory(directory, packages, installation);
        }));
    return true;
}
void PluginRuntime::applyMutation(Entry &entry)
{
    if (entry.mutation == Entry::Removing) {
        removeEntry(entry);
        return;
    }
    const bool snapshot =
        entry.mutation == Entry::Updating && !entry.newInstall && !entry.snapshotReady;
    const bool restore = entry.mutation == Entry::Rollback && !entry.dataRestored;
    if (snapshot || restore) {
        ++m_imports;
        changed(entry);
        const auto installation = entry.package.installationId;
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcherBase::finished, this,
                [this, watcher, target = &entry, snapshot, installation] {
                    --m_imports;
                    queueReconcile();
                    const auto error = watcher->result();
                    watcher->deleteLater();
                    if (m_closing) {
                        checkClosed();
                        return;
                    }
                    if (!error.isEmpty()) {
                        target->transactionError = error;
                        if (snapshot) {
                            target->snapshotReady = true;
                            target->dataRestored = true;
                            target->mutation = Entry::Rollback;
                            target->pendingPackage = target->previousPackage;
                            target->origin = target->previousOrigin;
                            applyMutation(*target);
                        } else {
                            target->package = target->previousPackage;
                            target->origin = target->previousOrigin;
                            target->source = target->previousSource;
                            target->state = Failed;
                            target->error = error;
                            target->applying = true;
                            finishMutation(*target);
                        }
                        return;
                    }
                    if (snapshot) {
                        target->snapshotReady = true;
                        m_preferences.setValue(
                            EchoMusic::SettingsKeys::Plugins::RollbackPrefix + installation, true);
                        m_preferences.sync();
                        if (m_preferences.status() != QSettings::NoError) {
                            target->transactionError = "Cannot persist update rollback journal";
                            target->mutation = Entry::Rollback;
                            target->pendingPackage = target->previousPackage;
                            target->origin = target->previousOrigin;
                        }
                    } else
                        target->dataRestored = true;
                    applyMutation(*target);
                });
        watcher->setFuture(QtConcurrent::run([root = m_root, installation, snapshot] {
            QDir().mkpath(root + "/transactions");
            const auto data = root + "/data/" + installation;
            const auto backup = root + "/transactions/" + installation;
            return snapshot ? PluginDataTransaction::capture(data, backup)
                            : PluginDataTransaction::restore(data, backup);
        }));
        return;
    }
    entry.applying = true;
    entry.package = entry.pendingPackage;
    entry.source = entry.package.root;
    entry.error.clear();
    entry.state = Disabled;
    changed(entry);
    if (entry.enabled && !m_safeMode)
        queueReconcile();
    else
        finishMutation(entry);
}
void PluginRuntime::finishMutation(Entry &entry)
{
    if (entry.mutation == Entry::None || !entry.applying ||
        (entry.context && entry.state != Active))
        return;
    if ((entry.state == Failed || entry.state == Blocked) && entry.mutation == Entry::Updating &&
        !entry.newInstall) {
        entry.transactionError = entry.error;
        entry.mutation = Entry::Rollback;
        entry.pendingPackage = entry.previousPackage;
        entry.origin = entry.previousOrigin;
        applyMutation(entry);
        return;
    }
    const auto mutation = entry.mutation;
    bool success = mutation == Entry::Updating && entry.state != Failed;
    auto error = entry.transactionError.isEmpty() ? entry.error : entry.transactionError;
    const auto installation = entry.package.installationId;
    const bool retainedSnapshot = entry.snapshotReady && (entry.dataRestored || success);
    // Publish catalog and removal of the rollback journal in one atomic sync.
    // Keep both versions and the data snapshot until that durable commit succeeds.
    entry.mutation = Entry::None;
    m_preferences.setValue(identityKey(entry.package.id, entry.origin), installation);
    if (retainedSnapshot)
        m_preferences.remove(EchoMusic::SettingsKeys::Plugins::RollbackPrefix + installation);
    persist();
    if (m_preferences.status() != QSettings::NoError) {
        if (entry.snapshotReady)
            m_preferences.setValue(EchoMusic::SettingsKeys::Plugins::RollbackPrefix + installation,
                                   true);
        error = "Cannot persist plugin installation metadata";
        // A rollback can also have an active restored context. If its metadata
        // cannot commit, retire that code before reporting transaction failure;
        // this is a real storage error, independent of process-exit detection.
        if (success || entry.context) {
            entry.mutation = mutation;
            entry.transactionError = error;
            if (entry.context)
                retire(entry, Failed, error);
            else {
                entry.state = Failed;
                entry.error = error;
                finishMutation(entry);
            }
            return;
        }
        success = false;
    }
    entry.applying = false;
    entry.newInstall = false;
    entry.previousPackage = {};
    entry.pendingPackage = {};
    entry.previousOrigin.clear();
    entry.previousSource.clear();
    entry.transactionError.clear();
    entry.snapshotReady = false;
    entry.dataRestored = false;
    changed(entry);
    auto notify = [this, installation, success, error] {
        emit managementFinished(installation, success, error);
    };
    if (retainedSnapshot && m_preferences.status() == QSettings::NoError)
        discardSnapshot(m_root + "/transactions/" + installation, notify);
    else
        notify();
}

bool PluginRuntime::uninstall(const QString &installation, bool removeData)
{
    auto *entry = find(installation);
    if (!entry || m_closing || m_maintenance || m_imports || entry->mutation != Entry::None ||
        entry->state == Activating || entry->state == Deactivating || entry->state == Discovered)
        return false;
    entry->mutation = Entry::Removing;
    entry->removeData = removeData;
    entry->applying = false;
    if (entry->context)
        retire(*entry, Disabled);
    else
        removeEntry(*entry);
    return true;
}
void PluginRuntime::removeEntry(Entry &entry)
{
    const auto installation = entry.package.installationId;
    m_host.theme.extensions()->removeInstallation(installation);
    // Atomically detach the catalog after all JS leases and SQL jobs have drained.
    // Package reclamation can fail independently without restoring executable state.
    int row = 0;
    while (m_entries[size_t(row)].get() != &entry)
        ++row;
    const bool removeData = entry.removeData;
    const auto key = identityKey(entry.package.id, entry.origin);
    const auto input =
        entry.origin.value("kind") == "local"
            ? rootSource(entry.origin.value("file", entry.origin.value("directory")).toString(),
                         m_root)
            : QString();
    beginRemoveRows({}, row, row);
    m_entries.erase(m_entries.begin() + row);
    endRemoveRows();
    if (removeData)
        m_preferences.remove(key);
    m_preferences.remove(EchoMusic::SettingsKeys::Plugins::FailuresPrefix + installation);
    persist();
    emit catalogChanged();
    ++m_imports;
    emit loadingChanged();
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, installation] {
        --m_imports;
        queueReconcile();
        const bool ok = watcher->result();
        watcher->deleteLater();
        emit managementFinished(installation, ok,
                                ok ? QString() : tr("插件已卸载，但部分文件无法清理"));
        emit loadingChanged();
        checkClosed();
    });
    watcher->setFuture(QtConcurrent::run([root = m_root, installation, removeData, input] {
        const bool source =
            input.isEmpty() || !QFileInfo::exists(input) ||
            (rootSource(input, root) == input &&
             (QFileInfo(input).isFile() ? QFile::remove(input) : QDir(input).removeRecursively()));
        const bool packages = QDir(root + "/packages/" + installation).removeRecursively();
        const bool data = !removeData || QDir(root + "/data/" + installation).removeRecursively();
        return source && packages && data;
    }));
}
void PluginRuntime::discardSnapshot(const QString &backup, std::function<void()> complete)
{
    ++m_imports;
    emit loadingChanged();
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, complete] {
        --m_imports;
        // A provider may have committed while snapshot cleanup still held the
        // import barrier. Resume its waiting consumers when that barrier drops.
        queueReconcile();
        watcher->deleteLater();
        emit loadingChanged();
        if (complete)
            complete();
        checkClosed();
    });
    watcher->setFuture(QtConcurrent::run([backup] { PluginDataTransaction::discard(backup); }));
}
} // namespace EchoMusic
