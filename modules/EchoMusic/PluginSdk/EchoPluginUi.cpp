#include "EchoPluginUi.h"
#include "EchoPluginContext.h"
#include "EchoPluginTask.h"
#include "ShellController.h"
#include "RouteController.h"
#include <memory>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
namespace EchoMusic
{
EchoPluginUi::EchoPluginUi(EchoPluginContext &scope)
    : QObject(&scope), m_parts(new EchoPluginUiParts(scope)),
      m_styles(new EchoPluginUiStyles(scope)), m_scope(scope)
{
}
bool EchoPluginUi::setState(const QVariantMap &values)
{
    if (!m_scope.active() ||
        QJsonDocument(QJsonObject::fromVariantMap(values)).toJson(QJsonDocument::Compact).size() >
            1024 * 1024)
        return false;
    const auto json = QJsonObject::fromVariantMap(values).toVariantMap();
    if (json != values)
        return false;
    if (m_state != json) {
        m_state = json;
        emit stateChanged();
    }
    return true;
}
EchoPluginRegistration *EchoPluginUi::registerSurface(const QString &id, const QString &target,
                                                      const QString &component)
{
    if (!QStringList{"playerbar.background", "mini.background", "lyrics.aboveControls"}.contains(
            target))
        return nullptr;
    const auto path = m_scope.package().resolve(component);
    if (path.isEmpty() || !path.endsWith(".qml"))
        return nullptr;
    auto *lease = m_scope.registration("surface", id);
    if (!lease)
        return nullptr;
    auto *model = m_scope.host().shell.pluginSurfaces();
    lease->stage(
        [this, model, lease, target, path] {
            QTimer::singleShot(0, lease, [this, model, lease, target, path] {
                if (m_scope.active() && m_scope.committed() && lease->active())
                    model->insert(lease->key(), target, QUrl::fromLocalFile(path), &m_scope);
            });
            return true;
        },
        [model, lease] { model->remove(lease->key()); });
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return lease;
}
EchoPluginRegistration *EchoPluginUi::registerSettings(const QString &component)
{
    const auto path = m_scope.package().resolve(component);
    if (path.isEmpty() || !path.endsWith(".qml"))
        return nullptr;
    auto *lease = m_scope.registration("settings", "main");
    if (!lease)
        return nullptr;
    lease->stage(
        [this, path] {
            m_settingsComponent = QUrl::fromLocalFile(path);
            emit settingsChanged();
            return true;
        },
        [this] {
            m_settingsComponent = QUrl();
            emit settingsChanged();
        });
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return lease;
}
EchoPluginRegistration *EchoPluginUi::registerPage(const QString &id, const QString &component)
{
    const auto path = m_scope.package().resolve(component);
    if (path.isEmpty() || !path.endsWith(".qml"))
        return nullptr;
    auto *lease = m_scope.registration("page", id);
    if (!lease)
        return nullptr;
    const auto route = lease->key();
    const auto revision = std::make_shared<quint64>(0);
    lease->stage(
        [this, path, route, revision] {
            RouteController::Definition definition;
            definition.name = route;
            definition.path = "/plugins/" + route;
            definition.component = QUrl::fromLocalFile(path);
            definition.pluginOwned = true;
            definition.initialProperties = {{"plugin", QVariant::fromValue(&m_scope)}};
            *revision = m_scope.host().router.registerPluginPage(std::move(definition));
            return *revision != 0;
        },
        [this, route, revision] { m_scope.host().router.unregisterPage(route, *revision); });
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return lease;
}
EchoPluginOperation *EchoPluginUi::navigate(const QString &id, const QVariantMap &query)
{
    const auto route = m_scope.id() + ":page:" + id;
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    op->start();
    if (!m_scope.committed())
        op->fail("NotActive", "Pages are published after activation succeeds");
    else if (m_scope.host().router.navigate(route, query))
        op->succeed();
    else
        op->fail("Unavailable", "Page is not registered or query is invalid");
    return op;
}
EchoPluginRegistration *EchoPluginUi::action(bool playerbar, const QString &id,
                                             const QString &title, const QString &icon,
                                             const QJSValue &callback)
{
    if (title.isEmpty() || title.size() > 256 || !callback.isCallable())
        return nullptr;
    auto *lease = m_scope.registration(playerbar ? "playerbar" : "titlebar", id);
    if (!lease)
        return nullptr;
    auto *model = playerbar ? m_scope.host().shell.playerbarActions()
                            : m_scope.host().shell.titlebarActions();
    auto handle = std::make_shared<ShellActionModel::Handle>();
    lease->stage(
        [this, model, handle, lease, id, title, icon, callback] {
            ShellActionModel::Contribution contribution;
            contribution.key = lease->key();
            contribution.id = id;
            contribution.title = title;
            contribution.icon = icon;
            contribution.enabled = true;
            contribution.invoke = [this, lease, callback] {
                if (m_scope.active() && lease->active())
                    m_scope.call(callback);
            };
            *handle = model->registerAction(*lease, std::move(contribution));
            return handle->generation != 0;
        },
        [model, handle] { model->unregisterAction(*handle); });
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return lease;
}
EchoPluginRegistration *EchoPluginUi::registerTitlebarAction(const QString &id,
                                                             const QString &title,
                                                             const QString &icon,
                                                             const QJSValue &callback)
{
    return action(false, id, title, icon, callback);
}
EchoPluginRegistration *EchoPluginUi::registerPlayerbarAction(const QString &id,
                                                              const QString &title,
                                                              const QString &icon,
                                                              const QJSValue &callback)
{
    return action(true, id, title, icon, callback);
}
EchoPluginTask *EchoPluginUi::createTask(const QString &id, const QString &title)
{
    if (title.isEmpty() || title.size() > 256)
        return nullptr;
    auto *lease = m_scope.registration("task", id);
    if (!lease)
        return nullptr;
    auto *task = new EchoPluginTask(m_scope, *lease, title);
    QQmlEngine::setObjectOwnership(task, QQmlEngine::CppOwnership);
    return task;
}
} // namespace EchoMusic
