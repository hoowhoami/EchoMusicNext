#include "EchoPluginBackups.h"
#include "EchoPluginContext.h"
#include "EchoPluginPromise.h"
#include "ApiClient.h"
#include <QTimer>
#include <QRegularExpression>
namespace EchoMusic
{
EchoPluginBackupRequest::EchoPluginBackupRequest(EchoPluginContext &scope, const QVariantMap &args)
    : QObject(&scope), m_scope(scope), m_owner(new QObject(this)),
      m_id(args.value("id").toString()), m_fileName(args.value("fileName").toString()),
      m_data(args.value("data").toByteArray()), m_summary(args.value("summary").toMap())
{
    m_context = scope.createPresentation(m_owner, "backup");
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}
EchoPluginContext *EchoPluginBackupRequest::context() const
{
    return m_context;
}
EchoPluginOperation *EchoPluginBackupRequest::track(EchoPluginOperation *operation)
{
    if (!operation)
        return nullptr;
    if (!m_active) {
        operation->cancel();
        return operation;
    }
    // A provider may only link operations it received from its own context tree.
    auto *scope = qobject_cast<EchoPluginContext *>(operation->parent());
    if (scope && scope->activationContext() != m_scope.activationContext())
        return nullptr;
    if (!operation->finished() && !m_linked.contains(operation))
        m_linked.append(operation);
    return operation;
}
void EchoPluginBackupRequest::cancel()
{
    finish(true);
}
void EchoPluginBackupRequest::finish(bool canceled)
{
    if (!m_active)
        return;
    m_active = false;
    m_context.clear();
    m_canceled = canceled;
    m_data.clear();
    m_summary.clear();
    for (auto operation : m_linked)
        if (operation && !operation->finished())
            operation->cancel();
    m_linked.clear();
    delete std::exchange(m_owner, nullptr);
    emit changed();
    QTimer::singleShot(0, this, [this] {
        setParent(nullptr);
        QQmlEngine::setObjectOwnership(this, QQmlEngine::JavaScriptOwnership);
    });
}
EchoPluginBackups::EchoPluginBackups(EchoPluginContext &scope)
    : QObject(&scope), m_scope(scope), m_host(scope.host().backups)
{
    connect(&scope, &EchoPluginContext::activeChanged, this, [this] {
        if (!m_scope.active() && m_host)
            m_host->invalidateOwner(owner());
    });
    connect(&scope.host().api, &ApiClient::sessionChanged, this, [this] {
        if (m_host)
            m_host->invalidateOwner(owner());
    });
}
// activeChanged releases owners before the containing context's members die.
// QObject deletes child facades after that point, so teardown must not read it.
EchoPluginBackups::~EchoPluginBackups() = default;
QString EchoPluginBackups::owner() const
{
    return m_scope.installationId() + ':' + m_scope.activationGeneration() + ':' +
           m_scope.contextId();
}
EchoPluginOperation *EchoPluginBackups::request(const QString &method, const QVariantMap &scope,
                                                const QByteArray &bytes, const QString &token)
{
    if (!m_scope.package().capabilities.contains("backups"))
        return m_scope.rejected("CapabilityDenied", "Declare the backups capability");
    if (!m_host)
        return m_scope.rejected("Unavailable", "Host backup service is unavailable");
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    op->start([cancel] { cancel->store(true); });
    const QPointer<EchoPluginOperation> guard(op);
    const auto done = [guard, cancel](BackupResult result) {
        if (!guard || guard->finished() || cancel->load())
            return;
        if (result.error.isEmpty())
            guard->succeed(result.value);
        else if (result.code == "Canceled")
            guard->cancel();
        else
            guard->fail(result.code, result.error);
    };
    if (method == "create")
        m_host->createArchive(owner(), scope, cancel, done);
    else if (method == "inspect")
        m_host->inspectArchive(owner(), bytes, cancel, done);
    else
        m_host->requestRestore(owner(), token, scope, cancel, done);
    return op;
}
EchoPluginOperation *EchoPluginBackups::create(const QVariantMap &scope)
{
    return request("create", scope, {}, {});
}
EchoPluginOperation *EchoPluginBackups::inspect(const QByteArray &data)
{
    return request("inspect", {}, data, {});
}
EchoPluginOperation *EchoPluginBackups::restore(const QString &token, const QVariantMap &scope)
{
    return request("restore", scope, {}, token);
}
EchoPluginRegistration *EchoPluginBackups::registerProvider(const QString &id,
                                                            const QJSValue &definition)
{
    if (!m_host || !m_scope.active() || m_scope.contextId() != "main" ||
        !m_scope.package().capabilities.contains("backups"))
        return nullptr;
    if (!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$").match(id).hasMatch() ||
        !definition.isObject())
        return nullptr;
    const auto name = definition.property("name").toString().trimmed();
    if (name.isEmpty() || name.size() > 80 || !definition.property("save").isCallable() ||
        !definition.property("list").isCallable() || !definition.property("load").isCallable())
        return nullptr;
    auto *lease = m_scope.registration("backup", id);
    if (!lease)
        return nullptr;
    const QPointer<EchoPluginRegistration> guard(lease);
    const auto key = m_scope.installationId() + ':' + id,
               registeredOwner = owner() + ":provider:" + id;
    const auto revision = lease->revision().toULongLong();
    const QVariantMap metadata{
        {"name", name},
        {"description", definition.property("description").isString()
                            ? definition.property("description").toString().left(240)
                            : QString()},
        {"pluginName", m_scope.package().name},
        {"removable", definition.property("remove").isCallable()}};
    lease->stage(
        [this, key, registeredOwner, revision, metadata, definition, guard] {
            if (!m_host)
                return false;
            return m_host->addProvider(
                key, revision, registeredOwner, metadata,
                [this, definition, guard](const QString &method, const QVariantMap &args,
                                          BackupCancel canceled, BackupCompletion done) {
                    if (!guard || !guard->active() || !m_scope.active() ||
                        !definition.property(method).isCallable()) {
                        done({{}, "Unavailable", "Backup provider is no longer registered"});
                        return;
                    }
                    auto *op = m_scope.operation();
                    if (op->finished()) {
                        done({{}, op->errorCode(), op->errorMessage()});
                        return;
                    }
                    auto *request = new EchoPluginBackupRequest(m_scope, args);
                    const QPointer<EchoPluginBackupRequest> requestGuard(request);
                    m_scope.engine().newQObject(
                        op); // retained by the completion callback until terminal delivery
                    op->start([requestGuard] {
                        if (requestGuard)
                            requestGuard->cancel();
                    });
                    const auto root = m_scope.engine().newQObject(op);
                    auto *poll = new QTimer(op);
                    poll->setInterval(50);
                    connect(poll, &QTimer::timeout, op, [op, canceled] {
                        if (canceled->load())
                            op->cancel();
                    });
                    poll->start();
                    QTimer::singleShot(120000, op, [op] {
                        if (!op->finished())
                            op->fail("Timeout", "Backup provider timed out");
                    });
                    connect(guard, &EchoPluginRegistration::activeChanged, op, [guard, op] {
                        if (!guard || !guard->active())
                            op->cancel();
                    });
                    connect(
                        op, &EchoPluginOperation::completed, this,
                        [op, requestGuard, done, root, poll] {
                            poll->stop();
                            const BackupResult result{op->result(), op->errorCode(),
                                                      op->errorMessage()};
                            if (requestGuard)
                                requestGuard->finish(op->state() != EchoPluginOperation::Succeeded);
                            done(result);
                        },
                        Qt::SingleShotConnection);
                    if (!request->context()) {
                        op->fail("Busy", "Too many active plugin presentations");
                        return;
                    }
                    const auto result = m_scope.call(definition.property(method),
                                                     {m_scope.engine().newQObject(request)});
                    if (result.isError()) {
                        op->fail("PluginError", result.toString());
                        return;
                    }
                    auto *bridge = new EchoPluginPromise(*op, op);
                    const auto attach = m_scope.engine().evaluate(R"JS((function(v,b,method){
                        Promise.resolve(v).then(function(r){
                            try {
                                if(method==='list') {
                                    if(!Array.isArray(r)) {
                                        if(!r || typeof r!=='object' || typeof r.length!=='number' || r.length<0 || Math.floor(r.length)!==r.length) throw new Error('Backup provider must return an array');
                                        r=Array.prototype.slice.call(r,0,500); // Qt typed sequences expose length/indexes.
                                    }
                                    r=r.slice(0,500).map(function(item){
                                        if(!item || typeof item!=='object') return {};
                                        return {id:String(item.id || '').slice(0,513),name:String(item.name || item.id || '').slice(0,200),createdAt:String(item.createdAt || '').slice(0,80),description:String(item.description || '').slice(0,300),size:Number(item.size)};
                                    });
                                }
                                if(method==='load') {
                                    if(ArrayBuffer.isView(r)) r=r.buffer.slice(r.byteOffset,r.byteOffset+r.byteLength);
                                    if(!(r instanceof ArrayBuffer) || r.byteLength>268435456) throw new Error('Backup provider must return at most 256 MiB of binary data');
                                }
                                if(method==='save' || method==='remove') r=null;
                                b.resolve(r);
                            } catch(e) { try{b.reject(String(e).slice(0,4096));}catch(ignored){} }
                        },function(e){try{b.reject(String(e).slice(0,4096));}catch(ignored){}});
                    }))JS");
                    const auto attached = m_scope.call(
                        attach, {result, m_scope.engine().newQObject(bridge), QJSValue(method)});
                    if (attached.isError())
                        op->fail("PluginError", attached.toString());
                });
        },
        [this, key, revision] {
            if (m_host)
                m_host->removeProvider(key, revision);
        });
    if (m_scope.committed())
        lease->commit();
    return lease;
}
} // namespace EchoMusic
