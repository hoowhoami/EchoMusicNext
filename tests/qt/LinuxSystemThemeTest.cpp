#include "LinuxSystemTheme.h"
#include <QDBusVirtualObject>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QPalette>
#include <QProcess>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QFile>
#include <memory>
#include <QtTest>

using namespace EchoMusic;
namespace
{
const QString service = QStringLiteral("org.freedesktop.portal.Desktop");
const QString path = QStringLiteral("/org/freedesktop/portal/desktop");
const QString interface = QStringLiteral("org.freedesktop.portal.Settings");
const QString appearance = QStringLiteral("org.freedesktop.appearance");
const QString key = QStringLiteral("color-scheme");

class Portal final : public QDBusVirtualObject
{
public:
    explicit Portal(const QDBusConnection &bus) : bus(bus) {}
    QString introspect(const QString &) const override
    {
        return {};
    }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.interface() != interface || message.member() != "Read")
            return false;
        ++reads;
        if (message.arguments() != QVariantList{appearance, key})
            return false;
        if (delay) {
            delayed = message;
            message.setDelayedReply(true);
        } else if (fail) {
            connection.send(message.createErrorReply(QDBusError::UnknownMethod, "No setting"));
        } else {
            respond(message, value);
        }
        return true;
    }
    void respond(const QDBusMessage &message, uint value)
    {
        // Reproduce the double variant used by Settings.Read, rather than just
        // invoking the adapter's slot with a pre-decoded integer.
        bus.send(message.createReply(QVariantList{
            QVariant::fromValue(QDBusVariant(QVariant::fromValue(QDBusVariant(value))))}));
    }
    void change(QVariant value, const QString &group = appearance, const QString &name = key)
    {
        auto message = QDBusMessage::createSignal(path, interface, "SettingChanged");
        message << group << name << QVariant::fromValue(QDBusVariant(value));
        bus.send(message);
    }
    QDBusConnection bus;
    uint value = 1;
    int reads = 0;
    bool delay = false, fail = false;
    QDBusMessage delayed;
};
} // namespace

class LinuxSystemThemeTest final : public QObject
{
    Q_OBJECT
    QProcess daemon;
    QTemporaryDir busConfig;
    QString address;
    std::unique_ptr<QDBusConnection> client, server;
    std::unique_ptr<Portal> portal;
    QPalette oldPalette;
private slots:
    void initTestCase()
    {
        const auto executable = QStandardPaths::findExecutable("dbus-daemon");
        if (executable.isEmpty())
            QSKIP("dbus-daemon is required for isolated portal integration tests");
        // No host service directories or launchd socket: never activate the
        // real desktop portal on the test bus, including the missing-service case.
        QFile config(busConfig.filePath("bus.conf"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen>"
                     "<auth>EXTERNAL</auth><policy context=\"default\">"
                     "<allow send_destination=\"*\"/><allow receive_sender=\"*\"/>"
                     "<allow own=\"*\"/></policy></busconfig>");
        config.close();
        daemon.start(executable,
                     {"--config-file=" + config.fileName(), "--nofork", "--print-address=1"});
        QVERIFY(daemon.waitForStarted());
        QTRY_VERIFY(daemon.canReadLine());
        address = QString::fromUtf8(daemon.readLine()).trimmed();
        QVERIFY(!address.isEmpty());
        client = std::make_unique<QDBusConnection>(
            QDBusConnection::connectToBus(address, "theme-test-client"));
        server = std::make_unique<QDBusConnection>(
            QDBusConnection::connectToBus(address, "theme-test-server"));
        QVERIFY2(client->isConnected(), qPrintable(client->lastError().message()));
        QVERIFY2(server->isConnected(), qPrintable(server->lastError().message()));
        oldPalette = QGuiApplication::palette();
    }
    void init()
    {
        portal = std::make_unique<Portal>(*server);
        QVERIFY(server->registerVirtualObject(path, portal.get()));
        QVERIFY(server->registerService(service));
    }
    void cleanup()
    {
        server->unregisterService(service);
        server->unregisterObject(path);
        portal.reset();
        QGuiApplication::setPalette(oldPalette);
    }
    void cleanupTestCase()
    {
        QDBusConnection::disconnectFromBus("theme-test-client");
        QDBusConnection::disconnectFromBus("theme-test-server");
        daemon.terminate();
        if (!daemon.waitForFinished(3000)) {
            daemon.kill();
            daemon.waitForFinished();
        }
    }
    void startupAndLiveChanges()
    {
        LinuxSystemTheme monitor(*client);
        QSignalSpy changes(&monitor, &LinuxSystemTheme::schemeChanged);
        QTRY_VERIFY(portal->reads > 0);
        QTRY_COMPARE(monitor.scheme(), Qt::ColorScheme::Dark);
        changes.clear();
        portal->change(2u);
        QTRY_COMPARE(monitor.scheme(), Qt::ColorScheme::Light);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes.constLast().at(0).value<Qt::ColorScheme>(), Qt::ColorScheme::Light);
        portal->change(2u);
        QTest::qWait(50);
        QCOMPARE(changes.size(), 1);
        portal->change(1u);
        QTRY_COMPARE(monitor.scheme(), Qt::ColorScheme::Dark);
        QCOMPARE(changes.size(), 2);
    }
    void lateReadCannotUndoNewerSignal()
    {
        portal->delay = true;
        LinuxSystemTheme monitor(*client);
        QTRY_VERIFY(portal->reads > 0);
        portal->change(1u);
        QTRY_COMPARE(monitor.scheme(), Qt::ColorScheme::Dark);
        portal->respond(portal->delayed, 2u);
        QTest::qWait(50);
        QCOMPARE(monitor.scheme(), Qt::ColorScheme::Dark);
    }
    void missingPreferencesUseQtThenPalette()
    {
        portal->fail = true;
        LinuxSystemTheme monitor(*client);
        QTRY_VERIFY(portal->reads > 0);
        QPalette dark = oldPalette;
        dark.setColor(QPalette::Window, QColor("#101010"));
        dark.setColor(QPalette::WindowText, Qt::white);
        QGuiApplication::setPalette(dark);
        const auto qt = QGuiApplication::styleHints()->colorScheme();
        const auto fallback = qt == Qt::ColorScheme::Unknown ? Qt::ColorScheme::Dark : qt;
        QTRY_COMPARE(monitor.scheme(), fallback);
        portal->change(2u);
        QTRY_COMPARE(monitor.scheme(), Qt::ColorScheme::Light);
        portal->change(0u);
        QTRY_COMPARE(monitor.scheme(), fallback);
        portal->change(2u);
        QTRY_COMPARE(monitor.scheme(), Qt::ColorScheme::Light);
        portal->change(99u);
        QTRY_COMPARE(monitor.scheme(), fallback);
        portal->change(2u);
        QTRY_COMPARE(monitor.scheme(), Qt::ColorScheme::Light);
        portal->change(QStringLiteral("dark"));
        QTRY_COMPARE(monitor.scheme(), fallback);
        portal->change(2u, "unrelated", key);
        portal->change(2u, appearance, "accent-color");
        QTest::qWait(50);
        QCOMPARE(monitor.scheme(), fallback);
    }
    void serviceRestartAndLateAvailability()
    {
        QVERIFY(server->unregisterService(service));
        LinuxSystemTheme monitor(*client);
        QTest::qWait(50);
        portal->value = 1;
        QVERIFY(server->registerService(service));
        QTRY_COMPARE(monitor.scheme(), Qt::ColorScheme::Dark);
        const auto reads = portal->reads;
        QVERIFY(server->unregisterService(service));
        QTest::qWait(50);
        portal->value = 2;
        QVERIFY(server->registerService(service));
        QTRY_VERIFY(portal->reads > reads);
        QTRY_COMPARE(monitor.scheme(), Qt::ColorScheme::Light);
    }
    void destructionWithPendingRead()
    {
        portal->delay = true;
        auto monitor = std::make_unique<LinuxSystemTheme>(*client);
        QTRY_VERIFY(portal->reads > 0);
        monitor.reset();
        portal->respond(portal->delayed, 1u);
        portal->change(2u);
        QTest::qWait(50);
    }
};
QTEST_MAIN(LinuxSystemThemeTest)
#include "LinuxSystemThemeTest.moc"
