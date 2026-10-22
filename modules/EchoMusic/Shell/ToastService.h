#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
/// GUI-thread transient notices: one visible item and at most two waiting.
/// Actions must not outlive their provider.
class ToastService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ShellController")
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(quint64 noticeId READ noticeId NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(Tone tone READ tone NOTIFY changed)
    Q_PROPERTY(bool standard READ standard NOTIFY changed)
    Q_PROPERTY(int repetitions READ repetitions NOTIFY changed)
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY changed)
    Q_PROPERTY(QString actionLabel READ actionLabel NOTIFY changed)
public:
    enum Tone
    {
        Info,
        Success,
        Warning,
        Danger
    };
    Q_ENUM(Tone)
    enum Presentation
    {
        Auto,
        Mini,
        Standard
    };
    Q_ENUM(Presentation)
    explicit ToastService(QObject *parent = nullptr);
    quint64 show(QString message, Tone tone = Info, int duration = 2600,
                 Presentation presentation = Auto, QString title = {});
    quint64 showOwned(QObject &provider, QString message, Tone tone = Info, int duration = 2600,
                      Presentation presentation = Auto, QString title = {});
    bool containsNotice(quint64 id) const;
    void dismissProvider(QObject *provider);
    quint64 showAction(QObject &provider, QString message, QString label,
                       std::function<void()> action, Tone tone = Info, int duration = 8000,
                       QString title = {});
    bool visible() const
    {
        return !m_items.isEmpty();
    }
    quint64 noticeId() const
    {
        return visible() ? m_items.front().id : 0;
    }
    QString message() const
    {
        return visible() ? m_items.front().message : QString();
    }
    QString title() const
    {
        return visible() ? m_items.front().title : QString();
    }
    Tone tone() const
    {
        return visible() ? m_items.front().tone : Info;
    }
    bool standard() const
    {
        return visible() && m_items.front().standard;
    }
    int repetitions() const
    {
        return visible() ? m_items.front().count : 0;
    }
    int queuedCount() const
    {
        return m_items.size();
    }
    QString actionLabel() const
    {
        return visible() ? m_items.front().label : QString();
    }
    Q_INVOKABLE void dismiss(quint64 id);
    Q_INVOKABLE void pause(quint64 id);
    Q_INVOKABLE void resume(quint64 id);
    Q_INVOKABLE void triggerAction(quint64 id);
    void shutdown();
signals:
    void changed();

private:
    struct Notice
    {
        quint64 id;
        QString message, title, label;
        Tone tone;
        int duration, count = 1;
        bool standard;
        QPointer<QObject> provider;
        std::function<void()> action;
    };
    quint64 enqueue(Notice notice);
    void schedule(int duration);
    QList<Notice> m_items;
    QTimer m_timer;
    QElapsedTimer m_elapsed;
    int m_remaining = 0;
    quint64 m_nextId = 0;
    bool m_closed = false;
};
} // namespace EchoMusic
