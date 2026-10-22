#include "EchoPluginTask.h"
#include "EchoPluginContext.h"
#include "ShellController.h"
#include "ApiClient.h"
#include "AccountService.h"
#include <QJSValueIterator>
#include <QTimer>
#include <cmath>
namespace EchoMusic
{
namespace
{
bool keys(const QJSValue &value, const QStringList &allowed)
{
    if (!value.isObject() || value.isArray())
        return false;
    QJSValueIterator i(value);
    while (i.hasNext()) {
        i.next();
        if (!allowed.contains(i.name()))
            return false;
    }
    return true;
}
bool text(const QJSValue &v, int max)
{
    return v.isUndefined() || (v.isString() && v.toString().size() <= max);
}
bool retention(const QJSValue &v, TaskCenterModel::Retention &out)
{
    if (!keys(v, {"mode", "delayMs"}))
        return false;
    const auto mode = v.property("mode").toString();
    if (mode == "manual") {
        out = {true, 0};
        return true;
    }
    const auto delay = v.property("delayMs").toNumber();
    if (mode != "auto" || !std::isfinite(delay) || delay < 0 || delay > 86400000 ||
        std::floor(delay) != delay)
        return false;
    out = {false, int(delay)};
    return true;
}
} // namespace
EchoPluginTask::EchoPluginTask(EchoPluginContext &scope, EchoPluginRegistration &lease)
    : QObject(&lease), m_scope(&scope), m_lease(&lease), m_epoch(scope.accountEpoch())
{
    const auto invalidate = [this] {
        if (m_scope && m_scope->accountEpoch() != m_epoch)
            dispose();
    };
    connect(&scope.host().api, &ApiClient::sessionChanged, this, invalidate);
    connect(&scope.host().account, &AccountService::sessionAdopted, this, invalidate);
}
bool EchoPluginTask::active() const
{
    return !m_retired && m_scope && m_scope->active() && m_scope->accountEpoch() == m_epoch &&
           m_lease && m_lease->active();
}
QString EchoPluginTask::status() const
{
    return QStringList{"pending", "running", "completed", "error", "aborted"}.value(int(m_status));
}
bool EchoPluginTask::parseContent(const QJSValue &value, TaskCenterModel::Content &out)
{
    if (value.isUndefined())
        return true;
    if (!keys(value, {"name", "icon", "priority", "progress", "error", "actions", "items"}) ||
        !text(value.property("name"), 256) || !text(value.property("icon"), 128) ||
        !text(value.property("error"), 4096))
        return false;
    if (value.hasProperty("icon"))
        out.icon = value.property("icon").toString();
    if (value.hasProperty("priority")) {
        auto n = value.property("priority").toNumber();
        if (!std::isfinite(n) || std::floor(n) != n || std::abs(n) > 10000)
            return false;
    }
    if (value.hasProperty("name")) {
        out.name = value.property("name").toString();
        if (out.name.isEmpty())
            return false;
    }
    if (value.hasProperty("error"))
        out.error = value.property("error").toString();
    if (value.hasProperty("progress")) {
        auto p = value.property("progress");
        if (!keys(p, {"done", "total", "percent", "label"}) || !text(p.property("label"), 512))
            return false;
        if (p.hasProperty("label"))
            out.statusLabel = p.property("label").toString();
        if (p.hasProperty("done") || p.hasProperty("total")) {
            auto done = p.property("done").toNumber(), total = p.property("total").toNumber();
            if (!std::isfinite(done) || !std::isfinite(total) || done < 0 || total <= 0 ||
                done > total)
                return false;
            out.percent = 100 * done / total;
        }
        if (p.hasProperty("percent")) {
            auto n = p.property("percent").toNumber();
            if (!std::isfinite(n) || n < 0 || n > 100)
                return false;
            out.percent = n;
        }
    }
    const auto actions = [this](const QJSValue &list, QList<TaskAction> &out) -> bool {
        if (!list.isArray() || list.property("length").toInt() > 16)
            return false;
        out.clear();
        QSet<QString> ids;
        for (int i = 0; i < list.property("length").toInt(); ++i) {
            const auto v = list.property(i);
            const auto id = v.property("id").toString(), label = v.property("label").toString(),
                       variant = v.property("variant").isUndefined()
                                     ? QString("ghost")
                                     : v.property("variant").toString();
            if (!keys(v, {"id", "label", "variant", "disabled", "closePanel", "onClick"}) ||
                id.isEmpty() || id.size() > 128 || ids.contains(id) || label.isEmpty() ||
                label.size() > 256 || !v.property("onClick").isCallable() ||
                !QStringList{"ghost", "primary", "danger"}.contains(variant))
                return false;
            if ((v.hasProperty("disabled") && !v.property("disabled").isBool()) ||
                (v.hasProperty("closePanel") && !v.property("closePanel").isBool()))
                return false;
            ids.insert(id);
            TaskAction a;
            a.id = id;
            a.label = label;
            a.variant = variant == "primary"  ? TaskAction::Primary
                        : variant == "danger" ? TaskAction::Danger
                                              : TaskAction::Ghost;
            a.enabled = !v.property("disabled").toBool();
            a.closePanel = v.property("closePanel").toBool();
            a.invoke = [guard = QPointer(this), cb = v.property("onClick")] {
                if (guard && guard->active())
                    guard->m_scope->invokeRegistered(cb, guard->m_lease, {});
            };
            out.append(a);
        }
        return true;
    };
    if (value.hasProperty("actions") && !actions(value.property("actions"), out.actions))
        return false;
    if (value.hasProperty("items")) {
        auto list = value.property("items");
        if (!list.isArray() || list.property("length").toInt() > 1000)
            return false;
        out.items.clear();
        QSet<QString> ids;
        for (int i = 0; i < list.property("length").toInt(); ++i) {
            const auto v = list.property(i);
            TaskDetail row;
            row.id = v.property("id").toString();
            row.name = v.property("name").toString();
            row.description = v.property("description").isUndefined()
                                  ? QString()
                                  : v.property("description").toString();
            row.error =
                v.property("error").isUndefined() ? QString() : v.property("error").toString();
            row.statusLabel = v.property("statusLabel").isUndefined()
                                  ? QString()
                                  : v.property("statusLabel").toString();
            if (!keys(v, {"id", "name", "description", "error", "statusLabel", "actions"}) ||
                row.id.isEmpty() || row.id.size() > 128 || ids.contains(row.id) ||
                row.name.isEmpty() || row.name.size() > 256 ||
                !text(v.property("description"), 4096) || !text(v.property("error"), 4096) ||
                !text(v.property("statusLabel"), 512))
                return false;
            if (v.hasProperty("actions") && !actions(v.property("actions"), row.actions))
                return false;
            ids.insert(row.id);
            out.items.append(row);
        }
    }
    return true;
}
bool EchoPluginTask::configure(const QJSValue &definition)
{
    if (!keys(definition, {"name", "icon", "status", "retention", "priority", "progress", "error",
                           "actions", "items"}))
        return false;
    auto patch = m_scope->engine().newObject();
    for (const auto &k : {"name", "icon", "priority", "progress", "error", "actions", "items"})
        if (definition.hasProperty(k))
            patch.setProperty(k, definition.property(k));
    if (!parseContent(patch, m_content) || m_content.name.isEmpty())
        return false;
    const auto status = definition.property("status").isUndefined()
                            ? QString("running")
                            : definition.property("status").toString();
    const auto index =
        QStringList{"pending", "running", "completed", "error", "aborted"}.indexOf(status);
    if (index < 0)
        return false;
    m_status = TaskCenterModel::Status(index);
    m_terminal = index >= 2;
    if (m_status == TaskCenterModel::Aborted)
        m_canceled = true;
    const auto policy = definition.property("retention");
    if (policy.isString()) {
        if (policy.toString() == "action-required")
            m_policy = TaskCenterModel::Policy::actionRequired();
        else if (policy.toString() != "transient")
            return false;
    } else if (!policy.isUndefined() &&
               (!keys(policy, {"completed", "error", "aborted"}) ||
                !retention(policy.property("completed"), m_policy.completed) ||
                !retention(policy.property("error"), m_policy.error) ||
                !retention(policy.property("aborted"), m_policy.aborted)))
        return false;
    if (definition.hasProperty("priority")) {
        auto n = definition.property("priority").toNumber();
        if (!std::isfinite(n) || std::floor(n) != n || n < -10000 || n > 10000)
            return false;
        m_priority = int(n);
    }
    return true;
}
bool EchoPluginTask::publish()
{
    if (!active())
        return false;
    auto *model = m_scope->host().shell.tasks();
    connect(model, &TaskCenterModel::taskRemoved, this,
            [this](const QString &id, const QString &token) {
                if (id == m_handle.id && token == QString::number(m_handle.generation))
                    dispose();
            });
    TaskCenterModel::Registration t;
    t.id = m_lease->key();
    t.content = m_content;
    t.status = m_status;
    t.priority = m_priority;
    t.policy = m_policy;
    t.cancel = [this] { cancel(); };
    m_handle = model->registerTask(*m_lease, std::move(t));
    return m_handle.generation != 0;
}
void EchoPluginTask::retire()
{
    if (std::exchange(m_retired, true))
        return;
    if (!m_terminal && !m_canceled) {
        m_canceled = true;
        emit canceledChanged();
        emit cancelRequested();
    }
    emit activeChanged();
    auto lease = m_lease;
    QTimer::singleShot(0, this, [this, lease] {
        setParent(nullptr);
        QQmlEngine::setObjectOwnership(this, QQmlEngine::JavaScriptOwnership);
        if (lease)
            lease->deleteLater();
    });
}
bool EchoPluginTask::cancel()
{
    if (!active() || m_terminal || m_canceled)
        return false;
    m_canceled = true;
    emit canceledChanged();
    emit cancelRequested();
    return true;
}
bool EchoPluginTask::start(const QJSValue &patch)
{
    if (!active() || m_terminal || m_canceled || m_status != TaskCenterModel::Pending)
        return false;
    if (!update(patch))
        return false;
    if (m_handle.generation && !m_scope->host().shell.tasks()->start(m_handle))
        return false;
    m_status = TaskCenterModel::Running;
    emit changed();
    return true;
}
bool EchoPluginTask::update(const QJSValue &patch)
{
    if (!active())
        return false;
    auto next = m_content;
    if (!parseContent(patch, next))
        return false;
    const auto priority =
        patch.hasProperty("priority") ? patch.property("priority").toInt() : m_priority;
    if (m_handle.generation && !m_scope->host().shell.tasks()->update(m_handle, next, priority))
        return false;
    m_priority = priority;
    m_content = next;
    emit changed();
    return true;
}
bool EchoPluginTask::finish(const QString &status, const QJSValue &patch)
{
    const int index = QStringList{"completed", "error", "aborted"}.indexOf(status);
    if (index < 0 || !active() || m_terminal)
        return false;
    auto next = m_content;
    if (!parseContent(patch, next))
        return false;
    const auto terminal = TaskCenterModel::Status(index + 2);
    const int priority =
        patch.hasProperty("priority") ? patch.property("priority").toInt() : m_priority;
    if (m_handle.generation && priority != m_priority &&
        !m_scope->host().shell.tasks()->update(m_handle, next, priority))
        return false;
    m_priority = priority;
    if (m_handle.generation && !m_scope->host().shell.tasks()->finish(m_handle, terminal, next))
        return false;
    m_content = next;
    m_status = terminal;
    m_terminal = true;
    if (terminal == TaskCenterModel::Aborted && !m_canceled) {
        m_canceled = true;
        emit canceledChanged();
        emit cancelRequested();
    }
    emit changed();
    return true;
}
bool EchoPluginTask::settle(TaskCenterModel::Status status, const QString &message)
{
    if (!m_scope)
        return false;
    auto patch = m_scope->engine().newObject();
    patch.setProperty("error", message);
    return finish(
        QStringList{"pending", "running", "completed", "error", "aborted"}.value(int(status)),
        patch);
}
bool EchoPluginTask::dismiss()
{
    if (!active())
        return false;
    dispose();
    return true;
}
bool EchoPluginTask::setProgress(double percent, const QString &label)
{
    if (!active() || m_terminal || m_canceled || !std::isfinite(percent) || percent < 0 ||
        percent > 100)
        return false;
    m_content.percent = percent;
    m_content.statusLabel = label;
    return !m_handle.generation ||
           m_scope->host().shell.tasks()->updateProgress(m_handle, percent, label);
}
bool EchoPluginTask::complete()
{
    return settle(TaskCenterModel::Completed, {});
}
bool EchoPluginTask::fail(const QString &message)
{
    return settle(TaskCenterModel::Error, message);
}
bool EchoPluginTask::abort()
{
    return settle(TaskCenterModel::Aborted, {});
}
void EchoPluginTask::dispose()
{
    if (m_lease)
        m_lease->dispose();
    retire();
}
} // namespace EchoMusic
