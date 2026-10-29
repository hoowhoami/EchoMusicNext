#include "AudioEngine.h"
#include "NativeBridge.h"
#include "PlaybackAudioControls.h"
#include "ThemeStore.h"
#include <QDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
class DspProviderUiTest : public QObject
{
    Q_OBJECT
    static QQuickItem *button(QQuickItem *root, const QString &text)
    {
        if (root->isVisible() && root->property("text").toString() == text &&
            root->inherits("QQuickAbstractButton"))
            return root;
        for (auto *child : root->childItems())
            if (auto *found = button(child, text))
                return found;
        return nullptr;
    }
    static QQuickItem *textItem(QQuickItem *root, const QString &text)
    {
        if (root->isVisible() && root->property("text").toString() == text && root->width() > 0)
            return root;
        for (auto *child : root->childItems())
            if (auto *found = textItem(child, text))
                return found;
        return nullptr;
    }
    static QQuickItem *namedItem(QQuickItem *root, const QString &name)
    {
        if (root->isVisible() && root->objectName() == name)
            return root;
        for (auto *child : root->childItems())
            if (auto *found = namedItem(child, name))
                return found;
        return nullptr;
    }
private slots:
    void productionSurfaces()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("preferences.ini"));
        ThemeStore theme(settings);
        theme.setFloatingFrosted(true);
        NativeBridge native;
        AudioEngine engine;
        QSignalSpy events(&engine, &AudioEngine::eventReceived),
            closed(&engine, &AudioEngine::closed);
        QTRY_VERIFY_WITH_TIMEOUT(!events.isEmpty(), 15000);
        PlaybackAudioControls audio(engine, native, settings);
        connect(&engine, &AudioEngine::eventReceived, &audio, &PlaybackAudioControls::handleEvent);
        audio.initialize();
        QTRY_VERIFY_WITH_TIMEOUT(!audio.providers()->busy(), 5000);
        QQmlEngine qml;
        theme.attachToEngine(qml);
        QSignalSpy warnings(&qml, &QQmlEngine::warnings);
        QQmlComponent component(&qml, QUrl("qrc:/qt/qml/EchoMusic/DspTests/DspHarness.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"audio", QVariant::fromValue(&audio)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        auto *manager = root->property("manager").value<QObject *>();
        QVERIFY(manager);
        const auto capture = qEnvironmentVariable("ECHO_DSP_CAPTURE_DIR");
        if (!capture.isEmpty())
            QVERIFY(QDir().mkpath(capture));
        for (auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            QVERIFY(QMetaObject::invokeMethod(manager, "open"));
            QTRY_VERIFY_WITH_TIMEOUT(manager->property("opacity").toDouble() > 0.999, 5000);
            auto *empty = namedItem(window->contentItem(), QStringLiteral("dspProviderEmptyState"));
            auto *heading = textItem(window->contentItem(), QStringLiteral("音效引擎"));
            auto *import = button(window->contentItem(), QStringLiteral("导入音效引擎"));
            QVERIFY(empty && heading && import);
            QCOMPARE(import->height(), 36.);
            QTRY_VERIFY(qAbs(empty->mapToScene({0, 0}).x() - heading->mapToScene({0, 0}).x()) <
                        0.5);
            QTRY_VERIFY(qAbs(empty->mapToScene({empty->width(), 0}).x() -
                             import->mapToScene({import->width(), 0}).x()) < 0.5);
            QVERIFY(import->width() < empty->width() / 2);
            QVERIFY(import->mapToScene({0, 0}).y() >=
                    empty->mapToScene({0, empty->height()}).y() + 8);
            if (!capture.isEmpty())
                QVERIFY(window->grabWindow().save(
                    capture + (mode == ThemeStore::Dark ? "/empty-dark.png" : "/empty-light.png")));
            QVERIFY(QMetaObject::invokeMethod(manager, "close"));
            QTRY_VERIFY_WITH_TIMEOUT(!manager->property("visible").toBool(), 5000);
        }
        auto path = qEnvironmentVariable("ECHO_DSP_TEST_PROVIDER");
        if (path.isEmpty())
            path = DSP_TEST_LIBRARY;
        audio.providers()->importFile(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending() && !audio.providerId().isEmpty(), 15000);
        for (auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            QVERIFY(QMetaObject::invokeMethod(manager, "open"));
            QTRY_VERIFY(manager->property("visible").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(manager->property("opacity").toDouble() > 0.999, 5000);
            QTest::qWait(100);
            auto *import = button(window->contentItem(), QStringLiteral("导入音效引擎"));
            QVERIFY(import);
            QCOMPARE(import->height(), 36.);
            auto *card = namedItem(window->contentItem(), QStringLiteral("dspProviderCard"));
            auto *heading = textItem(window->contentItem(), QStringLiteral("音效引擎"));
            QVERIFY(card && heading);
            auto *list = namedItem(window->contentItem(), QStringLiteral("dspProviderList"));
            QVERIFY(list);
            QTRY_VERIFY(card->mapToScene({0, card->height()}).y() <=
                        list->mapToScene({0, list->height()}).y() - 1.5);
            QTRY_VERIFY(qAbs(card->mapToScene({0, 0}).x() - heading->mapToScene({0, 0}).x() - 2) <
                        0.5);
            QVERIFY(import->width() < card->width() / 2);
            QTRY_VERIFY(qAbs(card->mapToScene({card->width(), 0}).x() -
                             import->mapToScene({import->width(), 0}).x() + 4) < 0.5);
            QTRY_VERIFY(textItem(window->contentItem(), QStringLiteral("使用中")));
            QCOMPARE(manager->property("width").toDouble(), 520.);
            if (QGuiApplication::platformName() != "offscreen" &&
                window->rendererInterface()->graphicsApi() != QSGRendererInterface::Software) {
                const auto image = window->grabWindow();
                QVERIFY(!image.isNull());
                auto *heading = textItem(window->contentItem(), QStringLiteral("音效引擎"));
                QVERIFY(heading);
                const auto origin = heading->mapToScene({0, 0});
                const auto dpr = window->devicePixelRatio();
                const auto bounds = QRectF(origin, heading->size()).toAlignedRect();
                const auto expected = heading->property("color").value<QColor>();
                int ink = 0;
                for (int y = qRound(bounds.top() * dpr); y < qRound(bounds.bottom() * dpr); ++y)
                    for (int x = qRound(bounds.left() * dpr); x < qRound(bounds.right() * dpr);
                         ++x) {
                        const auto pixel = image.pixelColor(x, y);
                        if (qAbs(pixel.red() - expected.red()) < 15 &&
                            qAbs(pixel.green() - expected.green()) < 15 &&
                            qAbs(pixel.blue() - expected.blue()) < 15)
                            ++ink;
                    }
                // The modal dimmer and captured scene must stay below popup
                // text. Visibility/property checks alone miss this RHI defect.
                QVERIFY2(ink > 20, "Frosted dialog foreground was dimmed/overpainted");
                if (!capture.isEmpty())
                    QVERIFY(
                        image.save(capture + (mode == ThemeStore::Dark ? "/manager-dark.png"
                                                                       : "/manager-light.png")));
            }
            QVERIFY(QMetaObject::invokeMethod(manager, "close"));
            QTRY_VERIFY_WITH_TIMEOUT(!manager->property("visible").toBool(), 5000);
            const auto presetId = audio.providerId() == "echomusic-viper"
                                      ? QStringLiteral("kugou-vinyl")
                                      : QStringLiteral("gain");
            audio.selectProviderPreset(presetId);
            QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending(), 10000);
            audio.editProviderPreset(presetId);
            const auto label = audio.providerId() == "echomusic-viper"
                                   ? QStringLiteral("黑胶唱片设置")
                                   : QStringLiteral("Gain设置");
            auto *presets = namedItem(window->contentItem(), QStringLiteral("productionDspPanel"));
            QVERIFY(presets);
            int presetIndex = 0;
            const auto availablePresets = audio.providerPresets();
            while (presetIndex < availablePresets.size() &&
                   availablePresets[presetIndex].toMap().value("id") != presetId)
                ++presetIndex;
            QVERIFY(presetIndex < availablePresets.size());
            QVERIFY(QMetaObject::invokeMethod(presets, "positionViewAtIndex",
                                              Q_ARG(int, presetIndex), Q_ARG(int, 4)));
            QQuickItem *edit = nullptr;
            QTRY_VERIFY((edit = button(window->contentItem(), label)));
            QVERIFY(QMetaObject::invokeMethod(edit, "clicked"));
            QTest::qWait(100);
            auto *back = button(window->contentItem(), QStringLiteral("返回引擎预设"));
            QVERIFY(back);
            if (!capture.isEmpty())
                QVERIFY(window->grabWindow().save(capture + (mode == ThemeStore::Dark
                                                                 ? "/controls-dark.png"
                                                                 : "/controls-light.png")));
            QVERIFY(QMetaObject::invokeMethod(back, "clicked"));
        }
        for (const auto &entry : warnings)
            qWarning() << entry;
        QCOMPARE(warnings.size(), 0);
        root.reset();
        engine.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        native.shutdown();
    }
};
int main(int argc, char **argv)
{
    QQuickStyle::setStyle("Basic");
    QApplication app(argc, argv);
    DspProviderUiTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "DspProviderUiTest.moc"
