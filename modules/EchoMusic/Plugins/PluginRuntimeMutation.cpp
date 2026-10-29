#include "PluginRuntime.h"
#include "ThemeStore.h"
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
    const auto inputPath = QFileInfo(directory).canonicalFilePath();
    const bool requestedEnabled =
        m_importEnabled.contains(inputPath) ? m_importEnabled.take(inputPath) : true;
    if (QFileInfo(directory).isFile()) {
        if (!directory.endsWith(".echomod", Qt::CaseInsensitive))
            return false;
        const auto stage = QSharedPointer<QTemporaryDir>::create();
        ++m_imports;
        emit loadingChanged();
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcherBase::finished, this,
                [this, watcher, stage, origin, expected, requestedEnabled] {
                    --m_imports;
                    auto error = watcher->result();
                    watcher->deleteLater();
                    if (!m_closing && error.isEmpty())
                        m_importEnabled[QFileInfo(stage->path()).canonicalFilePath()] =
                            requestedEnabled;
                    if (m_closing || !error.isEmpty() ||
                        !installDirectory(stage->path(), origin, expected, stage))
                        emit managementFinished(
                            {}, false, error.isEmpty() ? tr("无法安装 .echomod 插件包") : error);
                    queueReconcile();
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
    if (installationOrigin.isEmpty())
        installationOrigin = {{"kind", "local"}, {"name", tr("本地安装")}};
    QFile manifestFile(directory + "/manifest.json");
    if (!manifestFile.open(QIODevice::ReadOnly) || manifestFile.size() > 256 * 1024)
        return false;
    const auto descriptor = PluginPackage::inspectManifest(manifestFile.readAll());
    if (!descriptor.error.isEmpty() || (!expected.isEmpty() && descriptor.manifest != expected))
        return false;
    const auto existing = installationForId(descriptor.id);
    auto *target = find(existing);
    const auto plan = dependencyPlan(&descriptor);
    if (target && plan.errors.contains(descriptor.id))
        return false;
    for (const auto &other : m_entries)
        if (other->context && plan.errors.contains(other->package.id))
            return false;
    if (target && identityKey(descriptor.id, target->origin) !=
                      identityKey(descriptor.id, installationOrigin))
        return false;
    if (target && (target->mutation != Entry::None || target->state == Discovered ||
                   target->state == Activating || target->state == Deactivating))
        return false;
    const auto installation =
        existing.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : existing;
    ++m_imports;
    emit managementStarted(installation, descriptor.id);
    emit loadingChanged();
    auto *watcher = new QFutureWatcher<PluginPackage>(this);
    connect(watcher, &QFutureWatcher<PluginPackage>::finished, this,
            [this, watcher, existing, installation, installationOrigin, expected, directory,
             externalSource = !sourceLease, requestedEnabled] {
                --m_imports;
                auto package = watcher->result();
                watcher->deleteLater();
                if (m_closing || !package.error.isEmpty() ||
                    (!expected.isEmpty() && package.manifest != expected)) {
                    if (PluginPaths::validId(package.id))
                        cleanImages(package.id);
                    emit managementFinished(installation, false,
                                            m_closing                 ? tr("应用正在关闭")
                                            : package.error.isEmpty() ? tr("安装过程中清单已变更")
                                                                      : package.error);
                    queueReconcile();
                    emit loadingChanged();
                    checkClosed();
                    return;
                }
                if (existing.isEmpty()) {
                    const auto error =
                        PluginInstallationStore(m_root).prepareData(package.id, installationOrigin);
                    if (!error.isEmpty()) {
                        emit managementFinished(installation, false, error);
                        queueReconcile();
                        emit loadingChanged();
                        return;
                    }
                }
                auto *entry = find(existing);
                if (!entry) {
                    beginInsertRows({}, rowCount(), rowCount());
                    auto created = std::make_unique<Entry>();
                    created->package.id = package.id;
                    created->package.installationId = installation;
                    created->enabled = requestedEnabled;
                    created->newInstall = true;
                    entry = created.get();
                    m_entries.push_back(std::move(created));
                    endInsertRows();
                }
                entry->previousPackage = entry->package;
                entry->previousOrigin = entry->origin;
                entry->previousSource = entry->source;
                entry->previousTransaction = entry->transaction;
                entry->pendingPackage = std::move(package);
                entry->origin = installationOrigin;
                entry->importedInput = rootSource(directory, m_root);
                const auto canonicalCode = PluginPaths(m_root).code(entry->pendingPackage.id);
                if (entry->importedInput == canonicalCode)
                    entry->importedInput.clear();
                if (!entry->importedInput.isEmpty() && QFileInfo(entry->importedInput).isDir())
                    entry->origin["directory"] = canonicalCode;
                const auto rootFile =
                    rootSource(installationOrigin.value("file").toString(), m_root);
                if (!rootFile.isEmpty())
                    entry->importedInput = rootFile;
                // Only external developer sources remain live inputs for explicit reload.
                const auto external = externalSource && rootSource(directory, m_root).isEmpty() &&
                                      installationOrigin.value("kind") != "marketplace" &&
                                      !expected.size();
                entry->source =
                    external ? directory : PluginPaths(m_root).code(entry->pendingPackage.id);
                entry->mutation = Entry::Updating;
                entry->applying = false;
                if (entry->context)
                    retire(*entry, Disabled);
                else
                    applyMutation(*entry);
                queueReconcile();
            });
    watcher->setFuture(
        QtConcurrent::run([root = m_root, id = descriptor.id, directory,
                           images = PluginPaths(m_root).images(), installation, sourceLease] {
            Q_UNUSED(sourceLease)
            const auto error = PluginInstallTransaction::recover(root, id);
            if (!error.isEmpty()) {
                PluginPackage failed;
                failed.id = id;
                failed.error = error;
                return failed;
            }
            return PluginPackage::importDirectory(directory, images, installation);
        }));
    return true;
}
void PluginRuntime::applyMutation(Entry &entry)
{
    if (entry.mutation == Entry::Removing) {
        removeEntry(entry);
        return;
    }
    if (entry.mutation == Entry::Clearing) {
        m_fileService->forgetInstallation(entry.package.installationId);
        m_preferences.remove(SettingsKeys::Plugins::WindowBoundsPrefix +
                             entry.package.installationId);
        m_host.theme.extensions()->removeInstallation(entry.package.installationId);
        ++m_imports;
        auto *watcher = new QFutureWatcher<bool>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, target = &entry] {
            --m_imports;
            const auto ok = watcher->result();
            watcher->deleteLater();
            target->mutation = Entry::None;
            target->state = ok ? Disabled : Failed;
            target->error = ok ? QString() : tr("无法清除插件数据");
            changed(*target);
            emit notice(ok ? tr("插件数据已清空") : target->error, !ok);
            queueReconcile();
            checkClosed();
        });
        watcher->setFuture(QtConcurrent::run([path = PluginPaths(m_root).data(entry.package.id)] {
            return !QFileInfo(path).isSymLink() && QDir(path).removeRecursively();
        }));
        return;
    }
    const bool snapshot = entry.mutation == Entry::Updating && !entry.snapshotReady;
    const bool restore = entry.mutation == Entry::Rollback && !entry.dataRestored;
    if (snapshot || restore) {
        ++m_imports;
        changed(entry);
        auto *watcher = new QFutureWatcher<QPair<QString, QString>>(this);
        connect(watcher, &QFutureWatcherBase::finished, this,
                [this, watcher, target = &entry, snapshot] {
                    --m_imports;
                    const auto result = watcher->result();
                    watcher->deleteLater();
                    if (snapshot)
                        target->transaction = result.first;
                    if (m_closing) {
                        checkClosed();
                        return;
                    }
                    if (!result.second.isEmpty()) {
                        target->transactionError = result.second;
                        if (snapshot) {
                            target->snapshotReady = true;
                            target->dataRestored = result.first.isEmpty();
                            target->mutation = Entry::Rollback;
                            target->pendingPackage = target->previousPackage;
                            target->origin = target->previousOrigin;
                            target->source = target->previousSource;
                            applyMutation(*target);
                        } else {
                            // Keep the journal and stop. Never activate partially restored
                            // code/data.
                            target->package = target->previousPackage;
                            target->state = Failed;
                            target->error = result.second;
                            target->applying = false;
                            changed(*target);
                            emit managementFinished(target->package.installationId, false,
                                                    result.second);
                        }
                        queueReconcile();
                        return;
                    }
                    if (snapshot)
                        target->snapshotReady = true;
                    else
                        target->dataRestored = true;
                    applyMutation(*target);
                    queueReconcile();
                });
        watcher->setFuture(QtConcurrent::run([root = m_root, next = entry.pendingPackage,
                                              previous = entry.previousPackage,
                                              transaction = entry.transaction, snapshot] {
            return snapshot ? PluginInstallTransaction::begin(root, next, previous)
                            : qMakePair(transaction,
                                        PluginInstallTransaction::rollback(root, transaction));
        }));
        return;
    }
    entry.applying = true;
    entry.package = entry.pendingPackage;
    entry.error.clear();
    entry.state = Disabled;
    changed(entry);
    if (entry.newInstall && entry.mutation == Entry::Rollback) {
        entry.state = Failed;
        finishMutation(entry);
    } else if (entry.enabled && !m_safeMode)
        queueReconcile();
    else
        finishMutation(entry);
}
void PluginRuntime::finishMutation(Entry &entry)
{
    if (entry.mutation == Entry::None || !entry.applying ||
        (entry.context && entry.state != Active))
        return;
    if ((entry.state == Failed || (entry.state == Blocked && !entry.newInstall)) &&
        entry.mutation == Entry::Updating) {
        entry.transactionError = entry.error;
        entry.mutation = Entry::Rollback;
        entry.applying = false;
        entry.pendingPackage = entry.previousPackage;
        entry.origin = entry.previousOrigin;
        entry.source = entry.previousSource;
        applyMutation(entry);
        return;
    }
    const bool success = entry.mutation == Entry::Updating;
    const auto error = entry.transactionError;
    const auto installation = entry.package.installationId;
    const auto id = entry.package.id;
    const auto transaction = entry.transaction;
    const auto input = entry.importedInput;
    const auto oldMutation = entry.mutation;
    if (!success)
        entry.transaction = entry.previousTransaction;
    entry.mutation = Entry::None;
    if (entry.newInstall && !success) {
        m_host.permissions->forgetInstallation(installation);
        m_fileService->forgetInstallation(installation);
        m_preferences.remove(SettingsKeys::Plugins::FailuresPrefix + installation);
        for (size_t row = 0; row < m_entries.size(); ++row)
            if (m_entries[row].get() == &entry) {
                beginRemoveRows({}, int(row), int(row));
                m_entries.erase(m_entries.begin() + row);
                endRemoveRows();
                break;
            }
        emit catalogChanged();
    } else {
        if (!persist()) {
            entry.mutation = oldMutation;
            entry.transaction = transaction;
            entry.transactionError = m_storeError;
            if (success) {
                if (entry.context)
                    retire(entry, Failed, m_storeError);
                else {
                    entry.state = Failed;
                    finishMutation(entry);
                }
            } else {
                entry.mutation = Entry::None;
                if (entry.context)
                    retire(entry, Failed, m_storeError);
                else
                    entry.state = Failed;
                entry.error = m_storeError;
                changed(entry);
                emit managementFinished(installation, false, m_storeError);
            }
            return;
        }
        entry.transaction = QFileInfo(entry.transaction).fileName();
        entry.applying = false;
        entry.newInstall = false;
        entry.previousPackage = {};
        entry.pendingPackage = {};
        entry.previousOrigin.clear();
        entry.previousSource.clear();
        entry.previousTransaction.clear();
        entry.transactionError.clear();
        entry.snapshotReady = false;
        entry.dataRestored = false;
        changed(entry);
        if (success) {
            emit catalogChanged();
            // Root packages are consumed only after a committed installation.
            const auto file = entry.origin.value("file").toString();
            if (!file.isEmpty() && rootSource(file, m_root) == file)
                QFile::remove(file);
        }
    }
    auto notify = [this, id, installation, success, error] {
        cleanImages(id, [this, installation, success, error] {
            emit managementFinished(installation, success, error);
        });
    };
    if (!transaction.isEmpty())
        discardSnapshot(transaction, notify, success ? input : QString());
    else
        notify();
}
bool PluginRuntime::uninstall(const QString &installation, bool removeData)
{
    auto *entry = find(installation);
    if (!entry || !PluginPaths::validId(entry->package.id) || m_closing || m_maintenance ||
        m_imports || entry->mutation != Entry::None || entry->state == Activating ||
        entry->state == Deactivating || entry->state == Discovered)
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
    const auto installation = entry.package.installationId, id = entry.package.id;
    auto record = installationRecord(entry);
    record["name"] = entry.package.name;
    const bool removeData = entry.removeData;
    ++m_imports;
    emit loadingChanged();
    auto *watcher = new QFutureWatcher<QPair<QString, QString>>(this);
    connect(
        watcher, &QFutureWatcherBase::finished, this,
        [this, watcher, target = &entry, installation, id, record, removeData] {
            const auto result = watcher->result();
            watcher->deleteLater();
            auto error = result.second;
            if (error.isEmpty())
                error = PluginInstallationStore(m_root).detach(record, !removeData);
            if (!error.isEmpty()) {
                auto *rollback = new QFutureWatcher<QString>(this);
                connect(rollback, &QFutureWatcherBase::finished, this,
                        [this, rollback, target, installation, error] {
                            --m_imports;
                            const auto failure = rollback->result();
                            rollback->deleteLater();
                            target->mutation = Entry::None;
                            target->state = failure.isEmpty() ? Disabled : Failed;
                            target->error = failure;
                            changed(*target);
                            emit managementFinished(
                                installation, false,
                                error + (failure.isEmpty() ? QString() : "; rollback: " + failure));
                            queueReconcile();
                            checkClosed();
                        });
                rollback->setFuture(QtConcurrent::run([root = m_root, transaction = result.first] {
                    if (transaction.isEmpty())
                        return QString();
                    const auto failure = PluginInstallTransaction::rollback(root, transaction);
                    if (failure.isEmpty())
                        return PluginInstallTransaction::discard(transaction);
                    return failure;
                }));
                return;
            }
            m_host.theme.extensions()->removeInstallation(installation);
            m_fileService->forgetInstallation(installation);
            m_host.permissions->forgetInstallation(installation);
            int row = 0;
            while (m_entries[size_t(row)].get() != target)
                ++row;
            beginRemoveRows({}, row, row);
            m_entries.erase(m_entries.begin() + row);
            endRemoveRows();
            emit catalogChanged();
            m_preferences.remove(SettingsKeys::Plugins::FailuresPrefix + installation);
            auto *cleanup = new QFutureWatcher<QString>(this);
            connect(cleanup, &QFutureWatcherBase::finished, this,
                    [this, cleanup, installation, id] {
                        --m_imports;
                        const auto failure = cleanup->result();
                        cleanup->deleteLater();
                        cleanImages(id, [this, installation, failure] {
                            emit managementFinished(installation, failure.isEmpty(), failure);
                        });
                        queueReconcile();
                        emit loadingChanged();
                        checkClosed();
                    });
            cleanup->setFuture(QtConcurrent::run([root = m_root, transaction = result.first] {
                return PluginInstallTransaction::finishRemoval(root, transaction);
            }));
        });
    watcher->setFuture(QtConcurrent::run([root = m_root, id, installation, removeData] {
        return PluginInstallTransaction::remove(root, id, installation, removeData);
    }));
}
void PluginRuntime::discardSnapshot(const QString &transaction, std::function<void()> complete,
                                    const QString &input)
{
    ++m_imports;
    emit loadingChanged();
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, complete] {
        --m_imports;
        const auto error = watcher->result();
        watcher->deleteLater();
        if (!error.isEmpty())
            emit notice(error, true);
        queueReconcile();
        emit loadingChanged();
        if (complete)
            complete();
        checkClosed();
    });
    watcher->setFuture(QtConcurrent::run([transaction, input, root = m_root] {
        const auto error = PluginInstallTransaction::discard(transaction);
        if (!error.isEmpty())
            return error;
        if (!input.isEmpty() && rootSource(input, root) == input) {
            const auto removed =
                QFileInfo(input).isDir() ? QDir(input).removeRecursively() : QFile::remove(input);
            if (!removed)
                return QStringLiteral("Cannot consume imported plugin input");
        }
        return QString();
    }));
}
void PluginRuntime::cleanImages(const QString &id, std::function<void()> completed)
{
    if (!PluginPaths::validId(id)) {
        if (completed)
            completed();
        return;
    }
    QStringList keep;
    for (const auto &entry : m_entries)
        for (const auto &package : {entry->package, entry->previousPackage, entry->pendingPackage})
            if (package.id == id && !package.root.isEmpty())
                keep.append(package.root);
    ++m_imports;
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, completed] {
        --m_imports;
        watcher->deleteLater();
        queueReconcile();
        emit loadingChanged();
        if (completed)
            completed();
        checkClosed();
    });
    watcher->setFuture(QtConcurrent::run([directory = PluginPaths(m_root).images() + '/' + id,
                                          keep] {
        for (const auto &file : QDir(directory).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (!keep.contains(file.canonicalFilePath()) && !file.isSymLink())
                QDir(file.filePath()).removeRecursively();
    }));
}
} // namespace EchoMusic
