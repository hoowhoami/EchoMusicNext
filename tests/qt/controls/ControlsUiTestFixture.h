#pragma once
#include "EchoIconSource.h"
#include "EchoImageMetrics.h"
#include "EchoWheelScroll.h"
#include <QStyleHints>
#include <QAccessibilityHints>
#include <QWheelEvent>
#include <QScopeGuard>
#include "EchoBackdropRoot.h"
#include "EchoSelectModel.h"
#include <QAbstractItemModelTester>
#include <QStringListModel>
#include <QStandardItemModel>
#include "NativeWindow.h"
#include "ThemeStore.h"
#include "UiTypography.h"
#include <QDir>
#include <QFontDatabase>
#include <QPainterPath>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <QSGTextureProvider>
#include <QRunnable>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextLayout>
#include <QtTest>
#include <QTimer>
#include <functional>
#include <ctime>
#include <atomic>

using namespace EchoMusic;
namespace
{
[[maybe_unused]] QQuickItem *findVisual(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = findVisual(child, name))
            return found;
    return nullptr;
}
struct WheelRecord
{
    QPoint pixel, angle;
    Qt::ScrollPhase phase;
    bool inverted;
    quint64 timestamp;
    Qt::MouseEventSource source;
    const QPointingDevice *device;
};
class WheelRecorder : public QObject
{
public:
    QList<WheelRecord> records;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Wheel) {
            const auto *wheel = static_cast<QWheelEvent *>(event);
            records.append({wheel->pixelDelta(), wheel->angleDelta(), wheel->phase(),
                            wheel->inverted(), wheel->timestamp(), wheel->source(),
                            wheel->pointingDevice()});
        }
        return false;
    }
};
[[maybe_unused]] bool notifyMotionPreference(Qt::MotionPreference preference)
{
    // Inject the public Qt notification only in this test process; no OS
    // accessibility setting is written.
    auto *hints = const_cast<QAccessibilityHints *>(QGuiApplication::styleHints()->accessibility());
    return QMetaObject::invokeMethod(hints, "motionPreferenceChanged", Qt::DirectConnection,
                                     Q_ARG(Qt::MotionPreference, preference));
}
[[maybe_unused]] bool supportsFloatingEffects(QQuickWindow *window)
{
    const auto api = window->rendererInterface()->graphicsApi();
    return QSGRendererInterface::isApiRhiBased(api) && api != QSGRendererInterface::Null;
}
QSize renderedTextureSize(QQuickWindow *window, QQuickItem *item)
{
    struct Result
    {
        QSize size;
        std::atomic_bool ready = false;
    };
    const auto result = std::make_shared<Result>();
    window->scheduleRenderJob(QRunnable::create([item, result] {
                                  if (auto *provider = item->textureProvider())
                                      if (auto *texture = provider->texture())
                                          result->size = texture->textureSize();
                                  result->ready.store(true, std::memory_order_release);
                              }),
                              QQuickWindow::AfterRenderingStage);
    window->update();
    if (!QTest::qWaitFor([&] { return result->ready.load(std::memory_order_acquire); }))
        return {};
    return result->size;
}
struct ControlsSession
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings{directory.filePath("settings.ini")};
    ThemeStore theme{settings};
    QQmlEngine engine;
    QScopedPointer<QObject> root;
    QQuickWindow *window = nullptr;
    bool create(const QString &harness = QStringLiteral("ControlsHarness.qml"))
    {
        theme.attachToEngine(engine);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/ControlsTests/" + harness));
        root.reset(component.create());
        if (!root) {
            qWarning().noquote() << component.errorString();
            return false;
        }
        window = qobject_cast<QQuickWindow *>(root.data());
        return window && Test::prepareNativeInput(window);
    }
    QQuickItem *item(const QString &name)
    {
        return findVisual(window->contentItem(), name);
    }
    ~ControlsSession()
    {
        if (window)
            window->close();
    }
};
} // namespace
