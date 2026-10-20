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

namespace EchoMusic
{
SettingsDataService::SettingsDataService(QSettings &settings, PluginRuntime &plugins,
                                         RelaunchService &relaunch, NetworkPreferences &network,
                                         QString root, QString version, QObject *parent)
    : QObject(parent), m_settings(settings), m_plugins(plugins), m_relaunch(relaunch),
      m_network(network), m_root(std::move(root)), m_version(std::move(version))
{
    m_workers.setMaxThreadCount(1);
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
        m_plugins.endMaintenance();
        finishJob(tr("插件尚未释放全部资源，未创建备份，请稍后重试"));
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
QVariantMap SettingsDataService::preferenceValues() const
{
    QVariantMap values;
    for (const auto &key : m_settings.allKeys())
        if (SettingsBackupArchive::portableKey(key))
            values.insert(key, m_settings.value(key));
    return values;
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
    emit changed();
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
    *m_cancel = false;
    emit changed();
    const auto start = [this, file, settings, plugins, staging](bool ready) {
        m_barrierDeadline.stop();
        if (!ready || m_closing || *m_cancel) {
            (void)QtConcurrent::run(&m_workers,
                                    [staging] { QDir(staging->path()).removeRecursively(); });
            finishJob(tr("备份已取消"));
            return;
        }
        const auto preferences = SettingsBackupArchive::encodePreferences(preferenceValues());
        const auto entries = plugins ? m_plugins.backupEntries() : QVariantList{};
        disconnect(&m_work, nullptr, this, nullptr);
        connect(&m_work, &QFutureWatcherBase::finished, this, [this, plugins] {
            const auto result = m_work.result();
            if (plugins && !m_closing)
                m_plugins.endMaintenance();
            finishJob(result.error);
            if (result.error.isEmpty() && !m_closing) {
                emit notice(tr("备份已创建"), false);
                emit created();
            }
        });
        m_work.setFuture(
            QtConcurrent::run(&m_workers, [file, staging, settings, plugins, preferences, entries,
                                           root = m_root, version = m_version, cancel = m_cancel] {
                auto result = SettingsBackupArchive::create(
                    file.toLocalFile(), staging->path(), preferences, settings, entries, plugins,
                    root + "/plugins", root + "/theme-images", version, *cancel);
                QDir(staging->path()).removeRecursively();
                return result;
            }));
    };
    if (plugins) {
        if (!m_plugins.beginMaintenance(start)) {
            (void)QtConcurrent::run(&m_workers,
                                    [staging] { QDir(staging->path()).removeRecursively(); });
            finishJob(tr("插件正在处理任务，请稍后创建备份"));
            return;
        }
        m_barrierDeadline.start();
    } else
        start(true);
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
    *m_cancel = false;
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
        finishJob(result.error);
        if (accepted)
            emit inspected();
    });
    m_work.setFuture(QtConcurrent::run(&m_workers, [file, staging, cancel = m_cancel] {
        auto result = SettingsBackupArchive::inspect(file.toLocalFile(), staging->path(), *cancel);
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
    const auto values = m_restoreSettings
                            ? SettingsBackupArchive::decodePreferences(
                                  m_manifest.value("settings").toObject(), &decodeError)
                            : QVariantMap{};
    if (!decodeError.isEmpty()) {
        complete(decodeError);
        return;
    }
    QVariantMap oldValues;
    QJsonArray absent;
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (m_settings.contains(it.key()))
            oldValues.insert(it.key(), m_settings.value(it.key()));
        else
            absent.append(it.key());
    }
    const QJsonObject previous{{"values", SettingsBackupArchive::encodePreferences(oldValues)},
                               {"absent", absent}};
    const auto catalog = m_settings.value(EchoMusic::SettingsKeys::Plugins::Installations).toList();
    auto watcher = new QFutureWatcher<SettingsRestoreResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, values, complete] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (result.error.isEmpty()) {
            SettingsBackupArchive::applyPreferences(m_settings, values);
            if (m_restorePlugins)
                m_settings.setValue(EchoMusic::SettingsKeys::Plugins::Installations,
                                    result.catalog);
            m_settings.sync();
            if (m_settings.status() != QSettings::NoError)
                result.error = tr("无法提交恢复后的设置");
            else {
                result.journal.insert("committed", true);
                if (!SettingsBackupRestore::writeJournal(m_root, result.journal))
                    result.error = tr("无法提交恢复事务");
            }
        }
        auto cleanup = new QFutureWatcher<QString>(this);
        connect(cleanup, &QFutureWatcherBase::finished, this, [this, cleanup, result, complete] {
            const auto rollback = cleanup->result();
            cleanup->deleteLater();
            if (!result.error.isEmpty())
                SettingsBackupRestore::restorePreferences(m_settings, result.journal);
            const auto error =
                result.error + (rollback.isEmpty() ? QString() : "; rollback: " + rollback);
            if (!rollback.isEmpty() || m_settings.status() != QSettings::NoError) {
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
                     plugins = m_restorePlugins, root = m_root, catalog, previous] {
            return SettingsBackupRestore::prepare(staging->path(), manifest, settings, plugins,
                                                  root, catalog, previous);
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
        complete(error.isEmpty() && m_settings.status() != QSettings::NoError
                     ? tr("无法清除应用设置")
                     : error);
    });
    watcher->setFuture(QtConcurrent::run(&m_workers, [root = m_root, lock = m_instanceLock] {
        return SettingsBackupRestore::clearManagedFiles(root, lock);
    }));
}
QString SettingsDataService::recover(const QString &root, QSettings &settings)
{
    return SettingsBackupRestore::recover(root, settings);
}
} // namespace EchoMusic
