#include "EchoUiPartRegistration.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiLayout.h"
#include "EchoUiScope.h"
#include "EchoUiPresentation.h"
#include "EchoUiNativeContract.h"
#include "EchoUiControlContentFactory.h"
#include <QMetaProperty>
#include <QTimer>
#include <QQuickWindow>
#include <QQmlEngine>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlProperty>
#include <QMetaMethod>
#include <QtQuick/private/qquickitem_p.h>
#include <QtQml/private/qqmlanybinding_p.h>
#include <QtQml/private/qqmlmetatype_p.h>
#include <algorithm>
namespace EchoMusic
{
namespace
{
QObject *nativeLayout(QQuickItem *item)
{
    if (!item || EchoUiLayout::engine(item->parentItem()) != "layout")
        return nullptr;
    const auto type = QQmlMetaType::qmlType(QStringLiteral("QtQuick.Layouts/Layout"),
                                            QTypeRevision::fromVersion(6, 0));
    const auto function = type.isValid() ? type.attachedPropertiesFunction(nullptr) : nullptr;
    return function ? qmlAttachedPropertiesObject(item, function, true) : nullptr;
}
} // namespace
EchoUiPartRegistration::EchoUiPartRegistration(QObject *parent) : QObject(parent)
{
    connect(this, &EchoUiPartRegistration::observedChanged, this, [this] {
        if (m_complete)
            connectTarget();
    });
}
void EchoUiPartRegistration::initializeAutomatic(EchoUiPartRegistry &registry, QQuickItem *target,
                                                 const QString &role, const QString &key)
{
    Q_ASSERT(!m_complete);
    m_automatic = true;
    m_registry = &registry;
    m_target = target;
    m_role = role;
    m_key = key;
    m_complete = true;
    connectTarget();
    refresh();
}
QStringList EchoUiPartRegistration::actions() const
{
    auto result = m_actions;
    result += EchoUiNativeContract::actions(m_target);
    result.removeDuplicates();
    return result;
}
QVariantMap EchoUiPartRegistration::state() const
{
    auto result = EchoUiNativeContract::state(m_target);
    for (auto it = m_state.begin(); it != m_state.end(); ++it)
        result.insert(it.key(), it.value());
    return result;
}
void EchoUiPartRegistration::nativeStateChanged()
{
    emit stateChanged();
    emit changed();
    if (m_registry)
        m_registry->nativeStateChanged(this);
}
void EchoUiPartRegistration::nativeActivated()
{
    publish("activated");
}
EchoUiPartRegistration::~EchoUiPartRegistration()
{
    if (m_registry)
        m_registry->detach(this);
}
void EchoUiPartRegistration::setState(const QVariantMap &v)
{
    if (m_state == v)
        return;
    m_state = v;
    emit stateChanged();
    if (m_registry)
        m_registry->nativeStateChanged(this);
    if (std::exchange(m_stateNoticePending, true))
        return;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_stateNoticePending = false;
            emit changed();
        },
        Qt::QueuedConnection);
}
void EchoUiPartRegistration::componentComplete()
{
    m_complete = true;
    if (m_nativeFactory)
        m_nativeFactory->setOwnerTag(owner());
    if (auto *engine = qmlEngine(this))
        m_registry = EchoUiPartRegistry::forEngine(*engine);
    connectTarget();
    refresh();
}
void EchoUiPartRegistration::refresh()
{
    if (!m_complete || !m_registry || std::exchange(m_refreshing, true))
        return;
    const QPointer<EchoUiPartRegistration> self(this);
    if (m_nativeFactory)
        m_nativeFactory->setOwnerTag(owner());
    m_registry->detach(this);
    if (!self)
        return;
    m_registry->attach(this);
    if (!self)
        return;
    m_refreshing = false;
    emit changed();
}
void EchoUiPartRegistration::setTarget(QQuickItem *target)
{
    if (m_target == target)
        return;
    if (m_registry)
        m_registry->detach(this);
    m_target = target;
    if (m_complete)
        connectTarget();
    emit declarationChanged();
    refresh();
}
#define ECHO_PART_SETTER(Name, Type, Member)                                                       \
    void EchoUiPartRegistration::set##Name(Type value)                                             \
    {                                                                                              \
        if (Member == value)                                                                       \
            return;                                                                                \
        Member = value;                                                                            \
        emit declarationChanged();                                                                 \
        refresh();                                                                                 \
    }
ECHO_PART_SETTER(Owner, const QString &, m_owner)
ECHO_PART_SETTER(Role, const QString &, m_role)
ECHO_PART_SETTER(Key, const QString &, m_key)
ECHO_PART_SETTER(Surface, const QString &, m_surface)
ECHO_PART_SETTER(Properties, const QStringList &, m_properties)
ECHO_PART_SETTER(Actions, const QStringList &, m_actions)
ECHO_PART_SETTER(Active, bool, m_active)
ECHO_PART_SETTER(Replaceable, bool, m_replaceable)
#undef ECHO_PART_SETTER
void EchoUiPartRegistration::connectTarget()
{
    for (const auto &c : m_connections)
        disconnect(c);
    m_connections.clear();
    if (!m_target)
        return;
    QSet<int> wiredSignals;
    const auto stateSlot = metaObject()->method(metaObject()->indexOfSlot("nativeStateChanged()"));
    const auto activatedSlot = metaObject()->method(metaObject()->indexOfSlot("nativeActivated()"));
    auto watched = observed() ? EchoUiNativeContract::stateProperties(m_target) : QStringList();
    if (observed())
        watched += nativeProperties().values();
    for (const auto &name : watched) {
        const auto index = m_target->metaObject()->indexOfProperty(name.toUtf8());
        if (index < 0)
            continue;
        const auto property = m_target->metaObject()->property(index);
        if (property.hasNotifySignal() && !wiredSignals.contains(property.notifySignalIndex())) {
            wiredSignals.insert(property.notifySignalIndex());
            m_connections << connect(m_target, property.notifySignal(), this, stateSlot);
        }
    }
    if (observed())
        if (auto *attached = nativeLayout(m_target)) {
            for (const auto &name : EchoUiLayout::itemProperties(m_target->parentItem())) {
                const auto index = attached->metaObject()->indexOfProperty(name.toUtf8());
                if (index >= 0) {
                    const auto property = attached->metaObject()->property(index);
                    if (property.hasNotifySignal())
                        m_connections
                            << connect(attached, property.notifySignal(), this, stateSlot);
                }
            }
        }
    if (const int index = m_target->metaObject()->indexOfSignal("clicked()");
        observed() && index >= 0)
        m_connections << connect(m_target, m_target->metaObject()->method(index), this,
                                 activatedSlot);
    const auto update = [this] {
        emit changed();
        if (m_registry && !m_identity.isEmpty())
            emit m_registry->partChanged(this);
    };
    m_connections << connect(m_target, &QQuickItem::xChanged, this, update)
                  << connect(m_target, &QQuickItem::yChanged, this, update)
                  << connect(m_target, &QQuickItem::widthChanged, this, update)
                  << connect(m_target, &QQuickItem::heightChanged, this, update)
                  << connect(m_target, &QQuickItem::visibleChanged, this, update)
                  << connect(m_target, &QQuickItem::enabledChanged, this, update)
                  << connect(m_target, &QQuickItem::parentChanged, this,
                             [this] {
                                 connectTarget();
                                 applyNativeStyles();
                                 if (m_registry) {
                                     m_registry->addressTreeChanged(m_target);
                                     m_registry->notifyStructure();
                                 }
                                 // Reparenting inside the same live window preserves semantic
                                 // identity. Delegate key/session changes still retire it.
                                 if (!m_target || !m_target->window() || !m_target->parentItem())
                                     refresh();
                                 else {
                                     emit changed();
                                     if (m_registry)
                                         emit m_registry->partChanged(this);
                                 }
                             })
                  << connect(m_target, &QQuickItem::windowChanged, this,
                             [this] {
                                 connectTarget();
                                 refresh();
                             })
                  << connect(m_target, &QObject::destroyed, this, [this] {
                         if (m_registry)
                             m_registry->detach(this);
                         m_target = nullptr;
                         emit changed();
                     });
    if (auto *window = m_target->window())
        m_connections << connect(window, &QWindow::visibilityChanged, this, update);
}
void EchoUiPartRegistration::setBaseValues(const QVariantMap &values)
{
    if (m_base == values)
        return;
    m_base = values;
    emit changed();
}
void EchoUiPartRegistration::setStyleObserved(bool value)
{
    if (m_styleObserved == value)
        return;
    m_styleObserved = value;
    if (value)
        acquireState();
    else
        releaseState();
}
void EchoUiPartRegistration::setResolved(const QVariantMap &values, const QVariantMap &sources)
{
    const bool valuesChanged = m_overrides != values;
    if (!valuesChanged && m_visualSources == sources)
        return;
    m_overrides = values;
    m_visualSources = sources;
    const QPointer<EchoUiPartRegistration> self(this);
    if (valuesChanged)
        applyNativeStyles();
    if (!self)
        return;
    if (valuesChanged)
        emit overridesChanged();
    if (self)
        emit changed();
}
QMap<QString, QString> EchoUiPartRegistration::nativeProperties() const
{
    QMap<QString, QString> result;
    if (!m_target)
        return result;
    result.insert("opacity", "opacity");
    result.insert("scale", "scale");
    if (m_target->inherits("QQuickRectangle")) {
        result.insert("background", "color");
        result.insert("borderColor", "border.color");
        result.insert("borderWidth", "border.width");
        result.insert("radius", "radius");
    }
    if (m_target->inherits("QQuickText") || m_target->inherits("QQuickTextInput") ||
        m_target->inherits("QQuickTextEdit"))
        result.insert("foreground", "color");
    if (m_target->metaObject()->indexOfProperty("font") >= 0) {
        result.insert("font", "font");
        result.insert("fontSize", "font.pixelSize");
        result.insert("fontWeight", "font.weight");
        result.insert("letterSpacing", "font.letterSpacing");
    }
    if (!layoutKind().isEmpty())
        for (const auto &name : {"spacing", "rowSpacing", "columnSpacing"})
            if (m_target->metaObject()->indexOfProperty(name) >= 0)
                result.insert(name, name);
    result.insert(EchoUiNativeContract::visuals(m_target));
    return result;
}
QStringList EchoUiPartRegistration::properties() const
{
    auto result = m_properties;
    result += nativeProperties().keys();
    result += EchoUiLayout::itemProperties(m_target ? m_target->parentItem() : nullptr);
    result.removeDuplicates();
    return result;
}
QVariantMap EchoUiPartRegistration::baseValues() const
{
    auto result = m_base;
    const auto native = nativeProperties();
    if (m_target && m_registry)
        for (const auto &[key, path] : native.asKeyValueRange()) {
            if (result.contains(key))
                continue;
            const auto value =
                QQmlProperty(m_target, path, m_registry->engine().rootContext()).read();
            if (value.isValid())
                result.insert(key, value);
        }
    if (auto *attached = nativeLayout(m_target))
        for (const auto &key : EchoUiLayout::itemProperties(m_target->parentItem())) {
            if (result.contains(key))
                continue;
            const auto value = attached->property(key.toUtf8());
            if (value.isValid())
                result.insert(key, value);
        }
    return result;
}
void EchoUiPartRegistration::applyNativeStyles()
{
    const QPointer<EchoUiPartRegistration> self(this);
    const auto generation = ++m_nativeStyleGeneration;
    // A single adapter owns property restoration for every kind of host item.
    // Declared renderer properties stay with the existing shared component.
    QMap<QString, QVariantMap> byProperty;
    const auto native = nativeProperties();
    const auto layout = EchoUiLayout::itemProperties(m_target ? m_target->parentItem() : nullptr);
    for (auto it = m_overrides.begin(); m_target && it != m_overrides.end(); ++it) {
        const auto key = it.key();
        if (m_properties.contains(key))
            continue;
        const bool itemLayout = layout.contains(key) && key != "visible";
        const bool attached =
            itemLayout && EchoUiLayout::engine(m_target->parentItem()) == "layout";
        QString path;
        if (itemLayout)
            path = attached ? key : key == "preferredWidth" ? "width" : "height";
        else if (key == "visible")
            path = "visible";
        else
            path = native.value(key);
        if (path.isEmpty())
            continue;
        if (key == "font" && it.value().metaType().id() == QMetaType::QVariantMap) {
            const auto members = it.value().toMap();
            for (auto member = members.begin(); member != members.end(); ++member) {
                const auto property = "font." + member.key();
                byProperty[property] = {{"key", key},
                                        {"member", member.key()},
                                        {"property", property},
                                        {"attached", false}};
            }
        } else {
            byProperty[path] = {{"key", key}, {"property", path}, {"attached", attached}};
        }
    }
    QVariantList bindings;
    for (const auto &binding : byProperty)
        bindings.append(binding);
    // Qt Binding preserves expressions and values. An implicit dimension and
    // inherited visibility are native states, however, not their current value.
    // Save those states only while an actual override owns the property.
    if (m_target) {
        auto *nativeItem = QQuickItemPrivate::get(m_target);
        for (const auto &name : {QString("width"), QString("height"), QString("visible")}) {
            if (!byProperty.contains(name) || m_nativeDefaults.contains(name))
                continue;
            const bool wasBound = std::any_of(
                m_nativeBindings.cbegin(), m_nativeBindings.cend(),
                [&name](const QVariant &entry) { return entry.toMap().value("property") == name; });
            if (wasBound)
                continue;
            if (name == "width" && !nativeItem->widthValid())
                m_nativeDefaults[name] = true;
            else if (name == "height" && !nativeItem->heightValid())
                m_nativeDefaults[name] = true;
            else if (name == "visible" && m_registry &&
                     !QQmlAnyBinding::ofProperty(
                         QQmlProperty(m_target, name, m_registry->engine().rootContext())))
                m_nativeDefaults[name] = bool(nativeItem->explicitVisible);
        }
    }
    if (bindings != m_nativeBindings) {
        // Retire all previous property owners before installing a new set.
        // Resetting an Instantiator model in-place can construct a new Binding
        // before destroying its old counterpart, whose restoration then wins.
        auto *previous = m_nativeStyleAdapter.data();
        m_nativeStyleAdapter = nullptr;
        delete previous;
        if (!self || generation != m_nativeStyleGeneration)
            return;
        m_nativeBindings = bindings;
        emit nativeStyleBindingsChanged();
        if (!self || generation != m_nativeStyleGeneration)
            return;
    }
    if (bindings.isEmpty()) {
        auto *previous = m_nativeStyleAdapter.data();
        m_nativeStyleAdapter = nullptr;
        delete previous;
    } else if (!m_nativeStyleAdapter && m_registry) {
        QQmlComponent component(&m_registry->engine(),
                                QUrl("qrc:/qt/qml/EchoMusic/Controls/EchoUiNativeStyle.qml"));
        QPointer<QObject> object = component.beginCreate(m_registry->engine().rootContext());
        if (object) {
            QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
            component.setInitialProperties(object, {{"part", QVariant::fromValue(this)}});
        }
        component.completeCreate();
        if (!self || generation != m_nativeStyleGeneration) {
            if (object)
                object->deleteLater();
            return;
        }
        if (object && !component.isError()) {
            object->setParent(this);
            m_nativeStyleAdapter = object;
        } else {
            delete object.data();
            qWarning().noquote() << "Cannot create native UI property adapter:"
                                 << component.errorString();
        }
    }
    for (const auto &name : m_nativeDefaults.keys()) {
        if (byProperty.contains(name))
            continue;
        const auto value = m_nativeDefaults.take(name);
        if (m_target) {
            if (name == "width")
                m_target->resetWidth();
            else if (name == "height")
                m_target->resetHeight();
            else if (name == "visible")
                m_target->setVisible(value.toBool());
        }
        if (!self || generation != m_nativeStyleGeneration)
            return;
    }
}
void EchoUiPartRegistration::setReplacement(const QString &token, const QString &candidate)
{
    if (m_replacement == token && m_candidate == candidate)
        return;
    if (auto *native = qobject_cast<EchoUiRetainedContentFactory *>(m_nativeFactory))
        native->setNativeVisible(token.isEmpty());
    m_replacement = token;
    m_candidate = candidate;
    if (!token.isEmpty())
        ensureNativeFactory();
    if (auto *native = qobject_cast<EchoUiControlContentFactory *>(m_nativeFactory.data()))
        native->setLegacyCovered(!token.isEmpty());
    if (token.isEmpty() && candidate.isEmpty())
        releaseAdaptationLater();
    emit replacementChanged();
    emit changed();
}
QString EchoUiPartRegistration::surface() const
{
    if (!m_surface.isEmpty())
        return m_surface;
    for (auto *item = m_target.data(); item; item = item->parentItem()) {
        if (auto *scope = EchoUiScope::get(item); scope && !scope->surface().isEmpty())
            return scope->surface();
        const auto surface = item->property("echoUiSurface").toString();
        if (!surface.isEmpty())
            return surface;
    }
    const auto name = m_target && m_target->window() ? m_target->window()->objectName() : QString();
    return name.contains("mini", Qt::CaseInsensitive)           ? "mini"
           : name.contains("desktopLyric", Qt::CaseInsensitive) ? "desktop"
                                                                : "main";
}
QString EchoUiPartRegistration::windowId() const
{
    return m_registry && m_target ? m_registry->windowId(m_target->window()) : QString();
}
QRectF EchoUiPartRegistration::geometry() const
{
    return m_target ? m_target->mapRectToScene(m_target->boundingRect()) : QRectF();
}
bool EchoUiPartRegistration::presented() const
{
    return !m_identity.isEmpty() && EchoUiPresentation::presented(m_target);
}
QFont EchoUiPartRegistration::resolveFont(const QFont &base, const QVariantMap &values) const
{
    QFont font =
        values.value("font").canConvert<QFont>() ? values.value("font").value<QFont>() : base;
    if (values.contains("fontSize"))
        font.setPixelSize(qRound(values.value("fontSize").toDouble()));
    if (values.contains("fontWeight"))
        font.setWeight(QFont::Weight(qRound(values.value("fontWeight").toDouble())));
    if (values.contains("letterSpacing"))
        font.setLetterSpacing(QFont::AbsoluteSpacing, values.value("letterSpacing").toDouble());
    return font;
}
void EchoUiPartRegistration::publish(const QString &event, const QVariantMap &payload)
{
    if (!presented() || event.isEmpty() || event.size() > 128 ||
        QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact).size() > 65536)
        return;
    emit eventOccurred(event, payload);
}
bool EchoUiPartRegistration::invoke(const QString &action, const QVariantMap &arguments)
{
    if (!presented() || !m_target->isEnabled() || !actions().contains(action))
        return false;
    if (m_actions.contains(action)) {
        emit actionRequested(action, arguments);
        return true;
    }
    return EchoUiNativeContract::invoke(m_target, action, arguments);
}
} // namespace EchoMusic

namespace EchoMusic
{
QString EchoUiPartRegistration::owner() const
{
    if (m_owner != "@host")
        return m_owner;
    for (auto *item = static_cast<QObject *>(m_target.data()); item; item = item->parent()) {
        if (auto *scope = EchoUiScope::get(item); scope && !scope->owner().isEmpty())
            return scope->owner();
        const auto owner = item->property("_echoPluginOwner").toString();
        if (!owner.isEmpty())
            return owner;
    }
    return m_owner;
}
QString EchoUiPartRegistration::parentId() const
{
    return m_registry ? m_registry->parentOf(this) : QString();
}
QString EchoUiPartRegistration::address() const
{
    return m_registry ? m_registry->address(this) : QString();
}
QString EchoUiPartRegistration::scope() const
{
    return m_registry ? m_registry->scope(this) : QString();
}
QString EchoUiPartRegistration::resourceKey() const
{
    return m_registry ? m_registry->resourceKey(this) : QString();
}
QString EchoUiPartRegistration::layoutKind() const
{
    return EchoUiLayout::kind(m_target);
}
QString EchoUiPartRegistration::layoutEngine() const
{
    return EchoUiLayout::engine(m_target);
}
QStringList EchoUiPartRegistration::insertionPositions() const
{
    return EchoUiLayout::insertionPositions(m_target);
}
bool EchoUiPartRegistration::canCompose() const
{
    return m_nativeFactory || EchoUiControlContentFactory::supports(m_target);
}
bool EchoUiPartRegistration::ensureNativeFactory()
{
    if (!m_nativeFactory && m_registry && EchoUiControlContentFactory::supports(m_target))
        setNativeFactory(new EchoUiControlContentFactory(m_target, m_registry->engine(), this));
    return m_nativeFactory;
}
void EchoUiPartRegistration::releaseAdaptationLater()
{
    auto *factory = qobject_cast<EchoUiControlContentFactory *>(m_nativeFactory.data());
    if (!factory)
        return;
    const QPointer<EchoUiControlContentFactory> guard(factory);
    QTimer::singleShot(0, this, [this, guard] {
        if (!guard || m_nativeFactory != guard || contentFactory() != guard ||
            !m_replacement.isEmpty() || !m_candidate.isEmpty())
            return;
        guard->present(nullptr);
        setNativeFactory(nullptr);
        delete guard.data();
    });
}
void EchoUiPartRegistration::setNativeFactory(EchoUiContentFactory *factory)
{
    if (m_nativeFactory == factory)
        return;
    m_nativeFactory = factory;
    if (auto *native = qobject_cast<EchoUiRetainedContentFactory *>(factory))
        native->setNativeVisible(m_replacement.isEmpty());
    if (factory && m_complete)
        factory->setOwnerTag(owner());
    if (factory)
        connect(factory, &EchoUiContentFactory::changed, this, [this] {
            if (m_registry)
                m_registry->resolveComposition(this);
        });
    if (m_registry)
        m_registry->resolveComposition(this);
    emit factoryChanged();
}
void EchoUiPartRegistration::setContentFactory(EchoUiContentFactory *factory,
                                               const QString &signature, bool replaced)
{
    if (m_compositionSignature == signature && m_contentFactory == factory)
        return;
    m_contentFactory = factory;
    m_compositionSignature = signature;
    m_contentReplaced = replaced;
    if (auto *native = qobject_cast<EchoUiControlContentFactory *>(m_nativeFactory.data()))
        native->present(factory);
    if (!factory || factory == m_nativeFactory)
        releaseAdaptationLater();
    emit factoryChanged();
    emit changed();
}
QVariantMap EchoUiPartRegistration::layoutOverrides() const
{
    QVariantMap result;
    for (const auto &name : {"visible", "preferredWidth", "preferredHeight", "minimumWidth",
                             "minimumHeight", "maximumWidth", "maximumHeight", "fillWidth",
                             "fillHeight", "alignment", "rowSpan", "columnSpan", "row", "column",
                             "margins", "leftMargin", "rightMargin", "topMargin", "bottomMargin"})
        if (m_overrides.contains(name))
            result.insert(name, m_overrides.value(name));
    return result;
}
} // namespace EchoMusic
