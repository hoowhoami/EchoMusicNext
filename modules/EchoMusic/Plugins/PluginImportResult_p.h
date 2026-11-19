#pragma once
#include "PluginPackage.h"
#include "CrashDiagnostics.h"
#include <QException>
#include <QJsonArray>
#include <utility>

namespace EchoMusic::PluginRuntimeDetail
{
template <typename Work>
PluginPackage importedPackage(const QString &id, const char *context, Work &&work)
{
    try {
        return std::forward<Work>(work)();
    } catch (...) {
        const auto exception = std::current_exception();
        CrashDiagnostics::recordException(context, exception);
        PluginPackage failed;
        failed.id = id;
        failed.error = QStringLiteral("插件文件导入失败：后台任务发生异常");
        // Diagnostics preserve the original type, message and nested Qt
        // exception. The management transaction receives a normal failure value.
        try {
            const auto details = CrashDiagnostics::exceptionDetails(context, exception);
            const auto chain = details.value("chain").toArray();
            QString reason;
            for (const auto &cause : chain)
                if (!cause.toObject().value("message").toString().isEmpty())
                    reason = cause.toObject().value("message").toString();
            if (!reason.isEmpty())
                failed.error = QStringLiteral("插件文件导入失败：%1").arg(reason);
        } catch (...) {
            // Keep the allocation-free fallback if describing an OOM also fails.
        }
        return failed;
    }
}
} // namespace EchoMusic::PluginRuntimeDetail
