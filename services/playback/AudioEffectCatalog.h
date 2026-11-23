#pragma once
#include "ApiClient.h"
#include "PlaybackAudioControls.h"
#include <QAbstractListModel>
#include "ImpulseResponseInstaller.h"
#include <QTimer>
#include "ValueCache.h"
#include <atomic>
#include <QSet>
#include <memory>
namespace EchoMusic
{
class AudioEffectCatalogCache;
/// Owns public, persistent directory scopes and their local search projections.
/// Download authorization/URLs are ephemeral and bound to the current API session.
class AudioEffectCatalog final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY changed)
    Q_PROPERTY(bool browsingActive READ browsingActive WRITE setBrowsingActive NOTIFY changed)
    Q_PROPERTY(bool searching READ searching NOTIFY changed)
    Q_PROPERTY(bool relevance READ relevance WRITE setRelevance NOTIFY changed)
    Q_PROPERTY(int cachedCount READ cachedCount NOTIFY changed)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY changed)
    Q_PROPERTY(QString coverageText READ coverageText NOTIFY changed)
    Q_PROPERTY(QString searchPlaceholder READ searchPlaceholder NOTIFY changed)
    Q_PROPERTY(QString viewId READ viewId NOTIFY changed)
    Q_PROPERTY(bool complete READ complete NOTIFY changed)
    Q_PROPERTY(bool positionPending READ positionPending NOTIFY changed)
    Q_PROPERTY(bool validating READ validating NOTIFY changed)
    Q_PROPERTY(QString category READ category NOTIFY changed)
    Q_PROPERTY(QString brandName READ brandName NOTIFY changed)
    Q_PROPERTY(int sort READ sortValue WRITE setSort NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(bool brands READ brands NOTIFY changed)
    Q_PROPERTY(QVariantMap commonEffect READ commonEffect NOTIFY changed)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY changed)
public:
    AudioEffectCatalog(ApiClient &api, PlaybackAudioControls &controls, QObject *parent = nullptr,
                       const QString &cachePath = {});
    ~AudioEffectCatalog() override;
    QString query() const;
    void setQuery(const QString &query);
    bool browsingActive() const
    {
        return m_active;
    }
    void setBrowsingActive(bool active);
    bool searching() const
    {
        return m_searching;
    }
    bool relevance() const;
    void setRelevance(bool relevance);
    int cachedCount() const;
    int resultCount() const;
    bool complete() const;
    bool validating() const
    {
        return !m_pendingActivation.isEmpty();
    }
    QString coverageText() const;
    QString searchPlaceholder() const;
    QString viewId() const;
    Q_INVOKABLE void activateKey(const QString &key);
    Q_INVOKABLE void savePosition(const QString &view, const QString &key, qreal offset, qreal y,
                                  int row = -1);
    Q_INVOKABLE QVariantMap position() const;
    bool positionPending() const;
    Q_INVOKABLE int indexOfKey(const QString &key) const;
    Q_INVOKABLE QString keyAt(int row) const;
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_count;
    }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override
    {
        return {{Qt::UserRole + 1, "effect"}};
    }
    QString category() const
    {
        return m_category;
    }
    QString brandName() const
    {
        return m_brandName;
    }
    int sortValue() const
    {
        return m_sort;
    }
    bool loading() const
    {
        if (m_closing)
            return false;
        if (m_writing || m_request || m_common || ((m_active || m_firstRequest) && !m_cacheReady))
            return true;
        // Pagination is one loading operation, including the bounded pause between requests.
        const auto bucket = m_buckets.constFind(scope());
        return (m_active || m_firstRequest) && bucket != m_buckets.cend() && !bucket->complete &&
               !bucket->limited && bucket->error.isEmpty() && m_downloading.isEmpty() &&
               m_pendingActivation != "common";
    }
    QString errorMessage() const
    {
        return m_error.isEmpty() ? m_cacheError : m_error;
    }
    int total() const
    {
        return m_total;
    }
    bool hasMore() const
    {
        return m_more;
    }
    bool brands() const
    {
        return m_category == "headphone" && !m_brand;
    }
    QVariantMap commonEffect() const;
    bool downloading() const
    {
        return m_selecting || !m_downloading.isEmpty() || validating();
    }
    double downloadProgress() const
    {
        return m_progress;
    }
    Q_INVOKABLE void selectCategory(const QString &category);
    Q_INVOKABLE void setSort(int sort);
    Q_INVOKABLE void selectBrand(int row);
    Q_INVOKABLE void allBrands();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void activate(int row);
    void useDownloaded(const QString &identity);
    void shutdown();
signals:
    void changed();
    void projectionAboutToChange();
    void projectionChanged();
    void closed();
    void notice(const QString &message, bool warning);
    void downloaded(const QString &identity, const QString &name);

private:
    friend class AudioEffectCatalogTestAccess;
    void cancel(bool download = true);
    void requestPage();
    void schedule();
    void step();
    void switchScope();
    void resetScope(const QString &scope);
    void project(bool debounce = false);
    void requestWindow(int row);
    void applyMetadata(const QString &scope, const QJsonObject &metadata);
    void activateEffect(const QString &key, const QVariantMap &effect);
    void acceptPage(const QString &scope, const QJsonObject &page);
    void beginDownload(const QVariantMap &effect);
    void failRead(const QString &message);
    QString scope() const;
    QString entryKey(const QString &scope, const QVariantMap &row) const;
    void trim();
    struct Bucket
    {
        QString query, generation, error;
        int nextPage = 1, total = -1, count = 0;
        qint64 updated = 0, used = 0;
        bool complete = false, limited = false, relevance = true;
    };
    QHash<QString, Bucket> m_buckets;
    QHash<QString, QVariantMap> m_positions;
    std::unique_ptr<AudioEffectCatalogCache> m_cache;
    QTimer m_next, m_filter;
    mutable ValueCache<int, QVariantMap> m_window{128, 1024 * 1024};
    ValueCache<QString, QVariantMap> m_verified{100, 1024 * 1024};
    QSet<int> m_pendingWindows;
    QList<int> m_wantedWindows;
    std::shared_ptr<std::atomic_bool> m_projectionCanceled;
    bool m_projectionRunning = false, m_projectionDirty = false;
    bool m_writing = false, m_selecting = false, m_suspended = false, m_recovering = false;
    quint64 m_selectionGeneration = 0;
    int m_count = 0, m_anchorPosition = -1;
    QString m_anchorKey;
    mutable QPair<int, QVariantMap> m_oversized{-1, {}};
    mutable ValueCache<QString, int> m_keyPositions{128};
    mutable QSet<QString> m_pendingPositions;
    QString m_requestScope, m_pendingActivation, m_cacheError, m_projectionView;
    quint64 m_projectionGeneration = 0, m_appliedProjection = 0;
    int m_closedParts = 0;
    bool m_active = false, m_cacheReady = false, m_firstRequest = false, m_searching = false;
    QVariantMap decorate(QVariantMap effect) const;
    void finishDownload(bool success, const QString &error = {});
    void succeeded(quint64 request, const QByteArray &bytes);
    ApiClient &m_api;
    PlaybackAudioControls &m_controls;
    ImpulseResponseInstaller m_installer;
    quint64 m_downloadGeneration = 0;
    QVariantMap m_downloading, m_commonEffect;
    QStringList m_urls;
    QString m_category = "artist", m_brandName, m_error, m_destination, m_vpfDestination;
    QStringList m_vpfUrls;
    bool m_downloadingVpf = false;
    quint64 m_revision = 0, m_request = 0, m_common = 0, m_brand = 0;
    int m_page = 1, m_total = 0, m_sort = 2;
    bool m_more = true, m_closing = false;
    double m_progress = 0;
};
} // namespace EchoMusic
