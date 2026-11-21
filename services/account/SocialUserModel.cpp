#include "SocialUserModel.h"
#include "AccountIdentity.h"
#include "CacheCost.h"
#include <QTimer>
#include <algorithm>
#include <QJsonObject>
#include <QDateTime>
namespace EchoMusic
{
namespace
{
QJsonObject identityFor(const QJsonObject &row)
{
    QJsonObject identity;
    for (const auto *key : {"key", "user_id", "artist_id", "following", "mutual"})
        if (row.contains(key))
            identity.insert(key, row.value(key));
    return identity;
}
QJsonObject withRelationship(QJsonObject value, const QJsonObject &identity)
{
    for (const auto *key : {"key", "user_id", "artist_id", "following", "mutual"}) {
        if (identity.contains(key)) {
            if (value.value(key) != identity.value(key))
                value.insert(key, identity.value(key));
        } else if (value.contains(key))
            value.remove(key);
    }
    return value;
}
} // namespace
QHash<int, QByteArray> SocialUserModel::roleNames() const
{
    return {{Key, "entryKey"},
            {UserId, "userId"},
            {ArtistId, "artistId"},
            {Nickname, "nickname"},
            {AvatarUrl, "avatarUrl"},
            {Description, "description"},
            {Meta, "meta"},
            {Following, "following"},
            {CanUnfollow, "canUnfollow"},
            {VisitedAt, "visitedAt"},
            {RelationshipKnown, "relationshipKnown"},
            {AvatarIdentityIcon, "avatarIdentityIcon"},
            {AvatarIdentityLabel, "avatarIdentityLabel"},
            {IdentityBadges, "identityBadges"},
            {IdentityType, "identityType"}};
}
QVariant SocialUserModel::data(const QModelIndex &i, int role) const
{
    if (!i.isValid() || i.model() != this || i.row() < 0 || i.row() >= count())
        return {};
    const auto identity = m_rows[i.row()].toObject();
    auto r = identity;
    const bool relationshipRole = role == Key || role == UserId || role == ArtistId ||
                                  role == Following || role == CanUnfollow ||
                                  role == RelationshipKnown;
    if (m_store && !relationshipRole) {
        const auto hit = m_hot.lookup(i.row());
        if (hit)
            r = *hit;
        else if (!m_readFailed && m_demand.size() < 256) {
            m_demand.insert(i.row());
            if (!m_scheduled && !m_reading) {
                m_scheduled = true;
                QTimer::singleShot(0, const_cast<SocialUserModel *>(this), [this] { fetch(); });
            }
        }
    }
    switch (role) {
    case Key:
        return identity.value("key").toString();
    case UserId:
        return identity.value("user_id").toInteger();
    case ArtistId:
        return identity.value("artist_id").toInteger();
    case Nickname:
        return r.value("nickname").toString();
    case AvatarUrl:
        return r.value("avatar_url").toString();
    case AvatarIdentityIcon:
        return AccountIdentity::avatarIcon(
            AccountIdentity::fromDomain(r.value("identity").toObject()).avatarKind());
    case AvatarIdentityLabel:
        return AccountIdentity::avatarLabel(
            AccountIdentity::fromDomain(r.value("identity").toObject()).avatarKind());
    case IdentityBadges: {
        auto badges = AccountIdentity::fromDomain(r.value("identity").toObject()).badges(false);
        const auto membership = r.value("membership").toObject();
        const auto label = membership.value("super_active").toBool()    ? tr("超级VIP")
                           : membership.value("deluxe_active").toBool() ? tr("豪华VIP")
                                                                        : QString();
        if (!label.isEmpty())
            badges.prepend(QVariantMap{{"kind", "membership"}, {"label", label}});
        return badges;
    }
    case Description:
        return r.value("description").toString();
    case Following:
        return identity.value("following").toBool();
    case CanUnfollow:
        return identity.value("user_id").toInteger() > 0 && identity.value("following").toBool();
    case RelationshipKnown:
        return identity.value("user_id").toInteger() > 0 && identity.value("following").isBool();
    case VisitedAt:
        return r.value("visited_at_seconds").toInteger();
    case Meta:
        if (r.value("visited_at_seconds").isDouble() &&
            r.value("visited_at_seconds").toInteger() > 0)
            return QDateTime::fromSecsSinceEpoch(r.value("visited_at_seconds").toInteger())
                .toString("yyyy-MM-dd HH:mm");
        if (identity.value("user_id").toInteger() == 0 &&
            identity.value("artist_id").toInteger() > 0)
            return tr("歌手");
        if (identity.value("mutual").toBool())
            return tr("互相关注");
        // Following already determines the row action; keep metadata for the
        // distinct mutual relationship, artist identity and visitor timestamp.
        // Return a typed empty string: an invalid QVariant becomes "undefined"
        // when assigned to the delegate's required string property.
        return QString();
    case IdentityType:
        // Use raw iden_type with singerid to distinguish followed singers from users.
        return r.value("identity_facts").toObject().value("identity_type").toVariant();
    }
    return {};
}
QJsonObject SocialUserModel::entry(const QString &key) const
{
    for (const auto &v : m_rows) {
        const auto o = v.toObject();
        if (o["key"].toString() == key)
            return o;
    }
    return {};
}
void SocialUserModel::replace(QJsonArray rows)
{
    // Equal snapshots produce no notifications; account/tab replacement is atomic.
    // These endpoints return complete snapshots rather than paginated data.
    if (!m_store && rows == m_rows)
        return;
    beginResetModel();
    ++m_generation;
    m_hot.clear();
    m_demand.clear();
    m_reading = m_scheduled = m_readFailed = false;
    m_records.clear();
    if (m_store) {
        const auto snapshot = m_store->create(rows);
        m_rows = {};
        for (int i = 0; i < rows.size(); ++i) {
            const auto row = rows[i].toObject();
            m_rows.append(identityFor(row));
            m_records.append({snapshot, i});
            if (i < 64)
                m_hot.insert(i, row, decltype(m_hot)::NoExpiry, jsonValueCost(row));
        }
    } else
        m_rows = std::move(rows);
    endResetModel();
    emit changed();
}
void SocialUserModel::attachStore(JsonRecordStore &store)
{
    if (m_store)
        return;
    auto values = m_rows;
    m_store = &store;
    replace(std::move(values));
}
void SocialUserModel::readEntry(const QString &key, std::function<void(QJsonObject)> done)
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows[row].toObject().value("key").toString() != key)
            continue;
        if (!m_store) {
            done(m_rows[row].toObject());
            return;
        }
        if (const auto hit = m_hot.lookup(row)) {
            done(withRelationship(*hit, m_rows[row].toObject()));
            return;
        }
        const auto generation = m_generation;
        m_store->read(
            m_records[row], this,
            [this, row, generation, done = std::move(done)](QJsonArray rows, QString error) {
                if (generation != m_generation) {
                    done({});
                    return;
                }
                if (!error.isEmpty()) {
                    emit cacheFailed(error);
                    done({});
                    return;
                }
                done(rows.isEmpty()
                         ? QJsonObject{}
                         : withRelationship(rows.first().toObject(), m_rows[row].toObject()));
            });
        return;
    }
    done({});
}
void SocialUserModel::transform(std::function<QJsonArray(QJsonArray)> apply)
{
    if (!m_store) {
        replace(apply(m_rows));
        return;
    }
    const auto values = apply(m_rows);
    if (values == m_rows)
        return;
    // Confirmed relationships change only the thin index. Preserve immutable
    // source references instead of hydrating every user for a single follow.
    QHash<QString, QList<int>> positions;
    QHash<QString, int> used;
    for (int i = 0; i < m_rows.size(); ++i)
        positions[m_rows[i].toObject().value("key").toString()].append(i);
    QList<JsonRecordStore::Reference> records;
    QJsonArray identities, inserted;
    QList<int> insertedRows;
    QHash<int, QJsonObject> hot;
    for (int i = 0; i < values.size(); ++i) {
        const auto value = values[i].toObject();
        identities.append(identityFor(value));
        const auto key = value.value("key").toString();
        const auto found = positions.constFind(key);
        const int occurrence = used.value(key);
        used.insert(key, occurrence + 1);
        if (found != positions.cend() && occurrence < found->size()) {
            const int previous = found->at(occurrence);
            records.append(m_records[previous]);
            if (const auto hit = m_hot.peek(previous))
                hot.insert(i, *hit);
        } else {
            records.append(JsonRecordStore::Reference{});
            insertedRows.append(i);
            inserted.append(value);
            hot.insert(i, value);
        }
    }
    if (!inserted.isEmpty()) {
        const auto snapshot = m_store->create(std::move(inserted));
        for (int i = 0; i < insertedRows.size(); ++i)
            records[insertedRows[i]] = {snapshot, i};
    }
    beginResetModel();
    ++m_generation;
    m_demand.clear();
    m_reading = m_scheduled = m_readFailed = false;
    m_rows = std::move(identities);
    m_records = std::move(records);
    m_hot.clear();
    for (auto it = hot.cbegin(); it != hot.cend(); ++it)
        m_hot.insert(it.key(), it.value(), decltype(m_hot)::NoExpiry, jsonValueCost(it.value()));
    endResetModel();
    emit changed();
}
void SocialUserModel::fetch() const
{
    m_scheduled = false;
    if (m_reading || m_demand.isEmpty() || !m_store)
        return;
    const int start = *std::min_element(m_demand.cbegin(), m_demand.cend());
    const auto generation = m_generation;
    m_reading = true;
    auto *self = const_cast<SocialUserModel *>(this);
    const auto reference = m_records[start];
    int limit = 1;
    while (limit < 64 && start + limit < m_records.size() &&
           m_records[start + limit].snapshot == reference.snapshot &&
           m_records[start + limit].row == reference.row + limit)
        ++limit;
    m_store->read(
        reference, self,
        [this, self, start, generation](QJsonArray rows, QString error) {
            if (generation != m_generation)
                return;
            m_reading = false;
            if (!error.isEmpty()) {
                m_readFailed = true;
                m_demand.clear();
                emit self->cacheFailed(error);
                return;
            }
            for (int i = 0; i < rows.size(); ++i) {
                const auto row = rows[i].toObject();
                m_hot.insert(start + i, row, decltype(m_hot)::NoExpiry, jsonValueCost(row));
                m_demand.remove(start + i);
            }
            if (!rows.isEmpty())
                emit self->dataChanged(index(start), index(start + rows.size() - 1));
            if (!m_demand.isEmpty()) {
                m_scheduled = true;
                QTimer::singleShot(0, self, [this] { fetch(); });
            }
        },
        limit);
}
} // namespace EchoMusic
