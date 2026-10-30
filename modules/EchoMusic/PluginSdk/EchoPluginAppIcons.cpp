#include "EchoPluginAppIcons.h"
#include "EchoPluginContext.h"
#include "AppIconService.h"
namespace EchoMusic
{
EchoPluginAppIcons::EchoPluginAppIcons(EchoPluginContext &scope) : QObject(&scope), m_scope(scope)
{
    if (auto *service = scope.host().appIcons) {
        connect(service, &AppIconService::changed, this, &EchoPluginAppIcons::changed);
        connect(service, &AppIconService::drainedChanged, this,
                &EchoPluginAppIcons::drainedChanged);
        connect(service, &AppIconService::drainedChanged, this, &EchoPluginAppIcons::changed);
    }
}
bool EchoPluginAppIcons::drained() const
{
    const auto *service = m_scope.host().appIcons;
    return !m_withdrawing && m_pending == 0 &&
           (!service || service->completedShortcutRevision() >= m_shortcutBarrier);
}
QVariantMap EchoPluginAppIcons::snapshot() const
{
    if (!m_scope.active() || !m_scope.host().appIcons)
        return {};
    auto values = m_scope.host().appIcons->snapshot();
    values["error"] = m_error;
    values["busy"] = !drained();
    return values;
}
EchoPluginOperation *EchoPluginAppIcons::set(const QString &id, const QVariantMap &icons,
                                             int priority)
{
    auto *service = m_scope.host().appIcons;
    if (!service)
        return m_scope.rejected("Unavailable", "Application icon service unavailable");
    if (icons.isEmpty() || icons.size() > 4 || priority < -10000 || priority > 10000)
        return m_scope.rejected("InvalidArgument", "Invalid icon definition or priority");
    QHash<QString, QString> paths;
    QHash<QString, QByteArray> data;
    qsizetype inputBytes = 0;
    for (auto it = icons.begin(); it != icons.end(); ++it) {
        if (!QStringList{"window", "tray", "desktop", "taskbar"}.contains(it.key()) ||
            (it.value().metaType().id() != QMetaType::QString &&
             it.value().metaType().id() != QMetaType::QByteArray))
            return m_scope.rejected("InvalidArgument",
                                    "Unknown icon target or invalid package path");
        if (!service->supported(it.key()))
            return m_scope.rejected("Unsupported",
                                    "Icon target is unsupported on this platform: " + it.key());
        if (it.value().metaType().id() == QMetaType::QByteArray) {
            const auto bytes = it.value().toByteArray();
            inputBytes += bytes.size();
            if (bytes.isEmpty() || inputBytes > 4 * 1024 * 1024)
                return m_scope.rejected("QuotaExceeded", "Inline icon data accepts at most 4 MiB");
            data[it.key()] = bytes;
            continue;
        }
        const auto path = m_scope.package().resolve(it.value().toString());
        if (path.isEmpty())
            return m_scope.rejected("InvalidArgument", "Icon must be a package asset");
        paths[it.key()] = path;
    }
    return m_scope.permissions()->authorize(
        "appIcons", tr("更改应用、托盘或快捷方式图标"),
        [this, service, id, paths, data, priority](auto *op) {
            const auto epoch = m_scope.accountEpoch();
            ++m_pending;
            emit drainedChanged();
            service->prepare(
                paths,
                [self = QPointer(this), op = QPointer(op), epoch, service, id,
                 priority](PreparedAppIcons result) {
                    if (!self)
                        return;
                    --self->m_pending;
                    emit self->drainedChanged();
                    if (!op || op->finished())
                        return;
                    auto &scope = self->m_scope;
                    if (!scope.active() || scope.accountEpoch() != epoch) {
                        op->cancel();
                        return;
                    }
                    if (!scope.permissions()->granted("appIcons")) {
                        op->fail("PermissionDenied", "Icon authorization revoked");
                        return;
                    }
                    if (!result.error.isEmpty()) {
                        op->fail("ImageError", result.error);
                        return;
                    }
                    auto *lease = scope.registration("appIcon", id);
                    if (!lease) {
                        op->fail("InvalidArgument", "Invalid icon registration ID");
                        return;
                    }
                    const auto token =
                        lease->key() + ':' + scope.activationGeneration() + ':' + lease->revision();
                    connect(lease, &EchoPluginRegistration::activeChanged, lease, [lease] {
                        if (!lease->active()) {
                            lease->setParent(nullptr);
                            QQmlEngine::setObjectOwnership(lease, QQmlEngine::JavaScriptOwnership);
                        }
                    });
                    lease->stage(
                        [self, service, token, result, priority,
                         owner = QVariantMap{{"id", id},
                                             {"pluginId", scope.id()},
                                             {"pluginName", scope.package().name}}] {
                            if (self)
                                self->m_withdrawing = true;
                            const auto ok = service->put(token, result, priority, owner);
                            if (self) {
                                self->m_shortcutBarrier = service->shortcutRevision();
                                self->m_withdrawing = false;
                                emit self->drainedChanged();
                            }
                            return ok;
                        },
                        [self, service = QPointer(service), token, id] {
                            if (self)
                                self->m_withdrawing = true;
                            if (service)
                                service->remove(token);
                            if (self) {
                                if (service)
                                    self->m_shortcutBarrier = service->shortcutRevision();
                                self->m_withdrawing = false;
                                emit self->drainedChanged();
                            }
                            if (self && self->m_tokens.value(id) == token)
                                self->m_tokens.remove(id);
                        });
                    self->m_error.clear();
                    self->m_tokens[id] = token;
                    connect(service, &AppIconService::contributionFailed, lease,
                            [self, lease, token](const QString &failed, const QString &message) {
                                if (self && failed == token) {
                                    self->m_error = message;
                                    lease->dispose();
                                    emit self->changed();
                                }
                            });
                    connect(&scope, &EchoPluginContext::sessionInvalidated, lease,
                            &EchoPluginRegistration::dispose);
                    connect(scope.host().permissions, &PluginPermissionStore::revoked, lease,
                            [self, lease](const auto &installation, const auto &capability) {
                                if (self && installation == self->m_scope.installationId() &&
                                    capability == "appIcons")
                                    lease->dispose();
                            });
                    if (scope.committed() && !lease->commit()) {
                        lease->dispose();
                        op->fail("QuotaExceeded", "Icon contribution limit reached");
                        return;
                    }
                    op->succeed(QVariant::fromValue(lease));
                },
                data);
        });
}
EchoPluginOperation *EchoPluginAppIcons::setRuntimeWindowIcon(const QString &path, int priority)
{
    return set("runtime-window", {{"window", path}}, priority);
}
EchoPluginOperation *EchoPluginAppIcons::restore(const QString &target)
{
    auto *service = m_scope.host().appIcons;
    if (!service)
        return m_scope.rejected("Unavailable", "Application icon service unavailable");
    if (!service->supported(target))
        return m_scope.rejected("Unsupported", "Icon target is unsupported on this platform");
    return m_scope.permissions()->authorize(
        "appIcons", tr("恢复应用默认图标"), [this, service, target](auto *op) {
            m_withdrawing = true;
            for (const auto &token : m_tokens.values())
                service->removeTarget(token, target);
            m_shortcutBarrier = service->shortcutRevision();
            m_withdrawing = false;
            emit drainedChanged();
            if (target == "window") {
                op->succeed(QVariantMap{{"applied", true}});
                return;
            }
            if (service->hasTarget(target)) {
                op->fail("Conflict", "Another plugin still contributes this icon");
                return;
            }
            const auto epoch = m_scope.accountEpoch();
            const auto canceled = std::make_shared<std::atomic_bool>(false);
            connect(op, &EchoPluginOperation::completed, op, [op, canceled] {
                if (op->state() == EchoPluginOperation::Canceled)
                    canceled->store(true);
            });
            ++m_pending;
            emit drainedChanged();
            service->restoreShortcut(
                target,
                [self = QPointer(this), scope = QPointer(&m_scope), op = QPointer(op),
                 epoch](const QVariantMap &result) {
                    if (self) {
                        --self->m_pending;
                        emit self->drainedChanged();
                    }
                    if (!op || op->finished())
                        return;
                    if (!scope || !scope->active() || scope->accountEpoch() != epoch ||
                        result.value("canceled").toBool()) {
                        op->cancel();
                        return;
                    }
                    if (!result.value("error").toString().isEmpty())
                        op->fail("ShortcutError", result.value("error").toString());
                    else
                        op->succeed(result);
                },
                canceled);
        });
}
EchoPluginOperation *EchoPluginAppIcons::restoreDefaultWindowIcon()
{
    return restore("window");
}
EchoPluginOperation *EchoPluginAppIcons::restoreDefaultDesktopIcon()
{
    return restore("desktop");
}
EchoPluginOperation *EchoPluginAppIcons::restoreDefaultTaskbarIcon()
{
    return restore("taskbar");
}
EchoPluginOperation *EchoPluginAppIcons::refresh()
{
    return m_scope.permissions()->authorize("appIcons", tr("刷新应用图标"), [this](auto *op) {
        if (!m_scope.host().appIcons) {
            op->fail("Unavailable", "Application icon service unavailable");
            return;
        }
        m_scope.host().appIcons->refresh();
        m_shortcutBarrier = m_scope.host().appIcons->shortcutRevision();
        emit drainedChanged();
        op->succeed(snapshot());
    });
}
} // namespace EchoMusic
