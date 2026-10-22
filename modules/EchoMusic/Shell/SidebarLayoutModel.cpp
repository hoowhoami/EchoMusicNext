#include "SettingsKeys.h"
#include "SidebarLayoutModel.h"
#include "LibraryController.h"
#include <algorithm>
#include <utility>
#include <QTimer>
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
    m_pluginOrder = order;
    m_collapsedGroups =
        preferences.value(EchoMusic::SettingsKeys::Sidebar::CollapsedPluginGroups).toStringList();
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
        if (key.size() <= 256 && !m_pinned.contains(key))
            m_pinned.append(key);
    if (m_pinned.isEmpty())
        m_pinned = {"home"};
    m_navigation = new SidebarProjectionModel({}, this, true);
    m_shortcuts = new SidebarProjectionModel(shortcuts(), this);
    connect(this, &SidebarLayoutModel::changed, this, &SidebarLayoutModel::refreshSectionModel);
    refreshSectionModel();
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
    addPluginSections(result);
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
    QVariantList navigation;
    for (const auto &value : sections()) {
        const auto section = value.toMap();
        const auto key = section.value("key").toString();
        if (key == "playlists" || section.value("hidden").toBool())
            continue;
        if (key != "library")
            navigation.append(QVariantMap{{"key", "section:" + key},
                                          {"section", key},
                                          {"heading", true},
                                          {"title", section.value("title")},
                                          {"collapsible", section.value("collapsible", true)},
                                          {"collapsed", m_collapsedGroups.contains(key)}});
        if (m_collapsedGroups.contains(key))
            continue;
        for (const auto &item : section.value("items").toList())
            if (!item.toMap().value("hidden").toBool())
                navigation.append(item);
    }
    static_cast<SidebarProjectionModel *>(m_navigation)->reconcile(navigation);
    static_cast<SidebarProjectionModel *>(m_shortcuts)->reconcile(shortcuts());
    if (!m_sections)
        return;
    QVariantList rows;
    for (const auto &value : sections()) {
        auto section = value.toMap();
        const auto entries = section.take("items").toList();
        const auto sectionKey = section.value("key").toString();
        QAbstractItemModel *projection = sectionKey == "library" ? m_menuItems
                                         : sectionKey == "playlists"
                                             ? m_playlistItems
                                             : m_pluginSectionModels.value(sectionKey);
        if (!projection) {
            projection = new SidebarProjectionModel({}, this, true);
            m_pluginSectionModels.insert(sectionKey, projection);
        }
        auto *items = static_cast<SidebarProjectionModel *>(projection);
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
    QSet<QString> retained;
    for (const auto &row : rows)
        retained.insert(row.toMap().value("key").toString());
    for (const auto &key : m_pluginSectionModels.keys())
        if (!retained.contains(key)) {
            auto *obsolete = m_pluginSectionModels.take(key);
            obsolete->deleteLater();
        }
}
QVariantList SidebarLayoutModel::shortcuts() const
{
    QVariantList result;
    const auto plugins = pluginEntries(true);
    for (const auto &key : m_pinned) {
        for (const auto &e : m_shortcutCatalog)
            if (e.key == key)
                result.append(describe(e));
        for (const auto &e : plugins)
            if (e.toMap().value("key") == key && !e.toMap().value("hidden").toBool())
                result.append(e);
    }
    if (result.isEmpty() && !m_shortcutCatalog.isEmpty())
        result.append(describe(m_shortcutCatalog.first()));
    return result;
}
QString SidebarLayoutModel::homeRoute() const
{
    for (const auto &value : shortcuts()) {
        const auto route = value.toMap().value("route").toString();
        if (!route.isEmpty())
            return route;
    }
    return QStringLiteral("home");
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
    for (const auto &value : pluginEntries(true)) {
        auto entry = value.toMap();
        entry["pinned"] = m_pinned.contains(entry.value("key").toString());
        if (!entry.value("hidden").toBool())
            result.append(entry);
    }
    return result;
}
void SidebarLayoutModel::save()
{
    static_cast<SidebarProjectionModel *>(m_shortcuts)->reconcile(shortcuts());
    QStringList order = m_pluginOrder;
    for (const auto &entry : m_entries)
        if (!order.contains(entry.key))
            order.append(entry.key);
    m_preferences.setValue(EchoMusic::SettingsKeys::Sidebar::EntryOrder, order);
    m_preferences.setValue(EchoMusic::SettingsKeys::Sidebar::CollapsedPluginGroups,
                           m_collapsedGroups);
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
    if (isPlugin(key)) {
        if (!m_hidden.removeOne(key))
            m_hidden.append(key);
        save();
        return;
    }
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
    if (key != "library" && key != "playlists" &&
        std::none_of(m_plugins.cbegin(), m_plugins.cend(),
                     [&](const auto &e) { return e.values.value("section") == key; }))
        return;
    if (!m_hiddenGroups.removeOne(key))
        m_hiddenGroups.append(key);
    save();
}
void SidebarLayoutModel::toggleShortcut(const QString &key)
{
    endShortcutDrag(false);
    if (!isPlugin(key) && std::none_of(m_shortcutCatalog.cbegin(), m_shortcutCatalog.cend(),
                                       [&key](const Entry &e) { return e.key == key; }))
        return;
    if (m_pinned.contains(key)) {
        if (shortcuts().size() == 1)
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
    if (isPlugin(key) || isPlugin(before)) {
        for (const auto &value : sections()) {
            const auto items = value.toMap().value("items").toList();
            QStringList order;
            for (const auto &item : items)
                order.append(item.toMap().value("key").toString());
            const int from = order.indexOf(key), to = order.indexOf(before);
            if (from < 0 || to < 0)
                continue;
            order.removeAt(from);
            order.insert(qBound(0, to + (after ? 1 : 0) - (from < to ? 1 : 0), order.size()), key);
            auto retained = m_pluginOrder;
            for (const auto &item : items)
                retained.removeAll(item.toMap().value("key").toString());
            retained.append(order);
            m_pluginOrder = retained;
            save();
            return;
        }
        return;
    }
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
    if (!m_pluginOrder.isEmpty()) {
        m_pluginOrder.removeAll(key);
        int target = m_pluginOrder.indexOf(before);
        if (target < 0)
            target = m_pluginOrder.size();
        m_pluginOrder.insert(qMin(m_pluginOrder.size(), target + (after ? 1 : 0)), key);
    }
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
    m_pluginOrder.clear();
    m_collapsedGroups.clear();
    endResetModel();
    save();
}
} // namespace EchoMusic
