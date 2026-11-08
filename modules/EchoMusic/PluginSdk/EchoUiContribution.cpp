#include "EchoUiContribution.h"
#include "EchoUiLayout.h"
#include "EchoPluginUiParts.h"
#include "EchoPluginContext.h"
#include "EchoPluginViewContext.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiScope.h"
#include "EchoUiFocusLease.h"
#include "EchoUiPresentation.h"
#include "EchoPluginScriptValue_p.h"
#include <QQmlContext>
#include <QTimer>
#include <cmath>
#include <functional>
#include <QtQml/private/qqmlcomponent_p.h>
namespace EchoMusic
{
EchoUiPluginContentFactory::EchoUiPluginContentFactory(EchoPluginContext &scope,
                                                       EchoUiPartRegistration &part,
                                                       const QString &path, QObject *owner,
                                                       bool sibling, QString key, bool preparing)
    : EchoUiContentFactory(owner), m_scope(&scope), m_part(&part), m_component(&scope.engine()),
      m_key(std::move(key)), m_sibling(sibling), m_preparing(preparing)
{
    m_observer = new EchoUiPartRef(scope, part, this);
    connect(m_observer, &EchoUiPartRef::changed, this, &EchoUiContentFactory::presentationChanged);
    connect(&m_component, &QQmlComponent::statusChanged, this, [this](auto status) {
        if (status == QQmlComponent::Error)
            fail(m_component.errorString());
        else
            emit changed();
    });
    connect(&scope.engine(), &QQmlEngine::warnings, this,
            [this, path](const QList<QQmlError> &errors) {
                for (const auto &error : errors) {
                    bool owned = false;
                    if (error.object()) {
                        for (auto *object = error.object(); object; object = object->parent()) {
                            const auto tag = object->property("_echoUiFactory");
                            if (tag.isValid()) {
                                owned = tag.value<QObject *>() == this;
                                break;
                            }
                        }
                    } else if (error.url() == QUrl::fromLocalFile(path))
                        owned = true;
                    else if (m_view && m_scope && error.url().isLocalFile() &&
                             error.url().toLocalFile().startsWith(m_scope->package().root + '/')) {
                        const std::function<bool(QObject *)> owns = [&](QObject *object) {
                            if (auto *context = qmlContext(object);
                                context && context->baseUrl() == error.url())
                                return true;
                            for (auto *child : object->children())
                                if (owns(child))
                                    return true;
                            return false;
                        };
                        owned = owns(m_view);
                    }
                    if (owned) {
                        fail(error.toString());
                        break;
                    }
                }
            });
    m_component.loadUrl(QUrl::fromLocalFile(path), QQmlComponent::Asynchronous);
}
namespace
{
class PreparedNativeContent final : public EchoUiContentFactory
{
public:
    using EchoUiContentFactory::EchoUiContentFactory;
    QQuickItem *create(QQuickItem *parent, EchoUiContentFactory *) override
    {
        auto *item = new QQuickItem(parent);
        item->setSize(parent->size());
        item->setImplicitWidth(parent->width());
        item->setImplicitHeight(parent->height());
        return item;
    }
};
} // namespace
EchoUiPluginContentFactory::~EchoUiPluginContentFactory()
{
    discardPrepared();
}
void EchoUiPluginContentFactory::setModelData(const QVariantMap &row, const QVariantMap &entry)
{
    m_hasModelData = true;
    m_modelRow = row;
    m_modelEntry = entry;
    const QPointer<QQuickItem> view = m_preparedView ? m_preparedView : m_view;
    if (!view || !m_scope || !m_scope->active())
        return;
    if (view->metaObject()->indexOfProperty("row") >= 0)
        view->setProperty("row",
                          QVariant::fromValue(PluginScript::nativeValue(m_scope->engine(), row)));
    if (view && view->metaObject()->indexOfProperty("entry") >= 0)
        view->setProperty("entry",
                          QVariant::fromValue(PluginScript::nativeValue(m_scope->engine(), entry)));
}
bool EchoUiPluginContentFactory::updateModelData(const QVariantMap &row, const QVariantMap &entry)
{
    if (m_failed || !m_scope || !m_scope->active())
        return false;
    setModelData(row, entry);
    return true;
}
void EchoUiPluginContentFactory::discardPrepared()
{
    if (m_preparedScope)
        m_preparedScope->beginRetire();
    if (m_preparedView) {
        m_preparedView->setVisible(false);
        m_preparedView->setParentItem(nullptr);
        m_preparedView->deleteLater();
    }
    m_preparedView = nullptr;
    m_preparedPart = nullptr;
    m_preparedScope = nullptr;
    m_preparedPage = nullptr;
}
QSizeF EchoUiPluginContentFactory::naturalSizeHint() const
{
    if (!m_preparedView)
        return {};
    return {m_preparedView->implicitWidth() > 0 ? m_preparedView->implicitWidth()
                                                : m_preparedView->width(),
            m_preparedView->implicitHeight() > 0 ? m_preparedView->implicitHeight()
                                                 : m_preparedView->height()};
}
bool EchoUiPluginContentFactory::prepare(QQuickItem *parent)
{
    if (prepared())
        return true;
    if (!ready() || failed())
        return false;
    m_preparing = true;
    auto *placeholder = new PreparedNativeContent(this);
    auto *item = create(parent, placeholder);
    if (!item)
        return false;
    item->ensurePolished();
    const auto hint = naturalSizeHint();
    if (!std::isfinite(hint.width()) || !std::isfinite(hint.height()) || hint.width() < 0 ||
        hint.height() < 0 || hint.width() > 16384 || hint.height() > 16384) {
        fail("Prepared content has invalid natural dimensions");
        return false;
    }
    return true;
}
bool EchoUiPluginContentFactory::suspended() const
{
    if (!m_observer || !m_observer->valid())
        return true;
    if (m_sibling) {
        auto *parent = m_part && m_part->target() ? m_part->target()->parentItem() : nullptr;
        return !EchoUiPresentation::measurable(parent);
    }
    const bool measure = m_part && m_part->target() &&
                         (m_part->target()->width() <= 0 || m_part->target()->height() <= 0);
    return !m_observer->visible() &&
           !(measure && m_part && EchoUiPresentation::measurable(m_part->target()));
}
bool EchoUiPluginContentFactory::ready() const
{
    return !m_failed && m_scope && m_scope->active() && m_part && m_component.isReady();
}
void EchoUiPluginContentFactory::fail(const QString &error)
{
    if (m_failed)
        return;
    m_failed = true;
    m_error = error;
    const bool report = !m_preparing;
    QTimer::singleShot(0, this, [this, report] {
        emit changed();
        if (report && m_scope && m_scope->active())
            emit m_scope->callbackFailed("ui.parts contribution: " + m_error);
    });
}
QQuickItem *EchoUiPluginContentFactory::create(QQuickItem *parent, EchoUiContentFactory *content)
{
    if (!ready())
        return nullptr;
    if (m_preparedView && !m_preparing) {
        auto *item = m_preparedView.data();
        const auto part = m_preparedPart;
        const auto scope = m_preparedScope;
        const auto page = m_preparedPage;
        m_preparedView = nullptr;
        m_preparedPart = nullptr;
        m_preparedScope = nullptr;
        m_preparedPage = nullptr;
        if (!part || !scope || !part->valid()) {
            item->deleteLater();
            return nullptr;
        }
        part->setHostContent(content);
        for (auto *host : item->findChildren<EchoUiHostContent *>())
            if (host->part() == part)
                host->setFactory(content);
        item->setParent(parent);
        item->setParentItem(parent);
        item->setSize(parent->size());
        EchoUiScope::get(item)->renew();
        bindRetirement(parent, item, scope, page);
        return item;
    }
    auto *owner = new QObject(parent);
    owner->setProperty("active", true);
    auto *scope = m_scope->createPresentation(owner, m_part->surface());
    if (!scope) {
        delete owner;
        fail("Presentation scope limit exceeded");
        return nullptr;
    }
    auto *part = new EchoUiPartRef(*scope, *m_part, owner);
    part->setHostContent(content);
    auto *page = new EchoPluginViewContext(*scope, m_part->surface(), owner);
    connect(scope, &EchoPluginContext::callbackFailed, this,
            [this](const auto &error) { fail(error); });
    auto *object = m_component.beginCreate(m_scope->engine().rootContext());
    auto *item = qobject_cast<QQuickItem *>(object);
    if (item) {
        m_view = item;
        item->setProperty("_echoUiFactory", QVariant::fromValue<QObject *>(this));
        item->setProperty("_echoPluginOwner", m_scope->id());
        EchoUiScope::mark(item, m_scope->id(), m_part->surface(),
                          (m_part->persistent() ? "composition:" : "@composition:") +
                              m_part->address() + ':' + m_key);
        EchoUiScope::get(item)->setResourceKey(m_part->resourceKey());
        item->setParent(parent);
        item->setParentItem(parent);
        item->setSize(parent->size());
        QVariantMap properties{{"plugin", QVariant::fromValue(scope)},
                               {"part", QVariant::fromValue(part)}};
        if (item->metaObject()->indexOfProperty("page") >= 0)
            properties["page"] = QVariant::fromValue(page);
        if (m_hasModelData && item->metaObject()->indexOfProperty("row") >= 0)
            properties["row"] =
                QVariant::fromValue(PluginScript::nativeValue(m_scope->engine(), m_modelRow));
        if (m_hasModelData && item->metaObject()->indexOfProperty("entry") >= 0)
            properties["entry"] =
                QVariant::fromValue(PluginScript::nativeValue(m_scope->engine(), m_modelEntry));
        m_component.setInitialProperties(item, properties);
    }
    const bool missingRequired =
        QQmlComponentPrivate::get(&m_component)->hasUnsetRequiredProperties();
    m_component.completeCreate();
    if (missingRequired)
        fail("Contribution has unset required properties");
    if (!item || m_component.isError() || m_failed) {
        delete object;
        delete owner;
        fail(m_component.errorString().isEmpty()
                 ? QStringLiteral(
                       "Contribution needs an Item root with typed plugin and part properties")
                 : m_component.errorString());
        return nullptr;
    }
    owner->setParent(item);
    auto *focus = new EchoUiFocusLease(item, m_part->target(), owner);
    connect(scope, &EchoPluginContext::activeChanged, focus, [scope, focus] {
        if (!scope->active())
            focus->restore();
    });
    if (m_preparing) {
        m_preparedView = item;
        m_preparedPart = part;
        m_preparedScope = scope;
        m_preparedPage = page;
    } else
        bindRetirement(parent, item, scope, page);
    QQmlEngine::setObjectOwnership(item, QQmlEngine::CppOwnership);
    if (!std::exchange(m_constructed, true))
        emit realized();
    return item;
}
void EchoUiPluginContentFactory::bindRetirement(QQuickItem *parent, QQuickItem *item,
                                                EchoPluginContext *scope,
                                                EchoPluginViewContext *page)
{
    if (auto *host = qobject_cast<EchoUiContentHost *>(parent)) {
        const auto focus = item->findChildren<EchoUiFocusLease *>();
        for (auto *lease : focus)
            connect(host, &EchoUiContentHost::retiring, lease, [lease, item](auto *retiring) {
                if (retiring == item)
                    lease->restore();
            });
        connect(host, &EchoUiContentHost::retiring, scope,
                [scope, page = QPointer<EchoPluginViewContext>(page),
                 view = QPointer<QQuickItem>(item)](QQuickItem *retiring) {
                    if (view && view == retiring) {
                        if (page && page->active())
                            emit page->aboutToClose();
                        scope->beginRetire();
                    }
                });
    }
}
void EchoUiHostContent::setPart(EchoUiPartRef *part)
{
    if (m_part == part)
        return;
    if (m_part)
        disconnect(m_part, nullptr, &m_callbacks, nullptr);
    m_part = part;
    if (part)
        connect(part, &EchoUiPartRef::invalidated, &m_callbacks, [this] { setFactory(nullptr); });
    setFactory(part && part->valid() ? part->hostContent() : nullptr);
    emit partChanged();
}
EchoUiContribution::EchoUiContribution(EchoPluginUiParts &owner, EchoPluginRegistration &lease,
                                       QString id, QVariantMap selector, QVariantMap definition)
    : QObject(&lease), m_owner(&owner), m_lease(&lease), m_registry(owner.registry()),
      m_id(std::move(id)), m_selector(std::move(selector)), m_definition(std::move(definition))
{
}
EchoUiContribution::~EchoUiContribution()
{
    stop();
}
bool EchoUiContribution::active() const
{
    return m_started && m_owner && m_owner->scope().active() && m_lease && m_lease->active();
}
QVariantList EchoUiContribution::matches() const
{
    QVariantList result;
    for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
        const auto effective = m_registry ? m_registry->compositionState(it->token) : QVariantMap();
        result.append(QVariantMap{{"id", it.key()},
                                  {"state", effective.value("state", it->state)},
                                  {"error", effective.value("error", it->error)}});
    }
    return result;
}
bool EchoUiContribution::validate(EchoPluginContext &scope, const QVariantMap &definition,
                                  QString &error)
{
    const QStringList keys{"operation",   "component", "position", "layout", "priority",
                           "destination", "when",      "axis",     "gap"};
    for (auto it = definition.begin(); it != definition.end(); ++it)
        if (!keys.contains(it.key())) {
            error = "Unknown contribution field: " + it.key();
            return false;
        }
    const auto op = definition.value("operation", "insert").toString();
    if (!QStringList{"insert", "replace", "wrap", "hide", "move", "layout"}.contains(op)) {
        error = "Invalid UI operation";
        return false;
    }
    bool ok = true;
    const auto priorityValue = definition.value("priority", 0);
    const auto priority = priorityValue.toDouble(&ok);
    const auto type = priorityValue.metaType().id();
    const bool numeric = type == QMetaType::Int || type == QMetaType::UInt ||
                         type == QMetaType::LongLong || type == QMetaType::ULongLong ||
                         type == QMetaType::Double || type == QMetaType::Float;
    if (!numeric || !ok || !std::isfinite(priority) || priority != std::floor(priority) ||
        priority < -10000 || priority > 10000) {
        error = "Invalid UI contribution priority";
        return false;
    }
    if ((op != "insert" && op != "move" && definition.contains("position")) ||
        (op != "insert" && (definition.contains("axis") || definition.contains("gap"))) ||
        (op != "move" && definition.contains("destination")) ||
        (op != "insert" && op != "replace" && op != "wrap" && definition.contains("component"))) {
        error = "Field is not supported for this UI operation";
        return false;
    }
    if (op == "insert" || op == "replace" || op == "wrap") {
        const auto path = scope.package().resolve(definition.value("component").toString());
        if (path.isEmpty() || !path.endsWith(".qml")) {
            error = "Expected package QML component";
            return false;
        }
    }
    if (!QStringList{"append", "prepend", "before", "after", "background", "foreground"}.contains(
            definition.value("position", "append").toString())) {
        error = "Invalid contribution position";
        return false;
    }
    if (definition.contains("layout") &&
        definition.value("layout").metaType().id() != QMetaType::QVariantMap) {
        error = "Expected layout map";
        return false;
    }
    if (op == "move" && !QStringList{"append", "prepend"}.contains(
                            definition.value("position", "append").toString())) {
        error = "Move position must be append or prepend";
        return false;
    }
    if (definition.value("when").toMap().size() > 32) {
        error = "Too many state conditions";
        return false;
    }
    if (!QStringList{"horizontal", "vertical"}.contains(
            definition.value("axis", "horizontal").toString())) {
        error = "Insertion axis must be horizontal or vertical";
        return false;
    }
    const auto gapValue = definition.value("gap", 0);
    const auto gapType = gapValue.metaType().id();
    const bool gapNumeric = gapType == QMetaType::Int || gapType == QMetaType::UInt ||
                            gapType == QMetaType::LongLong || gapType == QMetaType::ULongLong ||
                            gapType == QMetaType::Double || gapType == QMetaType::Float;
    const auto gap = gapValue.toDouble(&ok);
    if (!gapNumeric || !ok || !std::isfinite(gap) || gap < 0 || gap > 1024) {
        error = "Invalid insertion gap";
        return false;
    }
    auto layout = definition.value("layout").toMap();
    if (!EchoUiPartRegistry::normalize(layout, error))
        return false;
    const QStringList layoutKeys{
        "visible",      "preferredWidth", "preferredHeight", "minimumWidth", "minimumHeight",
        "maximumWidth", "maximumHeight",  "fillWidth",       "fillHeight",   "alignment",
        "row",          "column",         "rowSpan",         "columnSpan",   "margins",
        "leftMargin",   "rightMargin",    "topMargin",       "bottomMargin"};
    for (const auto &name : layout.keys())
        if (!layoutKeys.contains(name)) {
            error = "Expected layout property: " + name;
            return false;
        }
    if (op == "move" &&
        (!definition.value("destination").canConvert<QVariantMap>() ||
         !EchoUiPartRegistry::validSelector(definition.value("destination").toMap()))) {
        error = "Expected destination selector";
        return false;
    }
    if (definition.contains("when") && !definition.value("when").canConvert<QVariantMap>()) {
        error = "Expected state condition map";
        return false;
    }
    return true;
}
bool EchoUiContribution::update(const QVariantMap &definition)
{
    QString error;
    if (!active() || !validate(m_owner->scope(), definition, error)) {
        m_error = error;
        emit changed();
        return false;
    }
    if (!m_previous.isEmpty()) {
        m_error = "A contribution update is already being prepared";
        emit changed();
        return false;
    }
    const auto generation = ++m_updateGeneration;
    if (m_preflight) {
        m_preflight->deleteLater();
        m_preflight = nullptr;
    }
    const auto operation = definition.value("operation", "insert").toString();
    if (operation != "insert" && operation != "wrap" && operation != "replace") {
        commitUpdate(definition);
        return true;
    }
    auto *component = new QQmlComponent(&m_owner->scope().engine(), this);
    m_preflight = component;
    auto inspect = [this, component, definition, generation] {
        if (generation != m_updateGeneration || !active())
            return;
        if (component->isError()) {
            m_error = component->errorString();
            m_preflight = nullptr;
            component->deleteLater();
            emit changed();
        } else if (component->isReady()) {
            m_preflight = nullptr;
            component->deleteLater();
            commitUpdate(definition);
        }
    };
    connect(component, &QQmlComponent::statusChanged, this, [inspect](auto) { inspect(); });
    component->loadUrl(QUrl::fromLocalFile(m_owner->scope().package().resolve(
                           definition.value("component").toString())),
                       QQmlComponent::Asynchronous);
    return true;
}
void EchoUiContribution::commitUpdate(const QVariantMap &definition)
{
    m_previousDefinition = m_definition;
    m_previous = std::exchange(m_instances, {});
    m_definition = definition;
    ++m_revision;
    m_error.clear();
    schedule();
}
void EchoUiContribution::finishUpdate()
{
    if (m_previous.isEmpty())
        return;
    bool ready = true, failed = false;
    for (const auto &instance : m_instances) {
        const auto effective =
            m_registry ? m_registry->compositionState(instance.token) : QVariantMap();
        failed = failed || (instance.factory && instance.factory->failed()) ||
                 instance.state == "failed" || instance.state == "unsupported" ||
                 effective.value("state") == "constrained";
        if (instance.factory && instance.part && !instance.factory->constructed())
            ready = false;
        if (effective.value("state") == "loading" || instance.state == "loading" ||
            (instance.state == "suspended" && !instance.error.isEmpty()))
            ready = false;
    }
    if (failed) {
        auto rejected = std::exchange(m_instances, {});
        for (auto &instance : rejected)
            remove(instance);
        m_definition = m_previousDefinition;
        m_instances = std::exchange(m_previous, {});
        m_error = "Contribution update failed; previous contribution restored";
        for (auto &instance : m_instances) {
            instance.state.clear();
            apply(instance);
        }
    } else if (ready) {
        auto previous = std::exchange(m_previous, {});
        for (auto &instance : previous)
            remove(instance);
        m_previousDefinition.clear();
    }
}

void EchoUiContribution::dispose()
{
    if (m_lease)
        m_lease->dispose();
}
void EchoUiContribution::start()
{
    if (m_started || !m_registry)
        return;
    m_started = true;
    m_registry->acquireStructure(this);
    const auto structural = [this] {
        if (m_registry && m_registry->enabled())
            schedule();
    };
    m_connections << connect(m_registry, &EchoUiPartRegistry::added, this,
                             [structural](auto *) { structural(); })
                  << connect(m_registry, &EchoUiPartRegistry::removed, this,
                             [structural](const auto &) { structural(); })
                  << connect(m_registry, &EchoUiPartRegistry::preferencesChanged, this,
                             [this] { schedule(); })
                  << connect(m_registry, &EchoUiPartRegistry::structureChanged, this, structural)
                  << connect(m_registry, &EchoUiPartRegistry::compositionChanged, this,
                             [this](const auto &id) {
                                 if (m_instances.contains(id))
                                     schedule();
                             });
    schedule();
}
void EchoUiContribution::stop()
{
    m_started = false;
    // A move/visibility change may retire this presentation while its own
    // contribution is applying. Keep the instance storage valid until unwind.
    if (m_applying)
        return;
    ++m_updateGeneration;
    if (m_preflight) {
        m_preflight->deleteLater();
        m_preflight = nullptr;
    }
    auto previous = std::exchange(m_previous, {});
    for (auto &instance : previous)
        remove(instance);
    for (auto connection : m_connections)
        disconnect(connection);
    m_connections.clear();
    auto instances = std::exchange(m_instances, {});
    for (auto &instance : instances)
        remove(instance);
    if (m_registry)
        m_registry->releaseStructure(this);
    emit changed();
}
void EchoUiContribution::schedule()
{
    if (std::exchange(m_pending, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_pending = false;
        if (active())
            reconcile();
    });
}
void EchoUiContribution::remove(Instance &instance, bool retire)
{
    if (retire) {
        disconnect(instance.stateConnection);
        instance.stateConnection = {};
        if (instance.observing && instance.part)
            instance.part->releaseState();
        instance.observing = false;
    }
    if (m_registry) {
        m_registry->removeMove(instance.token);
        m_registry->removeComposition(instance.token);
        m_registry->removeStyle(instance.token);
        m_registry->removeReplacement(instance.token);
    }
    if (instance.item) {
        delete instance.item.data();
        instance.item = nullptr;
    }
    if (instance.factory) {
        instance.factory->deleteLater();
        instance.factory = nullptr;
    }
}
void EchoUiContribution::reconcile()
{
    if (m_applying || !active() || !m_registry)
        return;
    m_applying = true;
    auto desired = m_registry->query(m_selector);
    if (m_definition.value("operation") == "move")
        for (auto it = m_instances.cbegin(); it != m_instances.cend(); ++it) {
            auto *part = it->part.data();
            // Moving changes ancestry/surface selectors itself. Keep the
            // accepted source identity until the move is withdrawn; otherwise
            // restore/rematch would oscillate indefinitely on the GUI queue.
            if (part && part->identity() == it.key() &&
                m_registry->moveWinner(it.key()) == it->token && !desired.contains(part))
                desired.append(part);
        }
    desired.removeIf([this](auto *part) {
        return part->owner() == m_owner->scope().id() || part->surface() == "inspector";
    });
    if (desired.size() > 256) {
        m_error = "UI contribution exceeds 256 live targets; nothing published";
        auto instances = std::exchange(m_instances, {});
        for (auto &instance : instances)
            remove(instance);
        if (!m_previous.isEmpty()) {
            m_definition = m_previousDefinition;
            m_instances = std::exchange(m_previous, {});
            for (auto &instance : m_instances) {
                instance.state.clear();
                apply(instance);
            }
        }
        m_applying = false;
        emit changed();
        return;
    }
    QSet<QString> keep;
    for (auto *part : desired) {
        // A broad rule never recursively targets the public nodes it creates.
        if (part->owner() == m_owner->scope().id() || part->surface() == "inspector")
            continue;

        if (keep.size() >= 256) {
            m_error = "UI contribution exceeds 256 live targets";
            break;
        }
        const auto identity = part->identity();
        keep.insert(identity);
        if (!m_instances.contains(identity)) {
            Instance instance;
            instance.part = part;
            instance.observing = !m_definition.value("when").toMap().isEmpty();
            if (instance.observing)
                part->acquireState();
            instance.token = m_lease->key() + ':' + m_lease->revision() + ':' + identity + ':' +
                             QString::number(m_revision);
            instance.stateConnection = connect(part, &EchoUiPartRegistration::stateChanged, this,
                                               &EchoUiContribution::schedule);
            m_instances.insert(identity, instance);
        }
        auto &instance = m_instances[identity];
        bool condition = m_registry->allowed(part, m_owner->scope().id(), m_id);
        const auto when = m_definition.value("when").toMap();
        for (auto i = when.begin(); i != when.end(); ++i)
            if (part->state().value(i.key()) != i.value())
                condition = false;
        if (!condition) {
            if (instance.state != "suspended") {
                remove(instance, false);
                instance.part = part;
                instance.state = "suspended";
                instance.error.clear();
            }
        }
        if (!instance.stateConnection)
            instance.stateConnection = connect(part, &EchoUiPartRegistration::stateChanged, this,
                                               &EchoUiContribution::schedule);
        if (condition)
            apply(instance);
        m_registry->reportComposition(instance.token, identity, m_owner->scope().id(), m_id,
                                      m_definition.value("operation", "insert").toString(),
                                      m_definition.value("priority", 0).toInt(), instance.state,
                                      instance.error);
    }
    for (auto it = m_instances.begin(); it != m_instances.end();) {
        if (keep.contains(it.key())) {
            ++it;
            continue;
        }
        remove(it.value());
        it = m_instances.erase(it);
    }
    finishUpdate();
    m_applying = false;
    if (!active()) {
        stop();
        return;
    }
    emit changed();
}
void EchoUiContribution::apply(Instance &instance)
{
    auto *part = instance.part.data();
    if (!part || !part->target())
        return;
    const auto op = m_definition.value("operation", "insert").toString();
    const auto priority = m_definition.value("priority", 0).toInt();
    auto layout = m_definition.value("layout").toMap();
    instance.state = "active";
    instance.error.clear();
    if (op != "insert" && op != "move" &&
        !EchoUiLayout::validate(part->target()->parentItem(), layout, instance.error)) {
        instance.state = "unsupported";
        return;
    }
    if (op == "wrap" || op == "replace" || op == "insert") {
        if (!instance.factory) {
            instance.factory = m_preparedFactories.take(part->identity());
            if (!instance.factory)
                instance.factory = new EchoUiPluginContentFactory(
                    m_owner->scope(), *part,
                    m_owner->scope().package().resolve(m_definition.value("component").toString()),
                    this,
                    op == "insert" && QStringList{"before", "after"}.contains(
                                          m_definition.value("position").toString()),
                    m_id);
            connect(instance.factory, &EchoUiPluginContentFactory::realized, this,
                    &EchoUiContribution::schedule);
            connect(instance.factory, &EchoUiContentFactory::changed, this,
                    [this, id = part->identity()] {
                        if (!m_instances.contains(id))
                            return;
                        auto &value = m_instances[id];
                        value.state.clear();
                        schedule();
                    });
        }
        if (instance.factory->failed()) {
            instance.state = "failed";
            instance.error = instance.factory->errorMessage();
            m_registry->removeComposition(instance.token);
            return;
        }
        if (!instance.factory->ready()) {
            instance.state = "loading";
            return;
        }
        if (op == "wrap" || op == "replace") {
            if (!part->ensureNativeFactory()) {
                instance.state = "unsupported";
                instance.error = "This region does not expose a host content factory";
                return;
            }
            if (!m_registry->putComposition(instance.token, part->identity(), m_owner->scope().id(),
                                            m_id, priority, op, instance.factory)) {
                instance.state = "failed";
                instance.error = "UI composition registry capacity exceeded";
                return;
            }
        } else {
            const auto position = m_definition.value("position", "append").toString();
            auto *parent = (position == "before" || position == "after")
                               ? part->target()->parentItem()
                               : part->target();
            if (!parent) {
                instance.state = "suspended";
                return;
            }
            if (position != "foreground" && position != "background") {
                if (EchoUiLayout::kind(parent).isEmpty() && position != "before" &&
                    position != "after") {
                    instance.state = "unsupported";
                    instance.error = "Flow insertion requires a native layout or positioner";
                    return;
                }
                if (!EchoUiLayout::validateInsertion(parent, layout, instance.error)) {
                    instance.state = "unsupported";
                    return;
                }
            }
            if (!instance.item) {
                QQmlComponent cell(
                    &m_owner->scope().engine(),
                    QUrl("qrc:/qt/qml/EchoMusic/PluginSdk/EchoUiContributionItem.qml"));
                auto *object = cell.createWithInitialProperties(
                    {{"factory", QVariant::fromValue(
                                     static_cast<EchoUiContentFactory *>(instance.factory.data()))},
                     {"layoutSpec", layout},
                     {"positionerLayout", EchoUiLayout::engine(parent) == "positioner"},
                     {"contributionKey", m_id},
                     {"contributionOwner", m_owner->scope().id()}});
                auto *item = qobject_cast<QQuickItem *>(object);
                if (!item) {
                    delete object;
                    instance.state = "failed";
                    instance.error = cell.errorString();
                    return;
                }
                item->setParent(this);
                EchoUiLayout::setDecoration(item,
                                            position == "foreground" || position == "background");
                item->setParentItem(parent);
                instance.item = item;
                if (position == "before")
                    item->stackBefore(part->target());
                else if (position == "after")
                    item->stackAfter(part->target());
                else if (position == "prepend" || position == "background") {
                    const auto children = parent->childItems();
                    if (!children.isEmpty() && children.first() != item)
                        item->stackBefore(children.first());
                }
                if (position == "foreground" || position == "background") {
                    item->setSize(parent->size());
                    item->setZ(position == "foreground" ? 10000 : 0);
                    connect(parent, &QQuickItem::widthChanged, item,
                            [item, parent] { item->setWidth(parent->width()); });
                    connect(parent, &QQuickItem::heightChanged, item,
                            [item, parent] { item->setHeight(parent->height()); });
                }
            }
            const bool anchoredSibling = (position == "before" || position == "after") &&
                                         EchoUiLayout::kind(parent).isEmpty();
            if (instance.item && instance.item->parentItem() != parent &&
                (!anchoredSibling || !instance.item->parentItem())) {
                instance.item->setParentItem(parent);
                instance.item->setProperty("positionerLayout",
                                           EchoUiLayout::engine(parent) == "positioner");
            }
        }
    } else if (op == "hide")
        layout["visible"] = false;
    else if (op == "move") {
        const auto destinations = m_registry->query(m_definition.value("destination").toMap());
        if (destinations.size() != 1 || destinations.first()->layoutKind().isEmpty()) {
            instance.state = "suspended";
            instance.error = "Move requires one live layout destination";
            return;
        }
        if (!EchoUiLayout::validate(destinations.first()->target(), layout, instance.error)) {
            instance.state = "unsupported";
            return;
        }
        if (!m_registry->putMove(instance.token, part->identity(), destinations.first()->identity(),
                                 m_definition.value("position", "append").toString(),
                                 m_owner->scope().id(), m_id, priority)) {
            instance.state = "failed";
            instance.error = "Invalid destination, window or composition cycle";
            return;
        }
    }
    if (op != "wrap" && op != "replace" &&
        !m_registry->putComposition(instance.token, part->identity(), m_owner->scope().id(), m_id,
                                    priority, op, instance.factory)) {
        remove(instance, false);
        instance.state = "failed";
        instance.error = "UI composition registry capacity exceeded";
        return;
    }
    if (op == "insert" && instance.item)
        m_registry->setCompositionItem(instance.token, instance.item,
                                       m_definition.value("position", "append").toString(),
                                       m_definition.value("axis", "horizontal").toString(),
                                       m_definition.value("gap", 0).toDouble());
    if (op != "insert" && !layout.isEmpty() &&
        !m_registry->putStyle(instance.token, m_owner->scope().id(), m_id, priority,
                              {{"id", part->identity()}}, layout)) {
        remove(instance, false);
        instance.state = "failed";
        instance.error = "UI style registry capacity exceeded";
    }
}
} // namespace EchoMusic
