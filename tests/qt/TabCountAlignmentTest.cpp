#include "NativeWindow.h"
#include "RenderedInk.h"
#include "ThemeStore.h"
#include "UiTypography.h"
#include <QDir>
#include <QFontDatabase>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include "SettingsStore.h"
#include <QTemporaryDir>
#include <QtTest>

using namespace EchoMusic;
namespace
{
QQuickItem *find(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *item = find(child, name))
            return item;
    return nullptr;
}
} // namespace
class TabCountAlignmentTest : public QObject
{
    Q_OBJECT
private slots:
    void allVisibleLabelsAndBadgesSharePaintedCenter()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        ThemeStore theme(settings);
        theme.setMotionEnabled(false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            import EchoMusic.Theme
            Window {
                id: window
                visible: true; width: 650; height: 100
                property real sampleScale: 1
                property real labelSize: 14
                property int labelWeight: Theme.semiboldWeight
                Rectangle { anchors.fill: parent; color: Theme.main }
                EchoDetailTabs {
                    objectName: "tabs"
                    x: 20; y: 8; scale: window.sampleScale
                    transformOrigin: Item.TopLeft
                    labels: ["歌曲", "歌手", "用户", "专辑", "视频"]
                    counts: [339, 4, 1, 1, 6]
                    labelPixelSize: window.labelSize
                    labelWeight: window.labelWeight
                    onPicked: index => currentIndex = index
                }
            }
        )",
                          QUrl("qrc:/tab-count-alignment.qml"));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        const auto artifacts = qEnvironmentVariable("ECHO_TAB_ALIGNMENT_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        auto *tabs = find(window->contentItem(), "tabs");
        QVERIFY(tabs);
        const QStringList families = QFontDatabase::hasFamily("Helvetica")
                                         ? QStringList{"system-ui", "Helvetica"}
                                         : QStringList{"system-ui"};
        for (int family = 0; family < families.size(); ++family) {
            theme.setGlobalFont(families[family]);
            for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
                theme.setMode(mode);
                for (const auto scale : {1., 1.25}) {
                    root->setProperty("sampleScale", scale);
                    for (const auto size : {14, 15}) {
                        root->setProperty("labelSize", size);
                        root->setProperty("labelWeight", size == 14 ? 600 : 700);
                        for (const auto active : {0, 4}) {
                            tabs->setProperty("currentIndex", active);
                            QTest::qWait(80);
                            const auto image = window->grabWindow();
                            QVERIFY(!image.isNull());
                            const qreal ratio = qreal(image.width()) / window->width();
                            const auto key = QString("%1-%2-%3-%4-%5")
                                                 .arg(family)
                                                 .arg(int(mode))
                                                 .arg(scale)
                                                 .arg(size)
                                                 .arg(active);
                            if (!artifacts.isEmpty())
                                QVERIFY(image.save(artifacts + "/tabs-" + key + ".png"));
                            for (int index = 0; index < 5; ++index) {
                                auto *label = find(tabs, QString("detailTabLabel-%1").arg(index));
                                auto *badge = find(tabs, QString("detailTabBadge-%1").arg(index));
                                QVERIFY(label && badge && badge->isVisible());
                                auto *box = label->property("textBox").value<QQuickItem *>();
                                QVERIFY(box);
                                const qreal textCenter =
                                    box->mapToScene({0, box->height() / 2}).y();
                                const qreal badgeCenter =
                                    badge->mapToScene({0, badge->height() / 2}).y();
                                const auto titleInk = Test::renderedInkCenter(
                                    image, window, box, label->property("color").value<QColor>());
                                const auto countInk = Test::renderedInkCenter(
                                    image, window, badge, theme.text(), {3, 3, 3, 3});
                                QVERIFY(titleInk && countInk);
                                qInfo()
                                    << key << index << "box centers" << textCenter << badgeCenter
                                    << "painted centers" << *titleInk << *countInk;
                                // Exact row centers; compare the two painted
                                // foregrounds within one physical pixel. Ink
                                // edges against a fractional box center also
                                // include half a pixel of edge quantization.
                                QVERIFY2(qAbs(textCenter - badgeCenter) < .001,
                                         qPrintable("row centers differ: " + key));
                                if (QGuiApplication::platformName() != "offscreen") {
                                    QVERIFY2(qAbs(*titleInk - badgeCenter) * ratio <= 1.5 + 1e-4,
                                             qPrintable(QString("%1/%2 painted title %3 badge %4")
                                                            .arg(key)
                                                            .arg(index)
                                                            .arg(*titleInk)
                                                            .arg(badgeCenter)));
                                    QVERIFY(qAbs(*countInk - badgeCenter) * ratio <= 1.5 + 1e-4);
                                    QVERIFY(qAbs(*titleInk - *countInk) * ratio <= 1 + 1e-4);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
};
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    configureUiTypography();
    TabCountAlignmentTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "TabCountAlignmentTest.moc"
