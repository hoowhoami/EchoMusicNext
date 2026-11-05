#include "AudioEffectCatalog.h"
#include "AudioEffectCatalogCache.h"
#include "AudioEffectSearch.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>
#include <QUuid>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace EchoMusic
{
namespace
{
constexpr int pageSize = 50, scopeLimit = 64, recordLimit = 20000;
constexpr qint64 lifetime = 6LL * 60 * 60 * 1000;
QString identity(const QVariantMap &effect)
{
    return effect.value("source").toString() + "-" + effect.value("id").toString();
}
bool resource(const QUrl &url)
{
    const auto host = url.host().toLower();
    return url.scheme() == "https" && url.userName().isEmpty() && url.password().isEmpty() &&
           (host == "kugou.com" || host.endsWith(".kugou.com")) && url.port(443) == 443;
}
QStringList terms(const QString &query)
{
    QStringList result;
    for (const auto &term : query.simplified().split(' ', Qt::SkipEmptyParts)) {
        const auto normalized = AudioEffectSearch::normalize(term);
        if (!normalized.isEmpty())
            result.append(normalized);
    }
    return result;
}
} // namespace
AudioEffectCatalog::AudioEffectCatalog(ApiClient &api, PlaybackAudioControls &controls,
                                       QObject *parent, const QString &cachePath)
    : QAbstractListModel(parent), m_cache(std::make_unique<AudioEffectCatalogCache>(cachePath)),
      m_api(api), m_controls(controls)
{
    m_searchPool.setMaxThreadCount(1);
    m_next.setSingleShot(true);
    m_next.setInterval(250);
    m_filter.setSingleShot(true);
    m_filter.setInterval(120);
    connect(&m_next, &QTimer::timeout, this, &AudioEffectCatalog::step);
    connect(&m_filter, &QTimer::timeout, this, [this] { project(); });
    const auto closed = [this] {
        if (++m_closedParts == 2)
            emit this->closed();
    };
    connect(&m_installer, &ImpulseResponseInstaller::closed, this, closed);
    connect(m_cache.get(), &AudioEffectCatalogCache::closed, this, closed);
    connect(m_cache.get(), &AudioEffectCatalogCache::failed, this, [this](const QString &error) {
        m_cacheError = error;
        emit changed();
    });
    connect(m_cache.get(), &AudioEffectCatalogCache::loaded, this,
            [this](const QJsonObject &scopes) {
                if (m_closing)
                    return;
                for (auto it = scopes.begin(); it != scopes.end(); ++it) {
                    auto &bucket = m_buckets[it.key()];
                    if (!bucket.rows.isEmpty())
                        continue;
                    const auto object = it.value().toObject();
                    bucket.generation = object.value("generation").toString();
                    bucket.nextPage = qMax(1, object.value("nextPage").toInt(1));
                    bucket.total = object.value("total").toInt(-1);
                    bucket.updated = object.value("updated").toInteger();
                    bucket.used = object.value("used").toInteger();
                    bucket.complete = object.value("complete").toBool();
                    bucket.limited = object.value("limited").toBool();
                    for (const auto &value : object.value("rows").toArray()) {
                        auto row = value.toObject().toVariantMap();
                        row["key"] = entryKey(it.key(), row);
                        AudioEffectSearch::prepare(row);
                        if (row.value("_generation").toString() == bucket.generation)
                            bucket.seen.insert(identity(row));
                        bucket.rows.append(row);
                    }
                }
                m_cacheReady = true;
                trim();
                if (m_buckets[scope()].complete &&
                    QDateTime::currentMSecsSinceEpoch() - m_buckets[scope()].updated > lifetime)
                    resetScope(scope());
                project();
                schedule();
            });
    connect(&m_installer, &ImpulseResponseInstaller::progress, this,
            [this](quint64 generation, double progress) {
                if (!m_closing && generation == m_downloadGeneration) {
                    m_progress = progress;
                    emit changed();
                }
            });
    connect(&m_installer, &ImpulseResponseInstaller::finished, this,
            [this](quint64 generation, bool success, const QString &, const QString &error) {
                if (!m_closing && generation == m_downloadGeneration)
                    finishDownload(success, error);
            });
    connect(&controls, &PlaybackAudioControls::changed, this, [this] {
        if (rowCount())
            emit dataChanged(index(0), index(rowCount() - 1));
        emit changed();
    });
    m_revision = api.sessionRevision();
    connect(&api, &ApiClient::succeeded, this, &AudioEffectCatalog::succeeded);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &payload) {
        if (id == m_request && id)
            failRead(ApiClient::errorMessage(payload, tr("音效目录加载失败，请重试")));
        else if (id == m_common && id) {
            m_common = 0;
            m_error = ApiClient::errorMessage(payload, tr("通用耳机音效加载失败"));
            if (m_pendingActivation == "common") {
                m_pendingActivation.clear();
                emit notice(m_error, true);
            }
            emit changed();
        }
    });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        cancel();
        m_revision = m_api.sessionRevision();
        for (auto it = m_buckets.begin(); it != m_buckets.end(); ++it) {
            resetScope(it.key());
            for (auto &row : it->rows) {
                row.remove("sound_urls");
                row.remove("vpf_urls");
                row.remove("unavailable_reason");
                row.remove("_verified");
            }
        }
        m_commonEffect.clear();
        if (m_active && brands())
            m_common = m_api.commonHeadphoneEffect();
        project();
        schedule();
    });
}
AudioEffectCatalog::~AudioEffectCatalog()
{
    shutdown();
    m_searchPool.waitForDone();
}
QString AudioEffectCatalog::scope() const
{
    return m_category == "market"      ? QString("market/%1").arg(m_sort)
           : m_category == "headphone" ? QString("headphone/%1").arg(m_brand)
                                       : "artist";
}
QString AudioEffectCatalog::entryKey(const QString &scope, const QVariantMap &row) const
{
    return scope + '|' + identity(row);
}
QString AudioEffectCatalog::query() const
{
    return m_buckets.value(scope()).query;
}
bool AudioEffectCatalog::relevance() const
{
    return m_buckets.value(scope()).relevance;
}
int AudioEffectCatalog::cachedCount() const
{
    return int(m_buckets.value(scope()).rows.size());
}
bool AudioEffectCatalog::complete() const
{
    return m_buckets.value(scope()).complete;
}
int AudioEffectCatalog::resultCount() const
{
    return rowCount() + int(!commonEffect().isEmpty());
}
QVariantMap AudioEffectCatalog::commonEffect() const
{
    if (!brands() || m_commonEffect.isEmpty() ||
        (!query().trimmed().isEmpty() &&
         AudioEffectSearch::rank(m_commonEffect, terms(query())) < 0))
        return {};
    return decorate(m_commonEffect);
}
QString AudioEffectCatalog::viewId() const
{
    return scope() + '?' + query() + (relevance() ? "#relevance" : "#source");
}
QString AudioEffectCatalog::searchPlaceholder() const
{
    return brands()                 ? tr("搜索耳机品牌")
           : m_brand                ? tr("搜索 %1 耳机型号").arg(m_brandName)
           : m_category == "artist" ? tr("搜索音效、歌手或作者")
                                    : tr("搜索名称、作者、标签或简介");
}
QString AudioEffectCatalog::coverageText() const
{
    if (!m_cacheReady)
        return tr("正在读取本地音效目录…");
    const auto bucket = m_buckets.value(scope());
    const auto prefix =
        query().isEmpty()
            ? tr("已缓存 %1 条").arg(cachedCount())
            : tr("找到 %1 条 · 已搜索 %2 条缓存").arg(resultCount()).arg(cachedCount());
    if (m_searching)
        return prefix + tr(" · 正在搜索…");
    if (!bucket.error.isEmpty())
        return prefix + tr(" · 部分目录加载失败");
    if (bucket.limited)
        return prefix + tr(" · 目录未完整加载，已达本地保护上限");
    if (bucket.complete)
        return prefix + tr(" · 当前列表已载入");
    return prefix + (m_active ? tr(" · 正在加载更多目录…") : tr(" · 目录未完整加载"));
}
void AudioEffectCatalog::setQuery(const QString &query)
{
    const auto value = query.left(128);
    if (m_buckets[scope()].query == value || m_closing)
        return;
    m_cache->load();
    m_buckets[scope()].query = value;
    project(!value.trimmed().isEmpty());
    emit changed();
}
void AudioEffectCatalog::setRelevance(bool value)
{
    if (m_buckets[scope()].relevance == value)
        return;
    m_buckets[scope()].relevance = value;
    project();
}
void AudioEffectCatalog::setBrowsingActive(bool value)
{
    if (m_closing || m_active == value)
        return;
    m_active = value;
    if (value) {
        m_cache->load();
        project();
        auto &bucket = m_buckets[scope()];
        bucket.used = QDateTime::currentMSecsSinceEpoch();
        if (bucket.complete && bucket.used - bucket.updated > lifetime)
            resetScope(scope());
        if (brands() && m_commonEffect.isEmpty() && !m_common)
            m_common = m_api.commonHeadphoneEffect();
        schedule();
    } else {
        m_next.stop();
        m_filter.stop();
        ++m_projectionGeneration;
        m_searchPool.clear();
        m_searching = false;
        m_firstRequest = false;
        if (!validating() && m_request) {
            m_api.cancel(m_request);
            m_request = 0;
        }
        if (m_common && m_pendingActivation != "common") {
            m_api.cancel(m_common);
            m_common = 0;
        }
    }
    emit changed();
}
QVariant AudioEffectCatalog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size() ||
        role != Qt::UserRole + 1)
        return {};
    return decorate(m_rows[index.row()]);
}
QVariantMap AudioEffectCatalog::decorate(QVariantMap effect) const
{
    if (effect.isEmpty())
        return effect;
    const auto path = m_controls.downloadedImpulseResponse(identity(effect));
    const bool verified =
        effect.value("_verified").toULongLong() == m_revision &&
        QDateTime::currentMSecsSinceEpoch() - effect.value("_verifiedAt").toLongLong() < 60000;
    effect.insert("downloaded", !path.isEmpty());
    effect.insert("active", !path.isEmpty() && m_controls.impulseEnabled() &&
                                m_controls.impulseResponse() == path);
    effect.insert("needs_validation",
                  !verified && path.isEmpty() && effect.value("source") != "brand");
    if (!verified) {
        effect.remove("sound_urls");
        effect.remove("vpf_urls");
        effect.remove("unavailable_reason");
    }
    return effect;
}
void AudioEffectCatalog::cancel(bool download)
{
    m_next.stop();
    for (auto *id : {&m_request, &m_common}) {
        if (*id)
            m_api.cancel(*id);
        *id = 0;
    }
    if (!download)
        return;
    m_pendingActivation.clear();
    ++m_downloadGeneration;
    m_installer.cancel();
    m_downloading.clear();
    m_urls.clear();
    m_progress = 0;
}
void AudioEffectCatalog::shutdown()
{
    if (m_closing)
        return;
    m_closing = true;
    cancel();
    m_filter.stop();
    ++m_projectionGeneration;
    m_searchPool.clear();
    m_installer.shutdown();
    m_cache->shutdown();
}
void AudioEffectCatalog::resetScope(const QString &key)
{
    auto &bucket = m_buckets[key];
    bucket.generation = QUuid::createUuid().toString();
    bucket.seen.clear();
    bucket.nextPage = 1;
    bucket.complete = false;
    bucket.limited = false;
    bucket.error.clear();
    bucket.total = -1;
}
void AudioEffectCatalog::switchScope()
{
    m_cache->load();
    if (!validating())
        cancel(false);
    m_error.clear();
    auto &bucket = m_buckets[scope()];
    bucket.used = QDateTime::currentMSecsSinceEpoch();
    if (bucket.generation.isEmpty())
        resetScope(scope());
    if (bucket.complete && bucket.used - bucket.updated > lifetime)
        resetScope(scope());
    m_firstRequest = bucket.rows.isEmpty();
    trim();
    project();
    schedule();
    if (brands() && m_commonEffect.isEmpty() && !m_common)
        m_common = m_api.commonHeadphoneEffect();
}
void AudioEffectCatalog::selectCategory(const QString &category)
{
    if (!QStringList{"artist", "headphone", "market"}.contains(category) || m_closing)
        return;
    if (m_category == category && !m_buckets.value(scope()).rows.isEmpty())
        return;
    m_category = category;
    m_brand = 0;
    m_brandName.clear();
    switchScope();
}
void AudioEffectCatalog::setSort(int sort)
{
    if (sort < 2 || sort > 4 || m_sort == sort || m_closing)
        return;
    m_sort = sort;
    if (m_category == "market")
        switchScope();
    else
        emit changed();
}
void AudioEffectCatalog::selectBrand(int row)
{
    if (!brands() || row < 0 || row >= m_rows.size())
        return;
    activateKey(m_rows[row].value("key").toString());
}
void AudioEffectCatalog::allBrands()
{
    if (m_closing)
        return;
    m_brand = 0;
    m_brandName.clear();
    switchScope();
}
void AudioEffectCatalog::refresh()
{
    if (m_closing)
        return;
    if (!validating())
        cancel(false);
    m_cache->load();
    resetScope(scope());
    m_error.clear();
    if (brands() && !m_common)
        m_common = m_api.commonHeadphoneEffect();
    m_firstRequest = true;
    project();
    schedule();
}
void AudioEffectCatalog::loadMore()
{
    if (m_closing || loading())
        return;
    m_cache->load();
    m_buckets[scope()].error.clear();
    m_error.clear();
    m_firstRequest = true;
    schedule();
}
void AudioEffectCatalog::schedule()
{
    if (!m_closing && !m_next.isActive())
        m_next.start();
}
void AudioEffectCatalog::step()
{
    if (m_closing || !m_cacheReady || m_request || !m_downloading.isEmpty() ||
        m_pendingActivation == "common")
        return;
    const auto key = validating() ? m_pendingActivation.section('|', 0, 0) : scope();
    const auto bucket = m_buckets.value(key);
    if ((!m_active && !m_firstRequest && !validating()) || bucket.complete || bucket.limited ||
        !bucket.error.isEmpty())
        return;
    m_requestScope = key;
    requestPage();
}
void AudioEffectCatalog::requestPage()
{
    const auto key = m_requestScope.isEmpty() ? scope() : m_requestScope;
    auto &bucket = m_buckets[key];
    if (bucket.generation.isEmpty())
        resetScope(key);
    const auto category = key.startsWith("market/") ? ApiClient::EffectCategory::Market
                          : key == "artist"         ? ApiClient::EffectCategory::Artist
                          : key == "headphone/0"    ? ApiClient::EffectCategory::HeadphoneBrands
                                                    : ApiClient::EffectCategory::HeadphoneModels;
    m_requestScope = key;
    m_revision = m_api.sessionRevision();
    m_request =
        m_api.audioEffects(category, quint32(bucket.nextPage), pageSize,
                           key.startsWith("market/") ? key.section('/', 1).toUInt() : 2,
                           key.startsWith("headphone/") ? key.section('/', 1).toULongLong() : 0);
    m_firstRequest = false;
    if (!m_request)
        failRead(tr("无法发起音效目录请求"));
    emit changed();
}
void AudioEffectCatalog::failRead(const QString &message)
{
    m_request = 0;
    m_buckets[m_requestScope.isEmpty() ? scope() : m_requestScope].error = message;
    m_error = message;
    if (validating()) {
        m_pendingActivation.clear();
        emit notice(message, true);
    }
    emit changed();
}
void AudioEffectCatalog::succeeded(quint64 id, const QByteArray &bytes)
{
    if (m_closing || m_revision != m_api.sessionRevision() || (id != m_request && id != m_common))
        return;
    const auto document = QJsonDocument::fromJson(bytes);
    if (id == m_common) {
        m_common = 0;
        m_commonEffect = document.isObject() ? document.object().toVariantMap() : QVariantMap{};
        if (!m_commonEffect.isEmpty()) {
            m_commonEffect["key"] = "common";
            m_commonEffect["_verified"] = m_revision;
            m_commonEffect["_verifiedAt"] = QDateTime::currentMSecsSinceEpoch();
            AudioEffectSearch::prepare(m_commonEffect);
        }
        if (m_pendingActivation == "common") {
            m_pendingActivation.clear();
            if (!m_commonEffect.isEmpty())
                beginDownload(m_commonEffect);
            else
                emit notice(tr("当前没有可用的通用耳机音效"), true);
        }
        emit changed();
        return;
    }
    m_request = 0;
    acceptPage(m_requestScope.isEmpty() ? scope() : m_requestScope, document.object());
}
void AudioEffectCatalog::acceptPage(const QString &key, const QJsonObject &page)
{
    auto &bucket = m_buckets[key];
    if (bucket.generation.isEmpty())
        resetScope(key);
    const auto items = page.value("items").toArray();
    int novel = 0;
    QHash<QString, int> indices;
    for (int i = 0; i < bucket.rows.size(); ++i)
        indices.insert(identity(bucket.rows[i]), i);
    QList<QVariantMap> changedRows;
    for (const auto &value : items) {
        auto row = value.toObject().toVariantMap();
        if (key == "headphone/0")
            row.insert("source", "brand");
        const auto id = identity(row);
        const auto old = indices.value(id, -1);
        const bool sameGeneration =
            old >= 0 && bucket.rows[old].value("_generation").toString() == bucket.generation;
        if (bucket.seen.contains(id) && sameGeneration && key.startsWith("headphone/") &&
            key != "headphone/0" && bucket.rows[old].value("name") != row.value("name") &&
            !bucket.rows[old].value("aliases").toStringList().contains(
                row.value("name").toString()))
            ++novel;
        if (!bucket.seen.contains(id)) {
            bucket.seen.insert(id);
            ++novel;
        }
        QStringList aliases = row.value("aliases").toStringList();
        if (sameGeneration) {
            aliases.append(bucket.rows[old].value("aliases").toStringList());
            if (key.startsWith("headphone/") && key != "headphone/0")
                aliases.append(bucket.rows[old].value("name").toString());
        }
        aliases.removeAll(row.value("name").toString());
        aliases.removeDuplicates();
        row["aliases"] = aliases;
        row["key"] = entryKey(key, row);
        row["_generation"] = bucket.generation;
        row["_ordinal"] =
            old >= 0 && bucket.rows[old].value("_generation").toString() == bucket.generation
                ? bucket.rows[old].value("_ordinal").toInt()
                : bucket.seen.size() - 1;
        row["_verified"] = m_revision;
        row["_verifiedAt"] = QDateTime::currentMSecsSinceEpoch();
        AudioEffectSearch::prepare(row);
        if (old >= 0)
            bucket.rows[old] = row;
        else {
            indices.insert(id, bucket.rows.size());
            bucket.rows.append(row);
        }
        changedRows.append(row);
    }
    bucket.total = page.value("total").isDouble() ? page.value("total").toInt(-1) : -1;
    bucket.error.clear();
    m_error.clear();
    ++bucket.nextPage;
    if (items.isEmpty()) {
        bucket.complete = true;
        bucket.updated = QDateTime::currentMSecsSinceEpoch();
        bucket.rows.removeIf(
            [&](const QVariantMap &row) { return !bucket.seen.contains(identity(row)); });
    } else if (!novel) {
        --bucket.nextPage;
        bucket.error = tr("服务重复返回相同目录，已暂停；可刷新重试");
    } else if (bucket.rows.size() >= 10000 || bucket.nextPage > 1000)
        bucket.limited = true;
    std::stable_sort(bucket.rows.begin(), bucket.rows.end(), [](const auto &a, const auto &b) {
        return a.value("_ordinal").toInt() < b.value("_ordinal").toInt();
    });
    persist(key, changedRows);
    if (key == scope())
        project();
    if (validating() && m_pendingActivation.startsWith(key + '|')) {
        const auto selected = findEntry(m_pendingActivation);
        if (selected && selected->value("_verified").toULongLong() == m_revision &&
            selected->value("_generation").toString() == bucket.generation) {
            const auto effect = *selected;
            m_pendingActivation.clear();
            beginDownload(effect);
        } else if (bucket.complete || bucket.limited || !bucket.error.isEmpty()) {
            m_pendingActivation.clear();
            emit notice(bucket.complete ? tr("此音效已下架或当前账号不可见")
                                        : tr("目录尚未完整加载，无法确认此音效，请重试"),
                        true);
        }
    }
    trim();
    schedule();
    emit changed();
}
void AudioEffectCatalog::persist(const QString &key, const QList<QVariantMap> &rows)
{
    const auto &bucket = m_buckets[key];
    QJsonObject metadata{{"generation", bucket.generation}, {"count", bucket.rows.size()},
                         {"nextPage", bucket.nextPage},     {"total", bucket.total},
                         {"updated", bucket.updated},       {"used", bucket.used},
                         {"complete", bucket.complete},     {"limited", bucket.limited}};
    QJsonArray publicRows;
    for (const auto &row : rows) {
        QJsonObject value;
        for (const auto &field :
             {"id", "source", "name", "author", "intro", "labels", "icon_url", "model_count",
              "user_count", "aliases", "_generation", "_ordinal"})
            if (row.contains(field))
                value[field] = QJsonValue::fromVariant(row.value(field));
        value["_identity"] = identity(row);
        publicRows.append(value);
    }
    m_cache->save(key, metadata, publicRows, bucket.complete);
}
void AudioEffectCatalog::trim()
{
    const auto total = [this] {
        int count = 0;
        for (const auto &bucket : std::as_const(m_buckets))
            count += bucket.rows.size();
        return count;
    };
    while (m_buckets.size() > scopeLimit || total() > recordLimit) {
        QString oldest;
        qint64 used = std::numeric_limits<qint64>::max();
        for (auto it = m_buckets.cbegin(); it != m_buckets.cend(); ++it)
            if (it.key() != scope() && it.key() != m_requestScope &&
                !m_pendingActivation.startsWith(it.key() + '|') && it->used < used) {
                oldest = it.key();
                used = it->used;
            }
        if (oldest.isEmpty()) {
            m_buckets[scope()].limited = true;
            break;
        }
        m_buckets.remove(oldest);
        m_cache->remove(oldest);
    }
}
void AudioEffectCatalog::project(bool debounce)
{
    ++m_projectionGeneration;
    m_filter.stop();
    m_searchPool.clear();
    if (debounce) {
        m_searching = true;
        m_filter.start();
        return;
    }
    const auto bucket = m_buckets.value(scope());
    m_page = bucket.nextPage;
    m_total = bucket.total;
    m_more = !bucket.complete && !bucket.limited;
    const auto generation = m_projectionGeneration;
    if (bucket.query.trimmed().isEmpty()) {
        applyProjection(bucket.rows, generation);
        return;
    }
    m_searching = true;
    emit changed();
    auto *watcher = new QFutureWatcher<QList<QVariantMap>>(this);
    connect(
        watcher, &QFutureWatcher<QList<QVariantMap>>::finished, this, [this, watcher, generation] {
            watcher->deleteLater();
            if (m_closing || generation != m_projectionGeneration || watcher->future().isCanceled())
                return;
            applyProjection(watcher->result(), generation);
        });
    watcher->setFuture(
        QtConcurrent::run(&m_searchPool, [rows = bucket.rows, words = terms(bucket.query),
                                          relevance = bucket.relevance] {
            QList<QPair<int, QVariantMap>> ranked;
            for (const auto &row : rows) {
                const auto rank = AudioEffectSearch::rank(row, words);
                if (rank >= 0)
                    ranked.append({rank, row});
            }
            if (relevance)
                std::stable_sort(ranked.begin(), ranked.end(),
                                 [](const auto &a, const auto &b) { return a.first < b.first; });
            QList<QVariantMap> result;
            for (const auto &entry : ranked)
                result.append(entry.second);
            return result;
        }));
}
void AudioEffectCatalog::applyProjection(const QList<QVariantMap> &rows, quint64 generation)
{
    if (m_closing || generation != m_projectionGeneration)
        return;
    // Appending another directory page does not invalidate the visible pool or
    // touch its scroll position. A different query/order uses the anchor handoff.
    if (m_projectionView == viewId() && rows.size() >= m_rows.size() &&
        std::equal(m_rows.cbegin(), m_rows.cend(), rows.cbegin())) {
        if (rows.size() > m_rows.size()) {
            const auto first = m_rows.size();
            beginInsertRows({}, first, rows.size() - 1);
            m_rows.append(rows.sliced(first));
            endInsertRows();
        }
        m_searching = false;
        emit changed();
        return;
    }
    emit projectionAboutToChange();
    beginResetModel();
    m_rows = rows;
    m_projectionView = viewId();
    for (auto &row : m_rows)
        if (!row.contains("key"))
            row["key"] = entryKey(scope(), row);
    endResetModel();
    m_searching = false;
    emit changed();
    emit projectionChanged();
}
void AudioEffectCatalog::savePosition(const QString &view, const QString &key, qreal offset,
                                      qreal y)
{
    if (m_closing || !std::isfinite(offset) || !std::isfinite(y))
        return;
    if (!m_positions.contains(view) && m_positions.size() >= 128)
        m_positions.erase(m_positions.begin());
    m_positions.insert(view, {{"key", key}, {"offset", offset}, {"y", y}});
}
QVariantMap AudioEffectCatalog::position() const
{
    return m_positions.value(viewId());
}
int AudioEffectCatalog::indexOfKey(const QString &key) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].value("key") == key)
            return i;
    return -1;
}
QString AudioEffectCatalog::keyAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows[row].value("key").toString() : QString();
}
QVariantMap *AudioEffectCatalog::findEntry(const QString &key)
{
    const auto bucketKey = key.section('|', 0, 0);
    auto it = m_buckets.find(bucketKey);
    if (it == m_buckets.end())
        return nullptr;
    for (auto &row : it->rows)
        if (entryKey(bucketKey, row) == key)
            return &row;
    return nullptr;
}
void AudioEffectCatalog::activate(int row)
{
    if (row == -1)
        activateKey("common");
    else if (row >= 0 && row < m_rows.size())
        activateKey(m_rows[row].value("key").toString());
}
void AudioEffectCatalog::activateKey(const QString &key)
{
    if (m_closing || downloading())
        return;
    const auto entry = key == "common" ? &m_commonEffect : findEntry(key);
    if (!entry || entry->isEmpty())
        return;
    const auto effect = *entry;
    if (effect.value("source") == "brand") {
        m_brand = effect.value("id").toULongLong();
        m_brandName = effect.value("name").toString();
        switchScope();
        return;
    }
    if (!m_controls.downloadedImpulseResponse(identity(effect)).isEmpty()) {
        useDownloaded(identity(effect));
        return;
    }
    if (!decorate(effect).value("needs_validation").toBool()) {
        beginDownload(effect);
        return;
    }
    if (key == "common") {
        cancel(false);
        m_pendingActivation = "common";
        m_common = m_api.commonHeadphoneEffect();
        if (!m_common) {
            m_pendingActivation.clear();
            emit notice(tr("无法确认通用耳机音效"), true);
        }
        emit changed();
        return;
    }
    cancel(false);
    m_pendingActivation = key;
    resetScope(key.section('|', 0, 0));
    schedule();
    emit changed();
}
void AudioEffectCatalog::beginDownload(const QVariantMap &effect)
{
    if (!effect.value("unavailable_reason").toString().isEmpty()) {
        m_error = effect.value("unavailable_reason").toString();
        emit changed();
        emit notice(m_error, true);
        return;
    }
    if (!effect.value("vpf_urls").toList().isEmpty()) {
        m_error = tr("此音效需要支持 VPF 的音效引擎");
        emit changed();
        emit notice(m_error, true);
        return;
    }
    m_urls.clear();
    for (const auto &raw : effect.value("sound_urls").toList()) {
        const QUrl url(raw.toString());
        if (resource(url))
            m_urls.append(url.toString());
    }
    if (m_urls.isEmpty()) {
        m_error = tr("此音效没有可用的脉冲响应资源");
        emit changed();
        emit notice(m_error, true);
        return;
    }
    m_downloading = effect;
    const auto directory = m_controls.effectFilesRoot() + "/downloads";
    m_destination = directory + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".irs";
    m_installer.install(++m_downloadGeneration, m_urls, m_destination);
    emit changed();
}
void AudioEffectCatalog::useDownloaded(const QString &key)
{
    if (m_closing)
        return;
    if (m_controls.effectPending()) {
        emit notice(tr("音效正在处理中，请稍后重试"), true);
        return;
    }
    const auto path = m_controls.downloadedImpulseResponse(key);
    if (path.isEmpty()) {
        emit notice(tr("音效文件已移除，请重新下载"), true);
        return;
    }
    m_controls.selectImpulseResponse(path);
    if (m_controls.impulseResponse() != path || !m_controls.impulseEnabled())
        emit notice(m_controls.errorMessage().isEmpty() ? tr("音效暂时无法使用")
                                                        : m_controls.errorMessage(),
                    true);
    else
        emit notice(tr("已使用音效"), false);
}
void AudioEffectCatalog::finishDownload(bool success, const QString &error)
{
    if (success) {
        m_controls.registerImpulseResponse(m_destination, m_downloading.value("name").toString(),
                                           identity(m_downloading));
        m_error.clear();
        emit downloaded(identity(m_downloading), m_downloading.value("name").toString());
    } else {
        m_error = error.isEmpty() ? tr("音效下载失败") : error;
        emit notice(m_error, true);
    }
    m_downloading = {};
    m_urls.clear();
    m_progress = 0;
    if (rowCount())
        emit dataChanged(index(0), index(rowCount() - 1));
    schedule();
    emit changed();
}
} // namespace EchoMusic
