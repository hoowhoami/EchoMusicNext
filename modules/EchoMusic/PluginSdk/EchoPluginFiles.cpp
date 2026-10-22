#include "EchoPluginFiles.h"
#include "EchoPluginContext.h"
#include "EchoPluginDownloads.h"
#include "EchoPluginFilePicker.h"
#include "PlaybackService.h"
#include "ApiClient.h"
#include <QUuid>
#include <QTimer>
#include <QQmlEngine>
namespace EchoMusic
{
EchoPluginFiles::EchoPluginFiles(EchoPluginContext &scope)
    : QObject(&scope), m_scope(scope), m_service(scope.host().files)
{
    resetScope();
    if (m_service) {
        connect(m_service, &PluginFileService::completed, this,
                [this](quint64 id, bool ok, const QVariant &value, const QString &code,
                       const QString &message) {
                    if (!m_requests.contains(id))
                        return;
                    const auto request = m_requests.take(id);
                    auto *op = request.operation.data();
                    if (!op || op->finished() || request.owner.scope != m_owner.scope ||
                        m_closing) {
                        if (ok && request.method == "openMedia")
                            m_service->request(request.owner, "releaseMedia",
                                               {{"id", value.toMap().value("id")}});
                        if (ok && request.method == "downloadStart" &&
                            value.toMap().value("_created").toBool())
                            m_service->request(request.owner, "downloadCancel",
                                               {{"id", value.toMap().value("id")}});
                    } else if (!ok) {
                        if (code == "Canceled" || code == "ScopeClosed")
                            op->cancel();
                        else
                            op->fail(code, message);
                    } else if (request.adapt)
                        request.adapt(op, value);
                    else
                        op->succeed(value);
                    checkDrained();
                });
        connect(m_service, &PluginFileService::scopeClosed, this, [this](const QString &scope) {
            m_closingScopes.remove(scope);
            checkDrained();
        });
        connect(m_service, &PluginFileService::leaseClosed, this, [this](const QString &id) {
            for (auto lease : m_media)
                if (lease && lease->id() == id)
                    lease->invalidate();
        });
        connect(m_service, &PluginFileService::downloadRemoved, this,
                [this](const QString &installation, const QString &id) {
                    if (installation != m_owner.installation)
                        return;
                    if (auto handle = m_downloads.take(id))
                        handle->invalidate();
                    m_boundDownloads.remove(id);
                });
        connect(m_service, &PluginFileService::downloadChanged, this,
                [this](const QString &installation, const QVariantMap &value) {
                    if (installation != m_owner.installation || m_closing)
                        return;
                    if (auto handle = m_downloads.value(value.value("id").toString()))
                        handle->accept(value);
                });
    }
    connect(&scope.host().api, &ApiClient::sessionChanged, this, [this] {
        if (!m_closing) {
            retireResources();
            resetScope();
        }
    });
    connect(&scope.host().playback, &PlaybackService::changed, this, [this] {
        const auto revision = m_scope.player()->revision();
        for (auto lease : m_media)
            if (lease && !lease->m_revision.isEmpty() && lease->m_revision != revision)
                lease->dispose();
        const auto bound = m_boundDownloads;
        for (auto it = bound.begin(); it != bound.end(); ++it)
            if (it.value() != revision) {
                m_boundDownloads.remove(it.key());
                if (m_service)
                    m_service->request(m_owner, "downloadCancel", {{"id", it.key()}});
            }
    });
}
void EchoPluginFiles::resetScope()
{
    m_owner = {m_scope.installationId(), m_scope.id(), m_scope.package().name,
               QUuid::createUuid().toString(QUuid::WithoutBraces),
               std::make_shared<std::atomic_bool>(false)};
}
bool EchoPluginFiles::validOptions(const QVariantMap &options, const QStringList &allowed) const
{
    for (auto it = options.begin(); it != options.end(); ++it)
        if (it.key() != "revision" && !allowed.contains(it.key()))
            return false;
    return !options.contains("revision") ||
           options.value("revision").metaType().id() == QMetaType::QString;
}
EchoPluginOperation *EchoPluginFiles::invoke(const QString &method, QVariantMap input,
                                             const QVariantMap &options, Adapt adapt,
                                             EchoPluginOperation *existing)
{
    auto *op = existing ? existing : m_scope.operation();
    if (op->finished())
        return op;
    if (m_closing || !m_scope.active()) {
        op->fail("ScopeClosed", "File scope has ended");
        return op;
    }
    if (!m_service) {
        op->fail("Unavailable", "Host file service is unavailable");
        return op;
    }
    const bool download = method.startsWith("download");
    if (!m_scope.package().capabilities.contains(download ? "downloads" : "localFiles")) {
        op->fail("CapabilityRequired",
                 download ? "Declare downloads capability" : "Declare localFiles capability");
        return op;
    }
    const auto revision = options.value("revision").toString();
    if (options.contains("revision") && revision != m_scope.player()->revision()) {
        op->fail("StaleTrack", "Playback track changed");
        return op;
    }
    const auto id = m_service->request(m_owner, method, input);
    if (!id) {
        op->fail("Busy", "Host file service is closing or its request queue is full");
        return op;
    }
    m_requests.insert(id, {op, std::move(adapt), m_owner, method});
    op->start();
    connect(op, &EchoPluginOperation::completed, this, [this, id, op] {
        if (op->state() == EchoPluginOperation::Canceled && m_service)
            m_service->cancel(id);
    });
    if (options.contains("revision"))
        connect(&m_scope.host().playback, &PlaybackService::changed, op, [this, op, revision] {
            if (!op->finished() && revision != m_scope.player()->revision())
                op->cancel();
        });
    return op;
}
EchoPluginOperation *EchoPluginFiles::select(bool directory, const QVariantMap &options,
                                             const QString &replace)
{
    if (!validOptions(options,
                      {"purpose", "access", "persist", "multiple", "filters", "defaultPath"}))
        return m_scope.rejected("InvalidArgument", "Unknown file selection option");
    const auto error = EchoPluginFilePicker::validate(directory, options);
    if (!error.isEmpty())
        return m_scope.rejected("InvalidArgument", error);
    if (!m_scope.committed())
        return m_scope.rejected("NotReady", "File selection requires a committed activation");
    if (!m_service || !m_scope.package().capabilities.contains("localFiles"))
        return m_scope.rejected("CapabilityRequired", "Declare localFiles capability");
    if (options.contains("revision") && options.value("revision") != m_scope.player()->revision())
        return m_scope.rejected("StaleTrack", "Playback track changed");
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    if (EchoPluginFilePicker::busy())
        return m_scope.rejected("Busy", "Another file selection is active");
    auto *picker = new EchoPluginFilePicker(m_scope.package().name, directory, options, this);
    const QPointer<EchoPluginFilePicker> guard(picker);
    op->start([guard] {
        if (guard)
            guard->cancel();
    });
    connect(picker, &EchoPluginFilePicker::rejected, op,
            [op] { op->succeed(QVariantMap{{"canceled", true}}); });
    connect(picker, &EchoPluginFilePicker::selected, op,
            [this, op, directory, options, replace](const QStringList &paths) {
                if (op->finished())
                    return;
                if (paths.isEmpty()) {
                    op->succeed(QVariantMap{{"canceled", true}});
                    return;
                }
                QVariantMap input{{"paths", paths},
                                  {"kind", directory ? "directory" : "file"},
                                  {"access", options.value("access", "read")},
                                  {"persist", options.value("persist", true)}};
                if (!replace.isEmpty())
                    input["replaceId"] = replace;
                invoke(
                    "grant", input, options,
                    [directory](auto *op, const auto &value) {
                        const auto rows = value.toList();
                        op->succeed(QVariantMap{
                            {"canceled", false},
                            {directory ? "directory" : "files",
                             directory ? QVariant(rows.value(0).toMap()) : QVariant(rows)}});
                    },
                    op);
            });
    if (options.contains("revision"))
        connect(&m_scope.host().playback, &PlaybackService::changed, op,
                [this, op, revision = options.value("revision").toString()] {
                    if (revision != m_scope.player()->revision())
                        op->cancel();
                });
    picker->open();
    return op;
}
EchoPluginOperation *EchoPluginFiles::requestDirectory(const QVariantMap &options)
{
    return select(true, options);
}
EchoPluginOperation *EchoPluginFiles::requestFiles(const QVariantMap &options)
{
    return select(false, options);
}
EchoPluginOperation *EchoPluginFiles::reauthorize(const QString &id, const QVariantMap &options)
{
    const auto grant = m_service ? m_service->grant(id) : QVariantMap();
    if (grant.value("installationId") != m_scope.installationId())
        return m_scope.rejected("AccessDenied", "Grant belongs to another installation");
    auto request = options;
    request["access"] = grant.value("access");
    request["persist"] = grant.value("persistent");
    request["multiple"] = false;
    return select(grant.value("kind") == "directory", request, id);
}
EchoPluginOperation *EchoPluginFiles::listGrants()
{
    return invoke("listGrants");
}
EchoPluginOperation *EchoPluginFiles::revoke(const QString &id)
{
    return invoke("revoke", {{"id", id}});
}
EchoPluginOperation *EchoPluginFiles::privateDirectory(const QString &kind)
{
    return invoke("privateDirectory", {{"kind", kind}});
}
void EchoPluginFiles::retireResources()
{
    const auto requests = m_requests;
    for (const auto &request : requests)
        if (request.operation)
            request.operation->cancel();
    for (auto lease : m_media)
        if (lease)
            lease->dispose();
    m_media.clear();
    for (auto handle : m_downloads)
        if (handle)
            handle->invalidate();
    m_downloads.clear();
    m_boundDownloads.clear();
    if (m_service) {
        m_closingScopes.insert(m_owner.scope);
        m_service->closeScope(m_owner.scope);
    }
    if (m_owner.canceled)
        m_owner.canceled->store(true);
}
void EchoPluginFiles::shutdown()
{
    if (std::exchange(m_closing, true))
        return;
    retireResources();
    checkDrained();
}
bool EchoPluginFiles::isDrained() const
{
    return m_requests.isEmpty() && m_closingScopes.isEmpty();
}
void EchoPluginFiles::checkDrained()
{
    if (m_closing && isDrained())
        QTimer::singleShot(0, this, [this] { emit drained(); });
}
EchoPluginMediaLease::EchoPluginMediaLease(EchoPluginFiles &files, const QVariantMap &value)
    : QObject(&files), m_files(files), m_id(value.value("id").toString()),
      m_url(value.value("url").toUrl()), m_owner(files.m_owner)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}
void EchoPluginMediaLease::invalidate()
{
    if (std::exchange(m_active, false)) {
        emit changed();
        QTimer::singleShot(0, this, [this] {
            setParent(nullptr);
            QQmlEngine::setObjectOwnership(this, QQmlEngine::JavaScriptOwnership);
        });
    }
}
void EchoPluginMediaLease::dispose()
{
    if (!m_active)
        return;
    invalidate();
    if (m_files.m_service)
        m_files.m_service->request(m_owner, "releaseMedia", {{"id", m_id}});
}
} // namespace EchoMusic
