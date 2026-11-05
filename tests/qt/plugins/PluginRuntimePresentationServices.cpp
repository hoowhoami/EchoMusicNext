#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "AppIconService.h"
#include "AppIconShortcuts_p.h"
#include "PlaybackClock.h"
#include <QImage>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif
using namespace EchoMusic::PluginTest;
namespace EchoMusic
{
class AppIconServiceTestAccess
{
public:
    static QHash<QString, QString> desired(const AppIconService &service)
    {
        return service.m_desiredShortcuts;
    }
};
} // namespace EchoMusic
namespace
{
#ifdef Q_OS_WIN
template <typename Callback> bool shortcutFile(const QString &path, bool create, Callback callback)
{
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return false;
    const auto uninit = qScopeGuard([] { CoUninitialize(); });
    IShellLinkW *link = nullptr;
    if (FAILED(
            CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
        return false;
    const auto release = qScopeGuard([link] { link->Release(); });
    IPersistFile *file = nullptr;
    if (FAILED(link->QueryInterface(IID_PPV_ARGS(&file))))
        return false;
    const auto releaseFile = qScopeGuard([file] { file->Release(); });
    if (!create && FAILED(file->Load(reinterpret_cast<LPCOLESTR>(path.utf16()), STGM_READ)))
        return false;
    return callback(link) &&
           (!create || SUCCEEDED(file->Save(reinterpret_cast<LPCOLESTR>(path.utf16()), TRUE)));
}
#endif
bool writeIconShortcut(const QString &path, const QString &icon, int index, const QString &comment)
{
#ifdef Q_OS_WIN
    return shortcutFile(path, true, [&](IShellLinkW *link) {
        const auto executable = QCoreApplication::applicationFilePath();
        return SUCCEEDED(link->SetPath(reinterpret_cast<LPCWSTR>(executable.utf16()))) &&
               SUCCEEDED(link->SetIconLocation(reinterpret_cast<LPCWSTR>(icon.utf16()), index)) &&
               SUCCEEDED(link->SetDescription(reinterpret_cast<LPCWSTR>(comment.utf16())));
    });
#else
    Q_UNUSED(index);
    const auto content = "[Desktop Entry]\nName=Test\nExec=\"" +
                         QCoreApplication::applicationFilePath().toUtf8() +
                         "\" %u\nIcon=" + icon.toUtf8() + "\nComment=" + comment.toUtf8() + "\n";
    write(path, content);
    return QFileInfo::exists(path);
#endif
}
QVariantMap readIconShortcut(const QString &path)
{
#ifdef Q_OS_WIN
    QVariantMap result;
    shortcutFile(path, false, [&](IShellLinkW *link) {
        wchar_t icon[32768]{}, comment[1024]{};
        int index = 0;
        if (FAILED(link->GetIconLocation(icon, 32768, &index)) ||
            FAILED(link->GetDescription(comment, 1024)))
            return false;
        result = {{"icon", QString::fromWCharArray(icon)},
                  {"index", index},
                  {"comment", QString::fromWCharArray(comment)}};
        return true;
    });
    return result;
#else
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QVariantMap result{{"index", 0}};
    for (const auto &line : QString::fromUtf8(file.readAll()).split('\n')) {
        if (line.startsWith("Icon="))
            result["icon"] = line.mid(5);
        if (line.startsWith("Comment="))
            result["comment"] = line.mid(8);
    }
    return result;
#endif
}
EchoPluginContext *installPresentationServices(Host &host, const QString &id)
{
    const auto source = pluginPackage(
        host.temporary.path(), id, "export function activate(c){}",
        {{"capabilities", QJsonArray{"appearance", "appIcons", "localFiles"}},
         {"requires", QJsonObject{{"hostVersion", ">=0.1.0"},
                                  {"sdkVersion", ">=1.0.0 <2.0.0"},
                                  {"features", QJsonArray{"playback.clock", "appearance.overrides",
                                                          "app.icons", "files.audioMetadata"}}}}});
    QImage image(24, 24, QImage::Format_ARGB32);
    image.fill(id.endsWith('b') ? Qt::blue : Qt::red);
    if (!image.save(source + "/icon.png"))
        return nullptr;
    host.runtime.importDirectory(source);
    if (!QTest::qWaitFor(
            [&] {
                return host.runtime.state(host.runtime.installationForId(id)) ==
                       PluginRuntime::Active;
            },
            7000))
        return nullptr;
    return host.runtime.context(host.runtime.installationForId(id));
}
struct Calls
{
    Host &host;
    QList<QJSValue> roots;
    EchoPluginOperation *keep(EchoPluginOperation *op)
    {
        roots << host.engine.newQObject(op);
        return op;
    }
    QVariant success(EchoPluginOperation *op)
    {
        keep(op);
        if (!QTest::qWaitFor([op] { return op->finished(); }, 7000) ||
            op->state() != EchoPluginOperation::Succeeded)
            qFatal("Operation failed: %s %s", qPrintable(op->errorCode()),
                   qPrintable(op->errorMessage()));
        return op->result();
    }
    EchoPluginRegistration *lease(EchoPluginOperation *op)
    {
        return qobject_cast<EchoPluginRegistration *>(success(op).value<QObject *>());
    }
};
void approve(Host &host)
{
    QObject::connect(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested,
                     host.runtime.permissions(), [&host](const QString &id, const QVariantMap &) {
                         host.runtime.permissions()->decide(id, true);
                     });
}
QVariantMap visuals(double main, double card = 1)
{
    return {{"light", QVariantMap{{"surface", QVariantMap{{"main", main}, {"card", card}}}}},
            {"dark", QVariantMap{{"surface", QVariantMap{{"main", main}, {"card", card}}}}}};
}
} // namespace
void PluginRuntimeTest::playbackClockNativeSamplesAndPluginLifetime()
{
    PlaybackClock anchor;
    QVariantMap state{{"trackId", "local"}, {"trackSeq", "1"},     {"sourceRevision", "1"},
                      {"positionMs", 1000}, {"durationMs", 10000}, {"playbackRate", 1.},
                      {"isPlaying", true},  {"isAdvancing", true}, {"displayState", "playing"},
                      {"busyReason", ""}};
    QVERIFY(anchor.observe(state, true));
    QTest::qWait(30);
    const auto beforeRate = anchor.position();
    state["playbackRate"] = 2.;
    QVERIFY(anchor.observe(state, false));
    QVERIFY(anchor.position() >= beforeRate);
    state["isAdvancing"] = false;
    state["displayState"] = "buffering";
    state["busyReason"] = "buffering";
    QVERIFY(anchor.observe(state, false));
    const auto stalled = anchor.position();
    QTest::qWait(20);
    QCOMPARE(anchor.position(), stalled);
    QVERIFY(!anchor.observe(state, false));
    state["positionMs"] = 2000;
    state["isAdvancing"] = true;
    state["displayState"] = "playing";
    state["busyReason"] = "";
    QVERIFY(anchor.observe(state, true));
    QCOMPARE(anchor.snapshot().value("positionMs").toInt(), 2000);
    Host host;
    approve(host);
    Calls calls{host};
    auto *scope = installPresentationServices(host, "presentation-a");
    QVERIFY(scope);
    QSignalSpy events(scope->player(), &EchoPluginPlayer::eventOccurred);
    QByteArray pcm(8000 * 2 * 15, '\0'), bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(pcm.size() + 36);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000)
           << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(pcm.size());
    stream.writeRawData(pcm.constData(), pcm.size());
    const auto path = host.temporary.path() + "/clock.wav";
    write(path, bytes);
    PlaybackTrack track;
    track.id = "clock";
    track.title = "Clock";
    track.localPath = path;
    track.durationMs = 15000;
    host.playback.playTracks({track}, 0, "Clock", "clock");
    QTRY_VERIFY_WITH_TIMEOUT(host.playback.playing() && !host.playback.loading() &&
                                 host.playback.positionMs() > 250,
                             15000);
    auto clock = scope->player()->clock();
    QCOMPARE(clock, host.playback.clock());
    QVERIFY(clock.value("isAdvancing").toBool());
    QVERIFY(clock.value("sampledAt").toLongLong() > 0);
    const auto stamp = clock.value("sampledAt");
    const auto projected = scope->player()->samplePosition();
    QTest::qWait(30);
    QVERIFY(scope->player()->samplePosition() >= projected);
    host.playback.setVolume(44);
    // Unrelated state reads and writes cannot turn an old observation into a fresh tick.
    QCOMPARE(scope->player()->clock().value("sampledAt"), stamp);
    host.playback.pause();
    QTRY_VERIFY(!scope->player()->clock().value("isAdvancing").toBool());
    const auto paused = scope->player()->samplePosition();
    QTest::qWait(40);
    QCOMPARE(scope->player()->samplePosition(), paused);
    const auto generation = scope->player()->clock().value("generation").toULongLong();
    host.playback.seek(3000);
    QCOMPARE(scope->player()->clock().value("displayState").toString(), "seeking");
    QVERIFY(!scope->player()->clock().value("isAdvancing").toBool());
    QVERIFY(scope->player()->clock().value("generation").toULongLong() > generation);
    QTRY_VERIFY(qAbs(scope->player()->positionMs() - 3000) < 500);
    QTRY_COMPARE(scope->player()->clock().value("displayState").toString(), "paused");
    host.playback.setSpeed(1.5);
    QCOMPARE(scope->player()->clock().value("playbackRate").toDouble(), 1.5);
    QObject owner;
    owner.setProperty("active", true);
    auto *presentation = scope->createPresentation(&owner, "test-clock");
    QVERIFY(presentation);
    QCOMPARE(presentation->player()->clock(), scope->player()->clock());
    host.engine.globalObject().setProperty("clockEvents", 0);
    QVERIFY(scope->player()->on("clock-probe", "clock",
                                host.engine.evaluate("(function(){clockEvents++;})"), true));
    QTRY_VERIFY(host.engine.globalObject().property("clockEvents").toInt() > 0);
    auto *oldLease = scope->registration("reentrant", "same");
    QPointer<EchoPluginRegistration> nested;
    connect(oldLease, &EchoPluginRegistration::activeChanged, &host.engine, [&] {
        if (!oldLease->active())
            nested = scope->registration("reentrant", "same");
    });
    auto *outer = scope->registration("reentrant", "same");
    QVERIFY(nested && nested->active());
    QVERIFY(!outer->active());
    nested->dispose();
    auto *player = scope->player();
    QVERIFY(host.api.setAccount(9911, "not-real"));
    QVERIFY(presentation->player()->clock().isEmpty());
    QVERIFY(events.size() > 0);
    Q_UNUSED(player);
    host.playback.stop();
}
void PluginRuntimeTest::temporaryAppearancePriorityLiveBaseAndRevocation()
{
    Host host;
    approve(host);
    Calls calls{host};
    auto *a = installPresentationServices(host, "presentation-a");
    QVERIFY(a);
    auto *b = installPresentationServices(host, "presentation-b");
    QVERIFY(b);
    host.theme.setMode(ThemeStore::Light);
    const auto old = host.theme.main();
    auto definition = visuals(.3, .4);
    auto light = definition.value("light").toMap();
    const QVariantMap transition{{"durationMs", 180},
                                 {"enterX", 16},
                                 {"enterScale", .98},
                                 {"easing", QVariantList{.2, 0, 0, 1}},
                                 {"mode", "parallel"}};
    light["pageTransition"] = transition;
    definition["light"] = light;
    auto *first = calls.lease(a->appearance()->setOverride("glass", definition, 0));
    QVERIFY(first);
    QCOMPARE(host.theme.extensionAppearance().value("pageTransition").toMap(), transition);
    auto *second = calls.lease(b->appearance()->setOverride(
        "glass", {{"light", QVariantMap{{"surface", QVariantMap{{"main", .8}}}}}}, 10));
    QVERIFY(second);
    QVERIFY(qAbs(host.theme.main().alphaF() - .8) < .01);
    QVERIFY(qAbs(host.theme.card().alphaF() - .4) < .01);
    host.router.registerPage(
        {"plugins", "/plugins", QUrl("qrc:/unused.qml"), {}, false, false, false, {}});
    QQuickWindow themeWindow;
    themeWindow.resize(1000, 720);
    QQmlComponent page(&host.engine, QUrl("qrc:/qt/qml/EchoMusic/Themes/ThemeCenterPage.qml"));
    QVERIFY2(page.isReady(), qPrintable(page.errorString()));
    QStringList warnings;
    const auto watch = connect(&host.engine, &QQmlEngine::warnings, &host.engine,
                               [&](const QList<QQmlError> &errors) {
                                   for (const auto &error : errors)
                                       warnings.append(error.toString());
                               });
    QScopedPointer<QObject> view(page.createWithInitialProperties(
        {{"context", QVariant::fromValue(host.router.current())},
         {"router", QVariant::fromValue(&host.router)},
         {"settings", QVariant::fromValue(&host.shellSettings)},
         {"parent", QVariant::fromValue(themeWindow.contentItem())},
         {"width", 1000},
         {"height", 720}}));
    QVERIFY2(view, qPrintable(page.errorString()));
    themeWindow.show();
    auto *root = qobject_cast<QQuickItem *>(view.data());
    QVERIFY(root);
    QTRY_VERIFY(visual(root, "temporaryAppearanceNotice")->isVisible());
    auto *manage = visual(root, "manageTemporaryAppearance");
    QVERIFY(manage);
    QVERIFY(QMetaObject::invokeMethod(manage, "clicked"));
    QCOMPARE(host.router.current()->name(), "plugins");
    view.reset();
    themeWindow.close();
    disconnect(watch);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    host.router.navigate("search");
    const auto selected = host.theme.extensions()->selectedTheme();
    auto *latest = calls.lease(a->appearance()->setOverride("newer", visuals(.6), 10));
    QVERIFY(latest);
    QVERIFY(qAbs(host.theme.main().alphaF() - .6) < .01);
    latest->dispose();
    QVERIFY(qAbs(host.theme.main().alphaF() - .8) < .01);
    host.theme.selectSolid(QColor("#884422"));
    second->dispose();
    QVERIFY(qAbs(host.theme.main().alphaF() - .3) < .01);
    first->dispose();
    QVERIFY(host.theme.extensionAppearance().value("pageTransition").toMap().isEmpty());
    QVERIFY(qAbs(host.theme.main().alphaF() - 1) < .01);
    QVERIFY(host.theme.main() != old);
    QCOMPARE(host.theme.extensions()->selectedTheme(), selected);
    auto *replace = calls.lease(a->appearance()->setOverride("same", visuals(.2)));
    QVERIFY(replace);
    auto *replacement = calls.lease(a->appearance()->setOverride("same", visuals(.7)));
    QVERIFY(replacement);
    QVERIFY(!replace->active());
    replace->dispose();
    QVERIFY(qAbs(host.theme.main().alphaF() - .7) < .01);
    for (const auto &record : host.runtime.permissions()->records()) {
        const auto row = record.toMap();
        if (row.value("installationId") == a->installationId() &&
            row.value("capability") == "appearance")
            QVERIFY(host.runtime.permissions()->revoke(row.value("id").toString()));
    }
    QVERIFY(!replacement->active());
    QCOMPARE(host.theme.temporaryAppearance().size(), 0);
    auto *accountLease = calls.lease(b->appearance()->setOverride("account", visuals(.1)));
    QVERIFY(accountLease);
    QVERIFY(host.api.setAccount(9911, "not-real"));
    QVERIFY(!accountLease->active());
    QVERIFY(host.theme.temporaryAppearance().isEmpty());
    auto *disabled = calls.lease(b->appearance()->setOverride("disable", visuals(.5)));
    QVERIFY(disabled);
    host.runtime.setEnabled(b->installationId(), false);
    QTRY_VERIFY(host.theme.temporaryAppearance().isEmpty());
    bool retired = false;
    const auto retire = connect(&host.theme, &ThemeStore::changed, &host.engine, [&] {
        if (!retired && !host.theme.temporaryAppearance().isEmpty()) {
            retired = true;
            a->beginRetire();
        }
    });
    auto *inPublication =
        calls.keep(a->appearance()->setOverride("retire-in-publish", visuals(.25)));
    QTRY_VERIFY(inPublication->finished());
    QVERIFY(retired);
    QCOMPARE(inPublication->state(), EchoPluginOperation::Canceled);
    QVERIFY(host.theme.temporaryAppearance().isEmpty());
    disconnect(retire);
}
void PluginRuntimeTest::applicationIconsPriorityNativeOwnersAndCancellation()
{
    Host host;
    approve(host);
    Calls calls{host};
    auto *a = installPresentationServices(host, "presentation-a");
    QVERIFY(a);
    auto *b = installPresentationServices(host, "presentation-b");
    QVERIFY(b);
    auto *service = a->host().appIcons;
    QVERIFY(service);
    QQuickWindow window;
    service->attach(&window);
    const auto original = QGuiApplication::windowIcon();
    auto *red =
        calls.lease(a->appIcons()->set("icon", {{"window", "icon.png"}, {"tray", "icon.png"}}, 0));
    QVERIFY(red);
    QCOMPARE(window.icon().pixmap(24).toImage().pixelColor(12, 12), QColor(Qt::red));
    QFile binaryFile(b->package().resolve("icon.png"));
    QVERIFY(binaryFile.open(QIODevice::ReadOnly));
    QJsonArray binaryValues;
    for (unsigned char byte : binaryFile.readAll())
        binaryValues.append(int(byte));
    host.engine.globalObject().setProperty("iconScope", host.engine.newQObject(a));
    auto binaryJs = host.engine.evaluate(
        "iconScope.appIcons.set('bytes',{window:new Uint8Array(" +
        QString::fromUtf8(QJsonDocument(binaryValues).toJson(QJsonDocument::Compact)) +
        ").buffer},20)");
    auto *binaryOp = qobject_cast<EchoPluginOperation *>(binaryJs.toQObject());
    QVERIFY2(binaryOp, qPrintable(binaryJs.toString()));
    auto *binaryLease = calls.lease(binaryOp);
    QVERIFY(binaryLease);
    QCOMPARE(window.icon().pixmap(24).toImage().pixelColor(12, 12), QColor(Qt::blue));
    binaryLease->dispose();
    QCOMPARE(window.icon().pixmap(24).toImage().pixelColor(12, 12), QColor(Qt::red));
    auto *blue = calls.lease(b->appIcons()->setRuntimeWindowIcon("icon.png", 10));
    QVERIFY(blue);
    QCOMPARE(QGuiApplication::windowIcon().pixmap(24).toImage().pixelColor(12, 12),
             QColor(Qt::blue));
    QCOMPARE(service->trayIcon(QIcon()).pixmap(24).toImage().pixelColor(12, 12), QColor(Qt::red));
    QQuickWindow later;
    service->attach(&later);
    QCOMPARE(later.icon().pixmap(24).toImage().pixelColor(12, 12), QColor(Qt::blue));
    blue->dispose();
    QCOMPARE(window.icon().pixmap(24).toImage().pixelColor(12, 12), QColor(Qt::red));
    calls.success(a->appIcons()->restoreDefaultWindowIcon());
    QCOMPARE(QGuiApplication::windowIcon().cacheKey(), original.cacheKey());
    QCOMPARE(later.icon().cacheKey(), original.cacheKey());
    QVERIFY(service->hasTarget("tray"));
    auto *bad = calls.keep(a->appIcons()->set("escape", {{"window", "../elsewhere.png"}}));
    QTRY_VERIFY(bad->finished());
    QCOMPARE(bad->state(), EchoPluginOperation::Failed);
    bool otherPrepared = false;
    service->prepare({{"window", b->package().resolve("icon.png")}},
                     [&](PreparedAppIcons result) { otherPrepared = result.error.isEmpty(); });
    QVERIFY(!service->drained());
    QVERIFY(a->appIcons()->drained()); // Another scope's decode cannot hold this scope open.
    QTRY_VERIFY(otherPrepared);
    QPointer<EchoPluginOperation> pending;
    const auto cancelPreparation =
        connect(service, &AppIconService::drainedChanged, &host.engine, [&] {
            if (pending && !service->drained())
                pending->cancel();
        });
    pending = calls.keep(a->appIcons()->set("cancel", {{"window", "icon.png"}}));
    QTRY_COMPARE(pending->state(), EchoPluginOperation::Canceled);
    QTRY_VERIFY(a->appIcons()->drained());
    disconnect(cancelPreparation);
    QTRY_VERIFY(service->drained());
    QVERIFY(!service->hasTarget("window"));
    auto *account = calls.lease(a->appIcons()->setRuntimeWindowIcon("icon.png"));
    QVERIFY(account);
    QVERIFY(host.api.setAccount(9911, "not-real"));
    QVERIFY(!account->active());
    QVERIFY(!red->active());
    QVERIFY(!service->hasTarget("window"));
    QVERIFY(!service->hasTarget("tray"));
    auto *next = calls.lease(b->appIcons()->setRuntimeWindowIcon("icon.png"));
    QVERIFY(next);
    for (const auto &record : host.runtime.permissions()->records()) {
        const auto row = record.toMap();
        if (row.value("installationId") == b->installationId() &&
            row.value("capability") == "appIcons")
            host.runtime.permissions()->revoke(row.value("id").toString());
    }
    QVERIFY(!next->active());
    QCOMPARE(QGuiApplication::windowIcon().cacheKey(), original.cacheKey());
#ifndef Q_OS_WIN
    auto *unsupported = calls.keep(a->appIcons()->restoreDefaultTaskbarIcon());
    QCOMPARE(unsupported->errorCode(), "Unsupported");
#endif
}
void PluginRuntimeTest::completeAudioMetadataRealFilesAndCancellation()
{
    Host host;
    Calls calls{host};
    auto *scope = installPresentationServices(host, "presentation-a");
    QVERIFY(scope);
    const auto directory = calls.success(scope->files()->privateDirectory()).toMap();
    const auto root = directory.value("file").toMap();
    for (const auto &name :
         {QStringLiteral("plugin-full-tags.flac"), QStringLiteral("plugin-no-tags.flac")}) {
        QFile fixture(QFINDTESTDATA(qPrintable("../../fixtures/audio/" + name)));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        auto ref = root;
        ref["path"] = name;
        calls.success(scope->files()->writeBytes(ref, fixture.readAll()));
        const auto metadata =
            calls.success(scope->files()->metadata(ref)).toMap().value("media").toMap();
        if (name.contains("full")) {
            QCOMPARE(metadata.value("title").toString(), QString::fromUtf8("插件元数据标题"));
            QCOMPARE(metadata.value("artist").toString(), QString::fromUtf8("测试歌手"));
            QCOMPARE(metadata.value("album").toString(), QString::fromUtf8("测试专辑"));
            QCOMPARE(metadata.value("year").toInt(), 2026);
            QCOMPARE(metadata.value("track").toInt(), 3);
            QCOMPARE(metadata.value("disk").toInt(), 2);
            QCOMPARE(metadata.value("genre").toStringList(), (QStringList{"Rock", "Jazz"}));
            QVERIFY(metadata.value("durationMs").toLongLong() >= 900);
        } else {
            for (const auto &key : {"title", "artist", "album", "year", "track", "disk", "genre"})
                QVERIFY(metadata.value(key).isNull());
        }
        auto *canceled = calls.keep(scope->files()->metadata(ref));
        canceled->cancel();
        QCOMPARE(canceled->state(), EchoPluginOperation::Canceled);
    }
    auto ref = root;
    ref["path"] = "plugin-full-tags.flac";
    auto *pending = calls.keep(scope->files()->metadata(ref));
    QVERIFY(host.api.setAccount(9911, "not-real"));
    QTRY_COMPARE(pending->state(), EchoPluginOperation::Canceled);
    auto *old = calls.keep(scope->files()->metadata(ref));
    QTRY_VERIFY(old->finished());
    // Plugin-private files intentionally survive account switches (preferences,
    // caches). Only the in-flight old-epoch delivery is canceled above.
    QCOMPARE(old->state(), EchoPluginOperation::Succeeded);
    auto *other = installPresentationServices(host, "metadata-other");
    QVERIFY(other);
    auto *foreign = calls.keep(other->files()->metadata(ref));
    QTRY_VERIFY(foreign->finished());
    QVERIFY(foreign->state() != EchoPluginOperation::Succeeded);
}
void PluginRuntimeTest::shortcutIconTransactionPreservesCurrentFields()
{
#ifdef Q_OS_WIN
    QSKIP("Linux desktop-entry transaction; Windows shell-link integration requires a Windows "
          "fixture");
#else
    QTemporaryDir temp;
    const auto path = temp.path() + "/test.desktop";
    const auto original =
        "[Desktop Entry]\nName=Test\nExec=\"" + QCoreApplication::applicationFilePath().toUtf8() +
        "\" %u\nIcon=original\nComment=keep\n[Desktop Action Other]\nIcon=other\n";
    write(path, original);
    AppIconShortcuts store;
    store.desktopLocations = {path};
    QVERIFY(store.apply({{"desktop", "/tmp/icon-a.png"}}).value("error").toString().isEmpty());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto bytes = file.readAll();
    file.close();
    QVERIFY(bytes.contains("Icon=/tmp/icon-a.png"));
    QVERIFY(bytes.contains("Icon=other"));
    bytes.replace("Comment=keep", "Comment=edited");
    write(path, bytes);
    QVERIFY(store.apply({}).value("error").toString().isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly));
    bytes = file.readAll();
    file.close();
    QVERIFY(bytes.contains("Icon=original"));
    QVERIFY(bytes.contains("Comment=edited"));
    QVERIFY(store.apply({{"desktop", "/tmp/icon-b.png"}}).value("error").toString().isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly));
    bytes = file.readAll();
    file.close();
    bytes.replace("Icon=/tmp/icon-b.png", "Icon=user-choice");
    write(path, bytes);
    const auto canceled = std::make_shared<std::atomic_bool>(true);
    QVERIFY(store.restoreDefault("desktop", canceled).value("canceled").toBool());
    store.apply({});
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("Icon=user-choice"));
#endif
}
void PluginRuntimeTest::shortcutIconRecoveryAcrossSessions_data()
{
    QTest::addColumn<QString>("target");
    QTest::newRow("desktop") << QString("desktop");
#ifdef Q_OS_WIN
    QTest::newRow("taskbar") << QString("taskbar");
#endif
}
void PluginRuntimeTest::shortcutIconRecoveryAcrossSessions()
{
    QFETCH(QString, target);
    QTemporaryDir temp;
#ifdef Q_OS_WIN
    const auto path = temp.filePath("application.lnk");
    const int originalIndex = 7;
#else
    const auto path = temp.filePath("application.desktop");
    const int originalIndex = 0;
#endif
    const auto journal = temp.filePath("state/shortcuts.json");
    const auto first = temp.filePath("first.png"), second = temp.filePath("second.png");
    QVERIFY(writeIconShortcut(path, "original", originalIndex, "keep"));
    {
        AppIconShortcuts store(journal);
        store.desktopLocations = store.taskbarLocations = {path};
        QVERIFY(store.apply({{target, first}}).value("error").toString().isEmpty());
    }
    QCOMPARE(readIconShortcut(path).value("icon").toString(), first);
    {
        // A new worker restores the original fields, not the previous plugin
        // image it encounters when the next process republishes its icon.
        AppIconShortcuts store(journal);
        store.desktopLocations = store.taskbarLocations = {path};
        QString error;
        QVERIFY(store.protectedPaths(error).contains(first));
        QVERIFY(error.isEmpty());
        QVERIFY(store.apply({{target, second}}).value("error").toString().isEmpty());
        QVERIFY(store.apply({}).value("error").toString().isEmpty());
    }
    QCOMPARE(readIconShortcut(path).value("icon").toString(), QString("original"));
    QCOMPARE(readIconShortcut(path).value("index").toInt(), originalIndex);
    QCOMPARE(readIconShortcut(path).value("comment").toString(), QString("keep"));
    {
        AppIconShortcuts store(journal);
        store.desktopLocations = store.taskbarLocations = {path};
        QVERIFY(store.apply({{target, first}}).value("error").toString().isEmpty());
    }
    QVERIFY(writeIconShortcut(path, "user-choice", originalIndex, "edited"));
    {
        AppIconShortcuts store(journal);
        QVERIFY(store.apply({}).value("error").toString().isEmpty());
    }
    QCOMPARE(readIconShortcut(path).value("icon").toString(), QString("user-choice"));
    QCOMPARE(readIconShortcut(path).value("comment").toString(), QString("edited"));
    // An unreadable/unwritable journal must fail before external icon writes.
    write(temp.filePath("blocked"), "not a directory");
    AppIconShortcuts blocked(temp.filePath("blocked/shortcuts.json"));
    blocked.desktopLocations = blocked.taskbarLocations = {path};
    QVERIFY(!blocked.apply({{target, second}}).value("error").toString().isEmpty());
    QCOMPARE(readIconShortcut(path).value("icon").toString(), QString("user-choice"));
    write(journal, "invalid journal");
    AppIconShortcuts corrupt(journal);
    corrupt.desktopLocations = corrupt.taskbarLocations = {path};
    QVERIFY(!corrupt.apply({{target, second}}).value("error").toString().isEmpty());
    QCOMPARE(readIconShortcut(path).value("icon").toString(), QString("user-choice"));
}
void PluginRuntimeTest::applicationIconShutdownKeepsShortcutWinners()
{
    Host host;
    approve(host);
    Calls calls{host};
    auto *scope = installPresentationServices(host, "icon-shutdown");
    QVERIFY(scope);
    auto *service = scope->host().appIcons;
    QQuickWindow window;
    service->attach(&window);
    const auto original = QGuiApplication::windowIcon();
    QVERIFY(calls.lease(
        scope->appIcons()->set("runtime", {{"window", "icon.png"}, {"tray", "icon.png"}})));
    PreparedAppIcons first;
    first.images["desktop"] = QImage(16, 16, QImage::Format_ARGB32);
    first.images["desktop"].fill(Qt::red);
    first.paths["desktop"] = host.temporary.filePath("first.png");
    QVERIFY(service->put("first", first, 0, {}));
    auto second = first;
    second.paths["desktop"] = host.temporary.filePath("second.png");
    QVERIFY(service->put("second", second, 10, {}));
    const auto winners = AppIconServiceTestAccess::desired(*service);
    const auto revision = service->shortcutRevision();
    QCOMPARE(winners.value("desktop"), second.paths.value("desktop"));
    QSignalSpy closed(&host.runtime, &PluginRuntime::closed);
    host.runtime.shutdown();
    service->remove("second");
    service->remove("first");
    service->refresh();
    QVERIFY(!service->put("late", first, 0, {}));
    QCOMPARE(AppIconServiceTestAccess::desired(*service), winners);
    QCOMPARE(service->shortcutRevision(), revision);
    QTRY_VERIFY(!closed.isEmpty());
    QTRY_VERIFY(service->drained());
    QVERIFY(!service->hasTarget("window"));
    QVERIFY(!service->hasTarget("tray"));
    QCOMPARE(window.icon().cacheKey(), original.cacheKey());
    QCOMPARE(service->completedShortcutRevision(), revision);
}
void PluginRuntimeTest::applicationIconStartupRestoresUnclaimedShortcuts()
{
    QString shortcut;
    Host host([&](const QString &root) {
        const auto directory = QFileInfo(root).absolutePath();
#ifdef Q_OS_WIN
        shortcut = directory + "/application.lnk";
#else
        shortcut = directory + "/application.desktop";
#endif
        QVERIFY(writeIconShortcut(shortcut, "last-plugin", 0, "keep"));
        const auto state = directory + "/app-icons";
        QVERIFY(QDir().mkpath(state));
        const QJsonObject record{{"icon", "original"}, {"applied", "last-plugin"}, {"index", 0}};
        write(state + "/plugin-icon-shortcuts.json",
              QJsonDocument(
                  QJsonObject{{"version", 1}, {"shortcuts", QJsonObject{{shortcut, record}}}})
                  .toJson());
    });
    // No plugin republishes the prior process's icon. The real startup barrier
    // withdraws it after catalog/activation work has settled, without touching
    // any shortcuts or journal outside this host's isolated settings directory.
    QTRY_COMPARE(readIconShortcut(shortcut).value("icon").toString(), QString("original"));
    QCOMPARE(readIconShortcut(shortcut).value("comment").toString(), QString("keep"));
}
