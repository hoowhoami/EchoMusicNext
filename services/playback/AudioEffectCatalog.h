#pragma once
#include "ApiClient.h"
#include "PlaybackAudioControls.h"
#include <QAbstractListModel>
#include "ImpulseResponseInstaller.h"
namespace EchoMusic
{
/// Typed, account-generation-bound catalog and streaming IR installer. Download
/// URLs originate in Rust domain contracts; no renderer URL or arbitrary route.
class AudioEffectCatalog final : public QAbstractListModel
{
    Q_OBJECT
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
    AudioEffectCatalog(ApiClient &api, PlaybackAudioControls &controls, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_rows.size());
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
        return m_request || m_common;
    }
    QString errorMessage() const
    {
        return m_error;
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
    QVariantMap commonEffect() const
    {
        return decorate(m_commonEffect);
    }
    bool downloading() const
    {
        return !m_downloading.isEmpty();
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
    void closed();
    void notice(const QString &message, bool warning);
    void downloaded(const QString &identity, const QString &name);

private:
    friend class AudioEffectCatalogTestAccess;
    void cancel(bool download = true);
    void requestPage();
    QVariantMap decorate(QVariantMap effect) const;
    void finishDownload(bool success, const QString &error = {});
    void succeeded(quint64 request, const QByteArray &bytes);
    ApiClient &m_api;
    PlaybackAudioControls &m_controls;
    ImpulseResponseInstaller m_installer;
    quint64 m_downloadGeneration = 0;
    QList<QVariantMap> m_rows;
    QVariantMap m_downloading, m_commonEffect;
    QStringList m_urls;
    QString m_category = "artist", m_brandName, m_error, m_destination;
    quint64 m_revision = 0, m_request = 0, m_common = 0, m_brand = 0;
    int m_page = 1, m_total = 0, m_sort = 2;
    bool m_more = true, m_closing = false;
    double m_progress = 0;
};
} // namespace EchoMusic
