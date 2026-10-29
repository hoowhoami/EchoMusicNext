#include "EchoPluginContext.h"
#include "EchoPluginUi.h"
#include "EchoPluginIo.h"
#include "EchoPluginPromise.h"
#include "EchoUiPartRegistry.h"
#include "ThemeStore.h"
#include "PlaybackService.h"
#include "AccountService.h"
#include "ApiClient.h"
#include <QTimer>
#include <QRegularExpression>
#include <QThread>
#include <algorithm>
#include <memory>
namespace EchoMusic
{
EchoPluginContext::EchoPluginContext(PluginPackage package, quint64 generation, QQmlEngine &engine,
                                     PluginHost host, QString dataPath, QObject *parent,
                                     EchoPluginContext *activation, QString contextId)
    : QObject(parent), m_activation(activation), m_contextId(std::move(contextId)),
      m_package(std::move(package)), m_generation(generation), m_engine(engine), m_host(host)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    m_permissions = new EchoPluginPermissions(*this);
    m_process = new EchoPluginProcess(*this);
    m_server = new EchoPluginServer(*this);
    connect(m_process, &EchoPluginProcess::drained, this, &EchoPluginContext::resourcesDrained);
    m_webServer = new EchoPluginWebServer(*this);
    connect(m_webServer, &EchoPluginWebServer::drained, this, &EchoPluginContext::resourcesDrained);
    m_backups = new EchoPluginBackups(*this);
    m_appearance = new EchoPluginAppearance(*this);
    m_fonts = new EchoPluginFonts(*this);
    m_text = new EchoPluginText(*this);
    connect(m_text, &EchoPluginText::drained, this, &EchoPluginContext::resourcesDrained);
    m_windows = new EchoPluginWindows(*this);
    m_audio = new EchoPluginAudio(*this);
    m_player = new EchoPluginPlayer(*this, host.playback);
    m_playlist = new EchoPluginPlaylist(*this);
    m_lyrics = new EchoPluginLyrics(*this);
    m_files = new EchoPluginFiles(*this);
    m_downloads = new EchoPluginDownloads(*m_files);
    connect(m_files, &EchoPluginFiles::drained, this, &EchoPluginContext::resourcesDrained);
    m_tasks = new EchoPluginTasks(*this);
    m_shortcuts = new EchoPluginShortcuts(*this);
    m_toast = new EchoPluginToast(*this);
    m_kugou = new EchoPluginKugou(*this);
    m_lyricEffects = new EchoPluginLyricEffects(*this);
    m_scroll = new EchoPluginScroll(*this);
    m_scroll->setParent(this);
    m_cover = new EchoPluginCover(*this);
    m_settings = new EchoPluginSettings(*this);
    m_ui = new EchoPluginUi(*this);
    m_plugins = new EchoPluginRegistry(*this);
    m_dataPath = dataPath;
    m_net = new EchoPluginNetwork(*this);
    connect(m_net, &EchoPluginNetwork::drained, this, &EchoPluginContext::resourcesDrained);
    m_sqlite = new EchoPluginSqlite(*this, dataPath + "/databases");
    connect(m_sqlite, &EchoPluginSqlite::drained, this, &EchoPluginContext::resourcesDrained);
    m_assets = new EchoPluginAssets(*this, dataPath, activation ? activation->assets() : nullptr);
    connect(m_assets, &EchoPluginAssets::drained, this, &EchoPluginContext::resourcesDrained);
    m_io = new EchoPluginIo(*this, std::move(dataPath));
    connect(m_io, &EchoPluginIo::drained, this, &EchoPluginContext::resourcesDrained);
    connect(&host.theme, &ThemeStore::changed, this, [this] {
        if (m_accepting)
            emit appearanceChanged();
    });
    connect(&host.theme, &ThemeStore::fontChanged, this, [this] {
        if (m_accepting)
            emit appearanceChanged();
    });
    const auto invalidate = [this] {
        ++m_accountEpoch;
        if (m_activation)
            beginRetire();
        m_host.theme.extensions()->invalidatePresentations();
        if (auto *registry = EchoUiPartRegistry::forEngine(m_engine))
            registry->requestSessionReset();
        const auto operations = m_operations;
        for (const auto &op : operations)
            if (op)
                op->cancel();
        emit sessionInvalidated();
    };
    connect(&host.account, &AccountService::sessionAdopted, this, invalidate);
    // Only identity/session changes invalidate delivery, not profile loading.
    connect(&host.api, &ApiClient::sessionChanged, this, invalidate);
}
EchoPluginContext::~EchoPluginContext()
{
    Q_ASSERT(drained());
}
QFont EchoPluginContext::font() const
{
    return m_host.theme.applicationFont();
}
QColor EchoPluginContext::accent() const
{
    return m_host.theme.accent();
}
bool EchoPluginContext::dark() const
{
    return m_host.theme.dark();
}
bool EchoPluginContext::supports(const QString &feature) const
{
    return PluginPackage::supportedFeatures().contains(feature);
}
EchoPluginOperation *EchoPluginContext::operation()
{
    auto *op = new EchoPluginOperation(this);
    QQmlEngine::setObjectOwnership(op, QQmlEngine::CppOwnership);
    m_engine.newQObject(op);
    connect(op, &EchoPluginOperation::completed, this, [op] {
        // Completed values have no scope resources. A JS caller retaining the
        // handle keeps its result alive; discarded results can be collected.
        QTimer::singleShot(0, op, [op] {
            op->setParent(nullptr);
            QQmlEngine::setObjectOwnership(op, QQmlEngine::JavaScriptOwnership);
        });
    });
    m_operations.erase(std::remove_if(m_operations.begin(), m_operations.end(),
                                      [](const auto &value) { return !value; }),
                       m_operations.end());
    m_operations.append(op);
    if (!m_accepting)
        op->fail("ScopeClosed", "Plugin activation has ended");
    return op;
}
EchoPluginOperation *EchoPluginContext::rejected(const QString &code, const QString &message)
{
    auto *op = operation();
    op->fail(code, message);
    return op;
}
EchoPluginOperation *EchoPluginContext::execute(std::function<void()> command, bool trackScoped)
{
    auto *op = operation();
    if (op->finished())
        return op;
    const auto epoch = m_accountEpoch;
    const auto track = m_host.playback.sourceRevision();
    op->start();
    QTimer::singleShot(0, op, [this, op, epoch, track, trackScoped, command = std::move(command)] {
        if (op->finished())
            return;
        if (!m_accepting || epoch != m_accountEpoch ||
            (trackScoped && track != m_host.playback.sourceRevision())) {
            op->cancel();
            return;
        }
        command();
        op->succeed();
    });
    return op;
}
EchoPluginRegistration *EchoPluginContext::registration(const QString &kind, const QString &id)
{
    if (!m_accepting || !QRegularExpression("^[a-zA-Z0-9_.-]{1,128}$").match(id).hasMatch())
        return nullptr;
    // Transient subscriptions delete their disposed leases. Prune weak entries
    // before another registration so repeated visibility cycles stay bounded.
    m_resources.removeIf([](const auto &lease) { return lease.isNull(); });
    for (auto it = m_registrations.begin(); it != m_registrations.end();) {
        if (it.value().isNull())
            it = m_registrations.erase(it);
        else
            ++it;
    }
    const auto key = registrationKey(kind, id);
    if (auto old = m_registrations.value(key))
        old->dispose();
    auto *lease = new EchoPluginRegistration(key, ++m_revision, this);
    QQmlEngine::setObjectOwnership(lease, QQmlEngine::CppOwnership);
    m_registrations.insert(key, lease);
    m_resources.append(lease);
    return lease;
}
QJSValue EchoPluginContext::call(const QJSValue &callback, const QJSValueList &args)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto arguments = m_engine.newArray(uint(args.size()));
    for (int i = 0; i < args.size(); ++i)
        arguments.setProperty(uint(i), args[i]);
    // QJSValue::call can return a thrown primitive without isError() becoming
    // true. An explicit JS try/catch envelope distinguishes it from a return.
    const auto invoke = m_engine.evaluate(
        "(function(fn,args) { try { return {ok:true,value:fn.apply(undefined,args)}; } "
        "catch(error) { return {ok:false,message:String(error),stack:error && error.stack ? "
        "String(error.stack) : ''}; } })");
    const auto envelope = invoke.call({callback, arguments});
    QJSValue result;
    if (envelope.isError())
        result = envelope;
    else if (!envelope.property("ok").toBool()) {
        result = m_engine.newErrorObject(QJSValue::GenericError,
                                         envelope.property("message").toString());
        result.setProperty("stack", envelope.property("stack"));
    } else
        result = envelope.property("value");
    if (m_engine.hasError()) {
        result = m_engine.catchError();
        if (!result.isError())
            result = m_engine.newErrorObject(QJSValue::GenericError, result.toString());
    }
    if (result.isError())
        emit callbackFailed(result.toString() + '\n' + result.property("stack").toString());
    return result;
}
EchoPluginRegistration *EchoPluginContext::onDispose(const QJSValue &callback)
{
    if (!callback.isCallable())
        return nullptr;
    auto *lease = registration("cleanup", QString::number(++m_revision));
    if (!lease)
        return nullptr;
    lease->stage({}, [this, callback] { call(callback); });
    lease->commit(); // Cleanup must also run when activation never commits.
    return lease;
}
EchoPluginRegistration *EchoPluginContext::setTimer(const QString &id, int intervalMs, bool repeat,
                                                    const QJSValue &callback)
{
    if (!callback.isCallable() || intervalMs < 1 || intervalMs > 86400000)
        return nullptr;
    auto *lease = registration("timer", id);
    if (!lease)
        return nullptr;
    auto *timer = new QTimer(lease);
    timer->setInterval(intervalMs);
    timer->setSingleShot(!repeat);
    connect(timer, &QTimer::timeout, lease, [this, callback, lease] {
        if (m_accepting && m_committed && lease->active())
            call(callback);
    });
    lease->stage(
        [timer] {
            timer->start();
            return true;
        },
        [timer] { timer->stop(); });
    if (m_committed)
        lease->commit();
    return lease;
}
EchoPluginRegistration *EchoPluginContext::onPlayerChanged(const QString &id,
                                                           const QJSValue &callback)
{
    if (!callback.isCallable())
        return nullptr;
    auto *lease = registration("playerEvent", id);
    if (!lease)
        return nullptr;
    auto connection = std::make_shared<QMetaObject::Connection>();
    lease->stage(
        [this, lease, callback, connection] {
            *connection =
                connect(m_player, &EchoPluginPlayer::changed, lease, [this, callback, lease] {
                    if (m_accepting && lease->active())
                        call(callback);
                });
            return true;
        },
        [connection] { disconnect(*connection); });
    if (m_committed)
        lease->commit();
    return lease;
}
EchoPluginRegistration *EchoPluginContext::registerCommand(const QString &id,
                                                           const QJSValue &callback)
{
    if (!callback.isCallable())
        return nullptr;
    auto *lease = registration("command", id);
    if (!lease)
        return nullptr;
    lease->stage(
        [this, id, callback] {
            m_commands.insert(id, callback);
            return true;
        },
        [this, id] { m_commands.remove(id); });
    if (m_committed)
        lease->commit();
    return lease;
}
EchoPluginOperation *EchoPluginContext::executeCommand(const QString &id,
                                                       const QVariantMap &arguments)
{
    if (!m_commands.contains(id))
        return rejected("Unavailable", "Command is not registered");
    return invokeRegistered(m_commands.value(id),
                            m_registrations.value(registrationKey("command", id)), arguments);
}
EchoPluginOperation *EchoPluginContext::invokeRegistered(const QJSValue &callback,
                                                         QPointer<EchoPluginRegistration> lease,
                                                         const QVariantMap &arguments,
                                                         bool reportRejection)
{
    auto *op = operation();
    if (op->finished())
        return op;
    op->start();
    const auto epoch = m_accountEpoch;
    QTimer::singleShot(0, op, [this, op, callback, lease, arguments, epoch, reportRejection] {
        if (op->finished())
            return;
        if (!m_accepting || epoch != m_accountEpoch || !lease || !lease->active()) {
            op->cancel();
            return;
        }
        const auto result = call(callback, {m_engine.toScriptValue(arguments)});
        if (op->finished() || !m_accepting || !lease || !lease->active()) {
            op->cancel();
            return;
        }
        if (result.isError())
            op->fail("PluginError", result.toString());
        else {
            QPointer<EchoPluginContext> guard(this);
            auto *bridge = new EchoPluginPromise(
                *op, parent(), [guard, reportRejection](const QString &message) {
                    if (reportRejection && guard && guard->active())
                        emit guard->callbackFailed(message);
                });
            QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
            const auto attach = m_engine.evaluate(
                "(function(value,bridge) { Promise.resolve(value).then(function(result) { "
                "try { bridge.resolve(result); } catch(ignored) {} }, function(error) { try { "
                "bridge.reject(String(error)); } catch(ignored) {} }); "
                "})");
            const auto attached = call(attach, {result, m_engine.newQObject(bridge)});
            if (attached.isError())
                op->fail("PluginError", attached.toString());
        }
    });
    return op;
}
bool EchoPluginContext::commit()
{
    if (!m_accepting)
        return false;
    // Composite APIs may stage child resources while publishing. Drain those
    // children in the same activation transaction; never leave unpublished leases.
    QSet<QString> processed;
    for (;;) {
        bool advanced = false;
        const auto resources = m_resources;
        for (const auto &lease : resources) {
            if (!lease || processed.contains(lease->revision()))
                continue;
            if (!m_accepting || processed.size() >= 4096)
                return false;
            processed.insert(lease->revision());
            advanced = true;
            if (!lease->commit())
                return false;
        }
        if (!advanced)
            break;
    }
    m_committed = true;
    return true;
}
void EchoPluginContext::freezeStorage(bool frozen)
{
    auto *owner = activationContext();
    owner->m_storageFrozen = frozen;
    if (!frozen) {
        owner->m_assets->resumeStorage();
        for (auto child : owner->m_presentations)
            if (child)
                child->m_assets->resumeStorage();
    }
}
void EchoPluginContext::storageBarrier(std::function<void()> ready)
{
    QList<QPointer<EchoPluginContext>> scopes{this};
    for (auto child : m_presentations)
        if (child)
            scopes.append(child);
    auto pending = std::make_shared<int>(scopes.size() * 4);
    const auto arrive = [pending, ready] {
        if (--*pending == 0)
            ready();
    };
    for (auto scope : scopes) {
        scope->m_appearance->storageBarrier(arrive);
        scope->m_io->storageBarrier(arrive);
        scope->m_sqlite->storageBarrier(arrive);
        scope->m_assets->storageBarrier(arrive);
    }
}
void EchoPluginContext::beginRetire()
{
    if (m_retiring)
        return;
    m_retiring = true;
    if (m_host.permissions)
        m_host.permissions->cancelScope(this);
    m_accepting = false;
    m_settings->blockSignals(true);
    for (auto child : m_presentations)
        if (child)
            child->beginRetire();
    ++m_accountEpoch;
    emit activeChanged();
    // Executable/UI contributions disappear before arbitrary cleanup callbacks.
    for (auto it = m_resources.crbegin(); it != m_resources.crend(); ++it)
        if (*it && !((*it)->key().contains(":cleanup:") ||
                     (*it)->key().contains(":cleanup.presentation.")))
            (*it)->dispose();
    const auto operations = m_operations;
    for (const auto &op : operations)
        if (op)
            op->cancel();
    m_process->shutdown();
    m_webServer->shutdown();
    m_files->shutdown();
    m_net->shutdown();
    m_io->shutdown();
    m_sqlite->shutdown();
    m_text->shutdown();
    m_assets->shutdown();
    for (auto it = m_resources.crbegin(); it != m_resources.crend(); ++it)
        if (*it &&
            ((*it)->key().contains(":cleanup:") || (*it)->key().contains(":cleanup.presentation.")))
            (*it)->dispose();
    m_commands.clear();
}
bool EchoPluginContext::drained() const
{
    if (m_activation && m_presentationOwner)
        return false;
    for (auto child : m_presentations)
        if (child && !child->drained())
            return false;
    return m_process->isDrained() && m_webServer->isDrained() && m_net->isDrained() &&
           m_io->isDrained() && m_sqlite->isDrained() && m_appearance->drained() &&
           m_files->isDrained() && m_text->isDrained() && m_assets->isDrained();
}
} // namespace EchoMusic
