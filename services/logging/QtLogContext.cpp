#include "QtLogContext.h"
#include <QFileInfo>
#include <QStringList>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <dlfcn.h>
#include <execinfo.h>
#endif

namespace EchoMusic
{
namespace
{
QString nativeStack()
{
    void *frames[16];
#ifdef Q_OS_WIN
    const auto count = CaptureStackBackTrace(0, 16, frames, nullptr);
#else
    const auto count = backtrace(frames, 16);
#endif
    QStringList result;
    for (int index = 0; index < count; ++index) {
        QString module, symbol;
        quintptr base = 0;
#ifdef Q_OS_WIN
        HMODULE handle = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(frames[index]), &handle)) {
            wchar_t path[1024];
            const auto size = GetModuleFileNameW(handle, path, 1024);
            if (size && size < 1024)
                module = QFileInfo(QString::fromWCharArray(path, int(size))).fileName();
            base = reinterpret_cast<quintptr>(handle);
        }
#else
        Dl_info info{};
        if (dladdr(frames[index], &info)) {
            module = QFileInfo(QString::fromLocal8Bit(info.dli_fname)).fileName();
            symbol = QString::fromLatin1(info.dli_sname).left(160);
            base = reinterpret_cast<quintptr>(info.dli_fbase);
        }
#endif
        // Module-relative offsets remain useful in stripped builds and do not
        // depend on ASLR. Do not log unknown absolute process addresses.
        if (module.isEmpty() || !base) {
            result.append(QStringLiteral("unknown"));
            continue;
        }
        auto frame =
            module.left(100) +
            QStringLiteral("+0x%1").arg(reinterpret_cast<quintptr>(frames[index]) - base, 0, 16);
        if (!symbol.isEmpty())
            frame += '(' + symbol + ')';
        result.append(frame);
    }
    return result.isEmpty() ? QStringLiteral("unavailable") : result.join(QStringLiteral(" <- "));
}
} // namespace

QString QtLogContext::describe(QtMsgType type, const QMessageLogContext &context,
                               const QString &message)
{
    if (type != QtWarningMsg && type != QtCriticalMsg)
        return {};
    QString details;
    if (context.file && *context.file) {
        // A plugin may live under a user-specific path; the source basename and
        // line identify it without copying the installation/data directory.
        details = QStringLiteral(" source=%1:%2")
                      .arg(QFileInfo(QString::fromUtf8(context.file)).fileName().left(160))
                      .arg(context.line);
        if (context.function && *context.function)
            details +=
                QStringLiteral(" function=%1").arg(QString::fromUtf8(context.function).left(200));
    }
    if (qstrcmp(context.category, "qt.core.qobject.connect") == 0) {
        if (details.isEmpty())
            details = QStringLiteral(" source=unavailable");
        // Preserve every warning. Only the extra stack capture has a session
        // budget, so a broken connection loop cannot continuously unwind.
        auto used = m_connectionTraces.load(std::memory_order_relaxed);
        while (used < 8 && !m_connectionTraces.compare_exchange_weak(used, used + 1,
                                                                     std::memory_order_relaxed)) {
        }
        if (used < 8) {
            const auto stack = nativeStack();
            if (message ==
                    QStringLiteral(
                        "QObject::connect(QJSEngine, QtObject): invalid nullptr parameter") &&
                stack.contains(QStringLiteral("QmlWorkerScript")))
                details += QStringLiteral(" connection_origin=qt_workerscript_init");
            details += QStringLiteral(" connect_stack=%1").arg(stack);
        } else {
            details += QStringLiteral(" connect_stack=limit_reached");
        }
    }
    return details;
}
} // namespace EchoMusic
