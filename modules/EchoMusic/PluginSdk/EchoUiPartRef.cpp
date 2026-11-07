#include "EchoUiPartRef.h"
#include "EchoPluginContext.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiPresentation.h"
#include "EchoUiValue.h"
#include <QJsonDocument>
#include <QColor>
#include <QMetaProperty>
#include <QMetaMethod>
#include <algorithm>
#include <cmath>
namespace EchoMusic
{
EchoUiPartRef::EchoUiPartRef(EchoPluginContext &scope, EchoUiPartRegistration &part,
                             QObject *parent)
    : QObject(parent), m_scope(&scope), m_part(&part), m_id(part.identity()), m_role(part.role()),
      m_key(part.key()), m_surface(part.surface()), m_window(part.windowId()),
      m_properties(part.properties()), m_actions(part.actions()),
      m_replaceable(part.replaceable() || part.canCompose())
{
    m_owner = part.owner();
    m_epoch = scope.accountEpoch();
    connect(&part, &EchoUiPartRegistration::changed, this, &EchoUiPartRef::notify);
    connect(&part, &QObject::destroyed, this, &EchoUiPartRef::notify);
    connect(&part, &EchoUiPartRegistration::eventOccurred, this,
            [this](const auto &event, const auto &payload) {
                if (valid())
                    emit eventOccurred(event, payload);
            });
    connect(&scope, &EchoPluginContext::activeChanged, this, &EchoUiPartRef::notify);
    connect(&scope, &QObject::destroyed, this, &EchoUiPartRef::notify);
    m_registry = EchoUiPartRegistry::forEngine(scope.engine());
    if (m_registry)
        m_registry->retainStructure(this);
    if (m_registry)
        connect(m_registry, &EchoUiPartRegistry::removed, this, [this](const auto &id) {
            if (id == m_id)
                notify();
        });
    if (m_registry)
        connect(m_registry, &EchoUiPartRegistry::structureChanged, this, &EchoUiPartRef::notify);
    if (part.target()) {
        m_presentation = new EchoUiPresentation(part.target(), this);
        connect(m_presentation, &EchoUiPresentation::changed, this, &EchoUiPartRef::notify);
    }
    watchAncestors();
    part.acquireState();
}
EchoUiPartRef::~EchoUiPartRef()
{
    if (m_registry)
        m_registry->releaseStructure(this);
    if (m_part) {
        disconnect(m_part, nullptr, this, nullptr);
        m_part->releaseState();
    }
}
bool EchoUiPartRef::valid() const
{
    return m_scope && m_scope->active() && m_scope->accountEpoch() == m_epoch && m_part &&
           m_part->identity() == m_id;
}
bool EchoUiPartRef::belongsTo(const EchoPluginContext *scope) const
{
    return m_scope == scope;
}
EchoUiPartRegistration *EchoUiPartRef::registration() const
{
    return valid() ? m_part.data() : nullptr;
}
bool EchoUiPartRef::visible() const
{
    return valid() && m_part->presented();
}
QRectF EchoUiPartRef::geometry() const
{
    return valid() ? m_part->geometry() : QRectF();
}
QVariantMap EchoUiPartRef::mapPoint(const QVariantMap &point, const QString &space) const
{
    if (!valid() || !m_part->target() || (space != "local" && space != "window") ||
        point.size() != 2 || !point.contains("x") || !point.contains("y"))
        return {{"valid", false}};
    const auto isNumber = [](const QVariant &value) {
        const auto type = value.metaType().id();
        return (type == QMetaType::Int || type == QMetaType::Double ||
                type == QMetaType::LongLong) &&
               std::isfinite(value.toDouble());
    };
    if (!isNumber(point.value("x")) || !isNumber(point.value("y")))
        return {{"valid", false}};
    const QPointF input(point.value("x").toDouble(), point.value("y").toDouble());
    const auto mapped = space == "window" ? m_part->target()->mapToScene(input)
                                          : m_part->target()->mapFromScene(input);
    if (!std::isfinite(mapped.x()) || !std::isfinite(mapped.y()))
        return {{"valid", false}};
    return {{"valid", true}, {"x", mapped.x()}, {"y", mapped.y()}, {"window", window()}};
}
QVariantMap EchoUiPartRef::stateSchema() const
{
    QVariantMap schema;
    const auto values = state();
    for (auto it = values.begin(); it != values.end(); ++it) {
        const auto type = it.value().metaType().id();
        const QString name = type == QMetaType::Bool ? "boolean"
                             : type == QMetaType::Int || type == QMetaType::LongLong ||
                                     type == QMetaType::UInt || type == QMetaType::ULongLong
                                 ? "integer"
                             : type == QMetaType::Double || type == QMetaType::Float ? "number"
                             : type == QMetaType::QString                            ? "string"
                             : type == QMetaType::QVariantList || type == QMetaType::QStringList
                                 ? "array"
                             : type == QMetaType::QVariantMap ? "object"
                                                              : "null";
        schema[it.key()] = QVariantMap{{"type", name}, {"readOnly", true}};
    }
    return schema;
}
QVariantMap EchoUiPartRef::actionSchema() const
{
    QVariantMap schema;
    if (!valid())
        return schema;
    for (const auto &action : actions()) {
        const bool declared = m_part->declaredActions().contains(action);
        QVariantMap properties;
        QStringList required;
        if (!declared && action == "setValue") {
            properties["value"] = QVariantMap{{"type", "number"}};
            required = QStringList{"value"};
        } else if (!declared && action == "selectIndex") {
            properties["index"] = QVariantMap{{"type", "integer"}};
            required = QStringList{"index"};
        } else if (!declared && action == "scrollTo") {
            properties["x"] = QVariantMap{{"type", "number"}};
            properties["y"] = QVariantMap{{"type", "number"}};
        } else if (!declared && action == "selectText") {
            properties["start"] = QVariantMap{{"type", "integer"}};
            properties["end"] = QVariantMap{{"type", "integer"}};
            required = QStringList{"start", "end"};
        } else if (!declared && action == "insertText") {
            properties["text"] = QVariantMap{{"type", "string"}, {"maxLength", 4096}};
            required = QStringList{"text"};
        }
        const bool native =
            !declared &&
            QStringList{"activate",    "focus",    "focusNext",  "focusPrevious", "setValue",
                        "selectIndex", "scrollTo", "selectText", "selectAll",     "insertText"}
                .contains(action);
        schema[action] = QVariantMap{{"type", "object"},
                                     {"properties", properties},
                                     {"required", required},
                                     {"additionalProperties", !native}};
    }
    return schema;
}
QVariantMap EchoUiPartRef::state() const
{
    qsizetype budget = 65536;
    return valid() ? EchoUiValue::json(m_part->state(), budget).toMap() : QVariantMap();
}
QVariantMap EchoUiPartRef::visualSources() const
{
    return valid() && m_registry ? m_registry->styleSources(m_id) : QVariantMap();
}
QVariantMap EchoUiPartRef::visuals() const
{
    if (!valid())
        return {};
    auto values = m_part->baseValues();
    const auto overrides = m_part->overrides();
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        values.insert(it.key(), it.value());
    return values;
}
namespace
{
const QStringList paletteFields{"text", "tone",   "secondaryText", "elevated",
                                "card", "border", "accent",        "accentText"};
QObject *paletteFor(QQuickItem *target)
{
    for (auto *item = target; item; item = item->parentItem()) {
        auto *palette = item->property("colors").value<QObject *>();
        if (palette && std::all_of(paletteFields.cbegin(), paletteFields.cend(),
                                   [palette](const QString &name) {
                                       return palette->property(name.toUtf8()).canConvert<QColor>();
                                   }))
            return palette;
    }
    return nullptr;
}
} // namespace
QVariantMap EchoUiPartRef::palette() const
{
    QVariantMap result;
    if (auto *colors = valid() ? paletteFor(m_part->target()) : nullptr)
        for (const auto &name : paletteFields)
            result.insert(name, colors->property(name.toUtf8()));
    return result;
}
void EchoUiPartRef::paletteChanged()
{
    watchAncestors();
    notify();
}
void EchoUiPartRef::notify()
{
    if (!valid() && !std::exchange(m_invalidated, true)) {
        if (m_registry)
            m_registry->releaseStructure(this);
        for (const auto &connection : m_ancestors)
            disconnect(connection);
        m_ancestors.clear();
        delete m_presentation;
        m_presentation = nullptr;
        if (m_part)
            disconnect(m_part, nullptr, this, nullptr);
        if (m_scope)
            disconnect(m_scope, nullptr, this, nullptr);
        if (m_registry)
            disconnect(m_registry, nullptr, this, nullptr);
        if (m_part)
            m_part->releaseState();
        m_part = nullptr;
        emit invalidated();
    }
    emit changed();
}
void EchoUiPartRef::watchAncestors()
{
    for (const auto &c : m_ancestors)
        disconnect(c);
    m_ancestors.clear();
    if (!valid())
        return;
    for (auto *item = m_part->target(); item; item = item->parentItem()) {
        const auto update = [this] { notify(); };
        m_ancestors << connect(item, &QQuickItem::widthChanged, this, update)
                    << connect(item, &QQuickItem::visibleChanged, this, update)
                    << connect(item, &QQuickItem::heightChanged, this, update)
                    << connect(item, &QQuickItem::clipChanged, this, update)
                    << connect(item, &QQuickItem::xChanged, this, update)
                    << connect(item, &QQuickItem::yChanged, this, update)
                    << connect(item, &QQuickItem::scaleChanged, this, update)
                    << connect(item, &QQuickItem::rotationChanged, this, update)
                    << connect(item, &QQuickItem::parentChanged, this, [this] {
                           watchAncestors();
                           notify();
                       });
        const auto index = item->metaObject()->indexOfProperty("colors");
        if (index >= 0) {
            const auto property = item->metaObject()->property(index);
            if (property.hasNotifySignal())
                m_ancestors << connect(
                    item, property.notifySignal(), this,
                    metaObject()->method(metaObject()->indexOfSlot("paletteChanged()")));
        }
    }
    if (auto *colors = paletteFor(m_part->target()))
        for (const auto &name : paletteFields) {
            const auto index = colors->metaObject()->indexOfProperty(name.toUtf8());
            if (index >= 0) {
                const auto property = colors->metaObject()->property(index);
                if (property.hasNotifySignal())
                    m_ancestors << connect(
                        colors, property.notifySignal(), this,
                        metaObject()->method(metaObject()->indexOfSignal("changed()")));
            }
        }
}
EchoPluginOperation *EchoUiPartRef::invoke(const QString &action, const QVariantMap &arguments)
{
    if (!m_scope)
        return nullptr;
    if (!valid() || !m_actions.contains(action))
        return m_scope->rejected("Unavailable", "UI part/action is no longer available");
    if (QJsonDocument::fromVariant(arguments).toJson(QJsonDocument::Compact).size() > 65536)
        return m_scope->rejected("InvalidArguments", "UI part action exceeds 64 KiB");
    // Dispatch through the host's declared action, keeping normal enabled and
    // session checks. No private QObject or slot name is returned to plugins.
    auto *op = m_scope->operation();
    if (op->finished())
        return op;
    if (!m_part->invoke(action, arguments))
        op->fail("Unavailable", "UI part action is disabled or hidden");
    else
        op->succeed();
    return op;
}
} // namespace EchoMusic

namespace EchoMusic
{
QString EchoUiPartRef::parentId() const
{
    return valid() ? m_part->parentId() : QString();
}
QString EchoUiPartRef::address() const
{
    return valid() ? m_part->address() : QString();
}
QString EchoUiPartRef::scope() const
{
    return valid() ? m_part->scope() : QString();
}
QString EchoUiPartRef::resourceKey() const
{
    return valid() ? m_part->resourceKey() : QString();
}
bool EchoUiPartRef::persistent() const
{
    return valid() && m_part->persistent();
}
QString EchoUiPartRef::surface() const
{
    return valid() ? m_part->surface() : m_surface;
}
QString EchoUiPartRef::layoutKind() const
{
    return valid() ? m_part->layoutKind() : QString();
}
QString EchoUiPartRef::layoutEngine() const
{
    return valid() ? m_part->layoutEngine() : QString();
}
QStringList EchoUiPartRef::insertionPositions() const
{
    return valid() ? m_part->insertionPositions() : QStringList();
}
QStringList EchoUiPartRef::properties() const
{
    return valid() ? m_part->properties() : QStringList();
}
bool EchoUiPartRef::automatic() const
{
    return valid() && m_part->automatic();
}
bool EchoUiPartRef::composable() const
{
    return valid() && m_part->canCompose();
}
} // namespace EchoMusic
