#include "EchoPluginFiles.h"
#include "EchoPluginContext.h"
#include "EchoPluginTask.h"
#include <QStringConverter>
#include <QQmlEngine>
namespace EchoMusic
{
EchoPluginOperation *EchoPluginFiles::fileOperation(const QString &method, const QVariantMap &file,
                                                    const QVariantMap &options,
                                                    const QStringList &allowed)
{
    if (!validOptions(options, allowed))
        return m_scope.rejected("InvalidArgument", "Unknown file operation option");
    auto input = options;
    input.remove("revision");
    input["file"] = file;
    return invoke(method, input, options);
}
EchoPluginOperation *EchoPluginFiles::stat(const QVariantMap &file, const QVariantMap &options)
{
    return fileOperation("stat", file, options, {});
}
EchoPluginOperation *EchoPluginFiles::metadata(const QVariantMap &file, const QVariantMap &options)
{
    return fileOperation("metadata", file, options, {});
}
EchoPluginOperation *EchoPluginFiles::mkdir(const QVariantMap &file, const QVariantMap &options)
{
    return fileOperation("mkdir", file, options, {"recursive"});
}
EchoPluginOperation *EchoPluginFiles::deleteFile(const QVariantMap &file,
                                                 const QVariantMap &options)
{
    return fileOperation("delete", file, options, {});
}
EchoPluginOperation *EchoPluginFiles::listFiles(const QVariantMap &file, const QVariantMap &options)
{
    return fileOperation("list", file, options, {"hidden", "depth", "limit", "kind"});
}
EchoPluginOperation *EchoPluginFiles::readText(const QVariantMap &file, const QVariantMap &options)
{
    return fileOperation("readText", file, options, {"offset", "maxBytes", "encoding"});
}
EchoPluginOperation *EchoPluginFiles::readBytes(const QVariantMap &file, const QVariantMap &options)
{
    return fileOperation("readBytes", file, options, {"offset", "maxBytes"});
}
EchoPluginOperation *EchoPluginFiles::writeText(const QVariantMap &file, const QString &text,
                                                const QVariantMap &options)
{
    if (!validOptions(options, {"encoding", "conflict"}))
        return m_scope.rejected("InvalidArgument", "Unknown file write option");
    const auto encoding = options.value("encoding", "utf-8").toString();
    if (!QStringList{"utf-8", "utf-16le", "utf-16be", "latin1"}.contains(encoding))
        return m_scope.rejected("InvalidArgument", "Unsupported text encoding");
    if (text.size() > 8 * 1024 * 1024)
        return m_scope.rejected("QuotaExceeded", "File writes accept at most 8 MiB");
    const auto type = encoding == "utf-16le"   ? QStringConverter::Utf16LE
                      : encoding == "utf-16be" ? QStringConverter::Utf16BE
                      : encoding == "latin1"   ? QStringConverter::Latin1
                                               : QStringConverter::Utf8;
    QStringEncoder encoder(type);
    const QByteArray bytes = encoder.encode(text);
    if (encoder.hasError())
        return m_scope.rejected("InvalidEncoding",
                                "Text cannot be encoded with this character set");
    auto write = options;
    write.remove("encoding");
    return writeBytes(file, bytes, write);
}
EchoPluginOperation *EchoPluginFiles::writeBytes(const QVariantMap &file, const QByteArray &bytes,
                                                 const QVariantMap &options)
{
    if (!validOptions(options, {"conflict"}))
        return m_scope.rejected("InvalidArgument", "Unknown file write option");
    if (bytes.size() > 8 * 1024 * 1024)
        return m_scope.rejected("QuotaExceeded", "File writes accept at most 8 MiB");
    auto input = options;
    input.remove("revision");
    input["file"] = file;
    input["bytes"] = bytes;
    return invoke("write", input, options);
}
EchoPluginOperation *EchoPluginFiles::copyFile(const QVariantMap &source, const QVariantMap &target,
                                               const QVariantMap &options)
{
    if (!validOptions(options, {"conflict"}))
        return m_scope.rejected("InvalidArgument", "Unknown copy option");
    auto input = options;
    input.remove("revision");
    input["file"] = source;
    input["target"] = target;
    auto *op = invoke("copy", input, options);
    if (!op->finished()) {
        auto *task = m_scope.ui()->createTask("file-copy-" + op->id(), tr("复制插件文件"));
        if (task) {
            connect(m_service, &PluginFileService::progress, op,
                    [task, this, op](quint64 nativeId, qint64 bytes, qint64 total) {
                        if (m_requests.value(nativeId).operation == op && !op->finished())
                            task->setProgress(total ? double(bytes) * 100 / total : 0,
                                              tr("正在复制"));
                    });
            connect(task, &EchoPluginTask::cancelRequested, op, &EchoPluginOperation::cancel);
            connect(op, &EchoPluginOperation::completed, task, [op, task] {
                if (op->state() == EchoPluginOperation::Succeeded)
                    task->complete();
                else if (op->state() == EchoPluginOperation::Canceled)
                    task->abort();
                else
                    task->fail(op->errorMessage());
            });
        }
    }
    return op;
}
EchoPluginOperation *EchoPluginFiles::openMedia(const QVariantMap &file, const QVariantMap &options)
{
    if (!validOptions(options, {"lifetime"}) ||
        !QStringList{"track", "scope"}.contains(options.value("lifetime", "track").toString()))
        return m_scope.rejected("InvalidArgument", "Unknown media lease lifetime");
    auto binding = options;
    if (options.value("lifetime", "track") == "track" && !binding.contains("revision"))
        binding["revision"] = m_scope.player()->revision();
    return invoke(
        "openMedia", {{"file", file}}, binding, [this, binding](auto *op, const auto &value) {
            m_media.removeIf([](const auto &lease) { return !lease || !lease->active(); });
            auto *lease = new EchoPluginMediaLease(*this, value.toMap());
            lease->m_revision = binding.value("revision").toString();
            m_media.append(lease);
            op->succeed(QVariant::fromValue(lease));
        });
}
} // namespace EchoMusic
