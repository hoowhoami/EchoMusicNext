#include "ToastService.h"
#include <QThread>
#include <utility>
namespace EchoMusic
{
ToastService::ToastService(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { dismiss(noticeId()); });
}
quint64 ToastService::show(QString message, Tone tone, int duration, Presentation presentation,
                           QString title)
{
    message = message.trimmed();
    title = title.trimmed();
    const bool standard =
        !title.isEmpty() || presentation == Standard ||
        (presentation == Auto && (message.contains('\n') || message.toUcs4().size() > 30));
    return enqueue({0, message, title, {}, tone, duration, 1, standard, {}, {}});
}
quint64 ToastService::showOwned(QObject &provider, QString message, Tone tone, int duration,
                                Presentation presentation, QString title)
{
    const bool standard =
        !title.isEmpty() || presentation == Standard ||
        (presentation == Auto && (message.contains('\n') || message.toUcs4().size() > 30));
    return enqueue(
        {0, message.trimmed(), title.trimmed(), {}, tone, duration, 1, standard, &provider, {}});
}
bool ToastService::containsNotice(quint64 id) const
{
    for (const auto &v : m_items)
        if (v.id == id)
            return true;
    return false;
}
void ToastService::dismissProvider(QObject *provider)
{
    QList<quint64> ids;
    for (const auto &n : m_items)
        if (n.provider == provider)
            ids.append(n.id);
    for (auto id : ids)
        dismiss(id);
}
quint64 ToastService::showAction(QObject &provider, QString message, QString label,
                                 std::function<void()> action, Tone tone, int duration,
                                 QString title)
{
    if (provider.thread() != thread() || !action || label.trimmed().isEmpty())
        return 0;
    return enqueue({0, message.trimmed(), title.trimmed(), label.trimmed(), tone, duration, 1, true,
                    &provider, std::move(action)});
}
quint64 ToastService::enqueue(Notice notice)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed || notice.message.isEmpty())
        return 0;
    for (auto &item : m_items) {
        if (item.message != notice.message || item.title != notice.title ||
            item.tone != notice.tone || item.label != notice.label ||
            item.provider != notice.provider)
            continue;
        ++item.count;
        item.duration = notice.duration;
        item.standard = notice.standard;
        item.provider = notice.provider;
        item.action = std::move(notice.action);
        if (item.id == noticeId())
            schedule(item.duration);
        emit changed();
        return item.id;
    }
    notice.id = ++m_nextId;
    const auto id = notice.id;
    m_items.append(std::move(notice));
    if (m_items.size() > 3)
        m_items.removeAt(1); // Retain the visible notice and newest waiting notices.
    if (m_items.size() == 1)
        schedule(m_items.front().duration);
    emit changed();
    return id;
}
void ToastService::schedule(int duration)
{
    m_timer.stop();
    m_remaining = qMax(0, duration);
    m_elapsed.invalidate();
    if (visible() && duration > 0) {
        m_elapsed.start();
        m_timer.start(duration);
    }
}
void ToastService::dismiss(quint64 id)
{
    if (!id)
        return;
    const bool current = id == noticeId();
    const auto removed = m_items.removeIf([id](const Notice &item) { return item.id == id; });
    if (!removed)
        return;
    if (current)
        schedule(visible() ? m_items.front().duration : 0);
    emit changed();
}
void ToastService::pause(quint64 id)
{
    // Passive messages expire even under a stationary pointer.
    if (id != noticeId() || !visible() || !m_items.front().action || !m_timer.isActive())
        return;
    m_remaining = qMax(0, m_remaining - int(m_elapsed.elapsed()));
    m_timer.stop();
}
void ToastService::resume(quint64 id)
{
    if (id != noticeId() || !visible() || m_items.front().duration <= 0 || m_timer.isActive())
        return;
    if (m_remaining <= 0)
        dismiss(id);
    else
        schedule(m_remaining);
}
void ToastService::triggerAction(quint64 id)
{
    if (id != noticeId() || !visible())
        return;
    const auto item = m_items.front(); // May synchronously enqueue or destroy its provider.
    dismiss(id);
    if (item.provider && item.action)
        item.action();
}
void ToastService::shutdown()
{
    m_closed = true;
    m_timer.stop();
    m_items.clear();
    emit changed();
}
} // namespace EchoMusic
