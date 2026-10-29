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

void PluginManager::attachSharing(SharingCommands &sharing)
{
    m_sharing = &sharing;
}
void PluginManager::share(const QString &key)
{
    const auto e = entry(key);
    if (e.isEmpty())
        return;
    if (m_sharing) {
        m_sharing->copyTarget({ShareResource::Plugin,
                               e.value("id").toString(),
                               {{"source", e.value("sourceUrl").toString()},
                                {"version", e.value("version").toString()},
                                {"path", e.value("path").toString()}}},
                              e.value("name").toString());
        return;
    }
    QUrl url("echomusicnext://plugin");
    QUrlQuery query;
    query.addQueryItem("source", e.value("sourceUrl").toString());
    query.addQueryItem("id", e.value("id").toString());
    query.addQueryItem("version", e.value("version").toString());
    query.addQueryItem("path", e.value("path").toString());
    url.setQuery(query);
    QGuiApplication::clipboard()->setText(url.toString());
    emit notice(tr("插件分享链接已复制"), false);
}
bool PluginManager::openShare(const ShareTarget &target)
{
    if (target.type != ShareResource::Plugin || !validShareTarget(target))
        return false;
    // The existing manager remains the owner of source approval, manifest
    // checks and installation. Encode each value once at this typed boundary.
    QByteArray query = "id=" + QUrl::toPercentEncoding(target.id);
    for (const auto &key : {"source", "version", "path"})
        query += '&' + QByteArray(key) + '=' + QUrl::toPercentEncoding(target.query.value(key));
    QUrl url("echomusicnext://plugin");
    url.setQuery(QString::fromLatin1(query), QUrl::StrictMode);
    return openShare(url);
}
bool PluginManager::openShare(const QUrl &url)
{
    if (m_closing || m_busy || m_catalogLoading || !url.isValid() ||
        url.toEncoded().size() > 8192 || url.scheme() != "echomusicnext" ||
        url.host() != "plugin" || !url.userInfo().isEmpty() || url.port() != -1 ||
        url.hasFragment() || (!url.path().isEmpty() && url.path() != "/"))
        return false;
    const QUrlQuery query(url);
    QSet<QString> names;
    for (const auto &item : query.queryItems()) {
        if (!QStringList{"source", "id", "version", "path"}.contains(item.first) ||
            names.contains(item.first))
            return false;
        names.insert(item.first);
    }
    const auto source = normalizeSource(query.queryItemValue("source", QUrl::FullyDecoded), {});
    const auto id = query.queryItemValue("id", QUrl::FullyDecoded);
    const auto path = query.queryItemValue("path", QUrl::FullyDecoded);
    const auto version = query.queryItemValue("version", QUrl::FullyDecoded);
    if (source.isEmpty() || !QRegularExpression("^[a-z][a-z0-9.-]{1,127}$").match(id).hasMatch() ||
        (!path.isEmpty() && !safePath(path)) ||
        !QRegularExpression("^[0-9]+\\.[0-9]+\\.[0-9]+$").match(version).hasMatch())
        return false;
    m_shareTarget = {{"sourceId", source.value("id")},
                     {"sourceUrl", source.value("url")},
                     {"id", id},
                     {"path", path},
                     {"version", version}};
    m_shareRecord.clear();
    m_shareError.clear();
    m_shareInstallError.clear();
    m_shareState = "loading";
    emit shareOpened();
    bool enabled = false;
    for (const auto &value : m_sources)
        if (value.toMap().value("id") == source.value("id"))
            enabled = value.toMap().value("enabled").toBool();
    if (enabled)
        refreshCatalog(true);
    resolveShared();
    emit changed();
    return true;
}
void PluginManager::resolveShared()
{
    if (m_shareTarget.isEmpty() || m_closing)
        return;
    QString state, error;
    QVariantMap record, source;
    for (const auto &value : m_sources)
        if (value.toMap().value("id") == m_shareTarget.value("sourceId"))
            source = value.toMap();
    if (!m_shareInstallError.isEmpty()) {
        state = "failed";
        error = m_shareInstallError;
    } else if (m_catalogLoading)
        state = "loading";
    else if (source.isEmpty() || !source.value("enabled").toBool())
        state = "need-source";
    else if (!source.value("lastError").toString().isEmpty()) {
        state = "failed";
        error = source.value("lastError").toString();
    } else {
        for (const auto &value : m_entries) {
            const auto candidate = value.toMap();
            if (candidate.value("sourceId") == m_shareTarget.value("sourceId") &&
                candidate.value("id") == m_shareTarget.value("id") &&
                candidate.value("path") == m_shareTarget.value("path")) {
                record = candidate;
                break;
            }
        }
        if (record.isEmpty()) {
            state = "failed";
            error = tr("插件源中未找到分享的插件");
        } else {
            record["sharedVersion"] = m_shareTarget.value("version");
            // EchoMusic warns about either direction of version drift; the
            // catalog's manifest and artifact remain the installation authority.
            const auto sharedVersion = m_shareTarget.value("version").toString();
            const auto version = record.value("version").toString();
            record["shareWarning"] =
                sharedVersion != version
                    ? tr("分享时版本为 v%1，当前插件源提供 v%2。").arg(sharedVersion, version)
                : !record.value("compatible").toBool() ? record.value("error").toString()
                                                       : QString();
            const auto current =
                m_runtime.record(m_runtime.installationForId(record.value("id").toString()));
            const bool update = availableUpdate(current, record);
            state = !current.isEmpty() && !update ? "installed" : "ready";
            record["installedVersion"] = current.value("version");
            record["updateAvailable"] = update;
        }
    }
    if (state != m_shareState || error != m_shareError || record != m_shareRecord) {
        m_shareState = state;
        m_shareError = error;
        m_shareRecord = record;
        emit changed();
    }
}
bool PluginManager::acceptShareSource()
{
    if (m_shareState != "need-source" || m_busy || m_catalogLoading)
        return false;
    for (const auto &value : m_sources) {
        const auto source = value.toMap();
        if (source.value("id") == m_shareTarget.value("sourceId"))
            return patchSource(source.value("id").toString(), source.value("name").toString(),
                               true);
    }
    return addSource(shareSourceUrl(), {});
}
bool PluginManager::installShared()
{
    if (m_shareState != "ready")
        return false;
    m_shareInstalling = true;
    if (install(m_shareRecord.value("key").toString()))
        return true;
    m_shareInstalling = false;
    return false;
}
EchoPluginContext *PluginManager::requestSettings(const QString &installation)
{
    const auto record = m_runtime.record(installation);
    auto *scope = m_runtime.context(installation);
    if (scope && scope->active() && !scope->ui()->settingsComponent().isEmpty())
        return scope;
    const auto message =
        !record.value("compatible").toBool() ? tr("插件与当前主程序版本不兼容，无法加载设置")
        : !record.value("enabled").toBool()  ? tr("启用插件后才能读取它提供的设置项")
        : m_runtime.safeMode()               ? tr("安全模式下不会加载插件设置")
        : record.value("status").toString() != tr("运行中") ? tr("插件运行后才能读取它提供的设置项")
                                                            : tr("该插件没有提供设置项");
    const bool warning = !record.value("compatible").toBool() ||
                         !record.value("enabled").toBool() || m_runtime.safeMode() ||
                         record.value("status").toString() != tr("运行中");
    emit notice(message, warning, !warning);
    return nullptr;
}
void PluginManager::openDocumentation()
{
    QDesktopServices::openUrl(QUrl(official + "/blob/main/docs/sdk.md"));
}

QString PluginManager::updateAllLabel() const
{
    return m_batch ? tr("更新中 %1/%2").arg(m_batchDone).arg(m_batchTotal)
                   : tr("更新全部 %1 个插件").arg(updateCount());
}
void PluginManager::refreshStatistics(bool force)
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (m_closing || m_statsLoading || now < m_statsRetryAt ||
        (!force && now - m_statsAt < 10 * 60 * 1000))
        return;
    QJsonArray plugins;
    QSet<QString> requested;
    for (const auto &value : m_entries) {
        const auto e = value.toMap();
        if (!sourceIds().contains(e.value("sourceId").toString()))
            continue;
        requested.insert(e.value("key").toString());
        plugins.append(QJsonObject{{"sourceId", e.value("sourceId").toString()},
                                   {"pluginId", e.value("id").toString()}});
    }
    if (plugins.isEmpty())
        return;
    m_statsLoading = true;
    const auto generation = m_generation;
    get(
        QUrl(statisticsEndpoint + "/v1/plugins/stats"), 1024 * 1024,
        [this, requested, generation](QByteArray bytes, QString error) {
            m_statsLoading = false;
            if (generation != m_generation)
                return;
            QVariantMap results;
            if (error.isEmpty())
                results = PluginMarketplaceStats::parse(bytes, requested, error);
            const auto now = QDateTime::currentMSecsSinceEpoch();
            if (!error.isEmpty()) {
                m_statsFailures = std::min(6, m_statsFailures + 1);
                m_statsRetryAt = now + std::min(30 * 60 * 1000, 30000 * (1 << m_statsFailures));
            } else {
                for (auto it = results.begin(); it != results.end(); ++it)
                    m_stats.insert(it.key(), it.value());
                m_statsAt = now;
                m_statsRetryAt = 0;
                m_statsFailures = 0;
            }
            save();
            rebuild();
        },
        QJsonDocument(QJsonObject{{"plugins", plugins}}).toJson(QJsonDocument::Compact), 10000);
}
void PluginManager::reportInstallation(const QVariantMap &record, bool success)
{
    const QString event = success ? m_installWasUpdate ? "update" : "install" : "failure";
    QJsonObject plugin{
        {"sourceId", record.value("sourceId").toString()},
        {"pluginId", record.value("id").toString()},
        {"version", record.value("version").toString()},
        {"sourceUrl", record.value("sourceUrl").toString()},
        {"sourceName",
         normalizeSource(record.value("sourceUrl").toString(), {}).value("repository").toString()},
        {"repo", record.value("repo").toString()},
        {"packagePath", record.value("path").toString()}};
    // Report canonical repository metadata, never user-defined source labels,
    // account/device/activation identity or private paths.
    get(
        QUrl(statisticsEndpoint + "/v1/plugins/events"), 1024 * 1024,
        [this](QByteArray, QString error) {
            if (error.isEmpty())
                refreshStatistics(true);
        },
        QJsonDocument(QJsonObject{{"event", event},
                                  {"plugin", plugin},
                                  {"error", success ? "" : "InstallFailed"}})
            .toJson(QJsonDocument::Compact),
        10000);
}
} // namespace EchoMusic
