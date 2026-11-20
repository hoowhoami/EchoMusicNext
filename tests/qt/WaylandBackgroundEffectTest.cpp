#include "WaylandBackgroundEffect.h"
#include "WindowBackdrop.h"
#include "HyprlandWindowBackdrop.h"
#include "wayland-ext-background-effect-v1-client-protocol.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPlatformSurfaceEvent>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <wayland-client.h>

namespace EchoMusic
{
namespace
{
class ObservedBackdropWindow final : public QWindow
{
public:
    int widthSubscribers() const
    {
        return receivers(SIGNAL(widthChanged(int)));
    }
    int heightSubscribers() const
    {
        return receivers(SIGNAL(heightChanged(int)));
    }
};
} // namespace
class WaylandBackgroundEffectTest : public QObject
{
    Q_OBJECT
private:
    struct Request
    {
        uint32_t object;
        uint16_t opcode;
        QByteArray arguments;
    };

    static QList<Request> requests(wl_display *display, int peer)
    {
        wl_display_flush(display);
        QByteArray bytes;
        char buffer[4096];
        ssize_t count;
        while ((count = recv(peer, buffer, sizeof(buffer), MSG_DONTWAIT)) > 0)
            bytes.append(buffer, count);
        QList<Request> result;
        qsizetype offset = 0;
        while (offset + 8 <= bytes.size()) {
            uint32_t header[2];
            std::memcpy(header, bytes.constData() + offset, sizeof(header));
            const auto size = header[1] >> 16;
            if (size < 8 || offset + size > bytes.size())
                break;
            result.append(
                {header[0], uint16_t(header[1] & 0xffff), bytes.mid(offset + 8, size - 8)});
            offset += size;
        }
        return result;
    }

    static uint32_t word(const QByteArray &bytes, qsizetype index)
    {
        uint32_t value = 0;
        if (bytes.size() >= (index + 1) * qsizetype(sizeof(value)))
            std::memcpy(&value, bytes.constData() + index * sizeof(value), sizeof(value));
        return value;
    }

private slots:
    void hyprlandRetirementCancelsPendingCommands()
    {
        QTemporaryDir directory;
        QFile executable(directory.filePath("hyprctl"));
        QVERIFY(executable.open(QIODevice::WriteOnly));
        executable.write(R"(#!/usr/bin/env python3
import json, os, sys, time
from pathlib import Path
root = Path(__file__).parent
with (root / 'calls.jsonl').open('a') as log:
    log.write(json.dumps(sys.argv[1:]) + '\n')
if sys.argv[1] == 'clients':
    if not (root / 'release').exists():
        (root / 'pending').touch()
        time.sleep(5)
    print(json.dumps([{'pid': os.getppid(), 'address': '0xcafe', 'title': 'Restored'}]))
)");
        executable.close();
        QVERIFY(executable.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const auto oldPath = qgetenv("PATH");
        const auto cleanup = qScopeGuard([&] { qputenv("PATH", oldPath); });
        qputenv("PATH", directory.path().toUtf8() + ':' + oldPath);
        QWindow retired;
        retired.setTitle("Retired");
        QWindow restored;
        restored.setTitle("Restored");
        HyprlandWindowBackdrop backdrop;
        backdrop.attach(&retired);
        QSignalSpy completed(&backdrop, &HyprlandWindowBackdrop::completed);
        backdrop.apply(true);
        QTRY_VERIFY(QFile::exists(directory.filePath("pending")));
        QPointer<QProcess> pending = backdrop.findChild<QProcess *>();
        QVERIFY(pending);
        backdrop.attach(nullptr);
        QTRY_VERIFY(pending.isNull());
        QCOMPARE(completed.size(), 0);
        QFile release(directory.filePath("release"));
        QVERIFY(release.open(QIODevice::WriteOnly));
        release.close();
        backdrop.attach(&restored);
        backdrop.apply(false);
        QTRY_COMPARE(completed.size(), 1);
        QVERIFY(completed.first().first().toString().isEmpty());
        QFile calls(directory.filePath("calls.jsonl"));
        QVERIFY(calls.open(QIODevice::ReadOnly));
        const auto lines = calls.readAll().trimmed().split('\n');
        QCOMPARE(lines.size(), 3);
        const auto command = QJsonDocument::fromJson(lines.last()).array();
        QCOMPARE(command[0].toString(), QString("dispatch"));
        QVERIFY(command[1].toString().contains("address:0xcafe"));
        QVERIFY(command[1].toString().contains("value = \"1\""));
        backdrop.attach(nullptr);
        QTRY_VERIFY(backdrop.findChildren<QProcess *>().isEmpty());
    }

    void backgroundDetachReleasesEffectsAndWindowSubscriptions()
    {
        int sockets[2];
        QCOMPARE(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
        auto *display = wl_display_connect_to_fd(sockets[0]);
        QVERIFY(display);
        auto *registry = wl_display_get_registry(display);
        auto *compositor = static_cast<wl_compositor *>(
            wl_registry_bind(registry, 1, &wl_compositor_interface, 1));
        auto *surface = wl_compositor_create_surface(compositor);
        const auto cleanup = qScopeGuard([&] {
            wl_surface_destroy(surface);
            wl_compositor_destroy(compositor);
            wl_display_disconnect(display);
            close(sockets[1]);
        });
        ObservedBackdropWindow window;
        const int widthSubscribers = window.widthSubscribers();
        const int heightSubscribers = window.heightSubscribers();
        WaylandBackgroundEffect effect;
        effect.m_display = display;
        effect.m_registry = registry;
        effect.m_compositor = compositor;
        effect.bindGlobal(registry, 2, ext_background_effect_manager_v1_interface.name, 1);
        effect.setEnabled(true);
        const auto managerId = wl_proxy_get_id(reinterpret_cast<wl_proxy *>(effect.m_manager));
        const uint32_t capability[] = {managerId, 12u << 16,
                                       EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR};
        QCOMPARE(send(sockets[1], capability, sizeof(capability), 0), ssize_t(sizeof(capability)));
        QVERIFY(wl_display_dispatch(display) >= 0);
        requests(display, sockets[1]);
        for (int iteration = 0; iteration < 100; ++iteration) {
            effect.attach(&window);
            effect.m_surface = surface;
            QVERIFY(effect.attach(&window));
            QVERIFY(effect.m_effect);
            QCOMPARE(window.widthSubscribers(), widthSubscribers + 1);
            QCOMPARE(window.heightSubscribers(), heightSubscribers + 1);
            const auto effectId = wl_proxy_get_id(reinterpret_cast<wl_proxy *>(effect.m_effect));
            const auto created = requests(display, sockets[1]);
            int creations = 0;
            for (const auto &request : created)
                if (request.object == managerId && request.opcode == 1)
                    ++creations;
            QCOMPARE(creations, 1);
            effect.attach(nullptr);
            QVERIFY(!effect.m_effect);
            QVERIFY(!effect.m_surface);
            QCOMPARE(window.widthSubscribers(), widthSubscribers);
            QCOMPARE(window.heightSubscribers(), heightSubscribers);
            const auto retired = requests(display, sockets[1]);
            QCOMPARE(retired.size(), 1);
            QCOMPARE(retired.first().object, effectId);
            QCOMPARE(retired.first().opcode, uint16_t(0));
            window.resize(640 + iteration, 480 + iteration);
            effect.setEnabled(iteration % 2 == 0);
            QVERIFY(requests(display, sockets[1]).isEmpty());
        }
        const uint32_t noCapability[] = {managerId, 12u << 16, 0};
        QCOMPARE(send(sockets[1], noCapability, sizeof(noCapability), 0),
                 ssize_t(sizeof(noCapability)));
        QVERIFY(wl_display_dispatch(display) >= 0);
        QCOMPARE(send(sockets[1], capability, sizeof(capability), 0), ssize_t(sizeof(capability)));
        QVERIFY(wl_display_dispatch(display) >= 0);
        QVERIFY(!effect.m_effect);
        QVERIFY(requests(display, sockets[1]).isEmpty());
    }

    void capabilitiesRecover_data()
    {
        QTest::addColumn<bool>("enabled");
        QTest::addColumn<bool>("restoredEnabled");
        QTest::newRow("enabled") << true << true;
        QTest::newRow("disabled") << false << false;
        QTest::newRow("disabled-while-unavailable") << true << false;
        QTest::newRow("enabled-while-unavailable") << false << true;
    }

    void capabilitiesRecover()
    {
        QFETCH(bool, enabled);
        QFETCH(bool, restoredEnabled);
        int sockets[2];
        QVERIFY(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0);
        auto *display = wl_display_connect_to_fd(sockets[0]);
        QVERIFY(display);
        auto *registry = wl_display_get_registry(display);
        auto *compositor = static_cast<wl_compositor *>(
            wl_registry_bind(registry, 1, &wl_compositor_interface, 1));
        auto *surface = wl_compositor_create_surface(compositor);
        const auto cleanup = qScopeGuard([&] {
            wl_surface_destroy(surface);
            wl_compositor_destroy(compositor);
            wl_display_disconnect(display);
            close(sockets[1]);
        });
        QWindow window;
        window.resize(640, 480);
        WaylandBackgroundEffect effect;
        QVERIFY(!effect.attach(&window));
        effect.m_display = display;
        effect.m_registry = registry;
        effect.m_compositor = compositor;
        effect.m_surface = surface;
        effect.bindGlobal(registry, 2, ext_background_effect_manager_v1_interface.name, 1);
        effect.setEnabled(enabled);
        QSignalSpy changes(&effect, &WaylandBackgroundEffect::availableChanged);
        const auto managerId = wl_proxy_get_id(reinterpret_cast<wl_proxy *>(effect.m_manager));
        const auto publish = [&](uint32_t flags) {
            const uint32_t event[] = {managerId, 12u << 16, flags};
            QCOMPARE(send(sockets[1], event, sizeof(event), 0), ssize_t(sizeof(event)));
            QVERIFY(wl_display_dispatch(display) >= 0);
        };
        publish(0);
        QCOMPARE(changes.size(), 0);
        QVERIFY(!effect.attach(&window));
        QVERIFY(!effect.m_effect);
        requests(display, sockets[1]);

        publish(EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR);
        QCOMPARE(changes.size(), 1);
        QVERIFY(effect.available());
        QVERIFY(effect.m_effect);
        auto *original = effect.m_effect;
        const auto effectId = wl_proxy_get_id(reinterpret_cast<wl_proxy *>(effect.m_effect));
        const auto compositorId = wl_proxy_get_id(reinterpret_cast<wl_proxy *>(compositor));
        const auto created = requests(display, sockets[1]);
        int creations = 0;
        int regions = 0;
        int applied = 0;
        for (const auto &request : created) {
            if (request.object == managerId && request.opcode == 1)
                ++creations;
            if (request.object == compositorId && request.opcode == WL_COMPOSITOR_CREATE_REGION)
                ++regions;
            if (request.object == effectId && request.opcode == 1) {
                QCOMPARE(word(request.arguments, 0) != 0, enabled);
                ++applied;
            }
            if (request.arguments.size() == 16 && request.opcode == WL_REGION_ADD) {
                QCOMPARE(word(request.arguments, 0), 0u);
                QCOMPARE(word(request.arguments, 1), 0u);
                QCOMPARE(word(request.arguments, 2), 640u);
                QCOMPARE(word(request.arguments, 3), 480u);
            }
        }
        QCOMPARE(creations, 1);
        QCOMPARE(regions > 0, enabled);
        QVERIFY(applied > 0);
        QVERIFY(effect.attach(&window));
        QCOMPARE(effect.m_effect, original);
        QVERIFY(requests(display, sockets[1]).isEmpty());

        publish(0);
        QCOMPARE(changes.size(), 2);
        QVERIFY(!effect.available());
        auto cleared = requests(display, sockets[1]);
        QVERIFY(!cleared.isEmpty());
        QCOMPARE(cleared.last().object, effectId);
        QCOMPARE(word(cleared.last().arguments, 0), 0u);
        effect.setEnabled(restoredEnabled);
        requests(display, sockets[1]);
        publish(EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR);
        QCOMPARE(changes.size(), 3);
        QCOMPARE(effect.m_effect, original);
        auto restored = requests(display, sockets[1]);
        bool restoredRegion = false;
        for (const auto &request : restored)
            if (request.object == effectId && request.opcode == 1)
                restoredRegion = word(request.arguments, 0) != 0;
        QCOMPARE(restoredRegion, restoredEnabled);
        publish(EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR);
        QCOMPARE(changes.size(), 3);
        QVERIFY(requests(display, sockets[1]).isEmpty());
        QPlatformSurfaceEvent destroying(QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed);
        QCoreApplication::sendEvent(&window, &destroying);
        QVERIFY(!effect.m_effect);
        effect.m_surface = surface;
        QVERIFY(effect.attach(&window));
        QVERIFY(effect.m_effect);
        effect.attach(nullptr);
        QVERIFY(!effect.m_effect);
    }

    void xcbRetainsHyprlandFallback()
    {
        if (QGuiApplication::platformName() != "xcb")
            QSKIP("Run with QT_QPA_PLATFORM=xcb under Xvfb to exercise XWayland fallback");
        QTemporaryDir directory;
        QFile executable(directory.filePath("hyprctl"));
        QVERIFY(executable.open(QIODevice::WriteOnly));
        executable.write(R"(#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
with Path(__file__).with_suffix('.log').open('a') as log:
    log.write(json.dumps(sys.argv[1:]) + '\n')
if sys.argv[1] == 'clients':
    print(json.dumps([{'pid': os.getppid(), 'address': '0xbeef', 'title': 'Test'}]))
)");
        executable.close();
        QVERIFY(executable.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const auto oldPath = qgetenv("PATH");
        const auto oldSignature = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
        const auto cleanup = qScopeGuard([&] {
            qputenv("PATH", oldPath);
            if (oldSignature.isNull())
                qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
            else
                qputenv("HYPRLAND_INSTANCE_SIGNATURE", oldSignature);
        });
        qputenv("PATH", directory.path().toUtf8() + ':' + oldPath);
        qputenv("HYPRLAND_INSTANCE_SIGNATURE", "fixture");
        QWindow window;
        window.setTitle("Test");
        WindowBackdrop backdrop(true);
        backdrop.attach(&window);
        QVERIFY(backdrop.supported());
        QSignalSpy completed(&backdrop, &WindowBackdrop::completed);
        QVERIFY(backdrop.apply(true, false).isEmpty());
        QTRY_COMPARE(completed.size(), 1);
        QVERIFY(completed.first().first().toString().isEmpty());
        QVERIFY(backdrop.apply(false, false).isEmpty());
        QTRY_COMPARE(completed.size(), 2);
        QVERIFY(completed.last().first().toString().isEmpty());
        QFile log(directory.filePath("hyprctl.log"));
        QVERIFY(log.open(QIODevice::ReadOnly));
        const auto calls = log.readAll().trimmed().split('\n');
        QCOMPARE(calls.size(), 4);
        QVERIFY(QJsonDocument::fromJson(calls[1]).array()[1].toString().contains("value = \"0\""));
        QVERIFY(QJsonDocument::fromJson(calls[3]).array()[1].toString().contains("value = \"1\""));
    }
};
} // namespace EchoMusic

QTEST_MAIN(EchoMusic::WaylandBackgroundEffectTest)
#include "WaylandBackgroundEffectTest.moc"
