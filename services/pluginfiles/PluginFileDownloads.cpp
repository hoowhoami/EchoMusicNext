#include "PluginPaths.h"
#include "PluginFileWorker_p.h"
#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <cmath>
#include <QRegularExpression>
#include <QFileInfo>
#include <algorithm>
namespace EchoMusic
{
namespace
{
[[noreturn]] void reject(const QString &code, const QString &message)
{
    throw PluginFileFailure{code, message};
}
bool terminal(const QString &state)
{
    return QStringList{"completed", "failed", "canceled", "interrupted", "paused"}.contains(state);
}
void validateSource(const QVariantMap &input)
{
    const auto url = QUrl(input.value("url").toString());
    if (!url.isValid() || !QStringList{"http", "https"}.contains(url.scheme()) ||
        url.host().isEmpty() || !url.userName().isEmpty() || !url.password().isEmpty() ||
        url.toEncoded().size() > 8192)
        reject("InvalidArgument", "Downloads require an HTTP(S) URL without credentials");
    const auto headers = input.value("headers").toMap();
    qsizetype bytes = 0;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        if (it.key().isEmpty() || it.key().contains(QRegularExpression("[^A-Za-z0-9_-]")) ||
            it.value().metaType().id() != QMetaType::QString ||
            it.value().toString().contains('\r') || it.value().toString().contains('\n') ||
            it.value().toString().contains('\0') ||
            QStringList{"range", "if-range", "accept-encoding", "content-length",
                        "transfer-encoding", "connection", "host"}
                .contains(it.key().toLower()))
            reject("InvalidArgument", "Invalid or reserved download header");
        bytes += it.key().size() + it.value().toString().toUtf8().size();
    }
    if (bytes > 32768)
        reject("QuotaExceeded", "Download headers exceed 32 KiB");
}
qint64 integer(const QVariantMap &input, const QString &key, qint64 fallback, qint64 low,
               qint64 high)
{
    if (!input.contains(key))
        return fallback;
    bool ok = false;
    const auto n = input.value(key).toDouble(&ok);
    if (!ok || !std::isfinite(n) || std::floor(n) != n || n < double(low) || n > double(high))
        reject("InvalidArgument", "Invalid download size or timeout");
    return qint64(n);
}
} // namespace
QVariantMap PluginFileWorker::downloadSnapshot(const PluginDownload &job) const
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto rate =
        job.startedAt && now > job.startedAt
            ? double(job.received - job.startedBytes) * 1000 / double(now - job.startedAt)
            : 0;
    return {
        {"id", job.id},
        {"runId", QString::number(job.run)},
        {"revision", QString::number(job.revision)},
        {"state", job.state},
        {"lifetime", job.options.value("lifetime", "scope")},
        {"name",
         job.options.value("name", QFileInfo(job.target.value("path").toString()).fileName())},
        {"sourceKey", job.options.value("sourceKey", QString())},
        {"target", job.target},
        {"receivedBytes", double(job.received)},
        {"totalBytes", job.total >= 0 ? QVariant(double(job.total)) : QVariant::fromValue(nullptr)},
        {"bytesPerSecond", rate},
        {"remainingSeconds", rate > 0 && job.total >= job.received
                                 ? QVariant(double(job.total - job.received) / rate)
                                 : QVariant::fromValue(nullptr)},
        {"createdAt", double(job.created)},
        {"updatedAt", double(job.updated)},
        {"canPause", QStringList{"queued", "connecting", "downloading"}.contains(job.state)},
        {"canResume", QStringList{"paused", "interrupted"}.contains(job.state)},
        {"canRetry", QStringList{"failed", "canceled", "interrupted"}.contains(job.state)},
        {"errorCode", job.errorCode},
        {"errorMessage", job.errorMessage},
        {"result", job.result.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(job.result)}};
}
void PluginFileWorker::publishDownload(const std::shared_ptr<PluginDownload> &job)
{
    job->updated = QDateTime::currentMSecsSinceEpoch();
    ++job->revision;
    const auto value = downloadSnapshot(*job);
    QMetaObject::invokeMethod(&service, [target = &service, owner = job->owner, id = job->id,
                                         grant = job->target.value("grantId").toString(),
                                         canceled = job->canceled, value] {
        target->m_downloadControls.insert(id, {owner.installation, owner.scope, grant, canceled});
        emit target->downloadChanged(owner.installation, value);
    });
}
void PluginFileWorker::saveDownloads()
{
    QJsonArray rows;
    for (const auto &job : std::as_const(jobs)) {
        auto options = job->options;
        options.remove("headers");
        rows.append(QJsonObject{{"id", job->id},
                                {"installation", job->owner.installation},
                                {"pluginId", job->owner.pluginId},
                                {"name", job->owner.name},
                                {"options", QJsonObject::fromVariantMap(options)},
                                {"target", QJsonObject::fromVariantMap(job->target)},
                                {"state", job->state},
                                {"run", QString::number(job->run)},
                                {"revision", QString::number(job->revision)},
                                {"received", double(job->received)},
                                {"total", double(job->total)},
                                {"created", double(job->created)},
                                {"updated", double(job->updated)},
                                {"etag", job->etag},
                                {"lastModified", job->lastModified},
                                {"result", QJsonObject::fromVariantMap(job->result)},
                                {"errorCode", job->errorCode},
                                {"errorMessage", job->errorMessage}});
    }
    const auto data = QJsonDocument(rows).toJson(QJsonDocument::Compact);
    if (data.size() > 8 * 1024 * 1024)
        reject("QuotaExceeded", "Download history exceeds its storage budget");
    QSaveFile file(PluginPaths(root).state("downloads.json"));
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        reject("StorageError", "Download state could not be saved");
}
void PluginFileWorker::loadDownloads()
{
    QFile file(PluginPaths(root).state("downloads.json"));
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
        reject("StorageError", "Download history cannot be read");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray() ||
        document.array().size() > 256)
        reject("StorageError", "Download history is invalid");
    for (const auto &value : document.array()) {
        const auto row = value.toObject();
        auto job = std::make_shared<PluginDownload>();
        job->id = row.value("id").toString();
        if (QUuid(job->id).isNull())
            continue;
        job->owner = {row.value("installation").toString(),
                      row.value("pluginId").toString(),
                      row.value("name").toString(),
                      {},
                      {}};
        if (QUuid(job->owner.installation).isNull() || !PluginPaths::validId(job->owner.pluginId))
            continue;
        job->options = row.value("options").toObject().toVariantMap();
        job->target = row.value("target").toObject().toVariantMap();
        job->state = row.value("state").toString();
        if (!terminal(job->state)) {
            job->state = "interrupted";
            job->errorCode = "Interrupted";
            job->errorMessage = "The previous transfer was interrupted";
        }
        job->received = qMax<qint64>(0, qint64(row.value("received").toDouble()));
        job->total = qint64(row.value("total").toDouble(-1));
        job->run = qMax<quint64>(1, row.value("run").toString().toULongLong());
        job->revision = row.value("revision").toString().toULongLong();
        job->created = qint64(row.value("created").toDouble());
        job->updated = qint64(row.value("updated").toDouble());
        job->etag = row.value("etag").toString();
        job->lastModified = row.value("lastModified").toString();
        job->result = row.value("result").toObject().toVariantMap();
        job->partPath =
            PluginPaths(root).cache(job->owner.pluginId) + "/downloads/" + job->id + ".part";
        jobs.insert(job->id, job);
    }
}
QVariant PluginFileWorker::downloads(const PluginFileOwner &owner, const QString &method,
                                     const QVariantMap &input)
{
    if (method == "downloadList") {
        QVariantList result;
        QList<std::shared_ptr<PluginDownload>> ordered;
        for (const auto &job : std::as_const(jobs))
            if (job->owner.installation == owner.installation)
                ordered.append(job);
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto &a, const auto &b) { return a->created > b->created; });
        const auto offset = integer(input, "offset", 0, 0, 256),
                   limit = integer(input, "limit", 100, 1, 256);
        for (qsizetype i = offset; i < ordered.size() && result.size() < limit; ++i)
            result.append(downloadSnapshot(*ordered[i]));
        return result;
    }
    if (method == "downloadStart") {
        validateSource(input);
        const auto target = input.value("target").toMap();
        resolve(owner, target, true, false);
        const auto key = input.value("idempotencyKey").toString();
        if (key.size() > 128 || input.value("name").toString().size() > 512 ||
            input.value("sourceKey").toString().size() > 256)
            reject("InvalidArgument", "Download label or key is too long");
        if (!key.isEmpty())
            for (const auto &job : std::as_const(jobs))
                if (job->owner.installation == owner.installation &&
                    job->options.value("idempotencyKey") == key) {
                    if (job->target != target ||
                        job->options.value("_sourceUrl", job->options.value("url")) !=
                            input.value("url"))
                        reject("IdempotencyConflict",
                               "Download key is already bound to another source or target");
                    return downloadSnapshot(*job);
                }
        if (jobs.size() >= 256 || std::count_if(jobs.cbegin(), jobs.cend(), [&](const auto &job) {
                                      return job->owner.installation == owner.installation;
                                  }) >= 64)
            reject("QuotaExceeded", "Remove old download history before starting more tasks");
        auto options = input;
        options["_sourceUrl"] = input.value("url");
        options["maxBytes"] = double(
            integer(input, "maxBytes", 8LL * 1024 * 1024 * 1024, 1, 64LL * 1024 * 1024 * 1024));
        if (input.contains("expectedBytes"))
            options["expectedBytes"] = double(
                integer(input, "expectedBytes", 0, 0, qint64(options["maxBytes"].toDouble())));
        options["connectTimeoutMs"] = integer(input, "connectTimeoutMs", 15000, 100, 120000);
        options["idleTimeoutMs"] = integer(input, "idleTimeoutMs", 30000, 100, 300000);
        const auto checksum = input.value("checksum").toMap();
        if (!checksum.isEmpty() && (checksum.value("algorithm") != "sha256" ||
                                    !QRegularExpression("^[a-fA-F0-9]{64}$")
                                         .match(checksum.value("value").toString())
                                         .hasMatch()))
            reject("InvalidArgument", "Checksum must be a SHA-256 value");
        if (!QStringList{"fail", "rename", "replace"}.contains(
                input.value("conflict", "fail").toString()))
            reject("InvalidArgument", "Invalid conflict policy");
        auto job = std::make_shared<PluginDownload>();
        job->owner = owner;
        job->target = target;
        job->options = options;
        job->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        job->created = QDateTime::currentMSecsSinceEpoch();
        job->partPath = PluginPaths(root).cache(owner.pluginId) + "/downloads/" + job->id + ".part";
        jobs.insert(job->id, job);
        try {
            saveDownloads();
        } catch (...) {
            jobs.remove(job->id);
            throw;
        }
        publishDownload(job);
        pumpDownloads();
        auto snapshot = downloadSnapshot(*job);
        snapshot["_created"] = true;
        return snapshot;
    }
    const auto id = input.value("id").toString();
    const auto job = jobs.value(id);
    if (!job || job->owner.installation != owner.installation)
        reject("NotFound", "Download does not belong to this installation");
    if (method == "downloadGet")
        return downloadSnapshot(*job);
    if (method == "downloadRemove") {
        if (!terminal(job->state))
            reject("Busy", "Cancel the transfer before removing it");
        job->file.close();
        QFile::remove(job->partPath);
        jobs.remove(id);
        saveDownloads();
        QMetaObject::invokeMethod(&service,
                                  [target = &service, id, installation = owner.installation] {
                                      target->m_downloadControls.remove(id);
                                      emit target->downloadRemoved(installation, id);
                                  });
        return {};
    }
    if (method == "downloadPause") {
        if (!QStringList{"queued", "connecting", "downloading"}.contains(job->state))
            reject("InvalidState", "This download cannot be paused now");
        finishDownload(job, "paused");
    } else if (method == "downloadCancel") {
        if (job->state != "completed" && job->state != "canceled") {
            finishDownload(job, "canceled", "Canceled", "Download canceled");
            QFile::remove(job->partPath);
        }
    } else if (method == "downloadResume" || method == "downloadRetry") {
        if (method == "downloadResume" &&
            !QStringList{"paused", "interrupted"}.contains(job->state))
            reject("InvalidState", "This download cannot be resumed");
        if (method == "downloadRetry" &&
            !QStringList{"failed", "canceled", "interrupted"}.contains(job->state))
            reject("InvalidState", "This download cannot be retried");
        const auto source = input.value("source").toMap();
        if (!source.isEmpty()) {
            auto merged = job->options;
            for (auto it = source.begin(); it != source.end(); ++it)
                if (QStringList{"url", "headers"}.contains(it.key()))
                    merged[it.key()] = it.value();
                else if (it.key() != "target")
                    reject("InvalidArgument", "Unknown resume field");
            validateSource(merged);
            job->options = merged;
            if (source.contains("target"))
                job->target = source.value("target").toMap();
        }
        resolve(owner, job->target, true, false);
        if (method == "downloadRetry") {
            ++job->run;
            job->received = 0;
            job->total = -1;
            job->etag.clear();
            job->lastModified.clear();
            QFile::remove(job->partPath);
        }
        job->owner = owner;
        job->redirects = 0;
        job->canceled = std::make_shared<std::atomic_bool>(false);
        job->result.clear();
        job->errorCode.clear();
        job->errorMessage.clear();
        job->state = "queued";
        saveDownloads();
        publishDownload(job);
        pumpDownloads();
    } else
        reject("InvalidArgument", "Unknown download command");
    return downloadSnapshot(*job);
}
void PluginFileWorker::finishDownload(const std::shared_ptr<PluginDownload> &job,
                                      const QString &state, const QString &code,
                                      const QString &message)
{
    ++job->attempt;
    job->deadline.stop();
    job->idle.stop();
    job->publish.stop();
    if (job->reply) {
        auto *reply = job->reply.data();
        job->reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (job->file.isOpen()) {
        job->file.flush();
        job->file.close();
    }
    job->state = state;
    job->errorCode = code;
    job->errorMessage = message;
    publishDownload(job);
    try {
        saveDownloads();
    } catch (const PluginFileFailure &error) {
        fail(0, error.code, error.message);
    }
    QTimer::singleShot(0, this, [this] { pumpDownloads(); });
}
void PluginFileWorker::retireDownloads(const std::function<bool(const PluginDownload &)> &predicate,
                                       const QString &code)
{
    const auto values = jobs.values();
    for (const auto &job : values)
        if (!terminal(job->state) && predicate(*job)) {
            job->canceled->store(true);
            finishDownload(job, "interrupted", code, "Download resources were revoked");
        }
}
void PluginFileWorker::pumpDownloads()
{
    if (closing)
        return;
    int active = std::count_if(jobs.cbegin(), jobs.cend(), [](const auto &job) {
        return QStringList{"connecting", "downloading", "verifying", "committing"}.contains(
            job->state);
    });
    const auto values = jobs.values();
    for (const auto &job : values)
        if (active < 4 && job->state == "queued") {
            ++active;
            startDownload(job);
        }
}
} // namespace EchoMusic
