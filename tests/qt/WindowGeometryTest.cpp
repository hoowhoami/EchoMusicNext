#include "SettingsKeys.h"
#include "WindowController.h"
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQuickWindow>
#include <QScreen>
#include <QTemporaryDir>
#include <QtTest>
#include <cstdio>

using namespace EchoMusic;
#ifdef Q_OS_MACOS
void zoomNativeWindow(QQuickWindow *window);
#endif
namespace
{
QJsonArray rectangle(const QRect &rect)
{
    return {rect.x(), rect.y(), rect.width(), rect.height()};
}
QRect testBounds(QQuickWindow &window, bool changed = false)
{
    const auto area = window.screen()->availableGeometry();
    const QSize size(qMin(changed ? 1240 : 1200, area.width()),
                     qMin(changed ? 720 : 700, area.height()));
    return {area.topLeft() + QPoint(qMin(20, area.width() - size.width()),
                                    qMin(30, area.height() - size.height())),
            size};
}
int child(const QString &file, const QString &phase)
{
    SettingsStore settings(file);
    WindowController windows(settings, true);
    QQuickWindow window;
    // Match Main.qml's flags and Cocoa initial visibility before attach.
    window.setFlags(windows.mainWindowFlags());
    window.resize(1150, 750);
    window.setMinimumSize({1100, 650});
    window.show();
    windows.attach(&window);
    const auto initial = window.geometry();
    windows.showWindow();
    QTest::qWait(100);
    QJsonObject result{{"initial", rectangle(initial)}, {"remember", windows.rememberSize()}};
    if (phase != "read") {
        if (phase == "disable")
            windows.setRememberSize(false);
        window.setGeometry(testBounds(window, phase == "disable"));
        result["normal"] = rectangle(window.geometry());
        if (phase != "immediate" && phase != "maximize-immediate")
            QTest::qWait(350); // Allow the real debounced save to complete.
        if (phase.startsWith("maximize") || phase == "roundtrip" || phase == "native-zoom") {
            result["beforeZoom"] = result.value("normal");
#ifdef Q_OS_MACOS
            if (phase == "native-zoom")
                zoomNativeWindow(&window);
            else
#endif
                windows.toggleMaximized();
            if (!QTest::qWaitFor([&] { return windows.maximized(); }, 2000))
                return 2;
            if (phase != "maximize-immediate")
                QTest::qWait(350);
            result["zoomed"] = rectangle(window.geometry());
            result["maximized"] = windows.maximized();
            if (phase == "roundtrip") {
                windows.toggleMaximized();
                if (!QTest::qWaitFor([&] { return !windows.maximized(); }, 2000))
                    return 3;
                QTest::qWait(100);
                result["restored"] = rectangle(window.geometry());
                window.setGeometry(testBounds(window, true));
                QTest::qWait(350);
                result["normal"] = rectangle(window.geometry());
            }
        }
    }
    result["actual"] = rectangle(window.geometry());
    result["beforeShutdown"] =
        rectangle(settings.value(SettingsKeys::Window::NormalGeometry).toRect());
    windows.shutdown();
    settings.sync();
    result["saved"] = rectangle(settings.value(SettingsKeys::Window::NormalGeometry).toRect());
    result["settingsOk"] = settings.status() == SettingsStore::NoError;
    const auto json = QJsonDocument(result).toJson(QJsonDocument::Compact);
    std::printf("%s\n", json.constData());
    return settings.status() == SettingsStore::NoError ? 0 : 4;
}
} // namespace
class WindowGeometryTest final : public QObject
{
    Q_OBJECT
    QJsonObject run(const QString &file, const QString &phase)
    {
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(), {"--geometry-child", file, phase});
        if (!process.waitForFinished(10000) || process.exitStatus() != QProcess::NormalExit ||
            process.exitCode() != 0) {
            const auto error = process.readAllStandardError();
            QTest::qFail(
                qPrintable(
                    QString("Geometry child %1 failed: %2").arg(phase, QString::fromUtf8(error))),
                __FILE__, __LINE__);
            return {};
        }
        const auto output = process.readAllStandardOutput().trimmed();
        const auto result = QJsonDocument::fromJson(output).object();
        if (!result.value("settingsOk").toBool())
            QTest::qFail(qPrintable(QString::fromUtf8(output)), __FILE__, __LINE__);
        return result;
    }
private slots:
    void restartPreservesNormalGeometry_data()
    {
        QTest::addColumn<QString>("phase");
        for (const auto &phase :
             {"save", "immediate", "maximize", "maximize-immediate", "roundtrip"})
            QTest::newRow(phase) << QString::fromLatin1(phase);
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa")
            QTest::newRow("native-zoom") << QStringLiteral("native-zoom");
#endif
    }
    void restartPreservesNormalGeometry()
    {
        QFETCH(QString, phase);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto file = directory.filePath("settings.ini");
        const auto saved = run(file, phase);
        QVERIFY(!saved.isEmpty());
        if (phase.startsWith("maximize") || phase == "native-zoom") {
            QVERIFY(saved.value("maximized").toBool());
            QVERIFY(saved.value("zoomed") != saved.value("normal"));
        }
        QCOMPARE(saved.value("saved"), saved.value("normal"));
        if (phase == "roundtrip") {
            QCOMPARE(saved.value("restored"), saved.value("beforeZoom"));
            QVERIFY(saved.value("normal") != saved.value("restored"));
        }
        const auto restored = run(file, "read");
        QCOMPARE(restored.value("initial"), saved.value("normal"));
        QCOMPARE(restored.value("actual"), saved.value("normal"));
        QCOMPARE(restored.value("saved"), saved.value("normal"));
    }
    void disabledMemoryUsesDefaultOnRestart()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto file = directory.filePath("settings.ini");
        const auto saved = run(file, "save");
        const auto disabled = run(file, "disable");
        QCOMPARE(disabled.value("saved"), saved.value("saved"));
        QVERIFY(disabled.value("actual") != saved.value("saved"));
        const auto restored = run(file, "read");
        QVERIFY(!restored.value("remember").toBool());
        QVERIFY(restored.value("initial") != saved.value("saved"));
    }
};
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    if (argc == 4 && QString::fromLocal8Bit(argv[1]) == "--geometry-child")
        return child(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    WindowGeometryTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "WindowGeometryTest.moc"
