#include "EchoPluginLyricEffects.h"
#include "EchoPluginContext.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiPartRegistration.h"
#include "EchoLyricEffectRegistry.h"
#include "EchoPluginUiParts.h"
#include "EchoPluginUiStyles.h"
#include <QJSValueIterator>
#include <QTimer>
#include <cmath>
namespace EchoMusic
{
EchoPluginLyricEffects::EchoPluginLyricEffects(EchoPluginContext &scope)
    : QObject(&scope), m_scope(scope)
{
}
QVariant EchoPluginLyricEffects::snapshot(EchoUiPartRef *part) const
{
    if (!part || !part->belongsTo(&m_scope) || !part->valid() ||
        !part->role().startsWith("lyrics."))
        return QVariant::fromValue(nullptr);
    auto state = part->state();
    state["role"] = part->role();
    return state;
}
EchoPluginRegistration *EchoPluginLyricEffects::registerEffect(const QString &id,
                                                               const QJSValue &definition)
{
    if (m_scope.activationContext() != &m_scope)
        return nullptr;
    if (id.size() > 80)
        return nullptr;
    if (!m_scope.package().capabilities.contains("lyricEffects") || !definition.isObject() ||
        definition.isArray())
        return nullptr;
    const QStringList allowed{"scopes", "order", "style", "component", "autoScroll"};
    QJSValueIterator it(definition);
    while (it.hasNext()) {
        it.next();
        if (!allowed.contains(it.name()))
            return nullptr;
    }
    QStringList scopes{"page"};
    if (definition.hasProperty("scopes"))
        scopes = definition.property("scopes").toVariant().toStringList();
    if (scopes.isEmpty() || scopes.size() > 2)
        return nullptr;
    for (const auto &s : scopes)
        if (s != "page" && s != "desktop")
            return nullptr;
    const double order =
        definition.property("order").isUndefined() ? 0 : definition.property("order").toNumber();
    if (!std::isfinite(order) || std::abs(order) > 10000)
        return nullptr;
    auto style = definition.property("style").toVariant().toMap();
    QString error;
    if (!EchoUiPartRegistry::normalize(style, error))
        return nullptr;
    auto component = definition.property("component").isUndefined()
                         ? QString()
                         : definition.property("component").toString();
    if (!component.isEmpty() &&
        (m_scope.package().resolve(component).isEmpty() || !component.endsWith(".qml")))
        return nullptr;
    const auto scroll = definition.property("autoScroll");
    if (!scroll.isUndefined() && !scroll.isCallable())
        return nullptr;
    if (style.isEmpty() && component.isEmpty() && !scroll.isCallable())
        return nullptr;
    auto *parts = EchoUiPartRegistry::forEngine(m_scope.engine());
    auto *scrolls = EchoLyricEffectRegistry::forEngine(m_scope.engine());
    auto *lease = m_scope.registration("lyricEffect", id);
    if (!lease || !parts || !scrolls)
        return nullptr;
    connect(lease, &EchoPluginRegistration::activeChanged, lease, [lease] {
        if (!lease->active()) {
            lease->setParent(nullptr);
            QQmlEngine::setObjectOwnership(lease, QQmlEngine::JavaScriptOwnership);
        }
    });
    struct State
    {
        QList<QPointer<EchoUiStyleRule>> styles;
        QHash<QString, QPointer<EchoUiPartMount>> mounts;
        quint64 scroll = 0, sequence = 0;
        bool closing = false;
    };
    auto state = std::make_shared<State>();
    auto guard = QPointer(lease);
    const auto attach = [this, guard, state, component, scopes, order,
                         id](EchoUiPartRegistration *part) {
        if (!guard || !guard->active() || state->closing || !part ||
            part->role() != "lyrics.viewport" || component.isEmpty())
            return;
        const auto surface = part->surface();
        if (!scopes.contains(surface == "playback" ? "page" : surface) ||
            state->mounts.contains(part->identity()))
            return;
        auto *ref = m_scope.ui()->parts()->query({{"id", part->identity()}});
        if (!ref)
            return;
        auto *mount =
            m_scope.ui()->parts()->mount("effect-" + id + "-" + QString::number(++state->sequence),
                                         ref, "foreground", component, int(order));
        if (mount)
            state->mounts.insert(part->identity(), mount);
    };
    const auto cleanup = [lease, state, scrolls] {
        state->closing = true;
        scrolls->remove(lease->key(), state->scroll);
        for (auto rule : state->styles)
            if (rule)
                rule->dispose();
        for (auto mount : state->mounts)
            if (mount)
                mount->dispose();
        state->styles.clear();
        state->mounts.clear();
    };
    lease->stage(
        [this, lease, guard, state, parts, scrolls, scroll, scopes, style, order, id, attach,
         cleanup] {
            for (const auto &scope : scopes)
                if (!style.isEmpty()) {
                    auto *rule = m_scope.ui()->styles()->registerRule(
                        "effect-" + id + "-" + scope,
                        {{"role", "lyrics.line"},
                         {"surface", scope == "page" ? "playback" : "desktop"}},
                        style, int(order));
                    if (!rule || !rule->active()) {
                        cleanup();
                        return false;
                    }
                    state->styles.append(rule);
                }
            connect(parts, &EchoUiPartRegistry::added, lease, attach);
            connect(parts, &EchoUiPartRegistry::removed, lease, [state](const QString &id) {
                if (auto mount = state->mounts.take(id))
                    mount->dispose();
            });
            for (auto *p : parts->query({{"role", "lyrics.viewport"}}))
                attach(p);
            if (scroll.isCallable()) {
                EchoLyricEffectRegistry::ScrollHandler h;
                h.key = lease->key();
                h.owner = lease;
                h.scopes = scopes;
                h.order = int(order);
                h.invoke = [this, guard, scroll](const QString &host, const QVariantMap &request) {
                    if (!guard || !guard->active() || !m_scope.active())
                        return false;
                    auto *ref = m_scope.ui()->parts()->query({{"id", host}});
                    if (!ref || !ref->visible())
                        return false;
                    auto args = m_scope.engine().newObject();
                    args.setProperty("host", m_scope.engine().newQObject(ref));
                    args.setProperty("request", m_scope.engine().toScriptValue(request));
                    args.setProperty("snapshot", m_scope.engine().toScriptValue(ref->state()));
                    auto result = m_scope.call(scroll, {args});
                    return !result.isError() && result.isBool() && result.toBool();
                };
                state->scroll = scrolls->put(h);
                if (!state->scroll) {
                    cleanup();
                    return false;
                }
            }
            return true;
        },
        cleanup);
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return lease;
}
} // namespace EchoMusic
