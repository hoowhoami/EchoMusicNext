#include "AccountUiFixture.h"
#include "AccentPalette.h"
#include "NativeWindow.h"
#include "EchoColorValue.h"
#include "EchoPageScrollMetrics.h"
#include "RouteController.h"
#include "SearchRouteCoordinator.h"
#include "SearchModel.h"
#include "ShellSettings.h"
#include "ShellController.h"
#include "ThemeStore.h"
#include "ThemeImageStore.h"
#include "PlaylistDetailService.h"
#include "PlaybackService.h"
#include <QScopeGuard>
#include <QPainter>
#include <QImageReader>
#include <QColorSpace>
#include "ToastService.h"
#include "UiTypography.h"
#include "WindowController.h"
#include "WindowBackdrop.h"
#include "HyprlandWindowBackdrop.h"
#include <QSGRendererInterface>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQmlIncubationController>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#ifdef Q_OS_WIN
#include "WindowsWindowBackdrop.h"
#include <QOperatingSystemVersion>
#include <dwmapi.h>
#include <thread>
#include <atomic>
#endif
#ifdef Q_OS_MACOS
QVariantMap nativeWindowAppearance(QWindow *window);
QVariantList nativeSystemCaptionFrames(QWindow *window);
#endif
using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static void artwork(PlaybackService &service, const QString &source)
    {
        service.m_track.cover = source;
        service.m_track.title = QStringLiteral("Transparent playback material");
        emit service.changed();
    }
};
} // namespace EchoMusic
namespace
{
QQuickItem *visual(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = visual(child, name))
            return found;
    return nullptr;
}
struct ThemeSession
{
    QTemporaryDir directory;
    QSettings preferences{directory.filePath("theme.ini"), QSettings::IniFormat};
    ThemeStore theme{preferences};
    ShellSettings shell{preferences};
    WindowController windows{preferences, false};
    ApiClient api{DeviceIdentity::temporary()};
    Test::AccountUiFixture accountUi{api};
    SearchModel search{api};
    RouteController routes;
    ShellController shellController{preferences, routes};
    SearchRouteCoordinator searchRoutes{search, routes, false};
    QQmlEngine engine;
    QScopedPointer<QObject> root;
    QQuickWindow *window = nullptr;
    bool visibleBeforeAttach = false, firstShowReady = false;
    explicit ThemeSession(bool nativeWindow = false) : windows(preferences, nativeWindow) {}
    bool create()
    {
        shellController.attachTheme(theme);
        routes.registerPage({"themes",
                             "/main/themes",
                             QUrl("qrc:/qt/qml/EchoMusic/Themes/ThemeCenterPage.qml"),
                             {},
                             false,
                             false});
        routes.navigate("themes");
        theme.attachToEngine(engine);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/App/Main.qml"));
        root.reset(component.createWithInitialProperties(
            {{"libraryController", QVariant::fromValue(&accountUi.library)},
             {"accountController", QVariant::fromValue(&accountUi.controller)},
             {"shellController", QVariant::fromValue(&shellController)},
             {"searchService", QVariant::fromValue(&search)},
             {"router", QVariant::fromValue(&routes)},
             {"shellSettings", QVariant::fromValue(&shell)},
             {"windowService", QVariant::fromValue(&windows)}}));
        if (!root) {
            qWarning().noquote() << component.errorString();
            return false;
        }
        window = qobject_cast<QQuickWindow *>(root.data());
        visibleBeforeAttach = window && window->isVisible();
        if (window)
            QObject::connect(window, &QWindow::visibleChanged, &windows, [this] {
                if (window->isVisible())
                    firstShowReady = windows.presentationReady();
            });
        windows.attach(window);
        return window;
    }
    QQuickItem *item(const QString &name)
    {
        return visual(window->contentItem(), name);
    }
    ~ThemeSession()
    {
        routes.shutdown();
        windows.shutdown();
        search.shutdown();
        api.shutdown();
        if (window)
            window->close();
        // Asynchronous Loaders/incubations must settle before the engine dies:
        // QML items deleted through posted events after engine destruction
        // crash in QQmlIncubator::clear (upstream issue #13). Drain a bounded
        // number of rounds, destroy the scene while the engine can still
        // service it, then drain the deferred teardown events it queues.
        for (int round = 0; round < 8; ++round) {
            QCoreApplication::processEvents(QEventLoop::AllEvents);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        root.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
};
void click(QQuickWindow *window, QQuickItem *item)
{
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
}
void render(QQuickWindow *window)
{
    QSignalSpy frames(window, &QQuickWindow::frameSwapped);
    window->update();
    QTRY_VERIFY(!frames.isEmpty());
}
} // namespace
class ThemeTest : public QObject
{
    Q_OBJECT
private slots:
    void nativeWindowCornersRemainClearDuringResize()
    {
#ifndef Q_OS_MACOS
        QSKIP("Window server corner capture requires macOS");
#else
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Native window capture requires Cocoa");
        ThemeSession session(true);
        QVERIFY(session.create());
        QVERIFY(Test::prepareNativeInput(session.window));
        session.theme.setMotionEnabled(false);
        // Application.cpp owns this connection in production; ThemeSession
        // constructs Main directly, so exercise that native bridge explicitly.
        const auto applyAppearance = [&] {
            session.windows.setAppearance(session.theme.windowFrosted(), session.theme.dark(),
                                          session.theme.transparency());
        };
        QObject::connect(&session.theme, &ThemeStore::changed, &session.windows, applyAppearance);
        applyAppearance();
        const auto captures =
            qEnvironmentVariable("ECHO_WINDOW_CAPTURES", session.directory.path());
        QVERIFY(QDir().mkpath(captures));
        int count = 0;
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            session.theme.setMode(mode);
            for (const bool zoomed : {false, true}) {
                session.shell.resetZoom();
                if (zoomed)
                    for (int i = 0; i < 5; ++i)
                        session.shell.zoomIn();
                for (const int variant : {0, 1, 2, 3}) {
                    session.theme.setTransparency(variant == 1 ? 60 : variant == 2 ? 100 : 0);
                    session.theme.setWindowFrosted(variant == 3);
                    for (const auto size : {QSize(1100, 650), QSize(1260, 790), QSize(1150, 750)}) {
                        if (variant == 3 && size == QSize(1150, 750)) {
                            session.window->showMinimized();
                            QTRY_COMPARE(session.window->visibility(), QWindow::Minimized);
                            session.window->showNormal();
                            QVERIFY(Test::prepareNativeInput(session.window));
                            session.windows.toggleFullscreen();
                            QTRY_COMPARE_WITH_TIMEOUT(nativeWindowAppearance(session.window)
                                                          .value("fullscreenSettled")
                                                          .toString(),
                                                      QString("full"), 10000);
                            session.windows.toggleFullscreen();
                            QTRY_COMPARE_WITH_TIMEOUT(nativeWindowAppearance(session.window)
                                                          .value("fullscreenSettled")
                                                          .toString(),
                                                      QString("normal"), 10000);
                            QVERIFY(Test::prepareNativeInput(session.window));
                        }
                        session.window->resize(size);
                        render(session.window);
                        const auto appearance = nativeWindowAppearance(session.window);
                        QCOMPARE(appearance.value("applicationBackdropCount").toInt(),
                                 variant == 3 ? 1 : 0);
                        QCOMPARE(appearance.value("backgroundAlpha").toReal(),
                                 variant == 0 ? 1. : 0.);
                        const auto path = captures + QString("/corner-%1-%2-%3-%4.png")
                                                         .arg(int(mode))
                                                         .arg(zoomed)
                                                         .arg(variant)
                                                         .arg(++count);
                        QProcess capture;
                        capture.start("/usr/sbin/screencapture",
                                      {"-x", "-o", "-l",
                                       nativeWindowAppearance(session.window)
                                           .value("windowNumber")
                                           .toString(),
                                       path});
                        QVERIFY(capture.waitForFinished(5000));
                        if (count == 1 && capture.exitCode() != 0)
                            QSKIP(qPrintable("Native window capture unavailable: " +
                                             QString::fromUtf8(capture.readAllStandardError())));
                        QCOMPARE(capture.exitCode(), 0);
                        const QImage image(path);
                        QVERIFY(!image.isNull());
                        for (const auto p : {QPoint(1, 1), QPoint(image.width() - 2, 1),
                                             QPoint(1, image.height() - 2),
                                             QPoint(image.width() - 2, image.height() - 2)}) {
                            const auto pixel = image.pixelColor(p);
                            QVERIFY2(pixel.alpha() < 8,
                                     qPrintable(QString("%1 native corner %2,%3 = %4 alpha %5")
                                                    .arg(path)
                                                    .arg(p.x())
                                                    .arg(p.y())
                                                    .arg(pixel.name())
                                                    .arg(pixel.alpha())));
                        }
                    }
                }
            }
        }
        qInfo() << "Window server captures with clear corners" << count;
#endif
    }
    void nativeCaptionStaysInPlaceDuringResize()
    {
#ifndef Q_OS_MACOS
        QSKIP("AppKit resize requires macOS");
#else
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Native caption test requires Cocoa");
        ThemeSession session(true);
        QVERIFY(session.create());
        QVERIFY(Test::prepareNativeInput(session.window));
        const auto baseline = nativeWindowAppearance(session.window).value("buttonFrames");
        const auto aligned = [&] {
            return nativeWindowAppearance(session.window).value("buttonFrames") == baseline;
        };
        int displaced = 0;
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            session.theme.setMode(mode);
            for (const bool zoomed : {false, true}) {
                session.shell.resetZoom();
                if (zoomed)
                    for (int i = 0; i < 5; ++i)
                        session.shell.zoomIn();
                QTRY_VERIFY(aligned());
                for (int step = 0; step < 40; ++step) {
                    session.window->resize(1100 + (step % 10) * 10, 650 + (step % 10) * 8);
                    if (!aligned()) {
                        ++displaced;
                        if (displaced == 1)
                            qInfo() << "Immediate resize caption"
                                    << nativeWindowAppearance(session.window).value("buttonFrames");
                    }
                    QTest::qWait(16);
                }
            }
        }
        qInfo() << "Resize samples displaced" << displaced << "/ 160";
        QCOMPARE(displaced, 0);
#endif
    }
    void independentPanelBackgroundsClipAndRelease()
    {
        ThemeSession session;
        QVERIFY(session.create());
        QVERIFY(Test::prepareNativeInput(session.window));
        if (session.window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
            QSKIP("Panel corner pixels require native RHI");
        session.theme.setMotionEnabled(false);
        session.window->resize(1100, 800);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            session.theme.setMode(mode);
            for (const qreal scale : {1., 1.25}) {
                session.shell.resetZoom();
                for (int step = 0; step < qRound((scale - 1) * 20); ++step)
                    session.shell.zoomIn();
                QCOMPARE(session.shell.uiScale(), scale);
                for (const bool frost : {false, true}) {
                    session.theme.setFloatingFrosted(frost);
                    for (const auto *name : {"settingsDialogOwner", "themeAppearanceDrawerOwner"}) {
                        QObject *owner = nullptr;
                        const auto findOwner = [&](auto &&self, QQuickItem *item) -> QObject * {
                            if (auto *found = item->findChild<QObject *>(name))
                                return found;
                            for (auto *child : item->childItems())
                                if (auto *found = self(self, child))
                                    return found;
                            return nullptr;
                        };
                        QTRY_VERIFY2((owner = findOwner(findOwner, session.window->contentItem())),
                                     name);
                        QVERIFY(!owner->property("item").value<QObject *>());
                        QVERIFY(QMetaObject::invokeMethod(owner, "open",
                                                          Q_ARG(QVariant, QVariantMap{})));
                        QPointer<QObject> popup = owner->property("item").value<QObject *>();
                        QVERIFY(popup);
                        QTRY_VERIFY(popup->property("opened").toBool());
                        QPointer<QQuickItem> mask;
                        QTRY_VERIFY(
                            (mask = popup->findChild<QQuickItem *>("popupContentRoundedMask")));
                        auto *surface = popup->property("background").value<QQuickItem *>();
                        auto *content = popup->property("contentItem").value<QQuickItem *>();
                        QVERIFY(surface && content);
                        QCOMPARE(QQmlProperty(surface, "border.width").read().toReal(), 1.);
                        QCOMPARE(surface->property("frosted").toBool(), frost);
                        if (frost)
                            QTRY_VERIFY(surface->findChild<QQuickItem *>("floatingBackdropBlur"));
                        for (auto *item : content->findChildren<QQuickItem *>()) {
                            if (item->metaObject()->indexOfProperty("contentY") >= 0)
                                item->setProperty(
                                    "contentY", qMax(0., item->property("contentHeight").toReal() -
                                                             item->height()));
                        }
                        QTest::qWait(100);
                        const auto filled = session.window->grabWindow();
                        content->setOpacity(0);
                        QTest::qWait(70);
                        const auto empty = session.window->grabWindow();
                        content->setOpacity(1);
                        QVERIFY(!filled.isNull() && !empty.isNull());
                        for (const auto p : {QPointF(1, 1), QPointF(surface->width() - 2, 1),
                                             QPointF(1, surface->height() - 2),
                                             QPointF(surface->width() - 2, surface->height() - 2),
                                             QPointF(.1, surface->height() / 2)}) {
                            const auto scene = surface->mapToScene(p) * filled.devicePixelRatio();
                            const auto a = filled.pixelColor(qFloor(scene.x()), qFloor(scene.y()));
                            const auto b = empty.pixelColor(qFloor(scene.x()), qFloor(scene.y()));
                            QVERIFY2(qAbs(a.red() - b.red()) < 4 &&
                                         qAbs(a.green() - b.green()) < 4 &&
                                         qAbs(a.blue() - b.blue()) < 4,
                                     qPrintable(QString("%1 content covers corner/border at %2,%3")
                                                    .arg(name)
                                                    .arg(p.x())
                                                    .arg(p.y())));
                        }
                        const auto captures = qEnvironmentVariable("ECHO_PANEL_CAPTURES");
                        if (!captures.isEmpty()) {
                            QDir().mkpath(captures);
                            QVERIFY(filled.save(captures + QString("/%1-%2-%3-%4.png")
                                                               .arg(name)
                                                               .arg(int(mode))
                                                               .arg(scale)
                                                               .arg(frost)));
                        }
                        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
                        QTRY_VERIFY(popup.isNull() && mask.isNull());
                    }
                }
            }
        }
    }
    void statelessControlSingletonsAreOwnedByEachEngine()
    {
        QPointer<EchoColorValue> color;
        QPointer<EchoPageScrollMetrics> metrics;
        {
            QQmlEngine first, second;
            color =
                first.singletonInstance<EchoColorValue *>("EchoMusic.Controls", "EchoColorValue");
            metrics = first.singletonInstance<EchoPageScrollMetrics *>("EchoMusic.Controls",
                                                                       "EchoPageScrollMetrics");
            QVERIFY(color);
            QVERIFY(metrics);
            QCOMPARE(color->parent(), &first);
            QCOMPARE(metrics->parent(), &first);
            QCOMPARE(
                first.singletonInstance<EchoColorValue *>("EchoMusic.Controls", "EchoColorValue"),
                color.data());
            auto *otherColor =
                second.singletonInstance<EchoColorValue *>("EchoMusic.Controls", "EchoColorValue");
            auto *otherMetrics = second.singletonInstance<EchoPageScrollMetrics *>(
                "EchoMusic.Controls", "EchoPageScrollMetrics");
            QVERIFY(otherColor && otherColor != color);
            QVERIFY(otherMetrics && otherMetrics != metrics);
            QCOMPARE(otherColor->parent(), &second);
            QCOMPARE(otherMetrics->parent(), &second);
            QCOMPARE(color->normalize("#abc"), QString("#aabbcc"));
            QVERIFY(metrics->wheelStep() > 0);
        }
        QVERIFY(color.isNull());
        QVERIFY(metrics.isNull());
    }
    void engineBindingsAreIsolatedAndBorrowTheStore()
    {
        QTemporaryDir directory;
        QSettings firstSettings(directory.filePath("first.ini"), QSettings::IniFormat);
        QSettings secondSettings(directory.filePath("second.ini"), QSettings::IniFormat);
        ThemeStore first(firstSettings), second(secondSettings);
        QPointer<ThemeStore> retained(&first);
        {
            QQmlEngine firstEngine, secondEngine;
            first.attachToEngine(firstEngine);
            first.attachToEngine(firstEngine); // Idempotent, before or after singleton use.
            second.attachToEngine(secondEngine);
            QCOMPARE(firstEngine.singletonInstance<ThemeStore *>("EchoMusic.Theme", "ThemeStore"),
                     &first);
            QCOMPARE(secondEngine.singletonInstance<ThemeStore *>("EchoMusic.Theme", "ThemeStore"),
                     &second);
            first.setMode(ThemeStore::Dark);
            second.setMode(ThemeStore::Light);
            QVERIFY(first.dark());
            QVERIFY(!second.dark());
            first.attachToEngine(firstEngine);
        }
        QVERIFY(retained); // Engine teardown never destroys application preferences.
    }
    void missingOrExpiredEngineBindingFailsClosed()
    {
        QQmlEngine engine;
        QVERIFY(!ThemeStore::create(&engine, &engine));
        QVERIFY(engine.hasError());
        QVERIFY(engine.catchError().toString().contains("attached before loading QML"));
        QTemporaryDir directory;
        QSettings settings(directory.filePath("theme.ini"), QSettings::IniFormat);
        {
            ThemeStore store(settings);
            store.attachToEngine(engine);
        }
        QVERIFY(!ThemeStore::create(&engine, &engine));
        QVERIFY(engine.hasError());
        engine.catchError();
    }
    void managedImagesNormalizeAndCropWithoutTrustingExtensions()
    {
        QTemporaryDir directory;
        QImage image(3000, 1000, QImage::Format_RGB32);
        image.fill(Qt::red);
        QPainter painter(&image);
        painter.fillRect(QRect(1500, 0, 1500, 1000), Qt::blue);
        painter.end();
        const auto source = directory.filePath("image.dat");
        QVERIFY(image.save(source, "PNG"));
        const auto result = ThemeImageStore::importImage(source, directory.filePath("managed"));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(ThemeImageStore::validId(result.id));
        QCOMPARE(result.size.width(), 2560);
        QVERIFY(result.size.height() <= 2560);
        QCOMPARE(ThemeImageStore::sample(directory.filePath("managed"), result.id,
                                         QRectF(0, 0, 1200, 800)),
                 QColor(Qt::red));
        QCOMPARE(ThemeImageStore::sample(directory.filePath("managed"), result.id,
                                         QRectF(1400, 0, 1160, 800)),
                 QColor(Qt::blue));
        QFile invalid(directory.filePath("fake.png"));
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("not an image");
        invalid.close();
        QVERIFY(!ThemeImageStore::importImage(invalid.fileName(), directory.filePath("managed"))
                     .error.isEmpty());
        QVERIFY(!ThemeImageStore::validId("../../image.png"));
    }
    void largePngDoesNotRaiseTheGuiDecoderAllocationLimit()
    {
        QTemporaryDir directory;
        const auto allocation = QImageReader::allocationLimit();
        QImage image(8192, 6144, QImage::Format_RGB32);
        image.fill(QColor("#3a70aa"));
        const auto source = directory.filePath("large.png");
        QVERIFY(image.save(source));
        image = {}; // The caller must not retain the huge original during import.
        const auto result = ThemeImageStore::importImage(source, directory.filePath("managed"));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.size, QSize(2560, 1920));
        QCOMPARE(result.sample, QColor("#3a70aa"));
        QCOMPARE(QImageReader::allocationLimit(), allocation);
        QCOMPARE(QDir(directory.filePath("managed"))
                     .entryList({".import-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
                     .size(),
                 0);
    }
    void backgroundPreviewDoesNotPersistAndLateImportCannotReviveIt()
    {
        QTemporaryDir directory;
        QSettings preferences(directory.filePath("preferences.ini"), QSettings::IniFormat);
        const auto imageDirectory = directory.filePath("images");
        ThemeStore theme(preferences, nullptr, imageDirectory);
        theme.selectSolid(QColor("#abcdef"));
        QImage image(300, 200, QImage::Format_RGB32);
        image.fill(QColor("#3a70aa"));
        const auto file = directory.filePath("source.png");
        QVERIFY(image.save(file));
        theme.importBackground(QUrl::fromLocalFile(file));
        QTRY_VERIFY(!theme.imageImporting());
        QVERIFY(theme.imageError().isEmpty());
        QVERIFY(theme.backgroundPreview());
        QCOMPARE(theme.themeKey(), QString("custom"));
        theme.setBackgroundCrop(QRectF(25, 30, 100, 120));
        theme.setBackgroundText(QColor(Qt::black));
        theme.setPanelOpacity(29); // General preferences persist independently.
        {
            ThemeStore reopened(preferences, nullptr, imageDirectory);
            QCOMPARE(reopened.themeKey(), QString("solid"));
            QCOMPARE(reopened.seedColor(), QColor("#abcdef"));
            QCOMPARE(reopened.panelOpacity(), 29);
            QVERIFY(reopened.backgroundImage().isEmpty());
        }
        const auto floatingText = theme.floatingText();
        theme.setBackgroundText(QColor("#ff0000"));
        QCOMPARE(theme.floatingText(), floatingText);
        const auto beforeCrop = theme.backgroundCrop();
        theme.setBackgroundCrop(QRectF(-5, 0, 100, 100));
        QCOMPARE(theme.backgroundCrop(), beforeCrop);
        QVERIFY(theme.applyBackground());
        QVERIFY(!theme.backgroundPreview());
        const auto retained = theme.backgroundImage();
        {
            ThemeStore reopened(preferences, nullptr, imageDirectory);
            QCOMPARE(reopened.themeKey(), QString("custom"));
            QCOMPARE(reopened.backgroundImage(), retained);
            QCOMPARE(reopened.backgroundCrop(), beforeCrop);
            QCOMPARE(reopened.backgroundText(), QColor("#ff0000"));
        }
        theme.selectEcho();
        theme.previewBackground();
        QVERIFY(theme.backgroundPreview());
        theme.setBackgroundShade(70);
        theme.cancelBackground(true);
        QCOMPARE(theme.backgroundShade(), 0);
        QCOMPARE(theme.themeKey(), QString("custom"));
        QVERIFY(theme.backgroundPreview());
        theme.endBackgroundPreview();
        QCOMPARE(theme.themeKey(), QString("echo"));
        theme.importBackground(QUrl::fromLocalFile(file));
        theme.endBackgroundPreview();
        QTest::qWait(200);
        QCOMPARE(theme.themeKey(), QString("echo"));
        QVERIFY(!theme.imageImporting());
        QCOMPARE(theme.backgroundImage(), retained);
        QVERIFY(QFile::exists(retained.toLocalFile()));
    }
    void managedImagesConvertTaggedColorAndPruneOnlyUnappliedAssets()
    {
        QTemporaryDir directory;
        const auto managed = directory.filePath("managed");
        QImage image(200, 100, QImage::Format_RGB32);
        image.fill(QColor("#ba6030"));
        image.setColorSpace(QColorSpace::DisplayP3);
        const auto expected = image.convertedToColorSpace(QColorSpace::SRgb).pixelColor(0, 0);
        const auto source = directory.filePath("tagged.png");
        QVERIFY(image.save(source));
        const auto normalized = ThemeImageStore::importImage(source, managed);
        QVERIFY2(normalized.error.isEmpty(), qPrintable(normalized.error));
        QCOMPARE(normalized.sample, expected);
        QSettings preferences(directory.filePath("preferences.ini"), QSettings::IniFormat);
        {
            ThemeStore theme(preferences, nullptr, managed);
            theme.importBackground(QUrl::fromLocalFile(source));
            QTRY_VERIFY(!theme.imageImporting());
            QVERIFY(theme.applyBackground());
        }
        const auto retained = QDir(managed).filePath(normalized.id);
        const auto orphan = QDir(managed).filePath(QString(64, 'a') + ".png");
        const auto staging = QDir(managed).filePath(".import-crash");
        QVERIFY(QDir().mkpath(staging));
        QFile pending(QDir(staging).filePath("partial"));
        QVERIFY(pending.open(QIODevice::WriteOnly));
        pending.write("incomplete");
        pending.close();
        QFile extra(orphan);
        QVERIFY(extra.open(QIODevice::WriteOnly));
        extra.write("unapplied");
        extra.close();
        ThemeStore reopened(preferences, nullptr, managed);
        reopened.pruneImageAssets();
        QTRY_VERIFY(!QFile::exists(orphan));
        QTRY_VERIFY(!QDir(staging).exists());
        QVERIFY(QFile::exists(retained));
        QSignalSpy unavailable(&reopened, &ThemeStore::backgroundUnavailable);
        QVERIFY(QFile::remove(retained));
        // A crop change schedules a new authoritative sample; stale completions
        // from the earlier asset must not change the restored theme.
        reopened.setBackgroundCrop(QRectF(0, 0, 100, 100));
        QTRY_COMPARE(unavailable.count(), 1);
        QVERIFY(!reopened.imageError().isEmpty());
        reopened.reportBackgroundUnavailable(QUrl::fromLocalFile(source));
        reopened.reportBackgroundUnavailable(reopened.backgroundImage());
        QCOMPARE(unavailable.count(), 1);
    }
    void compositorUpdatesCoalesceAndDetachCancels()
    {
#ifdef Q_OS_WIN
        QSKIP("The controlled hyprctl process fixture requires Unix executable scripts");
#endif
        QTemporaryDir directory;
        const auto program = directory.filePath("hyprctl");
        QFile executable(program);
        QVERIFY(executable.open(QIODevice::WriteOnly));
        executable.write(R"(#!/usr/bin/env python3
import json, os, sys, time
from pathlib import Path
with Path(__file__).with_suffix('.log').open('a') as log:
    log.write(json.dumps(sys.argv[1:]) + '\n')
if sys.argv[1] == 'clients':
    print('compositor diagnostic', file=sys.stderr)
    time.sleep(.1)
    print(json.dumps([{'pid': os.getppid(), 'address': '0xbeef', 'title': 'Test'}]))
elif sys.argv[1] == 'dispatch':
    sys.exit(1)
)");
        executable.close();
        QVERIFY(executable.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const auto originalPath = qgetenv("PATH");
        const auto restorePath = qScopeGuard([originalPath] { qputenv("PATH", originalPath); });
        qputenv("PATH",
                directory.path().toUtf8() + QDir::listSeparator().toLatin1() + originalPath);
        QQuickWindow window;
        window.setTitle("Test");
        HyprlandWindowBackdrop compositor;
        compositor.attach(&window);
        QSignalSpy completed(&compositor, &HyprlandWindowBackdrop::completed);
        compositor.apply(true);
        compositor.apply(false);
        QTRY_COMPARE(completed.size(), 2);
        for (const auto &entry : completed)
            QVERIFY(entry.first().toString().isEmpty());
        QFile log(directory.filePath("hyprctl.log"));
        QVERIFY(log.open(QIODevice::ReadOnly));
        const auto calls = log.readAll().trimmed().split('\n');
        QCOMPARE(calls.size(), 6); // Each registered client, once per latest state.
        QCOMPARE(QJsonDocument::fromJson(calls[2]).array(),
                 QJsonArray({"setprop", "address:0xbeef", "noblur", "0"}));
        QCOMPARE(QJsonDocument::fromJson(calls[5]).array(),
                 QJsonArray({"setprop", "address:0xbeef", "noblur", "1"}));
        compositor.apply(true);
        compositor.attach(nullptr);
        QTest::qWait(200);
        QCOMPARE(completed.size(), 2);
    }
    void compositorAddressDoesNotTouchAnotherWindow()
    {
        const QByteArray clients =
            R"([{"pid":42,"address":"0xabc","title":"Main"},{"pid":42,"address":"0xdef","title":"Plugin"},{"pid":9,"address":"0x123","title":"Main"}])";
        QCOMPARE(HyprlandWindowBackdrop::windowAddress(clients, 42, "Main"), QString("0xabc"));
        QCOMPARE(HyprlandWindowBackdrop::windowAddress(clients, 42, "Plugin"), QString("0xdef"));
        QVERIFY(HyprlandWindowBackdrop::windowAddress(clients, 42, "Other").isEmpty());
        QVERIFY(HyprlandWindowBackdrop::windowAddress("{}", 42, "Main").isEmpty());
        QVERIFY(HyprlandWindowBackdrop::windowAddress(
                    R"([{"pid":42,"address":"0xabc;exec","title":"Main"}])", 42, "Main")
                    .isEmpty());
        QCOMPARE(HyprlandWindowBackdrop::windowAddress(clients, 9, "Other"), QString("0x123"));
    }
    void nativeCaptionKeepsSystemDefaultButtons()
    {
#ifndef Q_OS_MACOS
        QSKIP("AppKit caption positioning requires macOS");
#else
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Native caption test requires Cocoa");
        ThemeSession session;
        QVERIFY(session.create());
        QVERIFY(Test::prepareNativeInput(session.window));
        const auto baseline = nativeWindowAppearance(session.window);
        const auto systemFrames = nativeSystemCaptionFrames(session.window);
        QCOMPARE(systemFrames.size(), 3);
        qInfo() << "System caption reference (points):" << systemFrames;
        const auto handle = session.window->winId();
        const auto verify = [&] {
            const auto current = nativeWindowAppearance(session.window);
            const auto frames = current.value("buttonFrames").toList();
            if (frames != systemFrames)
                return false;
            return current.value("nativeWindow") == baseline.value("nativeWindow") &&
                   current.value("buttonIdentities") == baseline.value("buttonIdentities") &&
                   current.value("buttonTargets") == baseline.value("buttonTargets") &&
                   current.value("buttonActions") == baseline.value("buttonActions");
        };
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            session.theme.setMode(mode);
            for (const bool zoomed : {false, true}) {
                session.shell.resetZoom();
                if (zoomed)
                    for (int step = 0; step < 5; ++step)
                        session.shell.zoomIn();
                for (int width : {1150, 1280}) {
                    session.window->resize(width, 750);
                    QTRY_VERIFY(verify());
                    QCOMPARE(session.window->winId(), handle);
                }
            }
        }
        session.window->showMinimized();
        QTRY_COMPARE(session.window->visibility(), QWindow::Minimized);
        session.window->showNormal();
        QVERIFY(Test::prepareNativeInput(session.window));
        QTRY_VERIFY(verify());
        session.windows.toggleFullscreen();
        QTRY_COMPARE_WITH_TIMEOUT(
            nativeWindowAppearance(session.window).value("fullscreenSettled").toString(),
            QString("full"), 10000);
        QTRY_VERIFY(session.windows.fullscreen());
        session.windows.toggleFullscreen();
        QTRY_COMPARE_WITH_TIMEOUT(
            nativeWindowAppearance(session.window).value("fullscreenSettled").toString(),
            QString("normal"), 10000);
        QVERIFY(!nativeWindowAppearance(session.window).value("nativeFullscreen").toBool());
        QTRY_VERIFY(verify());
        QCOMPARE(session.window->winId(), handle);
#endif
    }
    void toastRepetitionsUseTheSharedBadge()
    {
        ThemeSession session;
        QVERIFY(session.create());
        session.theme.setMotionEnabled(false);
        QQmlComponent referenceComponent(&session.engine);
        referenceComponent.setData(
            "import QtQuick\nimport EchoMusic.Controls\nimport EchoMusic.Theme\nEchoBadge { count: "
            "'×12'; colors: Theme.floating }",
            QUrl("qrc:/toast-badge-reference.qml"));
        std::unique_ptr<QObject> reference(referenceComponent.create());
        QVERIFY2(reference, qPrintable(referenceComponent.errorString()));
        const auto capture = qEnvironmentVariable("ECHO_TOAST_CAPTURE_DIR");
        if (!capture.isEmpty())
            QVERIFY(QDir().mkpath(capture));
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            session.theme.setMode(mode);
            for (bool zoomed : {false, true}) {
                session.shell.resetZoom();
                if (zoomed)
                    for (int step = 0; step < 5; ++step)
                        session.shell.zoomIn();
                for (bool standard : {false, true}) {
                    auto *toasts = session.shellController.toasts();
                    const auto id =
                        toasts->show("歌曲过渡 Transition", ToastService::Info, 0,
                                     standard ? ToastService::Standard : ToastService::Mini);
                    auto *badge = session.item("toastRepetitionBadge");
                    auto *card = session.item("toastCard");
                    QVERIFY(badge && card);
                    QVERIFY(!badge->isVisible());
                    for (int i = 1; i < 12; ++i)
                        QCOMPARE(
                            toasts->show("歌曲过渡 Transition", ToastService::Info, 0,
                                         standard ? ToastService::Standard : ToastService::Mini),
                            id);
                    QTRY_VERIFY(badge->isVisible());
                    QCOMPARE(badge->property("count").toString(), QString("×12"));
                    QCOMPARE(badge->implicitWidth(), reference->property("implicitWidth").toReal());
                    QCOMPARE(badge->implicitHeight(),
                             reference->property("implicitHeight").toReal());
                    QCOMPARE(badge->property("font").value<QFont>(),
                             reference->property("font").value<QFont>());
                    QTRY_VERIFY(qAbs(badge->x() - (card->width() - badge->width() * .65)) < .5);
                    QTRY_VERIFY(qAbs(badge->y() + badge->height() * .28) < .5);
                    auto *viewport = card->parentItem();
                    auto *anchor = viewport->property("anchorItem").value<QQuickItem *>();
                    QVERIFY(anchor);
                    for (int width : {1150, 1250}) {
                        session.window->resize(width, 750);
                        const auto aligned = [&] {
                            const auto rect = anchor->mapRectToItem(viewport->parentItem(),
                                                                    anchor->boundingRect());
                            const auto scale = session.shell.uiScale();
                            return qAbs(viewport->x() + viewport->width() * scale / 2 -
                                        rect.center().x()) < .6 &&
                                   qAbs(viewport->y() + viewport->height() * scale -
                                        (rect.y() - 12 * scale)) < .6;
                        };
                        QTRY_VERIFY(aligned());
                        render(session.window);
                    }
                    if (!capture.isEmpty())
                        QVERIFY(session.window->grabWindow().save(capture +
                                                                  QString("/toast-%1-%2-%3.png")
                                                                      .arg(int(mode))
                                                                      .arg(zoomed)
                                                                      .arg(standard)));
                    toasts->dismiss(id);
                    QTRY_VERIFY(!badge->isVisible());
                }
            }
        }
    }
    void mainWindowCompositionFlags()
    {
        ThemeSession session;
        QVERIFY(session.create());
        QCOMPARE(session.window->flags(), session.windows.mainWindowFlags());
#ifdef Q_OS_WIN
        QVERIFY(!session.visibleBeforeAttach);
        QVERIFY(session.firstShowReady);
        QVERIFY(session.windows.presentationReady());
        for (int build : {17763, 19045, 22000, 22620, 22621, 26100}) {
            const auto flags = WindowsWindowBackdrop::windowFlags(build);
            // Frameless on every build: the material switch must not change
            // the window presentation, or the persisted geometry ratchets one
            // resize-border ring per restart (see windowFlags).
            QVERIFY(flags.testFlag(Qt::FramelessWindowHint));
            // The Win11 branch keeps the client-extension and caption-painting
            // hints; the Win10 branch predates them.
            QCOMPARE(flags.testFlag(Qt::ExpandedClientAreaHint), build >= 22621);
            QCOMPARE(flags.testFlag(Qt::NoTitleBarBackgroundHint), build >= 22621);
            QCOMPARE(flags.testFlag(Qt::CustomizeWindowHint), build >= 22621);
            QVERIFY(!flags.testFlag(Qt::WindowCloseButtonHint));
        }
#elif defined(Q_OS_MACOS)
        QVERIFY(session.window->flags().testFlag(Qt::ExpandedClientAreaHint));
        QVERIFY(!session.window->flags().testFlag(Qt::CustomizeWindowHint));
#else
        QVERIFY(session.window->flags().testFlag(Qt::FramelessWindowHint));
#endif
    }
    void windowsMainWindowMaterialKeepsNativeFrame()
    {
#ifndef Q_OS_WIN
        QSKIP("Requires Windows 11 22H2+ and native DWM composition");
#else
        if (QGuiApplication::platformName() != "windows" ||
            QOperatingSystemVersion::current().microVersion() < 22621)
            QSKIP("Requires Windows 11 22H2+ and native DWM composition");
        QQuickWindow window;
        auto format = window.format();
        format.setAlphaBufferSize(8);
        window.setFormat(format);
        window.setFlags(WindowBackdrop::mainWindowFlags());
        window.setColor(Qt::transparent);
        window.resize(480, 320);
        const auto handle = reinterpret_cast<HWND>(window.winId());
        // The adapter must not touch the redirection surface policy: Qt owns it
        // (a DirectComposition swapchain for alpha windows), and flipping the
        // style after that swapchain exists leaves stale composition behind.
        const bool originalNoRedirection =
            (GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) != 0;
        WindowsWindowBackdrop backdrop;
        backdrop.attach(&window);
        window.show();
        QVERIFY(Test::prepareNativeInput(&window));
        QVERIFY(backdrop.supported());
        QVERIFY(backdrop.diagnostics().value("subclassInstalled").toBool());
        QCOMPARE(bool(GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP),
                 originalNoRedirection);
        QVERIFY(backdrop.diagnostics().value("requestedAlphaBufferSize").toInt() > 0);
        const auto nonLayered = [&] {
            return !(GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_LAYERED);
        };
        const auto hasFrame = [&] {
            return (GetWindowLongPtrW(handle, GWL_STYLE) & WS_THICKFRAME) != 0;
        };
        const auto materialType = [&] {
            int material = 0;
            return SUCCEEDED(DwmGetWindowAttribute(handle, 38, &material, sizeof(material)))
                       ? material
                       : -1;
        };
        QVERIFY(nonLayered());
        QVERIFY(hasFrame());
        const auto hit = [&](int x, int y) {
            RECT bounds{};
            GetWindowRect(handle, &bounds);
            return SendMessageW(handle, WM_NCHITTEST, 0,
                                MAKELPARAM(bounds.left + x, bounds.top + y));
        };
        QCOMPARE(hit(200, 20), LRESULT(HTCLIENT));
        QCOMPARE(hit(200, 150), LRESULT(HTCLIENT));
        QCOMPARE(hit(0, 0), LRESULT(HTTOPLEFT));
        QCOMPARE(hit(0, 150), LRESULT(HTLEFT));
        window.showMaximized();
        QTRY_VERIFY(window.windowStates().testFlag(Qt::WindowMaximized));
        QCoreApplication::processEvents();
        QCOMPARE(hit(0, 0), LRESULT(HTCLIENT));
        window.showFullScreen();
        QTRY_VERIFY(window.windowStates().testFlag(Qt::WindowFullScreen));
        // Drain queued style repair: it must not restore the caption bits that
        // Qt deliberately removed for fullscreen or break its state detection.
        QCoreApplication::processEvents();
        QVERIFY(!(GetWindowLongPtrW(handle, GWL_STYLE) & WS_CAPTION));
        QVERIFY(!hasFrame());
        QCOMPARE(hit(0, 0), LRESULT(HTCLIENT));
        RECT fullscreenBounds{};
        QVERIFY(GetWindowRect(handle, &fullscreenBounds));
        MONITORINFO monitor{sizeof(MONITORINFO)};
        QVERIFY(GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor));
        QCOMPARE(fullscreenBounds.left, monitor.rcMonitor.left);
        QCOMPARE(fullscreenBounds.top, monitor.rcMonitor.top);
        QCOMPARE(fullscreenBounds.right, monitor.rcMonitor.right);
        QCOMPARE(fullscreenBounds.bottom, monitor.rcMonitor.bottom);
        window.showNormal();
        QTRY_COMPARE(window.windowStates(), Qt::WindowStates(Qt::WindowNoState));
        QCoreApplication::processEvents();
        QVERIFY(GetWindowLongPtrW(handle, GWL_STYLE) & WS_CAPTION);
        QVERIFY(hasFrame());
        QCOMPARE(hit(0, 0), LRESULT(HTTOPLEFT));
        for (bool dark : {false, true}) {
            for (bool enabled : {true, false, true, false}) {
                QVERIFY2(backdrop.apply(enabled, dark).isEmpty(), "Native material rejected");
                QCoreApplication::processEvents();
                QVERIFY(hasFrame());
                QCOMPARE(reinterpret_cast<HWND>(window.winId()), handle);
                const auto menu = GetSystemMenu(handle, FALSE);
                QVERIFY(menu);
                const auto closeState = GetMenuState(menu, SC_CLOSE, MF_BYCOMMAND);
                QVERIFY(closeState != UINT(-1));
                QVERIFY(!(closeState & (MF_DISABLED | MF_GRAYED)));
            }
        }
        // Plain transparency keeps the desktop visible through the DComp
        // swapchain without a blur material: DWM_BB's blur region renders one
        // static snapshot on this OS, so no material is applied, and the
        // frame is not extended because its glass fill would cover the
        // desktop. The NCCALCSIZE override gives the client the whole window
        // while the theme is translucent: no non-client strip may remain at
        // the edges.
        QVERIFY(backdrop.apply(false, false, 80).isEmpty());
        QCoreApplication::processEvents();
        QVERIFY(hasFrame());
        RECT windowRect{}, clientRect{};
        QVERIFY(GetWindowRect(handle, &windowRect));
        QVERIFY(GetClientRect(handle, &clientRect));
        QCOMPARE(clientRect.right - clientRect.left, windowRect.right - windowRect.left);
        QCOMPARE(clientRect.bottom - clientRect.top, windowRect.bottom - windowRect.top);
        QVERIFY(backdrop.apply(false, false, 0).isEmpty());
        QCoreApplication::processEvents();
        QVERIFY(backdrop.apply(true, true).isEmpty());
        // Qt reinitializes frame/alpha policy on native style changes.
        window.setFlag(Qt::WindowStaysOnTopHint, true);
        QCoreApplication::processEvents();
        window.hide();
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(hasFrame());
        QCOMPARE(reinterpret_cast<HWND>(window.winId()), handle);
        backdrop.attach(nullptr);
        QVERIFY(hasFrame());
        QCOMPARE(bool(GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP),
                 originalNoRedirection);
#endif
    }
    void windowsLegacyAccentPreservesTransparentSurface()
    {
#ifndef Q_OS_WIN
        QSKIP("Requires Windows 10 or Windows 11 21H2 native composition");
#else
        const auto build = QOperatingSystemVersion::current().microVersion();
        if (QGuiApplication::platformName() != "windows" || build >= 22621)
            QSKIP("Requires the legacy Windows Accent path");
        QQuickWindow window;
        auto format = window.format();
        format.setAlphaBufferSize(8);
        window.setFormat(format);
        window.setFlags(WindowBackdrop::mainWindowFlags());
        window.setColor(Qt::transparent);
        window.resize(480, 320);
        WindowsWindowBackdrop backdrop;
        backdrop.attach(&window);
        window.show();
        QVERIFY(Test::prepareNativeInput(&window));
        QVERIFY(backdrop.supported());
        const auto handle = reinterpret_cast<HWND>(window.winId());
        const auto style = GetWindowLongPtrW(handle, GWL_EXSTYLE);
        QVERIFY(style & WS_EX_LAYERED);
        QVERIFY(backdrop.diagnostics().value("subclassInstalled").toBool());
        for (bool dark : {false, true}) {
            for (bool enabled : {true, false, true}) {
                QVERIFY(backdrop.apply(enabled, dark).isEmpty());
                QCoreApplication::processEvents();
                const auto state = backdrop.diagnostics();
                qInfo() << "Legacy material diagnostics" << state;
                if (!state.value("accentState").isValid())
                    QSKIP("The system does not expose Accent readback");
                QCOMPARE(state.value("accentState").toInt(), enabled ? 4 : 0);
                QCOMPARE(GetWindowLongPtrW(handle, GWL_EXSTYLE), style);
                QCOMPARE(reinterpret_cast<HWND>(window.winId()), handle);
                QVERIFY(window.requestedFormat().alphaBufferSize() > 0);
            }
            // The Acrylic stays on while the window is dragged: suspending it
            // for the duration of a modal move/size loop made the material
            // visibly disappear for the whole drag.
            SendMessageW(handle, WM_ENTERSIZEMOVE, 0, 0);
            QCOMPARE(backdrop.diagnostics().value("accentState").toInt(), 4);
            SendMessageW(handle, WM_EXITSIZEMOVE, 0, 0);
            QCoreApplication::processEvents();
            QCOMPARE(backdrop.diagnostics().value("accentState").toInt(), 4);
            SendMessageW(handle, WM_DWMCOMPOSITIONCHANGED, 0, 0);
            QCoreApplication::processEvents();
            QCOMPARE(backdrop.diagnostics().value("accentState").toInt(), 4);
            QCOMPARE(GetWindowLongPtrW(handle, GWL_EXSTYLE), style);
        }
        backdrop.attach(nullptr);
        QCOMPARE(GetWindowLongPtrW(handle, GWL_EXSTYLE), style);
#endif
    }
    void windowsFullscreenCoversMonitorAcrossNativeTransitions()
    {
#ifndef Q_OS_WIN
        QSKIP("Requires Windows native geometry and state transitions");
#else
        if (QGuiApplication::platformName() != "windows")
            QSKIP("Requires native Windows QPA");
        const bool modern = QOperatingSystemVersion::current().microVersion() >= 22621;
        ThemeSession session;
        QVERIFY(session.create());
        auto &window = *session.window;
        window.resize(1150, 750);
        window.show();
        QVERIFY(Test::prepareNativeInput(&window));
        WindowBackdrop backdrop(true);
        backdrop.attach(&window);
        const auto handle = reinterpret_cast<HWND>(window.winId());
        struct NativeLoopObserver final : QAbstractNativeEventFilter
        {
            HWND handle = nullptr;
            std::atomic_int entries = 0;
            bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override
            {
                const auto *event = static_cast<MSG *>(message);
                if (event->hwnd == handle && event->message == WM_ENTERSIZEMOVE)
                    ++entries;
                return false;
            }
        } observer;
        observer.handle = handle;
        qApp->installNativeEventFilter(&observer);
        const auto removeObserver = qScopeGuard([&] { qApp->removeNativeEventFilter(&observer); });
        const auto sendWithoutHangingOnRegression = [&](UINT message, WPARAM wParam,
                                                        LPARAM lParam) {
            const auto before = observer.entries.load();
            // A broken guard must fail the assertion, rather than leave the
            // test blocked forever in user32's modal move/resize loop.
            std::thread escape([&observer, before] {
                Sleep(100);
                if (observer.entries.load() > before) {
                    INPUT input{};
                    input.type = INPUT_KEYBOARD;
                    input.ki.wVk = VK_ESCAPE;
                    SendInput(1, &input, sizeof(INPUT));
                    input.ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(1, &input, sizeof(INPUT));
                }
            });
            const auto join = qScopeGuard([&] { escape.join(); });
            return SendMessageW(handle, message, wParam, lParam);
        };
        const auto normalStyle = GetWindowLongPtrW(handle, GWL_STYLE);
        // Reproduce the native half of Qt's transition before QWindow has
        // published FullScreen. Drain the adapter repair at that exact point.
        const auto nativeFullscreenStyle =
            normalStyle &
            ~LONG_PTR(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_MAXIMIZE);
        SetWindowLongPtrW(handle, GWL_STYLE, nativeFullscreenStyle);
        QCoreApplication::processEvents();
        QVERIFY(!window.windowStates().testFlag(Qt::WindowFullScreen));
        QVERIFY(!(GetWindowLongPtrW(handle, GWL_STYLE) & WS_CAPTION));
        SetWindowLongPtrW(handle, GWL_STYLE, normalStyle);
        QCoreApplication::processEvents();
        const auto coversMonitor = [&] {
            MONITORINFO monitor{sizeof(MONITORINFO)};
            RECT bounds{}, client{};
            POINT origin{};
            if (!GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor) ||
                !GetWindowRect(handle, &bounds) || !GetClientRect(handle, &client) ||
                !ClientToScreen(handle, &origin))
                return false;
            return EqualRect(&bounds, &monitor.rcMonitor) && origin.x == bounds.left &&
                   origin.y == bounds.top && client.right == bounds.right - bounds.left &&
                   client.bottom == bounds.bottom - bounds.top;
        };
        for (bool maximized : {false, true}) {
            window.showNormal();
            QCoreApplication::processEvents();
            const auto normal = window.geometry();
            RECT normalNative{};
            QVERIFY(GetWindowRect(handle, &normalNative));
            if (maximized)
                window.showMaximized();
            window.showFullScreen();
            QTRY_VERIFY(window.windowStates().testFlag(Qt::WindowFullScreen));
            QCoreApplication::processEvents();
            QTRY_VERIFY(coversMonitor());
            QVERIFY(!(GetWindowLongPtrW(handle, GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME)));
            auto *content = window.property("contentItem").value<QQuickItem *>();
            QVERIFY(content);
            QCOMPARE(content->mapRectToScene(content->boundingRect()),
                     QRectF(QPointF(), QSizeF(window.size())));
            RECT fixed{};
            QVERIFY(GetWindowRect(handle, &fixed));
            for (Qt::Edges edges :
                 {Qt::Edges(Qt::LeftEdge), Qt::Edges(Qt::RightEdge), Qt::Edges(Qt::TopEdge),
                  Qt::Edges(Qt::BottomEdge), Qt::LeftEdge | Qt::TopEdge,
                  Qt::RightEdge | Qt::TopEdge, Qt::LeftEdge | Qt::BottomEdge,
                  Qt::RightEdge | Qt::BottomEdge}) {
                auto *resize =
                    session.item("mainWindowResizeHandle-" + QString::number(edges.toInt()));
                QVERIFY(resize && !resize->isVisible());
            }
            for (const auto point :
                 {POINT{fixed.left, fixed.top}, POINT{fixed.right - 1, fixed.top},
                  POINT{fixed.left, fixed.bottom - 1}, POINT{fixed.right - 1, fixed.bottom - 1},
                  POINT{fixed.left, (fixed.top + fixed.bottom) / 2},
                  POINT{fixed.right - 1, (fixed.top + fixed.bottom) / 2},
                  POINT{(fixed.left + fixed.right) / 2, fixed.top},
                  POINT{(fixed.left + fixed.right) / 2, fixed.bottom - 1}})
                QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)),
                         LRESULT(HTCLIENT));
            for (WPARAM command :
                 {WPARAM(SC_MOVE), WPARAM(SC_MOVE | HTCAPTION), WPARAM(SC_SIZE | WMSZ_RIGHT)})
                sendWithoutHangingOnRegression(WM_SYSCOMMAND, command, 0);
            for (WPARAM hit :
                 {WPARAM(HTCAPTION), WPARAM(HTLEFT), WPARAM(HTTOPRIGHT), WPARAM(HTBOTTOM)})
                sendWithoutHangingOnRegression(WM_NCLBUTTONDOWN, hit,
                                               MAKELPARAM(fixed.left, fixed.top));
            QCOMPARE(observer.entries.load(), 0);
            for (UINT message : {UINT(WM_MOVING), UINT(WM_SIZING)}) {
                RECT proposed{fixed.left + 40, fixed.top + 30, fixed.right - 20, fixed.bottom - 10};
                QVERIFY(
                    SendMessageW(handle, message, WMSZ_RIGHT, reinterpret_cast<LPARAM>(&proposed)));
                QVERIFY(EqualRect(&proposed, &fixed));
            }
            RECT afterCommands{};
            QVERIFY(GetWindowRect(handle, &afterCommands));
            QVERIFY(EqualRect(&fixed, &afterCommands));
            QVERIFY(window.windowStates().testFlag(Qt::WindowFullScreen));
            window.hide();
            window.show();
            QCoreApplication::processEvents();
            QTRY_VERIFY(coversMonitor());
            window.setWindowStates(Qt::WindowFullScreen | Qt::WindowMinimized);
            QCoreApplication::processEvents();
            QVERIFY(IsIconic(handle));
            window.setWindowStates(Qt::WindowFullScreen);
            QCoreApplication::processEvents();
            QTRY_VERIFY(coversMonitor());
            if (maximized)
                window.showMaximized();
            else
                window.showNormal();
            QCoreApplication::processEvents();
            QCOMPARE(window.windowState(), maximized ? Qt::WindowMaximized : Qt::WindowNoState);
            QCOMPARE(bool(GetWindowLongPtrW(handle, GWL_STYLE) & WS_THICKFRAME), modern);
            if (!maximized) {
                // Qt restores fullscreen from the saved native frame geometry
                // and re-derives DIP by DPR rounding; at fractional scaling the
                // QWindow DIP rect drifts by one (upstream issue #16). The real
                // contract is the native rectangle, verified exactly; the DIP
                // geometry is held to a one-dip tolerance.
                RECT restoredNative{};
                QVERIFY(GetWindowRect(handle, &restoredNative));
                QVERIFY(EqualRect(&normalNative, &restoredNative));
                QVERIFY(qAbs(window.geometry().x() - normal.x()) <= 1);
                QVERIFY(qAbs(window.geometry().y() - normal.y()) <= 1);
                QVERIFY(qAbs(window.geometry().width() - normal.width()) <= 1);
                QVERIFY(qAbs(window.geometry().height() - normal.height()) <= 1);
            }
        }
#endif
    }
    void windowsCustomCaptionKeepsNativeMouseHover()
    {
#ifndef Q_OS_WIN
        QSKIP("Requires Windows native input delivery");
#else
        if (QGuiApplication::platformName() != "windows" ||
            QOperatingSystemVersion::current().microVersion() < 22621)
            QSKIP("Requires Windows 11 22H2+ native input delivery");
        QTemporaryDir directory;
        QSettings settings(directory.filePath("settings.ini"), QSettings::IniFormat);
        ThemeStore theme(settings);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            import EchoMusic.Shell
            Window {
                id: host
                width: 480; height: 320; color: "#202020"
                property int captionDoubleClicks: 0
                EchoWindowDragArea {
                    width: parent.width; height: 60
                    onMoveRequested: host.startSystemMove()
                    onDoubleClickRequested: {
                        host.captionDoubleClicks++;
                        if (host.visibility === Window.Maximized)
                            host.showNormal();
                        else
                            host.showMaximized();
                    }
                }
                EchoIconButton {
                    objectName: "nativeHoverAction"
                    x: 180; y: 140; glyph: "play"; text: "Native pointer hint"
                }
                ShellActionDragHandle {
                    objectName: "nativeDragTarget"
                    actionKey: "native-drag-probe"; glyph: "play"
                    x: 70; y: 220; width: 40; height: 40
                }
            }
        )",
                          QUrl());
        QScopedPointer<QObject> owner(component.create());
        QVERIFY2(owner, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(owner.data());
        QVERIFY(window);
        window->setFlags(WindowBackdrop::mainWindowFlags());
        WindowBackdrop backdrop(true);
        backdrop.attach(window);
        window->show();
        QVERIFY(Test::prepareNativeInput(window));
        auto *button = visual(window->contentItem(), "nativeHoverAction");
        QVERIFY(button);
        const auto handle = reinterpret_cast<HWND>(window->winId());
        const QPoint savedCursor = QCursor::pos();
        const auto cleanup = qScopeGuard([&] {
            INPUT release{};
            release.type = INPUT_MOUSE;
            release.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &release, sizeof(INPUT));
            QCursor::setPos(savedCursor);
        });
        const auto nativePoint = [&](QPoint local) {
            RECT rect{};
            GetClientRect(handle, &rect);
            POINT point{MulDiv(local.x(), rect.right, window->width()),
                        MulDiv(local.y(), rect.bottom, window->height())};
            ClientToScreen(handle, &point);
            return point;
        };
        // Use user32 delivery: QTest::mouseClick bypasses WM_NCHITTEST and
        // cannot reproduce Qt's expanded-caption synthetic button sequence.
        const auto empty = nativePoint({80, 80});
        SetCursorPos(empty.x, empty.y);
        for (DWORD action : {DWORD(MOUSEEVENTF_LEFTDOWN), DWORD(MOUSEEVENTF_LEFTUP)}) {
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = action;
            QCOMPARE(SendInput(1, &input, sizeof(INPUT)), UINT(1));
            QTest::qWait(30);
        }
        QTRY_COMPARE(QGuiApplication::mouseButtons(), Qt::MouseButtons(Qt::NoButton));
        QVERIFY(!window->mouseGrabberItem());
        const auto target =
            nativePoint(button->mapToScene(button->boundingRect().center()).toPoint());
        SetCursorPos(target.x, target.y);
        QTRY_VERIFY(button->property("hovered").toBool());
        auto *hint = button->findChild<QObject *>("iconButtonToolTip");
        QVERIFY(hint);
        QTRY_VERIFY(hint->property("popup").value<QObject *>());
        QTRY_VERIFY(hint->property("popup").value<QObject *>()->property("visible").toBool());
        QPointer<QObject> shownHint = hint->property("popup").value<QObject *>();
        QSignalSpy clicks(button, SIGNAL(clicked()));
        // A visible tooltip must not split native down/up delivery, leave the
        // control pressed, or reopen while the same pointer hover continues.
        for (DWORD action : {DWORD(MOUSEEVENTF_LEFTDOWN), DWORD(MOUSEEVENTF_LEFTUP)}) {
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = action;
            QCOMPARE(SendInput(1, &input, sizeof(INPUT)), UINT(1));
            QTest::qWait(30);
        }
        QTRY_COMPARE(clicks.size(), 1);
        QTRY_VERIFY(!button->property("pressed").toBool());
        QVERIFY(!button->property("down").toBool());
        QVERIFY(!window->mouseGrabberItem());
        QTRY_VERIFY(shownHint.isNull());
        QTest::qWait(600);
        QVERIFY(!hint->property("popup").value<QObject *>());
        SetCursorPos(empty.x, empty.y);
        QTRY_VERIFY(!button->property("hovered").toBool());
        // Real held-button motion checks both Quick drag handlers and user32's
        // modal move loop. Release must arrive even while that loop is running.
        const auto startDrag = [](POINT start) {
            return std::thread([start] {
                SetCursorPos(start.x, start.y);
                Sleep(80);
                INPUT input{};
                input.type = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                SendInput(1, &input, sizeof(INPUT));
                Sleep(80);
                for (int step = 1; step <= 6; ++step) {
                    SetCursorPos(start.x + step * 8, start.y + step * 12);
                    Sleep(40);
                }
                input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                SendInput(1, &input, sizeof(INPUT));
            });
        };
        auto *targetItem = visual(window->contentItem(), "nativeDragTarget");
        QVERIFY(targetItem);
        {
            auto itemDrag = startDrag(nativePoint(targetItem->mapToScene({20, 20}).toPoint()));
            const auto joinItemDrag = qScopeGuard([&] { itemDrag.join(); });
            QQuickItem *ghost = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT((ghost = visual(window->contentItem(), "native-drag-probe")) &&
                                         ghost->isVisible(),
                                     5000);
            QTRY_COMPARE(QGuiApplication::mouseButtons(), Qt::MouseButtons(Qt::NoButton));
            QTRY_VERIFY(!ghost->isVisible());
        }
        window->showMaximized();
        QTRY_VERIFY(window->windowStates().testFlag(Qt::WindowMaximized));
        QCoreApplication::processEvents();
        auto drag = startDrag(nativePoint({window->width() / 2, 25}));
        const auto joinDrag = qScopeGuard([&] { drag.join(); });
        QTRY_VERIFY_WITH_TIMEOUT(!window->windowStates().testFlag(Qt::WindowMaximized), 5000);
        QTRY_COMPARE(QGuiApplication::mouseButtons(), Qt::MouseButtons(Qt::NoButton));
        QVERIFY(window->width() < window->screen()->geometry().width());
        QCOMPARE(window->property("captionDoubleClicks").toInt(), 0);
#endif
    }
    void nativeWindowMaterialKeepsQtWindowOwnership()
    {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Cocoa ownership check requires the native macOS backend");
        WindowBackdrop backdrop(true);
        {
            QQuickWindow window;
            window.setColor(Qt::transparent);
            window.setFlags(Qt::Window | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
            window.resize(480, 320);
            window.show();
            QVERIFY(Test::prepareNativeInput(&window));
            backdrop.attach(&window);
            QVERIFY(backdrop.supported());
            const auto nativeView = window.winId();
#ifdef Q_OS_MACOS
            const auto native = nativeWindowAppearance(&window).value("nativeWindow");
#endif
            for (int round = 0; round < 6; ++round) {
                QVERIFY2(backdrop.apply(true, round % 2).isEmpty(), "Native material rejected");
                window.resize(480 + round * 10, 320 + round * 10);
                render(&window);
                QCOMPARE(window.winId(), nativeView);
                QVERIFY(backdrop.apply(false, false).isEmpty());
#ifdef Q_OS_MACOS
                QCOMPARE(nativeWindowAppearance(&window).value("backgroundAlpha").toReal(), 1.0);
                QVERIFY(nativeWindowAppearance(&window).value("shadow").toBool());
                for (int transparency : {80, 100}) {
                    QVERIFY(backdrop.apply(false, round % 2, transparency).isEmpty());
                    const auto appearance = nativeWindowAppearance(&window);
                    QCOMPARE(appearance.value("backgroundAlpha").toReal(), 0.0);
                    QVERIFY(!appearance.value("opaque").toBool());
                    QVERIFY(!appearance.value("shadow").toBool());
                    QVERIFY(appearance.value("closeButton").toBool());
                    QCOMPARE(appearance.value("nativeWindow"), native);
                }
                QVERIFY(backdrop.apply(true, round % 2, 80).isEmpty());
                QCOMPARE(nativeWindowAppearance(&window).value("backgroundAlpha").toReal(), 0.0);
#endif
            }
            qInfo() << "Native material graphics API:" << window.rendererInterface()->graphicsApi();
            QVERIFY(backdrop.apply(true, false).isEmpty());
            backdrop.attach(nullptr);
            QVERIFY(!backdrop.supported());
            QVERIFY(backdrop.apply(false, false).isEmpty());
            QCOMPARE(window.winId(), nativeView);
            backdrop.attach(&window);
            QVERIFY(backdrop.apply(true, true).isEmpty());
            window.close();
        }
        QVERIFY(!backdrop.supported());
        QVERIFY(backdrop.apply(false, false).isEmpty());
    }
    void skinFadesOnceAndAtmosphereTintsTheFinishedPanel()
    {
        ThemeSession session;
        QVERIFY(session.create());
        session.theme.setMode(ThemeStore::Light);
        session.theme.setPanelOpacity(100);
        render(session.window);
        auto *panel = session.item("mainPanelSurface");
        QVERIFY(panel);
        const auto point = panel->mapToScene(QPointF(12, 150));
        const auto pixel = [&] {
            const auto image = session.window->grabWindow();
            const auto dpr = session.window->devicePixelRatio();
            return image.pixelColor(qRound(point.x() * dpr), qRound(point.y() * dpr));
        };
        const auto before = pixel();
        QCOMPARE(before.alpha(), 255);
        session.theme.setCoverColor(QColor("#0040ff"));
        session.theme.setCoverAtmosphere(true);
        render(session.window);
        const auto tinted = pixel();
        qInfo() << "Panel atmosphere pixels:" << before << tinted
                << session.theme.coverAtmosphereColor();
        // The final tint must remain visible even over a 100% opaque panel.
        // Painting atmosphere beneath the panel incorrectly erases it entirely.
        QVERIFY(tinted.red() < before.red() - 8);
        QVERIFY(tinted.blue() > tinted.red() + 8);
        session.theme.setTransparency(80);
        render(session.window);
        qInfo() << "80% transparency pixel:" << pixel()
                << session.item("themeWindowBackground")->opacity();
        QVERIFY(qAbs(pixel().alpha() - 51) <= 2);
        session.theme.setTransparency(100);
        render(session.window);
        QCOMPARE(pixel().alpha(), 0);
        QCOMPARE(panel->opacity(), 1.0);
        QCOMPARE(session.item("playerPanelSurface")->opacity(), 1.0);
    }
    void playbackTransparencyReplacesShellAndRestoresOnExit()
    {
        ThemeSession session;
        PlaybackService playback(session.api, session.preferences, nullptr,
                                 session.directory.filePath("playback.sqlite"));
        const auto cleanup = qScopeGuard([&] {
            if (session.window)
                session.window->close();
            session.root.reset();
            session.window = nullptr;
            playback.shutdown();
        });
        session.theme.setMotionEnabled(false);
        QVERIFY(session.create());
        if (session.window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
            QSKIP("Native playback material pixels require RHI");
        session.shellController.attachPlayback(playback);
        QVERIFY(session.root->setProperty("playbackService", QVariant::fromValue(&playback)));
        QImage artwork(256, 256, QImage::Format_RGB32);
        artwork.fill(QColor("#d84c28"));
        const auto path = session.directory.filePath("opaque-cover.png");
        QVERIFY(artwork.save(path));
        PlaybackServiceTestAccess::artwork(playback, QUrl::fromLocalFile(path).toString());
        auto *pageOwner = session.root->findChild<QObject *>("playbackPageOwner");
        QVERIFY(pageOwner);
        QPointer<QObject> page;
        const auto openPage = [&] {
            const auto invoked =
                QMetaObject::invokeMethod(pageOwner, "open", Q_ARG(QVariant, QVariantMap{}));
            page = pageOwner->property("item").value<QObject *>();
            return invoked && !page.isNull();
        };
        auto *shell = session.item("shellContent");
        auto *skin = session.item("themeWindowBackground");
        QVERIFY(shell && skin);
        WindowBackdrop nativeBackdrop(true);
        nativeBackdrop.attach(session.window);
        QString materialError;
        QObject::connect(&session.theme, &ThemeStore::changed, &nativeBackdrop, [&] {
            if (nativeBackdrop.supported())
                materialError =
                    nativeBackdrop.apply(session.theme.windowFrosted(), session.theme.dark(),
                                         session.theme.transparency());
        });
        const auto snapshot = [&] {
            render(session.window);
            return session.window->grabWindow();
        };
        for (const bool zoomed : {false, true}) {
            session.shell.resetZoom();
            if (zoomed)
                for (int step = 0; step < 5; ++step)
                    session.shell.zoomIn();
            for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
                session.theme.setMode(mode);
                for (const bool frost : {false, true})
                    for (const int transparency : {0, 60, 100}) {
                        session.theme.setTransparency(transparency);
                        session.theme.setWindowFrosted(frost);
                        QVERIFY2(materialError.isEmpty(), qPrintable(materialError));
#ifdef Q_OS_MACOS
                        const auto native = nativeWindowAppearance(session.window);
                        QCOMPARE(native.value("backgroundAlpha").toReal(),
                                 frost || transparency > 0 ? 0. : 1.);
                        QCOMPARE(native.value("backdropCount").toInt(), frost ? 1 : 0);
#endif
                        playback.lyricAppearance()->setBackgroundBlur(true);
                        playback.lyricAppearance()->setBackgroundRhythm(true);
                        QVERIFY(openPage());
                        QTRY_VERIFY(page->property("opened").toBool());
                        QVERIFY(Test::prepareNativeInput(session.window));
                        QTRY_VERIFY(!shell->isVisible() && !skin->isVisible());
                        auto *background =
                            page->property("backgroundSurface").value<QQuickItem *>();
                        QVERIFY(background);
                        if (!frost && transparency < 100)
                            QTRY_VERIFY(background->property("blurReady").toBool());
                        else
                            QTRY_VERIFY(
                                !background->findChild<QQuickItem *>("playbackBackgroundTexture"));
                        auto *cover = session.item("playbackPageCover");
                        QVERIFY(cover);
                        QTRY_COMPARE(cover->property("status").toInt(), 1);
                        QTest::qWait(100);
                        const auto image = snapshot();
                        const auto captures = qEnvironmentVariable("ECHO_THEME_SCREENSHOTS");
                        if (!captures.isEmpty()) {
                            QDir().mkpath(captures);
                            QVERIFY(image.save(captures + QString("/lyric-%1-%2-%3-%4.png")
                                                              .arg(int(mode))
                                                              .arg(zoomed)
                                                              .arg(frost)
                                                              .arg(transparency)));
                        }
                        const auto dpr = image.width() / qreal(session.window->width());
                        const auto sample = [&](QPointF position) {
                            return image.pixelColor(qRound(position.x() * dpr),
                                                    qRound(position.y() * dpr));
                        };
                        const auto material = sample(background->mapToScene(QPointF(12, 120)));
                        qInfo() << "Main lyric material" << mode << frost << transparency
                                << material;
                        const int expected = frost ? 140 : qRound(255 * (1 - transparency / 100.));
                        QVERIFY(qAbs(material.alpha() - expected) <= 2);
                        const auto coverPixel = sample(
                            cover->mapToScene(QPointF(cover->width() / 2, cover->height() / 2)));
                        QCOMPARE(coverPixel.alpha(), 255);
                        QVERIFY(coverPixel.red() > coverPixel.blue() + 60);
                        QCOMPARE(cover->opacity(), 1.);
                        QVERIFY(QMetaObject::invokeMethod(page, "close"));
                        QTRY_VERIFY(page.isNull());
                        QTRY_VERIFY(shell->isVisible() && skin->isVisible());
                    }
            }
        }
        // Frost removes the shell from the entering frame, not just onOpened.
        // The previous close reloads the bar's asynchronous thumbnail. Verify
        // an actual loaded-cover flight, rather than its intentional placeholder.
        auto *bar = session.item("playerbar");
        QVERIFY(bar);
        auto *sourceCover = bar->property("coverItem").value<QQuickItem *>();
        QVERIFY(sourceCover);
        QTRY_COMPARE(sourceCover->property("status").toInt(), 1);
        render(session.window);
        session.theme.setMotionEnabled(true);
        session.theme.setWindowFrosted(true);
        QVERIFY(openPage());
        QTRY_VERIFY(page->property("visible").toBool());
        QVERIFY(!page->property("opened").toBool());
        QVERIFY(!shell->isVisible() && !skin->isVisible());
        QTRY_VERIFY(page->property("coverMoving").toBool());
        auto *motion = session.item("playbackPageMotion");
        QVERIFY(motion);
        QTRY_VERIFY(motion->property("progress").toReal() > .05);
        QVERIFY(sourceCover->property("resourceActive").toBool());
        QCOMPARE(sourceCover->property("status").toInt(), 1);
        const auto beforeFlightRect = motion->property("currentRect").toRectF();
        const auto flight = snapshot();
        const auto flightCenter =
            motion->mapToScene(motion->property("currentRect").toRectF().center());
        const auto flightDpr = flight.width() / qreal(session.window->width());
        const auto flightPixel = flight.pixelColor(qRound(flightCenter.x() * flightDpr),
                                                   qRound(flightCenter.y() * flightDpr));
        qInfo() << "Frost cover flight pixel" << flightPixel << beforeFlightRect
                << motion->property("currentRect") << motion->property("progress");
        const auto flightCaptures = qEnvironmentVariable("ECHO_THEME_SCREENSHOTS");
        if (!flightCaptures.isEmpty())
            QVERIFY(flight.save(flightCaptures + "/frost-cover-flight.png"));
        QCOMPARE(flightPixel.alpha(), 255);
        QVERIFY(flightPixel.red() > flightPixel.blue() + 60);
        QTRY_VERIFY(page->property("opened").toBool());
        QTRY_VERIFY(!sourceCover->property("resourceActive").toBool());
        QVERIFY(QMetaObject::invokeMethod(page, "close"));
        QTRY_VERIFY(shell->isVisible() && skin->isVisible());
        QTRY_VERIFY(page.isNull());
        session.theme.setWindowFrosted(false);
        session.theme.setTransparency(60);
        QVERIFY(openPage());
        QVERIFY(!page->property("opened").toBool());
        QVERIFY(shell->isVisible() && skin->isVisible());
        QTRY_VERIFY(page->property("opened").toBool());
        QVERIFY(!shell->isVisible() && !skin->isVisible());
        QVERIFY(QMetaObject::invokeMethod(page, "close"));
        QTRY_VERIFY(page.isNull());
        QVERIFY(shell->isVisible() && skin->isVisible());
    }
    void playlistNavigationKeepsTheShellMaterialContinuous()
    {
        ThemeSession session;
        PlaybackService playback(session.api, session.preferences, nullptr,
                                 session.directory.filePath("playback.sqlite"));
        SongMetadataService songMetadata(session.api);
        PlaylistDetailService playlists(
            session.api, playback, session.accountUi.playlists, session.accountUi.mutations,
            *session.accountUi.library.order(), songMetadata, session.routes, false);
        playlists.registerRoutes(session.routes);
        // Production Main retains route loaders. Tear those down before their
        // borrowed playlist/playback services, including on an assertion failure.
        const auto cleanup = qScopeGuard([&] {
            if (session.window)
                session.window->close();
            session.root.reset();
            session.window = nullptr;
            playlists.shutdown();
            session.routes.shutdown();
            playback.shutdown();
        });
        session.shell.setMotionEnabled(false);
        QVERIFY(session.create());
        session.window->resize(1150, 800);
        auto *panel = session.item("mainPanelSurface");
        auto *titlebar = session.item("shellTitleBar");
        QVERIFY(panel && titlebar);
        const auto settled = [&] {
            auto *controller = session.engine.incubationController();
            if (!controller)
                return true;
            controller->incubateFor(10);
            return controller->incubatingObjectCount() == 0;
        };
        QImage background(320, 240, QImage::Format_RGB32);
        background.fill(QColor("#597bab"));
        const auto imagePath = session.directory.filePath("background.png");
        QVERIFY(background.save(imagePath));
        for (const auto &skin : {QString("echo"), QString("solid"), QString("custom")}) {
            if (skin == "echo")
                session.theme.selectEcho();
            else if (skin == "solid")
                session.theme.selectSolid(QColor("#c67d45"));
            else {
                session.theme.importBackground(QUrl::fromLocalFile(imagePath));
                QTRY_VERIFY(!session.theme.imageImporting());
                QVERIFY(session.theme.imageError().isEmpty());
                QVERIFY(session.theme.applyBackground());
            }
            for (bool dark : {false, true})
                for (int variant : {0, 1, 2, 3}) {
                    const int transparency = variant % 2 ? 60 : 0;
                    session.shell.resetZoom();
                    if (variant >= 2)
                        for (int i = 0; i < 5; ++i)
                            session.shell.zoomIn();
                    session.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                    session.theme.setPanelOpacity(70);
                    session.theme.setTransparency(transparency);
                    session.theme.setCoverColor(QColor("#0033ff"));
                    session.theme.setCoverAtmosphere(true);
                    session.routes.navigate("themes");
                    QTRY_VERIFY(settled());
                    if (skin == "custom") {
                        auto *artwork = session.item("themeBackgroundImage");
                        QVERIFY(artwork);
                        // Import completion owns the managed file; Qt's
                        // asynchronous scene image has a separate lifecycle.
                        QTRY_COMPARE(artwork->property("status").toInt(), 1); // Image.Ready
                    }
                    render(session.window);
                    const auto before = session.window->grabWindow();
                    QVERIFY(session.routes.navigate("playlist-detail", {}, "material-check"));
                    QTRY_VERIFY(session.item("playlistDetailPage"));
                    QTRY_VERIFY(session.item("songCatalogView"));
                    QTRY_VERIFY(settled());
                    auto *context = playlists.contextFor(session.routes.current());
                    if (!context->songs()->count()) {
                        QJsonArray songs;
                        for (int i = 0; i < 80; ++i)
                            songs.append(QJsonObject{{"id", QString::number(i)},
                                                     {"title", "Material scroll check"}});
                        context->songs()->appendSongs(songs);
                    }
                    auto *catalog = session.item("songCatalogView");
                    auto *viewport = visual(catalog, "discoverySongs");
                    auto *header = visual(catalog, "songDetailHeader");
                    QVERIFY(viewport);
                    QVERIFY(header);
                    viewport->setProperty("contentY", -viewport->property("topMargin").toReal());
                    QTRY_COMPARE(catalog->property("scrollTop").toReal(), 0.);
                    QTRY_COMPARE(header->property("progress").toReal(), 0.);
                    QTRY_VERIFY(session.item("songDelegate"));
                    render(session.window);
                    const auto compareMaterial = [&] {
                        const auto after = session.window->grabWindow();
                        const auto dpr = session.window->devicePixelRatio();
                        // The unoccupied left gutter crosses titlebar, sliver,
                        // sticky toolbar and list. Compare the same viewport
                        // pixels, since the cover atmosphere is a gradient.
                        for (qreal y : {titlebar->height() / 2, titlebar->height() + 20,
                                        titlebar->height() + 210, panel->height() - 80}) {
                            const auto point = panel->mapToScene(QPointF(8, y)) * dpr;
                            const auto expected = before.pixelColor(point.toPoint());
                            const auto actual = after.pixelColor(point.toPoint());
                            QVERIFY2(qAbs(actual.red() - expected.red()) <= 2 &&
                                         qAbs(actual.green() - expected.green()) <= 2 &&
                                         qAbs(actual.blue() - expected.blue()) <= 2 &&
                                         qAbs(actual.alpha() - expected.alpha()) <= 2,
                                     qPrintable(QString("%1 dark=%2 transparency=%3 y=%4: %5 vs %6")
                                                    .arg(skin)
                                                    .arg(dark)
                                                    .arg(transparency)
                                                    .arg(y)
                                                    .arg(actual.name(QColor::HexArgb),
                                                         expected.name(QColor::HexArgb))));
                        }
                    };
                    compareMaterial();
                    viewport->setProperty("contentY", 500);
                    QTRY_VERIFY(catalog->property("scrollTop").toReal() > 300);
                    QTRY_COMPARE(header->property("progress").toReal(), 1.);
                    render(session.window);
                    compareMaterial();
                    const auto artifacts = qEnvironmentVariable("ECHO_THEME_MATERIAL_ARTIFACTS");
                    if (!artifacts.isEmpty()) {
                        QVERIFY(QDir().mkpath(artifacts));
                        QVERIFY(session.window->grabWindow().save(
                            artifacts + QString("/playlist-material-%1-%2-%3.png")
                                            .arg(skin)
                                            .arg(dark)
                                            .arg(variant)));
                    }
                }
        }
    }
    void backgroundFailureUsesTheSharedNoticeViewport()
    {
        ThemeSession session;
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QVERIFY(session.create());
        const auto id = session.shellController.toasts()->show("背景图片不可用，已显示主题默认底色",
                                                               ToastService::Warning, 0);
        render(session.window);
        auto *message = session.item("toastMessage");
        auto *card = session.item("toastCard");
        QVERIFY(message && card);
        QCOMPARE(message->property("text").toString(),
                 QString("背景图片不可用，已显示主题默认底色"));
        QTRY_COMPARE(card->height(), 36.0);
        QVERIFY(message->width() > 100);
        QVERIFY(card->mapToScene(QPointF()).y() < session.item("playerPanelSurface")->y());
        session.shellController.toasts()->dismiss(id);
        QCOMPARE(warnings.count(), 0);
    }
    void coverAndAppearancePreferencesShareTheProductionStore()
    {
        QTemporaryDir directory;
        QSettings preferences(directory.filePath("preferences.ini"), QSettings::IniFormat);
        ThemeStore theme(preferences);
        theme.setMode(ThemeStore::Light);
        theme.useCoverAccent();
        theme.setCoverColor(QColor("#eb8080"));
        QCOMPARE(theme.accentSource(), QString("cover"));
        QCOMPARE(theme.accent(), normalizeAccent(QColor("#eb8080"), false));
        theme.setCoverAtmosphere(true);
        theme.setAtmosphereHeight(10);
        theme.setAtmosphereStrength(999);
        theme.setTransparency(125);
        theme.setFloatingFrosted(true);
        theme.setWindowFrosted(true);
        ThemeStore reopened(preferences);
        QCOMPARE(reopened.accentSource(), QString("cover"));
        QVERIFY(reopened.coverAtmosphere());
        QCOMPARE(reopened.atmosphereHeight(), 20);
        QCOMPARE(reopened.atmosphereStrength(), 200);
        QCOMPARE(reopened.transparency(), 100);
        QVERIFY(reopened.floatingFrosted());
        QVERIFY(reopened.windowFrosted());
        reopened.resetAppearanceAdjustments();
        QCOMPARE(reopened.accentSource(), QString("theme"));
        QVERIFY(!reopened.coverAtmosphere());
        QCOMPARE(reopened.transparency(), 0);
        QVERIFY(!reopened.floatingFrosted());
        QVERIFY(!reopened.windowFrosted());
    }
    void fractionalSourceFontSizeKeepsFamilyAndWeight()
    {
        QTemporaryDir directory;
        QSettings preferences(directory.filePath("font.ini"), QSettings::IniFormat);
        ThemeStore theme(preferences);
        const auto base = theme.applicationFont();
        const auto whole = theme.styledFont(base, 25, 650, 0.1, false, false);
        const auto half = theme.styledFont(base, 12.5, 650, 0.1, false, false);
        QCOMPARE(whole.pixelSize(), 25);
        QVERIFY(qAbs(half.pointSizeF() * 2 - QFontInfo(whole).pointSizeF()) < 0.01);
        QCOMPARE(half.families(), base.families());
        QCOMPARE(half.weight(), QFont::Weight(650));
        QCOMPARE(half.letterSpacing(), whole.letterSpacing());
    }
    void lyricFontsPersistAndResolveIndependently()
    {
        QTemporaryDir directory;
        QSettings preferences(directory.filePath("fonts.ini"), QSettings::IniFormat);
        ThemeStore theme(preferences);
        const auto system = uiFont("system-ui");
        QString selected;
        for (const auto &family : theme.fontFamilies()) {
            if (family != system.families().first()) {
                selected = family;
                break;
            }
        }
        QVERIFY(!selected.isEmpty());
        theme.setGlobalFont(selected);
        QCOMPARE(theme.lyricBaseFont(), theme.applicationFont());
        QCOMPARE(theme.desktopLyricBaseFont(), theme.applicationFont());
        QCOMPARE(theme.miniLyricBaseFont(), theme.applicationFont());
        QCOMPARE(theme.taskbarLyricBaseFont(), theme.applicationFont());
        theme.setLyricFont("system-ui");
        theme.setDesktopLyricFont(selected);
        theme.setMiniLyricFont("system-ui");
        theme.setTaskbarLyricFont(selected);
        theme.setGlobalFont("system-ui");
        QCOMPARE(theme.lyricBaseFont(), system);
        QCOMPARE(theme.desktopLyricBaseFont().families().first(), selected);
        QCOMPARE(theme.miniLyricBaseFont(), system);
        QCOMPARE(theme.taskbarLyricBaseFont().families().first(), selected);
        theme.setGlobalFont(selected);
        QCOMPARE(theme.lyricBaseFont(), system);
        QCOMPARE(theme.miniLyricBaseFont(), system);
        {
            ThemeStore restored(preferences);
            QCOMPARE(restored.globalFont(), selected);
            QCOMPARE(restored.lyricFont(), QString("system-ui"));
            QCOMPARE(restored.desktopLyricFont(), selected);
            QCOMPARE(restored.miniLyricFont(), QString("system-ui"));
            QCOMPARE(restored.taskbarLyricFont(), selected);
            QCOMPARE(restored.desktopLyricBaseFont().families(), uiFont(selected).families());
        }
        theme.setLyricFont("follow");
        theme.setDesktopLyricFont("follow");
        theme.setMiniLyricFont("follow");
        theme.setTaskbarLyricFont("follow");
        QCOMPARE(theme.lyricBaseFont(), theme.applicationFont());
        QCOMPARE(theme.desktopLyricBaseFont(), theme.applicationFont());
        QCOMPARE(theme.miniLyricBaseFont(), theme.applicationFont());
        QCOMPARE(theme.taskbarLyricBaseFont(), theme.applicationFont());
        theme.setMiniLyricFont("EchoMissingFont-NotInstalled");
        QCOMPARE(theme.miniLyricFont(), QString("follow"));
        theme.setGlobalFont("system-ui");
    }
    void productionShellAndThemeTextFollowGlobalTypography()
    {
        ThemeSession session;
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QVERIFY(session.create());
        QTRY_VERIFY(session.item("themeCenterPage"));
        struct Sample
        {
            QPointer<QQuickItem> item;
            QFont font;
        };
        QList<Sample> samples;
        const auto collect = [&](auto &&self, QQuickItem *item) -> void {
            const auto font = item->property("font");
            if (font.canConvert<QFont>() && !item->property("text").toString().isEmpty())
                samples.append({item, font.value<QFont>()});
            for (auto *child : item->childItems())
                self(self, child);
        };
        collect(collect, session.window->contentItem());
        QVERIFY(samples.size() >= 20);
        const auto selected = QFontDatabase::hasFamily(QStringLiteral("Helvetica"))
                                  ? QStringLiteral("Helvetica")
                                  : QFontDatabase::families().value(0);
        for (const auto &preference : {selected, QStringLiteral("system-ui")}) {
            session.theme.setGlobalFont(preference);
            QTRY_COMPARE(session.window->property("font").value<QFont>().families(),
                         session.theme.applicationFont().families());
            for (const auto &sample : samples) {
                QVERIFY(sample.item);
                QTRY_COMPARE(sample.item->property("font").value<QFont>().families(),
                             session.theme.applicationFont().families());
                const auto font = sample.item->property("font").value<QFont>();
                QCOMPARE(font.pixelSize(), sample.font.pixelSize());
                QCOMPARE(font.weight(), sample.font.weight());
                QCOMPARE(font.letterSpacing(), sample.font.letterSpacing());
            }
        }
        QCOMPARE(warnings.count(), 0);
    }

    void initTestCase()
    {
        QQuickStyle::setStyle("Basic");
        configureUiTypography();
    }
    void init()
    {
        // Some Qt property binding warnings bypass QQmlEngine::warnings.
        // Do not accept a passing UI assertion with runtime QML diagnostics.
        QTest::failOnWarning(QRegularExpression(".*(?:QML|Binding loop detected|QQuickItem:).*"));
    }
    void originalColorGolden_data()
    {
        QTest::addColumn<QJsonObject>("sample");
        QFile input(QStringLiteral(ECHO_THEME_GOLDEN));
        QVERIFY(input.open(QIODevice::ReadOnly));
        const auto document = QJsonDocument::fromJson(input.readAll()).object();
        int index = 0;
        for (const auto &sample : document.value("cases").toArray())
            QTest::newRow(qPrintable(QString::number(index++))) << sample.toObject();
        QCOMPARE(index, 176);
    }
    void originalColorGolden()
    {
        QFETCH(QJsonObject, sample);
        const bool dark = sample.value("dark").toBool();
        const QColor seed(sample.value("seed").toString());
        const auto normalized = normalizeAccent(seed, dark);
        QCOMPARE(normalized.name(), sample.value("normalized").toString());
        QList<QColor> surfaces;
        for (const auto &surface : sample.value("surfaces").toArray())
            surfaces.append(QColor(surface.toString()));
        // Both normalized custom colors and unnormalized theme accents must match.
        for (bool raw : {false, true}) {
            const auto expected = sample.value(raw ? "rawPalette" : "palette").toObject();
            const auto actual = accentPalette(raw ? seed : normalized, dark, surfaces);
            QCOMPARE(actual.primary.name(), expected.value("primary").toString());
            QCOMPARE(actual.atmosphere.name(), expected.value("atmosphere").toString());
            QCOMPARE(actual.primaryText.name(), expected.value("primaryText").toString());
            QCOMPARE(actual.onPrimary.name(), expected.value("onPrimary").toString());
            QCOMPARE(actual.hover.name(), expected.value("hover").toString());
            QCOMPARE(actual.pressed.name(), expected.value("pressed").toString());
            QCOMPARE(actual.subtle.name(), expected.value("subtle").toString());
            QCOMPARE(actual.onSubtle.name(), expected.value("onSubtle").toString());
        }
    }
    void preferenceOwnershipAndReset()
    {
        QTemporaryDir temp;
        QSettings preferences(temp.filePath("settings.ini"), QSettings::IniFormat);
        ThemeStore theme(preferences);
        theme.selectSolid(QColor("#abcdef"));
        theme.setMode(ThemeStore::Dark);
        theme.setAccent(QColor("#ff00ff"));
        theme.setPanelOpacity(37);
        QCOMPARE(theme.customColor(), QColor("#ff00ff"));
        QCOMPARE(theme.accent(), normalizeAccent(QColor("#ff00ff"), true));
        ThemeStore reopened(preferences);
        QCOMPARE(reopened.customColor(), theme.customColor());
        QCOMPARE(reopened.accent(), theme.accent());
        QVERIFY(reopened.customAccent());
        reopened.resetAppearanceAdjustments();
        QCOMPARE(reopened.mode(), ThemeStore::Dark); // Original reset does not change mode or skin.
        QCOMPARE(reopened.seedColor(), QColor("#abcdef"));
        QCOMPARE(reopened.themeKey(), QString("solid"));
        QCOMPARE(reopened.accent(), QColor("#abcdef"));
        QCOMPARE(reopened.panelOpacity(), 50);
        QVERIFY(!reopened.customAccent());
        reopened.setMode(ThemeStore::System);
        reopened.adoptSystemScheme(Qt::ColorScheme::Light);
        QVERIFY(!reopened.dark());
        reopened.adoptSystemScheme(Qt::ColorScheme::Dark);
        QVERIFY(reopened.dark());
        ThemeStore restored(preferences);
        QCOMPARE(restored.mode(), ThemeStore::System);
        EchoColorValue value(nullptr);
        QCOMPARE(value.normalize(" #aBc "), QString("#aabbcc"));
        QCOMPARE(value.normalize("not-a-color"), QString("#31cfa1"));
        QCOMPARE(value.normalize("red"), QString("#31cfa1"));
    }
    void customEditorUsesTheRealCropAndClosesItsSubsurfaces()
    {
        ThemeSession session;
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QVERIFY(session.create());
        QVERIFY(Test::prepareNativeInput(session.window));
        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = session.item("themeCenterPage")));
        page->setProperty("tab", 2);
        QImage source(200, 600, QImage::Format_RGB32);
        source.fill(QColor("#3a70aa"));
        const auto file = session.directory.filePath("portrait.png");
        QVERIFY(source.save(file));
        session.theme.importBackground(QUrl::fromLocalFile(file));
        QTRY_VERIFY(!session.theme.imageImporting());
        auto *whole = session.item("selectWholeBackgroundAction");
        auto *reselect = session.item("reselectBackgroundAction");
        QVERIFY(whole && reselect);
        QTRY_COMPARE(whole->width(), reselect->width());
        auto *editor = session.item("themeImageEditor");
        auto *artwork = session.item("backgroundCropArtwork");
        auto *selection = session.item("backgroundCropSelection");
        QVERIFY(editor && artwork && selection);
        QTRY_COMPARE(artwork->property("status").toInt(), 1);
        QTRY_VERIFY(selection->width() > 0 && selection->height() > 0);
        QTRY_VERIFY(selection->isEnabled());
        render(session.window);
        QVERIFY(Test::prepareNativeInput(session.window));
        QVERIFY(qAbs(artwork->x() - (artwork->parentItem()->width() - artwork->width()) / 2) < 0.1);
        selection->forceActiveFocus();
        QTRY_VERIFY(selection->hasActiveFocus());
        QTest::keyClick(session.window, Qt::Key_Right);
        QVERIFY(session.theme.backgroundCrop().width() > 0);
        click(session.window, session.item("applyBackgroundAction"));
        QTRY_VERIFY(!session.theme.backgroundPreview());
        auto *reset = session.item("resetBackgroundAction");
        QVERIFY(reset);
        auto *resetLabel = visual(reset, "buttonLabel");
        QVERIFY(resetLabel);
        QTRY_VERIFY(resetLabel->width() + 0.1 >= resetLabel->implicitWidth());
        const auto applied = session.theme.backgroundCrop();
        selection->forceActiveFocus();
        QTest::keyClick(session.window, Qt::Key_Left, Qt::ShiftModifier);
        QTRY_VERIFY(session.theme.backgroundPreview());
        QVERIFY(session.theme.backgroundCrop() != applied);
        click(session.window, session.item("backgroundTextAction"));
        QPointer<QObject> picker = editor->property("textColorDialog").value<QObject *>();
        QVERIFY(picker);
        QTRY_VERIFY(picker->property("opened").toBool());
        page->setProperty("tab", 0);
        QTRY_VERIFY(picker.isNull());
        QCOMPARE(session.theme.backgroundCrop(), applied);
        QVERIFY(!session.theme.backgroundPreview());
        const auto captures = qEnvironmentVariable("ECHO_THEME_SCREENSHOTS");
        if (!captures.isEmpty()) {
            QDir().mkpath(captures);
            page->setProperty("tab", 2);
            for (auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
                session.theme.setMode(mode);
                render(session.window);
                QTest::qWait(200);
                QVERIFY(session.window->grabWindow().save(
                    captures + (mode == ThemeStore::Dark ? "/custom-editor-dark.png"
                                                         : "/custom-editor-light.png")));
            }
        }
        QCOMPARE(warnings.count(), 0);
    }
    void realThemeTabsColorDialogAndDrawer()
    {
        ThemeSession session;
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QVERIFY(session.create());
        QVERIFY(Test::prepareNativeInput(session.window));
        QQuickItem *tabs = nullptr;
        QTRY_VERIFY((tabs = session.item("themeBrowseTabs")));
        QTRY_COMPARE(tabs->width(), 288.0);
        QCOMPARE(tabs->height(), 44.0);
        QCOMPARE(tabs->property("inset").toReal(), 4.0);
        QVERIFY(visual(tabs, "tab-2")->isEnabled());
        // A selected background is insufficient: the shared check must load.
        QQuickItem *selectionCheck = nullptr;
        QTRY_VERIFY((selectionCheck = session.item("selectionCheck")));
        QTRY_COMPARE(selectionCheck->property("status").toInt(), 1);
        const QString screenshots = qEnvironmentVariable("ECHO_THEME_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QDir().mkpath(screenshots);
            session.theme.setMode(ThemeStore::Light);
            render(session.window);
            QTest::qWait(220);
            QVERIFY(session.window->grabWindow().save(screenshots + "/theme-catalog-light.png"));
            session.theme.setMode(ThemeStore::Dark);
            render(session.window);
            QTest::qWait(220);
            QVERIFY(session.window->grabWindow().save(screenshots + "/theme-catalog-dark.png"));
            session.theme.setMode(ThemeStore::Light);
        }
        auto *first = visual(tabs, "tab-0");
        QVERIFY(first);
        first->forceActiveFocus();
        QTest::keyClick(session.window, Qt::Key_Right);
        QTRY_COMPARE(tabs->property("currentIndex").toInt(), 1);
        QTest::keyClick(session.window, Qt::Key_Right);
        QTRY_COMPARE(tabs->property("currentIndex").toInt(), 2);
        QVERIFY(session.item("themeImageEditor")->isVisible());
        QTest::keyClick(session.window, Qt::Key_End);
        QTRY_COMPARE(tabs->property("currentIndex").toInt(), 2);
        QTest::keyClick(session.window, Qt::Key_Left);
        QTRY_COMPARE(tabs->property("currentIndex").toInt(), 1);
        auto *swatch = session.item("skinSwatch-0");
        QVERIFY(swatch);
        QCOMPARE(swatch->height(), 32.0);
        click(session.window, swatch);
        QTRY_COMPARE(session.theme.seedColor(), QColor("#7da3ce"));
        auto *customize = session.item("customSkinAction");
        QVERIFY(customize);
        click(session.window, customize);
        auto *page = session.item("themeCenterPage");
        QVERIFY(page);
        QPointer<QObject> picker = page->property("skinColorDialog").value<QObject *>();
        QVERIFY(picker);
        const auto presets = picker->property("presets").value<QStringList>();
        QCOMPARE(presets.size(), 18);
        // The dialog palette intentionally has a different order from the main 24-color palette.
        QCOMPARE(presets.mid(0, 8), (QStringList{"#7da3ce", "#6eafb7", "#bd97bb", "#ce929c",
                                                 "#bcaa81", "#87a793", "#9293bf", "#6b7180"}));
        QTRY_VERIFY(picker->property("opened").toBool());
        QQuickItem *hex = nullptr;
        QTRY_VERIFY((hex = session.item("colorHexInput")));
        // Wait for actual rendered popup geometry before native input.
        render(session.window);
        const QString beforeInput = qEnvironmentVariable("ECHO_THEME_SCREENSHOTS");
        if (!beforeInput.isEmpty()) {
            QDir().mkpath(beforeInput);
            QVERIFY(
                session.window->grabWindow().save(beforeInput + "/theme-color-dialog-input.png"));
        }
        click(session.window, hex);
        QTest::keyClick(session.window, Qt::Key_A, Qt::ControlModifier);
        for (const char key : QByteArray("#abc"))
            QTest::keyClick(session.window, key);
        QTRY_COMPARE(picker->property("draft").toString(), QString("#abc"));
        QCOMPARE(session.theme.seedColor(), QColor("#7da3ce"));
        auto *clear = visual(hex, "clearTextAction");
        QVERIFY(clear && clear->isVisible());
        click(session.window, clear);
        QTRY_COMPARE(picker->property("draft").toString(), QString());
        QCOMPARE(picker->property("normalized").toString(), QString("#31cfa1"));
        QCOMPARE(session.theme.seedColor(), QColor("#7da3ce"));
        click(session.window, session.item("colorCancelAction"));
        QTRY_VERIFY(picker.isNull());
        QCOMPARE(session.theme.seedColor(), QColor("#7da3ce"));
        click(session.window, customize);
        QTRY_VERIFY((picker = page->property("skinColorDialog").value<QObject *>()));
        QTRY_VERIFY(picker->property("opened").toBool());
        render(session.window);
        picker->setProperty("draft", QString("#abc"));
        // This path uses the real confirmation button and typed command.
        click(session.window, session.item("colorConfirmAction"));
        QTRY_COMPARE(session.theme.seedColor(), QColor("#aabbcc"));
        QTRY_VERIFY(picker.isNull());
        session.theme.setMode(ThemeStore::Light);
        if (!screenshots.isEmpty()) {
            QDir().mkpath(screenshots);
            session.window->update();
            QTest::qWait(220);
            QVERIFY(session.window->grabWindow().save(screenshots + "/theme-palette-light.png"));
        }
        click(session.window, customize);
        QTRY_VERIFY((picker = page->property("skinColorDialog").value<QObject *>()));
        QTRY_VERIFY(picker->property("opened").toBool());
        render(session.window);
        for (int i = 0; i < 5; ++i)
            session.shell.zoomIn();
        QCOMPARE(session.shell.uiScale(), 1.25);
        session.window->resize(1250, 800);
        auto *confirm = session.item("colorConfirmAction");
        QVERIFY(confirm);
        QTRY_VERIFY(confirm->mapToScene(QPointF(confirm->width(), confirm->height())).x() <=
                    session.window->width() - 8);
        if (!screenshots.isEmpty()) {
            session.window->update();
            QTest::qWait(220);
            QVERIFY(
                session.window->grabWindow().save(screenshots + "/theme-color-dialog-light.png"));
        }
        // Resize/zoom crosses a Cocoa window update; verify native input still
        // targets the test window before dispatching a keyboard close.
        QVERIFY(Test::prepareNativeInput(session.window));
        QTest::keyClick(session.window, Qt::Key_Escape);
        QTRY_VERIFY(picker.isNull());
        session.theme.setMode(ThemeStore::Dark);
        session.theme.setAccent(QColor("#f59e0b"));
        click(session.window, session.item("appearanceAction"));
        QPointer<QObject> drawer = page->property("appearanceDrawer").value<QObject *>();
        QVERIFY(drawer);
        QTRY_VERIFY(drawer->property("opened").toBool());
        render(session.window);
        auto *source = session.item("accentSourceControl");
        QVERIFY(source);
        QCOMPARE(source->property("currentIndex").toInt(), 2);
        source->forceActiveFocus();
        QTest::keyClick(session.window, Qt::Key_Up);
        QTest::keyClick(session.window, Qt::Key_Return);
        QTRY_VERIFY(!session.theme.customAccent());
        QCOMPARE(session.theme.accentSource(), QString("cover"));
        QCOMPARE(source->property("currentIndex").toInt(), 1);
        QTest::keyClick(session.window, Qt::Key_Down);
        QTest::keyClick(session.window, Qt::Key_Return);
        QTRY_VERIFY(session.theme.customAccent());
        QCOMPARE(source->property("currentIndex").toInt(), 2);
        QTRY_VERIFY(!source->property("popup").value<QObject *>());
        QTRY_VERIFY(source->mapToScene(QPointF(source->width(), source->height())).x() <=
                    session.window->width() - 8);
        if (!screenshots.isEmpty()) {
            session.window->update();
            QTest::qWait(220);
            QVERIFY(session.window->grabWindow().save(screenshots + "/theme-drawer-dark.png"));
        }
        const auto before = session.theme.customColor();
        click(session.window, session.item("customAccentAction"));
        QPointer<QObject> accentPicker = drawer->property("colorDialog").value<QObject *>();
        QVERIFY(accentPicker);
        QTRY_VERIFY(accentPicker->property("opened").toBool());
        render(session.window);
        auto *pickerContent = accentPicker->property("contentItem").value<QQuickItem *>();
        QVERIFY(pickerContent);
        auto *pickerClose = visual(pickerContent, "colorPickerClose");
        QVERIFY(pickerClose);
        QCOMPARE(pickerClose->size(), QSizeF(32, 32));
        const auto pickerScale = accentPicker->property("uiScale").toReal();
        QTRY_VERIFY(qAbs(pickerClose->mapToScene(QPointF(32, 0)).x() -
                         (accentPicker->property("x").toReal() +
                          (accentPicker->property("width").toReal() - 24) * pickerScale)) < 0.5);
        accentPicker->setProperty("draft", QString("#f0f"));
        QCOMPARE(session.theme.customColor(), before);
        if (!screenshots.isEmpty()) {
            session.window->update();
            QTest::qWait(220);
            QVERIFY(
                session.window->grabWindow().save(screenshots + "/theme-color-dialog-dark.png"));
        }
        // Find the active popup's content, not an earlier closed dialog's items.
        auto *accentContent = accentPicker->property("contentItem").value<QQuickItem *>();
        QVERIFY(accentContent);
        click(session.window, visual(accentContent, "colorConfirmAction"));
        QTRY_COMPARE(session.theme.customColor(), QColor("#ff00ff"));
        QTRY_VERIFY(accentPicker.isNull());
        render(session.window);
        click(session.window, session.item("resetAppearanceAction"));
        QTRY_VERIFY(!session.theme.customAccent());
        QCOMPARE(session.theme.seedColor(), QColor("#aabbcc"));
        QCOMPARE(session.theme.mode(), ThemeStore::Dark);
        QCOMPARE(session.theme.panelOpacity(), 50);
        // Route deactivation closes the shared in-app spectrum as well.
        session.theme.setAccent(before);
        render(session.window);
        click(session.window, session.item("customAccentAction"));
        QTRY_VERIFY((accentPicker = drawer->property("colorDialog").value<QObject *>()));
        QTRY_VERIFY(accentPicker->property("opened").toBool());
        accentContent = accentPicker->property("contentItem").value<QQuickItem *>();
        QPointer<QObject> activePicker = accentPicker;
        QVERIFY(!accentPicker->property("spectrumPopup").value<QObject *>());
        render(session.window);
        click(session.window, visual(accentContent, "colorSpectrumAction"));
        QPointer<QObject> spectrumPicker =
            accentPicker->property("spectrumPopup").value<QObject *>();
        QVERIFY(spectrumPicker);
        QTRY_VERIFY(spectrumPicker->property("visible").toBool());
        QTRY_VERIFY(spectrumPicker->property("opened").toBool());
        auto *spectrum = spectrumPicker->property("contentItem").value<QQuickItem *>();
        QVERIFY(spectrum);
        QCOMPARE(spectrum->property("value").toString(), before.name());
        if (!screenshots.isEmpty())
            QVERIFY(session.window->grabWindow().save(screenshots + "/theme-spectrum-dark.png"));
        auto *field = visual(spectrum, "colorSaturationValue");
        QVERIFY(field);
        // Draft-only pointer editing does not commit theme preferences.
        QTest::mouseClick(
            session.window, Qt::LeftButton, Qt::NoModifier,
            field->mapToScene({field->width() * 0.8, field->height() * 0.3}).toPoint());
        QTRY_VERIFY(accentPicker->property("draft").toString() != before.name());
        QCOMPARE(session.theme.customColor(), before);
        session.routes.navigate("search");
        QTRY_VERIFY(!drawer || !drawer->property("visible").toBool());
        QTRY_VERIFY(!activePicker || !activePicker->property("visible").toBool());
        QTRY_VERIFY(!spectrumPicker || !spectrumPicker->property("visible").toBool());
        QCOMPARE(warnings.size(), 0);
    }
};
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    ThemeTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ThemeTest.moc"
