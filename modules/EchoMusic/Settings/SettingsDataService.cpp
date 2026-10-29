#include "PluginInstallationStore.h"
#include "PluginInstallTransaction.h"
#include "SettingsKeys.h"
#include "SettingsDataService.h"
#include "PluginRuntime.h"
#include "SettingsBackupRestore.h"
#include "StorageService.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtConcurrent>
#include <QUuid>
#include <QDateTime>

namespace EchoMusic
{
SettingsDataService::SettingsDataService(EchoMusic::SettingsStore &settings, PluginRuntime &plugins,
                                         RelaunchService &relaunch, NetworkPreferences &network,
                                         QString root, QString version, QObject *parent)
    : BackupHost(parent), m_settings(settings), m_plugins(plugins), m_relaunch(relaunch),
      m_network(network), m_root(canonicalStoragePath(root)), m_version(std::move(version))
{
    m_workers.setMaxThreadCount(1);
    m_tokenSweep.setInterval(30000);
    connect(&m_tokenSweep, &QTimer::timeout, this, [this] {
        const auto now = QDateTime::currentMSecsSinceEpoch();
        for (const auto &token : m_inspections.keys())
            if (m_inspections[token].expires <= now)
                clearToken(token);
    });
    connect(this, &BackupHost::providerNotice, this, &SettingsDataService::notice);
    connect(this, &BackupHost::providerInspection, this, [this](const QVariantMap &result) {
        adoptProviderInspection(result.value("token").toString());
    });
    m_confirmationPoll.setInterval(50);
    connect(&m_confirmationPoll, &QTimer::timeout, this, [this] {
        if (!m_confirmationCancel)
            return;
        if (m_confirmationCancel->load() || !m_inspections.contains(m_confirmationToken) ||
            m_inspections.value(m_confirmationToken).expires < QDateTime::currentMSecsSinceEpoch())
            answerConfirmation(false);
    });
    m_instanceLock = m_root + "/instance.lock";
    m_expiry.setSingleShot(true);
    m_expiry.setInterval(10 * 60 * 1000);
    connect(&m_expiry, &QTimer::timeout, this, [this] {
        releaseInspection();
        if (!m_closing)
            emit notice(tr("备份预览已过期，请重新选择备份文件"), true);
    });
    m_barrierDeadline.setSingleShot(true);
    m_barrierDeadline.setInterval(30000);
    connect(&m_barrierDeadline, &QTimer::timeout, this, [this] {
        if (!m_busy || m_work.isRunning())
            return;
        *m_cancel = true;
        ++m_job;
        m_plugins.endMaintenance();
        finishJob(tr("插件存储仍在处理中，未创建备份，请稍后重试"));
    });
    connect(&m_relaunch, &RelaunchService::notice, this, [this](const QString &) {
        if (m_restore || m_clear) {
            m_restore = m_clear = false;
            finishJob(tr("无法准备重启，应用数据未更改"));
        }
    });
}
SettingsDataService::~SettingsDataService()
{
    shutdown();
    m_workers.waitForDone();
}
QVariantMap SettingsDataService::summary() const
{
    QStringList names;
    for (const auto &entry : m_manifest.value("plugins").toArray())
        names.append(entry.toObject().value("name").toString());
    const auto includes = m_manifest.value("includes").toObject();
    return {{"createdAt", m_manifest.value("createdAt").toString()},
            {"appVersion", m_manifest.value("appVersion").toString()},
            {"settings", includes.value("settings").toBool()},
            {"plugins", includes.value("plugins").toBool()},
            {"settingCount", m_manifest.value("settings").toObject().size()},
            {"pluginCount", names.size()},
            {"pluginNames", names}};
}
void SettingsDataService::finishJob(const QString &error)
{
    m_barrierDeadline.stop();
    m_busy = false;
    m_filesSnapshot.reset();
    emit changed();
    if (!error.isEmpty())
        completeApi({{}, "BackupError", error});
    if (!m_closing && !error.isEmpty())
        emit notice(error, true);
}
void SettingsDataService::create(const QUrl &file, bool settings, bool plugins)
{
    if (m_busy || m_closing || !file.isLocalFile() || (!settings && !plugins))
        return;
    if (m_guard && !m_guard()) {
        emit notice(tr("文件或插件仍在处理中，请稍后创建备份"), true);
        return;
    }
    releaseInspection();
    auto staging = QSharedPointer<QTemporaryDir>::create();
    if (!staging->isValid()) {
        emit notice(tr("无法创建备份临时目录"), true);
        return;
    }
    staging->setAutoRemove(false);
    m_busy = true;
    if (!m_apiDone)
        m_cancel = std::make_shared<std::atomic_bool>(false);
    emit changed();
    const auto job = ++m_job;
    const QPointer<SettingsDataService> guard(this);
    const auto start = [this, guard, file, settings, plugins, staging, job](bool ready) {
        if (!guard || job != m_job) {
            (void)QtConcurrent::run([staging] { QDir(staging->path()).removeRecursively(); });
            return;
        }
        m_barrierDeadline.stop();
        if (!ready || m_closing || *m_cancel) {
            if (plugins)
                m_plugins.endMaintenance();
            (void)QtConcurrent::run(&m_workers,
                                    [staging] { QDir(staging->path()).removeRecursively(); });
            finishJob(!ready && !*m_cancel && !m_closing
                          ? tr("存储仍在处理中，未创建备份，请稍后重试")
                          : tr("备份已取消"));
            return;
        }
        QString preferenceError;
        const auto portable =
            settings ? m_settings.portableValues(&preferenceError) : QVariantMap{};
        const auto preferences =
            preferenceError.isEmpty()
                ? SettingsBackupArchive::encodePreferences(portable, &preferenceError,
                                                           m_settings.portableFilesRoot())
                : QJsonObject{};
        if (!preferenceError.isEmpty()) {
            if (plugins)
                m_plugins.endMaintenance();
            (void)QtConcurrent::run(&m_workers,
                                    [staging] { QDir(staging->path()).removeRecursively(); });
            finishJob(preferenceError);
            return;
        }
        const auto entries = plugins ? m_plugins.backupEntries() : QVariantList{};
        disconnect(&m_work, nullptr, this, nullptr);
        connect(&m_work, &QFutureWatcherBase::finished, this, [this, plugins] {
            const auto result = m_work.result();
            m_filesSnapshot.reset();
            const bool api = bool(m_apiDone);
            if (plugins && !m_closing)
                m_plugins.endMaintenance();
            m_busy = false;
            if (result.error.isEmpty() && m_apiDone)
                completeApi({QVariantMap{{"data", result.data},
                                         {"fileName", "EchoMusicNext-" +
                                                          QDateTime::currentDateTimeUtc().toString(
                                                              "yyyyMMdd-HHmmss") +
                                                          ".echonext-backup"},
                                         {"summary",
                                          [this, &result] {
                                              const auto saved = m_manifest;
                                              m_manifest = result.manifest;
                                              const auto value = summary();
                                              m_manifest = saved;
                                              return value;
                                          }()}},
                             {},
                             {}});
            finishJob(result.error);
            if (result.error.isEmpty() && !m_closing && !api) {
                emit notice(tr("备份已创建"), false);
                emit created();
            }
        });
        m_work.setFuture(QtConcurrent::run(
            &m_workers,
            [file, staging, settings, plugins, preferences, entries, root = m_root,
             filesRoot = m_settings.portableFilesRoot(), version = m_version, cancel = m_cancel,
             transfer = m_transfer, filesSnapshot = m_filesSnapshot]() mutable {
                Q_UNUSED(filesSnapshot)
                auto result = SettingsBackupArchive::create(
                    file.toLocalFile(), staging->path(), preferences, settings, entries, plugins,
                    root + "/plugins", filesRoot, version, *cancel);
                if (transfer) {
                    if (result.error.isEmpty()) {
                        QFile archive(file.toLocalFile());
                        if (!archive.open(QIODevice::ReadOnly) ||
                            archive.size() > 256ll * 1024 * 1024)
                            result.error = "Backup byte transfer exceeds 256 MiB or cannot be read";
                        else
                            result.data = archive.readAll();
                    }
                    QDir(transfer->path()).removeRecursively();
                }
                QDir(staging->path()).removeRecursively();
                filesSnapshot.reset();
                return result;
            }));
    };
    // Close both write admissions before returning to the caller. Draining either
    // domain must not leave the other domain writable during a backup request.
    if (settings) {
        m_filesSnapshot = StorageFence::freeze(m_settings.portableFilesRoot());
        if (!m_filesSnapshot) {
            (void)QtConcurrent::run(&m_workers,
                                    [staging] { QDir(staging->path()).removeRecursively(); });
            finishJob(tr("可迁移数据正在处理，请稍后备份"));
            return;
        }
    }
    const auto pending = std::make_shared<int>(int(settings) + int(plugins));
    const auto valid = std::make_shared<bool>(true);
    const auto arrive = [pending, valid, start](bool ready) {
        *valid = *valid && ready;
        if (--*pending == 0)
            start(*valid);
    };
    m_barrierDeadline.start();
    if (plugins && !m_plugins.beginBackupSnapshot(arrive)) {
        // No plugin barrier was accepted, so no maintenance belongs to this job.
        m_barrierDeadline.stop();
        (void)QtConcurrent::run(&m_workers,
                                [staging] { QDir(staging->path()).removeRecursively(); });
        finishJob(tr("插件正在处理任务，请稍后创建备份"));
        return;
    }
    if (settings) {
        auto *barrier = new QFutureWatcher<bool>(this);
        connect(barrier, &QFutureWatcherBase::finished, this, [barrier, arrive] {
            const bool ready = barrier->result();
            barrier->deleteLater();
            arrive(ready);
        });
        barrier->setFuture(
            QtConcurrent::run(&m_workers, [fence = m_filesSnapshot, cancel = m_cancel]() mutable {
                const auto ready = fence->waitForWriters(*cancel);
                fence.reset();
                return ready;
            }));
    }
}
void SettingsDataService::inspect(const QUrl &file)
{
    if (m_busy || m_closing || !file.isLocalFile())
        return;
    releaseInspection();
    QDir().mkpath(m_root);
    auto staging = QSharedPointer<QTemporaryDir>::create(m_root + "/.settings-inspect-XXXXXX");
    if (!staging->isValid()) {
        emit notice(tr("无法创建恢复临时目录"), true);
        return;
    }
    staging->setAutoRemove(false);
    m_busy = true;
    if (!m_apiDone)
        m_cancel = std::make_shared<std::atomic_bool>(false);
    emit changed();
    disconnect(&m_work, nullptr, this, nullptr);
    connect(&m_work, &QFutureWatcherBase::finished, this, [this, staging] {
        const auto result = m_work.result();
        const bool accepted = result.error.isEmpty() && !m_closing && !*m_cancel;
        if (accepted) {
            m_staging = staging;
            m_manifest = result.manifest;
            m_expiry.start();
        } else
            (void)QtConcurrent::run(&m_workers,
                                    [staging] { QDir(staging->path()).removeRecursively(); });
        const auto publicSummary = summary();
        const bool api = bool(m_apiDone);
        if (accepted && api) {
            const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_inspections.insert(token, {m_apiOwner, m_staging, m_manifest,
                                         QDateTime::currentMSecsSinceEpoch() + 10 * 60 * 1000});
            m_tokenSweep.start();
            m_staging.reset();
            m_manifest = {};
            m_expiry.stop();
            m_busy = false;
            completeApi({QVariantMap{{"token", token}, {"summary", publicSummary}}, {}, {}});
            emit changed();
        } else {
            finishJob(accepted || !result.error.isEmpty() ? result.error : tr("备份检查已取消"));
            if (accepted)
                emit inspected();
        }
    });
    const auto input = std::exchange(m_inspectInput, {});
    m_work.setFuture(QtConcurrent::run(&m_workers, [file, staging, cancel = m_cancel, input,
                                                    transfer = m_transfer] {
        if (transfer) {
            QSaveFile output(file.toLocalFile());
            if (!output.open(QIODevice::WriteOnly) || output.write(input) != input.size() ||
                !output.commit()) {
                QDir(transfer->path()).removeRecursively();
                return SettingsBackupResult{"Cannot stage backup input", {}, {}};
            }
        }
        auto result = SettingsBackupArchive::inspect(file.toLocalFile(), staging->path(), *cancel);
        if (transfer)
            QDir(transfer->path()).removeRecursively();
        if (!result.error.isEmpty())
            QDir(staging->path()).removeRecursively();
        return result;
    }));
}
void SettingsDataService::releaseInspection()
{
    if (m_restore)
        return;
    m_expiry.stop();
    m_manifest = {};
    m_inspectionOwner.clear();
    if (m_staging) {
        const auto path = m_staging->path();
        m_staging.reset();
        (void)QtConcurrent::run(&m_workers, [path] { QDir(path).removeRecursively(); });
    }
    emit changed();
}
void SettingsDataService::restore(bool settings, bool plugins)
{
    const auto includes = m_manifest.value("includes").toObject();
    settings = settings && includes.value("settings").toBool();
    plugins = plugins && includes.value("plugins").toBool();
    if (!canRestore() || m_closing || (!settings && !plugins))
        return;
    if (m_guard && !m_guard()) {
        emit notice(tr("文件或插件仍在处理中，请稍后恢复"), true);
        return;
    }
    if (!m_relaunch.automaticRestart()) {
        emit notice(tr("此开发启动方式不支持自动恢复重启，请使用正式启动脚本"), true);
        return;
    }
    m_busy = m_restore = true;
    m_restoreSettings = settings;
    m_restorePlugins = plugins;
    m_expiry.stop();
    emit changed();
    m_relaunch.restart();
}
void SettingsDataService::clearApplicationData()
{
    if (m_busy || m_closing)
        return;
    if (m_guard && !m_guard()) {
        emit notice(tr("文件或插件仍在处理中，请稍后清除"), true);
        return;
    }
    if (m_network.busy()) {
        emit notice(tr("网络凭证仍在处理中，请稍后清除"), true);
        return;
    }
    if (!m_relaunch.automaticRestart()) {
        emit notice(tr("此开发启动方式不支持自动清除重启，请使用正式启动脚本"), true);
        return;
    }
    m_busy = m_clear = true;
    emit changed();
    m_relaunch.restart();
}
void SettingsDataService::shutdown()
{
    m_closing = true;
    shutdownProviders();
    m_tokenSweep.stop();
    answerConfirmation(false);
    const auto tokens = m_inspections.keys();
    for (const auto &token : tokens)
        clearToken(token);
    m_expiry.stop();
    m_barrierDeadline.stop();
    if (!m_restore && !m_clear)
        *m_cancel = true;
    if (!m_restore)
        releaseInspection();
}
void SettingsDataService::cancel()
{
    if (m_restore || m_clear)
        return;
    *m_cancel = true;
    if (m_busy && !m_work.isRunning())
        m_plugins.endMaintenance();
    releaseInspection();
}
void SettingsDataService::finishShutdown(std::function<void(QString)> complete)
{
    if (!m_restore && !m_clear) {
        if (m_work.isRunning()) {
            connect(
                &m_work, &QFutureWatcherBase::finished, this, [complete] { complete({}); },
                Qt::SingleShotConnection);
        } else
            complete({});
        return;
    }
    if (m_clear) {
        m_network.eraseCredentialForReset([this, complete](const QString &error) {
            if (!error.isEmpty()) {
                complete(error);
                return;
            }
            clearFiles(complete);
        });
        return;
    }
    QString decodeError;
    const auto values =
        m_restoreSettings
            ? SettingsBackupArchive::decodePreferences(m_manifest.value("settings").toObject(),
                                                       &decodeError, m_settings.portableFilesRoot())
            : QVariantMap{};
    if (!decodeError.isEmpty()) {
        complete(decodeError);
        return;
    }
    const auto oldValues =
        m_restoreSettings ? m_settings.portableValues(&decodeError) : QVariantMap{};
    if (!decodeError.isEmpty()) {
        complete(decodeError);
        return;
    }
    const QJsonObject previous{
        {"values", SettingsBackupArchive::encodePreferences(oldValues, &decodeError,
                                                            m_settings.portableFilesRoot())}};
    if (!decodeError.isEmpty()) {
        complete(decodeError);
        return;
    }
    const auto catalog = PluginInstallationStore(m_root + "/plugins").records();
    auto watcher = new QFutureWatcher<SettingsRestoreResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, values, complete] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (result.error.isEmpty()) {
            if (m_restoreSettings)
                SettingsBackupArchive::applyPreferences(m_settings, values);
            if (m_restorePlugins)
                result.error = PluginInstallationStore(m_root + "/plugins").save(result.catalog);
            m_settings.sync();
            if (m_settings.status() != EchoMusic::SettingsStore::NoError)
                result.error = tr("无法提交恢复后的设置");
            if (result.error.isEmpty()) {
                result.journal.insert("committed", true);
                if (!SettingsBackupRestore::writeJournal(m_root, result.journal))
                    result.error = tr("无法提交恢复事务");
            }
        }
        auto cleanup = new QFutureWatcher<QString>(this);
        connect(cleanup, &QFutureWatcherBase::finished, this, [this, cleanup, result, complete] {
            const auto rollback = cleanup->result();
            cleanup->deleteLater();
            const auto preferencesError =
                !result.error.isEmpty()
                    ? SettingsBackupRestore::restorePreferences(m_settings, result.journal)
                    : QString();
            const auto error =
                result.error + (rollback.isEmpty() ? QString() : "; rollback: " + rollback) +
                (preferencesError.isEmpty() ? QString() : "; preferences: " + preferencesError);
            if (!rollback.isEmpty() || !preferencesError.isEmpty() ||
                m_settings.status() != EchoMusic::SettingsStore::NoError) {
                complete(error.isEmpty() ? tr("无法恢复原有设置，已保留恢复事务以供下次启动重试")
                                         : error);
                return;
            }
            auto discard = new QFutureWatcher<void>(this);
            connect(discard, &QFutureWatcherBase::finished, this, [discard, complete, error] {
                discard->deleteLater();
                complete(error);
            });
            discard->setFuture(
                QtConcurrent::run(&m_workers, [root = m_root, journal = result.journal] {
                    SettingsBackupRestore::discard(root, journal);
                }));
        });
        cleanup->setFuture(
            QtConcurrent::run(&m_workers, [root = m_root, result, staging = m_staging] {
                QString error;
                if (!result.error.isEmpty() && QFile::exists(root + "/settings-restore.json"))
                    error = SettingsBackupRestore::rollbackFiles(root, result.journal);
                QDir(staging->path()).removeRecursively();
                return error;
            }));
    });
    m_staging->setAutoRemove(false);
    watcher->setFuture(QtConcurrent::run(
        &m_workers, [staging = m_staging, manifest = m_manifest, settings = m_restoreSettings,
                     plugins = m_restorePlugins, root = m_root, catalog, previous,
                     filesRoot = m_settings.portableFilesRoot()] {
            return SettingsBackupRestore::prepare(staging->path(), manifest, settings, plugins,
                                                  root, catalog, previous, filesRoot);
        }));
}
void SettingsDataService::clearFiles(std::function<void(QString)> complete)
{
    auto watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, complete] {
        const auto error = watcher->result();
        watcher->deleteLater();
        if (error.isEmpty()) {
            m_settings.clear();
            m_settings.sync();
        }
        complete(error.isEmpty() && m_settings.status() != EchoMusic::SettingsStore::NoError
                     ? tr("无法清除应用设置")
                     : error);
    });
    watcher->setFuture(QtConcurrent::run(&m_workers, [root = m_root, lock = m_instanceLock] {
        return SettingsBackupRestore::clearManagedFiles(root, lock);
    }));
}
QString SettingsDataService::recover(const QString &root, EchoMusic::SettingsStore &settings)
{
    return SettingsBackupRestore::recover(root, settings);
}
} // namespace EchoMusic
