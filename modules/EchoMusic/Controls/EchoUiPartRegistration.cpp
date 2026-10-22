#include "EchoUiPartRegistration.h"
#include "EchoUiPartRegistry.h"
#include <QQuickWindow>
#include <QQmlEngine>
#include <QJsonDocument>
namespace EchoMusic
{
EchoUiPartRegistration::EchoUiPartRegistration(QObject *parent) : QObject(parent) {}
EchoUiPartRegistration::~EchoUiPartRegistration()
{
    if (m_registry)
        m_registry->detach(this);
}
void EchoUiPartRegistration::setState(const QVariantMap &v)
{
    if (m_state == v)
        return;
    m_state = v;
    emit stateChanged();
    if (std::exchange(m_stateNoticePending, true))
        return;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_stateNoticePending = false;
            emit changed();
        },
        Qt::QueuedConnection);
}
void EchoUiPartRegistration::componentComplete()
{
    m_complete = true;
    if (auto *engine = qmlEngine(this))
        m_registry = EchoUiPartRegistry::forEngine(*engine);
    connectTarget();
    refresh();
}
void EchoUiPartRegistration::refresh()
{
    if (!m_complete || !m_registry)
        return;
    m_registry->detach(this);
    m_registry->attach(this);
    emit changed();
}
void EchoUiPartRegistration::setTarget(QQuickItem *target)
{
    if (m_target == target)
        return;
    if (m_registry)
        m_registry->detach(this);
    m_target = target;
    if (m_complete)
        connectTarget();
    emit declarationChanged();
    refresh();
}
#define ECHO_PART_SETTER(Name, Type, Member)                                                       \
    void EchoUiPartRegistration::set##Name(Type value)                                             \
    {                                                                                              \
        if (Member == value)                                                                       \
            return;                                                                                \
        Member = value;                                                                            \
        emit declarationChanged();                                                                 \
        refresh();                                                                                 \
    }
ECHO_PART_SETTER(Role, const QString &, m_role)
ECHO_PART_SETTER(Key, const QString &, m_key)
ECHO_PART_SETTER(Surface, const QString &, m_surface)
ECHO_PART_SETTER(Properties, const QStringList &, m_properties)
ECHO_PART_SETTER(Actions, const QStringList &, m_actions)
ECHO_PART_SETTER(Active, bool, m_active)
ECHO_PART_SETTER(Replaceable, bool, m_replaceable)
#undef ECHO_PART_SETTER
void EchoUiPartRegistration::connectTarget()
{
    for (const auto &c : m_connections)
        disconnect(c);
    m_connections.clear();
    if (!m_target)
        return;
    const auto update = [this] {
        emit changed();
        if (m_registry && !m_identity.isEmpty())
            emit m_registry->partChanged(this);
    };
    m_connections << connect(m_target, &QQuickItem::xChanged, this, update)
                  << connect(m_target, &QQuickItem::yChanged, this, update)
                  << connect(m_target, &QQuickItem::widthChanged, this, update)
                  << connect(m_target, &QQuickItem::heightChanged, this, update)
                  << connect(m_target, &QQuickItem::visibleChanged, this, update)
                  << connect(m_target, &QQuickItem::enabledChanged, this, update)
                  << connect(m_target, &QQuickItem::parentChanged, this,
                             [this] {
                                 connectTarget();
                                 refresh();
                             })
                  << connect(m_target, &QQuickItem::windowChanged, this,
                             [this] {
                                 connectTarget();
                                 refresh();
                             })
                  << connect(m_target, &QObject::destroyed, this, [this] {
                         if (m_registry)
                             m_registry->detach(this);
                         m_target = nullptr;
                         emit changed();
                     });
    if (auto *window = m_target->window())
        m_connections << connect(window, &QWindow::visibilityChanged, this, update);
}
void EchoUiPartRegistration::setBaseValues(const QVariantMap &values)
{
    if (m_base == values)
        return;
    m_base = values;
    emit changed();
}
void EchoUiPartRegistration::setResolved(const QVariantMap &values)
{
    if (m_overrides == values)
        return;
    m_overrides = values;
    emit overridesChanged();
    emit changed();
}
void EchoUiPartRegistration::setReplacement(const QString &token, const QString &candidate)
{
    if (m_replacement == token && m_candidate == candidate)
        return;
    m_replacement = token;
    m_candidate = candidate;
    emit replacementChanged();
    emit changed();
}
QString EchoUiPartRegistration::surface() const
{
    if (!m_surface.isEmpty())
        return m_surface;
    for (auto *item = m_target.data(); item; item = item->parentItem()) {
        const auto surface = item->property("echoUiSurface").toString();
        if (!surface.isEmpty())
            return surface;
    }
    const auto name = m_target && m_target->window() ? m_target->window()->objectName() : QString();
    return name.contains("mini", Qt::CaseInsensitive)           ? "mini"
           : name.contains("desktopLyric", Qt::CaseInsensitive) ? "desktop"
                                                                : "main";
}
QString EchoUiPartRegistration::windowId() const
{
    return m_registry && m_target ? m_registry->windowId(m_target->window()) : QString();
}
QRectF EchoUiPartRegistration::geometry() const
{
    return m_target ? m_target->mapRectToScene(m_target->boundingRect()) : QRectF();
}
bool EchoUiPartRegistration::presented() const
{
    if (m_identity.isEmpty() || !m_target || !m_target->isVisible() || !m_target->window() ||
        !m_target->window()->isVisible() || m_target->window()->visibility() == QWindow::Minimized)
        return false;
    QRectF visible = geometry().intersected(
        QRectF(0, 0, m_target->window()->width(), m_target->window()->height()));
    for (auto *parent = m_target->parentItem(); parent && !visible.isEmpty();
         parent = parent->parentItem())
        if (parent->clip())
            visible = visible.intersected(parent->mapRectToScene(parent->boundingRect()));
    return !visible.isEmpty();
}
QFont EchoUiPartRegistration::resolveFont(const QFont &base, const QVariantMap &values) const
{
    QFont font =
        values.value("font").canConvert<QFont>() ? values.value("font").value<QFont>() : base;
    if (values.contains("fontSize"))
        font.setPixelSize(qRound(values.value("fontSize").toDouble()));
    if (values.contains("fontWeight"))
        font.setWeight(QFont::Weight(qRound(values.value("fontWeight").toDouble())));
    if (values.contains("letterSpacing"))
        font.setLetterSpacing(QFont::AbsoluteSpacing, values.value("letterSpacing").toDouble());
    return font;
}
void EchoUiPartRegistration::publish(const QString &event, const QVariantMap &payload)
{
    if (!presented() || event.isEmpty() || event.size() > 128 ||
        QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact).size() > 65536)
        return;
    emit eventOccurred(event, payload);
}
bool EchoUiPartRegistration::invoke(const QString &action, const QVariantMap &arguments)
{
    if (!presented() || !m_target->isEnabled() || !m_actions.contains(action))
        return false;
    emit actionRequested(action, arguments);
    return true;
}
} // namespace EchoMusic
