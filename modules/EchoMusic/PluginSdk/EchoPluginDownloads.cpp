#include "EchoPluginDownloads.h"
#include "EchoPluginContext.h"
#include "EchoPluginTask.h"
#include <QQmlEngine>
#include <QTimer>
namespace EchoMusic
{
namespace
{
EchoPluginOperation *expiredDownload()
{
    auto *op = new EchoPluginOperation(nullptr);
    QQmlEngine::setObjectOwnership(op, QQmlEngine::JavaScriptOwnership);
    op->fail("ScopeClosed", "Download handle has expired");
    return op;
}
} // namespace
EchoPluginDownloads::EchoPluginDownloads(EchoPluginFiles &files) : QObject(&files), m_files(files)
{
}
EchoPluginOperation *EchoPluginDownloads::start(const QVariantMap &options)
{
    if (!m_files.validOptions(options,
                              {"url", "headers", "target", "name", "sourceKey", "idempotencyKey",
                               "conflict", "resume", "expectedBytes", "maxBytes", "checksum",
                               "connectTimeoutMs", "idleTimeoutMs", "lifetime"}))
        return m_files.m_scope.rejected("InvalidArgument", "Unknown download option");
    if (!QStringList{"track", "scope"}.contains(options.value("lifetime", "scope").toString()))
        return m_files.m_scope.rejected("InvalidArgument", "Invalid download lifetime");
    auto binding = options;
    if (options.value("lifetime", "scope") == "track" && !binding.contains("revision"))
        binding["revision"] = m_files.m_scope.player()->revision();
    auto input = options;
    input.remove("revision");
    input["lifetime"] = binding.contains("revision") ? "track" : "scope";
    return m_files.invoke(
        "downloadStart", input, binding, [this, binding](auto *op, const auto &value) {
            auto *handle = m_files.download(value.toMap(), true);
            if (binding.contains("revision"))
                m_files.m_boundDownloads.insert(handle->id(), binding.value("revision").toString());
            op->succeed(QVariant::fromValue(handle));
        });
}
EchoPluginOperation *EchoPluginDownloads::list(int offset, int limit)
{
    return m_files.invoke("downloadList", {{"offset", offset}, {"limit", limit}});
}
EchoPluginOperation *EchoPluginDownloads::get(const QString &id)
{
    return m_files.invoke("downloadGet", {{"id", id}});
}
EchoPluginOperation *EchoPluginDownloads::attach(const QString &id)
{
    return m_files.invoke("downloadGet", {{"id", id}}, {}, [this](auto *op, const auto &value) {
        op->succeed(QVariant::fromValue(m_files.download(value.toMap())));
    });
}
EchoPluginOperation *EchoPluginDownloads::remove(const QString &id)
{
    return m_files.invoke("downloadRemove", {{"id", id}}, {}, [this, id](auto *op, const auto &) {
        if (auto handle = m_files.m_downloads.take(id))
            handle->invalidate();
        m_files.m_boundDownloads.remove(id);
        op->succeed();
    });
}
EchoPluginDownload *EchoPluginFiles::download(const QVariantMap &value, bool task)
{
    const auto id = value.value("id").toString();
    auto *handle = m_downloads.value(id).data();
    if (!handle) {
        handle = new EchoPluginDownload(*this, value);
        m_downloads.insert(id, handle);
    } else
        handle->accept(value);
    if (task)
        handle->ensureTask();
    return handle;
}
EchoPluginDownload::EchoPluginDownload(EchoPluginFiles &files, const QVariantMap &value)
    : QObject(&files), m_files(files), m_snapshot(value)
{
    m_snapshot.remove("_created");
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}
void EchoPluginDownload::ensureTask()
{
    if (m_task && m_task->active())
        return;
    m_task = m_files.m_scope.ui()->createTask("download-" + id(),
                                              m_snapshot.value("name", tr("插件下载")).toString());
    if (m_task) {
        connect(m_task, &EchoPluginTask::cancelRequested, this, [this] {
            const auto state = m_snapshot.value("state").toString();
            if (m_active && state != "completed" && state != "failed" && state != "canceled" &&
                state != "interrupted")
                cancel();
        });
        accept(m_snapshot);
    }
}
void EchoPluginDownload::accept(const QVariantMap &snapshot)
{
    if (!m_active || snapshot.value("revision").toString().toULongLong() <
                         m_snapshot.value("revision").toString().toULongLong())
        return;
    m_snapshot = snapshot;
    m_snapshot.remove("_created");
    if (m_task) {
        const auto state = m_snapshot.value("state").toString();
        if (state == "completed")
            m_task->complete();
        else if (state == "failed")
            m_task->fail(m_snapshot.value("errorMessage").toString());
        else if (state == "canceled" || state == "interrupted")
            m_task->abort();
        else {
            const auto total = m_snapshot.value("totalBytes").toDouble(),
                       bytes = m_snapshot.value("receivedBytes").toDouble();
            m_task->setProgress(total > 0 ? qBound(0., bytes * 100 / total, 100.) : 0,
                                state == "paused" ? tr("已暂停") : tr("正在下载"));
        }
    }
    emit changed();
}
void EchoPluginDownload::invalidate()
{
    if (std::exchange(m_active, false)) {
        if (m_task)
            m_task->dispose();
        emit changed();
        QTimer::singleShot(0, this, [this] {
            setParent(nullptr);
            QQmlEngine::setObjectOwnership(this, QQmlEngine::JavaScriptOwnership);
        });
    }
}
EchoPluginOperation *EchoPluginDownload::command(const QString &command, const QVariantMap &source)
{
    if (!m_active)
        return expiredDownload();
    QVariantMap binding;
    if ((command == "downloadRetry" || command == "downloadResume") &&
        m_snapshot.value("lifetime") == "track") {
        const auto revision = m_files.m_scope.player()->revision();
        m_files.m_boundDownloads.insert(id(), revision);
        binding["revision"] = revision;
    }
    return m_files.invoke(
        command, {{"id", id()}, {"source", source}}, binding,
        [guard = QPointer<EchoPluginDownload>(this), command](auto *op, const auto &value) {
            if (!guard || !guard->m_active) {
                op->fail("ScopeClosed", "Download handle has expired");
                return;
            }
            if (command == "downloadRetry" || command == "downloadResume") {
                if (guard->m_task) {
                    // Rebinding task presentation must not cancel the new run.
                    disconnect(guard->m_task, &EchoPluginTask::cancelRequested, guard.data(),
                               nullptr);
                    guard->m_task->dispose();
                }
                guard->m_task = nullptr;
            }
            guard->accept(value.toMap());
            if (command == "downloadRetry" || command == "downloadResume")
                guard->ensureTask();
            op->succeed(guard->m_snapshot);
        });
}
EchoPluginOperation *EchoPluginDownload::pause()
{
    return command("downloadPause");
}
EchoPluginOperation *EchoPluginDownload::resume(const QVariantMap &source)
{
    return command("downloadResume", source);
}
EchoPluginOperation *EchoPluginDownload::retry(const QVariantMap &source)
{
    return command("downloadRetry", source);
}
EchoPluginOperation *EchoPluginDownload::cancel()
{
    return command("downloadCancel");
}
EchoPluginOperation *EchoPluginDownload::wait()
{
    if (!m_active)
        return expiredDownload();
    auto *op = m_files.m_scope.operation();
    if (op->finished())
        return op;
    op->start();
    const auto run = m_snapshot.value("runId").toString();
    const auto settle = [this, op, run] {
        if (op->finished())
            return;
        if (!m_active) {
            op->cancel();
            return;
        }
        if (m_snapshot.value("runId") != run) {
            op->fail("StaleRun", "Download started a different run");
            return;
        }
        const auto state = m_snapshot.value("state").toString();
        if (state == "completed")
            op->succeed(m_snapshot.value("result"));
        else if (state == "canceled")
            op->cancel();
        else if (state == "failed" || state == "interrupted")
            op->fail(m_snapshot.value("errorCode").toString(),
                     m_snapshot.value("errorMessage").toString());
    };
    connect(this, &EchoPluginDownload::changed, op, settle);
    settle();
    return op;
}
} // namespace EchoMusic
