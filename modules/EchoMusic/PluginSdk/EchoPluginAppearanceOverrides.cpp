#include "EchoPluginAppearance.h"
#include "EchoPluginContext.h"
#include "ThemeStore.h"
namespace EchoMusic
{
EchoPluginOperation *EchoPluginAppearance::setOverride(const QString &id,
                                                       const QVariantMap &definition, int priority)
{
    QString message;
    if (definition.isEmpty() || priority < -10000 || priority > 10000)
        return m_scope.rejected("InvalidArgument", "Invalid appearance override or priority");
    auto values = definition;
    for (auto variant = values.begin(); variant != values.end(); ++variant) {
        if ((variant.key() != "light" && variant.key() != "dark") ||
            variant.value().metaType().id() != QMetaType::QVariantMap ||
            !validateVisuals(variant.value().toMap(), message))
            return m_scope.rejected("InvalidArgument", message.isEmpty()
                                                           ? "Expected light/dark visual recipes"
                                                           : message);
        auto recipe = variant.value().toMap();
        auto background = recipe.value("background").toMap();
        if (background.contains("source")) {
            const auto path = m_scope.package().resolve(background.value("source").toString());
            if (path.isEmpty())
                return m_scope.rejected("InvalidArgument", "Background must be a package asset");
            background["source"] = QUrl::fromLocalFile(path).toString();
            recipe["background"] = background;
            variant.value() = recipe;
        }
    }
    return m_scope.permissions()->authorize(
        "appearance", tr("临时调整应用主题、背景与页面过渡"),
        [this, id, values, priority](auto *op) {
            auto *lease = m_scope.registration("appearanceOverride", id);
            if (!lease) {
                op->fail("InvalidArgument", "Invalid override ID or closed scope");
                return;
            }
            auto *registry = &m_scope.host().theme.overrides();
            const auto token =
                lease->key() + ':' + m_scope.activationGeneration() + ':' + lease->revision();
            const QVariantMap owner{
                {"id", id}, {"pluginId", m_scope.id()}, {"pluginName", m_scope.package().name}};
            connect(lease, &EchoPluginRegistration::activeChanged, lease, [lease] {
                if (!lease->active()) {
                    lease->setParent(nullptr);
                    QQmlEngine::setObjectOwnership(lease, QQmlEngine::JavaScriptOwnership);
                }
            });
            lease->stage([registry, token, values, priority,
                          owner] { return registry->put(token, values, priority, owner); },
                         [registry = QPointer(registry), token] {
                             if (registry)
                                 registry->remove(token);
                         });
            connect(&m_scope, &EchoPluginContext::sessionInvalidated, lease,
                    &EchoPluginRegistration::dispose);
            connect(m_scope.host().permissions, &PluginPermissionStore::revoked, lease,
                    [this, lease](const QString &installation, const QString &capability) {
                        if (installation == m_scope.installationId() && capability == "appearance")
                            lease->dispose();
                    });
            if (m_scope.committed() && !lease->commit()) {
                lease->dispose();
                op->fail("QuotaExceeded", "Appearance contribution limit reached");
                return;
            }
            op->succeed(QVariant::fromValue(lease));
        });
}
} // namespace EchoMusic
