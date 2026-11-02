#include "EchoPluginPermissions.h"
#include "EchoPluginContext.h"
#include "SettingsKeys.h"
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QUuid>
#include <QSet>
#include <algorithm>
namespace EchoMusic
{
namespace
{
const QString prefix = SettingsKeys::Plugins::ResourcePermissionsPrefix;
class PermissionPrompt final : public QDialog
{
public:
    explicit PermissionPrompt(const QVariantMap &request)
    {
        setObjectName("pluginPermissionPrompt");
        setWindowTitle(tr("插件授权"));
        setAttribute(Qt::WA_QuitOnClose, false);
        setWindowModality(Qt::ApplicationModal);
        auto *layout = new QVBoxLayout(this);
        auto *heading = new QLabel(
            tr("允许“%1”使用以下权限？").arg(request.value("pluginName").toString()), this);
        heading->setTextFormat(Qt::PlainText);
        heading->setWordWrap(true);
        layout->addWidget(heading);
        auto *body = new QPlainTextEdit(this);
        body->setObjectName("pluginPermissionList");
        body->setAccessibleName(tr("请求的权限及用途"));
        body->setReadOnly(true);
        QStringList lines;
        for (const auto &value : request.value("permissions").toList()) {
            const auto row = value.toMap();
            lines.append(row.value("name").toString());
            for (const auto &purpose : row.value("purposes").toStringList())
                if (!purpose.isEmpty())
                    lines.append(tr("用途：%1").arg(purpose));
            lines.append(QString());
        }
        body->setPlainText(lines.join('\n'));
        layout->addWidget(body);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, this);
        buttons->button(QDialogButtonBox::Yes)->setText(tr("允许"));
        buttons->button(QDialogButtonBox::Yes)->setAutoDefault(false);
        buttons->button(QDialogButtonBox::No)->setText(tr("不允许"));
        buttons->button(QDialogButtonBox::No)->setDefault(true);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
        const auto available = QGuiApplication::primaryScreen()
                                   ? QGuiApplication::primaryScreen()->availableGeometry().size()
                                   : QSize(800, 600);
        const int naturalHeight = qBound(
            220, (qMin(body->blockCount(), 16) + 1) * body->fontMetrics().lineSpacing() + 120, 440);
        resize(qMin(540, qMax(240, available.width() - 80)),
               qMin(naturalHeight, qMax(180, available.height() - 100)));
    }
};
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
    if (cap == "kugouAuth")
        return QObject::tr("读取当前酷狗账号凭证及设备身份");
    if (cap == "serverIntercept")
        return QObject::tr("拦截并修改宿主 API 请求及响应");
    return QObject::tr("操作浮窗与独立插件窗口");
}
bool supported(const QString &cap)
{
    return QStringList{"tcp",        "webServer",    "windows", "process",    "serverIntercept",
                       "navigation", "hostSettings", "cover",   "appearance", "appIcons",
                       "kugouAuth"}
        .contains(cap);
}
} // namespace
PluginPermissionStore::PluginPermissionStore(EchoMusic::SettingsStore &s, QObject *parent)
    : QObject(parent), m_settings(s)
{
}
PluginPermissionStore::~PluginPermissionStore()
{
    const auto dialog = m_dialog;
    closePrompt();
    delete dialog.data();
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
void PluginPermissionStore::closePrompt()
{
    if (auto *dialog = m_dialog.data()) {
        m_dialog.clear();
        dialog->disconnect(this);
        dialog->close();
        dialog->deleteLater();
    }
    m_activePrompt.clear();
    m_presented.clear();
}
void PluginPermissionStore::schedulePrompt()
{
    if (m_scheduled)
        return;
    m_scheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_scheduled = false;
        presentPrompt();
    });
}
QVariantMap PluginPermissionStore::promptRequest(const QString &id) const
{
    QList<Pending> callers;
    for (const auto &pending : m_pending)
        if (pending.prompt == id && pending.operation && !pending.operation->finished() &&
            pending.scope && pending.scope->active() &&
            pending.scope->accountEpoch() == pending.epoch)
            callers.append(pending);
    std::sort(callers.begin(), callers.end(),
              [](const auto &a, const auto &b) { return a.order < b.order; });
    QVariantMap request;
    QVariantList permissions;
    QHash<QString, int> indices;
    for (const auto &pending : callers) {
        if (request.isEmpty()) {
            request = pending.record;
            request["purpose"] = pending.purpose;
        }
        const auto index = indices.value(pending.permissionKey, -1);
        auto row = index < 0 ? pending.record : permissions[index].toMap();
        auto purposes = row.value("purposes").toStringList();
        if (!pending.purpose.isEmpty() && !purposes.contains(pending.purpose))
            purposes.append(pending.purpose);
        row["purposes"] = purposes;
        if (index < 0) {
            indices.insert(pending.permissionKey, permissions.size());
            permissions.append(row);
        } else
            permissions[index] = row;
    }
    if (!request.isEmpty())
        request["permissions"] = permissions;
    return request;
}
void PluginPermissionStore::presentPrompt()
{
    if (!m_activePrompt.isEmpty())
        return;
    while (!m_promptOrder.isEmpty()) {
        const auto id = m_promptOrder.first();
        const auto request = promptRequest(id);
        if (request.isEmpty()) {
            m_promptOrder.removeFirst();
            m_promptOwners.remove(id);
            continue;
        }
        m_activePrompt = id;
        for (const auto &pending : m_pending)
            if (pending.prompt == id)
                m_presented.insert(pending.permissionKey);
        emit confirmationRequested(id, request);
        // Confirmation observers may resolve or retire the question synchronously.
        if (m_activePrompt != id || !m_promptOwners.contains(id))
            return;
        auto *dialog = new PermissionPrompt(request);
        m_dialog = dialog;
        connect(dialog, &QDialog::finished, this,
                [this, id](int answer) { decide(id, answer == QDialog::Accepted); });
        dialog->show();
        return;
    }
}
void PluginPermissionStore::prunePrompts()
{
    for (const auto &id : m_promptOrder) {
        bool used = false;
        for (const auto &pending : m_pending)
            if (pending.prompt == id) {
                used = true;
                break;
            }
        if (!used)
            m_promptOwners.remove(id);
    }
    m_promptOrder.removeIf([this](const QString &id) { return !m_promptOwners.contains(id); });
    if (!m_activePrompt.isEmpty() && !m_promptOwners.contains(m_activePrompt))
        closePrompt();
    schedulePrompt();
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
    const auto owner = key(scope, "authorization-batch", QString::number(scope.accountEpoch()));
    QString prompt;
    // Freeze an on-screen permission set. Later requests for a new permission
    // wait behind it; they must never be silently approved by an old click.
    if (m_promptOwners.value(m_activePrompt) == owner && m_presented.contains(permissionKey))
        prompt = m_activePrompt;
    if (prompt.isEmpty())
        for (const auto &queued : m_promptOrder)
            if (queued != m_activePrompt && m_promptOwners.value(queued) == owner) {
                prompt = queued;
                break;
            }
    if (prompt.isEmpty()) {
        prompt = id;
        m_promptOwners.insert(prompt, owner);
        m_promptOrder.append(prompt);
    }
    m_pending.insert(id, {&scope, op, permissionKey, prompt, purpose.left(1024), record,
                          scope.accountEpoch(), ++m_nextRequest, std::move(ready)});
    const auto retire = [this, id] {
        if (m_pending.remove(id)) {
            prunePrompts();
            emit pendingChanged();
        }
    };
    connect(op, &EchoPluginOperation::completed, this, retire);
    connect(op, &QObject::destroyed, this, retire);
    emit pendingChanged();
    schedulePrompt();
}

void PluginPermissionStore::decide(const QString &id, bool allow)
{
    if (id != m_activePrompt || !m_promptOwners.contains(id))
        return;
    QList<Pending> callers;
    for (const auto &pending : m_pending.keys())
        if (m_pending.value(pending).prompt == id)
            callers.append(m_pending.take(pending));
    m_promptOwners.remove(id);
    m_promptOrder.removeAll(id);
    closePrompt();
    emit pendingChanged();
    QHash<QString, QVariantMap> grants;
    if (allow)
        for (const auto &p : callers)
            if (p.operation && !p.operation->finished() && p.scope && p.scope->active() &&
                p.scope->accountEpoch() == p.epoch)
                grants.insert(p.permissionKey, p.record);
    for (auto it = grants.cbegin(); it != grants.cend(); ++it) {
        const auto record = it.value();
        for (const auto &value : records()) {
            const auto row = value.toMap();
            if (row.value("installationId") == record.value("installationId") &&
                row.value("capability") == record.value("capability") &&
                row.value("resource") == record.value("resource")) {
                m_session.remove(row.value("id").toString());
                m_settings.remove(prefix + row.value("id").toString());
            }
        }
        m_session[it.key()] = record;
        m_settings.setValue(prefix + it.key(), record);
    }
    if (!grants.isEmpty())
        emit changed();
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
    schedulePrompt();
}
bool PluginPermissionStore::revoke(const QString &id)
{
    auto record = m_session.value(id, m_settings.value(prefix + id).toMap());
    if (record.isEmpty())
        return false;
    m_session.remove(id);
    m_settings.remove(prefix + id);
    // Revoking one capability must not reject unrelated items in a batch.
    const auto pendingIds = m_pending.keys();
    for (const auto &pendingId : pendingIds) {
        const auto pending = m_pending.value(pendingId);
        if (pending.record.value("installationId") == record.value("installationId") &&
            pending.record.value("capability") == record.value("capability") && pending.operation)
            pending.operation->fail("PermissionDenied", "Resource permission was revoked");
    }
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
