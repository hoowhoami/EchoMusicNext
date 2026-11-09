#include "EchoUiBatch.h"
#include "EchoPluginUiParts.h"
#include "EchoPluginContext.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiScope.h"
#include "EchoUiLayout.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <QQuickWindow>
#include <functional>
#include <algorithm>
namespace EchoMusic
{
EchoUiBatch::EchoUiBatch(EchoPluginUiParts &owner, EchoPluginRegistration &lease, QString id,
                         QVariantList entries)
    : QObject(&lease), m_owner(&owner), m_lease(&lease), m_registry(owner.registry()),
      m_id(std::move(id)), m_definition(std::move(entries))
{
}
EchoUiBatch::~EchoUiBatch()
{
    stop();
}
bool EchoUiBatch::active() const
{
    return m_owner && m_owner->scope().active() && m_lease && m_lease->active();
}
bool EchoUiBatch::validate(EchoPluginUiParts &owner, const QVariantList &entries, QString &error)
{
    if (entries.isEmpty() || entries.size() > 64 ||
        QJsonDocument::fromVariant(entries).toJson(QJsonDocument::Compact).size() > 1024 * 1024) {
        error = "Expected 1..64 batch entries within 1 MiB";
        return false;
    }
    QSet<QString> ids;
    for (const auto &value : entries) {
        if (value.metaType().id() != QMetaType::QVariantMap) {
            error = "Batch entries must be objects";
            return false;
        }
        const auto entry = value.toMap();
        for (const auto &key : entry.keys())
            if (!QStringList{"id", "selector", "definition", "style", "model", "priority"}.contains(
                    key)) {
                error = "Unknown batch entry field: " + key;
                return false;
            }
        const auto id = entry.value("id").toString();
        if (!QRegularExpression("^[a-zA-Z0-9_.-]{1,64}$").match(id).hasMatch() ||
            ids.contains(id)) {
            error = "Batch entry IDs must be unique stable identifiers";
            return false;
        }
        ids.insert(id);
        if (entry.value("selector").metaType().id() != QMetaType::QVariantMap ||
            !EchoUiPartRegistry::validSelector(entry.value("selector").toMap())) {
            error = "Invalid batch target selector";
            return false;
        }
        if (int(entry.contains("style")) + int(entry.contains("definition")) +
                int(entry.contains("model")) !=
            1) {
            error = "A batch entry requires exactly one of style, definition, or model";
            return false;
        }
        if (entry.contains("model")) {
            if (entry.contains("priority") ||
                entry.value("model").metaType().id() != QMetaType::QVariantMap ||
                !EchoUiModelContribution::validate(owner, entry.value("model").toMap(), error))
                return false;
        } else if (entry.contains("style")) {
            auto style = entry.value("style").toMap();
            const auto priority = entry.value("priority", 0);
            if (entry.value("style").metaType().id() != QMetaType::QVariantMap || style.isEmpty() ||
                (priority.metaType().id() != QMetaType::Int &&
                 priority.metaType().id() != QMetaType::Double) ||
                priority.toDouble() != priority.toInt() || priority.toInt() < -10000 ||
                priority.toInt() > 10000 || !EchoUiPartRegistry::normalize(style, error)) {
                if (error.isEmpty())
                    error = "Invalid batch style or priority";
                return false;
            }
        } else if (entry.contains("priority") ||
                   entry.value("definition").metaType().id() != QMetaType::QVariantMap ||
                   !EchoUiContribution::validate(owner.scope(), entry.value("definition").toMap(),
                                                 error)) {
            if (error.isEmpty())
                error = "Invalid batch contribution definition";
            return false;
        }
    }
    error.clear();
    return true;
}
void EchoUiBatch::start()
{
    update(m_definition);
}
void EchoUiBatch::stop()
{
    if (m_committing)
        return;
    if (m_registry)
        m_registry->beginPublication();
    clear(m_preparing);
    clear(m_current);
    if (m_registry) {
        m_registry->endPublication();
        m_registry->releaseStructure(this);
    }
    emit changed();
}
void EchoUiBatch::dispose()
{
    if (m_lease)
        m_lease->dispose();
}
void EchoUiBatch::cancel()
{
    if (m_committing || !m_preparing)
        return;
    clear(m_preparing);
    refreshStructureDemand();
    m_error.clear();
    emit changed();
}
void EchoUiBatch::clear(std::unique_ptr<Revision> &revision)
{
    if (!revision)
        return;
    auto retiring = std::move(revision);
    for (auto &target : retiring->targets) {
        if (target.lease)
            target.lease->dispose();
        if (target.contribution)
            target.contribution->stop();
        if (target.modelContribution)
            target.modelContribution->stop();
        if (!target.style.isEmpty() && m_registry)
            m_registry->removeStyle(target.token);
        if (target.factory)
            target.factory->discardPrepared();
        if (target.stage) {
            target.stage->setVisible(false);
            target.stage->setParentItem(nullptr);
        }
    }
    if (retiring->owner)
        retiring->owner->deleteLater();
}
bool EchoUiBatch::needsDiscovery(const QVariantList &entries) const
{
    return std::any_of(entries.cbegin(), entries.cend(), [this](const auto &value) {
        const auto entry = value.toMap();
        return m_registry->selectorNeedsDiscovery(entry.value("selector").toMap(),
                                                  entry.value("definition").toMap());
    });
}
void EchoUiBatch::refreshStructureDemand()
{
    if (!m_registry)
        return;
    if (!m_current && !m_preparing)
        m_registry->releaseStructure(this);
    else
        m_registry->retainStructure(this,
                                    (m_current && needsDiscovery(m_current->entries)) ||
                                        (m_preparing && needsDiscovery(m_preparing->entries)));
}
bool EchoUiBatch::update(const QVariantList &entries)
{
    QString error;
    if (!active() || !m_registry || m_committing || m_preparing ||
        !validate(*m_owner, entries, error)) {
        m_error = error.isEmpty() ? "Batch is closed or another update is being prepared" : error;
        emit changed();
        return false;
    }
    m_registry->acquireStructure(
        this, false, needsDiscovery(entries) || (m_current && needsDiscovery(m_current->entries)));
    auto revision = std::make_unique<Revision>();
    revision->owner = new QObject(this);
    revision->entries = entries;
    revision->number = ++m_attempt;
    revision->elapsed.start();
    for (const auto &value : entries) {
        const auto entry = value.toMap(), selector = entry.value("selector").toMap();
        auto parts = m_registry->query(selector);
        parts.removeIf([this](auto *part) {
            return part->surface() == "inspector" || part->owner() == m_owner->scope().id();
        });
        if (parts.isEmpty() || revision->targets.size() + parts.size() > 256) {
            error = "Batch target snapshot requires 1..256 live targets";
            break;
        }
        if (entry.contains("definition")) {
            const auto preflight = m_owner->preflight(selector, entry.value("definition").toMap());
            if (!preflight.value("valid").toBool()) {
                error = preflight.value("error").toString();
                if (error.isEmpty())
                    error = "One or more batch targets are incompatible";
                break;
            }
        }
        for (auto *part : parts) {
            Target target;
            target.entry = entry.value("id").toString();
            target.logicalId = m_id + '.' + target.entry;
            target.identity = part->identity();
            target.part = part;
            target.item = part->target();
            target.token = m_lease->key() + ":batch:" + QString::number(revision->number) + ':' +
                           target.entry + ':' + target.identity;
            target.selector = selector;
            target.selector["id"] = target.identity;
            target.definition = entry.value("definition").toMap();
            target.style = entry.value("style").toMap();
            target.model = entry.value("model").toMap();
            target.priority = entry.value("priority", 0).toInt();
            if (!target.model.isEmpty()) {
                target.lease =
                    new EchoPluginRegistration(target.token, revision->number, revision->owner);
                target.modelContribution = new EchoUiModelContribution(
                    *m_owner, *target.lease, target.logicalId, target.selector, target.model);
                target.modelContribution->m_prepareOnly = true;
                auto *model = target.modelContribution.data();
                target.lease->stage(
                    [model] {
                        model->start();
                        return true;
                    },
                    [model] { model->stop(); });
                target.lease->commit();
                model->reconcile();
                error = model->errorMessage();
            } else if (!target.style.isEmpty()) {
                if (!EchoUiPartRegistry::normalize(target.style, error))
                    break;
                for (const auto &field : target.style.keys())
                    if (!part->properties().contains(field)) {
                        error = "Unsupported batch style field: " + field;
                        break;
                    }
            } else if (target.definition.value("operation").toString() == "move") {
                const auto destinations =
                    m_registry->query(target.definition.value("destination").toMap());
                if (destinations.size() != 1) {
                    error = "Batch move destination is ambiguous or absent";
                    break;
                }
                target.definition["destination"] =
                    QVariantMap{{"id", destinations.first()->identity()}};
            }
            if (!error.isEmpty())
                break;
            const auto operation = target.definition.value("operation", "insert").toString();
            bool condition = m_registry->allowed(part, m_owner->scope().id(), target.logicalId);
            for (auto when = target.definition.value("when").toMap(); const auto &key : when.keys())
                condition = condition && part->state().value(key) == when.value(key);
            if (target.style.isEmpty() && target.model.isEmpty() && condition &&
                part->presented() && QStringList{"insert", "wrap", "replace"}.contains(operation)) {
                target.stage = new QQuickItem;
                target.stage->setParent(revision->owner);
                target.stage->setProperty("_echoUiInternal", true);
                target.stage->setProperty("_echoUiStaging", true);
                target.stage->setVisible(false);
                target.stage->setSize(part->target()->size());
                EchoUiScope::get(target.stage, true)->setActive(false);
                target.stage->setParentItem(part->target()->window()->contentItem());
                target.factory = new EchoUiPluginContentFactory(
                    m_owner->scope(), *part,
                    m_owner->scope().package().resolve(
                        target.definition.value("component").toString()),
                    revision->owner,
                    operation == "insert" && QStringList{"before", "after"}.contains(
                                                 target.definition.value("position").toString()),
                    target.logicalId, true);
                connect(target.factory, &EchoUiContentFactory::changed, this,
                        &EchoUiBatch::schedule);
            }
            revision->targets.append(target);
        }
        if (!error.isEmpty())
            break;
    }
    if (error.isEmpty()) {
        QHash<QQuickItem *, QQuickItem *> parents;
        for (const auto &target : revision->targets)
            if (target.definition.value("operation") == "move") {
                const auto destinations =
                    m_registry->query(target.definition.value("destination").toMap());
                if (destinations.size() == 1)
                    parents[target.item] = destinations.first()->target();
            }
        for (auto source = parents.cbegin(); source != parents.cend(); ++source) {
            QSet<QQuickItem *> visited;
            for (auto *item = source.value(); item && !visited.contains(item);
                 item = parents.value(item, item->parentItem())) {
                if (item == source.key()) {
                    error = "Batch moves form a composition cycle";
                    break;
                }
                visited.insert(item);
            }
            if (!error.isEmpty())
                break;
        }
    }
    m_preparing = std::move(revision);
    m_error.clear();
    if (!error.isEmpty()) {
        reject(error);
        return false;
    }
    schedule();
    const auto attempt = m_attempt;
    QTimer::singleShot(30000, this, [this, attempt] {
        if (m_preparing && m_preparing->number == attempt)
            reject("Batch preparation timed out");
    });
    emit changed();
    return true;
}
void EchoUiBatch::schedule()
{
    if (std::exchange(m_scheduled, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_scheduled = false;
        prepare();
    });
}
void EchoUiBatch::prepare()
{
    if (!active() || !m_preparing || m_committing)
        return;
    for (auto &target : m_preparing->targets) {
        if (!target.part || m_registry->find(target.identity) != target.part ||
            target.part->target() != target.item || !target.item || !target.item->window()) {
            reject("Batch target retired during preparation");
            return;
        }
        if (target.modelContribution) {
            target.modelContribution->reconcile();
            if (!target.modelContribution->errorMessage().isEmpty()) {
                reject(target.modelContribution->errorMessage());
                return;
            }
        }
        if (target.factory) {
            if (target.factory->failed()) {
                reject(target.factory->errorMessage());
                return;
            }
            if (!target.factory->ready())
                return;
            if (!target.factory->prepare(target.stage)) {
                if (target.factory->failed())
                    reject(target.factory->errorMessage());
                return;
            }
        }
    }
    commit();
}
QSet<QString> EchoUiBatch::tokens(const Revision *revision) const
{
    QSet<QString> result;
    if (revision)
        for (const auto &target : revision->targets) {
            if (!target.style.isEmpty())
                result.insert(target.token);
            if (target.contribution)
                for (const auto &instance : target.contribution->m_instances)
                    result.insert(instance.token);
        }
    return result;
}
void EchoUiBatch::reject(const QString &message)
{
    clear(m_preparing);
    m_error = message;
    refreshStructureDemand();
    emit changed();
    emit rejected(message);
}
QVariantList EchoUiBatch::matches() const
{
    QVariantList result;
    if (!m_current)
        return result;
    for (const auto &target : m_current->targets) {
        if (target.modelContribution) {
            for (const auto &value : target.modelContribution->matches()) {
                auto match = value.toMap();
                match["entry"] = target.entry;
                result.append(match);
            }
        } else if (target.contribution)
            for (const auto &value : target.contribution->matches()) {
                auto match = value.toMap();
                match["entry"] = target.entry;
                result.append(match);
            }
        else
            result.append(QVariantMap{
                {"entry", target.entry},
                {"id", target.identity},
                {"state",
                 target.part && target.part->identity() == target.identity ? "active" : "retired"},
                {"error", ""}});
    }
    return result;
}
void EchoUiBatch::commit()
{
    if (!active() || !m_preparing || m_committing)
        return;
    int styles = 0, compositions = 0, moves = 0;
    for (const auto &target : m_preparing->targets) {
        if (!target.model.isEmpty())
            continue;
        if (!target.style.isEmpty())
            ++styles;
        else {
            ++compositions;
            if (target.definition.value("operation") == "move")
                ++moves;
            if (target.definition.value("operation") == "hide" ||
                !target.definition.value("layout").toMap().isEmpty())
                ++styles;
        }
    }
    if (!m_registry->canPublish(styles, compositions, moves)) {
        reject("UI publication capacity exceeded");
        return;
    }
    QList<QPointer<EchoUiContentHost>> held;
    QSet<QQuickItem *> seen;
    const auto hold = [&](auto &&self, QQuickItem *item) -> void {
        if (!item || seen.contains(item) || item->property("_echoUiStaging").toBool())
            return;
        seen.insert(item);
        if (auto *host = qobject_cast<EchoUiContentHost *>(item)) {
            host->beginPublicationHold();
            held.append(host);
        }
        for (auto *child : item->childItems())
            self(self, child);
    };
    for (const auto &target : m_preparing->targets)
        if (target.item && target.item->window())
            hold(hold, target.item->window()->contentItem());
    QList<QPointer<EchoUiModelAdapter>> modelAdapters;
    QSet<QString> oldModelTokens;
    if (m_current)
        for (const auto &target : m_current->targets)
            if (target.modelContribution)
                for (const auto &instance : target.modelContribution->m_instances) {
                    oldModelTokens.insert(instance.token);
                    if (instance.adapter && !modelAdapters.contains(instance.adapter))
                        modelAdapters.append(instance.adapter);
                }
    for (const auto &target : m_preparing->targets)
        if (target.modelContribution)
            for (const auto &instance : target.modelContribution->m_instances)
                if (instance.adapter && !modelAdapters.contains(instance.adapter))
                    modelAdapters.append(instance.adapter);
    for (auto adapter : modelAdapters)
        if (adapter)
            adapter->beginPublication(oldModelTokens);
    m_committing = true;
    const auto oldTokens = tokens(m_current.get());
    m_registry->beginPublication();
    m_registry->maskPublication(oldTokens, true);
    QString error;
    for (auto &target : m_preparing->targets) {
        if (target.modelContribution) {
            target.modelContribution->m_prepareOnly = false;
            target.modelContribution->reconcile();
            error = target.modelContribution->errorMessage();
        } else if (!target.style.isEmpty()) {
            if (!m_registry->putStyle(target.token, m_owner->scope().id(), target.logicalId,
                                      target.priority, target.selector, target.style)) {
                error = "UI style capacity exceeded";
                break;
            }
        } else {
            target.lease =
                new EchoPluginRegistration(target.token, m_preparing->number, m_preparing->owner);
            target.contribution = new EchoUiContribution(*m_owner, *target.lease, target.logicalId,
                                                         target.selector, target.definition);
            if (target.factory) {
                target.factory->publishPrepared();
                target.factory->setParent(target.contribution);
                target.contribution->m_preparedFactories[target.identity] = target.factory;
            }
            auto *contribution = target.contribution.data();
            target.lease->stage(
                [contribution] {
                    contribution->start();
                    return true;
                },
                [contribution] { contribution->stop(); });
            target.lease->commit();
            contribution->reconcile();
            if (contribution->m_preparedFactories.contains(target.identity)) {
                auto unused = contribution->m_preparedFactories.take(target.identity);
                if (unused) {
                    unused->discardPrepared();
                    unused->deleteLater();
                }
                target.factory = nullptr;
            }
            for (const auto &instance : contribution->m_instances)
                if (instance.state == "failed" || instance.state == "unsupported" ||
                    !instance.error.isEmpty()) {
                    error = instance.error;
                    break;
                }
        }
        if (!error.isEmpty() || !active())
            break;
    }
    m_registry->endPublication();
    QSet<QString> affected;
    for (const auto &target : m_preparing->targets)
        affected.insert(target.identity);
    if (m_current)
        for (const auto &target : m_current->targets)
            affected.insert(target.identity);
    const auto placementError = m_registry->settleInsertions(affected);
    if (error.isEmpty())
        error = placementError;
    if (!active())
        error = "Batch activation retired during publication";
    if (error.isEmpty())
        for (const auto &target : m_preparing->targets)
            if (target.contribution)
                for (const auto &instance : target.contribution->m_instances) {
                    const auto effective = m_registry->compositionState(instance.token);
                    if (effective.value("state") == "constrained") {
                        error = effective.value("error").toString();
                        break;
                    }
                }
    if (!error.isEmpty()) {
        m_registry->beginPublication();
        clear(m_preparing);
        m_registry->maskPublication(oldTokens, false);
        m_registry->endPublication();
        m_registry->settleInsertions();
        for (auto adapter : modelAdapters)
            if (adapter)
                adapter->endPublication(false);
        for (auto host : held)
            if (host)
                host->endPublicationHold(false);
        m_committing = false;
        refreshStructureDemand();
        m_error = error;
        emit changed();
        emit rejected(error);
        if (!active())
            stop();
        return;
    }
    m_registry->beginPublication();
    clear(m_current);
    m_registry->maskPublication(oldTokens, false);
    m_registry->endPublication();
    for (auto adapter : modelAdapters)
        if (adapter)
            adapter->endPublication(true);
    m_current = std::move(m_preparing);
    m_revision = m_current->number;
    m_definition = m_current->entries;
    refreshStructureDemand();
    m_error.clear();
    for (auto host : held)
        if (host)
            host->endPublicationHold(true);
    // New insertion hosts did not exist when old content was held. Flush each
    // newly published subtree once, including its native-content placeholders.
    QSet<EchoUiContentHost *> flushed;
    QSet<QQuickItem *> walked;
    const auto flush = [&](auto &&self, QQuickItem *item) -> void {
        if (!item || walked.contains(item) || item->property("_echoUiStaging").toBool())
            return;
        walked.insert(item);
        if (auto *host = qobject_cast<EchoUiContentHost *>(item); host && !flushed.contains(host)) {
            flushed.insert(host);
            host->flush();
        }
        for (auto *child : item->childItems())
            self(self, child);
    };
    for (const auto &target : m_current->targets)
        if (target.item && target.item->window())
            flush(flush, target.item->window()->contentItem());
    for (auto &target : m_current->targets) {
        if (target.factory && target.factory->prepared())
            target.factory->discardPrepared();
        if (target.stage) {
            target.stage->setParentItem(nullptr);
            target.stage->deleteLater();
            target.stage = nullptr;
        }
    }
    m_committing = false;
    emit changed();
    emit committed();
    if (!active())
        stop();
}
} // namespace EchoMusic
