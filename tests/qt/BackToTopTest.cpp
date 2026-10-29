#include "ThemeStore.h"
#include "UiTypography.h"
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
#include <ctime>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#endif

using namespace EchoMusic;
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
struct Session
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings{directory.filePath("theme.ini")};
    ThemeStore theme{settings};
    QQmlEngine engine;
    QScopedPointer<QObject> root;
    QQuickWindow *window = nullptr;
    QStringList warnings;
    Session()
    {
        theme.setMotionEnabled(false);
        theme.attachToEngine(engine);
        QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                         [this](const QList<QQmlError> &items) {
                             for (const auto &item : items)
                                 warnings.append(item.toString());
                         });
    }
    bool create()
    {
        QQmlComponent component(&engine,
                                QUrl("qrc:/qt/qml/EchoMusic/BackToTopTests/BackToTopHarness.qml"));
        root.reset(component.create());
        if (!root) {
            qWarning().noquote() << component.errorString();
            return false;
        }
        window = qobject_cast<QQuickWindow *>(root.data());
        return window && QTest::qWaitForWindowExposed(window);
    }
    QQuickItem *item(const QString &name) const
    {
        return visual(window->contentItem(), name);
    }
    QQuickItem *control() const
    {
        return item("pageBackToTop")->property("control").value<QQuickItem *>();
    }
    void offset(qreal value)
    {
        const bool invoked =
            QMetaObject::invokeMethod(item("viewport"), "scrollTo", Q_ARG(qreal, value));
        if (!invoked)
            qFatal("Production viewport scrollTo invocation failed");
    }
    void click()
    {
        auto *button = item("backToTopButton");
        Q_ASSERT(button);
        const auto point = button->mapToScene({18, 18}).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, {}, point);
    }
    ~Session()
    {
        if (window)
            window->close();
        root.reset();
        QObject::disconnect(&engine, nullptr, &engine, nullptr);
    }
};
quint64 residentBytes()
{
#ifdef Q_OS_MACOS
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, task_info_t(&info), &count) ==
        KERN_SUCCESS)
        return info.resident_size;
#endif
    return 0;
}
} // namespace

class BackToTopTest final : public QObject
{
    Q_OBJECT
private slots:
    void thresholdAndNativeViews_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("variable-list") << 0;
        QTest::newRow("grid") << 1;
        QTest::newRow("plain") << 2;
    }
    void thresholdAndNativeViews()
    {
        QFETCH(int, kind);
        Session s;
        QVERIFY(s.create());
        s.root->setProperty("viewKind", kind);
        QTest::qWait(50);
        QVERIFY(!s.control());
        s.offset(300);
        QTRY_VERIFY(!s.control());
        s.offset(301);
        QTRY_VERIFY(s.control());
        QPointer<QQuickItem> control = s.control();
        // Changing delegate heights can legitimately rebase Qt's origin and
        // actual offset. Test the exact threshold before that independent layout.
        s.offset(500);
        s.root->setProperty("variableHeight", true);
        QTest::qWait(50);
        auto *owner = s.item("pageBackToTop");
        QSignalSpy wanted(owner, SIGNAL(wantedChanged()));
        for (int i = 0; i < 1000; ++i)
            s.offset(400 + i % 80);
        QCOMPARE(wanted.size(), 0);
        QCOMPARE(s.control(), control.data());
        s.offset(30000);
        QTest::qWait(50);
        auto *view = s.item(kind == 0 ? "list" : kind == 1 ? "grid" : "plain");
        QVERIFY(QMetaObject::invokeMethod(view, "flick", Q_ARG(qreal, 0), Q_ARG(qreal, 1300)));
        QTRY_VERIFY(view->property("moving").toBool());
        s.click();
        QTRY_VERIFY(control.isNull());
        QVERIFY(!view->property("moving").toBool());
        QTRY_VERIFY(qAbs(s.item("viewport")->property("scrollTop").toReal()) < 0.01);
        QVERIFY(s.root->property("delegateCount").toInt() < 150);
        QVERIFY2(s.warnings.isEmpty(), qPrintable(s.warnings.join('\n')));
    }
    void coordinatedViewsAndKeyboard()
    {
        Session s;
        QVERIFY(s.create());
        s.offset(1200);
        QTRY_VERIFY(s.control());
        s.root->setProperty("viewKind", 1);
        QTRY_COMPARE(s.item("viewport")->property("scrollTop").toReal(), qreal(1200));
        auto *button = s.item("backToTopButton");
        button->forceActiveFocus();
        QTest::keyClick(s.window, Qt::Key_Return);
        QTRY_VERIFY(!s.control());
        s.root->setProperty("viewKind", 0);
        QTRY_COMPARE(s.item("viewport")->property("scrollTop").toReal(), qreal(0));
        s.root->setProperty("threshold", 360);
        s.offset(360);
        QVERIFY(!s.control());
        s.offset(361);
        QTRY_VERIFY(s.control());
        s.root->setProperty("hideAction", true);
        QTRY_VERIFY(!s.control());
        QVERIFY2(s.warnings.isEmpty(), qPrintable(s.warnings.join('\n')));
    }
    void exitsReopenAndWindowLifecycle()
    {
        Session s;
        s.theme.setMotionEnabled(true);
        QVERIFY(s.create());
        s.offset(500);
        QTRY_VERIFY(s.control());
        QPointer<QQuickItem> first = s.control();
        QPointer<QObject> shadow;
        for (auto *child : first->findChildren<QObject *>())
            if (child->inherits("QQuickRectangularShadow"))
                shadow = child;
        QVERIFY(shadow);
        QTest::qWait(150);
        s.offset(0);
        QVERIFY(first);
        QVERIFY(!first->isEnabled());
        QTest::qWait(30);
        s.offset(500);
        QTest::qWait(160);
        QCOMPARE(s.control(), first.data());
        s.root->setProperty("pageActive", false);
        QTRY_VERIFY(first.isNull());
        QVERIFY(shadow.isNull());
        s.root->setProperty("pageActive", true);
        QTRY_VERIFY(s.control());
        QPointer<QQuickItem> second = s.control();
        s.window->hide();
        QTRY_VERIFY(second.isNull());
        s.window->show();
        QTRY_VERIFY(s.control());
        QPointer<QQuickItem> third = s.control();
        s.window->showMinimized();
        QTRY_VERIFY(third.isNull());
        s.window->showNormal();
        QTRY_VERIFY(s.control());
        third = s.control();
        s.offset(0);
        s.theme.setMotionEnabled(false);
        QTRY_VERIFY(third.isNull());
        s.offset(500);
        QTRY_VERIFY(s.control());
        s.item("viewport")->setProperty("visible", false);
        QTRY_VERIFY(!s.control());
        QVERIFY2(s.warnings.isEmpty(), qPrintable(s.warnings.join('\n')));
    }
    void targetReplacementAndDestruction()
    {
        Session s;
        QVERIFY(s.create());
        s.offset(500);
        QTRY_VERIFY(s.control());
        s.root->setProperty("viewKind", 2);
        QTRY_VERIFY(!s.control());
        s.offset(600);
        QTRY_VERIFY(s.control());
        QQmlComponent component(&s.engine);
        component.setData("import QtQuick; Flickable { width: 600; height: 400; "
                          "contentHeight: 10000; contentY: 700 }",
                          QUrl("qrc:/replacement-view.qml"));
        QPointer<QObject> replacement = component.create();
        QVERIFY2(replacement, qPrintable(component.errorString()));
        replacement->setParent(s.root.data());
        auto *owner = s.item("pageBackToTop");
        owner->setProperty("scrollView", QVariant::fromValue(replacement.data()));
        QVERIFY(QMetaObject::invokeMethod(owner, "returnToTop"));
        QCOMPARE(replacement->property("contentY").toReal(), qreal(0));
        replacement->setProperty("contentY", 700);
        QTRY_VERIFY(s.control());
        QPointer<QQuickItem> control = s.control();
        replacement->deleteLater();
        QTRY_VERIFY(replacement.isNull());
        QTRY_VERIFY(control.isNull());
        QVERIFY(!owner->property("scrollView").value<QObject *>());
        QVERIFY2(s.warnings.isEmpty(), qPrintable(s.warnings.join('\n')));
    }
    void rendering_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<qreal>("scale");
        for (bool dark : {false, true})
            for (qreal scale : {1., 1.25})
                QTest::newRow(qPrintable(QString("%1-%2").arg(dark ? "dark" : "light").arg(scale)))
                    << dark << scale;
    }
    void rendering()
    {
        QFETCH(bool, dark);
        QFETCH(qreal, scale);
        Session s;
        s.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
        QVERIFY(s.create());
        s.root->setProperty("uiScale", scale);
        s.offset(500);
        QTRY_VERIFY(s.control());
        auto *button = s.item("backToTopButton");
        const QRectF rect = button->mapRectToScene(button->boundingRect());
        QCOMPARE(rect.size(), QSizeF(36 * scale, 36 * scale));
        QCOMPARE(s.window->width() - rect.right(), 24 * scale);
        QCOMPARE(s.window->height() - rect.bottom(), 16 * scale);
        const auto path = qEnvironmentVariable("ECHO_BACK_TO_TOP_ARTIFACTS");
        if (!path.isEmpty()) {
            QVERIFY(QDir().mkpath(path));
            QTest::qWait(160);
            QVERIFY(s.window->grabWindow().save(
                path + QString("/%1-%2.png").arg(dark ? "dark" : "light").arg(scale)));
        }
        // grabWindow() itself queues RHI readback/repaint work. Static frame
        // accounting belongs to the separate probe, which never grabs a frame.
        QVERIFY2(s.warnings.isEmpty(), qPrintable(s.warnings.join('\n')));
    }
    void releaseResourceProbe()
    {
        if (!qEnvironmentVariableIsSet("ECHO_BACK_TO_TOP_PERF"))
            QSKIP("Opt-in Release idle CPU/RSS and repeated lifetime probe");
        Session s;
        QVERIFY(s.create());
        for (int i = 0; i < 20; ++i) {
            s.offset(500);
            QTRY_VERIFY(s.control());
            QTest::qWait(20);
            s.offset(0);
            QTRY_VERIFY(!s.control());
        }
        s.engine.collectGarbage();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto rssBefore = residentBytes();
        for (int i = 0; i < 300; ++i) {
            s.offset(500);
            QPointer<QQuickItem> control = s.control();
            QVERIFY(control);
            QTest::qWait(16);
            s.offset(0);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(control.isNull());
        }
        QTest::qWait(200);
        s.engine.collectGarbage();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto rssAfter = residentBytes();
        for (bool shown : {false, true}) {
            s.offset(shown ? 500 : 0);
            QTest::qWait(300);
            QSignalSpy frames(s.window, &QQuickWindow::frameSwapped);
            const auto start = std::clock();
            QTest::qWait(1000);
            const double cpuMs = double(std::clock() - start) * 1000 / CLOCKS_PER_SEC;
            qInfo() << "Static control" << shown << "CPU ms/1000ms" << cpuMs << "frames"
                    << frames.size();
            QCOMPARE(frames.size(), 0);
        }
        const auto api = s.window->rendererInterface()->graphicsApi();
        qInfo() << "300 create/destroy cycles RSS bytes" << rssBefore << rssAfter << "delegates"
                << s.root->property("delegateCount") << "graphics API"
                << (api == QSGRendererInterface::Metal      ? "Metal"
                    : api == QSGRendererInterface::Software ? "Software"
                                                            : "Other");
        QVERIFY(s.root->property("delegateCount").toInt() < 150);
        QVERIFY2(s.warnings.isEmpty(), qPrintable(s.warnings.join('\n')));
    }
};
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    configureUiTypography();
    BackToTopTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "BackToTopTest.moc"
