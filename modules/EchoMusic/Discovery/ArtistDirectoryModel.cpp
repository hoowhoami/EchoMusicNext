#include "ArtistDirectoryModel.h"
#include "SongCatalogModel.h"
#include "CacheCost.h"
#include <QQmlEngine>
#include <algorithm>
#include <numeric>

namespace EchoMusic
{
ArtistDirectoryModel::ArtistDirectoryModel(QObject *parent) : QAbstractListModel(parent)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    m_readTimer.setSingleShot(true);
    connect(&m_readTimer, &QTimer::timeout, this, &ArtistDirectoryModel::read);
}
void ArtistDirectoryModel::attach(SessionDatabase *database)
{
    if (!database || m_store)
        return;
    Q_ASSERT(m_ids.isEmpty());
    m_store = std::make_unique<JsonRecordStore>(*database);
    connect(m_store.get(), &JsonRecordStore::invalidated, this, [this] {
        cancelRead();
        m_source = {};
        m_hot.clear();
        m_oversized = {};
        m_oversizedSource = -1;
        m_recovering = !m_ids.isEmpty();
        emit changed();
        if (count())
            emit dataChanged(index(0), index(count() - 1));
        emit invalidated();
    });
}
void ArtistDirectoryModel::cancelRead()
{
    ++m_generation;
    m_readTimer.stop();
    delete m_reader.data();
    m_demand.clear();
    m_failed = false;
}
void ArtistDirectoryModel::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (!active) {
        cancelRead();
        m_oversized = {};
        m_oversizedSource = -1;
    } else if (count())
        emit dataChanged(index(0), index(count() - 1));
}
void ArtistDirectoryModel::replace(const QJsonArray &groups)
{
    cancelRead();
    beginResetModel();
    m_hot.clear();
    m_oversized = {};
    m_oversizedSource = -1;
    m_ids.clear();
    m_letters.clear();
    m_groups.clear();
    QJsonArray records;
    for (const auto &value : groups) {
        const auto group = value.toObject();
        const auto label = group.value("label").toString();
        m_letters.append(label);
        auto &members = m_groups[label];
        for (const auto &artist : group.value("artists").toArray()) {
            const auto id = artist.toObject().value("id");
            m_ids.append(id.isString()   ? id.toString()
                         : id.isDouble() ? QString::number(id.toInteger())
                                         : QString{});
            members.append(records.size());
            records.append(artist);
        }
    }
    m_fallback = {};
    m_source = m_store ? m_store->create(records) : JsonRecordStore::Snapshot{};
    if (!m_store)
        m_fallback = std::move(records);
    if (m_letter != "全部" && !m_letters.contains(m_letter)) {
        m_letter = m_letters.value(0);
        for (const auto &label : m_letters)
            if (label.size() > 1) {
                m_letter = label;
                break;
            }
    }
    project();
    endResetModel();
    finishRecovery();
    emit changed();
}
void ArtistDirectoryModel::clear()
{
    m_letter.clear();
    replace({});
}
void ArtistDirectoryModel::project()
{
    if (m_letter == "全部") {
        m_rows.resize(m_ids.size());
        std::iota(m_rows.begin(), m_rows.end(), 0);
    } else
        m_rows = m_groups.value(m_letter);
    m_positions.clear();
    for (int row = 0; row < m_rows.size(); ++row)
        m_positions.insert(m_rows[row], row);
}
bool ArtistDirectoryModel::selectLetter(const QString &letter)
{
    if ((letter != "全部" && !m_groups.contains(letter)) || letter == m_letter)
        return false;
    cancelRead();
    beginResetModel();
    m_letter = letter;
    project();
    endResetModel();
    emit changed();
    return true;
}
int ArtistDirectoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}
QHash<int, QByteArray> ArtistDirectoryModel::roleNames() const
{
    return {{SongCatalogModel::ResourceId, "resourceId"},
            {SongCatalogModel::Title, "title"},
            {SongCatalogModel::CoverUrl, "coverUrl"},
            {SongCatalogModel::PresentationReady, "presentationReady"}};
}
QVariant ArtistDirectoryModel::presentation(int source, int role) const
{
    auto view = m_hot.lookup(source);
    if (source == m_oversizedSource)
        view = m_oversized;
    if (!view && !m_store && source < m_fallback.size())
        view = m_fallback[source].toObject();
    if (!view) {
        auto *self = const_cast<ArtistDirectoryModel *>(this);
        m_demand.insert(source, true, ValueCache<int, bool>::NoExpiry);
        if (m_active && !m_recovering && !m_failed && !m_reader && !m_readTimer.isActive())
            self->m_readTimer.start();
        return role == SongCatalogModel::PresentationReady ? QVariant(false) : QVariant(QString());
    }
    if (role == SongCatalogModel::PresentationReady)
        return true;
    return view->value(role == SongCatalogModel::Title ? "title" : "cover_url").toString();
}
QVariant ArtistDirectoryModel::data(const QModelIndex &i, int role) const
{
    if (!i.isValid() || i.row() < 0 || i.row() >= count())
        return {};
    const auto source = m_rows[i.row()];
    if (role == SongCatalogModel::ResourceId)
        return m_ids[source];
    if (role == SongCatalogModel::Title || role == SongCatalogModel::CoverUrl ||
        role == SongCatalogModel::PresentationReady)
        return presentation(source, role);
    return {};
}
void ArtistDirectoryModel::read()
{
    if (!m_active || m_recovering || m_failed || m_reader || m_demand.size() == 0 || !m_store)
        return;
    // Demand keys are ordered by recency. Reading forward from the newest
    // (usually last) row would reread overlapping ranges for every earlier row.
    const auto demands = m_demand.keys();
    const auto first = *std::min_element(demands.cbegin(), demands.cend());
    const auto generation = m_generation;
    m_reader = new QObject(this);
    m_store->read(
        {m_source, first}, m_reader,
        [this, generation, first](auto records, auto error) {
            if (generation != m_generation)
                return;
            m_reader->deleteLater();
            m_reader.clear();
            if (!error.isEmpty() || records.isEmpty()) {
                m_failed = true;
                emit readFailed(error.isEmpty() ? tr("歌手目录缓存不完整") : error);
                return;
            }
            QList<int> changed;
            for (int i = 0; i < records.size(); ++i) {
                const auto record = records[i].toObject();
                const QJsonObject view{{"title", record.value("title")},
                                       {"cover_url", record.value("cover_url")}};
                // An unusually large visible name/URL may occupy the whole window.
                // Keep forward progress without admitting any complete provider metadata.
                if (jsonValueCost(view) > 512 * 1024) {
                    m_oversized = view;
                    m_oversizedSource = first + i;
                } else
                    m_hot.insert(first + i, view, ValueCache<int, QJsonObject>::NoExpiry,
                                 jsonValueCost(view));
                m_demand.remove(first + i);
                if (m_positions.contains(first + i))
                    changed.append(m_positions.value(first + i));
            }
            // Publish contiguous presentation ranges after admitting the batch;
            // identity/order did not change and need no proxy/delegate refresh.
            std::sort(changed.begin(), changed.end());
            for (int i = 0; i < changed.size();) {
                const int start = changed[i];
                int end = start;
                while (++i < changed.size() && changed[i] == end + 1)
                    end = changed[i];
                emit dataChanged(index(start), index(end),
                                 {SongCatalogModel::Title, SongCatalogModel::CoverUrl,
                                  SongCatalogModel::PresentationReady});
            }
            if (m_demand.size())
                m_readTimer.start();
        },
        32);
}
bool ArtistDirectoryModel::retryRead()
{
    if (!m_failed)
        return false;
    m_failed = false;
    m_readTimer.start();
    return true;
}
void ArtistDirectoryModel::finishRecovery()
{
    if (!m_recovering)
        return;
    m_recovering = false;
    emit changed();
}
} // namespace EchoMusic
