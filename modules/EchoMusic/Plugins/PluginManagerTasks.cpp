#include "PluginManager.h"
#include "PluginManagerHelpers_p.h"
#include <QNetworkInformation>

namespace EchoMusic
{
using namespace PluginManagerDetail;

QString PluginManager::updateIdentity(const QVariantMap &entry)
{
    // Source, publisher and version all belong to the offer's identity. A new
    // publisher must never inherit an old task's dismiss/retry state.
    return QString::fromUtf8(
        QJsonDocument(QJsonArray{entry.value("sourceId").toString(),
                                 entry.value("repository").toString(), entry.value("id").toString(),
                                 entry.value("version").toString()})
            .toJson(QJsonDocument::Compact));
}

QList<PluginManager::UpdateJob> PluginManager::updateEntries() const
{
    QList<UpdateJob> result;
    const auto enabled = sourceIds();
    for (const auto &value : m_entries) {
        const auto offer = value.toMap();
        const auto identity = updateIdentity(offer);
        if (!enabled.contains(offer.value("sourceId").toString()) ||
            m_dismissedUpdates.contains(identity))
            continue;
        const auto installed =
            m_runtime.record(m_runtime.installationForId(offer.value("id").toString()));
        if (installed.isEmpty() || !sameMarketplaceSource(installed, offer))
            continue;
        const auto job = m_updateJobs.constFind(identity);
        const auto currentVersion =
            QVersionNumber::fromString(installed.value("version").toString());
        const auto targetVersion = QVersionNumber::fromString(offer.value("version").toString());
        if (job != m_updateJobs.cend() &&
            (job->status == TaskCenterModel::Running ||
             (job->status == TaskCenterModel::Completed && currentVersion == targetVersion) ||
             (job->status == TaskCenterModel::Error && currentVersion < targetVersion))) {
            result.append(*job);
        } else if (targetVersion > currentVersion) {
            result.append(
                {offer, installed.value("version").toString(), {}, TaskCenterModel::Pending});
        }
    }
    return result;
}

void PluginManager::beginUpdate(const QVariantMap &offer, const QVariantMap &installed)
{
    m_activeUpdate = updateIdentity(offer);
    m_dismissedUpdates.remove(m_activeUpdate);
    // Keep one result per source/plugin, rather than retaining every old version.
    for (auto it = m_updateJobs.begin(); it != m_updateJobs.end();) {
        if (it->entry.value("key") == offer.value("key"))
            it = m_updateJobs.erase(it);
        else
            ++it;
    }
    m_updateJobs.insert(
        m_activeUpdate,
        {offer, installed.value("version").toString(), {}, TaskCenterModel::Running});
}

void PluginManager::attachTasks(TaskCenterModel &tasks)
{
    if (m_tasks == &tasks)
        return;
    if (m_tasks)
        m_tasks->remove(m_updateTask);
    m_tasks = &tasks;
    m_updateTask = {};
    m_taskFingerprint.clear();
    publishUpdates();
}

void PluginManager::publishUpdates()
{
    if (!m_tasks || m_closing)
        return;
    for (auto it = m_updateJobs.begin(); it != m_updateJobs.end();) {
        const auto installed =
            m_runtime.record(m_runtime.installationForId(it->entry.value("id").toString()));
        if (installed.isEmpty() || !sameMarketplaceSource(installed, it->entry) ||
            updateIdentity(entry(it->entry.value("key").toString())) != it.key())
            it = m_updateJobs.erase(it);
        else
            ++it;
    }
    const auto entries = updateEntries();
    if (entries.isEmpty()) {
        m_tasks->remove(m_updateTask);
        m_updateTask = {};
        m_taskFingerprint.clear();
        return;
    }
    TaskCenterModel::Content content;
    content.name = tr("插件更新");
    content.icon = "puzzle";
    int pending = 0, failed = 0;
    bool running = m_batch;
    QStringList available, failedKeys, identities;
    QJsonArray fingerprint;
    for (const auto &job : entries) {
        const auto &offer = job.entry;
        const auto identity = updateIdentity(offer);
        identities.append(identity);
        fingerprint.append(QJsonArray{identity, int(job.status), job.error});
        running = running || job.status == TaskCenterModel::Running;
        pending += job.status == TaskCenterModel::Pending;
        failed += job.status == TaskCenterModel::Error;
        TaskDetail detail;
        detail.id = identity;
        detail.name = offer.value("name").toString();
        detail.description =
            tr("%1 → %2").arg(job.installedVersion, offer.value("version").toString());
        detail.statusLabel = job.status == TaskCenterModel::Completed ? tr("已更新")
                             : job.status == TaskCenterModel::Running ? tr("更新中…")
                                                                      : QString();
        detail.error = job.error;
        const bool actionable =
            job.status == TaskCenterModel::Pending || job.status == TaskCenterModel::Error;
        if (actionable && offer.value("compatible").toBool()) {
            const auto key = offer.value("key").toString();
            available.append(key);
            if (job.status == TaskCenterModel::Error)
                failedKeys.append(key);
            detail.actions.append({"update",
                                   job.status == TaskCenterModel::Error ? tr("重试") : tr("更新"),
                                   TaskAction::Ghost,
                                   !m_busy && !m_batch && !m_catalogLoading && !m_runtime.loading(),
                                   false, [this, key, identity] {
                                       if (updateIdentity(entry(key)) == identity)
                                           install(key);
                                   }});
        } else if (actionable && detail.error.isEmpty()) {
            detail.error = offer.value("error").toString();
            if (detail.error.isEmpty())
                detail.error = tr("需要更新 EchoMusicNext 后再更新此插件");
        }
        content.items.append(std::move(detail));
    }
    const auto status = running   ? TaskCenterModel::Running
                        : pending ? TaskCenterModel::Pending
                        : failed  ? TaskCenterModel::Error
                                  : TaskCenterModel::Completed;
    content.statusLabel =
        running ? (m_batch ? tr("更新中 %1/%2").arg(m_batchDone).arg(m_batchTotal) : tr("更新中…"))
        : pending ? tr("%1 个插件有更新").arg(pending)
        : failed  ? tr("%1 个更新失败").arg(failed)
                  : tr("插件已更新");
    if (m_batch && m_batchTotal)
        content.percent = 100. * m_batchDone / m_batchTotal;
    const bool enabled = !m_busy && !m_catalogLoading && !m_runtime.loading();
    if (!running) {
        if (pending)
            content.actions.append(
                {"later", tr("稍后"), TaskAction::Ghost, true, false, [this, identities] {
                     for (const auto &identity : identities)
                         m_dismissedUpdates.insert(identity);
                     publishUpdates();
                 }});
        if (pending && !available.isEmpty())
            content.actions.append({"update-all", tr("全部更新"), TaskAction::Primary, enabled,
                                    false, [this, available] { updateBatch(available); }});
        if (!failedKeys.isEmpty())
            content.actions.append({"retry-failed", tr("重试失败项"), TaskAction::Ghost, enabled,
                                    false, [this, failedKeys] { updateBatch(failedKeys); }});
    }
    const auto signature =
        QString::fromUtf8(QJsonDocument(fingerprint).toJson(QJsonDocument::Compact));
    const bool changed = signature != m_taskFingerprint;
    const auto registerTask = [&] {
        TaskCenterModel::Registration registration;
        registration.id = "plugin-updates";
        registration.content = content;
        registration.status = status;
        m_updateTask = m_tasks->registerTask(*this, std::move(registration));
    };
    if (!m_updateTask.generation) {
        if (changed)
            registerTask();
    } else if (status != m_updateTaskStatus && (m_updateTaskStatus >= TaskCenterModel::Completed ||
                                                (m_updateTaskStatus == TaskCenterModel::Running &&
                                                 status == TaskCenterModel::Pending))) {
        registerTask();
    } else {
        bool updated;
        if (status >= TaskCenterModel::Completed && status != m_updateTaskStatus)
            updated = m_tasks->finish(m_updateTask, status, content);
        else {
            if (m_updateTaskStatus == TaskCenterModel::Pending &&
                status == TaskCenterModel::Running)
                m_tasks->start(m_updateTask);
            updated = m_tasks->update(m_updateTask, content);
        }
        // A closed/expired result stays closed on unrelated manager changes.
        if (!updated && changed)
            registerTask();
    }
    m_updateTaskStatus = status;
    m_taskFingerprint = signature;
}

bool PluginManager::catalogCheckSucceeded() const
{
    if (!m_loaded)
        return false;
    for (const auto &value : m_sources) {
        const auto source = value.toMap();
        if (source.value("enabled").toBool() && (!source.value("catalogValid").toBool() ||
                                                 !source.value("lastError").toString().isEmpty()))
            return false;
    }
    return true;
}

void PluginManager::startStartupUpdateCheck()
{
    if (m_closing || m_startupStarted)
        return;
    m_startupStarted = true;
    m_startupPending = true;
    m_startupCheck.setSingleShot(true);
    connect(&m_startupCheck, &QTimer::timeout, this, [this] {
        m_startupReady = true;
        tryStartupUpdateCheck();
    });
    QNetworkInformation::loadDefaultBackend();
    if (auto *information = QNetworkInformation::instance())
        connect(information, &QNetworkInformation::reachabilityChanged, this,
                [this, previous = information->reachability()](
                    QNetworkInformation::Reachability value) mutable {
                    // Learning the backend's initial state is not a reconnect.
                    const bool recovered = previous != QNetworkInformation::Reachability::Unknown &&
                                           previous != value &&
                                           value == QNetworkInformation::Reachability::Online;
                    previous = value;
                    if (recovered)
                        startupNetworkChanged();
                });
    m_startupCheck.start(5000);
}

void PluginManager::startupNetworkChanged()
{
    if (m_closing || m_startupChecked || m_startupChecking)
        return;
    m_startupPending = true;
    tryStartupUpdateCheck();
}

void PluginManager::tryStartupUpdateCheck()
{
    if (m_closing || !m_startupReady || m_startupChecked)
        return;
    if (m_startupChecking) {
        if (!m_catalogLoading) {
            m_startupChecking = false;
            m_startupChecked = catalogCheckSucceeded();
        }
        return;
    }
    if (!m_startupPending)
        return;
    if (const auto *information = QNetworkInformation::instance();
        information &&
        information->reachability() == QNetworkInformation::Reachability::Disconnected)
        return;
    if (m_busy || m_batch || m_localBatch || m_runtime.loading())
        return; // The existing changed/loading notifications resume this attempt.
    m_startupPending = false;
    if (!m_catalogLoading && catalogCheckSucceeded()) {
        m_startupChecked = true;
        return;
    }
    m_startupChecking = true;
    if (!m_catalogLoading)
        refreshCatalogImpl(true,
                           true); // Share a page's in-flight fetch; otherwise check installed IDs.
}
} // namespace EchoMusic
