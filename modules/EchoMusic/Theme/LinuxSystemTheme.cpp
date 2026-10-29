#include "LinuxSystemTheme.h"
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <QEvent>

namespace EchoMusic
{
namespace
{
const QString service = QStringLiteral("org.freedesktop.portal.Desktop");
const QString path = QStringLiteral("/org/freedesktop/portal/desktop");
const QString interface = QStringLiteral("org.freedesktop.portal.Settings");
const QString appearance = QStringLiteral("org.freedesktop.appearance");
const QString colorScheme = QStringLiteral("color-scheme");

Qt::ColorScheme portalScheme(QVariant value)
{
    // Settings.Read on version-1 portals can wrap its uint in two variants.
    for (int i = 0; i < 2 && value.metaType() == QMetaType::fromType<QDBusVariant>(); ++i)
        value = qvariant_cast<QDBusVariant>(value).variant();
    if (value.metaType() != QMetaType::fromType<uint>())
        return Qt::ColorScheme::Unknown;
    switch (value.toUInt()) {
    case 1:
        return Qt::ColorScheme::Dark;
    case 2:
        return Qt::ColorScheme::Light;
    default:
        return Qt::ColorScheme::Unknown;
    }
}
} // namespace

LinuxSystemTheme::LinuxSystemTheme(QObject *parent)
    : LinuxSystemTheme(QDBusConnection::sessionBus(), parent)
{
}

LinuxSystemTheme::LinuxSystemTheme(const QDBusConnection &bus, QObject *parent)
    : QObject(parent), m_bus(bus)
{
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            &LinuxSystemTheme::update);
    qGuiApp->installEventFilter(this);
    update();
    // Subscribe before reading, so a switch during startup cannot be missed.
    m_bus.connect(service, path, interface, QStringLiteral("SettingChanged"), this,
                  SLOT(settingChanged(QString, QString, QDBusVariant)));
    auto *watcher =
        new QDBusServiceWatcher(service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) {
                ++m_revision;
                m_portalScheme = Qt::ColorScheme::Unknown;
                update();
                if (!owner.isEmpty())
                    read();
            });
    read();
}

void LinuxSystemTheme::read()
{
    const auto revision = ++m_revision;
    auto message = QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("Read"));
    message << appearance << colorScheme;
    // Read is supported by the Ubuntu 22.04 portal baseline. Never block the GUI
    // waiting for desktop activation, and do not poll when no backend exists.
    auto *pending = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 3000), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this,
            [this, revision](QDBusPendingCallWatcher *pending) {
                const QDBusPendingReply<QDBusVariant> reply = *pending;
                pending->deleteLater();
                if (revision != m_revision)
                    return;
                m_portalScheme = reply.isValid() ? portalScheme(reply.value().variant())
                                                 : Qt::ColorScheme::Unknown;
                update();
            });
}

void LinuxSystemTheme::settingChanged(const QString &group, const QString &key,
                                      const QDBusVariant &value)
{
    if (group != appearance || key != colorScheme)
        return;
    ++m_revision; // A late initial read must not undo a newer desktop notification.
    m_portalScheme = portalScheme(value.variant());
    update();
}

void LinuxSystemTheme::update()
{
    auto scheme = m_portalScheme;
    if (scheme == Qt::ColorScheme::Unknown)
        scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Unknown) {
        // Generic Linux platform themes may expose only a palette. Do not treat
        // Unknown as an explicit light preference on a dark desktop palette.
        const auto palette = QGuiApplication::palette();
        scheme = palette.color(QPalette::Window).lightness() <
                         palette.color(QPalette::WindowText).lightness()
                     ? Qt::ColorScheme::Dark
                     : Qt::ColorScheme::Light;
    }
    if (m_scheme == scheme)
        return;
    m_scheme = scheme;
    emit schemeChanged(scheme);
}
bool LinuxSystemTheme::eventFilter(QObject *object, QEvent *event)
{
    if (object == qGuiApp && event->type() == QEvent::ApplicationPaletteChange)
        update();
    return QObject::eventFilter(object, event);
}
} // namespace EchoMusic
