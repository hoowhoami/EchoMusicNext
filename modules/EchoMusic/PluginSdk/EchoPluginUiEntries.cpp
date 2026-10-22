#include "EchoPluginUi.h"
#include "EchoPluginContext.h"
#include "SidebarLayoutModel.h"
#include "RouteController.h"
#include "EchoSongMenuRegistry.h"
#include "PlaybackService.h"
#include "ApiClient.h"
#include "AccountService.h"
#include <QJSValueIterator>
#include <cmath>
namespace EchoMusic
{
namespace
{
void releaseInactive(EchoPluginRegistration *lease)
{
    QObject::connect(lease, &EchoPluginRegistration::activeChanged, lease, [lease] {
        if (!lease->active()) {
            lease->setParent(nullptr);
            QQmlEngine::setObjectOwnership(lease, QQmlEngine::JavaScriptOwnership);
        }
    });
}
bool valid(const QJSValue &v, const QStringList &allowed)
{
    if (!v.isObject() || v.isArray())
        return false;
    QJSValueIterator it(v);
    while (it.hasNext()) {
        it.next();
        if (!allowed.contains(it.name()))
            return false;
    }
    return true;
}
bool condition(EchoPluginContext &scope, const QJSValue &value, const QVariantMap &context = {})
{
    if (value.isUndefined())
        return true;
    if (value.isBool())
        return value.toBool();
    if (!value.isCallable() || !scope.active())
        return false;
    auto result = scope.call(value, {scope.engine().toScriptValue(context)});
    return !result.isError() && result.isBool() && result.toBool();
}
} // namespace
EchoPluginRegistration *EchoPluginUi::registerSidebarItem(const QString &id,
                                                          const QJSValue &definition)
{
    return sidebarEntry(id, definition, false);
}
EchoPluginRegistration *EchoPluginUi::registerShortcutCard(const QString &id,
                                                           const QJSValue &definition)
{
    return sidebarEntry(id, definition, true);
}
EchoPluginRegistration *EchoPluginUi::sidebarEntry(const QString &id, const QJSValue &definition,
                                                   bool shortcut)
{
    if (m_scope.activationContext() != &m_scope)
        return nullptr;
    auto *model = m_scope.host().sidebar;
    if (!model || !valid(definition, {"title", "icon", "pageId", "section", "sectionTitle",
                                      "sectionOrder", "collapsible", "order", "before", "after",
                                      "visible", "disabled", "onClick"}))
        return nullptr;
    const auto title = definition.property("title");
    if (!title.isString() || title.toString().isEmpty() || title.toString().size() > 256)
        return nullptr;
    auto values = QVariantMap{{"title", title.toString()}};
    for (const auto &k : {"icon", "pageId", "section", "sectionTitle", "before", "after"})
        if (definition.hasProperty(k)) {
            const auto v = definition.property(k);
            if (!v.isString() || v.toString().size() > 256)
                return nullptr;
            values[k] = v.toString();
        }
    for (const auto &k : {"order", "sectionOrder"})
        if (definition.hasProperty(k)) {
            auto n = definition.property(k).toNumber();
            if (!std::isfinite(n) || std::abs(n) > 10000)
                return nullptr;
            values[k] = n;
        }
    for (const auto &k : {"visible", "disabled"}) {
        const auto v = definition.property(k);
        if (!v.isUndefined() && !v.isBool() && !v.isCallable())
            return nullptr;
    }
    if (definition.hasProperty("collapsible")) {
        if (!definition.property("collapsible").isBool())
            return nullptr;
        values["collapsible"] = definition.property("collapsible").toBool();
    }
    auto cb = definition.property("onClick");
    const auto pageId = values.take("pageId").toString();
    if (!cb.isCallable() && pageId.isEmpty())
        return nullptr;
    values["route"] = pageId.isEmpty() ? QString() : m_scope.registrationKey("page", pageId);
    if (values.value("section") == "playlists")
        return nullptr;
    auto *lease = m_scope.registration(shortcut ? "shortcutCard" : "sidebarItem", id);
    if (!lease)
        return nullptr;
    const auto generation = std::make_shared<quint64>(0);
    releaseInactive(lease);
    auto guard = QPointer(lease);
    SidebarLayoutModel::PluginEntry entry;
    entry.key = lease->key();
    entry.owner = lease;
    entry.shortcut = shortcut;
    entry.values = values;
    entry.visible = [this, guard, v = definition.property("visible")] {
        return guard && guard->active() && m_scope.active() && condition(m_scope, v);
    };
    entry.enabled = [this, guard, v = definition.property("disabled")] {
        if (!guard || !guard->active() || !m_scope.active())
            return false;
        if (v.isUndefined())
            return true;
        if (v.isBool())
            return !v.toBool();
        auto result = m_scope.call(v);
        return !result.isError() && result.isBool() && !result.toBool();
    };
    entry.invoke = [this, guard, cb, route = values.value("route").toString()] {
        if (!guard || !guard->active() || !m_scope.active())
            return;
        if (cb.isCallable())
            m_scope.invokeRegistered(cb, guard, {});
        else
            m_scope.host().router.navigate(route);
    };
    lease->stage(
        [model, entry, generation] {
            *generation = model->registerPluginEntry(entry);
            return *generation != 0;
        },
        [model, lease, generation] { model->unregisterPluginEntry(lease->key(), *generation); });
    connect(&m_scope.host().api, &ApiClient::sessionChanged, lease,
            [model] { model->refreshPlugins(); });
    connect(&m_scope.host().account, &AccountService::sessionAdopted, lease,
            [model] { model->refreshPlugins(); });
    connect(this, &EchoPluginUi::stateChanged, lease, [model] { model->refreshPlugins(); });
    connect(m_scope.player(), &EchoPluginPlayer::changed, lease,
            [model] { model->refreshPlugins(); });
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return lease;
}
EchoPluginRegistration *EchoPluginUi::registerSongMenuItem(const QString &id,
                                                           const QJSValue &definition)
{
    if (m_scope.activationContext() != &m_scope)
        return nullptr;
    if (!valid(definition, {"label", "order", "danger", "visible", "enabled", "onSelect"}))
        return nullptr;
    const auto label = definition.property("label"), cb = definition.property("onSelect");
    if (!label.isString() || label.toString().isEmpty() || label.toString().size() > 256 ||
        !cb.isCallable())
        return nullptr;
    const auto order =
        definition.property("order").isUndefined() ? 0 : definition.property("order").toNumber();
    if (!std::isfinite(order) || std::abs(order) > 10000)
        return nullptr;
    if (definition.hasProperty("danger") && !definition.property("danger").isBool())
        return nullptr;
    for (const auto &k : {"visible", "enabled"}) {
        auto v = definition.property(k);
        if (!v.isUndefined() && !v.isBool() && !v.isCallable())
            return nullptr;
    }
    auto *registry = EchoSongMenuRegistry::forEngine(m_scope.engine());
    if (!registry)
        return nullptr;
    auto *lease = m_scope.registration("songMenu", id);
    if (!lease)
        return nullptr;
    auto generation = std::make_shared<quint64>(0);
    releaseInactive(lease);
    auto guard = QPointer(lease);
    EchoSongMenuRegistry::Entry e;
    e.key = lease->key();
    e.label = label.toString();
    e.order = int(order);
    e.danger = definition.property("danger").toBool();
    e.owner = lease;
    e.epoch = [this] { return QString::number(m_scope.accountEpoch()); };
    e.visible = [this, guard, v = definition.property("visible")](const QVariantMap &c) {
        return guard && guard->active() && m_scope.active() && condition(m_scope, v, c);
    };
    e.enabled = [this, guard, v = definition.property("enabled")](const QVariantMap &c) {
        return guard && guard->active() && m_scope.active() && condition(m_scope, v, c);
    };
    e.invoke = [this, guard, cb](const QVariantMap &c) {
        if (guard && guard->active() && m_scope.active())
            m_scope.invokeRegistered(cb, guard, c);
    };
    lease->stage(
        [registry, e, generation] {
            *generation = registry->put(e);
            return *generation != 0;
        },
        [registry, lease, generation] { registry->remove(lease->key(), *generation); });
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return lease;
}
} // namespace EchoMusic
