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

void PluginSettlement::resolved()
{
    if (std::exchange(m_done, true))
        return;
    timeout.stop();
    auto callback = std::exchange(complete, {});
    if (callback)
        callback({});
    deleteLater();
}
void PluginSettlement::rejected(const QString &message)
{
    if (std::exchange(m_done, true))
        return;
    timeout.stop();
    auto callback = std::exchange(complete, {});
    if (callback)
        callback(message.isEmpty() ? "Plugin Promise rejected" : message);
    deleteLater();
}
void PluginSettlement::invalidate()
{
    m_done = true;
    timeout.stop();
    complete = {};
    deleteLater();
}
PluginRuntime::PluginRuntime(QQmlEngine &engine, QSettings &preferences, PluginHost host,
                             QString root, bool safeMode)
    : m_engine(engine), m_preferences(preferences), m_host(host), m_root(std::move(root)),
      m_safeMode(safeMode ||
                 preferences.value(EchoMusic::SettingsKeys::Plugins::SafeMode, false).toBool())
{
    connect(this, &PluginRuntime::loadingChanged, this, &PluginRuntime::advanceRefresh,
            Qt::QueuedConnection);
    connect(this, &PluginRuntime::pluginFailed, this, [this] { m_refreshNotice = false; });
    connect(this, &PluginRuntime::managementFinished, this,
            [this](const QString &, bool success, const QString &) {
                if (!success)
                    m_refreshNotice = false;
            });
}
PluginRuntime::~PluginRuntime()
{
    Q_ASSERT(m_imports == 0);
    for (const auto &entry : m_entries)
        Q_ASSERT(!entry->context);
}
int PluginRuntime::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}
QVariant PluginRuntime::data(const QModelIndex &item, int role) const
{
    if (!item.isValid() || item.row() < 0 || item.row() >= rowCount())
        return {};
    const auto &entry = *m_entries[size_t(item.row())];
    switch (role) {
    case IdRole:
        return entry.package.id;
    case NameRole:
        return entry.package.name;
    case VersionRole:
        return entry.package.version;
    case StateRole:
        return int(entry.state);
    case EnabledRole:
        return entry.enabled;
    case ErrorRole:
        return entry.error;
    case InstallationRole:
        return entry.package.installationId;
    case RecordRole:
        return record(entry.package.installationId);
    default:
        return {};
    }
}
QHash<int, QByteArray> PluginRuntime::roleNames() const
{
    return {{IdRole, "pluginId"},
            {NameRole, "pluginName"},
            {VersionRole, "pluginVersion"},
            {StateRole, "pluginState"},
            {EnabledRole, "pluginEnabled"},
            {ErrorRole, "pluginError"},
            {InstallationRole, "installationId"},
            {RecordRole, "pluginRecord"}};
}
PluginRuntime::Entry *PluginRuntime::find(const QString &installation) const
{
    for (const auto &entry : m_entries)
        if (entry->package.installationId == installation)
            return entry.get();
    return nullptr;
}
EchoPluginContext *PluginRuntime::context(const QString &installation) const
{
    auto *entry = find(installation);
    return entry ? entry->context.data() : nullptr;
}
PluginRuntime::State PluginRuntime::state(const QString &installation) const
{
    auto *entry = find(installation);
    return entry ? entry->state : Invalid;
}
bool PluginRuntime::loading() const
{
    if (m_reconcileQueued && !m_closing && !m_safeMode && !m_maintenance)
        return true;
    if (m_imports || m_refreshing || m_startScanPending)
        return true;
    for (const auto &entry : m_entries)
        if (entry->retirementPending || entry->state == Discovered || entry->state == Activating ||
            entry->state == Deactivating)
            return true;
    return false;
}
void PluginRuntime::changed(Entry &entry)
{
    queueReconcile();
    emit catalogChanged();
    if (!entry.error.isEmpty() && entry.failure.value("message").toString() != entry.error) {
        entry.failure = {{"message", entry.error},
                         {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                         {"reason", entry.state == Incompatible ? "不兼容"
                                    : entry.state == Blocked    ? "依赖未就绪"
                                                                : "运行异常"}};
        m_preferences.setValue(EchoMusic::SettingsKeys::Plugins::FailuresPrefix +
                                   entry.package.installationId,
                               entry.failure);
    }
    if (!std::exchange(m_loadingNotification, true)) {
        QTimer::singleShot(0, this, [this] {
            m_loadingNotification = false;
            emit loadingChanged();
        });
    }
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].get() == &entry) {
            emit dataChanged(index(int(i)), index(int(i)));
            break;
        }
}
void PluginRuntime::persist()
{
    QVariantList catalog;
    for (const auto &entry : m_entries)
        if (!(entry->mutation == Entry::Updating && entry->newInstall))
            catalog.append(
                QVariantMap{{"source", entry->mutation == Entry::Updating && !entry->newInstall
                                           ? entry->previousSource
                                           : entry->source},
                            {"installation", entry->package.installationId},
                            {"root", entry->mutation == Entry::Updating && !entry->newInstall
                                         ? entry->previousPackage.root
                                         : entry->package.root},
                            {"origin", entry->mutation == Entry::Updating && !entry->newInstall
                                           ? entry->previousOrigin
                                           : entry->origin},
                            {"enabled", entry->enabled}});
    m_preferences.setValue(EchoMusic::SettingsKeys::Plugins::Installations, catalog);
    m_preferences.sync();
}
bool PluginRuntime::setEnabled(const QString &installation, bool enabled)
{
    auto *entry = find(installation);
    if (!entry || m_closing || m_maintenance || (enabled && m_imports) ||
        m_preferences.value(EchoMusic::SettingsKeys::Plugins::RollbackPrefix + installation, false)
            .toBool() ||
        entry->state == Discovered || entry->state == Invalid || entry->state == Incompatible ||
        entry->mutation != Entry::None || entry->state == Deactivating ||
        (enabled && entry->state == Activating)) {
        emit notice(tr("插件启停失败，请稍后重试"), true);
        return false;
    }
    entry->enabled = enabled;
    if (!enabled && !entry->context && entry->state == Blocked) {
        entry->state = Disabled;
        entry->error.clear();
    }
    persist();
    changed(*entry);
    if (!enabled && entry->context)
        retire(*entry, Disabled);
    else if (enabled && !m_safeMode && !entry->context) {
        if (entry->state == Failed) {
            entry->state = Disabled;
            entry->error.clear();
            changed(*entry);
        }
        queueReconcile();
    }
    emit notice(enabled ? tr("插件已启用") : tr("插件已禁用"), false);
    return true;
}
bool PluginRuntime::reload(const QString &installation)
{
    auto *entry = find(installation);
    if (!entry || m_closing || m_maintenance || m_imports ||
        m_preferences.value(EchoMusic::SettingsKeys::Plugins::RollbackPrefix + installation, false)
            .toBool() ||
        entry->state == Discovered || entry->state == Deactivating || entry->state == Activating ||
        entry->mutation != Entry::None)
        return false;
    if (entry->context) {
        entry->reloadPending = true;
        retire(*entry, Disabled);
    } else
        load(*entry, QFileInfo::exists(entry->source) ? entry->source : entry->package.root);
    return true;
}
bool PluginRuntime::retry(const QString &installation)
{
    auto *entry = find(installation);
    if (!entry || m_closing || m_safeMode || entry->context ||
        (entry->state != Failed && entry->state != Blocked))
        return false;
    return setEnabled(installation, true);
}
EchoPluginOperation *PluginRuntime::executeCommand(const QString &installation, const QString &id,
                                                   const QVariantMap &arguments)
{
    auto *entry = find(installation);
    return entry && entry->state == Active && entry->context
               ? entry->context->executeCommand(id, arguments)
               : nullptr;
}
int PluginRuntime::enabledCount() const
{
    return int(std::count_if(m_entries.begin(), m_entries.end(),
                             [](const auto &entry) { return entry->enabled; }));
}
QString PluginRuntime::installationForId(const QString &id) const
{
    for (const auto &entry : m_entries)
        if (entry->package.id == id)
            return entry->package.installationId;
    return {};
}
QVariantMap PluginRuntime::record(const QString &installation) const
{
    const auto *entry = find(installation);
    if (!entry)
        return {};
    QString status =
        !entry->enabled                ? tr("已禁用")
        : m_safeMode                   ? tr("安全模式")
        : entry->state == Active       ? tr("运行中")
        : entry->state == Failed       ? tr("出错")
        : entry->state == Invalid      ? tr("无效")
        : entry->state == Incompatible ? tr("不兼容")
        : entry->state == Blocked      ? tr("依赖未就绪")
        : entry->state == Activating || entry->state == Deactivating || entry->state == Discovered
            ? tr("加载中")
            : tr("未运行");
    auto result = entry->package.manifest;
    result.insert("installationId", installation);
    result.insert("status", status);
    result.insert("enabled", entry->enabled);
    result.insert("busy", entry->retirementPending || entry->mutation != Entry::None ||
                              entry->state == Discovered || entry->state == Activating ||
                              entry->state == Deactivating);
    result.insert("compatible", entry->state != Invalid && entry->state != Incompatible);
    result.insert("dependencyBlocked", entry->state == Blocked);
    result.insert("error", entry->error);
    result.insert("failure", entry->failure);
    result.insert("settingsAvailable", entry->context && entry->context->active() &&
                                           !entry->context->ui()->settingsComponent().isEmpty());
    result.insert("sourceName", entry->origin.value("name", tr("本地安装")));
    result.insert("origin", entry->origin);
    result.insert("iconUrl", QUrl::fromLocalFile(entry->package.resolve(
                                 entry->package.manifest.value("icon").toString())));
    return result;
}
bool PluginRuntime::clearFailure(const QString &installation)
{
    auto *entry = find(installation);
    if (!entry || !entry->error.isEmpty())
        return false;
    entry->failure.clear();
    m_preferences.remove(EchoMusic::SettingsKeys::Plugins::FailuresPrefix + installation);
    changed(*entry);
    return true;
}
bool PluginRuntime::setSafeMode(bool enabled)
{
    if (m_closing || m_maintenance || loading())
        return false;
    if (m_safeMode == enabled)
        return true;
    m_safeMode = enabled;
    m_preferences.setValue(EchoMusic::SettingsKeys::Plugins::SafeMode, enabled);
    m_preferences.sync();
    for (const auto &entry : m_entries) {
        if (enabled && entry->context)
            retire(*entry, Disabled);
        changed(*entry);
    }
    emit safeModeChanged();
    emit notice(enabled ? tr("插件安全模式已开启") : tr("插件安全模式已关闭"), false);
    return true;
}
void PluginRuntime::shutdown()
{
    if (std::exchange(m_closing, true))
        return;
    if (auto ready = std::exchange(m_maintenanceReady, {}))
        ready(false);
    for (const auto &entry : m_entries)
        if (entry->context)
            retire(*entry, Disabled);
    checkClosed();
}
bool PluginRuntime::beginMaintenance(std::function<void(bool)> ready)
{
    if (m_closing || m_maintenance || loading())
        return false;
    for (const auto &entry : m_entries)
        if (entry->mutation != Entry::None)
            return false;
    m_maintenance = true;
    emit loadingChanged();
    m_maintenanceReady = std::move(ready);
    for (const auto &entry : m_entries)
        if (entry->context)
            retire(*entry, Disabled);
    QTimer::singleShot(0, this, &PluginRuntime::checkMaintenance);
    return true;
}
void PluginRuntime::checkMaintenance()
{
    if (!m_maintenance || !m_maintenanceReady || m_closing)
        return;
    for (const auto &entry : m_entries)
        if (entry->context)
            return;
    auto ready = std::exchange(m_maintenanceReady, {});
    ready(true);
}
void PluginRuntime::endMaintenance()
{
    if (!m_maintenance)
        return;
    m_maintenance = false;
    auto ready = std::exchange(m_maintenanceReady, {});
    if (ready)
        ready(false);
    queueReconcile();
    emit loadingChanged();
}
QVariantList PluginRuntime::backupEntries() const
{
    Q_ASSERT(m_maintenance || m_closed);
    QVariantList entries;
    for (const auto &entry : m_entries) {
        if (!entry->package.error.isEmpty() || entry->package.root.isEmpty())
            continue;
        auto origin = entry->origin;
        origin.remove("directory");
        entries.append(QVariantMap{{"id", entry->package.id},
                                   {"name", entry->package.name},
                                   {"installation", entry->package.installationId},
                                   {"root", entry->package.root},
                                   {"origin", origin},
                                   {"enabled", entry->enabled}});
    }
    return entries;
}
void PluginRuntime::checkClosed()
{
    if (!m_closing || m_closed || m_imports)
        return;
    for (const auto &entry : m_entries)
        if (entry->context)
            return;
    m_closed = true;
    // One queued boundary releases pending page delegates/context deleteLater.
    QTimer::singleShot(0, this, [this] { emit closed(); });
}
} // namespace EchoMusic
