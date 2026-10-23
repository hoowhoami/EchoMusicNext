#include "PluginFileService.h"
#include "PluginFileWorker_p.h"
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <utility>
#include <algorithm>
namespace EchoMusic
{
PluginFileService::PluginFileService(QString root, QObject *parent)
    : QAbstractListModel(parent), m_root(std::move(root))
{
    m_worker = new PluginFileWorker(*this, m_root);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(&m_thread, &QThread::finished, this, [this] {
        m_closed = true;
        emit closed();
    });
    connect(this, &PluginFileService::completed, this,
            [this](quint64 id, bool ok, const QVariant &, const QString &, const QString &message) {
                const auto pending = m_pending.take(id);
                if (pending.scope.isEmpty()) {
                    m_error = ok ? QString() : message;
                    emit changed();
                }
            });
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->initialize(); });
}
PluginFileService::~PluginFileService()
{
    shutdown();
    m_thread.wait();
}
int PluginFileService::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}
QVariant PluginFileService::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const auto row = m_rows[index.row()].toMap();
    return role == Qt::UserRole + 1   ? QVariant(row)
           : role == Qt::UserRole + 2 ? row.value("pluginName")
                                      : QVariant();
}
QHash<int, QByteArray> PluginFileService::roleNames() const
{
    return {{Qt::UserRole + 1, "grant"}, {Qt::UserRole + 2, "pluginName"}};
}
void PluginFileService::publishGrants(const QVariantList &rows)
{
    beginResetModel();
    m_rows = rows;
    m_loading = false;
    endResetModel();
    emit changed();
}
QVariantMap PluginFileService::grant(const QString &id) const
{
    for (const auto &value : m_rows)
        if (value.toMap().value("id") == id)
            return value.toMap();
    return {};
}
quint64 PluginFileService::request(const PluginFileOwner &owner, const QString &method,
                                   const QVariantMap &input)
{
    // Keep bounded control capacity so a full I/O queue cannot prevent grant
    // revocation or the track-change cancellation of an active transfer.
    const bool control = method == "revoke" || method == "removeGrant" ||
                         method == "downloadCancel" || method == "releaseMedia" ||
                         (method == "grant" && input.contains("replaceId"));
    if ((m_snapshot && !control) || m_closing || m_pending.size() >= (control ? 40 : 32))
        return 0;
    std::function<QSet<QString>(const QVariant &)> references = [&](const QVariant &value) {
        QSet<QString> result;
        if (value.metaType().id() == QMetaType::QVariantMap) {
            const auto map = value.toMap();
            if (map.contains("grantId"))
                result.insert(map.value("grantId").toString());
            for (const auto &child : map)
                result.unite(references(child));
        } else if (value.metaType().id() == QMetaType::QVariantList)
            for (const auto &child : value.toList())
                result.unite(references(child));
        return result;
    };
    const auto grantId = input.value(method == "grant" ? "replaceId" : "id").toString();
    if ((method == "revoke" || method == "removeGrant" || method == "grant") &&
        !grantId.isEmpty() &&
        (owner.installation.isEmpty() ||
         grant(grantId).value("installationId") == owner.installation)) {
        for (const auto &pending : std::as_const(m_pending))
            if (pending.grants.contains(grantId))
                pending.canceled->store(true);
        for (const auto &control : std::as_const(m_downloadControls))
            if (control.grant == grantId)
                control.canceled->store(true);
    }
    if (method == "downloadCancel") {
        const auto control = m_downloadControls.value(input.value("id").toString());
        if (control.installation == owner.installation && control.canceled)
            control.canceled->store(true);
    }
    if (owner.canceled)
        m_scopes.insert(owner.scope, owner.canceled);
    const auto id = ++m_next;
    auto canceled = std::make_shared<std::atomic_bool>(false);
    m_pending.insert(id, {owner.scope, owner.installation, references(input), canceled});
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, id, owner, method, input, canceled] {
        worker->execute(id, owner, method, input, canceled);
    });
    return id;
}
void PluginFileService::snapshotBarrier(std::function<void(bool)> ready)
{
    if (m_closing) {
        ready(false);
        return;
    }
    QMetaObject::invokeMethod(
        m_worker,
        [this, ready] {
            QMetaObject::invokeMethod(
                this, [this, ready] { ready(!m_closing && m_downloadControls.isEmpty()); },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}
void PluginFileService::cancel(quint64 id)
{
    if (m_pending.contains(id))
        m_pending[id].canceled->store(true);
}
void PluginFileService::closeScope(const QString &scope)
{
    if (m_scopes.contains(scope))
        m_scopes.take(scope)->store(true);
    for (const auto &pending : std::as_const(m_pending))
        if (pending.scope == scope)
            pending.canceled->store(true);
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker,
                                  [worker = m_worker, scope] { worker->closeScope(scope); });
}
void PluginFileService::forgetInstallation(const QString &id)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, id] { worker->forget(id); });
}
void PluginFileService::refresh()
{
    request({}, "refresh");
}
void PluginFileService::revoke(const QString &id)
{
    request({}, "revoke", {{"id", id}});
}
void PluginFileService::remove(const QString &id)
{
    request({}, "removeGrant", {{"id", id}});
}
void PluginFileService::reauthorize(const QString &id)
{
    const auto entry = grant(id);
    if (!m_closing && !entry.isEmpty())
        emit selectionRequested(entry);
}
void PluginFileService::shutdown()
{
    if (std::exchange(m_closing, true))
        return;
    for (const auto &scope : std::as_const(m_scopes))
        if (scope)
            scope->store(true);
    for (const auto &pending : std::as_const(m_pending))
        pending.canceled->store(true);
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] {
        worker->stop();
        QThread::currentThread()->quit();
    });
}
PluginFileWorker::PluginFileWorker(PluginFileService &value, QString path)
    : service(value), root(std::move(path))
{
}
PluginFileWorker::~PluginFileWorker() = default;
void PluginFileWorker::initialize()
{
    try {
        QDir().mkpath(root + "/resources");
        QFile file(root + "/resources/grants.json");
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
                throw PluginFileFailure{"StorageError", "Cannot read file grants"};
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(file.readAll(), &error);
            if (error.error != QJsonParseError::NoError || !document.isArray())
                throw PluginFileFailure{"StorageError", "File grant ledger is invalid"};
            for (const auto &value : document.array()) {
                const auto row = value.toObject().toVariantMap();
                PluginFileGrant grant;
                grant.owner = {row.value("installationId").toString(),
                               row.value("pluginId").toString(),
                               row.value("pluginName").toString(),
                               {},
                               {}};
                grant.id = row.value("id").toString();
                grant.path = row.value("path").toString();
                grant.kind = row.value("kind").toString();
                grant.access = row.value("access").toString();
                grant.revision = row.value("revision").toString();
                grant.revoked = row.value("revoked").toBool();
                grant.persistent = true;
                if (!grant.id.isEmpty() && !grant.owner.installation.isEmpty() &&
                    QDir::isAbsolutePath(grant.path))
                    grants.insert(grant.id, grant);
            }
        }
        loadDownloads();
        publishGrants();
    } catch (const PluginFileFailure &error) {
        initializationError = error.message;
        QMetaObject::invokeMethod(&service, [target = &service, error] {
            target->m_error = error.message;
            target->m_loading = false;
            emit target->changed();
        });
    }
}
void PluginFileWorker::check(FileCancellation cancel)
{
    if (cancel && cancel->load())
        throw PluginFileFailure{"Canceled", "File operation was canceled"};
}
void PluginFileWorker::complete(quint64 id, const QVariant &value)
{
    QMetaObject::invokeMethod(&service, [target = &service, id, value] {
        emit target->completed(id, true, value, {}, {});
    });
}
void PluginFileWorker::fail(quint64 id, const QString &code, const QString &message)
{
    QMetaObject::invokeMethod(&service, [target = &service, id, code, message] {
        emit target->completed(id, false, {}, code, message);
        if (!id) {
            target->m_error = message;
            emit target->changed();
        }
    });
}
void PluginFileWorker::execute(quint64 id, const PluginFileOwner &owner, const QString &method,
                               const QVariantMap &input, FileCancellation canceled)
{
    try {
        check(canceled);
        if (!initializationError.isEmpty())
            throw PluginFileFailure{"StorageError", initializationError};
        if (closing || (!owner.scope.isEmpty() && closedScopes.contains(owner.scope)))
            throw PluginFileFailure{"ScopeClosed", "File scope has ended"};
        QVariant result;
        if (method == "refresh")
            publishGrants();
        else if (method.startsWith("download"))
            result = downloads(owner, method, input);
        else if (method == "openMedia")
            result = openMedia(owner, input);
        else if (method == "releaseMedia") {
            const auto key = input.value("id").toString();
            const auto lease = media.value(key);
            if (media.contains(key) && (lease.owner.installation != owner.installation ||
                                        lease.owner.scope != owner.scope))
                throw PluginFileFailure{"AccessDenied", "Media lease belongs to another scope"};
            releaseMedia(key);
        } else
            result = files(id, owner, method, input, canceled);
        if (canceled && canceled->load()) {
            if (method == "openMedia")
                releaseMedia(result.toMap().value("id").toString());
            if (method == "downloadStart" && result.toMap().value("_created").toBool())
                downloads(owner, "downloadCancel", {{"id", result.toMap().value("id")}});
        }
        check(canceled);
        complete(id, result);
    } catch (const PluginFileFailure &error) {
        fail(id, error.code, error.message);
    }
}
QVariantMap PluginFileWorker::describe(const PluginFileGrant &grant) const
{
    const QFileInfo file(grant.path);
    return {
        {"id", grant.id},
        {"revision", grant.revision},
        {"name", file.fileName()},
        {"kind", grant.kind},
        {"access", grant.access},
        {"persistent", grant.persistent},
        {"revoked", grant.revoked},
        {"available", !grant.revoked && file.exists() && file.canonicalFilePath() == grant.path},
        {"displayPath", grant.path},
        {"file", reference(grant)},
        {"installationId", grant.owner.installation},
        {"pluginId", grant.owner.pluginId},
        {"pluginName", grant.owner.name}};
}
QVariantMap PluginFileWorker::reference(const PluginFileGrant &grant, const QString &path) const
{
    return {{"grantId", grant.id}, {"revision", grant.revision}, {"path", path}};
}
void PluginFileWorker::saveGrants()
{
    QJsonArray rows;
    for (const auto &grant : std::as_const(grants))
        if (grant.persistent) {
            auto row = describe(grant);
            row.remove("available");
            row.remove("file");
            row.remove("displayPath");
            row.insert("path", grant.path);
            rows.append(QJsonObject::fromVariantMap(row));
        }
    QSaveFile file(root + "/resources/grants.json");
    const auto data = QJsonDocument(rows).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        throw PluginFileFailure{"StorageError", "Could not save file grants"};
}
void PluginFileWorker::publishGrants()
{
    QVariantList rows;
    for (const auto &grant : std::as_const(grants))
        if (grant.kind != "data" && grant.kind != "cache")
            rows.append(describe(grant));
    std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
        return a.toMap().value("pluginName").toString() + a.toMap().value("id").toString() <
               b.toMap().value("pluginName").toString() + b.toMap().value("id").toString();
    });
    QMetaObject::invokeMethod(&service, [target = &service, rows] { target->publishGrants(rows); });
}
void PluginFileWorker::closeScope(const QString &scope)
{
    closedScopes.insert(scope);
    if (closedScopes.size() > 4096)
        closedScopes.erase(closedScopes.begin());
    retireMedia([&](const auto &owner, const auto &) { return owner.scope == scope; });
    retireDownloads([&](const auto &job) { return job.owner.scope == scope; }, "ScopeClosed");
    grants.removeIf([&](const auto &pair) {
        return !pair.value().persistent && pair.value().owner.scope == scope;
    });
    publishGrants();
    QMetaObject::invokeMethod(&service,
                              [target = &service, scope] { emit target->scopeClosed(scope); });
}
void PluginFileWorker::forget(const QString &installation)
{
    retireMedia(
        [&](const auto &owner, const auto &) { return owner.installation == installation; });
    retireDownloads([&](const auto &job) { return job.owner.installation == installation; },
                    "ScopeClosed");
    grants.removeIf(
        [&](const auto &pair) { return pair.value().owner.installation == installation; });
    QStringList removed;
    for (const auto &job : std::as_const(jobs))
        if (job->owner.installation == installation)
            removed.append(job->id);
    for (const auto &id : removed)
        jobs.remove(id);
    QDir(root + "/cache/" + installation).removeRecursively();
    QMetaObject::invokeMethod(&service, [target = &service, installation, removed] {
        for (const auto &id : removed) {
            target->m_downloadControls.remove(id);
            emit target->downloadRemoved(installation, id);
        }
    });
    try {
        saveDownloads();
        saveGrants();
        publishGrants();
    } catch (const PluginFileFailure &error) {
        fail(0, error.code, error.message);
    }
}
void PluginFileWorker::stop()
{
    closing = true;
    retireMedia([](const auto &, const auto &) { return true; });
    retireDownloads([](const auto &) { return true; }, "ScopeClosed");
    if (server)
        server->close();
}
} // namespace EchoMusic
