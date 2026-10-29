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
            m_fetchedAt = doc.value("fetchedAt").toString();
            m_stats = doc.value("statistics").toObject().toVariantMap();
            m_statsAt = qint64(doc.value("statisticsAt").toDouble());
            m_statsRetryAt = qint64(doc.value("statisticsRetryAt").toDouble());
        }
    }
    connect(&runtime, &PluginRuntime::catalogChanged, this, &PluginManager::rebuild);
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
    rebuild();
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
    ++m_generation;
    const auto replies = m_replies;
    for (auto *reply : replies)
        reply->abort();
    m_updateQueue.clear();
    m_localQueue.clear();
}
} // namespace EchoMusic
