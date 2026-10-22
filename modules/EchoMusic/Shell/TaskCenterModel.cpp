#include "TaskCenterModel.h"
#include <QSet>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <limits>

namespace EchoMusic
{
namespace
{
QVariantList actionData(const QList<TaskAction> &actions)
{
    QVariantList result;
    for (const auto &action : actions)
        result.append(QVariantMap{{"id", action.id},
                                  {"label", action.label},
                                  {"variant", int(action.variant)},
                                  {"enabled", action.enabled}});
    return result;
}
bool validActions(const QList<TaskAction> &actions)
{
    QSet<QString> ids;
    for (const auto &a : actions) {
        if (a.id.isEmpty() || a.label.isEmpty() || !a.invoke || ids.contains(a.id))
            return false;
        ids.insert(a.id);
    }
    return true;
}
bool validContent(const TaskCenterModel::Content &content)
{
    if (content.name.isEmpty() || !validActions(content.actions))
        return false;
    QSet<QString> ids;
    for (const auto &item : content.items) {
        if (item.id.isEmpty() || ids.contains(item.id) || !validActions(item.actions))
            return false;
        ids.insert(item.id);
    }
    return true;
}
QString statusLabel(TaskCenterModel::Status status)
{
    switch (status) {
    case TaskCenterModel::Pending:
        return QStringLiteral("待操作");
    case TaskCenterModel::Running:
        return QStringLiteral("进行中");
    case TaskCenterModel::Completed:
        return QStringLiteral("已完成");
    case TaskCenterModel::Error:
        return QStringLiteral("失败");
    case TaskCenterModel::Aborted:
        return QStringLiteral("已中止");
    }
    return {};
}
} // namespace
QHash<int, QByteArray> TaskDetailsModel::roleNames() const
{
    return {{IdRole, "detailId"},
            {NameRole, "detailName"},
            {DescriptionRole, "detailDescription"},
            {ErrorRole, "detailError"},
            {StatusRole, "detailStatus"},
            {ActionsRole, "detailActions"}};
}
QVariant TaskDetailsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size() || index.column() != 0)
        return {};
    const auto &item = m_items[index.row()];
    switch (role) {
    case IdRole:
        return item.id;
    case NameRole:
        return item.name;
    case DescriptionRole:
        return item.description;
    case ErrorRole:
        return item.error;
    case StatusRole:
        return item.statusLabel;
    case ActionsRole:
        return actionData(item.actions);
    default:
        return {};
    }
}
void TaskDetailsModel::replace(QList<TaskDetail> items)
{
    const auto previous = count();
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    if (previous != count()) {
        emit countChanged();
        if ((previous <= 3) != (count() <= 3))
            setExpanded(count() <= 3);
    }
}
void TaskDetailsModel::setExpanded(bool expanded)
{
    if (m_expanded == expanded)
        return;
    m_expanded = expanded;
    emit expandedChanged();
}
void TaskCenterModel::resetDetailExpansion()
{
    // DialogContent creates new native details elements on each activation.
    // Within one activation, expansion belongs to the model across pooling.
    for (const auto &entry : m_entries)
        entry->details->setExpanded(entry->details->count() <= 3);
}
TaskCenterModel::TaskCenterModel(QObject *parent) : QAbstractListModel(parent)
{
    m_clock.start();
    m_expiry.setSingleShot(true);
    connect(&m_expiry, &QTimer::timeout, this, [this] {
        QList<Handle> expired;
        for (const auto &entry : m_entries)
            if (entry->expires >= 0 && entry->expires <= m_clock.elapsed())
                expired.push_back({entry->task.id, entry->generation});
        for (const auto &handle : expired)
            remove(handle);
        scheduleExpiry();
    });
}
TaskCenterModel::~TaskCenterModel()
{
    shutdown();
}
QHash<int, QByteArray> TaskCenterModel::roleNames() const
{
    return {{IdRole, "taskId"},
            {TokenRole, "taskToken"},
            {NameRole, "taskName"},
            {StatusRole, "taskStatus"},
            {StatusLabelRole, "taskStatusLabel"},
            {PercentRole, "taskPercent"},
            {ErrorRole, "taskError"},
            {ActionsRole, "taskActions"},
            {DetailsRole, "taskDetails"},
            {DismissibleRole, "taskDismissible"},
            {ProgressLabelRole, "taskProgressLabel"},
            {HasProgressRole, "taskHasProgress"},
            {IconRole, "taskIcon"}};
}
QVariant TaskCenterModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= count() || index.column() != 0)
        return {};
    const auto &e = *m_entries[index.row()];
    const auto &t = e.task;
    const auto &c = t.content;
    switch (role) {
    case IdRole:
        return t.id;
    case TokenRole:
        return QString::number(e.generation);
    case IconRole:
        return c.icon;
    case NameRole:
        return c.name;
    case StatusRole:
        return t.status;
    case StatusLabelRole:
        return statusLabel(t.status);
    case ProgressLabelRole:
        return c.statusLabel == statusLabel(t.status) ? QString{} : c.statusLabel;
    case HasProgressRole:
        return e.progressReported || c.percent.has_value() || !c.statusLabel.isEmpty();
    case PercentRole:
        return !c.percent                  ? -1.0
               : std::isfinite(*c.percent) ? std::clamp(*c.percent, 0.0, 100.0)
                                           : 0.0;
    case ErrorRole:
        return c.error;
    case ActionsRole:
        return actionData(c.actions);
    case DetailsRole:
        return QVariant::fromValue(e.details);
    case DismissibleRole:
        return dismissible(e);
    default:
        return {};
    }
}
int TaskCenterModel::rowFor(const Handle &handle) const
{
    for (int row = 0; row < count(); ++row)
        if (m_entries[row]->task.id == handle.id &&
            m_entries[row]->generation == handle.generation && m_entries[row]->owner)
            return row;
    return -1;
}
TaskCenterModel::Retention TaskCenterModel::retention(const Entry &e) const
{
    switch (e.task.status) {
    case Completed:
        return e.task.policy.completed;
    case Error:
        return e.task.policy.error;
    case Aborted:
        return e.task.policy.aborted;
    default:
        return {true, 0};
    }
}
bool TaskCenterModel::dismissible(const Entry &e) const
{
    return e.task.status != Running && e.task.status != Pending && retention(e).manual;
}
TaskCenterModel::Handle TaskCenterModel::registerTask(QObject &owner, Registration task)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed || owner.thread() != thread() || task.id.isEmpty() ||
        !validContent(task.content) || task.policy.completed.delayMs < 0 ||
        task.policy.error.delayMs < 0 || task.policy.aborted.delayMs < 0 || task.status < Pending ||
        task.status > Aborted)
        return {};
    const QPointer<QObject> ownerGuard = &owner;
    for (int row = 0; row < count(); ++row)
        if (m_entries[row]->task.id == task.id) {
            if (m_entries[row]->owner != &owner)
                return {};
            removeAt(row);
            break;
        }
    if (m_closed || !ownerGuard)
        return {};
    // Cancellation of the previous generation may itself register a new task.
    for (const auto &entry : m_entries)
        if (entry->task.id == task.id)
            return {};
    const Handle handle{task.id, ++m_generation};
    auto entry = std::make_unique<Entry>();
    entry->task = std::move(task);
    entry->owner = &owner;
    entry->generation = handle.generation;
    entry->details = new TaskDetailsModel(this);
    entry->details->replace(entry->task.content.items);
    const auto policy = retention(*entry);
    if (!policy.manual)
        entry->expires = m_clock.elapsed() + policy.delayMs;
    int row = 0;
    while (row < count() && m_entries[row]->task.priority > entry->task.priority)
        ++row;
    beginInsertRows({}, row, row);
    m_entries.insert(m_entries.begin() + row, std::move(entry));
    endInsertRows();
    m_entries[row]->ownerConnection = connect(&owner, &QObject::destroyed, this, [this, handle] {
        // QPointer is already null when destroyed is emitted. Match the token
        // independently here, so old owner destruction cannot erase replacements.
        for (int row = 0; row < count(); ++row)
            if (m_entries[row]->generation == handle.generation) {
                removeAt(row);
                break;
            }
        scheduleExpiry();
    });
    emit countChanged();
    scheduleExpiry();
    return handle;
}
void TaskCenterModel::applyContent(Entry &entry, Content content)
{
    entry.details->replace(content.items);
    entry.task.content = std::move(content);
}
bool TaskCenterModel::update(const Handle &handle, Content content, std::optional<int> priority)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto row = rowFor(handle);
    if (m_closed || row < 0 || !validContent(content))
        return false;
    applyContent(*m_entries[row], std::move(content));
    int destination = row;
    if (priority && *priority != m_entries[row]->task.priority) {
        m_entries[row]->task.priority = *priority;
        destination = 0;
        for (int i = 0; i < count(); ++i)
            if (i != row && (m_entries[i]->task.priority > *priority ||
                             (m_entries[i]->task.priority == *priority &&
                              m_entries[i]->generation > m_entries[row]->generation)))
                ++destination;
        if (destination != row) {
            beginMoveRows({}, row, row, {}, destination > row ? destination + 1 : destination);
            auto moved = std::move(m_entries[row]);
            m_entries.erase(m_entries.begin() + row);
            m_entries.insert(m_entries.begin() + destination, std::move(moved));
            endMoveRows();
        }
    }
    emit dataChanged(index(destination), index(destination));
    return true;
}
bool TaskCenterModel::start(const Handle &handle)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto row = rowFor(handle);
    if (m_closed || row < 0 || m_entries[row]->task.status != Pending)
        return false;
    m_entries[row]->task.status = Running;
    emit dataChanged(index(row), index(row));
    return true;
}
bool TaskCenterModel::updateProgress(const Handle &handle, std::optional<double> percent,
                                     QString label)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto row = rowFor(handle);
    if (m_closed || row < 0)
        return false;
    auto &entry = *m_entries[row];
    entry.progressReported = true;
    auto &content = entry.task.content;
    content.percent = percent;
    content.statusLabel = std::move(label);
    emit dataChanged(index(row), index(row), {PercentRole, ProgressLabelRole, HasProgressRole});
    return true;
}
bool TaskCenterModel::finish(const Handle &handle, Status terminal, Content content)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto row = rowFor(handle);
    if (m_closed || row < 0 || terminal < Completed || terminal > Aborted || !validContent(content))
        return false;
    auto &entry = *m_entries[row];
    if (entry.task.status != Running && entry.task.status != Pending)
        return false;
    entry.task.status = terminal;
    applyContent(entry, std::move(content));
    const auto policy = retention(entry);
    entry.expires = policy.manual ? -1 : m_clock.elapsed() + policy.delayMs;
    emit dataChanged(index(row), index(row));
    scheduleExpiry();
    return true;
}
void TaskCenterModel::removeAt(int row)
{
    auto &entry = *m_entries[row];
    const auto removedId = entry.task.id;
    const auto removedToken = QString::number(entry.generation);
    auto cancel = entry.owner && (entry.task.status == Running || entry.task.status == Pending)
                      ? entry.task.cancel
                      : std::function<void()>();
    // Complete the model mutation before cancellation, which may synchronously
    // publish another task. Removed detail models remain valid until deferred GC.
    auto *details = entry.details;
    disconnect(entry.ownerConnection);
    beginRemoveRows({}, row, row);
    m_entries.erase(m_entries.begin() + row);
    endRemoveRows();
    details->deleteLater();
    emit countChanged();
    if (cancel) {
        // Provider failure must not interrupt model teardown or leave later
        // tasks alive during application shutdown.
        try {
            cancel();
        } catch (...) {
            qWarning("Task cancellation callback failed");
        }
    }
    emit taskRemoved(removedId, removedToken);
}

bool TaskCenterModel::remove(const Handle &handle)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto row = rowFor(handle);
    if (row < 0)
        return false;
    removeAt(row);
    scheduleExpiry();
    return true;
}
bool TaskCenterModel::dismiss(const QString &id, const QString &token)
{
    const Handle handle{id, token.toULongLong()};
    const auto row = rowFor(handle);
    if (row < 0 || !dismissible(*m_entries[row]))
        return false;
    return remove(handle);
}
bool TaskCenterModel::invokeAction(const QString &id, const QString &token, const QString &actionId,
                                   const QString &detailId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto row = rowFor({id, token.toULongLong()});
    if (m_closed || row < 0)
        return false;
    const auto &entry = *m_entries[row];
    const QList<TaskAction> *actions = &entry.task.content.actions;
    if (!detailId.isEmpty()) {
        const auto item =
            std::find_if(entry.details->m_items.cbegin(), entry.details->m_items.cend(),
                         [&](const auto &i) { return i.id == detailId; });
        if (item == entry.details->m_items.cend())
            return false;
        actions = &item->actions;
    }
    const auto action = std::find_if(actions->cbegin(), actions->cend(),
                                     [&](const auto &a) { return a.id == actionId; });
    if (action == actions->cend() || !action->enabled || !action->invoke)
        return false;
    const auto invoke = action->invoke;
    const bool close = action->closePanel;
    // May remove this task. Match the original panel's failure isolation and
    // closePanel semantics; no entry references survive the provider call.
    try {
        invoke();
    } catch (...) {
        qWarning("Task action callback failed");
    }
    if (close)
        emit closePanelRequested();
    return true;
}
void TaskCenterModel::scheduleExpiry()
{
    m_expiry.stop();
    if (m_closed)
        return;
    qint64 earliest = std::numeric_limits<qint64>::max();
    for (const auto &entry : m_entries)
        if (entry->expires >= 0)
            earliest = std::min(earliest, entry->expires);
    if (earliest != std::numeric_limits<qint64>::max())
        m_expiry.start(int(std::min<qint64>(std::numeric_limits<int>::max(),
                                            std::max<qint64>(0, earliest - m_clock.elapsed()))));
}
void TaskCenterModel::shutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed)
        return;
    m_closed = true;
    m_expiry.stop();
    while (!m_entries.empty())
        removeAt(count() - 1);
}
} // namespace EchoMusic
