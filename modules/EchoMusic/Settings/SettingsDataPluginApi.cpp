#include "SettingsDataService.h"
#include <QDateTime>
#include <QDir>
#include <QtConcurrent>
namespace EchoMusic
{
namespace
{
bool options(const QVariantMap &input)
{
    if (!input.value("settings", true).toBool() && !input.value("plugins", true).toBool())
        return false;
    for (auto it = input.begin(); it != input.end(); ++it)
        if ((it.key() != "settings" && it.key() != "plugins") ||
            it.value().metaType().id() != QMetaType::Bool)
            return false;
    return true;
}
} // namespace
void SettingsDataService::cancelUi()
{
    cancelProvider();
    if (!m_apiDone)
        cancel();
    releaseInspection();
}
void SettingsDataService::completeApi(BackupResult result)
{
    auto done = std::exchange(m_apiDone, {});
    m_apiOwner.clear();
    if (m_transfer) {
        const auto staging = std::exchange(m_transfer, {});
        (void)QtConcurrent::run(&m_workers,
                                [staging] { QDir(staging->path()).removeRecursively(); });
    }
    if (done)
        done(std::move(result));
}
void SettingsDataService::createArchive(const QString &owner, const QVariantMap &scope,
                                        BackupCancel cancel, BackupCompletion done)
{
    if (m_busy || m_closing || m_confirmationDone || m_apiDone) {
        done({{}, "Busy", tr("备份仍在处理中")});
        return;
    }
    if (!options(scope)) {
        done({{}, "InvalidArgument", tr("请选择有效备份范围")});
        return;
    }
    if (m_guard && !m_guard()) {
        done({{}, "Busy", tr("文件或插件仍在处理中，请稍后创建备份")});
        return;
    }
    auto transfer = QSharedPointer<QTemporaryDir>::create();
    if (!transfer->isValid()) {
        done({{}, "BackupError", tr("无法创建备份临时目录")});
        return;
    }
    transfer->setAutoRemove(false);
    m_transfer = transfer;
    m_apiOwner = owner;
    m_apiDone = std::move(done);
    m_cancel = cancel;
    create(QUrl::fromLocalFile(transfer->filePath("archive.echonext-backup")),
           scope.value("settings", true).toBool(), scope.value("plugins", true).toBool());
    if (!m_busy && m_apiDone)
        completeApi({{}, "BackupError", tr("无法开始创建备份")});
}
void SettingsDataService::inspectArchive(const QString &owner, const QByteArray &data,
                                         BackupCancel cancel, BackupCompletion done)
{
    if (m_busy || m_closing || m_confirmationDone) {
        done({{}, "Busy", tr("备份仍在处理中")});
        return;
    }
    const auto now = QDateTime::currentMSecsSinceEpoch();
    for (const auto &key : m_inspections.keys())
        if (m_inspections[key].expires < now)
            clearToken(key);
    if (data.isEmpty() || data.size() > 256ll * 1024 * 1024 || m_inspections.size() >= 8) {
        done({{}, "InvalidArgument", tr("备份为空、超过256 MiB或预览数量已达上限")});
        return;
    }
    auto transfer = QSharedPointer<QTemporaryDir>::create();
    if (!transfer->isValid()) {
        done({{}, "BackupError", tr("无法创建检查临时目录")});
        return;
    }
    transfer->setAutoRemove(false);
    m_transfer = transfer;
    m_apiOwner = owner;
    m_apiDone = std::move(done);
    m_cancel = cancel;
    m_inspectInput = data;
    inspect(QUrl::fromLocalFile(transfer->filePath("input.echonext-backup")));
    if (!m_busy && m_apiDone)
        completeApi({{}, "BackupError", tr("无法开始检查备份")});
}
void SettingsDataService::clearToken(const QString &token)
{
    if (!m_inspections.contains(token))
        return;
    const auto item = m_inspections.take(token);
    if (m_inspections.isEmpty())
        m_tokenSweep.stop();
    (void)QtConcurrent::run(
        &m_workers, [staging = item.staging] { QDir(staging->path()).removeRecursively(); });
}
void SettingsDataService::releaseOwner(const QString &owner)
{
    if (m_inspectionOwner == owner)
        releaseInspection();
    if (m_apiOwner == owner && m_apiDone) {
        m_cancel->store(true);
    }
    if (m_confirmationToken.size() && m_inspections.value(m_confirmationToken).owner == owner)
        answerConfirmation(false);
    for (const auto &token : m_inspections.keys())
        if (m_inspections[token].owner == owner)
            clearToken(token);
}
void SettingsDataService::requestRestore(const QString &owner, const QString &token,
                                         const QVariantMap &scope, BackupCancel cancel,
                                         BackupCompletion done)
{
    if (m_busy || m_closing || m_confirmationDone) {
        done({{}, "Busy", tr("备份仍在处理中")});
        return;
    }
    const auto item = m_inspections.value(token);
    if (token.isEmpty() || item.owner != owner || !item.staging ||
        item.expires < QDateTime::currentMSecsSinceEpoch()) {
        done({{}, "InvalidToken", tr("备份预览已过期或不属于当前插件")});
        return;
    }
    if (!options(scope)) {
        done({{}, "InvalidArgument", tr("请选择有效恢复范围")});
        return;
    }
    const auto includes = item.manifest.value("includes").toObject();
    if (!(scope.value("settings", true).toBool() && includes.value("settings").toBool()) &&
        !(scope.value("plugins", true).toBool() && includes.value("plugins").toBool())) {
        done({{}, "InvalidArgument", tr("备份不包含所选恢复范围")});
        return;
    }
    m_confirmationToken = token;
    m_confirmationDone = std::move(done);
    m_confirmationCancel = cancel;
    const auto old = m_manifest;
    m_manifest = item.manifest;
    m_confirmation = {{"summary", summary()},
                      {"settings", scope.value("settings", true)},
                      {"plugins", scope.value("plugins", true)}};
    m_manifest = old;
    m_confirmationPoll.start();
    emit confirmationChanged();
}
void SettingsDataService::answerConfirmation(bool accepted, bool settings, bool plugins)
{
    if (!m_confirmationDone)
        return;
    const auto token = std::exchange(m_confirmationToken, {});
    auto done = std::exchange(m_confirmationDone, {});
    const auto cancel = std::exchange(m_confirmationCancel, {});
    m_confirmationPoll.stop();
    const auto requested = m_confirmation;
    m_confirmation = {};
    emit confirmationChanged();
    if (!accepted || cancel->load()) {
        done({{}, "Canceled", tr("已取消恢复")});
        return;
    }
    const auto item = m_inspections.value(token);
    if (!item.staging || item.expires < QDateTime::currentMSecsSinceEpoch()) {
        clearToken(token);
        done({{}, "InvalidToken", tr("备份预览已过期")});
        return;
    }
    settings = settings && requested.value("settings").toBool() &&
               item.manifest.value("includes").toObject().value("settings").toBool();
    plugins = plugins && requested.value("plugins").toBool() &&
              item.manifest.value("includes").toObject().value("plugins").toBool();
    if ((!settings && !plugins) || m_guard && !m_guard() || !m_relaunch.automaticRestart()) {
        done({{}, "Unavailable", tr("当前无法恢复并重启，应用数据未更改")});
        return;
    }
    m_inspections.remove(token);
    if (m_inspections.isEmpty())
        m_tokenSweep.stop();
    releaseInspection();
    m_staging = item.staging;
    m_manifest = item.manifest;
    restore(settings, plugins);
    if (m_restore)
        done(
            {QVariantMap{{"restartScheduled", true},
                         {"settingsImported", settings},
                         {"pluginsImported", plugins ? summary().value("pluginCount").toInt() : 0}},
             {},
             {}});
    else
        done({{}, "RestoreError", tr("无法准备恢复，应用数据未更改")});
}
void SettingsDataService::adoptProviderInspection(const QString &token)
{
    if (m_busy || !m_inspections.contains(token))
        return;
    const auto item = m_inspections.take(token);
    if (m_inspections.isEmpty())
        m_tokenSweep.stop();
    if (item.expires < QDateTime::currentMSecsSinceEpoch()) {
        (void)QtConcurrent::run(
            &m_workers, [staging = item.staging] { QDir(staging->path()).removeRecursively(); });
        return;
    }
    releaseInspection();
    m_staging = item.staging;
    m_manifest = item.manifest;
    m_inspectionOwner = item.owner;
    m_expiry.start();
    emit changed();
    emit inspected();
}
} // namespace EchoMusic
