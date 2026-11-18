#pragma once

#include <QGuiApplication>
#include <QPointer>
#include <QWindow>
#include <QtTest>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace EchoMusic::Test
{
inline void verifyFloatingWindowsAcrossMinimize(QWindow &main, const QList<QWindow *> &windows)
{
    const auto bounds = [](QWindow *window) -> QRect {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "windows") {
            RECT rect;
            if (!GetWindowRect(reinterpret_cast<HWND>(window->winId()), &rect))
                return {};
            return {int(rect.left), int(rect.top), int(rect.right - rect.left),
                    int(rect.bottom - rect.top)};
        }
#endif
        return window->geometry();
    };
    struct FloatingWindow
    {
        QPointer<QWindow> window;
        QRect geometry;
        QByteArray name;
    };
    QList<FloatingWindow> expected;
    for (auto *window : windows) {
        QVERIFY(window);
        const auto geometry = bounds(window);
        QVERIFY(geometry.isValid());
        expected.append({window, geometry, window->objectName().toUtf8()});
    }
    const auto verifyWindows = [&] {
        for (const auto &entry : expected) {
            const auto window = entry.window.data();
            QVERIFY2(window && window->isVisible(), entry.name.constData());
            QVERIFY2(!window->transientParent(), entry.name.constData());
            QVERIFY2(!window->windowStates().testFlag(Qt::WindowMinimized), entry.name.constData());
#ifdef Q_OS_WIN
            if (QGuiApplication::platformName() == "windows") {
                const auto handle = reinterpret_cast<HWND>(window->winId());
                QVERIFY2(IsWindowVisible(handle), entry.name.constData());
                QVERIFY2(!IsIconic(handle), entry.name.constData());
                QVERIFY2(!GetWindow(handle, GW_OWNER), entry.name.constData());
            }
#endif
            QCOMPARE(bounds(window), entry.geometry);
        }
    };
    verifyWindows();
    if (QTest::currentTestFailed())
        return;
    for (const bool minimize : {true, false}) {
        if (minimize)
            main.showMinimized();
        else
            main.showNormal();
        QTRY_COMPARE(main.windowStates().testFlag(Qt::WindowMinimized), minimize);
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "windows")
            QTRY_COMPARE(bool(IsIconic(reinterpret_cast<HWND>(main.winId()))), minimize);
#endif
        // Keep checking across event dispatch while Main is minimized, so
        // a native hide followed by a restore cannot pass on the final state.
        for (int sample = 0; sample < (minimize ? 8 : 1); ++sample) {
            QTest::qWait(80);
            verifyWindows();
            if (QTest::currentTestFailed())
                return;
        }
    }
}
} // namespace EchoMusic::Test
