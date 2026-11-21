#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include "JsonRecordStore.h"
#include "ValueCache.h"
#include <QSet>
namespace EchoMusic
{
/// Thin relationship/order index with a bounded viewport of canonical records.
class SocialUserModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
public:
    enum Role
    {
        Key = Qt::UserRole + 1,
        UserId,
        ArtistId,
        Nickname,
        AvatarUrl,
        Description,
        Meta,
        Following,
        CanUnfollow,
        RelationshipKnown,
        VisitedAt,
        AvatarIdentityIcon,
        AvatarIdentityLabel,
        IdentityBadges,
        IdentityType
    };
    explicit SocialUserModel(QObject *parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : count();
    }
    int count() const
    {
        return m_rows.size();
    }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void replace(QJsonArray rows);
    void attachStore(JsonRecordStore &);
    void readEntry(const QString &, std::function<void(QJsonObject)>);
    void transform(std::function<QJsonArray(QJsonArray)>);
    QJsonArray rows() const
    {
        return m_rows;
    }
    QJsonObject entry(const QString &key) const;

private:
    friend class ProfileServiceTestAccess;
    void fetch() const;
    QJsonArray m_rows;
    JsonRecordStore *m_store = nullptr;
    QList<JsonRecordStore::Reference> m_records;
    mutable ValueCache<int, QJsonObject> m_hot{256, 8 * 1024 * 1024};
    mutable QSet<int> m_demand;
    mutable bool m_reading = false, m_scheduled = false, m_readFailed = false;
    quint64 m_generation = 0;
signals:
    void changed();
    void cacheFailed(const QString &);
};
} // namespace EchoMusic
