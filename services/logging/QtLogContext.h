#pragma once
#include <QLoggingCategory>
#include <QString>
#include <atomic>

namespace EchoMusic
{
// Captures only static source/symbol metadata on the emitting thread. Never
// retain Qt context pointers or inspect arguments, objects, or stack memory.
class QtLogContext final
{
public:
    QString describe(QtMsgType type, const QMessageLogContext &context, const QString &message);

private:
    std::atomic_uint m_connectionTraces{0};
};
} // namespace EchoMusic
