#include "RecommendationCards.h"
#include "SettingsKeys.h"
#include "RouteController.h"
#include "SongMetadataService.h"
#include "CacheCost.h"
#include <QJsonObject>
#include <QQmlEngine>
#include <QRandomGenerator>
namespace EchoMusic
{
void RecommendationItemsModel::attach(SessionDatabase *database)
{
    if (!database || m_store)
        return;
    m_store = std::make_unique<JsonRecordStore>(*database);
    connect(m_store.get(), &JsonRecordStore::cacheFailed, this, [this](const QString &error) {
        m_reads.clear();
        m_readFailed = true;
        emit readFailed(error);
    });
    connect(m_store.get(), &JsonRecordStore::invalidated, this, [this] {
        ++m_generation;
        m_hot.clear();
        m_reads.clear();
        emit invalidated();
    });
}
void RecommendationItemsModel::replace(QJsonArray rows)
{
    beginResetModel();
    ++m_generation;
    m_hot.clear();
    m_reads.clear();
    m_readFailed = false;
    m_count = rows.size();
    m_source = {};
    m_fallback = {};
    if (m_store)
        m_source = m_store->create(std::move(rows));
    else
        m_fallback = std::move(rows);
    endResetModel();
    emit countChanged();
}
QVariant RecommendationItemsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_count || role != Qt::UserRole + 1)
        return {};
    auto *self = const_cast<RecommendationItemsModel *>(this);
    if (const auto hit = self->m_hot.lookup(index.row()))
        return hit->toVariantMap();
    self->read(index.row());
    if (const auto hit = self->m_hot.peek(index.row()))
        return hit->toVariantMap();
    return QVariantMap{};
}
void RecommendationItemsModel::requestItem(int row, QObject *consumer,
                                           std::function<void(QJsonObject)> done)
{
    if (row < 0 || row >= m_count)
        return;
    const auto generation = m_generation;
    if (m_store)
        m_store->read({m_source, row}, consumer,
                      [this, generation, done = std::move(done)](QJsonArray rows, QString error) {
                          if (generation != m_generation)
                              return;
                          if (!error.isEmpty() || rows.isEmpty()) {
                              m_reads.clear();
                              m_readFailed = true;
                              emit readFailed(error.isEmpty() ? tr("内容暂时无法读取") : error);
                              return;
                          }
                          done(rows.first().toObject());
                      });
    else
        done(m_fallback[row].toObject());
}
bool RecommendationItemsModel::retryRead()
{
    if (!m_readFailed)
        return false;
    m_readFailed = false;
    m_reads.clear();
    if (m_count > 0)
        emit dataChanged(index(0), index(m_count - 1));
    return true;
}
void RecommendationItemsModel::read(int row)
{
    if (m_reads.contains(row))
        return;
    m_reads.insert(row);
    const auto generation = m_generation;
    requestItem(row, this, [this, row, generation](QJsonObject record) {
        if (generation != m_generation)
            return;
        m_reads.remove(row);
        QJsonObject view;
        for (const auto &key : {"id", "title", "cover_url", "url", "kind", "description", "color",
                                "start_index", "song_count"})
            view[key] = record[key];
        QJsonArray preview;
        for (const auto &value : record["preview"].toArray()) {
            if (preview.size() == 3)
                break;
            const auto song = value.toObject();
            preview.append(QJsonObject{
                {"id", song["id"]}, {"title", song["title"]}, {"artist", song["artist"]}});
        }
        view["preview"] = preview;
        m_hot.insert(row, view, ValueCache<int, QJsonObject>::NoExpiry, jsonValueCost(view));
        emit dataChanged(index(row), index(row));
    });
}
RecommendationCard::RecommendationCard(QString key, QString title, PlaybackService &playback,
                                       SongCatalogModel *borrowed, SongCatalogActions *actions,
                                       QObject *parent)
    : QObject(parent), m_playback(playback), m_key(std::move(key)), m_title(std::move(title)),
      m_collection(borrowed), m_actions(actions)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(&m_groups, QQmlEngine::CppOwnership);
    connect(&m_groups, &RecommendationItemsModel::readFailed, this, [this](const QString &error) {
        cancelGroupPlay();
        setState(3, error);
    });
    if (!m_collection) {
        m_ownedCollection = std::make_unique<SongCatalogModel>();
        m_collection = m_ownedCollection.get();
    }
    if (!m_actions) {
        m_ownedActions = std::make_unique<SongCatalogActions>(*m_collection, playback, this);
        m_actions = m_ownedActions.get();
    }
    if (m_ownedCollection)
        m_collection->enableSessionStorage();
    if (auto *metadata = playback.metadata()) {
        m_groups.attach(metadata->sessionDatabase());
    }
    m_actions->setQueueContext(m_title, "queue:recommendation:" + m_key);
    connect(&m_groups, &QAbstractItemModel::modelReset, this, &RecommendationCard::cancelGroupPlay);
    connect(m_collection, &SongCatalogModel::readFailed, this, [this] { cancelGroupPlay(); });
}
QString RecommendationCard::groupQueueKey(int index) const
{
    return "queue:recommendation:" + m_key + ":group:" + QString::number(index);
}
RecommendationCard *RecommendationCard::groupCard(int index, QObject *owner)
{
    if (!owner || m_groupParent || index < 0)
        return nullptr;
    const auto name = "recommendation-group-" + m_key + "-" + QString::number(index);
    if (auto *existing = owner->findChild<RecommendationCard *>(name, Qt::FindDirectChildrenOnly))
        return existing;
    auto *result = new RecommendationCard(m_key + ":group:" + QString::number(index),
                                          QStringLiteral("…"), m_playback, nullptr, nullptr, owner);
    result->setObjectName(name);
    result->m_groupParent = this;
    result->m_groupIndex = index;
    result->collection()->setMetadataActive(false);
    if (auto *context = qobject_cast<RouteContext *>(owner)) {
        result->collection()->setMetadataActive(context->presented());
        connect(context, &RouteContext::presentationChanged, result, [context, result] {
            result->collection()->setMetadataActive(context->presented());
        });
    }
    connect(this, &RecommendationCard::changed, result, &RecommendationCard::syncGroup);
    connect(this, &QObject::destroyed, result, [result] { result->clear(); });
    connect(m_collection, &SongCatalogModel::storageReadyChanged, result, [result] {
        if (result->m_groupParent && result->m_groupParent->collection()->storageReady())
            result->syncGroup();
    });
    result->syncGroup();
    return result;
}
void RecommendationCard::syncGroup()
{
    delete m_groupRead.data();
    if (!m_groupParent)
        return;
    auto *parent = m_groupParent.data();
    if (parent->status() != 2) {
        if (!parent->collection()->totalCount())
            m_collection->clear();
        setState(parent->status(), parent->errorMessage());
        return;
    }
    if (m_groupIndex >= parent->groups()->count()) {
        m_collection->clear();
        setState(3, tr("未找到这组推荐内容"));
        return;
    }
    setState(1);
    if (!parent->collection()->storageReady())
        return;
    m_groupRead = new QObject(this);
    parent->groups()->requestItem(m_groupIndex, m_groupRead, [this](QJsonObject group) {
        if (!m_groupParent)
            return;
        m_providerTitle = group["title"].toString();
        m_subtitle = group["description"].toString();
        m_groupFirst = group["start_index"].toInt();
        m_groupCount = group["song_count"].toInt();
        m_collection->replaceSourceRange(*m_groupParent->collection(), m_groupFirst, m_groupCount);
        m_actions->setQueueContext(title(), "queue:recommendation:" + m_key);
        setState(2);
    });
}
void RecommendationCard::cancelGroupPlay()
{
    delete m_groupPlayRead.data();
    if (m_preparingGroup != -1) {
        m_preparingGroup = -1;
        emit groupPlaybackChanged();
    }
}
void RecommendationCard::playGroup(int index)
{
    if (index < 0 || index >= m_groups.count() || m_preparingGroup == index)
        return;
    cancelGroupPlay();
    m_groupPlayRead = new QObject(this);
    m_preparingGroup = index;
    emit groupPlaybackChanged();
    const auto sequence = m_playback.clock().value("trackSeq");
    const QPointer<QObject> request = m_groupPlayRead;
    m_groups.requestItem(index, request, [this, index, request, sequence](QJsonObject group) {
        if (!request || request != m_groupPlayRead)
            return;
        if (sequence != m_playback.clock().value("trackSeq")) {
            cancelGroupPlay();
            return;
        }
        // A refreshed response may assign this position to another style.
        // Only pause/resume the queue that still names this actual group.
        if (m_playback.sourceKey() == groupQueueKey(index) &&
            m_playback.queueTitle() == group["title"].toString()) {
            cancelGroupPlay();
            if (!m_playback.loading())
                m_playback.toggle();
            return;
        }
        QStringList ids;
        const int first = qBound(0, group["start_index"].toInt(), m_collection->totalCount());
        const int count =
            qBound(0, group["song_count"].toInt(), m_collection->totalCount() - first);
        for (int row = first; row < first + count; ++row)
            ids.append(m_collection->sourceData(row, SongCatalogModel::ResourceId).toString());
        const auto title = group["title"].toString();
        m_collection->requestRows(
            ids, request, [this, index, request, sequence, title](PlaybackTracks tracks) {
                if (!request || request != m_groupPlayRead)
                    return;
                // Retire the read before playback emits new application state.
                cancelGroupPlay();
                if (sequence != m_playback.clock().value("trackSeq"))
                    return;
                tracks.removeIf([](const auto &track) { return !track.playable(); });
                if (tracks.isEmpty())
                    return;
                const int start = m_playback.playMode() == PlaybackService::Shuffle
                                      ? QRandomGenerator::global()->bounded(int(tracks.size()))
                                      : 0;
                m_playback.playTracks(tracks, start, title, groupQueueKey(index));
            });
    });
}
void RecommendationCard::setState(int state, QString error)
{
    m_status = state;
    m_error = std::move(error);
    emit changed();
}
void RecommendationCard::accept(const QJsonObject &o)
{
    m_providerTitle = o["title"].toString().trimmed();
    m_subtitle = o["description"].toString().trimmed();
    if (m_subtitle.trimmed().isEmpty()) {
        // Summaries are derived once from a bounded prefix of the response;
        // they never enumerate the catalog or start metadata reads.
        const auto songs = o["songs"].toArray();
        QStringList artists, titles;
        for (int i = 0; i < 6 && i < songs.size(); ++i) {
            const auto song = songs[i].toObject();
            const auto reason = song["metadata"]
                                    .toObject()["recommendation"]
                                    .toObject()["headline"]
                                    .toString()
                                    .trimmed();
            if (m_key == "new" && !reason.isEmpty()) {
                m_subtitle = reason;
                break;
            }
            const auto title = song["title"].toString().trimmed();
            if (!title.isEmpty() && titles.size() < 3 && !titles.contains(title))
                titles.append(title);
            const auto names = song["artists"].toArray();
            for (int j = 0; j < 3 && j < names.size() && artists.size() < 3; ++j) {
                const auto name = names[j].toObject()["name"].toString().trimmed();
                if (!name.isEmpty() && !artists.contains(name))
                    artists.append(name);
            }
        }
        if (m_subtitle.trimmed().isEmpty()) {
            if (!artists.isEmpty())
                m_subtitle = tr("歌手：%1").arg(artists.join(" / "));
            else if (!titles.isEmpty())
                m_subtitle = tr("推荐：%1").arg(titles.join(" / "));
        }
    }
    m_collection->replaceSongs(o["songs"].toArray());
    m_groups.replace(o["groups"].toArray());
    m_actions->setQueueContext(title(), "queue:recommendation:" + m_key);
    setState(2);
}
void RecommendationCard::clear()
{
    m_collection->clear();
    m_groups.replace({});
    m_providerTitle.clear();
    m_subtitle.clear();
    setState(0);
}
void RecommendationCards::add(RecommendationCard *card)
{
    m_catalog.append(card);
}
QStringList RecommendationCards::defaults() const
{
    return {"fm", "daily", "3001", "style", "new"};
}
void RecommendationCards::setPreferences(SettingsStore *preferences)
{
    m_preferences = preferences;
    auto order = preferences
                     ? preferences->value(SettingsKeys::Discovery::Cards, defaults()).toStringList()
                     : defaults();
    const bool migrateDefaults =
        preferences &&
        preferences->value(SettingsKeys::Discovery::CardsDefaultsVersion, 0).toInt() < 1;
    if (migrateDefaults) {
        const QStringList previousDefaults{"daily", "style", "3001", "3004", "3005", "3006", "3014",
                                           "3101",  "3102",  "3103", "3105", "3106", "new"};
        if (order == previousDefaults)
            order = defaults();
    }
    QStringList next;
    for (const auto &key : order)
        if (find(key) && !next.contains(key))
            next.append(key);
    if (next.isEmpty())
        next = defaults();
    beginResetModel();
    m_order = next;
    endResetModel();
    if (preferences && (migrateDefaults || order != m_order))
        preferences->setValue(SettingsKeys::Discovery::Cards, m_order);
    if (migrateDefaults)
        preferences->setValue(SettingsKeys::Discovery::CardsDefaultsVersion, 1);
    emit changed();
}
RecommendationCard *RecommendationCards::find(const QString &key) const
{
    for (auto *card : m_catalog)
        if (card->key() == key)
            return card;
    return nullptr;
}
RecommendationCard *RecommendationCards::at(int row) const
{
    return row >= 0 && row < m_order.size() ? find(m_order[row]) : nullptr;
}
QVariant RecommendationCards::data(const QModelIndex &i, int role) const
{
    return i.isValid() && role == Qt::UserRole + 1 ? QVariant::fromValue(at(i.row())) : QVariant{};
}
QVariantList RecommendationCards::catalog() const
{
    QVariantList rows;
    for (auto *card : m_catalog)
        rows.append(QVariant::fromValue(card));
    return rows;
}
void RecommendationCards::save()
{
    if (m_preferences)
        m_preferences->setValue(SettingsKeys::Discovery::Cards, m_order);
    emit changed();
}
void RecommendationCards::toggle(const QString &key)
{
    if (!find(key))
        return;
    int row = m_order.indexOf(key);
    if (row >= 0) {
        if (m_order.size() == 1)
            return;
        beginRemoveRows({}, row, row);
        m_order.removeAt(row);
        endRemoveRows();
    } else {
        row = m_order.size();
        beginInsertRows({}, row, row);
        m_order.append(key);
        endInsertRows();
    }
    save();
}
void RecommendationCards::move(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_order.size() || to >= m_order.size() || from == to)
        return;
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_order.move(from, to);
    endMoveRows();
    save();
}
void RecommendationCards::restoreDefaults()
{
    beginResetModel();
    m_order = defaults();
    endResetModel();
    save();
}
} // namespace EchoMusic
