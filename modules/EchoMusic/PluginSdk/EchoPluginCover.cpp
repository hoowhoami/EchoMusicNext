#include "EchoPluginCover.h"
#include "EchoPluginContext.h"
#include "EchoCoverRegistry.h"
#include <QDir>
#include <QUrl>
#include <cmath>
namespace EchoMusic
{
EchoPluginCover::EchoPluginCover(EchoPluginContext &scope) : QObject(&scope), m_scope(scope) {}
QString EchoPluginCover::createThemedIconCoverUrl(const QString &icon, const QString &color) const
{
    if (!m_scope.active())
        return {};
    return EchoCoverRegistry::themedIconUrl(icon,
                                            color.isEmpty() ? m_scope.accent() : QColor(color))
        .toString();
}
QString EchoPluginCover::createThemedIconCoverUrl(const QVariantMap &options) const
{
    if (!m_scope.active() || options.size() > 2)
        return {};
    for (auto it = options.begin(); it != options.end(); ++it)
        if (!QStringList{"icon", "color"}.contains(it.key()))
            return {};
    if (options.contains("color") && options.value("color").metaType().id() != QMetaType::QString)
        return {};
    const auto color = options.value("color").toString();
    const auto icon = options.value("icon", "music");
    if (icon.metaType().id() == QMetaType::QString)
        return createThemedIconCoverUrl(icon.toString(), color);
    if (icon.metaType().id() != QMetaType::QVariantMap)
        return {};
    const auto value = icon.toMap();
    for (auto it = value.begin(); it != value.end(); ++it)
        if (!QStringList{"body", "width", "height"}.contains(it.key()))
            return {};
    if (value.value("body").metaType().id() != QMetaType::QString)
        return {};
    for (const auto &key : {"width", "height"})
        if (value.contains(key)) {
            const auto type = value.value(key).metaType().id();
            if (type != QMetaType::Double && type != QMetaType::Int && type != QMetaType::LongLong)
                return {};
        }
    return EchoCoverRegistry::themedSvgUrl(value.value("body").toString(),
                                           value.value("width", 24).toDouble(),
                                           value.value("height", 24).toDouble(),
                                           color.isEmpty() ? m_scope.accent() : QColor(color))
        .toString();
}
EchoPluginOperation *EchoPluginCover::setFallback(const QString &id, const QJSValue &resolver,
                                                  int priority)
{
    if (!resolver.isCallable() || priority < -10000 || priority > 10000)
        return m_scope.rejected("InvalidArgument", "Invalid cover resolver or priority");
    return m_scope.permissions()->authorize(
        "cover", tr("自定义缺失或加载失败的封面"), [this, id, resolver, priority](auto *op) {
            auto *registry = EchoCoverRegistry::forEngine(m_scope.engine());
            auto *lease = m_scope.registration("coverFallback", id);
            if (!registry || !lease) {
                op->fail("InvalidArgument", "Invalid cover registration");
                return;
            }
            const auto token =
                lease->key() + ':' + m_scope.activationGeneration() + ':' + lease->revision();
            connect(lease, &EchoPluginRegistration::activeChanged, lease, [lease] {
                if (!lease->active()) {
                    lease->setParent(nullptr);
                    QQmlEngine::setObjectOwnership(lease, QQmlEngine::JavaScriptOwnership);
                }
            });
            auto connections = std::make_shared<QList<QMetaObject::Connection>>();
            lease->stage(
                [this, id, lease, registry, resolver, token, priority, connections] {
                    const bool ok = registry->put(
                        token, priority, [this, lease, resolver](const QVariantMap &context) {
                            if (!m_scope.active() || !lease->active())
                                return QUrl{};
                            const auto result =
                                m_scope.call(resolver, {m_scope.engine().toScriptValue(context)});
                            if (!result.isString() || result.toString().size() > 1024 * 1024)
                                return QUrl{};
                            QUrl url(result.toString());
                            if (url.isEmpty())
                                return QUrl{};
                            if (url.isRelative()) {
                                const auto file = m_scope.package().resolve(result.toString());
                                return file.isEmpty() ? QUrl{} : QUrl::fromLocalFile(file);
                            }
                            if (url.isLocalFile()) {
                                const auto relative = QDir(m_scope.package().root)
                                                          .relativeFilePath(url.toLocalFile());
                                const auto file = m_scope.package().resolve(relative);
                                return file.isEmpty() ? QUrl{} : QUrl::fromLocalFile(file);
                            }
                            if (QStringList{"http", "https", "qrc"}.contains(url.scheme()) ||
                                (url.scheme() == "data" &&
                                 result.toString().startsWith("data:image/")))
                                return url;
                            return QUrl{};
                        });
                    if (!ok)
                        return false;
                    m_tokens[id] = token;
                    connections->append(connect(&m_scope, &EchoPluginContext::sessionInvalidated,
                                                lease, &EchoPluginRegistration::dispose));
                    connections->append(
                        connect(m_scope.host().permissions, &PluginPermissionStore::revoked, lease,
                                [this, lease](const auto &installation, const auto &cap) {
                                    if (installation == m_scope.installationId() && cap == "cover")
                                        lease->dispose();
                                }));
                    return true;
                },
                [this, id, token, registry = QPointer(registry), connections] {
                    if (registry)
                        registry->remove(token);
                    if (m_tokens.value(id) == token)
                        m_tokens.remove(id);
                    for (auto c : *connections)
                        QObject::disconnect(c);
                    connections->clear();
                });
            if (m_scope.committed() && !lease->commit()) {
                lease->dispose();
                op->fail("Unavailable", "Cover registry capacity exceeded");
                return;
            }
            op->succeed(QVariant::fromValue(lease));
        });
}
void EchoPluginCover::invalidate()
{
    if (!m_scope.active())
        return;
    if (auto *registry = EchoCoverRegistry::forEngine(m_scope.engine()))
        for (const auto &token : m_tokens)
            registry->invalidate(token);
}
} // namespace EchoMusic
