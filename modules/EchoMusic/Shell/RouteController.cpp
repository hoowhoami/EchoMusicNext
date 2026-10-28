#include "RouteController.h"
#include "SidebarLayoutModel.h"
#include <QScopedValueRollback>
#include <QSet>
#include <QThread>
#include <QUrlQuery>
#include <algorithm>

namespace EchoMusic
{
RouteController::RouteController(QObject *parent) : QAbstractListModel(parent)
{
    // Register routes only for available page components.
    // Metadata controls caching and refresh behavior.
    const QStringList excluded = {"sidebar-home",    "login",       "loading",
                                  "error",           "song-detail", "mv-detail",
                                  "share-resolve",   "search",      "plugin-share-resolve",
                                  "listen-together", "profile",     "settings",
                                  "themes"};
    const QList<QPair<QString, QString>> routes = {{"sidebar-home", "/main"},
                                                   {"loading", "/"},
                                                   {"login", "/login"},
                                                   {"home", "/main/home"},
                                                   {"explore", "/main/explore"},
                                                   {"free-listen", "/main/free-listen"},
                                                   {"personal-fm", "/main/personal-fm"},
                                                   {"discover-flow", "/main/discover-flow"},
                                                   {"recommend-songs", "/main/recommend"},
                                                   {"listen-together", "/main/listen-together"},
                                                   {"search", "/main/search"},
                                                   {"recognize", "/main/recognize"},
                                                   {"ranking", "/main/ranking"},
                                                   {"history", "/main/history"},
                                                   {"favorites", "/main/favorites"},
                                                   {"cloud", "/main/cloud"},
                                                   {"purchased", "/main/purchased"},
                                                   {"profile", "/main/profile"},
                                                   {"settings", "/main/settings"},
                                                   {"themes", "/main/themes"},
                                                   {"plugin-management", "/main/settings/plugins"},
                                                   {"playlist-detail", "/main/playlist/:id"},
                                                   {"artist-detail", "/main/artist/:id"},
                                                   {"album-detail", "/main/album/:id"},
                                                   {"song-detail", "/main/detail/:id"},
                                                   {"mv-detail", "/main/mv/:id"},
                                                   {"share-resolve", "/main/share"},
                                                   {"plugin-share-resolve", "/main/plugin-share"},
                                                   {"error", "/main/error"}};
    const QStringList tabs = {"explore",   "search",          "history",       "favorites",
                              "purchased", "playlist-detail", "artist-detail", "album-detail"};
    for (const auto &[name, path] : routes) {
        Definition definition{name,
                              path,
                              {},
                              tabs.contains(name) ? QStringList{"tab"} : QStringList{},
                              !excluded.contains(name),
                              true,
                              false,
                              {}};
        definition.accountScoped = name != "themes" && name != "search" &&
                                   name != "plugin-management" && name != "plugin-share-resolve";
        definition.skipHistory = name == "loading" || name == "login";
        if (name == "song-detail")
            definition.tabQueryKeys = {"tab", "mainTab", "commentTab"};
        if (name == "plugin-management")
            definition.tabQueryKeys = {"view"};
        m_definitions.insert(name, definition);
    }
}
int RouteController::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_pages.size());
}
QVariant RouteController::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_pages.size() ||
        role != Qt::UserRole + 1)
        return {};
    return QVariant::fromValue(m_pages.at(index.row()));
}
QHash<int, QByteArray> RouteController::roleNames() const
{
    return {{Qt::UserRole + 1, "pageContext"}};
}
bool RouteController::registerPage(Definition definition)
{
    if (definition.name.isEmpty() || !definition.path.startsWith('/') ||
        definition.component.isEmpty() || m_closed || m_changing ||
        (m_definitions.contains(definition.name) &&
         !m_definitions.value(definition.name).component.isEmpty()))
        return false;
    const auto name = definition.name;
    m_definitions.insert(name, std::move(definition));
    emit registryChanged();
    return true;
}
quint64 RouteController::registerPluginPage(Definition definition)
{
    const auto name = definition.name;
    definition.pluginOwned = true;
    if (!registerPage(std::move(definition)))
        return 0;
    const auto revision = ++m_registrationRevision;
    m_pluginPageRevisions.insert(name, revision);
    return revision;
}
bool RouteController::unregisterPage(const QString &name, quint64 revision)
{
    if (!revision || m_pluginPageRevisions.value(name) != revision ||
        !m_definitions.value(name).pluginOwned)
        return false;
    if (m_changing) {
        QMetaObject::invokeMethod(
            this, [this, name, revision] { unregisterPage(name, revision); }, Qt::QueuedConnection);
        return true;
    }
    QScopedValueRollback<bool> mutation(m_changing, true);
    emit dismissOverlays();
    const bool wasCurrent = m_current && m_current->name() == name;
    if (wasCurrent) {
        m_current->m_active = false;
        emit m_current->activeChanged();
        m_current = nullptr;
    }
    for (int i = int(m_pages.size()) - 1; i >= 0; --i)
        if (m_pages[i]->name() == name)
            removePage(i);
    for (int i = int(m_history.size()) - 1; i >= 0; --i)
        if (m_history[i].name == name) {
            m_history.removeAt(i);
            if (i <= m_cursor)
                --m_cursor;
        }
    m_definitions.remove(name);
    m_pluginPageRevisions.remove(name);
    m_cursor = std::clamp(m_cursor, -1, int(m_history.size()) - 1);
    if (wasCurrent && !m_closed) {
        if (m_cursor < 0 && !m_history.isEmpty())
            m_cursor = 0;
        if (m_cursor >= 0)
            present(m_history[m_cursor]);
        else if (available(homeRoute())) {
            m_history.append({homeRoute(), homeResourceId(), {}});
            m_cursor = 0;
            present(m_history[0]);
        }
    }
    emit registryChanged();
    emit routeChanged();
    emit cacheChanged();
    return true;
}
bool RouteController::available(const QString &name) const
{
    return name == "settings" ||
           (m_definitions.contains(name) && !m_definitions.value(name).component.isEmpty());
}
bool RouteController::valid(const Location &location) const
{
    if (!available(location.name) || location.name == "settings" || location.resource.size() > 512)
        return false;
    const auto definition = m_definitions.value(location.name);
    if (definition.path.contains(":id") && location.resource.isEmpty())
        return false;
    if (location.query.size() > 32)
        return false;
    for (auto it = location.query.cbegin(); it != location.query.cend(); ++it)
        if (it.key().size() > 128 || it.value().metaType().id() != QMetaType::QString ||
            it.value().toString().size() > 4096)
            return false;
    return true;
}
QString RouteController::canonical(const Location &location) const
{
    const auto definition = m_definitions.value(location.name);
    QString path = definition.path;
    path.replace(":id", QString::fromLatin1(QUrl::toPercentEncoding(location.resource)));
    QUrlQuery query;
    for (auto it = location.query.cbegin(); it != location.query.cend(); ++it)
        if (it.key() != "_t" && !definition.tabQueryKeys.contains(it.key()))
            query.addQueryItem(it.key(), it.value().toString());
    return path + (query.isEmpty() ? QString{} : "?" + query.toString(QUrl::FullyEncoded));
}
bool RouteController::navigate(const QString &name, const QVariantMap &query,
                               const QString &resourceId)
{
    return goTo(name, query, resourceId, false);
}
bool RouteController::replace(const QString &name, const QVariantMap &query,
                              const QString &resourceId)
{
    return goTo(name, query, resourceId, true);
}
bool RouteController::goTo(const QString &name, const QVariantMap &query, const QString &resourceId,
                           bool replace)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed)
        return false;
    if (m_changing) {
        QMetaObject::invokeMethod(
            this,
            [this, name, query, resourceId, replace] { goTo(name, query, resourceId, replace); },
            Qt::QueuedConnection);
        return true;
    }
    if (name == "settings") {
        emit settingsRequested();
        return true;
    }
    Location location{name, resourceId, query};
    location.query.remove("_t"); // Refresh is a native generation, never a user query parameter.
    if (!valid(location)) {
        emit navigationRejected(name);
        return false;
    }
    QScopedValueRollback<bool> mutation(m_changing, true);
    emit dismissOverlays();
    if (m_cursor >= 0 && m_history[m_cursor].name == name &&
        m_history[m_cursor].resource == resourceId && m_history[m_cursor].query == location.query)
        return true;
    if (replace || m_definitions.value(name).skipHistory ||
        (m_cursor >= 0 && m_definitions.value(m_history[m_cursor].name).skipHistory)) {
        if (m_cursor < 0) {
            m_history.append(location);
            m_cursor = 0;
        } else
            m_history[m_cursor] = location;
    } else {
        while (m_history.size() > m_cursor + 1)
            m_history.removeLast();
        m_history.append(location);
        ++m_cursor;
        if (m_history.size() > 128) {
            m_history.removeFirst();
            --m_cursor;
        }
    }
    present(location);
    return true;
}
void RouteController::replaceQuery(const QVariantMap &query)
{
    if (m_closed || m_cursor < 0)
        return;
    if (m_changing) {
        QMetaObject::invokeMethod(
            this, [this, query] { replaceQuery(query); }, Qt::QueuedConnection);
        return;
    }
    auto next = m_history[m_cursor];
    next.query = query;
    next.query.remove("_t");
    if (!valid(next) || next.query == m_history[m_cursor].query)
        return;
    QScopedValueRollback<bool> mutation(m_changing, true);
    m_history[m_cursor] = next;
    present(next);
}
void RouteController::back()
{
    if (m_closed || !canGoBack())
        return;
    if (m_changing) {
        QMetaObject::invokeMethod(this, &RouteController::back, Qt::QueuedConnection);
        return;
    }
    QScopedValueRollback<bool> mutation(m_changing, true);
    emit dismissOverlays();
    present(m_history[--m_cursor]);
}
void RouteController::forward()
{
    if (m_closed || !canGoForward())
        return;
    if (m_changing) {
        QMetaObject::invokeMethod(this, &RouteController::forward, Qt::QueuedConnection);
        return;
    }
    QScopedValueRollback<bool> mutation(m_changing, true);
    emit dismissOverlays();
    present(m_history[++m_cursor]);
}
bool RouteController::cacheable(const RouteContext *page) const
{
    return m_enabled && m_max > 0 && m_definitions.value(page->name()).cache;
}
void RouteController::present(const Location &location)
{
    const auto branch = canonical(location);
    const auto key = QString::number(m_accountEpoch) + ':' + branch +
                     "::refresh:" + QString::number(m_revisions.value(branch));
    RouteContext *next = nullptr;
    for (auto *page : m_pages)
        if (page->m_key == key) {
            next = page;
            break;
        }
    auto *previous = m_current;
    if (next == previous && next) {
        next->m_query = location.query;
        emit next->queryChanged();
        emit routeChanged();
        emit pageActivated(next);
        return;
    }
    if (!next) {
        next = new RouteContext(this);
        next->m_name = location.name;
        next->m_key = key;
        next->m_canonical = branch;
        next->m_resource = location.resource;
        next->m_component = m_definitions.value(location.name).component;
        next->m_pluginOwned = m_definitions.value(location.name).pluginOwned;
        next->m_initialProperties = m_definitions.value(location.name).initialProperties;
        next->m_instance = QString::number(++m_instance);
        next->m_query = location.query;
        const int row = int(m_pages.size());
        beginInsertRows({}, row, row);
        m_pages.append(next);
        endInsertRows();
    } else {
        next->m_query = location.query;
        emit next->queryChanged();
    }
    // Publish the new identity before pruning the outgoing object. Animation and
    // refresh never retain a reference to an invalidated page's QML item.
    m_current = next;
    next->m_used = ++m_clock;
    if (previous) {
        previous->m_active = false;
        emit previous->activeChanged();
    }
    next->m_active = true;
    emit next->activeChanged();
    emit routeChanged();
    if (previous && (!cacheable(previous) || previous->m_canonical == branch))
        removePage(int(m_pages.indexOf(previous)));
    prune();
    // Refresh revisions live only as long as a history entry or mounted page.
    // Repeated navigation must not grow this metadata after its page is evicted.
    QSet<QString> live;
    for (const auto &entry : m_history)
        live.insert(canonical(entry));
    for (const auto *page : m_pages)
        live.insert(page->m_canonical);
    for (auto it = m_revisions.begin(); it != m_revisions.end();) {
        if (!live.contains(it.key()))
            it = m_revisions.erase(it);
        else
            ++it;
    }
    emit pageActivated(next);
    emit cacheChanged();
}
void RouteController::removePage(int row)
{
    if (row < 0 || row >= m_pages.size())
        return;
    auto *page = m_pages[row];
    beginRemoveRows({}, row, row);
    m_pages.removeAt(row);
    endRemoveRows();
    page->deleteLater();
}
void RouteController::prune()
{
    for (int i = int(m_pages.size()) - 1; i >= 0; --i)
        if (m_pages[i] != m_current && !cacheable(m_pages[i]))
            removePage(i);
    while (m_pages.size() > m_max + (m_current && !cacheable(m_current) ? 1 : 0)) {
        int oldest = -1;
        for (int i = 0; i < m_pages.size(); ++i)
            if (m_pages[i] != m_current &&
                (oldest < 0 || m_pages[i]->m_used < m_pages[oldest]->m_used))
                oldest = i;
        if (oldest < 0)
            break;
        removePage(oldest);
    }
}
void RouteController::refresh()
{
    if (m_closed || m_cursor < 0)
        return;
    if (m_changing) {
        QMetaObject::invokeMethod(this, &RouteController::refresh, Qt::QueuedConnection);
        return;
    }
    QScopedValueRollback<bool> mutation(m_changing, true);
    ++m_revisions[canonical(m_history[m_cursor])];
    present(m_history[m_cursor]);
}
void RouteController::setCachePolicy(bool enabled, int maximum)
{
    if (m_closed)
        return;
    if (m_changing) {
        QMetaObject::invokeMethod(
            this, [this, enabled, maximum] { setCachePolicy(enabled, maximum); },
            Qt::QueuedConnection);
        return;
    }
    QScopedValueRollback<bool> mutation(m_changing, true);
    m_enabled = enabled;
    m_max = std::clamp(maximum, 0, 30);
    prune();
    emit cacheChanged();
}
void RouteController::setHomeLayout(SidebarLayoutModel *layout)
{
    m_homeLayout = layout;
}
QString RouteController::homeRoute() const
{
    const auto route = m_homeLayout ? m_homeLayout->homeRoute() : QStringLiteral("home");
    return available(route) ? route : QStringLiteral("home");
}
QString RouteController::homeResourceId() const
{
    return m_homeLayout && homeRoute() == m_homeLayout->homeRoute() ? m_homeLayout->homeResourceId()
                                                                    : QString();
}
bool RouteController::navigateHome(bool replaceCurrent)
{
    if (m_closed)
        return false;
    if (m_changing) {
        QMetaObject::invokeMethod(
            this, [this, replaceCurrent] { navigateHome(replaceCurrent); }, Qt::QueuedConnection);
        return true;
    }
    return goTo(homeRoute(), {}, homeResourceId(), replaceCurrent);
}
void RouteController::invalidateAccount()
{
    resetAccount(false);
}
void RouteController::invalidateAccountPreservingRoute()
{
    resetAccount(true);
}
void RouteController::resetAccount(bool preserveRoute)
{
    if (m_closed)
        return;
    if (m_changing) {
        QMetaObject::invokeMethod(
            this, [this, preserveRoute] { resetAccount(preserveRoute); }, Qt::QueuedConnection);
        return;
    }
    QScopedValueRollback<bool> mutation(m_changing, true);
    ++m_accountEpoch;
    m_revisions.clear();
    emit dismissOverlays();
    // Explicit login/account changes resolve the current first shortcut with fresh account
    // data. Only SQL startup restoration retains a navigation made meanwhile.
    const Location location = preserveRoute && m_cursor >= 0
                                  ? m_history[m_cursor]
                                  : Location{homeRoute(), homeResourceId(), {}};
    m_history = {location};
    m_cursor = 0;
    auto *previous = m_current;
    m_current = nullptr;
    if (previous) {
        previous->m_active = false;
        emit previous->activeChanged();
    }
    for (int i = int(m_pages.size()) - 1; i >= 0; --i)
        removePage(i);
    present(location);
}
void RouteController::shutdown()
{
    if (m_closed)
        return;
    if (m_changing) {
        QMetaObject::invokeMethod(this, &RouteController::shutdown, Qt::QueuedConnection);
        return;
    }
    m_closed = true;
    if (m_current) {
        m_current->m_active = false;
        emit m_current->activeChanged();
    }
    m_current = nullptr;
    for (int i = int(m_pages.size()) - 1; i >= 0; --i)
        removePage(i);
    m_history.clear();
    m_cursor = -1;
    m_revisions.clear();
    emit routeChanged();
    emit cacheChanged();
}
} // namespace EchoMusic
