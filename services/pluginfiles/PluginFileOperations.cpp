#include "PluginPaths.h"
#include "PluginFileWorker_p.h"
#include "echo_ffi.h"
#include <QDirIterator>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QImageReader>
#include <QTemporaryFile>
#include <QSaveFile>
#include <QStringConverter>
#include <QUuid>
#include <QDateTime>
#include <cmath>
#include <QElapsedTimer>
namespace EchoMusic
{
namespace
{
[[noreturn]] void error(const QString &code, const QString &message)
{
    throw PluginFileFailure{code, message};
}
QVariantMap statValue(const PluginFileResolved &resolved)
{
    const QFileInfo info(resolved.path);
    if (!info.exists())
        error("NotFound", "File does not exist");
    return {
        {"file", resolved.reference},
        {"name", info.fileName()},
        {"size", double(info.size())},
        {"modifiedAt", double(info.lastModified().toMSecsSinceEpoch())},
        {"directory", info.isDir()},
        {"mimeType", QMimeDatabase().mimeTypeForFile(info, QMimeDatabase::MatchExtension).name()}};
}
QByteArray readBounded(const QString &path, qint64 offset, qint64 limit, FileCancellation cancel)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(offset))
        error("ReadError", file.errorString());
    QByteArray bytes;
    while (bytes.size() < limit && !file.atEnd()) {
        PluginFileWorker::check(cancel);
        const auto chunk = file.read(qMin<qint64>(128 * 1024, limit - bytes.size()));
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError)
            error("ReadError", file.errorString());
        bytes.append(chunk);
    }
    return bytes;
}
qint64 number(const QVariantMap &input, const QString &key, qint64 fallback, qint64 maximum)
{
    if (!input.contains(key))
        return fallback;
    bool ok = false;
    const auto n = input.value(key).toDouble(&ok);
    if (!ok || !std::isfinite(n) || std::floor(n) != n || n < 0 || n > double(maximum))
        error("InvalidArgument", "Invalid file offset or limit");
    return qint64(n);
}
} // namespace
QString PluginFileWorker::privateRoot(const QString &id, const QString &kind) const
{
    return kind == "cache" ? PluginPaths(root).cache(id) + "/files"
                           : PluginPaths(root).data(id) + "/files";
}
QString PluginFileWorker::relative(const QVariant &value)
{
    const auto path = value.toString();
    if (value.metaType().id() != QMetaType::QString || path.size() > 4096 ||
        QDir::isAbsolutePath(path) || path.contains('\0') || path.contains('\\') ||
        path.contains(':'))
        error("InvalidReference", "Expected a portable relative path");
    const auto parts = path.split('/');
    for (const auto &part : parts)
        if (part == ".." || part == ".")
            error("InvalidReference", "Parent traversal is not allowed");
    return path;
}
PluginFileResolved PluginFileWorker::resolve(const PluginFileOwner &owner, const QVariantMap &ref,
                                             bool write, bool existing) const
{
    const auto id = ref.value("grantId").toString();
    if (!grants.contains(id))
        error("GrantUnavailable", "File grant is missing");
    const auto grant = grants.value(id);
    if (grant.owner.installation != owner.installation)
        error("AccessDenied", "The file belongs to another installation");
    if (grant.revoked || grant.revision != ref.value("revision").toString())
        error("GrantRevoked", "File grant has been revoked or replaced");
    if (!grant.persistent && grant.owner.scope != owner.scope && grant.kind != "data" &&
        grant.kind != "cache")
        error("AccessDenied", "Temporary file grant belongs to another scope");
    if (write && grant.access != "read-write")
        error("ReadOnly", "This file grant is read-only");
    const auto path = relative(ref.value("path", QString()));
    const QFileInfo anchor(grant.path);
    if (!anchor.exists() || anchor.canonicalFilePath() != grant.path)
        error("GrantUnavailable", "Granted location is missing or was redirected");
    QString target = grant.path;
    if (grant.kind == "file") {
        if (!path.isEmpty())
            error("InvalidReference", "A selected file has no child paths");
    } else {
        for (const auto &part : path.split('/', Qt::SkipEmptyParts)) {
            target += '/' + part;
            const QFileInfo item(target);
            if (item.isSymLink() || item.isJunction())
                error("InvalidReference", "Symbolic links are not followed by file grants");
            if (item.exists() && item.canonicalFilePath() != QDir::cleanPath(target))
                error("InvalidReference", "File escaped its granted directory");
        }
    }
    if (existing && !QFileInfo::exists(target))
        error("NotFound", "File does not exist");
    return {target, grant, reference(grant, path)};
}
bool PluginFileWorker::authorized(const PluginFileOwner &owner, const QVariantMap &ref,
                                  bool write) const
{
    try {
        resolve(owner, ref, write, false);
        return true;
    } catch (const PluginFileFailure &) {
        return false;
    }
}
QString PluginFileWorker::conflictPath(const QString &path, const QString &mode)
{
    if (!QStringList{"fail", "rename", "replace"}.contains(mode))
        error("InvalidArgument", "Invalid conflict policy");
    if (!QFileInfo::exists(path) || mode == "replace")
        return path;
    if (mode == "fail")
        error("AlreadyExists", "The destination already exists");
    const QFileInfo file(path);
    for (int i = 1; i <= 10000; ++i) {
        const auto candidate = file.absolutePath() + '/' + file.completeBaseName() +
                               QString(" (%1)").arg(i) +
                               (file.suffix().isEmpty() ? QString() : '.' + file.suffix());
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    error("AlreadyExists", "No available destination name");
}
QVariant PluginFileWorker::files(quint64 id, const PluginFileOwner &owner, const QString &method,
                                 const QVariantMap &input, FileCancellation canceled)
{
    if (method == "grant") {
        const auto paths = input.value("paths").toStringList();
        if (paths.isEmpty() || paths.size() > 256 || owner.installation.isEmpty())
            error("InvalidArgument", "No valid files were selected");
        const auto access = input.value("access", "read").toString();
        const auto kind = input.value("kind").toString();
        if (!QStringList{"read", "read-write"}.contains(access) ||
            !QStringList{"directory", "file"}.contains(kind))
            error("InvalidArgument", "Invalid file grant");
        auto updated = grants;
        QVariantList result;
        for (const auto &path : paths) {
            check(canceled);
            const QFileInfo file(path);
            if (!file.exists() || (kind == "directory" ? !file.isDir() : !file.isFile()))
                error("NotFound", "Selected location is not available");
            PluginFileGrant grant{owner,
                                  QUuid::createUuid().toString(QUuid::WithoutBraces),
                                  file.canonicalFilePath(),
                                  kind,
                                  access,
                                  QUuid::createUuid().toString(QUuid::WithoutBraces),
                                  input.value("persist", true).toBool(),
                                  false};
            const auto replacing = input.value("replaceId").toString();
            if (!replacing.isEmpty()) {
                if (paths.size() != 1 || !updated.contains(replacing) ||
                    updated[replacing].owner.installation != owner.installation)
                    error("AccessDenied", "Cannot replace this grant");
                grant.id = replacing;
                if (!grant.persistent && grant.owner.scope.isEmpty())
                    grant.owner.scope = updated[replacing].owner.scope;
            }
            updated.insert(grant.id, grant);
            auto value = describe(grant);
            value["file"] = reference(grant);
            result.append(value);
        }
        if (updated.size() > 1024)
            error("QuotaExceeded", "Too many file grants");
        auto previous = std::exchange(grants, updated);
        try {
            check(canceled);
            saveGrants();
        } catch (...) {
            grants = previous;
            throw;
        }
        // Old revisions are never revived by reauthorization.
        retireMedia([&](const auto &o, const auto &ref) { return !authorized(o, ref); });
        retireDownloads([&](const auto &job) { return !authorized(job.owner, job.target, true); },
                        "GrantRevoked");
        publishGrants();
        return result;
    }
    if (method == "revoke" || method == "removeGrant") {
        const auto key = input.value("id").toString();
        if (!grants.contains(key) ||
            (!owner.installation.isEmpty() && grants[key].owner.installation != owner.installation))
            error("AccessDenied", "Cannot change this file grant");
        if (grants[key].kind == "data" || grants[key].kind == "cache")
            error("AccessDenied", "Private directories are managed by the host");
        grants[key].revoked = true;
        retireMedia([&](const auto &, const auto &ref) { return ref.value("grantId") == key; });
        retireDownloads([&](const auto &job) { return job.target.value("grantId") == key; },
                        "GrantRevoked");
        if (method == "removeGrant")
            grants.remove(key);
        saveGrants();
        publishGrants();
        return {};
    }
    if (method == "listGrants") {
        QVariantList result;
        for (const auto &grant : std::as_const(grants))
            if (grant.owner.installation == owner.installation && grant.kind != "data" &&
                grant.kind != "cache")
                result.append(describe(grant));
        return result;
    }
    if (method == "privateDirectory") {
        const auto kind = input.value("kind", "data").toString();
        if (!QStringList{"data", "cache"}.contains(kind))
            error("InvalidArgument", "Unknown private directory");
        if (!PluginPaths::validId(owner.pluginId))
            error("InvalidArgument", "Invalid private directory owner");
        const auto key = "private/" + owner.installation + '/' + kind;
        const auto path = privateRoot(owner.pluginId, kind);
        if (!QDir().mkpath(path))
            error("WriteError", "Cannot create private directory");
        if (!grants.contains(key)) {
            grants.insert(key, {owner, key, QFileInfo(path).canonicalFilePath(), kind, "read-write",
                                "1", true, false});
            saveGrants();
        }
        auto result = describe(grants[key]);
        result["file"] = reference(grants[key]);
        return result;
    }
    const auto ref = input.value("file").toMap();
    const bool write = QStringList{"write", "mkdir", "delete"}.contains(method);
    auto resolved = resolve(owner, ref, write, !write || method == "delete");
    if (method == "stat")
        return statValue(resolved);
    if (method == "list") {
        if (!QFileInfo(resolved.path).isDir())
            error("NotDirectory", "Expected a directory");
        const auto limit = number(input, "limit", 200, 5000), depth = number(input, "depth", 0, 32);
        const auto kind = input.value("kind", "all").toString();
        if (!QStringList{"all", "file", "directory", "image", "audio", "video"}.contains(kind))
            error("InvalidArgument", "Unknown file kind");
        QVariantList result;
        QStringList queue{resolved.path};
        bool truncated = false;
        int visited = 0;
        for (qsizetype at = 0; at < queue.size() && !truncated; ++at) {
            QDirIterator entries(
                queue[at],
                QDir::AllEntries | QDir::NoDotAndDotDot |
                    (input.value("hidden").toBool() ? QDir::Hidden : QDir::Filter(0)),
                QDirIterator::NoIteratorFlags);
            while (entries.hasNext()) {
                check(canceled);
                entries.next();
                const auto item = entries.fileInfo();
                if (item.isSymLink() || item.isJunction())
                    continue;
                if (++visited > 100000) {
                    truncated = true;
                    break;
                }
                const auto relativeToListing =
                    QDir(resolved.path).relativeFilePath(item.absoluteFilePath());
                if (item.isDir() && relativeToListing.count('/') < depth)
                    queue.append(item.absoluteFilePath());
                const auto mime =
                    QMimeDatabase().mimeTypeForFile(item, QMimeDatabase::MatchExtension).name();
                if (kind != "all" && !(kind == "directory" ? item.isDir()
                                       : kind == "file"    ? item.isFile()
                                                           : mime.startsWith(kind + '/')))
                    continue;
                if (result.size() >= limit) {
                    truncated = true;
                    break;
                }
                const auto child =
                    QDir(resolved.grant.path).relativeFilePath(item.absoluteFilePath());
                result.append(statValue(
                    {item.absoluteFilePath(), resolved.grant, reference(resolved.grant, child)}));
            }
        }
        return QVariantMap{{"entries", result}, {"truncated", truncated}};
    }
    if (method == "readBytes" || method == "readText") {
        if (!QFileInfo(resolved.path).isFile())
            error("NotFile", "Expected a file");
        const auto offset = number(input, "offset", 0, 9007199254740991LL),
                   limit = number(input, "maxBytes", 1024 * 1024, 8 * 1024 * 1024);
        const auto bytes = readBounded(resolved.path, offset, limit, canceled);
        QVariantMap result{{"bytesRead", bytes.size()},
                           {"truncated", QFileInfo(resolved.path).size() > offset + bytes.size()}};
        if (method == "readBytes")
            result["bytes"] = bytes;
        else {
            const auto encoding = input.value("encoding", "utf-8").toString();
            if (!QStringList{"utf-8", "utf-16le", "utf-16be", "latin1"}.contains(encoding))
                error("InvalidArgument", "Unsupported text encoding");
            const auto type = encoding == "utf-16le"   ? QStringConverter::Utf16LE
                              : encoding == "utf-16be" ? QStringConverter::Utf16BE
                              : encoding == "latin1"   ? QStringConverter::Latin1
                                                       : QStringConverter::Utf8;
            QStringDecoder decoder(type);
            const QString text = decoder.decode(bytes);
            if (decoder.hasError())
                error("InvalidEncoding", "File content does not match its declared encoding");
            result["text"] = text;
        }
        check(canceled);
        resolve(owner, ref, false);
        return result;
    }
    if (method == "metadata") {
        auto result = statValue(resolved);
        if (QFileInfo(resolved.path).isDir())
            return result;
        QImageReader image(resolved.path);
        const auto size = image.size();
        if (size.isValid()) {
            result["width"] = size.width();
            result["height"] = size.height();
        }
        if (result.value("mimeType").toString().startsWith("audio/") ||
            result.value("mimeType").toString().startsWith("video/")) {
            struct Probe
            {
                bool ok = false;
                QByteArray bytes;
            };
            Probe probe;
            const auto path = resolved.path.toUtf8();
            echo_audio_read_file_metadata(
                reinterpret_cast<const uint8_t *>(path.constData()), size_t(path.size()),
                [](void *context, const EchoResult *value) {
                    auto &probe = *static_cast<Probe *>(context);
                    probe.ok = value->status == EchoStatus::Success;
                    probe.bytes = QByteArray(reinterpret_cast<const char *>(value->payload),
                                             qsizetype(value->payload_len));
                },
                &probe);
            if (!probe.ok)
                error("MetadataError", "Media metadata could not be read");
            auto metadata = QJsonDocument::fromJson(probe.bytes).object().toVariantMap();
            result["media"] = QVariantMap{
                {"title", metadata.value("title")}, {"artist", metadata.value("artist")},
                {"album", metadata.value("album")}, {"durationMs", metadata.value("duration_ms")},
                {"year", metadata.value("year")},   {"track", metadata.value("track")},
                {"disk", metadata.value("disk")},   {"genre", metadata.value("genre")}};
        }
        check(canceled);
        resolve(owner, ref, false);
        return result;
    }
    if (method == "mkdir") {
        const bool ok = input.value("recursive").toBool() ? QDir().mkpath(resolved.path)
                                                          : QDir().mkdir(resolved.path);
        if (!ok && !QFileInfo(resolved.path).isDir())
            error("WriteError", "Cannot create directory");
        check(canceled);
        return statValue(resolve(owner, ref, true));
    }
    if (method == "delete") {
        check(canceled);
        const bool ok = QFileInfo(resolved.path).isDir() ? QDir().rmdir(resolved.path)
                                                         : QFile::remove(resolved.path);
        if (!ok)
            error("WriteError", "Cannot remove file or non-empty directory");
        publishGrants();
        return {};
    }
    if (method == "write" || method == "copy") {
        PluginFileResolved source;
        if (method == "copy") {
            source = resolved;
            resolved = resolve(owner, input.value("target").toMap(), true, false);
            if (!QFileInfo(source.path).isFile())
                error("NotFile", "Only file copies are supported");
        }
        const auto mode = input.value("conflict", "fail").toString();
        if (resolved.grant.kind == "file" && mode == "rename")
            error("InvalidArgument", "A selected file cannot authorize a renamed sibling");
        const auto destination = conflictPath(resolved.path, mode);
        auto targetRef = resolved.reference;
        if (resolved.grant.kind != "file")
            targetRef["path"] = QDir(resolved.grant.path).relativeFilePath(destination);
        resolve(owner, targetRef, true, false);
        if (method == "copy" && source.path == destination)
            error("InvalidArgument", "Source and destination are the same file");
        QTemporaryFile temporary(QFileInfo(destination).absolutePath() + "/.echo-file-XXXXXX");
        if (!temporary.open())
            error("WriteError", temporary.errorString());
        qint64 copied = 0;
        if (method == "write") {
            const auto bytes = input.value("bytes").toByteArray();
            if (bytes.size() > 8 * 1024 * 1024)
                error("QuotaExceeded", "A file write accepts at most 8 MiB");
            if (temporary.write(bytes) != bytes.size())
                error("WriteError", temporary.errorString());
            copied = bytes.size();
        } else {
            QFile file(source.path);
            if (!file.open(QIODevice::ReadOnly))
                error("ReadError", file.errorString());
            const auto total = file.size();
            QElapsedTimer progressClock;
            progressClock.start();
            while (!file.atEnd()) {
                check(canceled);
                resolve(owner, source.reference, false);
                resolve(owner, targetRef, true, false);
                const auto chunk = file.read(128 * 1024);
                if ((chunk.isEmpty() && file.error() != QFileDevice::NoError) ||
                    temporary.write(chunk) != chunk.size())
                    error("WriteError", "File copy failed");
                copied += chunk.size();
                if (progressClock.elapsed() >= 200 || copied == total) {
                    progressClock.restart();
                    QMetaObject::invokeMethod(&service, [target = &service, id, copied, total] {
                        emit target->progress(id, copied, total);
                    });
                }
            }
        }
        if (!temporary.flush())
            error("WriteError", temporary.errorString());
        temporary.close();
        check(canceled);
        resolve(owner, targetRef, true, false);
        if (mode == "replace") {
            QFile sourceFile(temporary.fileName());
            QSaveFile output(destination);
            if (!sourceFile.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
                error("WriteError", "Cannot open atomic destination");
            while (!sourceFile.atEnd()) {
                check(canceled);
                const auto chunk = sourceFile.read(128 * 1024);
                if (output.write(chunk) != chunk.size())
                    error("WriteError", output.errorString());
            }
            check(canceled);
            resolve(owner, targetRef, true, false);
            if (!output.commit())
                error("WriteError", output.errorString());
        } else if (!QFile::rename(temporary.fileName(), destination))
            error("AlreadyExists", "The destination changed before commit");
        return QVariantMap{{"file", targetRef}, {"bytes", double(copied)}};
    }
    error("InvalidArgument", "Unknown file operation");
}
} // namespace EchoMusic
