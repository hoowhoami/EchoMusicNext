#include "EchoPluginHost.h"
#include "EchoPluginContext.h"
#include "RouteController.h"
#include "ApiClient.h"
#include "AccountService.h"
#include <QJsonDocument>
#include <QJsonObject>
namespace EchoMusic
{
namespace
{
const QStringList routes{"home",          "ranking",           "free-listen", "recommend-songs",
                         "discover-flow", "personal-fm",       "explore",     "recognize",
                         "search",        "song-detail",       "mv-detail",   "artist-detail",
                         "album-detail",  "playlist-detail",   "favorites",   "history",
                         "cloud",         "purchased",         "profile",     "listen-together",
                         "themes",        "plugin-management", "settings"};
}
EchoPluginNavigation::EchoPluginNavigation(EchoPluginContext &s) : QObject(&s), m_scope(s) {}
QStringList EchoPluginNavigation::available() const
{
    QStringList result;
    if (!m_scope.active())
        return result;
    if (m_scope.host().router.available(m_scope.host().router.homeRoute()))
        result.append("home");
    for (const auto &name : routes)
        if (name != "home" && m_scope.host().router.available(name))
            result.append(name);
    return result;
}
QVariantMap EchoPluginNavigation::snapshot() const
{
    const auto &router = m_scope.host().router;
    auto *page = router.current();
    // Plugin pages publish only their route identity, never trusted initialProperties.
    return {{"name", page ? page->name() : QString()},
            {"resourceId", page ? page->resourceId() : QString()},
            {"query", page ? page->query() : QVariantMap{}},
            {"canGoBack", router.canGoBack()},
            {"canGoForward", router.canGoForward()}};
}
EchoPluginOperation *EchoPluginNavigation::read()
{
    return m_scope.permissions()->authorize("navigation", tr("读取当前页面和导航历史状态"),
                                            [this](auto *op) { op->succeed(snapshot()); });
}
EchoPluginOperation *EchoPluginNavigation::navigate(const QVariantMap &target)
{
    const auto name = target.value("name").toString();
    if (!routes.contains(name) || target.size() > 4 ||
        target.value("name").metaType().id() != QMetaType::QString)
        return m_scope.rejected("InvalidArgument", "Unknown host route");
    for (auto it = target.begin(); it != target.end(); ++it)
        if (!QStringList{"name", "resourceId", "query", "replace"}.contains(it.key()))
            return m_scope.rejected("InvalidArgument", "Unknown navigation field");
    if ((target.contains("replace") &&
         target.value("replace").metaType().id() != QMetaType::Bool) ||
        (target.contains("resourceId") &&
         target.value("resourceId").metaType().id() != QMetaType::QString) ||
        (target.contains("query") &&
         target.value("query").metaType().id() != QMetaType::QVariantMap))
        return m_scope.rejected("InvalidArgument", "Invalid navigation fields");
    const auto query = target.value("query").toMap();
    const auto resource = target.value("resourceId").toString();
    if (resource.size() > 512 || query.size() > 32 ||
        (name == "search" && query.value("q").toString().trimmed().isEmpty()))
        return m_scope.rejected("InvalidArgument", "Invalid resource or search query");
    for (auto it = query.begin(); it != query.end(); ++it)
        if (it.key().size() > 128 || it.value().metaType().id() != QMetaType::QString ||
            it.value().toString().size() > 4096)
            return m_scope.rejected("InvalidArgument",
                                    "Navigation query must contain bounded strings");
    return m_scope.permissions()->authorize(
        "navigation", tr("打开宿主页面"),
        [this, name, query, resource, replace = target.value("replace").toBool()](auto *op) {
            if (!m_scope.committed()) {
                op->fail("NotActive", "Navigation requires a committed activation");
                return;
            }
            auto &router = m_scope.host().router;
            const bool ok = name == "home" ? router.navigateHome(replace)
                            : replace      ? router.replace(name, query, resource)
                                           : router.navigate(name, query, resource);
            if (ok)
                op->succeed(snapshot());
            else
                op->fail("Unavailable", "Host page or resource is unavailable");
        });
}
EchoPluginOperation *EchoPluginNavigation::history(const QString &command)
{
    if (!QStringList{"back", "forward", "refresh"}.contains(command))
        return m_scope.rejected("InvalidArgument", "Expected back, forward or refresh");
    return m_scope.permissions()->authorize(
        "navigation", tr("操作页面导航历史"), [this, command](auto *op) {
            auto &router = m_scope.host().router;
            if (!m_scope.committed()) {
                op->fail("NotActive", "Navigation requires a committed activation");
                return;
            }
            if ((command == "back" && !router.canGoBack()) ||
                (command == "forward" && !router.canGoForward())) {
                op->fail("Unavailable", "No history entry");
                return;
            }
            if (command == "back")
                router.back();
            else if (command == "forward")
                router.forward();
            else
                router.refresh();
            op->succeed(snapshot());
        });
}
EchoPluginOperation *EchoPluginNavigation::observe(const QString &id, const QJSValue &callback)
{
    if (!callback.isCallable())
        return m_scope.rejected("InvalidArgument", "Expected observer callback");
    return m_scope.permissions()->authorize(
        "navigation", tr("订阅宿主页面变化"), [this, id, callback](auto *op) {
            auto *lease = m_scope.registration("hostNavigation", id);
            if (!lease) {
                op->fail("InvalidArgument", "Invalid observer ID");
                return;
            }
            connect(lease, &EchoPluginRegistration::activeChanged, lease, [lease] {
                if (!lease->active()) {
                    lease->setParent(nullptr);
                    QQmlEngine::setObjectOwnership(lease, QQmlEngine::JavaScriptOwnership);
                }
            });
            auto connections = std::make_shared<QList<QMetaObject::Connection>>();
            lease->stage(
                [this, lease, callback, connections] {
                    connections->append(
                        connect(&m_scope.host().router, &RouteController::routeChanged, lease,
                                [this, lease, callback] {
                                    if (m_scope.active() && lease->active())
                                        m_scope.call(callback,
                                                     {m_scope.engine().toScriptValue(snapshot())});
                                }));
                    connections->append(connect(&m_scope, &EchoPluginContext::sessionInvalidated,
                                                lease, &EchoPluginRegistration::dispose));
                    connections->append(connect(
                        m_scope.host().permissions, &PluginPermissionStore::revoked, lease,
                        [this, lease](const auto &installation, const auto &cap) {
                            if (installation == m_scope.installationId() && cap == "navigation")
                                lease->dispose();
                        }));
                    return true;
                },
                [connections] {
                    for (auto c : *connections)
                        QObject::disconnect(c);
                    connections->clear();
                });
            if (m_scope.committed() && !lease->commit()) {
                lease->dispose();
                op->fail("Unavailable", "Observer unavailable");
                return;
            }
            op->succeed(QVariant::fromValue(lease));
        });
}
} // namespace EchoMusic
