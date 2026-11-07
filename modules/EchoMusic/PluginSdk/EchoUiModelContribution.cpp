#include "EchoUiModelContribution.h"
#include "EchoPluginUiParts.h"
#include "EchoPluginContext.h"
#include "EchoUiContribution.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiPartRegistry.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
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
} // namespace
EchoUiPluginModelRecipe::EchoUiPluginModelRecipe(EchoPluginUiParts &owner,
                                                 EchoUiPartRegistration &part, QString key,
                                                 QObject *parent)
    : EchoUiModelRecipe(parent), m_owner(&owner), m_part(&part), m_identity(part.identity()),
      m_key(std::move(key))
{
}
bool EchoUiPluginModelRecipe::active() const
{
    return !m_retired && m_owner && m_owner->scope().active() && m_part &&
           m_part->identity() == m_identity;
}
bool EchoUiPluginModelRecipe::activate(const QVariantMap &entry, const QVariantMap &event)
{
    if (!active() || !entry.value("enabled", true).toBool())
        return false;
    auto arguments = entry.value("arguments").toMap();
    for (auto it = event.begin(); it != event.end(); ++it)
        arguments[it.key()] = it.value();
    return m_owner->scope().executeCommand(entry.value("command").toString(), arguments) != nullptr;
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
void EchoUiPluginModelRecipe::retire()
{
    if (!std::exchange(m_retired, true)) {
        select({});
        emit changed();
    }
}
EchoUiContentFactory *EchoUiPluginModelRecipe::factory(QObject *owner, const QVariantMap &row,
                                                       const QVariantMap &entry)
{
    if (!active())
        return nullptr;
    const auto component = entry.value("component").toString();
    auto *factory = new EchoUiPluginContentFactory(
        m_owner->scope(), *m_part, m_owner->scope().package().resolve(component), owner, false,
        "model:" + m_key + ':' + entry.value("key").toString());
    factory->setModelData(row, entry);
    return factory;
}
EchoUiModelContribution::EchoUiModelContribution(EchoPluginUiParts &owner,
                                                 EchoPluginRegistration &lease, QString id,
                                                 QVariantMap selector, QVariantMap definition)
    : QObject(&lease), m_owner(&owner), m_lease(&lease), m_registry(owner.registry()),
      m_id(std::move(id)), m_selector(std::move(selector)), m_definition(std::move(definition))
{
}
bool EchoUiModelContribution::active() const
{
    return m_owner && m_owner->scope().active() && m_lease && m_lease->active();
}
bool EchoUiModelContribution::validate(EchoPluginUiParts &owner, const QVariantMap &definition,
                                       QString &error)
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
    if (entries.metaType().id() != QMetaType::QVariantList || entries.toList().isEmpty() ||
        entries.toList().size() > 256 ||
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
            if (!QStringList{"key", "component", "width", "height", "label", "enabled", "values",
                             "command", "arguments", "separator", "checkable", "checked", "danger",
                             "contentTarget", "glyph", "keywords"}
                     .contains(field))
                return fail("Unknown model entry field: " + field);
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
            if (!entry.value("separator", false).toBool() &&
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
        if ((tabs || settings) && entry.value("label").toString().isEmpty())
            return fail("Tabs and settings sections require a label");
        if (tabs && (entry.value("label").toString().isEmpty() ||
                     entry.value("contentTarget").metaType().id() != QMetaType::QVariantMap ||
                     !EchoUiPartRegistry::validSelector(entry.value("contentTarget").toMap()) ||
                     entry.value("contentTarget").toMap().isEmpty()))
            return fail("Tabs require a label and a unique public content target selector");
        const auto path = owner.scope().package().resolve(component);
        if (!menu && (component.isEmpty() || !component.endsWith(".qml") || path.isEmpty()))
            return fail("Model entry requires a package QML component");
        if (!menu && !checked.contains(path)) {
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
    if (!active() || !validate(*m_owner, definition, error)) {
        m_error = error;
        emit changed();
        return false;
    }
    if (definition == m_definition && m_error.isEmpty())
        return true;
    setProperty("_echoUiRejectedSnapshot", QVariant());
    m_definition = definition;
    m_error.clear();
    schedule();
    return true;
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
    m_registry->acquireStructure(this);
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
    schedule();
}
void EchoUiModelContribution::stop()
{
    for (const auto &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    m_started = false;
    const auto instances = std::exchange(m_instances, {});
    for (const auto &instance : instances) {
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
void EchoUiModelContribution::reconcile()
{
    if (!active() || !m_started || !m_registry || m_applying)
        return;
    m_applying = true;
    auto parts = m_registry->query(m_selector);
    parts.removeIf([this](auto *part) {
        return part->surface() == "inspector" || part->owner() == m_owner->scope().id();
    });
    const auto snapshot = [&] {
        QStringList values;
        for (auto *part : parts) {
            auto *view = part->target();
            auto *adapter = EchoUiModelAdapter::forView(view, m_owner->scope().engine(), false);
            values.append(part->identity() + ':' + view->property("count").toString() + ':' +
                          view->property("rows").toString() + ':' +
                          view->property("columns").toString() + ':' +
                          QString::number(adapter ? adapter->entries().size() : 0));
        }
        values.sort();
        return values.join('|');
    };
    if (!m_error.isEmpty() && property("_echoUiRejectedSnapshot").toString() == snapshot()) {
        m_applying = false;
        return;
    }
    if (parts.size() > 256) {
        m_error = "Model contribution exceeds 256 live targets";
        const auto instances = std::exchange(m_instances, {});
        for (const auto &instance : instances) {
            if (instance.recipe) {
                instance.recipe->retire();
                instance.recipe->deleteLater();
            }
            if (instance.adapter)
                instance.adapter->remove(instance.token);
        }
        m_matches.clear();
        setProperty("_echoUiRejectedSnapshot", snapshot());
        m_applying = false;
        emit changed();
        return;
    }
    bool failed = false;
    QString failure;
    QSet<QString> keep;
    QVariantList matches;
    for (auto *part : parts) {
        if (!m_registry->allowed(part, m_owner->scope().id(), m_id))
            continue;
        const auto id = part->identity();
        keep.insert(id);
        if (!m_instances.contains(id)) {
            const auto token = m_lease->key() + ':' + m_lease->revision() + ':' + id;
            m_instances[id] = {nullptr, new EchoUiPluginModelRecipe(*m_owner, *part, token, this),
                               token};
        }
        auto &instance = m_instances[id];
        if (!instance.adapter)
            instance.adapter =
                EchoUiModelAdapter::forView(part->target(), m_owner->scope().engine(), true);
        EchoUiModelAdapter::Record record;
        record.token = instance.token;
        record.owner = m_owner->scope().id();
        record.id = m_id;
        record.kind = m_definition.value("kind").toString();
        record.priority = m_definition.value("priority", 0).toInt();
        record.rank = m_registry->preferenceRank(part, record.owner, record.id);
        record.recipe = instance.recipe;
        for (const auto &value : m_definition.value("entries").toList()) {
            const auto entry = value.toMap();
            auto values = entry.value("values").toMap();
            for (auto field = entry.begin(); field != entry.end(); ++field)
                if (field.key() != "values")
                    values[field.key()] = field.value();
            const auto key = entry.value("key").toString();
            record.entries.append({key, record.owner, record.id, instance.token + ':' + key, values,
                                   entry.value("enabled", true).toBool()});
        }
        QString error;
        const bool success = instance.adapter && instance.adapter->put(record, error);
        if (!success) {
            failed = true;
            failure = error.isEmpty() ? "Unsupported model target" : error;
        }
        matches.append(QVariantMap{{"id", id},
                                   {"address", part->address()},
                                   {"kind", record.kind},
                                   {"state", success ? "active" : "unsupported"},
                                   {"error", error}});
    }
    for (auto it = m_instances.begin(); it != m_instances.end();) {
        if (keep.contains(it.key())) {
            ++it;
            continue;
        }
        if (it->recipe) {
            it->recipe->retire();
            it->recipe->deleteLater();
        }
        if (it->adapter)
            it->adapter->remove(it->token);
        it = m_instances.erase(it);
    }
    if (failed) {
        for (const auto &instance : std::as_const(m_instances)) {
            if (instance.recipe) {
                instance.recipe->retire();
                instance.recipe->deleteLater();
            }
            if (instance.adapter)
                instance.adapter->remove(instance.token);
        }
        m_instances.clear();
        for (auto &match : matches) {
            auto descriptor = match.toMap();
            descriptor["state"] = "error";
            descriptor["error"] = failure;
            match = descriptor;
        }
    }
    setProperty("_echoUiRejectedSnapshot", failed ? QVariant(snapshot()) : QVariant());
    m_error = failed ? failure : QString();
    m_matches = matches;
    m_applying = false;
    emit changed();
}
} // namespace EchoMusic
