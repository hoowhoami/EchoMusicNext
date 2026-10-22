#include "GlobalShortcuts.h"
#include <QAbstractNativeEventFilter>
#include <QGuiApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QTimer>
#include <QUuid>
#if QT_CONFIG(xcb)
#include <QtGui/qguiapplication_platform.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#endif

namespace EchoMusic
{
struct PortalShortcut
{
    QString id;
    QVariantMap options;
};
using PortalShortcuts = QList<PortalShortcut>;
QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument << shortcut.id << shortcut.options;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument >> shortcut.id >> shortcut.options;
    argument.endStructure();
    return argument;
}
} // namespace EchoMusic
Q_DECLARE_METATYPE(EchoMusic::PortalShortcut)
Q_DECLARE_METATYPE(EchoMusic::PortalShortcuts)

namespace EchoMusic
{
namespace
{
const QString service = QStringLiteral("org.freedesktop.portal.Desktop");
const QString path = QStringLiteral("/org/freedesktop/portal/desktop");
const QString interface = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");
QString portalTrigger(QKeyCombination combination)
{
    QString result;
    const auto modifiers = combination.keyboardModifiers();
    if (modifiers.testFlag(Qt::ControlModifier))
        result += "CTRL+";
    if (modifiers.testFlag(Qt::AltModifier))
        result += "ALT+";
    if (modifiers.testFlag(Qt::ShiftModifier))
        result += "SHIFT+";
    if (modifiers.testFlag(Qt::MetaModifier))
        result += "LOGO+";
    switch (combination.key()) {
    case Qt::Key_Space:
        return result + "space";
    case Qt::Key_Return:
        return result + "Return";
    case Qt::Key_PageUp:
        return result + "Prior";
    case Qt::Key_PageDown:
        return result + "Next";
    default:
        return result + QKeySequence(combination.key()).toString(QKeySequence::PortableText);
    }
}
} // namespace
/// An asynchronous portal session owns all Wayland bindings. Request paths are
/// connected before calls, with bounded wait and close on replacement/destruction.
class ShortcutPortal final : public QObject
{
    Q_OBJECT
public:
    explicit ShortcutPortal(GlobalShortcuts &owner) : m_owner(owner)
    {
        qDBusRegisterMetaType<PortalShortcut>();
        qDBusRegisterMetaType<PortalShortcuts>();
        m_timeout.setSingleShot(true);
        m_timeout.setInterval(30000);
        connect(&m_timeout, &QTimer::timeout, this, [this] { fail(tr("系统全局快捷键授权超时")); });
        m_bus.connect(service, path, interface, "Activated", this,
                      SLOT(activated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
    }
    ~ShortcutPortal() override
    {
        clear();
    }
    void clear()
    {
        ++m_generation;
        m_timeout.stop();
        if (!m_request.isEmpty()) {
            m_bus.disconnect(service, m_request, "org.freedesktop.portal.Request", "Response", this,
                             SLOT(response(uint, QVariantMap)));
            m_bus.asyncCall(QDBusMessage::createMethodCall(
                service, m_request, "org.freedesktop.portal.Request", "Close"));
        }
        if (!m_session.isEmpty())
            m_bus.asyncCall(QDBusMessage::createMethodCall(
                service, m_session, "org.freedesktop.portal.Session", "Close"));
        m_request.clear();
        m_session.clear();
        m_bindings.clear();
    }
    void replace(const QList<GlobalShortcuts::Binding> &bindings)
    {
        clear();
        m_bindings = bindings;
        if (bindings.isEmpty())
            return;
        // Registry identifies an unsandboxed desktop process on recent portals.
        // Older portals lack Registry and continue through their existing path.
        auto registry = QDBusMessage::createMethodCall(
            service, path, "org.freedesktop.host.portal.Registry", "Register");
        registry << QStringLiteral(ECHO_APPLICATION_ID) << QVariantMap{};
        auto watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(registry), this);
        const auto generation = m_generation;
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, generation](QDBusPendingCallWatcher *done) {
                    done->deleteLater();
                    if (generation != m_generation)
                        return;
                    request(false);
                });
    }
private slots:
    void response(uint code, const QVariantMap &results)
    {
        m_timeout.stop();
        m_bus.disconnect(service, m_request, "org.freedesktop.portal.Request", "Response", this,
                         SLOT(response(uint, QVariantMap)));
        m_request.clear();
        if (code) {
            fail(code == 1 ? tr("已取消系统全局快捷键授权") : tr("系统未能注册全局快捷键"));
            return;
        }
        if (m_session.isEmpty()) {
            // CreateSession returns an object path (o), not a string (s).
            m_session = qdbus_cast<QDBusObjectPath>(results.value("session_handle")).path();
            if (!QDBusObjectPath(m_session).path().startsWith(
                    "/org/freedesktop/portal/desktop/session/")) {
                fail(tr("系统返回的快捷键会话无效"));
                return;
            }
            request(true);
            return;
        }
        const auto registered = qdbus_cast<PortalShortcuts>(results.value("shortcuts"));
        QSet<QString> ids;
        for (const auto &entry : registered)
            ids.insert(entry.id);
        for (const auto &entry : std::as_const(m_bindings))
            if (!ids.contains(QString::number(entry.command)))
                emit m_owner.registrationFailed(entry.command, tr("此快捷键未获系统授权"));
            else
                emit m_owner.registered(entry.command);
    }
    void activated(const QDBusObjectPath &session, const QString &id, qulonglong,
                   const QVariantMap &)
    {
        if (session.path() != m_session)
            return;
        bool valid = false;
        const int command = id.toInt(&valid);
        if (valid)
            for (const auto &entry : std::as_const(m_bindings))
                if (entry.command == command) {
                    emit m_owner.activated(command);
                    return;
                }
    }

private:
    void fail(const QString &message)
    {
        const auto bindings = m_bindings;
        clear();
        for (const auto &entry : bindings)
            emit m_owner.registrationFailed(entry.command, message);
    }
    void request(bool bind)
    {
        QString sender = m_bus.baseService().mid(1).replace('.', '_');
        const auto token = "echo_" + QUuid::createUuid().toString(QUuid::Id128);
        m_request = path + "/request/" + sender + "/" + token;
        m_bus.connect(service, m_request, "org.freedesktop.portal.Request", "Response", this,
                      SLOT(response(uint, QVariantMap)));
        QVariantMap options{{"handle_token", token}};
        auto call = QDBusMessage::createMethodCall(service, path, interface,
                                                   bind ? "BindShortcuts" : "CreateSession");
        if (bind) {
            PortalShortcuts entries;
            for (const auto &entry : std::as_const(m_bindings))
                entries.append({QString::number(entry.command),
                                {{"description", entry.description},
                                 {"preferred_trigger", portalTrigger(entry.sequence[0])}}});
            call << QVariant::fromValue(QDBusObjectPath(m_session)) << QVariant::fromValue(entries)
                 << QString() << options;
        } else {
            options.insert("session_handle_token", token);
            call << options;
        }
        auto watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
        const auto generation = m_generation;
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, generation](QDBusPendingCallWatcher *done) {
                    const QDBusPendingReply<QDBusObjectPath> reply = *done;
                    done->deleteLater();
                    if (generation != m_generation)
                        return;
                    if (reply.isError())
                        fail(tr("当前桌面无法提供全局快捷键：%1").arg(reply.error().message()));
                    else if (reply.value().path() != m_request && !m_request.isEmpty())
                        fail(tr("系统快捷键请求路径不匹配"));
                });
        m_timeout.start();
    }
    GlobalShortcuts &m_owner;
    QDBusConnection m_bus = QDBusConnection::sessionBus();
    QList<GlobalShortcuts::Binding> m_bindings;
    QTimer m_timeout;
    QString m_request, m_session;
    quint64 m_generation = 0;
};
struct GlobalShortcuts::Impl : QAbstractNativeEventFilter
{
    GlobalShortcuts &owner;
    ShortcutPortal portal;
#if QT_CONFIG(xcb)
    struct Grab
    {
        xcb_keycode_t key;
        quint16 modifiers;
        int command;
    };
    QList<Grab> grabs;
    xcb_connection_t *connection = nullptr;
    xcb_window_t root = 0;
#endif
    explicit Impl(GlobalShortcuts &object) : owner(object), portal(object)
    {
        qGuiApp->installNativeEventFilter(this);
    }
    void clear()
    {
        portal.clear();
#if QT_CONFIG(xcb)
        if (connection) {
            for (const auto &grab : std::as_const(grabs))
                xcb_ungrab_key(connection, grab.key, root, grab.modifiers);
            xcb_flush(connection);
        }
        grabs.clear();
#endif
    }
    ~Impl()
    {
        clear();
        qGuiApp->removeNativeEventFilter(this);
    }
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *) override
    {
#if QT_CONFIG(xcb)
        if (type != "xcb_generic_event_t")
            return false;
        const auto event = static_cast<xcb_generic_event_t *>(message);
        if ((event->response_type & 0x7f) != XCB_KEY_PRESS)
            return false;
        const auto key = reinterpret_cast<xcb_key_press_event_t *>(event);
        for (const auto &grab : std::as_const(grabs))
            if (key->detail == grab.key && (key->state & 0xff) == grab.modifiers) {
                emit owner.activated(grab.command);
                return true;
            }
#else
        Q_UNUSED(type)
        Q_UNUSED(message)
#endif
        return false;
    }
};
GlobalShortcuts::GlobalShortcuts(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(*this))
{
}
GlobalShortcuts::~GlobalShortcuts() = default;
void GlobalShortcuts::replace(const QList<Binding> &bindings)
{
    m_impl->clear();
    if (bindings.isEmpty())
        return;
#if QT_CONFIG(xcb)
    if (auto native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
        m_impl->connection = native->connection();
        auto display = native->display();
        m_impl->root = DefaultRootWindow(display);
        unsigned int numLock = 0;
        auto modifiers = XGetModifierMapping(display);
        const auto numKey = XKeysymToKeycode(display, XK_Num_Lock);
        if (modifiers) {
            for (int i = 0; i < 8 * modifiers->max_keypermod; ++i)
                if (modifiers->modifiermap[i] == numKey)
                    numLock |= 1u << (i / modifiers->max_keypermod);
            XFreeModifiermap(modifiers);
        }
        for (const auto &entry : bindings) {
            const auto combination = entry.sequence[0];
            const auto name =
                portalTrigger(QKeyCombination(Qt::NoModifier, combination.key())).toLatin1();
            const auto symbol = XStringToKeysym(name.constData());
            const auto code = XKeysymToKeycode(display, symbol);
            quint16 flags = 0;
            const auto qtModifiers = combination.keyboardModifiers();
            if (qtModifiers.testFlag(Qt::ControlModifier))
                flags |= XCB_MOD_MASK_CONTROL;
            if (qtModifiers.testFlag(Qt::AltModifier))
                flags |= XCB_MOD_MASK_1;
            if (qtModifiers.testFlag(Qt::ShiftModifier))
                flags |= XCB_MOD_MASK_SHIFT;
            if (qtModifiers.testFlag(Qt::MetaModifier))
                flags |= XCB_MOD_MASK_4;
            QList<Impl::Grab> installed;
            const QSet<quint16> locks{0, XCB_MOD_MASK_LOCK, quint16(numLock),
                                      quint16(numLock | XCB_MOD_MASK_LOCK)};
            bool valid = code != 0;
            for (auto lock : locks) {
                if (!valid)
                    break;
                const auto cookie =
                    xcb_grab_key_checked(m_impl->connection, 0, m_impl->root, flags | lock, code,
                                         XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
                auto error = xcb_request_check(m_impl->connection, cookie);
                valid = !error;
                free(error);
                if (valid)
                    installed.append({code, quint16(flags | lock), entry.command});
            }
            if (valid) {
                m_impl->grabs.append(installed);
                emit registered(entry.command);
            } else {
                for (const auto &grab : installed)
                    xcb_ungrab_key(m_impl->connection, grab.key, m_impl->root, grab.modifiers);
                emit registrationFailed(entry.command, tr("系统未能注册此快捷键，可能已被占用"));
            }
        }
        xcb_flush(m_impl->connection);
        return;
    }
#endif
    m_impl->portal.replace(bindings);
}
} // namespace EchoMusic
#include "LinuxGlobalShortcuts.moc"
