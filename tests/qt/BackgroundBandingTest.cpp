#include "NativeWindow.h"
#include "ThemeStore.h"
#include "UiTypography.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include "SettingsStore.h"
#include <QTemporaryDir>
#include <QtTest>

using namespace EchoMusic;
class BackgroundBandingTest : public QObject
{
    Q_OBJECT
private slots:
    void atmosphereKeepsItsFadeWithoutHorizontalBands()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore preferences(directory.filePath("theme.ini"));
        ThemeStore theme(preferences);
        theme.setMotionEnabled(false);
        theme.setPanelOpacity(50);
        theme.setCoverColor(QColor("#4879ab"));
        theme.setCoverAtmosphere(true);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import EchoMusic.Theme
            Window {
                id: window
                visible: true; width: 1100; height: 800
                color: "#606060"
                property real sampleZoom: 1
                property real materialOpacity: 1
                Item {
                    width: window.width / window.sampleZoom
                    height: window.height / window.sampleZoom
                    scale: window.sampleZoom; transformOrigin: Item.TopLeft
                    ThemeBackground {
                        objectName: "skin"
                        anchors.fill: parent
                        opacity: window.materialOpacity
                        surfaceEffects: Component {
                            Rectangle {
                                x: 230; y: 8
                                width: parent.width - 240; height: parent.height - 108
                                radius: Theme.shellRadius
                                color: Theme.panelColor
                            }
                        }
                    }
                }
            }
        )",
                          QUrl("qrc:/background-banding.qml"));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        if (window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
            QSKIP("Banding requires the native RHI framebuffer");
        const auto artifacts = qEnvironmentVariable("ECHO_BANDING_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        auto *skin = root->findChild<QQuickItem *>("skin");
        QVERIFY(skin);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (const auto zoom : {1., 1.25}) {
                root->setProperty("sampleZoom", zoom);
                for (const auto strength : {20, 100, 200}) {
                    theme.setAtmosphereStrength(strength);
                    for (const auto opacity : {1., .4}) {
                        root->setProperty("materialOpacity", opacity);
                        QTest::qWait(100);
                        QCOMPARE(QQmlProperty(skin, "layer.enabled").read().toBool(), opacity < 1.);
                        const auto image = window->grabWindow();
                        QVERIFY(!image.isNull());
                        const auto key = QString("%1-%2-%3-%4")
                                             .arg(int(mode))
                                             .arg(zoom)
                                             .arg(strength)
                                             .arg(opacity);
                        const qreal ratio = qreal(image.width()) / window->width();
                        const int x0 = int((window->width() - 180) * ratio);
                        const int span = qRound(128 * ratio);
                        const int first = qRound(window->height() * .7 * .25 * ratio);
                        const int last = qRound(window->height() * .7 * .75 * ratio);
                        int variedRows = 0, longestPlateau = 0, run = 0;
                        QColor previous;
                        qreal worstMeanError = 0;
                        const auto tint = theme.coverAtmosphereColor();
                        const auto shell = theme.shell();
                        const auto panel = theme.panelColor();
                        for (int y = first; y < last; ++y) {
                            int low[3]{255, 255, 255}, high[3]{0, 0, 0};
                            double mean[3]{};
                            for (int x = x0; x < x0 + span; ++x) {
                                const auto color = image.pixelColor(x, y);
                                const int rgb[]{color.red(), color.green(), color.blue()};
                                for (int c = 0; c < 3; ++c) {
                                    low[c] = qMin(low[c], rgb[c]);
                                    high[c] = qMax(high[c], rgb[c]);
                                    mean[c] += qreal(rgb[c]) / span;
                                }
                            }
                            if (high[0] > low[0] || high[1] > low[1] || high[2] > low[2])
                                ++variedRows;
                            const auto color = image.pixelColor(x0, y);
                            run = color == previous ? run + 1 : 1;
                            previous = color;
                            longestPlateau = qMax(longestPlateau, run);
                            // Reference the original five-stop gradient over the panel and skin.
                            const qreal t = (y + .5) / (window->height() * .7 * ratio);
                            const qreal gain = qMax(1., strength / 100.);
                            const qreal peak = (mode == ThemeStore::Dark ? .30 : .32) * gain;
                            const qreal mid = (mode == ThemeStore::Dark ? .123 : .127) * gain;
                            const qreal positions[]{0, .22, .55, .802, 1};
                            const qreal levels[]{peak, peak * .94, mid, mid * .3, 0};
                            int stop = 0;
                            while (stop < 3 && t > positions[stop + 1])
                                ++stop;
                            const qreal f =
                                (t - positions[stop]) / (positions[stop + 1] - positions[stop]);
                            const qreal alpha = (levels[stop] * (1 - f) + levels[stop + 1] * f) *
                                                qMin(1., strength / 100.);
                            const qreal s[]{shell.redF(), shell.greenF(), shell.blueF()};
                            const qreal p[]{panel.redF(), panel.greenF(), panel.blueF()};
                            const qreal a[]{tint.redF(), tint.greenF(), tint.blueF()};
                            for (int c = 0; c < 3; ++c) {
                                const qreal base =
                                    p[c] * panel.alphaF() + s[c] * (1 - panel.alphaF());
                                const qreal expected =
                                    (base * (1 - alpha) + a[c] * alpha) * opacity * 255 +
                                    96 * (1 - opacity);
                                worstMeanError = qMax(worstMeanError, qAbs(mean[c] - expected));
                            }
                        }
                        const qreal variedFraction = qreal(variedRows) / (last - first);
                        qInfo() << key << "varied rows" << variedFraction << "longest plateau"
                                << longestPlateau << "mean error" << worstMeanError;
                        if (!artifacts.isEmpty()) {
                            QVERIFY(image.save(artifacts + "/background-" + key + ".png"));
                            QFile report(artifacts + "/metrics-" + key + ".json");
                            QVERIFY(report.open(QIODevice::WriteOnly));
                            report.write(
                                QJsonDocument(QJsonObject{{"varied_rows", variedFraction},
                                                          {"longest_plateau", longestPlateau},
                                                          {"mean_error", worstMeanError}})
                                    .toJson());
                        }
                        QVERIFY2(worstMeanError < 2., qPrintable(key + " changed the source fade"));
                        QVERIFY2(variedFraction > .8,
                                 qPrintable(key + " retains coherent horizontal bands"));
                    }
                }
            }
        }
        // Grain is tied to physical pixels, never time or redraw count.
        const auto stable = window->grabWindow();
        QTest::qWait(100);
        QCOMPARE(window->grabWindow(), stable);
        QTest::qWait(100);
        QSignalSpy frames(window, &QQuickWindow::frameSwapped);
        QTest::qWait(200);
        QCOMPARE(frames.size(), 0);
        qInfo() << "Idle frames over 200ms:" << frames.size();
        QPointer<QObject> shader = root->findChild<QObject *>("themeAtmosphereComposite");
        QVERIFY(shader);
        window->hide();
        QTRY_VERIFY(shader.isNull());
        window->show();
        QVERIFY(Test::prepareNativeInput(window));
        QTRY_VERIFY((shader = root->findChild<QObject *>("themeAtmosphereComposite")));
        root->setProperty("materialOpacity", 0.);
        QTRY_VERIFY(shader.isNull());
        root->setProperty("materialOpacity", 1.);
        QTRY_VERIFY((shader = root->findChild<QObject *>("themeAtmosphereComposite")));
        theme.setCoverAtmosphere(false);
        QTRY_VERIFY(shader.isNull());
    }
};
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    configureUiTypography();
    BackgroundBandingTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "BackgroundBandingTest.moc"
