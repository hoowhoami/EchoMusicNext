#include "PluginFileWorker_p.h"
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>
namespace EchoMusic
{
namespace
{
void checkDownload(const PluginDownload &job)
{
    if (job.owner.canceled && job.owner.canceled->load())
        throw PluginFileFailure{"ScopeClosed", "Download scope has ended"};
    if (job.canceled->load())
        throw PluginFileFailure{"Canceled", "Download was canceled"};
}
bool sameOrigin(const QUrl &a, const QUrl &b)
{
    return a.scheme() == b.scheme() && a.host() == b.host() && a.port() == b.port();
}
} // namespace
void PluginFileWorker::startDownload(const std::shared_ptr<PluginDownload> &job)
{
    try {
        checkDownload(*job);
        resolve(job->owner, job->target, true, false);
        const auto destination = resolve(job->owner, job->target, true, false);
        if (QFileInfo(destination.path).isDir())
            throw PluginFileFailure{"NotFile", "Download target must name a file"};
        if (!network)
            network = new QNetworkAccessManager(this);
        if (!QDir().mkpath(QFileInfo(job->partPath).absolutePath()))
            throw PluginFileFailure{"WriteError", "Cannot create download staging directory"};
        job->file.setFileName(job->partPath);
        if (!job->file.open(QIODevice::ReadWrite))
            throw PluginFileFailure{"WriteError", job->file.errorString()};
        job->received = job->file.size();
        if (!job->options.value("resume", true).toBool() ||
            (job->etag.isEmpty() && job->lastModified.isEmpty())) {
            if (!job->file.resize(0))
                throw PluginFileFailure{"WriteError", job->file.errorString()};
            job->received = 0;
        }
        if (job->received > qint64(job->options.value("maxBytes").toDouble()))
            throw PluginFileFailure{"QuotaExceeded", "Partial file exceeds download budget"};
        job->hash.reset();
        if (!job->file.seek(0))
            throw PluginFileFailure{"ReadError", "Cannot read partial download"};
        while (!job->file.atEnd()) {
            checkDownload(*job);
            const auto bytes = job->file.read(128 * 1024);
            if (bytes.isEmpty() && job->file.error() != QFileDevice::NoError)
                throw PluginFileFailure{"ReadError", job->file.errorString()};
            job->hash.addData(bytes);
        }
        job->headers = false;
        job->expectedEnd = -1;
        job->state = "connecting";
        job->startedAt = QDateTime::currentMSecsSinceEpoch();
        job->startedBytes = job->received;
        const auto attempt = ++job->attempt;
        QNetworkRequest request(QUrl(job->options.value("url").toString()));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        request.setRawHeader("Accept-Encoding", "identity");
        request.setRawHeader("User-Agent", "EchoMusicNext/0.1.0");
        const auto headers = job->options.value("headers").toMap();
        for (auto it = headers.begin(); it != headers.end(); ++it)
            request.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());
        if (job->received) {
            request.setRawHeader("Range", "bytes=" + QByteArray::number(job->received) + '-');
            request.setRawHeader("If-Range",
                                 (!job->etag.isEmpty() ? job->etag : job->lastModified).toUtf8());
        }
        auto *reply = network->get(request);
        job->reply = reply;
        reply->setReadBufferSize(256 * 1024);
        const std::weak_ptr<PluginDownload> weak(job);
        connect(reply, &QNetworkReply::metaDataChanged, this, [this, weak, attempt] {
            if (const auto job = weak.lock(); job && job->attempt == attempt)
                consumeDownload(job);
        });
        connect(reply, &QNetworkReply::readyRead, this, [this, weak, attempt] {
            if (const auto job = weak.lock(); job && job->attempt == attempt)
                consumeDownload(job);
        });
        connect(reply, &QNetworkReply::finished, this, [this, weak, attempt] {
            const auto job = weak.lock();
            if (!job || job->attempt != attempt || !job->reply)
                return;
            consumeDownload(job);
            if (job->attempt != attempt || !job->reply)
                return;
            if (job->reply->error() != QNetworkReply::NoError) {
                finishDownload(job, "interrupted", "NetworkError", job->reply->errorString());
                return;
            }
            if (!job->headers) {
                finishDownload(job, "failed", "InvalidResponse",
                               "Download response has no valid headers");
                return;
            }
            if ((job->total >= 0 && job->received != job->total) ||
                (job->expectedEnd >= 0 && job->received != job->expectedEnd + 1)) {
                finishDownload(job, "interrupted", "Incomplete",
                               "Download ended before its declared size");
                return;
            }
            commitDownload(job);
        });
        job->deadline.disconnect(this);
        job->idle.disconnect(this);
        job->publish.disconnect(this);
        job->deadline.setSingleShot(true);
        job->idle.setSingleShot(true);
        job->publish.setInterval(200);
        connect(&job->deadline, &QTimer::timeout, this, [this, weak, attempt] {
            if (const auto job = weak.lock(); job && job->attempt == attempt)
                finishDownload(job, "interrupted", "ConnectTimeout",
                               "Download connection timed out");
        });
        connect(&job->idle, &QTimer::timeout, this, [this, weak, attempt] {
            if (const auto job = weak.lock(); job && job->attempt == attempt)
                finishDownload(job, "interrupted", "IdleTimeout",
                               "Download stopped receiving data");
        });
        connect(&job->publish, &QTimer::timeout, this, [this, weak, attempt] {
            if (const auto job = weak.lock(); job && job->attempt == attempt)
                publishDownload(job);
        });
        job->deadline.start(job->options.value("connectTimeoutMs").toInt());
        job->publish.start();
        publishDownload(job);
    } catch (const PluginFileFailure &error) {
        finishDownload(job,
                       error.code == "ScopeClosed" ? "interrupted"
                       : error.code == "Canceled"  ? "canceled"
                                                   : "failed",
                       error.code, error.message);
    }
}
void PluginFileWorker::consumeDownload(const std::shared_ptr<PluginDownload> &job)
{
    if (!job->reply)
        return;
    try {
        checkDownload(*job);
        resolve(job->owner, job->target, true, false);
        auto *reply = job->reply.data();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (!status)
            return;
        if (status >= 300 && status < 400) {
            const auto next = reply->url().resolved(
                reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
            if (++job->redirects > 5 || !next.isValid() ||
                !QStringList{"http", "https"}.contains(next.scheme()) ||
                (reply->url().scheme() == "https" && next.scheme() != "https") ||
                !next.userName().isEmpty() || !next.password().isEmpty())
                throw PluginFileFailure{"RedirectError", "Download redirect is invalid or unsafe"};
            auto headers = job->options.value("headers").toMap();
            if (!sameOrigin(reply->url(), next))
                for (const auto &key : headers.keys())
                    if (QStringList{"authorization", "cookie", "proxy-authorization"}.contains(
                            key.toLower()))
                        headers.remove(key);
            job->options["headers"] = headers;
            job->options["url"] = next.toString(QUrl::FullyEncoded);
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
            job->reply = nullptr;
            job->file.flush();
            job->file.close();
            job->deadline.stop();
            job->idle.stop();
            job->publish.stop();
            ++job->attempt;
            QTimer::singleShot(0, this, [this, job] { startDownload(job); });
            return;
        }
        if (status != 200 && status != 206)
            throw PluginFileFailure{"HttpError", QString("Download returned HTTP %1").arg(status)};
        if (!job->headers) {
            const auto encoding = reply->rawHeader("Content-Encoding").trimmed().toLower();
            if (!encoding.isEmpty() && encoding != "identity")
                throw PluginFileFailure{
                    "InvalidResponse",
                    "Encoded transfer bodies are not accepted for resumable files"};
            const auto maximum = qint64(job->options.value("maxBytes").toDouble());
            bool lengthOk = false;
            const auto length = reply->rawHeader("Content-Length").toLongLong(&lengthOk);
            if (lengthOk && (length < 0 || length > maximum))
                throw PluginFileFailure{"QuotaExceeded",
                                        "Download response exceeds its byte budget"};
            if (status == 206) {
                const auto match =
                    QRegularExpression("^bytes ([0-9]+)-([0-9]+)/([0-9]+|\\*)$")
                        .match(QString::fromLatin1(reply->rawHeader("Content-Range")));
                bool startOk = false, endOk = false, totalOk = false;
                const auto start = match.captured(1).toLongLong(&startOk),
                           end = match.captured(2).toLongLong(&endOk);
                const auto total =
                    match.captured(3) == "*" ? -1 : match.captured(3).toLongLong(&totalOk);
                if (!match.hasMatch() || !startOk || !endOk || start != job->received ||
                    end < start || end >= maximum ||
                    (total != -1 && (!totalOk || total <= end || total > maximum)) ||
                    (lengthOk && length != end - start + 1))
                    throw PluginFileFailure{"InvalidRange",
                                            "Download response does not match the requested range"};
                const auto etag = QString::fromLatin1(reply->rawHeader("ETag"));
                if (job->received && !job->etag.isEmpty() && !etag.isEmpty() && etag != job->etag)
                    throw PluginFileFailure{"SourceChanged",
                                            "Download source changed during resume"};
                job->total = total;
                job->expectedEnd = end;
            } else {
                if (job->received) {
                    if (!job->file.resize(0) || !job->file.seek(0))
                        throw PluginFileFailure{"WriteError", "Cannot restart partial file"};
                    job->received = 0;
                    job->hash.reset();
                }
                job->total = lengthOk ? length : -1;
            }
            if (job->options.contains("expectedBytes") && job->total >= 0 &&
                job->total != qint64(job->options.value("expectedBytes").toDouble()))
                throw PluginFileFailure{"SizeMismatch",
                                        "Download size does not match its declaration"};
            const auto validator = [reply](const QByteArray &key) {
                const auto value = QString::fromLatin1(reply->rawHeader(key));
                return value.size() <= 1024 && !value.contains('\r') && !value.contains('\n')
                           ? value
                           : QString();
            };
            job->etag = validator("ETag");
            if (job->etag.startsWith("W/"))
                job->etag.clear();
            job->lastModified = validator("Last-Modified");
            job->headers = true;
            job->state = "downloading";
            job->deadline.stop();
            job->idle.start(job->options.value("idleTimeoutMs").toInt());
            publishDownload(job);
            try {
                saveDownloads();
            } catch (const PluginFileFailure &error) {
                throw error;
            }
        }
        while (reply->bytesAvailable()) {
            checkDownload(*job);
            resolve(job->owner, job->target, true, false);
            const auto bytes = reply->read(128 * 1024);
            if (bytes.isEmpty())
                break;
            if (job->received + bytes.size() > qint64(job->options.value("maxBytes").toDouble()) ||
                (job->total >= 0 && job->received + bytes.size() > job->total))
                throw PluginFileFailure{"QuotaExceeded",
                                        "Download exceeded its declared byte budget"};
            if (job->file.write(bytes) != bytes.size())
                throw PluginFileFailure{"WriteError", job->file.errorString()};
            job->received += bytes.size();
            job->hash.addData(bytes);
            job->idle.start(job->options.value("idleTimeoutMs").toInt());
        }
    } catch (const PluginFileFailure &error) {
        finishDownload(job,
                       error.code == "ScopeClosed" ? "interrupted"
                       : error.code == "Canceled"  ? "canceled"
                                                   : "failed",
                       error.code, error.message);
    }
}
void PluginFileWorker::commitDownload(const std::shared_ptr<PluginDownload> &job)
{
    try {
        checkDownload(*job);
        job->state = "verifying";
        publishDownload(job);
        if (!job->file.flush())
            throw PluginFileFailure{"WriteError", job->file.errorString()};
        job->file.close();
        if (job->options.contains("expectedBytes") &&
            job->received != qint64(job->options.value("expectedBytes").toDouble()))
            throw PluginFileFailure{"SizeMismatch",
                                    "Downloaded size does not match its declaration"};
        const auto hash = QString::fromLatin1(job->hash.result().toHex());
        const auto expected = job->options.value("checksum").toMap().value("value").toString();
        if (!expected.isEmpty() && expected.compare(hash, Qt::CaseInsensitive) != 0)
            throw PluginFileFailure{"ChecksumMismatch", "Downloaded SHA-256 does not match"};
        auto resolved = resolve(job->owner, job->target, true, false);
        const auto mode = job->options.value("conflict", "fail").toString();
        if (resolved.grant.kind == "file" && mode == "rename")
            throw PluginFileFailure{"InvalidArgument",
                                    "A selected file cannot authorize a renamed sibling"};
        const auto path = conflictPath(resolved.path, mode);
        auto finalRef = resolved.reference;
        if (resolved.grant.kind != "file")
            finalRef["path"] = QDir(resolved.grant.path).relativeFilePath(path);
        resolve(job->owner, finalRef, true, false);
        job->state = "committing";
        publishDownload(job);
        QFile source(job->partPath);
        if (!source.open(QIODevice::ReadOnly))
            throw PluginFileFailure{"ReadError", "Cannot read completed staging file"};
        const auto copy = [&](QIODevice &target) {
            while (!source.atEnd()) {
                checkDownload(*job);
                resolve(job->owner, finalRef, true, false);
                const auto bytes = source.read(128 * 1024);
                if ((bytes.isEmpty() && source.error() != QFileDevice::NoError) ||
                    target.write(bytes) != bytes.size())
                    throw PluginFileFailure{"WriteError", "Cannot commit downloaded file"};
            }
            checkDownload(*job);
            resolve(job->owner, finalRef, true, false);
        };
        if (mode == "replace") {
            QSaveFile output(path);
            if (!output.open(QIODevice::WriteOnly))
                throw PluginFileFailure{"WriteError", output.errorString()};
            copy(output);
            if (!output.commit())
                throw PluginFileFailure{"WriteError", output.errorString()};
        } else {
            QTemporaryFile output(QFileInfo(path).absolutePath() + "/.echo-download-XXXXXX");
            if (!output.open())
                throw PluginFileFailure{"WriteError", output.errorString()};
            copy(output);
            if (!output.flush())
                throw PluginFileFailure{"WriteError", output.errorString()};
            output.close();
            if (!QFile::rename(output.fileName(), path))
                throw PluginFileFailure{"AlreadyExists",
                                        "Download destination changed before commit"};
        }
        source.close();
        QFile::remove(job->partPath);
        job->result = {{"taskId", job->id},
                       {"file", finalRef},
                       {"bytes", double(job->received)},
                       {"sha256", hash}};
        finishDownload(job, "completed");
    } catch (const PluginFileFailure &error) {
        finishDownload(job,
                       error.code == "ScopeClosed" ? "interrupted"
                       : error.code == "Canceled"  ? "canceled"
                                                   : "failed",
                       error.code, error.message);
    }
}
} // namespace EchoMusic
