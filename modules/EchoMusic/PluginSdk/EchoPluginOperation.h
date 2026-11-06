#pragma once
#include <QObject>
#include <QVariant>
#include <QJSEngine>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <functional>

namespace EchoMusic
{
/// GUI-thread, exactly-once result. Cancellation invalidates delivery before
/// invoking the transport cancel hook. Results contain SDK values, never DTOs.
class EchoPluginOperation final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by PluginSdk services")
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(QVariant result READ scriptResult NOTIFY changed)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    enum State
    {
        Pending,
        Running,
        Succeeded,
        Failed,
        Canceled
    };
    Q_ENUM(State)
    explicit EchoPluginOperation(QObject *parent, QJSEngine *engine = nullptr);
    QString id() const
    {
        return m_id;
    }
    State state() const
    {
        return m_state;
    }
    QVariant result() const
    {
        return m_result;
    }
    QVariant scriptResult() const;
    QString errorCode() const
    {
        return m_code;
    }
    QString errorMessage() const
    {
        return m_message;
    }
    bool finished() const
    {
        return m_state >= Succeeded;
    }
    void start(std::function<void()> cancel = {});
    void succeed(QVariant result = {});
    void fail(QString code, QString message);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void completed();

private:
    void finish(State state, QVariant result, QString code, QString message);
    QString m_id, m_code, m_message;
    QVariant m_result;
    QPointer<QJSEngine> m_engine;
    mutable QVariant m_scriptResult;
    State m_state = Pending;
    std::function<void()> m_cancel;
};
} // namespace EchoMusic
