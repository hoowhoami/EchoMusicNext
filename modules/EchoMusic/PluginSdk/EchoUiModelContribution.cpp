#include "EchoUiModelContribution.h"
#include "EchoPluginUiParts.h"
#include "EchoPluginContext.h"
#include "EchoPluginOperation.h"
#include "RouteController.h"
#include "EchoUiValue.h"
#include "EchoUiScope.h"
#include "EchoUiContribution.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiPartRegistry.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <QQuickWindow>
#include <cmath>
#include <QCryptographicHash>
namespace EchoMusic
{
namespace
{
bool modelJson(const QVariant &value, int depth = 0)
{
    if (depth > 16)
        return false;
    switch (value.metaType().id()) {
    case QMetaType::UnknownType:
    case QMetaType::Nullptr:
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::QString:
        return true;
    case QMetaType::Double:
        return std::isfinite(value.toDouble());
    case QMetaType::QVariantList:
        for (const auto &child : value.toList())
            if (!modelJson(child, depth + 1))
                return false;
        return true;
    case QMetaType::QVariantMap:
        for (const auto &child : value.toMap())
            if (!modelJson(child, depth + 1))
                return false;
        return true;
    default:
        return false;
    }
}
QVariantMap commandContext(QVariantMap context)
{
    // A valid menu trigger closes its popup before the queued command begins.
    // Closing retires providers, but does not revoke an already accepted intent.
    context.remove("active");
    context.remove("revision");
    context.remove("targetId");
    context.remove("resourceKey");
    return context;
}
QVariantMap currentModelContext(EchoPluginUiParts &owner, EchoUiPartRegistration &part)
{
    auto context = part.modelContext();
    if (auto *route = owner.scope().host().router.current()) {
        auto values = context.value("values").toMap();
        values["route"] = QVariantMap{{"name", route->name()}, {"resourceId", route->resourceId()}};
        context["values"] = values;
    }
    context["revision"] = QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument::fromVariant(context).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256)
            .toHex());
    return context;
}
} // namespace
EchoUiPluginModelRecipe::EchoUiPluginModelRecipe(EchoPluginUiParts &owner,
                                                 EchoUiPartRegistration &part, QString key,
                                                 QObject *parent)
    : EchoUiModelRecipe(parent), m_owner(&owner), m_part(&part), m_identity(part.identity()),
      m_key(std::move(key))
{
    const auto cancelStale = [this] {
        if (!m_owner || !m_part ||
            commandContext(currentModelContext(*m_owner, *m_part)) == commandContext(m_context))
            return;
        const auto pending = std::exchange(m_invocations, {});
        for (auto operation : pending)
            if (operation)
                operation->cancel();
    };
    connect(&part, &EchoUiPartRegistration::changed, this, cancelStale);
    connect(&owner.scope().host().router, &RouteController::routeChanged, this, cancelStale);
}
void EchoUiPluginModelRecipe::setContext(const QVariantMap &context)
{
    if (m_context == context)
        return;
    if (commandContext(m_context) != commandContext(context)) {
        const auto pending = std::exchange(m_invocations, {});
        for (auto operation : pending)
            if (operation)
                operation->cancel();
    }
    m_context = context;
}
bool EchoUiPluginModelRecipe::active() const
{
    return !m_retired && m_owner && m_owner->scope().active() && m_part &&
           m_part->identity() == m_identity;
}
bool EchoUiPluginModelRecipe::arm(const QVariantMap &entry)
{
    if (!active() || !entry.value("enabled", true).toBool() ||
        !m_context.value("active", true).toBool() ||
        currentModelContext(*m_owner, *m_part) != m_context)
        return false;
    m_armedContext = m_context;
    m_armedKey = entry.value("key").toString();
    QTimer::singleShot(0, this, [this] {
        m_armedContext.clear();
        m_armedKey.clear();
    });
    return true;
}
bool EchoUiPluginModelRecipe::activate(const QVariantMap &entry, const QVariantMap &event)
{
    if (!active() || !entry.value("enabled", true).toBool())
        return false;
    const auto current = currentModelContext(*m_owner, *m_part);
    const bool ordinary = m_context.value("active", true).toBool() && current == m_context;
    const bool armed = !m_armedContext.isEmpty() && m_armedKey == entry.value("key").toString() &&
                       commandContext(current) == commandContext(m_armedContext);
    const auto accepted = armed ? m_armedContext : m_context;
    m_armedContext.clear();
    m_armedKey.clear();
    if (!ordinary && !armed)
        return false;
    auto arguments = entry.value("arguments").toMap();
    for (auto it = event.begin(); it != event.end(); ++it)
        arguments[it.key()] = it.value();
    arguments["context"] = accepted;
    auto *operation = m_owner->scope().executeCommand(entry.value("command").toString(), arguments);
    if (operation) {
        m_invocations.removeIf([](const auto &pending) { return !pending || pending->finished(); });
        m_invocations.append(operation);
        if (auto *contribution = qobject_cast<EchoUiModelContribution *>(parent())) {
            connect(contribution, &EchoUiModelContribution::changed, operation,
                    [contribution, operation] {
                        if (!contribution->active())
                            operation->cancel();
                    });
            connect(contribution, &QObject::destroyed, operation, &EchoPluginOperation::cancel);
        }
        auto *router = &m_owner->scope().host().router;
        const auto route = accepted.value("values").toMap().value("route").toMap();
        connect(router, &RouteController::routeChanged, operation, [operation, router, route] {
            auto *current = router->current();
            if (!current || current->name() != route.value("name").toString() ||
                current->resourceId() != route.value("resourceId").toString())
                operation->cancel();
        });
        QSet<QObject *> visited;
        const auto watch = [&](QObject *object) {
            if (!object || visited.contains(object))
                return;
            visited.insert(object);
            if (auto *scope = EchoUiScope::get(object)) {
                const auto context = scope->modelContext();
                const auto resource = scope->resourceKey();
                connect(scope, &EchoUiScope::modelContextChanged, operation,
                        [operation, scope, context] {
                            if (scope->modelContext() != context)
                                operation->cancel();
                        });
                connect(scope, &EchoUiScope::changed, operation, [operation, scope, resource] {
                    if (scope->resourceKey() != resource)
                        operation->cancel();
                });
            }
        };
        for (auto *object = static_cast<QObject *>(m_part->target()); object;
             object = object->parent())
            watch(object);
        for (auto *item = m_part->target(); item; item = item->parentItem())
            watch(item);
    }
    return operation != nullptr;
}
bool EchoUiPluginModelRecipe::select(const QVariantMap &entry)
{
    if (entry.isEmpty()) {
        if (m_selectedContent)
            m_selectedContent->dispose();
        m_selectedContent = nullptr;
        m_selectedEntry.clear();
        return true;
    }
    if (!active())
        return false;
    if (m_selectedEntry == entry && m_selectedContent && m_selectedContent->active())
        return true;
    auto selector = entry.value("contentTarget").toMap();
    if (selector.contains("window") && selector.value("window") != m_part->windowId())
        return false;
    selector["window"] = m_part->windowId();
    const auto targets = m_owner->registry()->query(selector);
    if (targets.size() != 1 || !targets.first()->canCompose() || targets.first() == m_part ||
        targets.first()->surface() == "inspector")
        return false;
    const QVariantMap definition{{"operation", "replace"}, {"component", entry.value("component")}};
    if (m_selectedContent &&
        m_selectedEntry.value("contentTarget") != entry.value("contentTarget")) {
        m_selectedContent->dispose();
        m_selectedContent = nullptr;
    }
    if (m_selectedContent) {
        if (!m_selectedContent->update(definition))
            return false;
    } else {
        const auto id =
            "model-tab-" +
            QString::fromLatin1(QCryptographicHash::hash(m_key.toUtf8(), QCryptographicHash::Sha256)
                                    .toHex()
                                    .left(32));
        m_selectedContent = m_owner->contribute(
            id,
            targets.first()->persistent() ? QVariantMap{{"address", targets.first()->address()}}
                                          : QVariantMap{{"id", targets.first()->identity()}},
            definition);
        if (!m_selectedContent)
            return false;
    }
    m_selectedEntry = entry;
    return true;
}
void EchoUiPluginModelRecipe::retire(bool preserveAcceptedIntents)
{
    if (!std::exchange(m_retired, true)) {
        if (preserveAcceptedIntents) {
            m_invocations.clear();
            m_context.clear();
        } else
            setContext({});
        m_armedContext.clear();
        m_armedKey.clear();
        discardPrepared();
        select({});
        emit changed();
    }
}
bool EchoUiPluginModelRecipe::prepare(const QVariantMap &entry, QString &error)
{
    if (entry.value("component").toString().isEmpty() || !m_part->presented())
        return true;
    if (!m_stage) {
        m_stage = new QQuickItem;
        m_stage->setParent(this);
        m_stage->setProperty("_echoUiInternal", true);
        m_stage->setProperty("_echoUiStaging", true);
        m_stage->setVisible(false);
        m_stage->setSize(m_part->target()->size());
        if (m_part->target()->window())
            m_stage->setParentItem(m_part->target()->window()->contentItem());
    }
    const auto key = entry.value("key").toString();
    if (m_prepared.value(key))
        return true;
    // Validate a component once per preparation, not once per declared virtual
    // row. Data-specific failures are reported by the realized cell recipe.
    for (auto prepared : m_prepared)
        if (prepared && prepared->property("_echoUiPreparedComponent").toString() ==
                            entry.value("component").toString())
            return true;
    auto *factory = new EchoUiPluginContentFactory(
        m_owner->scope(), *m_part,
        m_owner->scope().package().resolve(entry.value("component").toString()), this, false,
        "model:" + m_key + ':' + key, true);
    factory->setProperty("_echoUiPreparedComponent", entry.value("component"));
    // Column construction uses a real native row when one exists. Future rows
    // remain virtual and report row-specific construction errors at realization.
    QVariantMap values = entry.value("values").toMap();
    if (auto *source = m_part->target()->property("model").value<QAbstractItemModel *>();
        source && source->rowCount()) {
        const auto roles = source->roleNames();
        for (auto role = roles.begin(); role != roles.end(); ++role)
            if (!role.value().startsWith("echoUi"))
                values[QString::fromUtf8(role.value())] = source->index(0, 0).data(role.key());
    }
    qsizetype budget = 65536;
    const auto boundedValues = EchoUiValue::json(values, budget);
    factory->setProperty("_echoUiPreparedRow",
                         QJsonDocument::fromVariant(boundedValues).toJson(QJsonDocument::Compact));
    factory->setModelData({{"index", 0}, {"column", 0}, {"values", boundedValues}}, entry);
    if (!factory->prepare(m_stage)) {
        error = factory->errorMessage().isEmpty() ? "Model component preparation failed"
                                                  : factory->errorMessage();
        factory->discardPrepared();
        delete factory;
        return false;
    }
    m_prepared[key] = factory;
    return true;
}
void EchoUiPluginModelRecipe::commitPrepared()
{
    if (m_prepared.isEmpty())
        return;
    for (auto factory : m_prepared)
        if (factory)
            factory->publishPrepared();
    // Visible cells adopt a matching prepared tree; unused construction state
    // is bounded to the first presentation turn, including hidden windows.
    const auto prepared = m_prepared;
    QTimer::singleShot(100, this, [this, prepared] {
        for (auto it = prepared.begin(); it != prepared.end(); ++it)
            if (it.value() && m_prepared.value(it.key()) == it.value()) {
                auto factory = m_prepared.take(it.key());
                factory->discardPrepared();
                factory->deleteLater();
            }
        if (m_prepared.isEmpty())
            discardPrepared();
    });
}
void EchoUiPluginModelRecipe::discardPrepared()
{
    const auto prepared = std::exchange(m_prepared, {});
    for (auto factory : prepared)
        if (factory) {
            factory->discardPrepared();
            factory->deleteLater();
        }
    if (m_stage) {
        m_stage->setParentItem(nullptr);
        m_stage->deleteLater();
        m_stage = nullptr;
    }
}
EchoUiContentFactory *EchoUiPluginModelRecipe::factory(QObject *owner, const QVariantMap &row,
                                                       const QVariantMap &entry)
{
    if (!active())
        return nullptr;
    auto *factory = m_prepared.value(entry.value("key").toString()).data();
    const auto rowBytes =
        QJsonDocument::fromVariant(row.value("values")).toJson(QJsonDocument::Compact);
    if (factory && factory->property("_echoUiPreparedRow").toByteArray() == rowBytes) {
        m_prepared.remove(entry.value("key").toString());
        factory->publishPrepared();
        factory->setParent(owner);
    } else
        factory = new EchoUiPluginContentFactory(
            m_owner->scope(), *m_part,
            m_owner->scope().package().resolve(entry.value("component").toString()), owner, false,
            "model:" + m_key + ':' + entry.value("key").toString());
    factory->setModelData(row, entry);
    connect(factory, &EchoUiContentFactory::changed, this,
            [this, factory, key = entry.value("key").toString()] {
                if (factory->failed())
                    emit constructionFailed(key, factory->errorMessage());
            });
    return factory;
}
EchoUiModelContribution::EchoUiModelContribution(EchoPluginUiParts &owner,
                                                 EchoPluginRegistration &lease, QString id,
                                                 QVariantMap selector, QVariantMap definition,
                                                 QJSValue provider)
    : QObject(&lease), m_owner(&owner), m_lease(&lease), m_registry(owner.registry()),
      m_id(std::move(id)), m_selector(std::move(selector)), m_definition(std::move(definition)),
      m_provider(std::move(provider))
{
}
bool EchoUiModelContribution::active() const
{
    return m_owner && m_owner->scope().active() && m_lease && m_lease->active();
}
bool EchoUiModelContribution::validate(EchoPluginUiParts &owner, const QVariantMap &definition,
                                       QString &error, bool allowEmpty)
{
    const auto fail = [&](const QString &text) {
        error = text;
        return false;
    };
    if (!QStringList{"rows", "columns", "menu", "tabs", "settings"}.contains(
            definition.value("kind").toString()))
        return fail("Unsupported native model contribution kind");
    for (const auto &key : definition.keys())
        if (!QStringList{"kind", "entries", "priority"}.contains(key))
            return fail("Unknown model contribution field: " + key);
    const auto priority = definition.value("priority", 0);
    if ((priority.metaType().id() != QMetaType::Int &&
         priority.metaType().id() != QMetaType::Double) ||
        priority.toDouble() != priority.toInt() || priority.toInt() < -10000 ||
        priority.toInt() > 10000)
        return fail("Invalid model contribution priority");
    if (!modelJson(definition))
        return fail("Model definitions require finite JSON values with at most 16 nesting levels");
    const auto entries = definition.value("entries");
    if (entries.metaType().id() != QMetaType::QVariantList ||
        (!allowEmpty && entries.toList().isEmpty()) || entries.toList().size() > 256 ||
        QJsonDocument::fromVariant(definition).toJson(QJsonDocument::Compact).size() > 1024 * 1024)
        return fail("Expected 1..256 bounded model entries");
    const bool menu = definition.value("kind").toString() == "menu";
    const bool tabs = definition.value("kind").toString() == "tabs";
    const bool settings = definition.value("kind").toString() == "settings";
    QSet<QString> keys;
    QSet<QString> checked;
    for (const auto &value : entries.toList()) {
        if (value.metaType().id() != QMetaType::QVariantMap)
            return fail("Expected a model entry object");
        const auto entry = value.toMap();
        for (const auto &field : entry.keys())
            if (!QStringList{"key",       "component", "width",    "height",        "label",
                             "enabled",   "values",    "command",  "arguments",     "separator",
                             "checkable", "checked",   "danger",   "contentTarget", "glyph",
                             "keywords",  "operation", "position", "anchor",        "target",
                             "group",     "children"}
                     .contains(field))
                return fail("Unknown model entry field: " + field);
        const auto operation = entry.value("operation", "insert").toString();
        const auto position = entry.value("position", "append").toString();
        if (!QStringList{"insert", "hide", "move", "replace"}.contains(operation) ||
            !QStringList{"append", "prepend", "before", "after"}.contains(position))
            return fail("Invalid model structure operation or position");
        for (const auto *field : {"anchor", "target", "group"})
            if (entry.contains(field) &&
                (entry.value(field).metaType().id() != QMetaType::QString ||
                 entry.value(field).toString().size() > 512))
                return fail("Model structure references must be bounded strings");
        if ((position == "before" || position == "after") &&
            entry.value("anchor").toString().isEmpty())
            return fail("Relative model placement requires an anchor");
        if (operation != "insert" && entry.value("target").toString().isEmpty())
            return fail("Model modification requires a target");
        const bool structuralOnly = operation == "hide" || operation == "move";
        for (const auto &dimension : {QStringLiteral("width"), QStringLiteral("height")})
            if (entry.contains(dimension)) {
                const auto size = entry.value(dimension);
                if ((size.metaType().id() != QMetaType::Int &&
                     size.metaType().id() != QMetaType::Double) ||
                    !std::isfinite(size.toDouble()) || size.toDouble() <= 0 ||
                    size.toDouble() > 16384)
                    return fail("Entry dimensions must be finite numbers in (0, 16384]");
            }
        if (entry.contains("label") &&
            (entry.value("label").metaType().id() != QMetaType::QString ||
             entry.value("label").toString().size() > 4096))
            return fail("Entry label must be a bounded string");
        const auto key = entry.value("key").toString(),
                   component = entry.value("component").toString();
        if (!QRegularExpression("^[a-zA-Z0-9_.-]{1,128}$").match(key).hasMatch() ||
            keys.contains(key))
            return fail("Model entry keys must be unique stable identifiers");
        keys.insert(key);
        if (menu) {
            for (const auto *flag : {"separator", "checkable", "checked", "danger"})
                if (entry.contains(flag) && entry.value(flag).metaType().id() != QMetaType::Bool)
                    return fail("Menu flags must be boolean");
            if (entry.contains("arguments") &&
                entry.value("arguments").metaType().id() != QMetaType::QVariantMap)
                return fail("Menu arguments must be an object");
            if (!structuralOnly && !entry.contains("children") &&
                !entry.value("separator", false).toBool() &&
                (entry.value("label").toString().isEmpty() ||
                 !QRegularExpression("^[a-zA-Z0-9_.:-]{1,128}$")
                      .match(entry.value("command").toString())
                      .hasMatch()))
                return fail("Menu actions require a label and a registered plugin command");
        }
        for (const auto *field : {"glyph", "keywords"})
            if (entry.contains(field) &&
                (entry.value(field).metaType().id() != QMetaType::QString ||
                 entry.value(field).toString().size() > 4096))
                return fail("Model labels and keywords must be bounded strings");
        if (!structuralOnly && (tabs || settings) && entry.value("label").toString().isEmpty())
            return fail("Tabs and settings sections require a label");
        if (!structuralOnly && tabs &&
            (entry.value("label").toString().isEmpty() ||
             entry.value("contentTarget").metaType().id() != QMetaType::QVariantMap ||
             !EchoUiPartRegistry::validSelector(entry.value("contentTarget").toMap()) ||
             entry.value("contentTarget").toMap().isEmpty()))
            return fail("Tabs require a label and a unique public content target selector");
        if (entry.contains("children")) {
            if (!menu || entry.value("children").metaType().id() != QMetaType::QVariantList ||
                entry.value("label").toString().isEmpty())
                return fail("Submenus require a menu label and child entries");
            QString nestedError;
            // modelJson already bounds the entire definition's recursion and size.
            if (!validate(owner, {{"kind", "menu"}, {"entries", entry.value("children")}},
                          nestedError))
                return fail(nestedError);
        }
        const auto path = owner.scope().package().resolve(component);
        if (!structuralOnly && !menu &&
            (component.isEmpty() || !component.endsWith(".qml") || path.isEmpty()))
            return fail("Model entry requires a package QML component");
        if (!structuralOnly && !menu && !checked.contains(path)) {
            QQmlComponent qml(&owner.scope().engine(), QUrl::fromLocalFile(path));
            if (qml.isError())
                return fail(qml.errorString());
            checked.insert(path);
        }
        if (entry.contains("enabled") && entry.value("enabled").metaType().id() != QMetaType::Bool)
            return fail("Entry enabled must be boolean");
        if (entry.contains("values") &&
            entry.value("values").metaType().id() != QMetaType::QVariantMap)
            return fail("Entry values must be an object");
    }
    error.clear();
    return true;
}
bool EchoUiModelContribution::update(const QVariantMap &definition)
{
    QString error;
    if (!active() || !validate(*m_owner, definition, error, m_provider.isCallable())) {
        m_error = error;
        emit changed();
        return false;
    }
    if (definition == m_definition && m_error.isEmpty())
        return true;
    setProperty("_echoUiRejectedSnapshot", QVariant());
    m_requested = definition;
    for (auto &instance : m_instances) {
        if (instance.providerRequest)
            instance.providerRequest->cancel();
        instance.needsProvider = true;
        if (!instance.runtimeError.isEmpty() && instance.recipe) {
            instance.runtimeError.clear();
            emit instance.recipe->changed();
        }
    }
    m_error.clear();
    schedule();
    return true;
}
void EchoUiModelContribution::cancel()
{
    m_requested.reset();
    for (auto &instance : m_instances) {
        const auto request = instance.providerRequest;
        instance.providerRequest = nullptr;
        if (request)
            request->cancel();
        if (instance.providedDefinition.isEmpty())
            instance.providedDefinition = instance.priorDefinition;
        instance.resolved = !instance.providedDefinition.isEmpty();
        instance.needsProvider = false;
        instance.providerError.clear();
        if (instance.recipe)
            instance.recipe->discardPrepared();
    }
    m_error.clear();
    schedule();
    emit changed();
}
void EchoUiModelContribution::dispose()
{
    if (m_lease)
        m_lease->dispose();
}
void EchoUiModelContribution::start()
{
    if (m_started || !m_registry || !active())
        return;
    m_started = true;
    m_registry->acquireStructureFor(this, m_selector);
    m_connections << connect(m_registry, &EchoUiPartRegistry::structureChanged, this,
                             &EchoUiModelContribution::schedule)
                  << connect(m_registry, &EchoUiPartRegistry::preferencesChanged, this,
                             &EchoUiModelContribution::schedule)
                  << connect(m_registry, &EchoUiPartRegistry::added, this,
                             [this](auto *part) {
                                 if (EchoUiPartRegistry::matches(*part, m_selector))
                                     schedule();
                             })
                  << connect(m_registry, &EchoUiPartRegistry::removed, this,
                             [this](const auto &id) {
                                 if (m_instances.contains(id))
                                     schedule();
                             });
    m_connections << connect(m_registry, &EchoUiPartRegistry::partChanged, this,
                             [this](auto *part) {
                                 if (m_instances.contains(part->identity()))
                                     schedule();
                             })
                  << connect(&m_owner->scope().host().router, &RouteController::routeChanged, this,
                             &EchoUiModelContribution::schedule);
    schedule();
}
void EchoUiModelContribution::stop()
{
    m_requested.reset();
    for (const auto &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    m_started = false;
    const auto instances = std::exchange(m_instances, {});
    for (const auto &instance : instances) {
        if (instance.providerRequest)
            instance.providerRequest->cancel();
        if (instance.recipe) {
            instance.recipe->retire();
            instance.recipe->deleteLater();
        }
        if (instance.adapter)
            instance.adapter->remove(instance.token);
    }
    if (m_registry)
        m_registry->releaseStructure(this);
    m_matches.clear();
    emit changed();
}
void EchoUiModelContribution::schedule()
{
    if (std::exchange(m_pending, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_pending = false;
        reconcile();
    });
}
void EchoUiModelContribution::ensureAdapter(const QString &id, EchoUiPartRegistration &part,
                                            Instance &instance)
{
    if (instance.adapter)
        return;
    auto *adapter = EchoUiModelAdapter::forView(part.target(), m_owner->scope().engine(), true);
    instance.adapter = adapter;
    if (!adapter)
        return;
    connect(adapter, &EchoUiModelAdapter::projectionErrorChanged, this,
            [this, id, adapter](const QString &error) {
                const auto found = m_instances.find(id);
                if (found == m_instances.end() || found->adapter != adapter)
                    return;
                for (auto &match : m_matches) {
                    auto value = match.toMap();
                    if (value.value("id") == id) {
                        value["state"] = error.isEmpty() ? "active" : "error";
                        value["error"] = error;
                        match = value;
                    }
                }
                m_error = error;
                adapter->setStatus(found->token, {{"owner", m_owner->scope().id()},
                                                  {"id", m_id},
                                                  {"kind", m_definition.value("kind")},
                                                  {"state", error.isEmpty() ? "active" : "error"},
                                                  {"error", error},
                                                  {"revision", revision()}});
                emit changed();
            });
}
void EchoUiModelContribution::requestContext(const QString &id, EchoUiPartRegistration &part,
                                             Instance &instance, const QVariantMap &context)
{
    if (instance.providerRequest)
        instance.providerRequest->cancel();
    instance.priorDefinition =
        instance.context == context ? instance.providedDefinition : QVariantMap();
    instance.context = context;
    instance.needsProvider = false;
    instance.resolved = false;
    instance.providerError.clear();
    instance.providedDefinition.clear();
    if (instance.recipe)
        instance.recipe->setContext(context);
    if (!context.value("active", true).toBool())
        return;
    auto *operation = m_owner->scope().invokeRegistered(m_provider, m_lease, context);
    instance.providerRequest = operation;
    const auto baseDefinition = m_requested.value_or(m_definition);
    const auto complete = [this, id, context, operation, baseDefinition] {
        auto it = m_instances.find(id);
        if (it == m_instances.end() || it->context != context || it->providerRequest != operation)
            return;
        it->providerRequest = nullptr;
        it->resolved = true;
        if (operation->state() != EchoPluginOperation::Succeeded) {
            it->providerError = operation->errorMessage().isEmpty()
                                    ? "Model context provider canceled"
                                    : operation->errorMessage();
        } else {
            auto definition = baseDefinition;
            const auto result = operation->result();
            if (!result.isValid() || result.isNull())
                definition["entries"] = QVariantList();
            else if (result.metaType().id() == QMetaType::QVariantList)
                definition["entries"] = result;
            else if (result.metaType().id() == QMetaType::QVariantMap)
                for (auto values = result.toMap(); const auto &key : values.keys())
                    definition[key] = values.value(key);
            else
                it->providerError = "Model provider must return entries, a definition, or null";
            if (it->providerError.isEmpty() &&
                (definition.value("kind") != baseDefinition.value("kind") ||
                 !validate(*m_owner, definition, it->providerError, true))) {
                if (it->providerError.isEmpty())
                    it->providerError = "Model provider cannot change projection kind";
            }
            if (it->providerError.isEmpty())
                it->providedDefinition = definition;
        }
        schedule();
    };
    connect(operation, &EchoPluginOperation::completed, this, complete);
    if (operation->finished())
        complete();
    QTimer::singleShot(15000, operation, [operation] {
        if (!operation->finished())
            operation->fail("TIMEOUT", "Model context provider timed out");
    });
    Q_UNUSED(part);
}
void EchoUiModelContribution::reconcile()
{
    if (!active() || !m_started || !m_registry || m_applying)
        return;
    m_applying = true;
    auto parts = m_registry->query(m_selector);
    parts.removeIf([this](auto *part) {
        return part->surface() == "inspector" || part->owner() == m_owner->scope().id();
    });
    const auto requested = m_requested.value_or(m_definition);
    struct Candidate
    {
        QString id;
        EchoUiModelAdapter::Record record, previous;
        QVariantMap context;
        QPointer<EchoUiPartRegistration> part;
    };
    QList<Candidate> candidates;
    QVariantList matches;
    QSet<QString> keep;
    QString failure;
    if (parts.size() > 256)
        failure = "Model contribution exceeds 256 live targets";
    QList<QPointer<EchoUiPartRegistration>> targets;
    for (auto *part : parts)
        targets.append(part);
    for (auto guard : targets) {
        auto *part = guard.data();
        if (!part)
            continue;
        if (!failure.isEmpty())
            break;
        if (!m_registry->allowed(part, m_owner->scope().id(), m_id))
            continue;
        const auto id = part->identity();
        keep.insert(id);
        if (!m_instances.contains(id)) {
            const auto token = m_lease->key() + ':' + m_lease->revision() + ':' + id;
            auto *recipe = new EchoUiPluginModelRecipe(*m_owner, *part, token, this);
            Instance created;
            created.recipe = recipe;
            created.token = token;
            m_instances[id] = created;
            connect(recipe, &EchoUiPluginModelRecipe::constructionFailed, this,
                    [this, id](const QString &key, const QString &error) {
                        m_error = "Model cell " + key + ": " + error;
                        m_instances[id].runtimeError = m_error;
                        if (auto adapter = m_instances.value(id).adapter)
                            adapter->setStatus(
                                m_instances.value(id).token,
                                {{"state", "error"}, {"error", m_error}, {"revision", revision()}});
                        for (auto &match : m_matches) {
                            auto value = match.toMap();
                            if (value.value("id") == id) {
                                value["state"] = "error";
                                value["error"] = m_error;
                                match = value;
                            }
                        }
                        emit changed();
                    });
        }
        auto &instance = m_instances[id];
        ensureAdapter(id, *part, instance);
        const auto context = currentModelContext(*m_owner, *part);
        auto definition = requested;
        if (m_provider.isCallable()) {
            if (instance.context != context || instance.needsProvider) {
                if (instance.context != context) {
                    if (instance.adapter)
                        instance.adapter->remove(instance.token);
                    instance.adapter = nullptr;
                }
                requestContext(id, *part, instance, context);
            }
            if (!instance.resolved || !instance.providerError.isEmpty()) {
                matches.append(
                    QVariantMap{{"id", id},
                                {"address", part->address()},
                                {"kind", requested.value("kind")},
                                {"state", !context.value("active", true).toBool() ? "suspended"
                                          : instance.providerRequest              ? "preparing"
                                          : instance.providerError.isEmpty()      ? "suspended"
                                                                                  : "error"},
                                {"error", instance.providerError}});
                continue;
            }
            definition = instance.providedDefinition;
        }
        ensureAdapter(id, *part, instance);
        Candidate candidate;
        candidate.id = id;
        candidate.context = context;
        candidate.part = part;
        auto &record = candidate.record;
        record.token = instance.token;
        record.owner = m_owner->scope().id();
        record.id = m_id;
        record.kind = definition.value("kind").toString();
        record.priority = definition.value("priority", 0).toInt();
        record.rank = m_registry->preferenceRank(part, record.owner, record.id);
        record.recipe = instance.recipe;
        for (const auto &value : definition.value("entries").toList()) {
            const auto entry = value.toMap();
            auto values = entry.value("values").toMap();
            for (auto field = entry.begin(); field != entry.end(); ++field)
                if (field.key() != "values")
                    values[field.key()] = field.value();
            const auto key = entry.value("key").toString();
            record.entries.append({key, record.owner, record.id, instance.token + ':' + key, values,
                                   entry.value("enabled", true).toBool()});
        }
        if (!instance.adapter || !instance.adapter->canPut(record, failure)) {
            if (failure.isEmpty())
                failure = "Unsupported model target";
            break;
        }
        candidate.previous = instance.adapter->record(record.token);
        if (candidate.previous != record) {
            for (const auto &entry : record.entries) {
                const auto old = std::find_if(
                    candidate.previous.entries.begin(), candidate.previous.entries.end(),
                    [&](const auto &value) {
                        return value.key == entry.key &&
                               value.values.value("component") == entry.values.value("component");
                    });
                if (old == candidate.previous.entries.end() &&
                    !instance.recipe->prepare(entry.values, failure))
                    break;
            }
        }
        if (!guard || guard->identity() != id || !active()) {
            failure = "Model target or activation retired during preparation";
            break;
        }
        candidates.append(candidate);
        matches.append(QVariantMap{{"id", id},
                                   {"address", part->address()},
                                   {"kind", record.kind},
                                   {"state", record.entries.isEmpty() ? "suspended" : "active"},
                                   {"error", ""}});
    }
    bool waiting = false;
    QString providerFailure;
    for (const auto &instance : m_instances) {
        waiting = waiting || bool(instance.providerRequest);
        if (providerFailure.isEmpty())
            providerFailure = instance.providerError;
    }
    if (m_requested && (waiting || !providerFailure.isEmpty())) {
        for (auto &instance : m_instances)
            if (instance.recipe)
                instance.recipe->discardPrepared();
        if (!providerFailure.isEmpty()) {
            for (auto &instance : m_instances) {
                auto request = instance.providerRequest;
                instance.providerRequest = nullptr;
                if (request)
                    request->cancel();
                instance.providedDefinition = instance.priorDefinition;
                instance.resolved = !instance.providedDefinition.isEmpty();
            }
            m_requested.reset();
        }
        m_matches = matches;
        m_error = providerFailure;
        m_applying = false;
        emit changed();
        return;
    }
    if (m_prepareOnly) {
        m_error = failure;
        m_matches = matches;
        m_applying = false;
        emit changed();
        return;
    }
    QList<QPointer<EchoUiModelAdapter>> adapters;
    for (const auto &candidate : candidates) {
        auto adapter = m_instances[candidate.id].adapter;
        if (adapter && !adapters.contains(adapter)) {
            adapter->beginPublication();
            adapters.append(adapter);
        }
    }
    QList<Candidate> published;
    if (failure.isEmpty()) {
        m_registry->beginPublication();
        for (const auto &candidate : candidates) {
            if (!candidate.part || m_registry->find(candidate.id) != candidate.part || !active()) {
                failure = "Model target or activation retired before publication";
                break;
            }
            auto &instance = m_instances[candidate.id];
            instance.recipe->setContext(candidate.context);
            if (!instance.adapter->put(candidate.record, failure))
                break;
            published.append(candidate);
        }
        if (!failure.isEmpty()) {
            for (const auto &candidate : published) {
                auto &instance = m_instances[candidate.id];
                QString ignored;
                if (candidate.previous.token.isEmpty()) {
                    instance.adapter->remove(candidate.record.token);
                    instance.adapter = nullptr;
                } else
                    instance.adapter->put(candidate.previous, ignored);
            }
        }
        m_registry->endPublication();
    }
    if (failure.isEmpty())
        for (auto &instance : m_instances)
            if (instance.recipe)
                instance.recipe->commitPrepared();
    for (auto adapter : adapters)
        if (adapter)
            adapter->endPublication(failure.isEmpty());
    if (!failure.isEmpty())
        for (auto &instance : m_instances)
            if (instance.recipe)
                instance.recipe->discardPrepared();
    if (failure.isEmpty()) {
        for (auto it = m_instances.begin(); it != m_instances.end();) {
            if (keep.contains(it.key())) {
                ++it;
                continue;
            }
            if (it->providerRequest)
                it->providerRequest->cancel();
            if (it->recipe) {
                it->recipe->retire(true);
                it->recipe->deleteLater();
            }
            if (it->adapter)
                it->adapter->remove(it->token);
            it = m_instances.erase(it);
        }
        if (m_requested || (m_revision == 0 && !waiting)) {
            m_definition = requested;
            ++m_revision;
        }
        m_matches = matches;
    }
    if (!failure.isEmpty()) {
        QVariantList rejected = m_matches;
        for (auto part : targets) {
            if (!part || rejected.size() >= 256)
                continue;
            bool found = false;
            for (auto &value : rejected) {
                auto match = value.toMap();
                if (match.value("id") == part->identity()) {
                    match["error"] = failure;
                    value = match;
                    found = true;
                    break;
                }
            }
            if (!found)
                rejected.append(QVariantMap{{"id", part->identity()},
                                            {"address", part->address()},
                                            {"kind", requested.value("kind")},
                                            {"state", "error"},
                                            {"error", failure}});
        }
        m_matches = rejected;
    }
    for (auto &instance : m_instances)
        if (instance.adapter)
            instance.adapter->setStatus(instance.token,
                                        {{"owner", m_owner->scope().id()},
                                         {"id", m_id},
                                         {"kind", requested.value("kind")},
                                         {"state", failure.isEmpty() ? "active" : "error"},
                                         {"error", failure},
                                         {"revision", revision()}});
    m_requested.reset();
    m_error = failure.isEmpty() ? providerFailure : failure;
    for (auto &match : m_matches) {
        auto value = match.toMap();
        const auto instance = m_instances.value(value.value("id").toString());
        if (!instance.runtimeError.isEmpty()) {
            value["state"] = "error";
            value["error"] = instance.runtimeError;
            match = value;
            if (m_error.isEmpty())
                m_error = instance.runtimeError;
        }
    }
    m_applying = false;
    emit changed();
}
} // namespace EchoMusic
