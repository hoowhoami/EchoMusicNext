#include "EchoPluginAction.h"
#include "EchoPluginContext.h"
#include "EchoPluginUi.h"
#include "ShellController.h"
#include <QJSValueIterator>
#include <QTimer>
#include <cmath>
namespace EchoMusic
{
bool EchoPluginAction::valid(const QJSValue &d, bool playerbar, bool patch)
{
    if (!d.isObject() || d.isArray())
        return false;
    QJSValueIterator it(d);
    while (it.hasNext()) {
        it.next();
        const auto key = it.name();
        const auto value = it.value();
        if (patch && QStringList{"order", "defaultPlacement", "badgeDefaultVisible"}.contains(key))
            return false;
        if (value.isUndefined() &&
            QStringList{"tooltip", "badgeTitle", "visible", "disabled", "active", "badge",
                        "trigger", "order", "defaultPlacement", "badgeDefaultVisible"}
                .contains(key))
            continue;
        if (QStringList{"title", "icon", "tooltip", "badgeTitle"}.contains(key)) {
            if (!value.isString() || value.toString().size() > 256 ||
                ((key == "title" || key == "icon") && value.toString().isEmpty()))
                return false;
        } else if (QStringList{"visible", "disabled", "active"}.contains(key)) {
            if (!value.isBool() && !value.isCallable())
                return false;
        } else if (key == "badge") {
            if (!value.isNull() && !value.isString() && !value.isNumber() && !value.isCallable())
                return false;
            if (value.isString() && value.toString().size() > 64)
                return false;
            if (value.isNumber() && !std::isfinite(value.toNumber()))
                return false;
        } else if (key == "badgeDefaultVisible") {
            if (patch || !value.isBool())
                return false;
        } else if (key == "trigger") {
            if (!value.isString() || !QStringList{"click", "hover"}.contains(value.toString()))
                return false;
        } else if (key == "defaultPlacement") {
            if (patch || !value.isString() ||
                !(playerbar ? QStringList{"left", "center", "right", "more"}
                            : QStringList{"toolbar", "more"})
                     .contains(value.toString()))
                return false;
        } else if (key == "order") {
            const double n = value.toNumber();
            if (patch || !value.isNumber() || !std::isfinite(n) || std::abs(n) > 10000 ||
                std::floor(n) != n)
                return false;
        } else if (key == "onClick") {
            if (!value.isCallable())
                return false;
        } else
            return false;
    }
    return patch || (d.property("title").isString() && d.property("icon").isString() &&
                     d.property("onClick").isCallable());
}
EchoPluginAction::EchoPluginAction(EchoPluginContext &scope, EchoPluginRegistration &lease,
                                   ShellActionModel &model, QJSValue definition)
    : QObject(&lease), m_scope(&scope), m_lease(&lease), m_model(&model),
      m_definition(std::move(definition)), m_key(lease.key()), m_revision(lease.revision())
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    scope.engine().newQObject(this);
    connect(&lease, &EchoPluginRegistration::activeChanged, this, [this] {
        if (active())
            return;
        cancel();
        if (m_scope) {
            disconnect(m_scope, nullptr, this, nullptr);
            disconnect(m_scope->ui(), nullptr, this, nullptr);
            disconnect(m_scope->player(), nullptr, this, nullptr);
        }
        m_state["busy"] = false;
        m_state["visible"] = false;
        m_definition = {};
        emit changed();
        setParent(nullptr);
        QQmlEngine::setObjectOwnership(this, QQmlEngine::JavaScriptOwnership);
        if (m_lease)
            m_lease->deleteLater();
    });
    connect(&scope, &EchoPluginContext::sessionInvalidated, &lease,
            &EchoPluginRegistration::dispose);
    connect(&scope, &EchoPluginContext::appearanceChanged, this, &EchoPluginAction::refresh);
    connect(scope.ui(), &EchoPluginUi::stateChanged, this, &EchoPluginAction::refresh);
    connect(scope.player(), &EchoPluginPlayer::changed, this, &EchoPluginAction::refresh);
}
bool EchoPluginAction::active() const
{
    return m_scope && m_scope->active() && m_lease && m_lease->active();
}
bool EchoPluginAction::busy() const
{
    return m_running && !m_running->finished();
}
QJSValue EchoPluginAction::evaluate(const QString &name) const
{
    auto value = m_definition.property(name);
    return value.isCallable() && active() ? m_scope->call(value) : value;
}
bool EchoPluginAction::publish(bool playerbar, const QString &id)
{
    if (!active() || !m_model)
        return false;
    ShellActionModel::Contribution a;
    a.key = m_key;
    a.id = id;
    a.title = m_definition.property("title").toString();
    a.icon = m_definition.property("icon").toString();
    a.order = m_definition.hasProperty("order") ? m_definition.property("order").toInt() : 1000;
    const auto placement = m_definition.property("defaultPlacement").toString();
    a.defaultZone = placement == "toolbar"  ? ShellActionModel::Toolbar
                    : placement == "left"   ? ShellActionModel::Left
                    : placement == "center" ? ShellActionModel::Center
                    : placement == "right"  ? ShellActionModel::Right
                                            : ShellActionModel::More;
    a.badgeDefaultVisible = !m_definition.hasProperty("badgeDefaultVisible") ||
                            m_definition.property("badgeDefaultVisible").toBool();
    a.supportsBadge = m_definition.hasProperty("badge");
    a.enabled = true;
    a.invoke = [this] { invoke(); };
    m_handle = m_model->registerAction(*m_lease, std::move(a));
    Q_UNUSED(playerbar);
    reconcile();
    return m_handle.generation != 0;
}
void EchoPluginAction::refresh()
{
    if (!active() || std::exchange(m_queued, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_queued = false;
        reconcile();
    });
}
void EchoPluginAction::reconcile()
{
    if (!active() || !m_handle.generation || !m_model)
        return;
    // State callbacks may withdraw their own lease and allocate enough JS to
    // collect discarded handles before this native reconciliation unwinds.
    const auto keepAlive = m_scope->engine().newQObject(this);
    const auto visible = evaluate("visible"), disabled = evaluate("disabled"),
               selected = evaluate("active"), badge = evaluate("badge");
    if (!active())
        return;
    // Callback failures fail closed; Promise/object states are not booleans.
    const bool shown = visible.isUndefined() || (visible.isBool() && visible.toBool());
    const bool enabled = disabled.isUndefined() || (disabled.isBool() && !disabled.toBool());
    const auto badgeText =
        (badge.isString() || (badge.isNumber() && std::isfinite(badge.toNumber())))
            ? badge.toString().left(64)
            : QString();
    const auto title = m_definition.property("title").toString(),
               icon = m_definition.property("icon").toString(),
               tooltip = m_definition.property("tooltip").isString()
                             ? m_definition.property("tooltip").toString()
                             : title;
    m_state = {{"title", title},       {"icon", icon},
               {"tooltip", tooltip},   {"visible", shown},
               {"disabled", !enabled}, {"active", selected.isBool() && selected.toBool()},
               {"badge", badgeText},   {"busy", busy()}};
    m_model->setPresentation(m_handle, title, icon);
    m_model->setInteraction(m_handle, tooltip,
                            m_definition.property("badgeTitle").isString()
                                ? m_definition.property("badgeTitle").toString()
                                : QString(),
                            m_definition.property("trigger").toString() == "hover");
    m_model->setState(m_handle, enabled, shown, busy(), badgeText,
                      selected.isBool() && selected.toBool());
    emit changed();
}
bool EchoPluginAction::update(const QJSValue &patch)
{
    if (!active() || !valid(patch, true, true))
        return false;
    // Copy reviewed fields so later writes to an author's object cannot bypass validation.
    QJSValueIterator it(patch);
    while (it.hasNext()) {
        it.next();
        if (it.value().isUndefined())
            m_definition.deleteProperty(it.name());
        else
            m_definition.setProperty(it.name(), it.value());
    }
    refresh();
    return true;
}
EchoPluginOperation *EchoPluginAction::invoke()
{
    if (!m_scope)
        return nullptr;
    if (!active())
        return m_scope->rejected("ScopeClosed", "Action has been withdrawn");
    reconcile();
    if (busy() || !m_state.value("visible").toBool() || m_state.value("disabled").toBool())
        return m_scope->rejected("Busy", "Action is hidden, disabled or already running");
    m_running = m_scope->invokeRegistered(m_definition.property("onClick"), m_lease, {}, true);
    auto *operation = m_running.data();
    connect(operation, &EchoPluginOperation::completed, this, [this, operation] {
        if (m_running != operation)
            return;
        m_running = nullptr;
        reconcile();
    });
    reconcile();
    return operation;
}
void EchoPluginAction::cancel()
{
    if (m_running)
        m_running->cancel();
}
void EchoPluginAction::dispose()
{
    if (m_lease)
        m_lease->dispose();
}
EchoPluginAction *EchoPluginUi::registerTitlebarActionDefinition(const QString &id,
                                                                 const QJSValue &definition)
{
    return actionDefinition(false, id, definition);
}
EchoPluginAction *EchoPluginUi::registerPlayerbarActionDefinition(const QString &id,
                                                                  const QJSValue &definition)
{
    return actionDefinition(true, id, definition);
}
EchoPluginAction *EchoPluginUi::actionDefinition(bool playerbar, const QString &id,
                                                 const QJSValue &definition)
{
    if (!EchoPluginAction::valid(definition, playerbar))
        return nullptr;
    auto *lease = m_scope.registration(playerbar ? "playerbar" : "titlebar", id);
    if (!lease)
        return nullptr;
    auto &model = *(playerbar ? m_scope.host().shell.playerbarActions()
                              : m_scope.host().shell.titlebarActions());
    auto copy = m_scope.engine().newObject();
    QJSValueIterator it(definition);
    while (it.hasNext()) {
        it.next();
        if (!it.value().isUndefined())
            copy.setProperty(it.name(), it.value());
    }
    auto *handle = new EchoPluginAction(m_scope, *lease, model, copy);
    lease->stage([handle, playerbar, id] { return handle->publish(playerbar, id); },
                 [handle, model = QPointer(&model)] {
                     handle->cancel();
                     if (model)
                         model->unregisterAction(handle->m_handle);
                 });
    if (m_scope.committed() && !lease->commit()) {
        lease->dispose();
        return nullptr;
    }
    return handle;
}
} // namespace EchoMusic
