#include "AudioEffectCatalog.h"
#include "AudioEffectCatalogCache.h"
#include "AudioEffectSearch.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>
#include <QUuid>
#include "CacheCost.h"
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
    connect(m_cache.get(), &AudioEffectCatalogCache::invalidated, this, [this] {
        if (m_closing)
            return;
        emit projectionAboutToChange();
        m_recovering = true;
        if (m_request)
            m_api.cancel(m_request);
        m_request = 0;
        m_cacheReady = false;
        m_verified.clear();
        for (auto it = m_buckets.begin(); it != m_buckets.end(); ++it) {
            it->count = 0;
            resetScope(it.key());
        }
    });
    connect(m_cache.get(), &AudioEffectCatalogCache::loaded, this,
            [this](const QJsonObject &scopes) {
                if (m_closing)
                    return;
                for (auto it = scopes.begin(); it != scopes.end(); ++it)
                    applyMetadata(it.key(), it.value().toObject());
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
                    const bool combined = !m_urls.isEmpty() && !m_vpfUrls.isEmpty();
                    m_progress = combined ? (m_downloadingVpf ? .5 : 0.) + progress * .5 : progress;
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
        m_verified.clear();
        for (auto it = m_buckets.begin(); it != m_buckets.end(); ++it)
            resetScope(it.key());
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
    return m_buckets.value(scope()).count;
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
    m_suspended = !value;
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
        if (m_projectionCanceled)
            m_projectionCanceled->store(true);
        // Pausing directory enumeration must not replace a still-presented
        // card with a cold placeholder. The shared LRU remains bounded even
        // when the popup is destroyed and allows a warm reopen.
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
    if (!index.isValid() || index.row() < 0 || index.row() >= m_count || role != Qt::UserRole + 1)
        return {};
    if (const auto row = m_window.lookup(index.row()))
        return decorate(*row);
    if (m_oversized.first == index.row())
        return decorate(m_oversized.second);
    const_cast<AudioEffectCatalog *>(this)->requestWindow(index.row());
    return QVariantMap{{"_loading", true}};
}
void AudioEffectCatalog::requestWindow(int row)
{
    if (m_closing || m_searching || m_recovering || row < 0 || row >= m_count)
        return;
    const int first = row;
    for (int pending : m_pendingWindows)
        if (row >= pending && row < pending + 32)
            return;
    if (m_pendingWindows.size() >= 4) {
        m_wantedWindows.removeAll(first);
        if (m_wantedWindows.size() >= 4)
            m_wantedWindows.removeFirst();
        m_wantedWindows.append(first);
        return;
    }
    m_pendingWindows.insert(first);
    const auto generation = m_appliedProjection;
    m_cache->read(
        m_appliedProjection, first, [this, generation, first](QJsonArray rows, QString error) {
            if (m_closing || generation != m_appliedProjection)
                return;
            m_pendingWindows.remove(first);
            if (!error.isEmpty()) {
                m_cacheError = error;
                emit changed();
                return;
            }
            for (int i = 0; i < rows.size() && first + i < m_count; ++i) {
                auto row = rows[i].toObject().toVariantMap();
                if (auto verified = m_verified.lookup(row.value("key").toString());
                    verified && verified->value("_generation") == row.value("_generation"))
                    row = *verified;
                const auto cost = jsonValueCost(QJsonObject::fromVariantMap(row));
                if (!m_window.insert(first + i, row, ValueCache<int, QVariantMap>::NoExpiry, cost))
                    m_oversized = {first + i, row};
            }
            if (!rows.isEmpty())
                emit dataChanged(index(first), index(qMin(m_count - 1, first + 31)));
            if (!m_wantedWindows.isEmpty())
                requestWindow(m_wantedWindows.takeFirst());
        });
}
QVariantMap AudioEffectCatalog::decorate(QVariantMap effect) const
{
    if (effect.isEmpty())
        return effect;
    if (auto verified = m_verified.peek(effect.value("key").toString());
        verified && verified->value("_generation") == effect.value("_generation"))
        effect = *verified;
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
    const auto soundUrls = effect.value("sound_urls").toList();
    const bool hasIr = std::any_of(soundUrls.cbegin(), soundUrls.cend(), [](const QVariant &url) {
        return resource(QUrl(url.toString()));
    });
    const bool hasVpf = !effect.value("vpf_urls").toList().isEmpty();
    const bool partial = path.isEmpty() ? hasIr && hasVpf && !m_controls.supportsVpf()
                                        : m_controls.partialImpulseResponse(path);
    effect.insert("partial", partial);
    effect.insert("support_text", partial ? tr("仅使用 IR 卷积，未应用 VPF 参数") : QString());
    if (!path.isEmpty() && QFileInfo(path).suffix() == "vpf" && !m_controls.supportsVpf())
        effect.insert("unavailable_reason", tr("此音效仅含 VPF 参数，需要启用支持 VPF 的音效引擎"));
    if (verified && path.isEmpty() && effect.value("unavailable_reason").toString().isEmpty() &&
        !hasIr && (!m_controls.supportsVpf() || !hasVpf))
        effect.insert("unavailable_reason",
                      hasVpf ? tr("此音效仅含 VPF 参数，需要启用支持 VPF 的音效引擎")
                             : tr("此音效没有可用的脉冲响应资源"));
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
    m_selecting = false;
    ++m_selectionGeneration;
    if (!download)
        return;
    m_pendingActivation.clear();
    ++m_downloadGeneration;
    // Cancellation and removal share the install worker, after any in-flight commit.
    m_installer.cancel({m_destination, m_vpfDestination});
    m_destination.clear();
    m_vpfDestination.clear();
    m_vpfUrls.clear();
    m_downloadingVpf = false;
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
    if (m_projectionCanceled)
        m_projectionCanceled->store(true);
    m_installer.shutdown();
    m_cache->shutdown();
}
void AudioEffectCatalog::resetScope(const QString &key)
{
    auto &bucket = m_buckets[key];
    bucket.generation = QUuid::createUuid().toString();
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
    m_firstRequest = bucket.count == 0;
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
    if (m_category == category && m_buckets.value(scope()).count > 0)
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
    if (brands())
        activate(row);
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
    if (m_closing || !m_cacheReady || m_writing || m_selecting || m_request ||
        !m_downloading.isEmpty() || m_pendingActivation == "common")
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
void AudioEffectCatalog::applyMetadata(const QString &key, const QJsonObject &object)
{
    auto &bucket = m_buckets[key];
    bucket.generation = object.value("generation").toString();
    bucket.nextPage = qMax(1, object.value("nextPage").toInt(1));
    bucket.total = object.value("total").toInt(-1);
    bucket.count = object.value("count").toInt();
    bucket.updated = object.value("updated").toInteger();
    bucket.used = object.value("used").toInteger();
    bucket.complete = object.value("complete").toBool();
    bucket.limited = object.value("limited").toBool();
    bucket.error = object.value("error").toString();
}
void AudioEffectCatalog::acceptPage(const QString &key, const QJsonObject &page)
{
    auto &bucket = m_buckets[key];
    if (bucket.generation.isEmpty())
        resetScope(key);
    const auto generation = bucket.generation;
    const auto revision = m_revision;
    QJsonObject metadata{
        {"generation", generation},
        {"count", bucket.count},
        {"nextPage", bucket.nextPage},
        {"total", page.value("total").isDouble() ? page.value("total").toInt(-1) : -1},
        {"updated", bucket.updated},
        {"used", bucket.used},
        {"complete", false},
        {"limited", false}};
    m_writing = true;
    m_cache->ingest(
        key, metadata, page.value("items").toArray(),
        [this, key, generation, revision](QJsonObject metadata, QJsonArray rows, QString error) {
            if (m_closing)
                return;
            m_writing = false;
            if (revision != m_revision || m_buckets[key].generation != generation) {
                schedule();
                return;
            }
            if (!error.isEmpty()) {
                m_requestScope = key;
                failRead(tr("音效目录缓存写入失败：%1").arg(error));
                return;
            }
            applyMetadata(key, metadata);
            m_error.clear();
            m_cacheError.clear();
            QVariantMap selected;
            for (const auto &value : rows) {
                auto row = value.toObject().toVariantMap();
                row["_verified"] = revision;
                row["_verifiedAt"] = QDateTime::currentMSecsSinceEpoch();
                const auto rowKey = row.value("key").toString();
                if (m_pendingActivation == rowKey)
                    selected = row;
                const auto cost = jsonValueCost(QJsonObject::fromVariantMap(row));
                for (int position : m_window.keys()) {
                    const auto existing = m_window.peek(position);
                    if (!existing || existing->value("key") != rowKey)
                        continue;
                    if (!m_window.insert(position, row, ValueCache<int, QVariantMap>::NoExpiry,
                                         cost))
                        m_oversized = {position, row};
                    emit dataChanged(index(position), index(position));
                }
                m_verified.insert(rowKey, row, 60000, cost);
            }
            if (key == scope())
                project();
            if (validating() && m_pendingActivation.startsWith(key + '|')) {
                if (!selected.isEmpty()) {
                    m_pendingActivation.clear();
                    beginDownload(selected);
                } else if (m_buckets[key].complete || m_buckets[key].limited ||
                           !m_buckets[key].error.isEmpty()) {
                    m_pendingActivation.clear();
                    emit notice(m_buckets[key].complete
                                    ? tr("此音效已下架或当前账号不可见")
                                    : tr("目录尚未完整加载，无法确认此音效，请重试"),
                                true);
                }
            }
            trim();
            schedule();
            emit changed();
        });
}
void AudioEffectCatalog::trim()
{
    const auto total = [this] {
        int count = 0;
        for (const auto &bucket : std::as_const(m_buckets))
            count += bucket.count;
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
    if (m_projectionCanceled)
        m_projectionCanceled->store(true);
    m_projectionDirty = true;
    m_searching = true;
    emit changed();
    if (debounce) {
        m_filter.start();
        return;
    }
    if (!m_cacheReady || m_closing || m_projectionRunning)
        return;
    m_projectionRunning = true;
    m_projectionDirty = false;
    const auto generation = m_projectionGeneration;
    const auto view = viewId();
    const auto anchor = position().value("key").toString();
    m_projectionCanceled = std::make_shared<std::atomic_bool>(false);
    m_cache->project(
        scope(), view, terms(query()), relevance(), generation, m_appliedProjection, anchor,
        m_projectionCanceled,
        [this, generation, view, anchor](AudioEffectCatalogCache::Projection result) {
            m_projectionRunning = false;
            if (m_closing)
                return;
            if (generation != m_projectionGeneration || m_projectionDirty) {
                if (m_projectionDirty)
                    project();
                return;
            }
            m_searching = false;
            m_pendingWindows.clear();
            m_wantedWindows.clear();
            m_keyPositions.clear();
            m_pendingPositions.clear();
            if (!result.error.isEmpty()) {
                m_cacheError = result.error;
                emit changed();
                return;
            }
            m_cacheError.clear();
            m_page = m_buckets[scope()].nextPage;
            m_total = m_buckets[scope()].total;
            m_more = !m_buckets[scope()].complete && !m_buckets[scope()].limited;
            m_anchorKey = anchor;
            const int savedRow = position().value("row", -1).toInt();
            m_anchorPosition = anchor.isEmpty() && savedRow >= 0
                                   ? (savedRow < result.count ? savedRow : -1)
                                   : result.anchor;
            if (m_recovering && m_projectionView == view && m_count &&
                (!anchor.isEmpty() || savedRow >= 0) && m_anchorPosition < 0 && m_more &&
                m_buckets[scope()].error.isEmpty()) {
                // Keep the presented viewport until the original stable anchor
                // has been refetched. Partial cold data must not collapse it.
                emit changed();
                return;
            }
            m_recovering = false;
            const bool retain = result.incremental && m_projectionView == view;
            m_appliedProjection = generation;
            if (retain) {
                if (result.append && result.count > m_count) {
                    beginInsertRows({}, m_count, result.count - 1);
                    m_count = result.count;
                    endInsertRows();
                }
                for (const auto &change : result.changes) {
                    using Change = AudioEffectCatalogCache::Projection::Change;
                    const auto remap = [&](int row) {
                        if (change.kind == Change::Insert)
                            return row >= change.first ? row + change.last - change.first + 1 : row;
                        if (change.kind == Change::Remove)
                            return row < change.first   ? row
                                   : row <= change.last ? -1
                                                        : row - change.last + change.first - 1;
                        return row == change.first ? change.destination
                               : row >= change.destination && row < change.first ? row + 1
                                                                                 : row;
                    };
                    if (change.kind == Change::Insert)
                        beginInsertRows({}, change.first, change.last);
                    else if (change.kind == Change::Remove)
                        beginRemoveRows({}, change.first, change.last);
                    else
                        beginMoveRows({}, change.first, change.last, {}, change.destination);
                    QList<QPair<int, QVariantMap>> hot;
                    for (int position : m_window.keys()) {
                        const int target = remap(position);
                        if (target >= 0)
                            hot.append({target, *m_window.peek(position)});
                    }
                    m_window.clear();
                    for (const auto &[position, row] : hot)
                        m_window.insert(position, row, ValueCache<int, QVariantMap>::NoExpiry,
                                        jsonValueCost(QJsonObject::fromVariantMap(row)));
                    m_oversized.first = remap(m_oversized.first);
                    if (m_oversized.first < 0)
                        m_oversized = {-1, {}};
                    if (change.kind == Change::Insert) {
                        m_count += change.last - change.first + 1;
                        endInsertRows();
                    } else if (change.kind == Change::Remove) {
                        m_count -= change.last - change.first + 1;
                        endRemoveRows();
                    } else
                        endMoveRows();
                }
            } else {
                emit projectionAboutToChange();
                beginResetModel();
                m_window.clear();
                m_oversized = {-1, {}};
                m_count = result.count;
                m_projectionView = view;
                endResetModel();
                emit projectionChanged();
            }
            QSet<int> pages;
            for (int row : m_window.keys())
                pages.insert(row / 32 * 32);
            if (pages.isEmpty() && m_count)
                pages.insert(qMax(0, result.anchor) / 32 * 32);
            if (!m_suspended)
                for (int first : pages)
                    requestWindow(first);
            if (retain && (positionPending() || (!anchor.isEmpty() && result.anchor >= 0)))
                emit projectionChanged();
            if (retain && m_count)
                emit dataChanged(index(0), index(m_count - 1));
            emit changed();
        });
}
void AudioEffectCatalog::savePosition(const QString &view, const QString &key, qreal offset,
                                      qreal y, int row)
{
    if (m_closing || !std::isfinite(offset) || !std::isfinite(y))
        return;
    if (!m_positions.contains(view) && m_positions.size() >= 128)
        m_positions.erase(m_positions.begin());
    QVariantMap value{{"key", key}, {"offset", offset}, {"y", y}};
    if (row < 0 && !key.isEmpty())
        row = indexOfKey(key);
    if (row >= 0)
        value["row"] = row;
    m_positions.insert(view, value);
    if (view == viewId() && !m_recovering && row >= 0 && row < m_count) {
        m_anchorKey = key;
        m_anchorPosition = row;
    }
}
QVariantMap AudioEffectCatalog::position() const
{
    return m_positions.value(viewId());
}
bool AudioEffectCatalog::positionPending() const
{
    const auto bucket = m_buckets.value(scope());
    return (!position().value("key").toString().isEmpty() ||
            position().value("row", -1).toInt() >= 0) &&
           m_anchorPosition < 0 && !bucket.complete && !bucket.limited && bucket.error.isEmpty();
}
int AudioEffectCatalog::indexOfKey(const QString &key) const
{
    if (key.isEmpty())
        return -1;
    if (key == m_anchorKey && m_anchorPosition >= 0)
        return m_anchorPosition;
    for (int row : m_window.keys())
        if (m_window.peek(row)->value("key") == key)
            return row;
    if (auto position = m_keyPositions.lookup(key))
        return *position;
    if (!m_closing && !m_searching && key.startsWith(scope() + '|') &&
        !m_pendingPositions.contains(key) && m_pendingPositions.size() < 4) {
        m_pendingPositions.insert(key);
        auto *self = const_cast<AudioEffectCatalog *>(this);
        const auto generation = m_appliedProjection;
        self->m_cache->locate(
            self->m_appliedProjection, key, [self, generation, key](int position) {
                if (self->m_closing || generation != self->m_appliedProjection)
                    return;
                self->m_pendingPositions.remove(key);
                self->m_keyPositions.insert(key, position, ValueCache<QString, int>::NoExpiry);
                emit self->changed();
                if (key == self->position().value("key").toString())
                    emit self->projectionChanged();
            });
    }
    return -1;
}
QString AudioEffectCatalog::keyAt(int row) const
{
    if (auto entry = m_window.peek(row))
        return entry->value("key").toString();
    const_cast<AudioEffectCatalog *>(this)->requestWindow(row);
    return {};
}
void AudioEffectCatalog::activate(int row)
{
    activateKey(row == -1 ? QString("common") : keyAt(row));
}
void AudioEffectCatalog::activateKey(const QString &key)
{
    if (m_closing || downloading() || key.isEmpty())
        return;
    if (key == "common") {
        activateEffect(key, m_commonEffect);
        return;
    }
    if (auto row = m_verified.lookup(key)) {
        activateEffect(key, *row);
        return;
    }
    for (int position : m_window.keys()) {
        const auto row = m_window.peek(position);
        if (row && row->value("key") == key) {
            activateEffect(key, *row);
            return;
        }
    }
    cancel(false);
    m_selecting = true;
    const auto generation = ++m_selectionGeneration;
    emit changed();
    m_cache->find(key, [this, key, generation](QJsonArray rows, QString error) {
        if (m_closing || generation != m_selectionGeneration)
            return;
        m_selecting = false;
        if (!error.isEmpty()) {
            m_cacheError = error;
            emit changed();
            return;
        }
        if (rows.isEmpty() && m_buckets.contains(key.section('|', 0, 0))) {
            cancel(false);
            m_pendingActivation = key;
            resetScope(key.section('|', 0, 0));
            schedule();
        } else if (!rows.isEmpty())
            activateEffect(key, rows.first().toObject().toVariantMap());
        emit changed();
    });
}
void AudioEffectCatalog::activateEffect(const QString &key, const QVariantMap &effect)
{
    if (effect.isEmpty())
        return;
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
    cancel(false);
    m_pendingActivation = key;
    if (key == "common") {
        m_common = m_api.commonHeadphoneEffect();
        if (!m_common) {
            m_pendingActivation.clear();
            emit notice(tr("无法确认通用耳机音效"), true);
        }
    } else
        resetScope(key.section('|', 0, 0));
    schedule();
    emit changed();
}
void AudioEffectCatalog::beginDownload(const QVariantMap &effect)
{
    const auto available = decorate(effect);
    if (!available.value("unavailable_reason").toString().isEmpty()) {
        m_error = available.value("unavailable_reason").toString();
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
    m_vpfUrls.clear();
    for (const auto &raw : effect.value("vpf_urls").toList()) {
        const QUrl url(raw.toString());
        if (resource(url))
            m_vpfUrls.append(url.toString());
    }
    if (m_urls.isEmpty() && (m_vpfUrls.isEmpty() || !m_controls.supportsVpf())) {
        m_error = tr("此音效没有可用的脉冲响应资源");
        emit changed();
        emit notice(m_error, true);
        return;
    }
    m_downloading = available;
    const auto directory = m_controls.effectFilesRoot() + "/downloads";
    const auto stem = directory + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_vpfDestination = m_vpfUrls.isEmpty() ? QString() : stem + ".vpf";
    m_downloadingVpf = m_urls.isEmpty();
    m_destination = m_downloadingVpf ? m_vpfDestination : stem + ".irs";
    m_installer.install(++m_downloadGeneration, m_downloadingVpf ? m_vpfUrls : m_urls,
                        m_destination);
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
        emit notice(m_controls.partialImpulseResponse(path) ? tr("已使用卷积音效，未应用 VPF 参数")
                                                            : tr("已使用音效"),
                    false);
}
void AudioEffectCatalog::finishDownload(bool success, const QString &error)
{
    if (success && !m_downloadingVpf && !m_vpfUrls.isEmpty()) {
        m_downloadingVpf = true;
        m_progress = .5;
        m_installer.install(m_downloadGeneration, m_vpfUrls, m_vpfDestination);
        emit changed();
        return;
    }
    if (success) {
        m_controls.registerImpulseResponse(m_destination, m_downloading.value("name").toString(),
                                           identity(m_downloading), !m_vpfDestination.isEmpty(),
                                           m_vpfDestination);
        m_error.clear();
        emit downloaded(identity(m_downloading), m_downloading.value("name").toString());
    } else {
        m_error = error.isEmpty() ? tr("音效下载失败") : error;
        emit notice(m_error, true);
    }
    if (!success) {
        m_installer.cancel({m_destination, m_vpfDestination});
    }
    m_destination.clear();
    m_vpfDestination.clear();
    m_vpfUrls.clear();
    m_downloadingVpf = false;
    m_downloading = {};
    m_urls.clear();
    m_progress = 0;
    if (rowCount())
        emit dataChanged(index(0), index(rowCount() - 1));
    schedule();
    emit changed();
}
} // namespace EchoMusic
