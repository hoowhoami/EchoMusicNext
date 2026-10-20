#include "SettingsKeys.h"
#include "SidebarLayoutModel.h"
#include "LibraryController.h"
#include <algorithm>
#include <utility>
namespace EchoMusic
{
namespace
{
// Read-only presentation projection. Incremental changes keep surviving cards,
// hover handlers and an active drag source alive; preferences remain owned by
// SidebarLayoutModel rather than a second writable UI model.
class SidebarProjectionModel : public QAbstractListModel
{
public:
    explicit SidebarProjectionModel(QVariantList rows, QObject *parent, bool rowData = false)
        : QAbstractListModel(parent), m_rows(std::move(rows)), m_rowData(rowData)
    {
    }
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }
    QHash<int, QByteArray> roleNames() const override
    {
        if (m_rowData)
            return {{Qt::UserRole + 1, "rowData"}};
        return {{Qt::UserRole + 1, "shortcutKey"},
                {Qt::UserRole + 2, "shortcutTitle"},
                {Qt::UserRole + 3, "shortcutGlyph"},
                {Qt::UserRole + 4, "shortcutRoute"}};
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
            return {};
        const auto row = m_rows[index.row()].toMap();
        if (m_rowData)
            return role == Qt::UserRole + 1 ? QVariant(row) : QVariant{};
        switch (role) {
        case Qt::UserRole + 1:
            return row.value("key");
        case Qt::UserRole + 2:
            return row.value("title");
        case Qt::UserRole + 3:
            return row.value("glyph");
        case Qt::UserRole + 4:
            return row.value("route");
        default:
            return {};
        }
    }
    void reconcile(const QVariantList &next)
    {
        QSet<QString> keys;
        for (const auto &value : next)
            keys.insert(value.toMap().value("key").toString());
        for (int row = m_rows.size() - 1; row >= 0; --row) {
            if (keys.contains(m_rows[row].toMap().value("key").toString()))
                continue;
            beginRemoveRows({}, row, row);
            m_rows.removeAt(row);
            endRemoveRows();
        }
        for (int row = 0; row < next.size(); ++row) {
            const auto key = next[row].toMap().value("key");
            int from = row;
            while (from < m_rows.size() && m_rows[from].toMap().value("key") != key)
                ++from;
            if (from == m_rows.size()) {
                beginInsertRows({}, row, row);
                m_rows.insert(row, next[row]);
                endInsertRows();
            } else if (from != row) {
                beginMoveRows({}, from, from, {}, row);
                m_rows.move(from, row);
                endMoveRows();
            }
            if (m_rows[row] != next[row]) {
                m_rows[row] = next[row];
                emit dataChanged(index(row), index(row));
            }
        }
    }

private:
    QVariantList m_rows;
    bool m_rowData;
};
} // namespace
SidebarLayoutModel::SidebarLayoutModel(QSettings &preferences, QObject *parent)
    : QAbstractListModel(parent), m_preferences(preferences)
{
    // Exclude entries already represented by shortcut cards.
    // Only these three catalog entries belong in the visible menu.
    m_entries = {{"favorites", tr("我最喜爱"), "heart", "library"},
                 {"cloud", tr("音乐云盘"), "cloud", "library"},
                 {"history", tr("播放历史"), "clock", "library"}};
    m_shortcutCatalog = {{"home", tr("为您推荐"), "sparkles", {}},
                         {"explore", tr("探索发现"), "compass", {}},
                         {"free-listen", tr("免费听"), "music-discount", {}},
                         {"discover-flow", tr("刷歌"), "vinyl", {}},
                         {"personal-fm", tr("私人 FM"), "radio", {}},
                         {"recommend", tr("每日推荐"), "calendar", {}},
                         {"ranking", tr("排行榜"), "arrows-sort", {}},
                         {"purchased", tr("已购音乐"), "shopping-bag", {}},
                         {"listen-together", tr("一起听"), "headphones", {}}};
    m_hidden = preferences.value(EchoMusic::SettingsKeys::Sidebar::HiddenEntries).toStringList();
    m_hiddenGroups =
        preferences.value(EchoMusic::SettingsKeys::Sidebar::HiddenGroups).toStringList();
    const auto order =
        preferences.value(EchoMusic::SettingsKeys::Sidebar::EntryOrder).toStringList();
    // Ignore removed/unknown entries and append newly introduced entries in their
    // source order. Reordering never permits one entry to leave its group.
    std::stable_sort(m_entries.begin(), m_entries.end(), [&order](const Entry &a, const Entry &b) {
        const int ai = order.indexOf(a.key), bi = order.indexOf(b.key);
        return (ai < 0 ? order.size() : ai) < (bi < 0 ? order.size() : bi);
    });
    const auto pins =
        preferences
            .value(EchoMusic::SettingsKeys::Sidebar::Shortcuts, QStringList{"home", "explore"})
            .toStringList();
    for (const auto &key : pins)
        if (std::any_of(m_shortcutCatalog.cbegin(), m_shortcutCatalog.cend(),
                        [&key](const Entry &e) { return e.key == key; }) &&
            !m_pinned.contains(key))
            m_pinned.append(key);
    if (m_pinned.isEmpty())
        m_pinned = {"home"};
    m_shortcuts = new SidebarProjectionModel(shortcuts(), this);
    connect(this, &SidebarLayoutModel::changed, this, &SidebarLayoutModel::refreshSectionModel);
}
int SidebarLayoutModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}
QVariant SidebarLayoutModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const auto &e = m_entries.at(index.row());
    switch (role) {
    case Key:
        return e.key;
    case Title:
        return e.title;
    case Glyph:
        return e.glyph;
    case Group:
        return e.group;
    case Hidden:
        return m_hidden.contains(e.key);
    case Pinned:
        return m_pinned.contains(e.key);
    default:
        return {};
    }
}
QHash<int, QByteArray> SidebarLayoutModel::roleNames() const
{
    return {{Key, "entryKey"},     {Title, "entryTitle"},   {Glyph, "entryGlyph"},
            {Group, "entryGroup"}, {Hidden, "entryHidden"}, {Pinned, "entryPinned"}};
}
QVariantMap SidebarLayoutModel::describe(const Entry &e) const
{
    return {{"key", e.key},
            {"title", e.title},
            // Preference keys follow EchoMusic; route names follow RouteController.
            {"route", e.key == "recommend" ? QStringLiteral("recommend-songs") : e.key},
            {"glyph", e.glyph},
            {"hidden", m_hidden.contains(e.key)}};
}
QVariantList SidebarLayoutModel::sections() const
{
    QVariantList result;
    for (const auto &key : {QStringLiteral("library")}) {
        QVariantList items;
        for (const auto &e : m_entries)
            if (e.group == key)
                items.append(describe(e));
        result.append(QVariantMap{{"key", key},
                                  {"title", tr("音乐菜单")},
                                  {"collapsed", false},
                                  {"hidden", m_hiddenGroups.contains(key)},
                                  {"items", items}});
    }
    auto pinned = m_library ? m_library->pinnedLayoutEntries() : QVariantList{};
    for (auto &value : pinned) {
        auto entry = value.toMap();
        entry.insert("hidden", m_hidden.contains(entry.value("key").toString()));
        value = entry;
    }
    result.append(QVariantMap{{"key", "playlists"},
                              {"title", tr("歌单")},
                              {"lockedOrder", true},
                              {"collapsed", false},
                              {"hidden", !playlistAreaVisible()},
                              {"items", pinned}});
    return result;
}
QAbstractItemModel *SidebarLayoutModel::sectionModel()
{
    // Data projections are first requested by the actual editor instance;
    // they contain no UI, images, popup resources or writable preferences.
    if (!m_sections) {
        m_menuItems = new SidebarProjectionModel({}, this, true);
        m_playlistItems = new SidebarProjectionModel({}, this, true);
        m_sections = new SidebarProjectionModel({}, this, true);
        refreshSectionModel();
    }
    return m_sections;
}
void SidebarLayoutModel::refreshSectionModel()
{
    if (!m_sections)
        return;
    QVariantList rows;
    for (const auto &value : sections()) {
        auto section = value.toMap();
        const auto entries = section.take("items").toList();
        auto *items = static_cast<SidebarProjectionModel *>(
            section.value("key") == QStringLiteral("library") ? m_menuItems : m_playlistItems);
        items->reconcile(entries);
        const auto first = entries.isEmpty() ? QVariantMap{} : entries.first().toMap();
        section.insert("items", QVariant::fromValue<QAbstractItemModel *>(items));
        section.insert("itemCount", entries.size());
        section.insert("visibleCount",
                       std::count_if(entries.cbegin(), entries.cend(), [](const QVariant &entry) {
                           return !entry.toMap()["hidden"].toBool();
                       }));
        section.insert("glyph", first.value("glyph", "playlist-add"));
        section.insert("cover", first.value("cover", ""));
        rows.append(section);
    }
    static_cast<SidebarProjectionModel *>(m_sections)->reconcile(rows);
}
QVariantList SidebarLayoutModel::shortcuts() const
{
    QVariantList result;
    for (const auto &key : m_pinned)
        for (const auto &e : m_shortcutCatalog)
            if (e.key == key)
                result.append(describe(e));
    return result;
}
QString SidebarLayoutModel::homeRoute() const
{
    const auto entries = shortcuts();
    return entries.isEmpty() ? QStringLiteral("home")
                             : entries.first().toMap().value("route").toString();
}
bool SidebarLayoutModel::beginShortcutDrag(const QString &key)
{
    if (!m_dragKey.isEmpty() || !m_pinned.contains(key))
        return false;
    m_dragKey = key;
    m_dragOriginal = m_pinned;
    return true;
}
void SidebarLayoutModel::previewShortcutMove(const QString &key, const QString &target)
{
    if (key != m_dragKey)
        return;
    const int from = m_pinned.indexOf(key), to = m_pinned.indexOf(target);
    if (from < 0 || to < 0 || from == to)
        return;
    m_pinned.move(from, to);
    static_cast<SidebarProjectionModel *>(m_shortcuts)->reconcile(shortcuts());
    emit changed();
}
void SidebarLayoutModel::endShortcutDrag(bool commit)
{
    if (m_dragKey.isEmpty())
        return;
    const auto original = std::exchange(m_dragOriginal, {});
    m_dragKey.clear();
    if (!commit) {
        m_pinned = original;
        static_cast<SidebarProjectionModel *>(m_shortcuts)->reconcile(shortcuts());
        emit changed();
    } else if (m_pinned != original) {
        save();
    }
}
QVariantList SidebarLayoutModel::shortcutCatalog() const
{
    QVariantList result;
    for (const auto &e : m_shortcutCatalog) {
        auto entry = describe(e);
        entry.insert("pinned", m_pinned.contains(e.key));
        result.append(entry);
    }
    return result;
}
void SidebarLayoutModel::save()
{
    static_cast<SidebarProjectionModel *>(m_shortcuts)->reconcile(shortcuts());
    QStringList order;
    for (const auto &e : m_entries)
        order.append(e.key);
    m_preferences.setValue(EchoMusic::SettingsKeys::Sidebar::EntryOrder, order);
    m_preferences.setValue(EchoMusic::SettingsKeys::Sidebar::HiddenEntries, m_hidden);
    m_preferences.setValue(EchoMusic::SettingsKeys::Sidebar::HiddenGroups, m_hiddenGroups);
    // Other layout preferences may save during a drag. Never persist a preview.
    m_preferences.setValue(EchoMusic::SettingsKeys::Sidebar::Shortcuts,
                           m_dragKey.isEmpty() ? m_pinned : m_dragOriginal);
    applyPlaylistVisibility();
    emit changed();
}
void SidebarLayoutModel::toggleHidden(const QString &key)
{
    if (key == "playlist:created:default" || key == "playlist:created:liked") {
        if (!m_hidden.removeOne(key))
            m_hidden.append(key);
        save();
        return;
    }

    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).key == key) {
            if (!m_hidden.removeOne(key))
                m_hidden.append(key);
            emit dataChanged(index(i), index(i), {Hidden});
            save();
            return;
        }
}
void SidebarLayoutModel::toggleGroup(const QString &key)
{
    if (key != "library" && key != "playlists")
        return;
    if (!m_hiddenGroups.removeOne(key))
        m_hiddenGroups.append(key);
    save();
}
void SidebarLayoutModel::toggleShortcut(const QString &key)
{
    endShortcutDrag(false);
    if (std::none_of(m_shortcutCatalog.cbegin(), m_shortcutCatalog.cend(),
                     [&key](const Entry &e) { return e.key == key; }))
        return;
    if (m_pinned.contains(key)) {
        if (m_pinned.size() == 1)
            return;
        m_pinned.removeOne(key);
    } else
        m_pinned.append(key);
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).key == key)
            emit dataChanged(index(i), index(i), {Pinned});
    save();
}

void SidebarLayoutModel::moveBefore(const QString &key, const QString &before, bool after)
{
    int from = -1, to = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).key == key)
            from = i;
        if (m_entries.at(i).key == before)
            to = i;
    }
    if (from < 0 || to < 0 || from == to || m_entries.at(from).group != m_entries.at(to).group)
        return;
    const int insertion = to + (after ? 1 : 0);
    const int destination = insertion > from ? insertion - 1 : insertion;
    if (destination == from)
        return;
    beginMoveRows({}, from, from, {}, insertion);
    m_entries.move(from, destination);
    endMoveRows();
    save();
}
void SidebarLayoutModel::moveShortcutBefore(const QString &key, const QString &before, bool after)
{
    endShortcutDrag(false);
    const int from = m_pinned.indexOf(key), to = m_pinned.indexOf(before);
    if (from < 0 || to < 0 || from == to)
        return;
    const int insertion = to + (after ? 1 : 0);
    const int destination = insertion > from ? insertion - 1 : insertion;
    if (from == destination)
        return;
    m_pinned.move(from, destination);
    save();
}
LibraryController *SidebarLayoutModel::libraryController() const
{
    return m_library.data();
}
void SidebarLayoutModel::setLibraryController(LibraryController *controller)
{
    if (m_library == controller)
        return;
    if (m_library)
        disconnect(m_library, nullptr, this, nullptr);
    m_library = controller;
    if (controller) {
        connect(controller, &LibraryController::changed, this, &SidebarLayoutModel::changed);
        connect(controller, &QObject::destroyed, this, [this] { emit changed(); });
    }
    applyPlaylistVisibility();
    emit changed();
}
bool SidebarLayoutModel::playlistAreaVisible() const
{
    return !m_hiddenGroups.contains("playlists");
}
bool SidebarLayoutModel::menuVisible() const
{
    return !m_hiddenGroups.contains("library") &&
           std::any_of(m_entries.cbegin(), m_entries.cend(),
                       [this](const Entry &entry) { return !m_hidden.contains(entry.key); });
}
void SidebarLayoutModel::applyPlaylistVisibility()
{
    if (!m_library)
        return;
    QSet<quint32> hidden;
    if (m_hidden.contains("playlist:created:default"))
        hidden.insert(1);
    if (m_hidden.contains("playlist:created:liked"))
        hidden.insert(2);
    m_library->setHiddenDefaultKinds(hidden);
}
void SidebarLayoutModel::restoreShortcuts()
{
    endShortcutDrag(false);
    m_pinned = {"home", "explore"};
    if (!m_entries.isEmpty())
        emit dataChanged(index(0), index(m_entries.size() - 1), {Pinned});
    save();
}
void SidebarLayoutModel::restoreDefaults()
{
    beginResetModel();
    const QStringList order{"favorites", "cloud", "history"};
    std::sort(m_entries.begin(), m_entries.end(), [&order](const Entry &a, const Entry &b) {
        return order.indexOf(a.key) < order.indexOf(b.key);
    });
    m_hidden.clear();
    m_hiddenGroups.clear();
    endResetModel();
    save();
}
} // namespace EchoMusic
