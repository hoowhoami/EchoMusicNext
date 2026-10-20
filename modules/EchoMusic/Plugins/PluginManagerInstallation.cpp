#include "SettingsKeys.h"
#include "PluginManager.h"
#include "SharingCommands.h"
#include "NetworkPreferences.h"
#include "PluginPackage.h"
#include "PluginMarketplaceStats.h"
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QDateTime>
#include <QCryptographicHash>
#include "PluginManagerHelpers_p.h"

namespace EchoMusic
{
using namespace PluginManagerDetail;

bool PluginManager::install(const QString &key)
{
    if (m_busy || m_catalogLoading || m_closing || m_runtime.loading())
        return false;
    const auto e = entry(key);
    if (e.isEmpty() || !e.value("compatible").toBool() ||
        !sourceIds().contains(e.value("sourceId").toString()))
        return false;
    const auto current = m_runtime.record(m_runtime.installationForId(e.value("id").toString()));
    const auto origin = current.value("origin").toMap();
    if (!current.isEmpty() && !sameMarketplaceSource(current, e)) {
        emit notice(
            origin.value("kind") != "marketplace"
                ? tr("该插件已从本地安装")
                : tr("该插件已从 %1 安装").arg(origin.value("name", tr("其他插件源")).toString()),
            false, true);
        return false;
    }
    m_pendingPluginId = e.value("id").toString();
    m_busy = true;
    m_installWasUpdate = !m_runtime.installationForId(e.value("id").toString()).isEmpty();
    m_installingKey = key;
    rebuild();
    get(QUrl(raw({{"repository", e.value("repository")}}, e.value("artifact").toString())),
        128 * 1024 * 1024, [this, e](QByteArray bytes, QString error) {
            if (!error.isEmpty())
                finishInstall(false, error);
            else
                installArchive(bytes, e);
        });
    return true;
}
void PluginManager::installArchive(const QByteArray &bytes, const QVariantMap &entry)
{
    if (!entry.isEmpty() &&
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) !=
            entry.value("sha256").toString()) {
        finishInstall(false, tr("插件包 SHA-256 与插件源索引不一致，请刷新后重试"));
        return;
    }
    m_staging = QSharedPointer<QTemporaryDir>::create();
    if (!m_staging->isValid()) {
        finishInstall(false, tr("无法创建安装暂存目录"));
        return;
    }
    QFile file(m_staging->path() + "/package.echomod");
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        finishInstall(false, tr("无法写入插件包"));
        return;
    }
    file.close();
    extractLocal(file.fileName(), entry);
}
void PluginManager::extractLocal(const QString &archive, const QVariantMap &entry)
{
    if (!m_staging)
        m_staging = QSharedPointer<QTemporaryDir>::create();
    const auto stage = m_staging;
    auto *watcher = new QFutureWatcher<QPair<QString, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, entry, stage] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (m_closing)
            return;
        if (!result.second.isEmpty()) {
            finishInstall(false, result.second);
            return;
        }
        const QVariantMap origin = entry.isEmpty()
                                       ? QVariantMap{{"kind", "local"}, {"name", tr("本地安装")}}
                                       : QVariantMap{{"kind", "marketplace"},
                                                     {"id", entry.value("sourceId")},
                                                     {"name", entry.value("sourceName")},
                                                     {"url", entry.value("sourceUrl")}};
        m_pendingPluginId = localPluginId(result.first);
        if (!m_runtime.installDirectory(result.first, origin, entry.value("manifest").toMap(),
                                        stage))
            finishInstall(false, tr("插件包无效、清单已变更或运行时正忙，请刷新后重试"));
    });
    watcher->setFuture(QtConcurrent::run([archive, stage, entry]() -> QPair<QString, QString> {
        const auto unpacked = stage->path() + "/unpacked";
        if (const auto error = PluginPackage::extractArchive(archive, unpacked); !error.isEmpty())
            return {{}, error};
        QString candidate = unpacked;
        if (!QFileInfo(candidate + "/manifest.json").isFile())
            return {{}, "Archive must contain one plugin manifest"};
        return {candidate, {}};
    }));
}
bool PluginManager::installLocal(const QUrl &url)
{
    if (!url.isLocalFile() || m_busy || m_catalogLoading || m_closing || m_runtime.loading())
        return false;
    const auto path = url.toLocalFile();
    if (QFileInfo(path).isDir()) {
        m_busy = true;
        m_installingKey.clear();
        emit changed();
        m_pendingPluginId = localPluginId(path);
        if (!m_runtime.installDirectory(path, {{"kind", "local"}, {"name", tr("本地安装")}})) {
            finishInstall(false, tr("插件目录无效或不兼容"));
            return true;
        }
        return true;
    }
    if (!path.endsWith(".echomod", Qt::CaseInsensitive)) {
        emit notice(tr("请拖入 .echomod 插件包或开发目录"), true);
        return false;
    }
    m_busy = true;
    m_installingKey.clear();
    emit changed();
    extractLocal(path, {});
    return true;
}
bool PluginManager::installLocalBatch(const QList<QUrl> &urls)
{
    if (urls.isEmpty() || urls.size() > 128 || m_catalogLoading || m_closing)
        return false;
    if (m_localBatch) {
        if (m_localTotal + urls.size() > 128)
            return false;
        // Native file-open events arrive separately, including while the first
        // file is being captured. Keep them in the same bounded install queue.
        m_localQueue.append(urls);
        m_localTotal += int(urls.size());
        rebuild();
        return true;
    }
    if (m_busy)
        return false;
    m_localQueue = urls;
    m_localTotal = int(urls.size());
    m_localDone = 0;
    m_localFailed = false;
    m_localBatch = true;
    startLocalNext();
    return true;
}
void PluginManager::startLocalNext()
{
    if (m_closing)
        return;
    if (m_runtime.loading() || m_catalogLoading) {
        m_busy = true;
        rebuild();
        QTimer::singleShot(25, this, &PluginManager::startLocalNext);
        return;
    }
    m_busy = false;
    const auto next = m_localQueue.takeFirst();
    if (!installLocal(next))
        finishInstall(false, tr("无法安装所拖入的插件，请使用 .echomod 插件包或开发目录"));
}
void PluginManager::finishInstall(bool success, const QString &error)
{
    const auto selected = entry(m_installingKey);
    if (!selected.isEmpty())
        reportInstallation(selected, success);
    m_busy = false;
    m_installingKey.clear();
    m_staging.clear();
    m_pendingPluginId.clear();
    m_pendingInstallation.clear();
    if (m_shareInstalling) {
        m_shareInstalling = false;
        m_shareInstallError = success ? QString() : error;
        resolveShared();
        if (success)
            emit sharedInstalled();
    }
    if (m_localBatch) {
        ++m_localDone;
        m_localFailed = m_localFailed || !success;
        if (!success)
            emit notice(error, true);
        if (!m_localQueue.isEmpty() && !m_closing) {
            m_busy = true;
            rebuild();
            QTimer::singleShot(0, this, &PluginManager::startLocalNext);
            return;
        }
        m_localBatch = false;
        rebuild();
        emit notice(m_localFailed ? tr("批量安装完成，部分插件安装失败") : tr("插件安装完成"),
                    m_localFailed);
        return;
    }
    m_updateFailed = m_updateFailed || !success;
    if (m_batch)
        ++m_batchDone;
    rebuild();
    if (!m_updateQueue.isEmpty() && !m_closing) {
        m_busy = true;
        m_installingKey = m_updateQueue.first();
        rebuild();
        QTimer::singleShot(0, this, &PluginManager::startUpdateNext);
        return;
    }
    const bool wasBatch = m_batch;
    m_batch = false;
    emit changed();
    emit notice(success && !m_updateFailed ? wasBatch ? tr("插件全部更新完成") : tr("插件安装完成")
                : error.isEmpty()          ? tr("部分插件更新失败")
                                           : error,
                !success || m_updateFailed);
    m_updateFailed = false;
}
void PluginManager::startUpdateNext()
{
    if (m_closing || m_updateQueue.isEmpty())
        return;
    if (m_runtime.loading()) {
        QTimer::singleShot(25, this, &PluginManager::startUpdateNext);
        return;
    }
    const auto next = m_updateQueue.takeFirst();
    m_busy = false;
    if (!install(next))
        finishInstall(false, tr("插件更新未能开始"));
}
void PluginManager::updateAll()
{
    if (m_busy || m_catalogLoading || m_closing)
        return;
    QSet<QString> ids;
    for (const auto &value : m_entries) {
        const auto e = value.toMap();
        auto current = m_runtime.record(m_runtime.installationForId(e.value("id").toString()));
        if (availableUpdate(current, e) && sourceIds().contains(e.value("sourceId").toString()) &&
            !ids.contains(e.value("id").toString())) {
            m_updateQueue.append(e.value("key").toString());
            ids.insert(e.value("id").toString());
        }
    }
    if (!m_updateQueue.isEmpty()) {
        m_batch = true;
        m_batchTotal = int(m_updateQueue.size());
        m_batchDone = 0;
        const auto next = m_updateQueue.takeFirst();
        if (!install(next))
            finishInstall(false, tr("插件更新未能开始"));
    }
}
} // namespace EchoMusic
