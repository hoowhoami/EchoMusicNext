#include "EchoPluginOperation.h"
#include "EchoPluginScriptValue_p.h"
#include <QUuid>
#include <utility>
namespace EchoMusic
{
EchoPluginOperation::EchoPluginOperation(QObject *parent, QJSEngine *engine)
    : QObject(parent), m_id(QUuid::createUuid().toString(QUuid::WithoutBraces)), m_engine(engine)
{
}
QVariant EchoPluginOperation::scriptResult() const
{
    if (!m_engine || !PluginScript::isContainer(m_result))
        return m_result;
    // Qt's QVariant sequence wrappers serialize as JSON arrays but fail
    // Array.isArray(). Materialize once in the owning engine, while retaining
    // the native QVariant for host consumers and cross-operation forwarding.
    if (!m_scriptResult.isValid())
        m_scriptResult = QVariant::fromValue(PluginScript::nativeValue(*m_engine, m_result));
    return m_scriptResult;
}
void EchoPluginOperation::start(std::function<void()> cancel)
{
    if (m_state != Pending)
        return;
    m_cancel = std::move(cancel);
    m_state = Running;
    emit changed();
}
void EchoPluginOperation::finish(State state, QVariant result, QString code, QString message)
{
    if (finished())
        return;
    m_state = state;
    m_result = std::move(result);
    m_code = std::move(code);
    m_message = std::move(message);
    m_cancel = {};
    emit changed();
    emit completed();
}
void EchoPluginOperation::succeed(QVariant result)
{
    finish(Succeeded, std::move(result), {}, {});
}
void EchoPluginOperation::fail(QString code, QString message)
{
    finish(Failed, {}, std::move(code), std::move(message));
}
void EchoPluginOperation::cancel()
{
    if (finished())
        return;
    auto cancel = std::exchange(m_cancel, {});
    finish(Canceled, {}, "Canceled", "Operation canceled");
    if (cancel)
        cancel();
}
} // namespace EchoMusic
