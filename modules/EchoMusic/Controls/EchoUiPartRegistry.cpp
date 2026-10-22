#include "EchoUiPartRegistry.h"
#include "EchoUiPartRegistration.h"
#include <QColor>
#include <QQuickWindow>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <QTimer>
namespace EchoMusic
{
EchoUiPartRegistry::EchoUiPartRegistry(QObject *parent) : QObject(parent) {}
EchoUiPartRegistry *EchoUiPartRegistry::forEngine(QQmlEngine &engine)
{
    return engine.singletonInstance<EchoUiPartRegistry *>("EchoMusic.Controls",
                                                          "EchoUiPartRegistry");
}
void EchoUiPartRegistry::requestSessionReset()
{
    for (auto i = m_parts.begin(); i != m_parts.end(); ++i)
        m_sessionResetParts.insert(i.key());
    if (std::exchange(m_sessionResetPending, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_sessionResetPending = false;
        const auto ids = std::exchange(m_sessionResetParts, {});
        for (const auto &id : ids)
            if (auto *part = find(id)) {
                detach(part);
                attach(part);
            }
    });
}
void EchoUiPartRegistry::attach(EchoUiPartRegistration *part)
{
    if (!part->active() || !part->target() || !part->target()->window() ||
        (!part->target()->parentItem() &&
         part->target()->window()->contentItem() != part->target()) ||
        part->role().isEmpty())
        return;
    part->m_identity = QString::number(++m_sequence);
    emit part->identityChanged();
    m_parts.insert(part->identity(), part);
    resolve(part);
    emit added(part);
}
void EchoUiPartRegistry::detach(EchoUiPartRegistration *part)
{
    const auto id = std::exchange(part->m_identity, {});
    if (id.isEmpty())
        return;
    m_parts.remove(id);
    emit part->identityChanged();
    part->setResolved({});
    part->setReplacement({});
    emit removed(id);
}
EchoUiPartRegistration *EchoUiPartRegistry::find(const QString &id) const
{
    return m_parts.value(id);
}
bool EchoUiPartRegistry::validSelector(const QVariantMap &selector)
{
    for (auto it = selector.begin(); it != selector.end(); ++it) {
        if (!QStringList{"id", "role", "key", "surface", "window"}.contains(it.key()) ||
            it.value().metaType().id() != QMetaType::QString || it.value().toString().size() > 256)
            return false;
        if (it.key() != "role" && it.value().toString().contains('*'))
            return false;
        const auto role = it.value().toString();
        if (it.key() == "role" && role.contains('*') && role != "*" &&
            (!role.endsWith(".*") || role.count('*') != 1))
            return false;
    }
    return true;
}
bool EchoUiPartRegistry::matches(const EchoUiPartRegistration &part, const QVariantMap &selector)
{
    for (auto it = selector.begin(); it != selector.end(); ++it) {
        const auto expected = it.value().toString();
        const auto actual = it.key() == "id"        ? part.identity()
                            : it.key() == "role"    ? part.role()
                            : it.key() == "key"     ? part.key()
                            : it.key() == "surface" ? part.surface()
                                                    : part.windowId();
        if (it.key() == "role" && expected == "*")
            continue;
        if (it.key() == "role" && expected.endsWith(".*")) {
            if (!actual.startsWith(expected.chopped(1)))
                return false;
        } else if (actual != expected)
            return false;
    }
    return true;
}
QList<EchoUiPartRegistration *> EchoUiPartRegistry::query(const QVariantMap &selector) const
{
    QList<EchoUiPartRegistration *> result;
    if (!validSelector(selector))
        return result;
    for (auto part : m_parts)
        if (part && matches(*part, selector))
            result.append(part);
    std::sort(result.begin(), result.end(), [](auto *a, auto *b) {
        return a->identity().toULongLong() < b->identity().toULongLong();
    });
    return result;
}
bool EchoUiPartRegistry::normalize(QVariantMap &values, QString &error)
{
    const QStringList colors{"foreground", "secondaryForeground", "background", "borderColor",
                             "accent"};
    const QHash<QString, QPair<double, double>> numbers{
        {"scale", {0, 8}},      {"translateX", {-4096, 4096}}, {"translateY", {-4096, 4096}},
        {"radius", {0, 512}},   {"borderWidth", {0, 32}},      {"opacity", {0, 1}},
        {"fontSize", {1, 256}}, {"fontWeight", {1, 1000}},     {"letterSpacing", {-20, 100}}};
    if (values.size() > 32) {
        error = "Too many visual properties";
        return false;
    }
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (colors.contains(it.key())) {
            QColor color;
            if (it.value().metaType() == QMetaType::fromType<QColor>())
                color = it.value().value<QColor>();
            else if (it.value().metaType().id() == QMetaType::QString)
                color = QColor(it.value().toString());
            if (!color.isValid()) {
                error = "Invalid color: " + it.key();
                return false;
            }
            it.value() = color;
        } else if (numbers.contains(it.key())) {
            const auto type = it.value().metaType().id();
            if (type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::LongLong &&
                type != QMetaType::ULongLong && type != QMetaType::Double &&
                type != QMetaType::Float) {
                error = "Expected numeric visual property: " + it.key();
                return false;
            }
            const auto value = it.value().toDouble();
            const auto bounds = numbers.value(it.key());
            if (!std::isfinite(value) || value < bounds.first || value > bounds.second) {
                error = "Visual property outside supported range: " + it.key();
                return false;
            }
        } else if (it.key() == "font") {
            if (it.value().metaType() != QMetaType::fromType<QFont>()) {
                error = "font must be a Qt font";
                return false;
            }
        } else {
            error = "Unknown visual property: " + it.key();
            return false;
        }
    }
    return true;
}
bool EchoUiPartRegistry::putStyle(const QString &token, const QString &owner, const QString &id,
                                  int priority, const QVariantMap &selector,
                                  const QVariantMap &values)
{
    if (!m_styles.contains(token) && m_styles.size() >= 4096)
        return false;
    const auto previous = m_styles.value(token);
    m_styles.insert(token, {token, owner, id, priority, selector, values});
    const auto parts = m_parts.values();
    for (auto part : parts)
        if (part && (matches(*part, selector) ||
                     (!previous.token.isEmpty() && matches(*part, previous.selector))))
            resolve(part);
    return true;
}
void EchoUiPartRegistry::removeStyle(const QString &token)
{
    const auto removed = m_styles.take(token);
    if (removed.token.isEmpty())
        return;
    const auto parts = m_parts.values();
    for (auto part : parts)
        if (part && matches(*part, removed.selector))
            resolve(part);
}
void EchoUiPartRegistry::resolve(EchoUiPartRegistration *part)
{
    QList<Style> rules;
    for (const auto &rule : m_styles)
        if (matches(*part, rule.selector))
            rules.append(rule);
    std::sort(rules.begin(), rules.end(), [](const auto &a, const auto &b) {
        return std::tie(a.priority, a.owner, a.id) < std::tie(b.priority, b.owner, b.id);
    });
    QVariantMap values;
    for (const auto &rule : rules)
        for (auto it = rule.values.begin(); it != rule.values.end(); ++it)
            if (part->properties().contains(it.key()))
                values.insert(it.key(), it.value());
    part->setResolved(values);
}
void EchoUiPartRegistry::putReplacement(const QString &token, const QString &part,
                                        const QString &owner, const QString &id, int priority,
                                        const QString &position)
{
    const auto old = m_replacements.constFind(token);
    if (old != m_replacements.cend() && old->priority == priority &&
        old->values.value("position") == position)
        return;
    m_replacements.insert(token,
                          {token, owner, id, priority, {{"id", part}}, {{"position", position}}});
    if (auto *p = find(part)) {
        resolveReplacement(p);
        emit p->mountOrderChanged();
    }
}
void EchoUiPartRegistry::removeReplacement(const QString &token)
{
    const auto rule = m_replacements.take(token);
    if (auto *p = find(rule.selector.value("id").toString())) {
        resolveReplacement(p);
        emit p->mountOrderChanged();
    }
}
void EchoUiPartRegistry::setMountReady(const QString &token, bool ready)
{
    auto it = m_replacements.find(token);
    if (it == m_replacements.end() || it->values.value("ready").toBool() == ready)
        return;
    it->values["ready"] = ready;
    if (auto *part = find(it->selector.value("id").toString()))
        resolveReplacement(part);
}
void EchoUiPartRegistry::resolveReplacement(EchoUiPartRegistration *part)
{
    QStringList ordered = mountOrder(part->identity(), "replace");
    QString applied;
    for (const auto &token : ordered)
        if (m_replacements.value(token).values.value("ready").toBool())
            applied = token;
    part->setReplacement(applied, ordered.isEmpty() ? QString() : ordered.last());
}
QStringList EchoUiPartRegistry::mountOrder(const QString &part, const QString &position) const
{
    QList<Style> mounts;
    for (const auto &mount : m_replacements)
        if (mount.selector.value("id") == part && mount.values.value("position") == position)
            mounts.append(mount);
    std::sort(mounts.begin(), mounts.end(), [](const auto &a, const auto &b) {
        return std::tie(a.priority, a.owner, a.id) < std::tie(b.priority, b.owner, b.id);
    });
    QStringList result;
    for (const auto &mount : mounts)
        result.append(mount.token);
    return result;
}
QString EchoUiPartRegistry::windowId(QQuickWindow *window) const
{
    if (!window)
        return {};
    if (!m_windows.contains(window)) {
        m_windows.insert(window, QString::number(++m_windowSequence));
        connect(window, &QObject::destroyed, this, [this, window] { m_windows.remove(window); });
    }
    return m_windows.value(window);
}
} // namespace EchoMusic
