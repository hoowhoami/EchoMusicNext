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

bool PluginManager::safePath(const QString &path)
{
    if (path.isEmpty() || path.size() > 1024 || path.contains('\\') || path.contains(':') ||
        path.startsWith('/') || path.endsWith('/'))
        return false;
    for (const auto &part : path.split('/'))
        if (part.isEmpty() || part == "." || part == ".." || part.startsWith('.'))
            return false;
    return true;
}
QVariantMap PluginManager::normalizeSource(const QString &text, const QString &name)
{
    QUrl url(text.trimmed());
    if (url.scheme() != "https" || url.host().toLower() != "github.com" ||
        !url.userInfo().isEmpty() || url.port() != -1 || url.hasQuery() || url.hasFragment())
        return {};
    const auto parts = url.path().split('/', Qt::SkipEmptyParts);
    if (parts.size() != 2 || !QRegularExpression("^[A-Za-z0-9_.-]+$").match(parts[0]).hasMatch() ||
        !QRegularExpression("^[A-Za-z0-9_.-]+$").match(parts[1]).hasMatch())
        return {};
    auto repo = parts[1];
    if (repo.endsWith(".git"))
        repo.chop(4);
    const auto repository = parts[0] + '/' + repo;
    const auto canonical = "https://github.com/" + repository;
    return {{"id", "github:" + repository.toLower()},
            {"name", name.trimmed().isEmpty() ? repo : name.trimmed().left(128)},
            {"url", canonical},
            {"repository", repository},
            {"enabled", true},
            {"official", canonical.compare(official, Qt::CaseInsensitive) == 0},
            {"pluginCount", 0},
            {"lastError", ""}};
}
void PluginManager::setNetworkPreferences(NetworkPreferences *network)
{
    m_networkPreferences = network;
}
void PluginManager::get(const QUrl &url, qint64 limit, Completion complete, const QByteArray &body,
                        int timeout)
{
    const auto target = body.isEmpty() && m_networkPreferences
                            ? m_networkPreferences->acceleratedGithubUrl(url)
                            : url;
    getAttempt(target, url, limit, std::move(complete), body, timeout, target != url);
}
void PluginManager::getAttempt(const QUrl &url, const QUrl &origin, qint64 limit,
                               Completion complete, const QByteArray &body, int timeout,
                               bool fallback)
{
    if (m_closing)
        return;
    QNetworkRequest request(m_resolveUrl ? m_resolveUrl(url) : url);
    request.setTransferTimeout(timeout);
    request.setRawHeader("User-Agent", "EchoMusicNext-Plugin-Marketplace/0.1");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!body.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto *reply = body.isEmpty() ? m_network.get(request) : m_network.post(request, body);
    m_replies.insert(reply);
    auto bytes = std::make_shared<QByteArray>();
    auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QIODevice::readyRead, this, [reply, bytes, limit, tooLarge] {
        bytes->append(reply->read(limit - bytes->size() + 1));
        if (bytes->size() > limit) {
            *tooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, bytes, limit, tooLarge, complete = std::move(complete), origin, body,
             timeout, fallback] {
                m_replies.remove(reply);
                if (reply->isOpen())
                    bytes->append(reply->read(limit - bytes->size() + 1));
                const auto status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QString error = *tooLarge || bytes->size() > limit ? tr("响应超过大小限制")
                                : reply->error() != QNetworkReply::NoError || status != 200
                                    ? tr("HTTP %1：%2").arg(status).arg(reply->errorString())
                                    : QString();
                reply->deleteLater();
                if (!m_closing && fallback && !error.isEmpty())
                    getAttempt(origin, origin, limit, std::move(complete), body, timeout, false);
                else if (!m_closing)
                    complete(*bytes, error);
            });
}
void PluginManager::fetchSource(QVariantMap source, quint64 generation,
                                std::function<void(QVariantMap, QVariantList)> complete)
{
    get(QUrl(raw(source, "echo-plugins.json")), 256 * 1024,
        [this, source, generation, complete](QByteArray bytes, QString error) mutable {
            if (generation != m_generation)
                return;
            QJsonParseError parse;
            const auto doc = QJsonDocument::fromJson(bytes, &parse).object();
            if (error.isEmpty() &&
                (parse.error != QJsonParseError::NoError ||
                 doc.value("schemaVersion").toInt() != 2 || !doc.value("plugins").isArray() ||
                 doc.value("plugins").toArray().size() > 512))
                error = tr("插件索引无效");
            if (!error.isEmpty()) {
                source["lastError"] = error;
                complete(source, {});
                return;
            }
            auto paths = std::make_shared<QStringList>();
            auto artifacts = std::make_shared<QHash<QString, QVariantMap>>();
            for (const auto &value : doc.value("plugins").toArray()) {
                const auto path = value.toObject().value("path").toString();
                const auto artifact = value.toObject().value("artifact").toString();
                const auto checksum = value.toObject().value("sha256").toString();
                if (!safePath(path) || paths->contains(path) || !safePath(artifact) ||
                    !artifact.endsWith(".echomod", Qt::CaseInsensitive) ||
                    !QRegularExpression("^[a-f0-9]{64}$").match(checksum).hasMatch()) {
                    source["lastError"] = tr("插件索引路径无效或重复");
                    complete(source, {});
                    return;
                }
                paths->append(path);
                artifacts->insert(path, {{"artifact", artifact}, {"sha256", checksum}});
            }
            auto results = std::make_shared<QVariantList>();
            auto current = std::make_shared<int>(0);
            auto next = std::make_shared<std::function<void()>>();
            // Serial per-source requests bound memory and preserve index order.
            // Break the callback cycle on every terminal path.
            std::weak_ptr<std::function<void()>> weakNext = next;
            *next = [this, source, generation, complete, paths, artifacts, results, current,
                     weakNext]() mutable {
                const auto next = weakNext.lock();
                if (!next)
                    return;
                if (generation != m_generation || m_closing) {
                    *next = {};
                    return;
                }
                if (*current >= paths->size()) {
                    source["lastError"] = "";
                    source["pluginCount"] = results->size();
                    source["lastFetchedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
                    auto finish = complete;
                    *next = {};
                    finish(source, *results);
                    return;
                }
                const auto path = paths->at((*current)++);
                get(QUrl(raw(source, path + "/manifest.json")), 256 * 1024,
                    [this, source, generation, complete, path, artifacts, results,
                     next](QByteArray bytes, QString error) mutable {
                        if (generation != m_generation) {
                            *next = {};
                            return;
                        }
                        auto package = PluginPackage::inspectManifest(bytes);
                        if (error.isEmpty() && !package.error.isEmpty() && !package.incompatible)
                            error = package.error;
                        if (!error.isEmpty()) {
                            source["lastError"] = error;
                            auto finish = complete;
                            *next = {};
                            finish(source, {});
                            return;
                        }
                        auto record = package.manifest;
                        record.insert("key", source.value("id").toString() + ':' + package.id);
                        record.insert("sourceId", source.value("id"));
                        record.insert("sourceName", source.value("name"));
                        record.insert("sourceUrl", source.value("url"));
                        record.insert("repository", source.value("repository"));
                        record.insert("path", path);
                        record.insert("artifact", artifacts->value(path).value("artifact"));
                        record.insert("sha256", artifacts->value(path).value("sha256"));
                        record.insert("compatible", package.error.isEmpty());
                        record.insert("error", package.error);
                        record.insert(
                            "iconUrl",
                            package.manifest.value("icon").toString().isEmpty()
                                ? QString()
                                : raw(source,
                                      path + '/' + package.manifest.value("icon").toString()));
                        record.insert("manifest", package.manifest);
                        for (const auto &old : *results)
                            if (old.toMap().value("id") == record.value("id")) {
                                source["lastError"] = tr("插件索引包含重复ID");
                                auto finish = complete;
                                *next = {};
                                finish(source, {});
                                return;
                            }
                        results->append(record);
                        auto continueFetch = *next;
                        continueFetch();
                    });
            };
            auto begin = *next;
            begin();
        });
}
void PluginManager::refreshCatalog(bool force)
{
    if (force)
        m_shareInstallError.clear();
    if (m_closing || m_catalogLoading || m_busy || (m_loaded && !force))
        return;
    m_catalogLoading = true;
    emit changed();
    const auto generation = ++m_generation;
    auto pending = std::make_shared<int>(0);
    for (const auto &value : m_sources)
        if (value.toMap().value("enabled").toBool())
            ++*pending;
    if (!*pending) {
        m_catalogLoading = false;
        m_loaded = true;
        rebuild();
        save();
        return;
    }
    for (const auto &value : m_sources) {
        auto source = value.toMap();
        if (!source.value("enabled").toBool())
            continue;
        fetchSource(source, generation,
                    [this, generation, pending, force](QVariantMap source, QVariantList plugins) {
                        if (generation != m_generation)
                            return;
                        const auto id = source.value("id");
                        for (auto &value : m_sources)
                            if (value.toMap().value("id") == id)
                                value = source;
                        if (source.value("lastError").toString().isEmpty()) {
                            for (int i = int(m_entries.size()) - 1; i >= 0; --i)
                                if (m_entries[i].toMap().value("sourceId") == id)
                                    m_entries.removeAt(i);
                            m_entries.append(plugins);
                        }
                        if (--*pending == 0) {
                            m_catalogLoading = false;
                            m_loaded = true;
                            m_fetchedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
                            save();
                            refreshStatistics(force);
                        }
                        rebuild();
                    });
    }
}
QVariantList PluginManager::sourceOptions() const
{
    QVariantList values{QVariantMap{{"label", tr("全部源")}, {"value", ""}}};
    for (const auto &value : m_sources) {
        const auto source = value.toMap();
        values.append(QVariantMap{{"label", source.value("name")},
                                  {"value", source.value("id")},
                                  {"disabled", !source.value("enabled").toBool()}});
    }
    return values;
}
QString PluginManager::sourceSummary() const
{
    const auto enabled = std::count_if(m_sources.cbegin(), m_sources.cend(), [](const auto &value) {
        return value.toMap().value("enabled").toBool();
    });
    return m_sources.isEmpty() ? tr("暂无插件源")
                               : tr("%1/%2 个源启用").arg(enabled).arg(m_sources.size());
}
QStringList PluginManager::sourceNames() const
{
    QStringList values{tr("全部插件源")};
    for (const auto &source : m_sources)
        if (source.toMap().value("enabled").toBool())
            values.append(source.toMap().value("name").toString());
    return values;
}
QStringList PluginManager::sourceIds() const
{
    QStringList values{""};
    for (const auto &source : m_sources)
        if (source.toMap().value("enabled").toBool())
            values.append(source.toMap().value("id").toString());
    return values;
}
QString PluginManager::sourceError() const
{
    for (const auto &value : m_sources) {
        auto s = value.toMap();
        if (s.value("enabled").toBool() && !s.value("lastError").toString().isEmpty())
            return s.value("name").toString() + "：" + s.value("lastError").toString();
    }
    return {};
}
int PluginManager::catalogCount() const
{
    int count = 0;
    const auto enabled = sourceIds();
    for (const auto &entry : m_entries)
        if (enabled.contains(entry.toMap().value("sourceId").toString()))
            ++count;
    return count;
}
int PluginManager::updateCount() const
{
    int count = 0;
    QSet<QString> ids;
    for (const auto &value : m_entries) {
        const auto e = value.toMap();
        const auto current =
            m_runtime.record(m_runtime.installationForId(e.value("id").toString()));
        if (availableUpdate(current, e) && !ids.contains(e.value("id").toString()) &&
            sourceIds().contains(e.value("sourceId").toString())) {
            ++count;
            ids.insert(e.value("id").toString());
        }
    }
    return count;
}
void PluginManager::rebuild()
{
    QVariantList rows;
    const auto enabled = sourceIds();
    for (const auto &value : m_entries) {
        auto e = value.toMap();
        if (!enabled.contains(e.value("sourceId").toString()) ||
            (!m_sourceFilter.isEmpty() && e.value("sourceId") != m_sourceFilter))
            continue;
        const auto text = e.value("name").toString() + ' ' + e.value("id").toString() + ' ' +
                          e.value("author").toString() + ' ' + e.value("sourceName").toString() +
                          ' ' + e.value("description").toString() + ' ' +
                          e.value("tags").toStringList().join(' ');
        if (!text.contains(m_search.trimmed(), Qt::CaseInsensitive))
            continue;
        const auto current =
            m_runtime.record(m_runtime.installationForId(e.value("id").toString()));
        const bool installed = !current.isEmpty();
        const bool update = availableUpdate(current, e);
        e["installed"] = installed;
        e["updateAvailable"] = update;
        e["installedVersion"] = current.value("version");
        e["installLabel"] = !e.value("compatible").toBool() ? tr("不兼容")
                            : installed                     ? update ? tr("更新") : tr("已安装")
                                                            : tr("安装");
        e["status"] = !e.value("compatible").toBool() ? tr("不兼容")
                      : update                        ? tr("可更新")
                      : installed                     ? tr("已安装")
                                                      : QString();
        e["busy"] = m_busy && e.value("key") == m_installingKey;
        e["stats"] = m_stats.value(e.value("key").toString());
        rows.append(e);
    }
    m_catalog.replace(rows);
    m_sourceModel.replace(m_sources);
    emit changed();
}
void PluginManager::save()
{
    m_preferences.setValue(EchoMusic::SettingsKeys::Plugins::Sources, m_sources);
    m_preferences.sync();
    QDir().mkpath(m_runtime.directory());
    QSaveFile cache(m_runtime.directory() + "/catalog.json");
    if (cache.open(QIODevice::WriteOnly)) {
        cache.write(QJsonDocument(QJsonObject{{"schemaVersion", 2},
                                              {"plugins", QJsonArray::fromVariantList(m_entries)},
                                              {"fetchedAt", m_fetchedAt},
                                              {"statistics", QJsonObject::fromVariantMap(m_stats)},
                                              {"statisticsAt", double(m_statsAt)},
                                              {"statisticsRetryAt", double(m_statsRetryAt)}})
                        .toJson());
        cache.commit();
    }
}
void PluginManager::setSearch(const QString &value)
{
    if (m_search == value)
        return;
    m_search = value;
    rebuild();
}
void PluginManager::setSourceFilter(const QString &value)
{
    if (m_sourceFilter == value)
        return;
    m_sourceFilter = value;
    rebuild();
}
bool PluginManager::addSource(const QString &url, const QString &name)
{
    if (m_catalogLoading || m_busy || m_closing)
        return false;
    const auto source = normalizeSource(url, name);
    if (source.isEmpty()) {
        emit notice(tr("请输入有效的GitHub仓库地址"), true);
        return false;
    }
    for (const auto &value : m_sources)
        if (value.toMap().value("id") == source.value("id")) {
            emit notice(tr("插件源已存在"), true);
            return false;
        }
    m_catalogLoading = true;
    emit changed();
    const auto generation = ++m_generation;
    fetchSource(source, generation, [this, generation](QVariantMap source, QVariantList plugins) {
        if (generation != m_generation)
            return;
        m_catalogLoading = false;
        if (!source.value("lastError").toString().isEmpty()) {
            if (source.value("id") == m_shareTarget.value("sourceId"))
                m_shareInstallError = source.value("lastError").toString();
            emit notice(source.value("lastError").toString(), true);
            emit sourceAdded(false);
        } else {
            m_sources.append(source);
            m_entries.append(plugins);
            save();
            emit notice(tr("插件源已添加"), false);
            emit sourceAdded(true);
        }
        rebuild();
    });
    return true;
}
bool PluginManager::patchSource(const QString &id, const QString &name, bool enabled)
{
    if (m_catalogLoading || m_busy || m_closing)
        return false;
    for (auto &value : m_sources) {
        auto source = value.toMap();
        if (source.value("id") != id)
            continue;
        if (!name.trimmed().isEmpty())
            source["name"] = name.trimmed().left(128);
        source["enabled"] = enabled;
        value = source;
        m_sourceFilter.clear();
        save();
        rebuild();
        if (enabled)
            refreshCatalog(true);
        return true;
    }
    return false;
}
bool PluginManager::removeSource(const QString &id)
{
    if (m_catalogLoading || m_busy || m_closing)
        return false;
    for (int i = 0; i < m_sources.size(); ++i)
        if (m_sources[i].toMap().value("id") == id) {
            if (m_sources[i].toMap().value("official").toBool())
                return false;
            m_sources.removeAt(i);
            for (int j = int(m_entries.size()) - 1; j >= 0; --j)
                if (m_entries[j].toMap().value("sourceId") == id)
                    m_entries.removeAt(j);
            m_sourceFilter.clear();
            save();
            rebuild();
            return true;
        }
    return false;
}
} // namespace EchoMusic
