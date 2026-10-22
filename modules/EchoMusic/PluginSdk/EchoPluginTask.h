#pragma once
#include "EchoPluginRegistration.h"
#include "TaskCenterModel.h"
#include <QJSValue>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginTasks;
class EchoPluginTask final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by EchoPluginTasks")
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool canceled READ canceled NOTIFY canceledChanged)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    EchoPluginTask(EchoPluginContext &scope, EchoPluginRegistration &lease);
    bool active() const;
    bool canceled() const
    {
        return m_canceled;
    }
    QString status() const;
    Q_INVOKABLE bool cancel();
    Q_INVOKABLE bool start(const QJSValue &patch = {});
    Q_INVOKABLE bool update(const QJSValue &patch);
    Q_INVOKABLE bool finish(const QString &status, const QJSValue &patch = {});
    Q_INVOKABLE bool dismiss();
    Q_INVOKABLE bool setProgress(double percent, const QString &label = {});
    Q_INVOKABLE bool complete();
    Q_INVOKABLE bool fail(const QString &message);
    Q_INVOKABLE bool abort();
    Q_INVOKABLE void dispose();
signals:
    void activeChanged();
    void changed();
    void canceledChanged();
    void cancelRequested();

private:
    friend class EchoPluginTasks;
    bool configure(const QJSValue &definition);
    bool parseContent(const QJSValue &patch, TaskCenterModel::Content &content);
    bool publish();
    bool settle(TaskCenterModel::Status status, const QString &message);
    void retire();
    QPointer<EchoPluginContext> m_scope;
    QPointer<EchoPluginRegistration> m_lease;
    TaskCenterModel::Handle m_handle;
    TaskCenterModel::Content m_content;
    TaskCenterModel::Policy m_policy;
    TaskCenterModel::Status m_status = TaskCenterModel::Running;
    int m_priority = 0;
    quint64 m_epoch;
    bool m_terminal = false, m_canceled = false, m_retired = false;
};
} // namespace EchoMusic
