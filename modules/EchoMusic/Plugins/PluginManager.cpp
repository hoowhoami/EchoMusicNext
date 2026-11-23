#include "PluginPaths.h"
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
#include "PluginArchiveDownload_p.h"
namespace EchoMusic
{
using namespace PluginManagerDetail;

PluginManager::PluginManager(PluginRuntime &runtime, EchoMusic::SettingsStore &preferences,
                             QObject *parent)
    : QObject(parent), m_runtime(runtime), m_preferences(preferences), m_catalog(this),
      m_sourceModel(this), m_network(this)
{
    m_sources = preferences.value(EchoMusic::SettingsKeys::Plugins::Sources).toList();
    bool found = false;
    for (const auto &value : m_sources)
        if (value.toMap().value("url") == official)
            found = true;
    if (!found)
        m_sources.prepend(normalizeSource(official, tr("EchoMusicNext 官方插件源")));
    QFile cache(PluginPaths(runtime.directory()).cache() + "/marketplace/catalog.json");
    if (cache.open(QIODevice::ReadOnly) && cache.size() <= 8 * 1024 * 1024) {
        const auto doc = QJsonDocument::fromJson(cache.readAll()).object();
        if (doc.value("schemaVersion").toInt() == 2) {
            m_entries = doc.value("plugins").toArray().toVariantList();
            // Compatibility belongs to this host, never to the machine which
            // wrote a portable cache or to an older SDK feature catalog.
            for (auto &value : m_entries) {
                auto record = value.toMap();
                const auto package = PluginPackage::inspectManifest(
                    QJsonDocument::fromVariant(record.value("manifest")).toJson());
                applyPackageCompatibility(record, package);
                value = record;
            }
            m_fetchedAt = doc.value("fetchedAt").toString();
            m_stats = doc.value("statistics").toObject().toVariantMap();
            m_statsAt = qint64(doc.value("statisticsAt").toDouble());
            m_statsRetryAt = qint64(doc.value("statisticsRetryAt").toDouble());
        }
    }
    connect(&runtime, &PluginRuntime::catalogChanged, this, &PluginManager::rebuild);
    connect(&runtime, &PluginRuntime::loadingChanged, this, [this] {
        publishUpdates();
        tryStartupUpdateCheck();
    });
    connect(&runtime, &PluginRuntime::managementStarted, this,
            [this](const QString &installation, const QString &pluginId) {
                if (m_busy && pluginId == m_pendingPluginId)
                    m_pendingInstallation = installation;
            });
    connect(&runtime, &PluginRuntime::managementFinished, this,
            [this](const QString &installation, bool ok, const QString &error) {
                if (m_busy && installation == m_pendingInstallation)
                    finishInstall(ok, error);
                else
                    emit notice(ok ? (m_runtime.record(installation).isEmpty() ? tr("插件已卸载")
                                                                               : tr("插件已安装"))
                                   : error,
                                !ok);
            });
    connect(this, &PluginManager::changed, this, &PluginManager::resolveShared);
    connect(this, &PluginManager::changed, this, &PluginManager::publishUpdates);
    connect(this, &PluginManager::changed, this, &PluginManager::tryStartupUpdateCheck);
    rebuild();
}
void PluginManager::attachRecordStore(JsonRecordStore &store)
{
    if (m_recordStore)
        return;
    m_recordStore = &store;
    connect(&store, &JsonRecordStore::invalidated, this, [this] {
        ++m_saveRevision;
        m_manifests.clear();
        // The existing on-disk marketplace snapshot is another disposable
        // source. Reattach exact publications before falling back to the network.
        QFile cache(PluginPaths(m_runtime.directory()).cache() + "/marketplace/catalog.json");
        QHash<QString, QVariantMap> records;
        if (cache.open(QIODevice::ReadOnly) && cache.size() <= 8 * 1024 * 1024) {
            const auto doc = QJsonDocument::fromJson(cache.readAll()).object();
            if (doc.value("schemaVersion").toInt() == 2)
                for (const auto &value : doc.value("plugins").toArray()) {
                    const auto record = value.toObject().toVariantMap();
                    records.insert(record.value("key").toString(), record);
                }
        }
        m_manifestRecovery = false;
        for (auto &value : m_entries) {
            auto row = value.toMap();
            const auto record = records.value(row.value("key").toString());
            if (!record.isEmpty() && record.value("version") == row.value("version") &&
                record.value("sha256") == row.value("sha256") && record.contains("manifest")) {
                row.insert("manifest", record.value("manifest"));
                value = row;
            } else
                m_manifestRecovery = true;
        }
        cacheManifests();
        emit changed();
    });
    connect(this, &PluginManager::changed, this, [this] {
        if (!m_manifestRecovery || m_closing || m_catalogLoading || m_busy || m_batch ||
            m_localBatch)
            return;
        m_manifestRecovery = false;
        m_loaded = m_catalogComplete = false;
        refreshCatalog(true);
    });
    cacheManifests();
    rebuild();
}
void PluginManager::cacheManifests()
{
    if (!m_recordStore)
        return;
    QJsonArray manifests;
    QStringList keys;
    QSet<QString> live;
    for (auto &value : m_entries) {
        auto record = value.toMap();
        const auto key = record.value("key").toString();
        live.insert(key);
        if (!record.contains("manifest"))
            continue;
        manifests.append(QJsonObject::fromVariantMap(record));
        // Rich declarations remain in the immutable source. The synchronous
        // catalog index contains only card/search/update/source identity data.
        static const QSet<QString> fields{"key",
                                          "id",
                                          "name",
                                          "version",
                                          "author",
                                          "description",
                                          "tags",
                                          "category",
                                          "sourceId",
                                          "sourceName",
                                          "sourceUrl",
                                          "repo",
                                          "repository",
                                          "ref",
                                          "path",
                                          "artifact",
                                          "artifactUrl",
                                          "sha256",
                                          "manifestUrl",
                                          "iconUrl",
                                          "compatible",
                                          "error",
                                          "platforms",
                                          "platformLabel",
                                          "platformIncompatible",
                                          "stale",
                                          "refreshError",
                                          "refreshErrorDetail"};
        for (auto it = record.begin(); it != record.end();)
            if (!fields.contains(it.key()))
                it = record.erase(it);
            else
                ++it;
        keys.append(key);
        value = std::move(record);
    }
    if (!keys.isEmpty()) {
        auto snapshot = m_recordStore->create(std::move(manifests));
        for (int i = 0; i < keys.size(); ++i)
            m_manifests.insert(keys[i], {snapshot, i});
    }
    for (auto it = m_manifests.begin(); it != m_manifests.end();)
        if (!live.contains(it.key()))
            it = m_manifests.erase(it);
        else
            ++it;
}
void PluginManager::readManifest(const QString &key, std::function<void(QVariantMap, QString)> done)
{
    if (!m_recordStore) {
        done(entry(key).value("manifest").toMap(), {});
        return;
    }
    const auto ref = m_manifests.value(key);
    if (!ref.snapshot) {
        done({}, tr("插件发布清单不可用，请刷新后重试"));
        return;
    }
    m_recordStore->read(
        ref, this, [this, key, ref, done = std::move(done)](QJsonArray rows, QString error) {
            const auto replacement = m_manifests.value(key);
            if (!error.isEmpty() && replacement.snapshot && replacement.snapshot != ref.snapshot) {
                readManifest(key, std::move(done));
                return;
            }
            done(rows.isEmpty()
                     ? QVariantMap{}
                     : rows.first().toObject().value("manifest").toObject().toVariantMap(),
                 error);
        });
}
PluginManager::~PluginManager()
{
    shutdown();
}
QVariantMap PluginManager::entry(const QString &key) const
{
    for (const auto &value : m_entries)
        if (value.toMap().value("key") == key)
            return value.toMap();
    return {};
}
bool PluginManager::openDirectory()
{
    const bool opened = QDir().mkpath(m_runtime.directory()) &&
                        QDesktopServices::openUrl(QUrl::fromLocalFile(m_runtime.directory()));
    if (!opened)
        emit notice(tr("打开插件目录失败"), true);
    return opened;
}
bool PluginManager::openRepository(const QString &key)
{
    const auto url = entry(key).value("repo").toString();
    return !url.isEmpty() && QDesktopServices::openUrl(QUrl(url));
}
void PluginManager::shutdown()
{
    if (m_closing)
        return;
    m_closing = true;
    m_startupCheck.stop();
    if (m_tasks)
        m_tasks->remove(m_updateTask);
    m_updateTask = {};
    ++m_generation;
    if (m_archiveDownload)
        m_archiveDownload->cancel();
    const auto replies = m_replies;
    for (auto *reply : replies)
        reply->abort();
    m_archiveDownload.clear();
    // Extraction/import workers retain their own staging lease until drained.
    m_staging.clear();
    m_updateQueue.clear();
    m_localQueue.clear();
}
} // namespace EchoMusic
