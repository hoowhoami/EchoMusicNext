#include "EchoPluginPermissions.h"
#include "EchoPluginContext.h"
#include "SettingsKeys.h"
#include <QCryptographicHash>
#include <QMessageBox>
#include <QTimer>
#include <QUuid>
#include <QSet>
namespace EchoMusic
{
namespace
{
const QString prefix = SettingsKeys::Plugins::ResourcePermissionsPrefix;
QString label(const QString &cap)
{
    if (cap == "appearance")
        return QObject::tr("临时调整宿主外观");
    if (cap == "appIcons")
        return QObject::tr("更改应用与快捷方式图标");
    if (cap == "navigation")
        return QObject::tr("读取和操作宿主页面导航");
    if (cap == "hostSettings")
        return QObject::tr("读取和更改宿主设置");
    if (cap == "cover")
        return QObject::tr("更改宿主封面回退样式");
    if (cap == "tcp")
        return QObject::tr("连接 TCP 设备或服务");
    if (cap == "webServer")
        return QObject::tr("启动本机 HTTP / WebSocket 服务");
    if (cap == "process")
        return QObject::tr("运行插件附带的程序");
    if (cap == "serverIntercept")
        return QObject::tr("拦截并修改宿主 API 请求及响应");
    return QObject::tr("操作浮窗与独立插件窗口");
}
bool supported(const QString &cap)
{
    return QStringList{"tcp",        "webServer",    "windows", "process",    "serverIntercept",
                       "navigation", "hostSettings", "cover",   "appearance", "appIcons"}
        .contains(cap);
}
} // namespace
PluginPermissionStore::PluginPermissionStore(EchoMusic::SettingsStore &s, QObject *parent)
    : QObject(parent), m_settings(s)
{
}
QString PluginPermissionStore::key(const EchoPluginContext &scope, const QString &cap,
                                   const QString &resource) const
{
    return QString::fromLatin1(
        QCryptographicHash::hash((scope.installationId() + ':' + scope.package().contentHash + ':' +
                                  cap + (resource.isEmpty() ? QString() : ':' + resource))
                                     .toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex());
}
QRect PluginPermissionStore::windowBounds(const QString &installation, const QString &id) const
{
    return m_settings.value(SettingsKeys::Plugins::WindowBoundsPrefix + installation + '/' + id)
        .toRect();
}
void PluginPermissionStore::saveWindowBounds(const QString &installation, const QString &id,
                                             const QRect &bounds)
{
    m_settings.setValue(SettingsKeys::Plugins::WindowBoundsPrefix + installation + '/' + id,
                        bounds);
}
bool PluginPermissionStore::granted(const EchoPluginContext &scope, const QString &cap,
                                    const QString &resource) const
{
    return supported(cap) && scope.package().capabilities.contains(cap) &&
           (m_session.contains(key(scope, cap, resource)) ||
            m_settings.contains(prefix + key(scope, cap, resource)));
}
bool PluginPermissionStore::waiting(const QString &installation) const
{
    for (const auto &pending : m_pending)
        if (pending.scope && pending.scope->installationId() == installation)
            return true;
    return false;
}
QVariantList PluginPermissionStore::records() const
{
    auto rows = m_session;
    for (const auto &k : m_settings.allKeys())
        if (k.startsWith(prefix))
            rows[k.mid(prefix.size())] = m_settings.value(k).toMap();
    QVariantList out;
    for (auto it = rows.begin(); it != rows.end(); ++it) {
        auto row = it.value();
        row["id"] = it.key();
        out.append(row);
    }
    return out;
}
void PluginPermissionStore::prunePrompts()
{
    for (const auto &prompt : m_promptKeys.keys()) {
        bool used = false;
        for (const auto &pending : m_pending)
            if (pending.permissionKey == m_promptKeys.value(prompt)) {
                used = true;
                break;
            }
        if (used)
            continue;
        m_promptKeys.remove(prompt);
        if (auto box = m_dialogs.take(prompt)) {
            box->close();
            box->deleteLater();
        }
    }
}
void PluginPermissionStore::authorize(EchoPluginContext &scope, const QString &cap,
                                      const QString &purpose, EchoPluginOperation *op,
                                      std::function<void()> ready, const QString &resource)
{
    if (cap == "process" && resource.isEmpty()) {
        op->fail("InvalidArgument", "Use process.launch to authorize a specific executable");
        return;
    }
    if (!supported(cap) || !scope.package().capabilities.contains(cap)) {
        op->fail("CapabilityDenied", "Declare " + cap + " capability");
        return;
    }
    if (!scope.active()) {
        op->cancel();
        return;
    }
    if (granted(scope, cap, resource)) {
        QPointer<EchoPluginContext> guard(&scope);
        const auto epoch = scope.accountEpoch();
        QTimer::singleShot(0, op, [this, guard, epoch, cap, op, ready, resource] {
            if (op->finished())
                return;
            if (!guard || !guard->active() || guard->accountEpoch() != epoch) {
                op->cancel();
                return;
            }
            if (!granted(*guard, cap, resource)) {
                op->fail("PermissionDenied", "Resource permission was revoked");
                return;
            }
            ready();
        });
        return;
    }
    if (m_pending.size() >= 64) {
        op->fail("Busy", "Too many authorization requests");
        return;
    }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto permissionKey = key(scope, cap, resource);
    const QVariantMap record{{"installationId", scope.installationId()},
                             {"pluginName", scope.package().name},
                             {"capability", cap},
                             {"name", resource.isEmpty()
                                          ? label(cap)
                                          : label(cap) + " · " + resource.section(':', 0, -2)},
                             {"contentHash", scope.package().contentHash},
                             {"resource", resource}};
    m_pending.insert(id,
                     {&scope, op, permissionKey, record, scope.accountEpoch(), std::move(ready)});
    connect(op, &EchoPluginOperation::completed, this, [this, id] {
        if (m_pending.remove(id)) {
            prunePrompts();
            emit pendingChanged();
        }
    });
    emit pendingChanged();
    if (!m_pending.contains(id))
        return;
    // Concurrent calls for the same permission share one question. Canceling a
    // single caller does not dismiss another caller's authorization request.
    if (m_promptKeys.values().contains(permissionKey))
        return;
    m_promptKeys[id] = permissionKey;
    auto request = record;
    request["purpose"] = purpose.left(1024);
    emit confirmationRequested(id, request);
    if (!m_promptKeys.contains(id))
        return;
    auto *box = new QMessageBox(QMessageBox::Question, tr("插件授权"),
                                tr("允许“%1”%2？").arg(scope.package().name, label(cap)),
                                QMessageBox::Yes | QMessageBox::No);
    box->setTextFormat(Qt::PlainText);
    box->setAttribute(Qt::WA_QuitOnClose, false);
    box->setInformativeText(purpose.left(1024));
    box->setDefaultButton(QMessageBox::No);
    m_dialogs[id] = box;
    connect(box, &QMessageBox::finished, this,
            [this, id](int answer) { decide(id, answer == QMessageBox::Yes); });
    // The host uses QQuickWindow, not a QWidget parent. QDialog::open()
    // requests a window-modal sheet, which Cocoa cannot present parentless.
    // Keep authorization nonblocking and modal across the application's windows.
    box->setWindowModality(Qt::ApplicationModal);
    box->show();
}
void PluginPermissionStore::decide(const QString &id, bool allow)
{
    if (!m_promptKeys.contains(id))
        return;
    const auto permissionKey = m_promptKeys.take(id);
    QList<Pending> callers;
    for (const auto &pending : m_pending.keys())
        if (m_pending.value(pending).permissionKey == permissionKey)
            callers.append(m_pending.take(pending));
    if (auto box = m_dialogs.take(id)) {
        box->close();
        box->deleteLater();
    }
    emit pendingChanged();
    QVariantMap record;
    for (const auto &p : callers)
        if (p.operation && !p.operation->finished() && p.scope && p.scope->active() &&
            p.scope->accountEpoch() == p.epoch) {
            record = p.record;
            break;
        }
    if (allow && !record.isEmpty()) {
        for (const auto &value : records()) {
            const auto row = value.toMap();
            if (row.value("installationId") == record.value("installationId") &&
                row.value("capability") == record.value("capability") &&
                row.value("resource") == record.value("resource")) {
                m_session.remove(row.value("id").toString());
                m_settings.remove(prefix + row.value("id").toString());
            }
        }
        m_session[permissionKey] = record;
        m_settings.setValue(prefix + permissionKey, record);
        emit changed();
    }
    for (const auto &p : callers) {
        if (!p.operation || p.operation->finished())
            continue;
        if (!p.scope || !p.scope->active() || p.scope->accountEpoch() != p.epoch) {
            p.operation->cancel();
            continue;
        }
        if (!allow) {
            p.operation->fail("PermissionDenied", "User denied resource permission");
            continue;
        }
        if (!granted(*p.scope, p.record.value("capability").toString(),
                     p.record.value("resource").toString())) {
            p.operation->fail("PermissionDenied", "Resource permission was revoked");
            continue;
        }
        p.ready();
    }
}
bool PluginPermissionStore::revoke(const QString &id)
{
    auto record = m_session.value(id, m_settings.value(prefix + id).toMap());
    if (record.isEmpty())
        return false;
    m_session.remove(id);
    m_settings.remove(prefix + id);
    // Reject pending confirmations for the same owner before closing live resources.
    QSet<QString> prompts;
    for (const auto &pending : m_pending)
        if (pending.record.value("installationId") == record.value("installationId") &&
            pending.record.value("capability") == record.value("capability"))
            for (auto it = m_promptKeys.begin(); it != m_promptKeys.end(); ++it)
                if (it.value() == pending.permissionKey)
                    prompts.insert(it.key());
    for (const auto &prompt : prompts)
        decide(prompt, false);
    emit revoked(record.value("installationId").toString(), record.value("capability").toString());
    emit changed();
    return true;
}
void PluginPermissionStore::forgetInstallation(const QString &installation)
{
    for (const auto &value : records()) {
        const auto row = value.toMap();
        if (row.value("installationId") == installation)
            revoke(row.value("id").toString());
    }
    m_settings.remove(SettingsKeys::Plugins::WindowBoundsPrefix + installation);
}
void PluginPermissionStore::cancelScope(EchoPluginContext *scope)
{
    const auto ids = m_pending.keys();
    for (const auto &id : ids)
        if (m_pending.value(id).scope == scope) {
            auto op = m_pending.value(id).operation;
            if (op)
                op->cancel();
        }
}
EchoPluginPermissions::EchoPluginPermissions(EchoPluginContext &s) : QObject(&s), m_scope(s) {}
bool EchoPluginPermissions::granted(const QString &cap) const
{
    return m_scope.active() && m_scope.host().permissions &&
           m_scope.host().permissions->granted(m_scope, cap);
}
EchoPluginOperation *
EchoPluginPermissions::authorize(const QString &cap, const QString &purpose,
                                 std::function<void(EchoPluginOperation *)> ready)
{
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    op->start();
    if (!m_scope.host().permissions) {
        op->fail("Unavailable", "Resource authorization is unavailable");
        return op;
    }
    m_scope.host().permissions->authorize(m_scope, cap, purpose, op, [op, ready] {
        if (!op->finished())
            ready(op);
    });
    return op;
}
EchoPluginOperation *EchoPluginPermissions::request(const QString &cap, const QString &purpose)
{
    return authorize(cap, purpose, [](auto *op) { op->succeed(true); });
}
} // namespace EchoMusic
