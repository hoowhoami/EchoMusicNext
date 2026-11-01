#include "EchoPluginDialog.h"
#include "EchoPluginUi.h"
#include "EchoPluginContext.h"
#include "EchoPluginTask.h"
#include "ShellController.h"
#include "RouteController.h"
#include <memory>
#include <cmath>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
namespace EchoMusic
{
EchoPluginUi::EchoPluginUi(EchoPluginContext &scope)
    : QObject(&scope), m_navigation(new EchoPluginNavigation(scope)),
      m_parts(new EchoPluginUiParts(scope)), m_styles(new EchoPluginUiStyles(scope)), m_scope(scope)
{
}
EchoPluginCover *EchoPluginUi::cover() const
{
    return m_scope.cover();
}
EchoPluginAppearance *EchoPluginUi::lyricsPage() const
{
    return m_scope.appearance();
}
QVariantMap EchoPluginUi::m_scopeState() const
{
    const auto root = m_scope.activationContext();
    return root != &m_scope ? root->ui()->state() : m_state;
}
EchoPluginOperation *EchoPluginUi::showDialog(const QVariantMap &options)
{
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    for (auto it = options.begin(); it != options.end(); ++it)
        if (!QStringList{"component", "width", "title", "modal", "closeOnMask"}.contains(
                it.key())) {
            op->fail("InvalidArgument", "Unknown dialog option");
            return op;
        }
    if (options.value("component").metaType().id() != QMetaType::QString ||
        (options.contains("title") &&
         options.value("title").metaType().id() != QMetaType::QString) ||
        (options.contains("modal") && options.value("modal").metaType().id() != QMetaType::Bool) ||
        (options.contains("closeOnMask") &&
         options.value("closeOnMask").metaType().id() != QMetaType::Bool)) {
        op->fail("InvalidArgument", "Invalid dialog option type");
        return op;
    }
    const auto path = m_scope.package().resolve(options.value("component").toString());
    bool widthValid = false;
    const double width = options.value("width", 400).toDouble(&widthValid);
    if (path.isEmpty() || !path.endsWith(".qml") || !widthValid || !std::isfinite(width) ||
        width < 160 || width > 2048 || options.value("title").toString().size() > 256) {
        op->fail("InvalidArgument", "Invalid package component or dialog dimensions");
        return op;
    }
    // Count the whole activation, including presentation callers.
    int count = 0;
    for (auto *dialog : m_scope.activationContext()->findChildren<EchoPluginDialog *>())
        count += dialog->active();
    if (count >= 8) {
        op->fail("LimitExceeded", "Too many open plugin dialogs");
        return op;
    }
    QVariantMap definition{{"component", QUrl::fromLocalFile(path)},
                           {"width", width},
                           {"title", options.value("title", m_scope.package().name)},
                           {"modal", options.value("modal", true)},
                           {"closeOnMask", options.value("closeOnMask", true)}};
    auto *dialog = new EchoPluginDialog(m_scope, op);
    op->start();
    // Defer creation so activation, cancellation and signal connections settle first.
    connect(op, &EchoPluginOperation::completed, dialog, [dialog, op] {
        if (op->state() != EchoPluginOperation::Succeeded && !dialog->active())
            dialog->deleteLater();
    });
    QTimer::singleShot(0, op, [dialog = QPointer(dialog), definition, op] {
        if (dialog && !op->finished())
            dialog->open(definition, op);
    });
    return op;
}
bool EchoPluginUi::setState(const QVariantMap &values)
{
    if (m_scope.activationContext() != &m_scope) {
        return m_scope.active() && m_scope.activationContext()->ui()->setState(values);
    }
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
    const auto route = m_scope.registrationKey("page", id);
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
    auto definition = m_scope.engine().newObject();
    definition.setProperty("title", title);
    definition.setProperty("icon", icon);
    definition.setProperty("onClick", callback);
    auto *handle = actionDefinition(playerbar, id, definition);
    return handle ? handle->lease() : nullptr;
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
    auto definition = m_scope.engine().newObject();
    definition.setProperty("name", title);
    return m_scope.tasks()->registerTask(id, definition);
}
} // namespace EchoMusic
