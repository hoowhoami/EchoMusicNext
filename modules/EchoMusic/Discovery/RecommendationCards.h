#pragma once
#include "SongCatalogModel.h"
#include "SongCatalogActions.h"
#include "SettingsStore.h"
#include "JsonRecordStore.h"
#include "ValueCache.h"
#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>
#include <memory>
namespace EchoMusic
{
/// Immutable theme-group payloads share the session SQL owner. Only the visible
/// presentation window is kept in memory; no provider JSON is copied into QML.
class RecommendationItemsModel final : public QAbstractListModel
{
    friend class DiscoveryServiceTestAccess;
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a recommendation card")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit RecommendationItemsModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    void attach(SessionDatabase *);
    int count() const
    {
        return m_count;
    }
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_count;
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{Qt::UserRole + 1, "entry"}};
    }
    QVariant data(const QModelIndex &, int) const override;
    void replace(QJsonArray);
    bool retryRead();
    void requestItem(int, QObject *, std::function<void(QJsonObject)>);
signals:
    void countChanged();
    void readFailed(const QString &);
    void invalidated();

private:
    void read(int);
    std::unique_ptr<JsonRecordStore> m_store;
    JsonRecordStore::Snapshot m_source;
    ValueCache<int, QJsonObject> m_hot{32, 1024 * 1024};
    QSet<int> m_reads;
    QJsonArray m_fallback;
    quint64 m_generation = 0;
    int m_count = 0;
    bool m_readFailed = false;
};
/// One canonical, windowed song source per card; previews and details borrow it.
class RecommendationCard final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by DiscoveryService")
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(int preparingGroup READ preparingGroup NOTIFY groupPlaybackChanged)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY changed)
    Q_PROPERTY(int status READ status NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(EchoMusic::SongCatalogModel *collection READ collection CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogActions *actions READ actions CONSTANT)
    Q_PROPERTY(EchoMusic::RecommendationItemsModel *groups READ groups CONSTANT)
public:
    RecommendationCard(QString key, QString title, PlaybackService &,
                       SongCatalogModel *borrowed = nullptr, SongCatalogActions *actions = nullptr,
                       QObject *parent = nullptr);
    QString key() const
    {
        return m_key;
    }
    QString title() const
    {
        return !m_providerTitle.isEmpty() ? m_providerTitle : m_title;
    }
    QString subtitle() const
    {
        return m_subtitle;
    }
    int status() const
    {
        return m_status;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    SongCatalogModel *collection() const
    {
        return m_collection;
    }
    SongCatalogActions *actions() const
    {
        return m_actions;
    }
    RecommendationItemsModel *groups()
    {
        return &m_groups;
    }
    Q_INVOKABLE EchoMusic::RecommendationCard *groupCard(int index, QObject *owner);
    Q_INVOKABLE void playGroup(int index);
    Q_INVOKABLE QString groupQueueKey(int index) const;
    int preparingGroup() const
    {
        return m_preparingGroup;
    }
    void setState(int state, QString error = {});
    void accept(const QJsonObject &);
    void clear();
signals:
    void changed();
    void groupPlaybackChanged();

private:
    void syncGroup();
    void cancelGroupPlay();
    PlaybackService &m_playback;
    QPointer<RecommendationCard> m_groupParent;
    QPointer<QObject> m_groupRead, m_groupPlayRead;
    int m_groupIndex = -1, m_groupFirst = 0, m_groupCount = 0, m_preparingGroup = -1;
    const QString m_key, m_title;
    QString m_providerTitle, m_subtitle, m_error;
    int m_status = 0;
    RecommendationItemsModel m_groups;
    std::unique_ptr<SongCatalogModel> m_ownedCollection;
    std::unique_ptr<SongCatalogActions> m_ownedActions;
    SongCatalogModel *m_collection;
    SongCatalogActions *m_actions;
};
/// Owns only bounded card configuration. Payloads belong to the cards' models.
class RecommendationCards final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by DiscoveryService")
    Q_PROPERTY(QVariantList catalog READ catalog NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
public:
    explicit RecommendationCards(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    void add(RecommendationCard *);
    void setPreferences(SettingsStore *);
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_order.size();
    }
    int count() const
    {
        return m_order.size();
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{Qt::UserRole + 1, "card"}};
    }
    QVariant data(const QModelIndex &, int) const override;
    QVariantList catalog() const;
    Q_INVOKABLE EchoMusic::RecommendationCard *at(int row) const;
    RecommendationCard *find(const QString &) const;
    Q_INVOKABLE bool contains(const QString &key) const
    {
        return m_order.contains(key);
    }
    Q_INVOKABLE void toggle(const QString &);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void restoreDefaults();
    QList<RecommendationCard *> all() const
    {
        return m_catalog;
    }
signals:
    void changed();

private:
    void save();
    QStringList defaults() const;
    QList<RecommendationCard *> m_catalog;
    QStringList m_order;
    SettingsStore *m_preferences = nullptr;
};
} // namespace EchoMusic
