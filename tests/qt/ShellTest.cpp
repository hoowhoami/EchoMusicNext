#include <QJsonArray>
#include "AccountUiFixture.h"
#include "QrLoginTestAccess.h"
#include <QBuffer>
#include <QImage>
#include "RouteController.h"
#include "NativeWindow.h"
#include "SearchModel.h"
#include "SearchRouteCoordinator.h"
#include "ShellSettings.h"
#include "ShellController.h"
#include "ThemeStore.h"
#include "UiTypography.h"
#include "WindowController.h"
#include <QAbstractItemModelTester>
#include <QFont>
#include <QDir>
#include <QPointer>
#include <QScopeGuard>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QSignalSpy>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include "BrowserVerificationSession.h"
#include <QtTest>
#ifdef Q_OS_MACOS
#include <thread>
#include <atomic>
#endif
using namespace EchoMusic;
namespace EchoMusic
{
// Canonical UI-only catalog rows; no provider response or account is fabricated.
class UserPlaylistModelTestAccess
{
public:
    static void pinned(UserPlaylistModel &model)
    {
        model.beginResetModel();
        model.m_rows.clear();
        for (quint32 kind : {1u, 2u}) {
            UserPlaylistModel::Row row;
            row.globalId = QString("sidebar-test-%1").arg(kind);
            row.title = kind == 1 ? QStringLiteral("默认歌单") : QStringLiteral("我喜欢的音乐");
            row.defaultKind = kind;
            row.sourceCode = 1;
            model.m_rows.append(row);
        }
        model.endResetModel();
        emit model.changed();
    }
};
// Canonical control-flow seam. capabilities() has no upstream HTTP and these
// values do not serve as provider samples or successful login evidence.
class ApiClientTestAccess
{
public:
    static quint64 holdCandidate(ApiClient &api, quint64 user)
    {
        for (auto it = api.m_pending.cbegin(); it != api.m_pending.cend(); ++it) {
            if (it->request.value("operation") != QStringLiteral("user_detail") ||
                it->request.value("identity").toObject().value("userid").toInteger() !=
                    qint64(user))
                continue;
            auto pending = *it;
            const auto native = it.key();
            api.cancelRequest(native);
            static quint64 sequence = quint64(1) << 61;
            const auto held = ++sequence;
            pending.logicalId = held;
            api.m_pending.insert(held, pending);
            return held;
        }
        return 0;
    }
    static quint64 operation(ApiClient &api, const QString &name)
    {
        for (auto it = api.m_pending.cbegin(); it != api.m_pending.cend(); ++it)
            if (it->request.value("operation").toString() == name)
                return it.key();
        return 0;
    }
    static void reply(ApiClient &api, quint64 request, int status, const QByteArray &payload)
    {
        api.deliver(request, api.sessionRevision(), status, payload);
    }
    static void challenge(ApiClient &api, quint64 request)
    {
        reply(api, request, 5,
              R"({"kind":"challenge","challenge":{"event_id":"switch-control-flow"}})");
    }
    static int pendingCount(const ApiClient &api)
    {
        return api.m_pending.size();
    }
};
class SavedAccountModelTestAccess
{
public:
    static void holdValidation(SavedAccountModel &saved, quint64 id)
    {
        saved.m_operation->validationRequest = id;
    }
};
class AccountControllerTestAccess
{
public:
    static void enableOnline(AccountController &controller)
    {
        controller.m_online = true;
    }
    static quint64 holdSms(AccountController &controller, ApiClient &api, bool sending)
    {
        controller.m_online = true;
        controller.m_smsCountdown = 0;
        controller.m_smsTimer.stop();
        const auto id = api.capabilities();
        if (sending)
            controller.m_sendSms = id;
        else
            controller.m_smsLogin = id;
        emit controller.changed();
        return id;
    }
};
} // namespace EchoMusic
namespace
{
RouteController::Definition definition(QString name, bool cached = true)
{
    return {name,
            "/" + name + "/:id",
            QUrl("qrc:/qt/qml/EchoMusic/ShellTests/ProbePage.qml"),
            {"tab"},
            cached};
}
void drain()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}
QQuickItem *findVisual(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = findVisual(child, name))
            return found;
    return nullptr;
}
QQuickItem *findVisibleVisual(QQuickItem *root, const QString &name)
{
    if (!root->isVisible())
        return nullptr;
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = findVisibleVisual(child, name))
            return found;
    return nullptr;
}
QObject *probe(QObject *root, RouteContext *context)
{
    return findVisual(qobject_cast<QQuickItem *>(root), "probe-" + context->instanceId());
}
} // namespace
class ShellTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QQuickStyle::setStyle("Basic");
        configureUiTypography();
    }
    void sidebarLongPlaylistTitleKeepsItsLineOnHover()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore preferences(directory.filePath("settings.ini"));
        ThemeStore theme(preferences);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import EchoMusic.Library
            import EchoMusic.Theme
            Window {
                width: 360; height: 180; visible: true
                color: Theme.shell
                Item {
                    objectName: "rowHost"; x: 20; y: 20
                    SidebarPlaylistRow {
                        width: 184
                        title: "很长的中文歌单名称以及 English playlist title 0123456789"
                        coverUrl: ""; cloudListId: 7; globalId: "probe"
                        creatorGlobalId: "owner"; kind: "owned"; defaultKind: 0
                    }
                }
            }
        )",
                          QUrl());
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window && Test::prepareNativeInput(window));
        auto *host = findVisual(window->contentItem(), "rowHost");
        auto *row = findVisual(host, "sidebarPlaylistRow");
        auto *label = findVisual(row, "sidebarPlaylistTitle");
        auto *edit = findVisual(row, "sidebarPlaylistEdit");
        QVERIFY(row && label && edit);
        auto *text = label->property("textItem").value<QQuickItem *>();
        QVERIFY(text);
        const auto render = [&] {
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
            window->update();
            return QTest::qWaitFor([&] { return !frames.isEmpty(); });
        };
        const auto inkCenter = [&] {
            const auto projected = label->property("firstLineInkCenter");
            double value = projected.toDouble();
            if (!projected.isValid() && !QMetaObject::invokeMethod(label, "firstLineInkCenter",
                                                                   Q_RETURN_ARG(double, value)))
                return qQNaN();
            return label->mapToScene({0, value}).y();
        };
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (qreal scale : {1., 1.25}) {
                host->setScale(scale);
                for (const auto &title :
                     {QStringLiteral("很长的中文歌单名称以及 English playlist title"),
                      QStringLiteral("A very long playlist with gypq and 0123456789"),
                      QStringLiteral("xxxxxxxxxxxx ÅÉgypq ZZZZZZZZZZZZZZZZZ")}) {
                    row->setProperty("title", title);
                    for (int width : {166, 184, 200}) {
                        row->setWidth(width);
                        Test::moveNativePointer(window, {340, 160});
                        QTRY_VERIFY(!row->property("hovered").toBool());
                        QVERIFY(render());
                        const auto height = label->height();
                        const auto center = label->mapToScene({0, label->height() / 2}).y();
                        const auto glyphCenter = inkCenter();
                        const auto baseline =
                            text->mapToScene({0, text->property("baselineOffset").toReal()}).y();
                        QVERIFY(qIsFinite(glyphCenter));
                        const auto textWidth = text->width();
                        QCOMPARE(text->property("lineCount").toInt(), 1);
                        Test::moveNativePointer(window, row->mapToScene({10, 20}).toPoint());
                        QTRY_VERIFY(edit->isVisible());
                        QVERIFY(render());
                        QCOMPARE(label->height(), height);
                        QVERIFY(qAbs(label->mapToScene({0, label->height() / 2}).y() - center) <
                                .01);
                        QVERIFY(qAbs(inkCenter() - glyphCenter) < .01);
                        // Centering the newly elided glyph bounds can keep the
                        // ink center stable while moving the shared prefix.
                        QVERIFY(
                            qAbs(text->mapToScene({0, text->property("baselineOffset").toReal()})
                                     .y() -
                                 baseline) < .01);
                        QCOMPARE(text->property("lineCount").toInt(), 1);
                        QVERIFY(text->width() < textWidth);
                        const auto textRight = text->mapToScene({text->width(), 0}).x();
                        QVERIFY(textRight <= edit->mapToScene({0, 0}).x());
                        const auto captures = qEnvironmentVariable("ECHO_SIDEBAR_ROW_CAPTURE_DIR");
                        if (!captures.isEmpty() && width == 184) {
                            QVERIFY(QDir().mkpath(captures));
                            QVERIFY(window->grabWindow().save(
                                captures + QString("/row-%1-%2-%3.png")
                                               .arg(int(mode))
                                               .arg(scale)
                                               .arg(title.startsWith("A") ? "latin" : "mixed")));
                        }
                    }
                }
            }
        }
        auto *remove = findVisual(row, "sidebarPlaylistRemove");
        QVERIFY(remove);
        QSignalSpy edits(row, SIGNAL(editRequested(double)));
        QSignalSpy removals(row, SIGNAL(removeRequested(double, QString, bool)));
        host->setScale(1);
        row->setWidth(184);
        for (const auto &kind : {QStringLiteral("owned"), QStringLiteral("collected")}) {
            row->setProperty("kind", kind);
            Test::moveNativePointer(window, row->mapToScene({10, 20}).toPoint());
            QVERIFY(render());
            QCOMPARE(edit->isVisible(), kind == "owned");
            QVERIFY(remove->isVisible());
            QCOMPARE(remove->property("text").toString(),
                     kind == "owned" ? QStringLiteral("删除歌单") : QStringLiteral("取消收藏"));
            QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
                              row->mapToScene({10, 20}).toPoint());
            row->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window, Qt::Key_Menu);
            QTest::keyClick(window, Qt::Key_F10, Qt::ShiftModifier);
            QCOMPARE(edits.count(), 0);
            if (kind == "owned") {
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                                  edit->mapToScene(edit->boundingRect().center()).toPoint());
                QCOMPARE(edits.count(), 1);
                edits.clear();
            }
        }
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          remove->mapToScene(remove->boundingRect().center()).toPoint());
        QCOMPARE(removals.count(), 1);
        QCOMPARE(removals.first().at(0).toDouble(), 7.);
        QCOMPARE(removals.first().at(2).toBool(), false);
        QCOMPARE(edits.count(), 0);
    }
    void smsRequestsShareOneSubmissionLifetime()
    {
        ApiClient api(DeviceIdentity::temporary());
        Test::AccountUiFixture accountUi(api);
        auto &controller = accountUi.controller;
        controller.openLogin();
        controller.setMethod(1);
        for (const bool sending : {true, false}) {
            const auto id = AccountControllerTestAccess::holdSms(controller, api, sending);
            const auto count = ApiClientTestAccess::pendingCount(api);
            QCOMPARE(controller.sendingSms(), sending);
            QCOMPARE(controller.busy(), !sending);
            controller.sendSms(QStringLiteral("13800000000"));
            controller.submitSms(QStringLiteral("13800000000"), QStringLiteral("unit-code"));
            QCOMPARE(ApiClientTestAccess::pendingCount(api), count);
            QCOMPARE(controller.sendingSms(), sending);
            QCOMPARE(controller.busy(), !sending);
            QVERIFY(api.cancel(id));
            QTRY_VERIFY(!controller.sendingSms() && !controller.busy());
        }
        controller.closeLogin();
    }
    void liveQrMethodSwitchesCreateFreshSessions()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_QR_SWITCH"))
            QSKIP("Opt-in anonymous create/switch/cancel only; no account login");
        ApiClient api(DeviceIdentity::temporary());
        int expiries = 0;
        Test::AccountUiFixture fixture(api);
        AccountControllerTestAccess::enableOnline(fixture.controller);
        fixture.controller.openLogin();
        for (auto *qr :
             {fixture.controller.qr(), fixture.controller.qq(), fixture.controller.wechat()})
            connect(qr, &QrLoginService::changed, this,
                    [qr, &expiries] { expiries += qr->state() == QrLoginService::State::Expired; });
        for (int method : {0, 3, 4, 0, 3, 4}) {
            fixture.controller.setMethod(method);
            auto *active = method == 0   ? fixture.controller.qr()
                           : method == 3 ? fixture.controller.qq()
                                         : fixture.controller.wechat();
            QCOMPARE(active->state(), QrLoginService::State::Creating);
            QVERIFY(active->imageDataUrl().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(active->state() != QrLoginService::State::Creating, 65000);
            QCOMPARE(active->state(), QrLoginService::State::Waiting);
            QVERIFY(!active->imageDataUrl().isEmpty());
            for (auto *qr :
                 {fixture.controller.qr(), fixture.controller.qq(), fixture.controller.wechat()})
                if (qr != active) {
                    QCOMPARE(qr->state(), QrLoginService::State::Idle);
                    QVERIFY(qr->imageDataUrl().isEmpty());
                }
        }
        // Switch again while creation has no chance to complete on the GUI.
        fixture.controller.setMethod(0);
        fixture.controller.setMethod(3);
        fixture.controller.setMethod(4);
        fixture.controller.setMethod(0);
        QTRY_COMPARE_WITH_TIMEOUT(fixture.qr.state(), QrLoginService::State::Waiting, 65000);
        QCOMPARE(expiries, 0);
        fixture.controller.closeLogin();
        QCOMPARE(fixture.qr.state(), QrLoginService::State::Idle);
        QSignalSpy closed(&api, &ApiClient::closed);
        api.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void productionLoginSurfaceUsesOnlyItsBackground()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore preferences(directory.filePath("settings.ini"));
        ThemeStore theme(preferences);
        ShellSettings shell(preferences);
        shell.setMotionEnabled(false);
        WindowController windows(preferences, false);
        ApiClient api(DeviceIdentity::temporary());
        Test::AccountUiFixture accountUi(api);
        SearchModel search(api);
        RouteController routes;
        SearchRouteCoordinator searchRoutes(search, routes, false);
        ShellController shellController(preferences, routes);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/App/Main.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"libraryController", QVariant::fromValue(&accountUi.library)},
             {"accountController", QVariant::fromValue(&accountUi.controller)},
             {"shellController", QVariant::fromValue(&shellController)},
             {"searchService", QVariant::fromValue(&search)},
             {"router", QVariant::fromValue(&routes)},
             {"shellSettings", QVariant::fromValue(&shell)},
             {"windowService", QVariant::fromValue(&windows)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        windows.attach(window);
        window->resize(1400, 1000);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        qInfo() << "Login rendering platform" << QGuiApplication::platformName() << "graphics API"
                << window->rendererInterface()->graphicsApi();
        const auto artifacts = qEnvironmentVariable("ECHO_ACCOUNT_ARTIFACTS");
        if (!artifacts.isEmpty())
            QDir().mkpath(artifacts);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (const auto scale : {1.0, 1.25}) {
                shell.resetZoom();
                while (shell.uiScale() < scale - 0.001)
                    shell.zoomIn();
                for (const bool frost : {false, true}) {
                    theme.setFloatingFrosted(frost);
                    for (int cycle = 0; cycle < 2; ++cycle) {
                        accountUi.controller.openLogin();
                        auto *card = findVisibleVisual(window->contentItem(), "loginCard");
                        QVERIFY(card);
                        QTest::qWait(150);
                        auto *backdrop = window->property("echoBackdropRoot").value<QQuickItem *>();
                        QVERIFY(backdrop);
                        QCOMPARE(backdrop->objectName(), QString("loginBackdropScene"));
                        QCOMPARE(card->property("hasBackdrop").toBool(), frost);
                        const auto image = window->grabWindow();
                        QVERIFY(!image.isNull());
                        const auto point = card->mapToScene({12, card->height() / 2});
                        const auto pixel =
                            image.pixelColor(qRound(point.x() * image.width() / window->width()),
                                             qRound(point.y() * image.height() / window->height()));
                        if (!artifacts.isEmpty() && cycle == 0)
                            QVERIFY(image.save(artifacts + QString("/login-%1-%2-frost%3.png")
                                                               .arg(int(mode))
                                                               .arg(scale)
                                                               .arg(frost)));
                        const auto expected = theme.elevated();
                        qInfo() << "Login surface" << mode << scale << frost << cycle << "pixel"
                                << pixel << "expected" << expected;
                        QVERIFY2(qAbs(pixel.red() - expected.red()) <= 3 &&
                                     qAbs(pixel.green() - expected.green()) <= 3 &&
                                     qAbs(pixel.blue() - expected.blue()) <= 3,
                                 "Login surface must not sample its own card/effect graph");
                        accountUi.controller.closeLogin();
                        QTRY_VERIFY(!findVisibleVisual(window->contentItem(), "loginCard"));
                        QTRY_COMPARE(window->property("echoBackdropRoot")
                                         .value<QQuickItem *>()
                                         ->objectName(),
                                     QString("shellFrame"));
                    }
                }
            }
        }
        root.reset();
        api.shutdown();
    }
    void browserVerificationClosesOnlyItsOwnDialog()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore preferences(directory.filePath("settings.ini"));
        ThemeStore theme(preferences);
        theme.setMotionEnabled(false);
        ShellSettings shell(preferences);
        WindowController windows(preferences, false);
        ApiClient api(DeviceIdentity::temporary());
        QTcpServer proxy;
        QVERIFY(proxy.listen(QHostAddress::LocalHost));
        QVERIFY(api.setNetworkTransport(proxy.serverPort()));
        Test::AccountUiFixture accountUi(api);
        connect(accountUi.controller.verification(),
                &VerificationService::returnToApplicationRequested, &windows,
                &WindowController::showWindow);
        SearchModel search(api);
        RouteController routes;
        ShellController shellController(preferences, routes);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/App/Main.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"libraryController", QVariant::fromValue(&accountUi.library)},
             {"accountController", QVariant::fromValue(&accountUi.controller)},
             {"shellController", QVariant::fromValue(&shellController)},
             {"searchService", QVariant::fromValue(&search)},
             {"router", QVariant::fromValue(&routes)},
             {"shellSettings", QVariant::fromValue(&shell)},
             {"windowService", QVariant::fromValue(&windows)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        windows.attach(window);
        window->resize(1400, 900);
        QVERIFY(Test::prepareNativeInput(window));
        auto *page = findVisibleVisual(window->contentItem(), "shellContent");
        QVERIFY(page);
        QQmlComponent recipe(&engine);
        recipe.setData(R"(
import QtQuick
import EchoMusic.Controls
QtObject {
 id: host
 required property Item page
 property alias formOwner: formOwner
 property alias drawerOwner: drawerOwner
 property int closedCount: 0
 property EchoPopupLoader form: EchoPopupLoader { id: formOwner; hostItem: host.page
  sourceComponent: EchoDialog { heading: "业务表单"; managedDismiss: true
   onDismissRequested: close()
   onClosed: host.closedCount++
   body: EchoTextField { objectName: "businessVerificationDraft"; text: "保留未保存草稿" }
  }
 }
 property EchoPopupLoader drawer: EchoPopupLoader { id: drawerOwner; hostItem: host.page
  sourceComponent: EchoDrawer { onClosed: host.closedCount++; contentItem: Text { text: "业务抽屉" } }
 }
})",
                       QUrl());
        QScopedPointer<QObject> business(
            recipe.createWithInitialProperties({{"page", QVariant::fromValue(page)}}));
        QVERIFY2(business, qPrintable(recipe.errorString()));
        auto *formOwner = business->property("formOwner").value<QObject *>();
        auto *drawerOwner = business->property("drawerOwner").value<QObject *>();
        QVERIFY(formOwner && drawerOwner);
        QVERIFY(QMetaObject::invokeMethod(formOwner, "open", Q_ARG(QVariant, QVariantMap{})));
        QVERIFY(QMetaObject::invokeMethod(drawerOwner, "open", Q_ARG(QVariant, QVariantMap{})));
        QPointer<QObject> form = formOwner->property("item").value<QObject *>();
        QPointer<QObject> drawer = drawerOwner->property("item").value<QObject *>();
        QVERIFY(form && drawer);
        QTRY_VERIFY(form->property("opened").toBool() && drawer->property("opened").toBool());
        auto *draft = form->findChild<QObject *>("businessVerificationDraft");
        QVERIFY(draft);
        for (const bool cancel : {false, true}) {
            auto *verification = accountUi.controller.verification();
            const auto original = api.userDetail(); // Anonymous identity; HTTP stays on loopback.
            ApiClientTestAccess::challenge(api, original);
            const auto info = ApiClientTestAccess::operation(api, "get_verify_info");
            QVERIFY(info);
            ApiClientTestAccess::reply(
                api, info, 0,
                R"({"provider":"tencent","verification_type":23,"app_id":"197787253"})");
            QTRY_VERIFY(verification->open());
            QTRY_VERIFY(!page->isEnabled());
            QTRY_VERIFY(!form->property("enabled").toBool());
            QPointer<QObject> verificationDialog;
            QTRY_VERIFY((verificationDialog = root->findChild<QObject *>("verificationDialog")));
            QTRY_VERIFY(verificationDialog->property("opened").toBool());
            QVERIFY(verificationDialog->property("enabled").toBool());
            QTest::keyClick(window, Qt::Key_Escape);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(8, 8));
            QVERIFY(verification->open());
            QCOMPARE(formOwner->property("item").value<QObject *>(), form.data());
            QCOMPARE(drawerOwner->property("item").value<QObject *>(), drawer.data());
            QCOMPARE(business->property("closedCount").toInt(), 0);
            if (cancel) {
                verification->cancel();
            } else {
                // Exercise the production loopback callback without opening an
                // external browser or claiming a real provider proof/login.
                auto *browser = verification->findChild<BrowserVerificationSession *>();
                QVERIFY(browser);
                const auto url = browser->start();
                QVERIFY(!url.isEmpty());
                const QByteArray proof =
                    R"({"ret":0,"ticket":"generated-control-proof","randstr":"generated"})";
                const auto authority = url.authority().toLatin1();
                const QByteArray request =
                    "POST /result HTTP/1.1\r\nHost: " + authority + "\r\nOrigin: http://" +
                    authority + "\r\nX-Echo-Verification: " + url.fragment().toLatin1() +
                    "\r\nContent-Type: application/json\r\nContent-Length: " +
                    QByteArray::number(proof.size()) + "\r\n\r\n" + proof;
                QTcpSocket peer;
                peer.setProxy(QNetworkProxy::NoProxy);
                peer.connectToHost(url.host(), quint16(url.port()));
                QTRY_COMPARE(peer.state(), QAbstractSocket::ConnectedState);
                QCOMPARE(peer.write(request), qint64(request.size()));
                QTRY_VERIFY(ApiClientTestAccess::operation(api, "sidedt"));
                const auto submitted = ApiClientTestAccess::operation(api, "sidedt");
                ApiClientTestAccess::reply(api, submitted, 0, R"({"acknowledged":true})");
            }
            QTRY_VERIFY(!verification->open());
            QTRY_VERIFY(verificationDialog.isNull());
            QTRY_VERIFY(page->isEnabled());
            QVERIFY(form && drawer);
            QVERIFY(form->property("visible").toBool());
            QVERIFY(drawer->property("visible").toBool());
            QTRY_VERIFY(form->property("enabled").toBool());
            QCOMPARE(draft->property("text").toString(), QString("保留未保存草稿"));
            QCOMPARE(business->property("closedCount").toInt(), 0);
        }
        QVERIFY(QMetaObject::invokeMethod(formOwner, "close"));
        QVERIFY(QMetaObject::invokeMethod(drawerOwner, "close"));
        QTRY_VERIFY(form.isNull() && drawer.isNull());
        // Search also has focus-based dismissal outside the shared owner.
        auto *searchInput = findVisibleVisual(window->contentItem(), "searchInput");
        QVERIFY(searchInput);
        auto *searchBox = searchInput->parentItem()->parentItem();
        searchInput->setProperty("text", QString("保留搜索词"));
        QVERIFY(QMetaObject::invokeMethod(searchBox, "focusSearch"));
        QPointer<QObject> suggestions;
        QTRY_VERIFY((suggestions = root->findChild<QObject *>("searchSuggestionsPopup")));
        QTRY_VERIFY(suggestions->property("opened").toBool());
        const auto searchVerification = api.userDetail();
        ApiClientTestAccess::challenge(api, searchVerification);
        const auto searchInfo = ApiClientTestAccess::operation(api, "get_verify_info");
        QVERIFY(searchInfo);
        ApiClientTestAccess::reply(
            api, searchInfo, 0,
            R"({"provider":"tencent","verification_type":23,"app_id":"197787253"})");
        QTRY_VERIFY(accountUi.controller.verification()->open());
        QVERIFY(suggestions && suggestions->property("visible").toBool());
        accountUi.controller.verification()->cancel();
        QTRY_VERIFY(!accountUi.controller.verification()->open());
        QTest::qWait(50); // Flush the search box's queued outside-focus dismissal.
        QVERIFY(suggestions && suggestions->property("visible").toBool());
        QCOMPARE(searchInput->property("text").toString(), QString("保留搜索词"));
        QVERIFY(QMetaObject::invokeMethod(searchBox, "dismissSearch"));
        QTRY_VERIFY(suggestions.isNull());
        QCOMPARE(warnings.size(), 0);
        routes.shutdown();
        windows.shutdown();
    }
    void productionAccountSwitchValidationAndCancellation()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore preferences(directory.filePath("settings.ini"));
        ThemeStore theme(preferences);
        ShellSettings shell(preferences);
        shell.setMotionEnabled(false);
        WindowController windows(preferences, false);
        ApiClient api(DeviceIdentity::temporary());
        QTcpServer proxy;
        QVERIFY(proxy.listen(QHostAddress::LocalHost));
        QVERIFY(api.setNetworkTransport(proxy.serverPort()));
        Test::AccountUiFixture accountUi(api);
        QSignalSpy ready(&accountUi.storage, &StorageService::accountsReady);
        accountUi.storage.initialize(directory.filePath("accounts.sqlite"));
        QTRY_COMPARE(ready.size(), 1);
        const auto authorization = [](qint64 id) {
            return QJsonDocument(QJsonObject{{"user_id", id},
                                             {"token", "control-flow-token"},
                                             {"nickname", QString("Account %1").arg(id)}})
                .toJson();
        };
        QVERIFY(accountUi.account.adoptQrAuthorization(authorization(1)));
        QTRY_VERIFY(!accountUi.saved.busy());
        QVERIFY(accountUi.account.adoptQrAuthorization(authorization(2)));
        QTRY_VERIFY(!accountUi.saved.busy());
        SearchModel search(api);
        RouteController routes;
        ShellController shellController(preferences, routes);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/App/Main.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"libraryController", QVariant::fromValue(&accountUi.library)},
             {"accountController", QVariant::fromValue(&accountUi.controller)},
             {"shellController", QVariant::fromValue(&shellController)},
             {"searchService", QVariant::fromValue(&search)},
             {"router", QVariant::fromValue(&routes)},
             {"shellSettings", QVariant::fromValue(&shell)},
             {"windowService", QVariant::fromValue(&windows)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        windows.attach(window);
        window->resize(1400, 900);
        QVERIFY(Test::prepareNativeInput(window));
        auto visible = [&](const QString &name) {
            return findVisibleVisual(window->contentItem(), name);
        };
        auto click = [&](QQuickItem *item) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
        };
        auto *owner = root->findChild<QObject *>("accountSwitcherDialogOwner");
        QVERIFY(owner);
        QSignalSpy switched(&accountUi.saved, &SavedAccountModel::switched);
        const auto hold = [&] {
            const auto id = ApiClientTestAccess::holdCandidate(api, 1);
            if (id)
                SavedAccountModelTestAccess::holdValidation(accountUi.saved, id);
            return id;
        };
        const auto artifacts = qEnvironmentVariable("ECHO_ACCOUNT_SWITCH_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        for (const auto mode : {ThemeStore::Dark, ThemeStore::Light}) {
            theme.setMode(mode);
            for (const auto scale : {1.0, 1.25}) {
                shell.resetZoom();
                while (shell.uiScale() < scale - 0.001)
                    shell.zoomIn();
                QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
                QTRY_VERIFY(visible("savedAccountAction"));
                QTRY_VERIFY(owner->property("opened").toBool());
                click(visible("savedAccountAction"));
                const auto held = hold();
                QVERIFY(held);
                QTRY_VERIFY(visible("savedAccountAction")->property("loading").toBool());
                QCOMPARE(visible("savedAccountAction")->property("text").toString(),
                         QString("正在验证…"));
                QCOMPARE(accountUi.account.userId(), quint64(2));
                if (!artifacts.isEmpty()) {
                    QTest::qWait(30);
                    QVERIFY(window->grabWindow().save(
                        artifacts + QString("/loading-%1-%2.png").arg(int(mode)).arg(scale)));
                }
                ApiClientTestAccess::reply(api, held, 5, R"({"kind":"authentication"})");
                QTRY_VERIFY(!accountUi.saved.busy());
                QTRY_COMPARE(visible("savedAccountAction")->property("text").toString(),
                             QString("重新登录"));
                QVERIFY(visible("savedAccountSwitchError"));
                QCOMPARE(accountUi.account.state(), AccountService::State::SignedIn);
                if (!artifacts.isEmpty()) {
                    QTest::qWait(30);
                    QVERIFY(window->grabWindow().save(
                        artifacts + QString("/expired-%1-%2.png").arg(int(mode)).arg(scale)));
                }
                click(visible("savedAccountAction"));
                QTRY_VERIFY(accountUi.controller.loginOpen());
                QTRY_VERIFY(!visible("savedAccountAction"));
                QTRY_VERIFY(visible("accountReloginPrompt"));
                QrLoginServiceTestAccess::authorize(*accountUi.controller.qr(), authorization(3));
                QCOMPARE(accountUi.account.userId(), quint64(2));
                QVERIFY(accountUi.controller.errorMessage().contains("不一致"));
                QTRY_VERIFY(visible("loginAccountError"));
                QVERIFY(
                    visible("loginAccountError")->property("text").toString().contains("不一致"));
                if (!artifacts.isEmpty()) {
                    QTest::qWait(30);
                    QVERIFY(window->grabWindow().save(
                        artifacts + QString("/relogin-%1-%2.png").arg(int(mode)).arg(scale)));
                }
                accountUi.controller.closeLogin();
                QCOMPARE(accountUi.account.userId(), quint64(2));
                QTRY_VERIFY(owner->property("item").value<QObject *>() == nullptr);
                // A direct retry clears the expired marker and starts a new owner operation.
                QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
                QTRY_VERIFY(visible("savedAccountAction"));
                QVERIFY(accountUi.saved.switchAccount(1));
                const auto cancelled = hold();
                QVERIFY(cancelled);
                QVERIFY(QMetaObject::invokeMethod(owner, "close"));
                QTRY_VERIFY(!accountUi.saved.busy());
                emit api.succeeded(cancelled, R"({"id":1})");
                QCOMPARE(accountUi.account.userId(), quint64(2));
                QCOMPARE(switched.size(), 0);
                QTRY_VERIFY(owner->property("item").value<QObject *>() == nullptr);
            }
        }
        // A candidate challenge disables the shell, but must retain the switcher
        // and candidate until the verification dialog is explicitly cancelled.
        QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
        QTRY_VERIFY(visible("savedAccountAction"));
        click(visible("savedAccountAction"));
        const auto challenged = hold();
        QVERIFY(challenged);
        ApiClientTestAccess::challenge(api, challenged);
        QTRY_VERIFY(accountUi.controller.verification()->open());
        QVERIFY(owner->property("visible").toBool());
        QCOMPARE(accountUi.saved.switchingUserId(), quint64(1));
        accountUi.controller.verification()->cancel();
        QTRY_VERIFY(!accountUi.saved.busy());
        QCOMPARE(accountUi.account.userId(), quint64(2));
        QVERIFY(QMetaObject::invokeMethod(owner, "close"));
        QTRY_VERIFY(owner->property("item").value<QObject *>() == nullptr);
        QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
        QTRY_VERIFY(visible("savedAccountAction"));
        click(visible("savedAccountAction"));
        const auto loginChallenge = hold();
        QVERIFY(loginChallenge);
        ApiClientTestAccess::challenge(api, loginChallenge);
        const auto info = ApiClientTestAccess::operation(api, "get_verify_info");
        QVERIFY(info);
        ApiClientTestAccess::reply(api, info, 0, R"({"verification_type":38,"provider":"login"})");
        QCOMPARE(accountUi.controller.verification()->verificationType(), quint32(38));
        accountUi.controller.verification()->beginLogin();
        QVERIFY(accountUi.controller.loginOpen());
        QCOMPARE(accountUi.account.userId(), quint64(2));
        QVERIFY(accountUi.controller.loginPrompt().contains("Account 1"));
        QrLoginServiceTestAccess::authorize(*accountUi.controller.qr(), authorization(1));
        QCOMPARE(accountUi.account.userId(), quint64(1));
        QVERIFY(!accountUi.controller.loginOpen());
        QTRY_VERIFY(!accountUi.saved.busy());
        QCOMPARE(warnings.size(), 0);
        routes.shutdown();
        windows.shutdown();
    }
    void productionAccountEntryOverlayAndDraftLifetime()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore preferences(directory.filePath("settings.ini"));
        ThemeStore theme(preferences);
        ShellSettings shell(preferences);
        shell.setMotionEnabled(false);
        WindowController windows(preferences, false);
        ApiClient api(DeviceIdentity::temporary());
        Test::AccountUiFixture accountUi(api);
        SearchModel search(api);
        RouteController routes;
        SearchRouteCoordinator searchRoutes(search, routes, false);
        routes.navigate("search");
        ShellController shellController(preferences, routes);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/App/Main.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"libraryController", QVariant::fromValue(&accountUi.library)},
             {"accountController", QVariant::fromValue(&accountUi.controller)},
             {"shellController", QVariant::fromValue(&shellController)},
             {"searchService", QVariant::fromValue(&search)},
             {"router", QVariant::fromValue(&routes)},
             {"shellSettings", QVariant::fromValue(&shell)},
             {"windowService", QVariant::fromValue(&windows)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        windows.attach(window);
        window->resize(1400, 900);
        QVERIFY(Test::prepareNativeInput(window));
        auto visible = [&](const QString &name) {
            return findVisibleVisual(window->contentItem(), name);
        };
        auto click = [&](QQuickItem *item) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
        };
        auto *route = routes.current();
        // The offscreen platform's input helper does not wait for exposure;
        // wait for production layouts before calculating the hover coordinate.
        QSignalSpy initialFrames(window, &QQuickWindow::frameSwapped);
        window->update();
        QTRY_VERIFY(!initialFrames.isEmpty());
        const auto nativeTipCount = [&] {
            QSet<QObject *> visited;
            int count = 0;
            const auto visit = [&](auto &&self, QObject *object) -> void {
                if (!object || visited.contains(object))
                    return;
                visited.insert(object);
                count += object->inherits("QQuickToolTip");
                for (auto *child : object->children())
                    self(self, child);
                if (auto *item = qobject_cast<QQuickItem *>(object))
                    for (auto *child : item->childItems())
                        self(self, child);
            };
            visit(visit, root.data());
            return count;
        };
        const auto nativeBusinessPopups = [&] {
            QSet<QObject *> visited;
            QList<QObject *> found;
            const auto visit = [&](auto &&self, QObject *object) -> void {
                if (!object || visited.contains(object))
                    return;
                visited.insert(object);
                if (object->inherits("QQuickPopup") && !object->inherits("QQuickToolTip"))
                    found.append(object);
                for (auto *child : object->children())
                    self(self, child);
                if (auto *item = qobject_cast<QQuickItem *>(object))
                    for (auto *child : item->childItems())
                        self(self, child);
            };
            visit(visit, root.data());
            return found;
        };
        qInfo() << "Production Main idle business Popups:" << nativeBusinessPopups().size();
        QCOMPARE(nativeBusinessPopups().size(), 0);
        for (int cycle = 0; cycle < 3; ++cycle) {
            auto *trigger = visible("sidebarAccountTrigger");
            QVERIFY(trigger);
            click(trigger);
            QTRY_COMPARE(nativeBusinessPopups().size(), 1);
            QPointer<QObject> retired = nativeBusinessPopups().first();
            QTRY_VERIFY(retired->property("opened").toBool());
            QVERIFY(QMetaObject::invokeMethod(retired, "close"));
            QTRY_VERIFY(retired.isNull());
            QTRY_COMPARE(nativeBusinessPopups().size(), 0);
        }
        Test::moveNativePointer(window, {400, 15});
        QTest::qWait(550);
        qInfo() << "Production Main idle native ToolTips:" << nativeTipCount();
        QCOMPARE(nativeTipCount(), 0);
        for (const auto &name : {"titlebarBackAction", "titlebar-action-recognize"}) {
            auto *disabled = visible(name);
            QVERIFY2(disabled && !disabled->isEnabled(),
                     qPrintable(QString("%1 exists=%2 enabled=%3 back=%4 route=%5")
                                    .arg(name)
                                    .arg(disabled != nullptr)
                                    .arg(disabled ? disabled->isEnabled() : false)
                                    .arg(routes.canGoBack())
                                    .arg(routes.current()->name())));
            QSignalSpy clicks(disabled, SIGNAL(clicked()));
            Test::moveNativePointer(window, disabled->mapToScene({17, 17}).toPoint());
            QTRY_VERIFY(disabled->property("hoverHighlighted").toBool());
            auto *background = disabled->property("background").value<QQuickItem *>();
            QVERIFY(background);
            QVERIFY(background->property("color").value<QColor>().alpha() > 0);
            click(disabled);
            QCOMPARE(clicks.size(), 0);
            QCOMPARE(routes.current(), route);
        }
        auto *user = visible("sidebarUserLink");
        QVERIFY(user);
        auto *shortcut = visible("sidebarLayoutTrigger");
        QVERIFY(shortcut);
        auto *tip = shortcut->findChild<QObject *>(QStringLiteral("iconButtonToolTip"));
        QVERIFY(tip);
        const auto hoverPoint =
            shortcut->mapToScene({shortcut->width() / 2, shortcut->height() / 2}).toPoint();
        Test::moveNativePointer(window, hoverPoint);
        QTRY_VERIFY(shortcut->property("hovered").toBool());
        QTRY_VERIFY(tip->property("requestedVisible").toBool());
        QTRY_VERIFY2(
            tip->property("visible").toBool(),
            qPrintable(QString("eligible=%1 dismissed=%2 requested=%3 has-popup=%4 at=%5,%6")
                           .arg(tip->property("eligible").toBool())
                           .arg(tip->property("dismissedUntilHoverEnds").toBool())
                           .arg(tip->property("requestedVisible").toBool())
                           .arg(tip->property("popup").value<QObject *>() != nullptr)
                           .arg(hoverPoint.x())
                           .arg(hoverPoint.y())));
        qInfo() << "Production Main hovered native ToolTips:" << nativeTipCount();
        QCOMPARE(nativeTipCount(), 1);
        // Open without moving the pointer: an existing popup hint must close
        // even though its painted anchor still occupies the same position.
        accountUi.controller.openLogin();
        QTRY_VERIFY(!shortcut->isEnabled());
        QTRY_VERIFY(!tip->property("visible").toBool());
        QTRY_COMPARE(nativeTipCount(), 0);
        QTest::qWait(550);
        QVERIFY(!tip->property("visible").toBool());
        accountUi.controller.closeLogin();
        QTRY_VERIFY(shortcut->isEnabled());
        QVERIFY(Test::prepareNativeInput(window));
        Test::moveNativePointer(window, {window->width() - 20, window->height() / 2});
        QTRY_VERIFY(!shortcut->property("hovered").toBool());
        Test::moveNativePointer(
            window,
            shortcut->mapToScene({shortcut->width() / 2, shortcut->height() / 2}).toPoint());
        QTRY_VERIFY(shortcut->property("hovered").toBool());
        QTRY_VERIFY(tip->property("requestedVisible").toBool());
        QVERIFY(!tip->property("visible").toBool());
        // Also cancel a hint which has not completed its 500ms delay yet.
        accountUi.controller.openLogin();
        QTest::qWait(550);
        QVERIFY(!tip->property("visible").toBool());
        accountUi.controller.closeLogin();
        click(user);
        QTRY_VERIFY(accountUi.controller.loginOpen());
        QTRY_VERIFY(visible("loginOverlay"));
        auto *back = visible("loginBackButton");
        QVERIFY(back);
        QCOMPARE(back->position(), QPointF(24, 48));
        QCOMPARE(back->size(), QSizeF(40, 40));
        QCOMPARE(routes.current(), route);
        for (const auto mode : {ThemeStore::Dark, ThemeStore::Light}) {
            if (!accountUi.controller.loginOpen())
                accountUi.controller.openLogin();
            theme.setMode(mode);
            accountUi.controller.setMethod(2);
            QQuickItem *username = nullptr, *password = nullptr;
            QTRY_VERIFY((username = visible("loginUsername")) &&
                        (password = visible("loginPassword")));
            auto *submit = visible("loginSubmit");
            QVERIFY(submit);
            QVERIFY(username->width() > 250);
            QCOMPARE(username->height(), 56.0);
            QCOMPARE(password->height(), username->height());
            QCOMPARE(submit->height(), username->height());
            QCOMPARE(submit->width(), username->width());
            QCOMPARE(password->y() - username->y() - username->height(), 16.0);
            for (auto *control : {username, password, submit}) {
                auto *surface = control->property("background").value<QQuickItem *>();
                QVERIFY(surface);
                QCOMPARE(surface->property("radius").toReal(), 6.0);
            }
            QCOMPARE(username->property("font").value<QFont>().pixelSize(), 15);
            QCOMPARE(submit->property("font").value<QFont>().pixelSize(), 15);
            qreal previousRight = -1;
            const QStringList labels{QStringLiteral("酷狗"), QStringLiteral("微信"),
                                     QStringLiteral("QQ"), QStringLiteral("验证码"),
                                     QStringLiteral("账号")};
            int displayed = 0;
            for (const int method : {0, 4, 3, 1, 2}) {
                auto *button = visible("loginMethod-" + QString::number(method));
                auto *icon = visible("loginMethodIcon-" + QString::number(method));
                auto *caption = visible("loginMethodCaption-" + QString::number(method));
                QVERIFY(button && icon && caption);
                QVERIFY(button->x() > previousRight);
                previousRight = button->x() + button->width();
                QCOMPARE(caption->property("text").toString(), labels.at(displayed++));
                click(button);
                QTRY_COMPARE(accountUi.controller.method(), method);
                QVERIFY(button->property("selected").toBool());
                const auto top = icon->mapToItem(button, {0, 0}).y();
                const auto textTop = caption->mapToItem(button, {0, 0}).y();
                QCOMPARE(textTop - top - icon->height(), 4.0);
                QCOMPARE(top, button->height() - textTop - caption->height());
            }
            username->setProperty("text", QStringLiteral("test draft"));
            password->setProperty("text", QStringLiteral("test draft password"));
            QPointer<QQuickItem> draft = password;
            username->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(!accountUi.controller.loginOpen());
            QTRY_VERIFY(draft.isNull());
            accountUi.controller.openLogin();
            accountUi.controller.setMethod(2);
            QTRY_VERIFY(visible("loginPassword"));
            QVERIFY(visible("loginPassword")->property("text").toString().isEmpty());
            accountUi.controller.setMethod(3);
            QCOMPARE(accountUi.controller.method(), 3);
            QTRY_COMPARE(visible("loginQrTitle")->property("text").toString(),
                         QStringLiteral("QQ 登录"));
            accountUi.controller.setMethod(4);
            QTRY_COMPARE(visible("loginQrTitle")->property("text").toString(),
                         QStringLiteral("微信登录"));
            accountUi.controller.setMethod(1);
            QTRY_VERIFY(visible("loginMobile") && visible("loginSmsCode") &&
                        visible("loginSendSms"));
            QCOMPARE(visible("loginMobile")->height(), 56.0);
            QCOMPARE(visible("loginSmsSubmit")->height(), 56.0);
            QJsonArray candidates;
            for (int row = 1; row <= 1000; ++row)
                candidates.append(
                    QJsonObject{{"user_id", row},
                                {"nickname", QStringLiteral("unit-account-%1").arg(row)},
                                {"avatar_url", ""},
                                {"username", ""}});
            QVERIFY(accountUi.controller.smsCandidates()->replace(candidates));
            QQuickItem *candidateList = nullptr;
            QTRY_VERIFY((candidateList = visible("smsCandidateList")));
            QCOMPARE(candidateList->property("count").toInt(), 1000);
            const auto *content = candidateList->property("contentItem").value<QQuickItem *>();
            QVERIFY(content);
            QTRY_VERIFY(content->childItems().size() > 0);
            QVERIFY(content->childItems().size() < 24);
            auto *back = visible("smsCandidatesBack");
            auto *firstCandidate = visible("smsCandidateRow-0");
            QVERIFY(back && firstCandidate);
            QTRY_COMPARE(back->mapToItem(candidateList, {back->width(), 0}).x(),
                         firstCandidate->width());
            candidateList->setProperty("contentY", 720 * 40);
            QTest::qWait(120);
            QVERIFY(content->childItems().size() < 24);
            accountUi.controller.resetSmsCandidates();
            accountUi.controller.setMethod(2);
            const auto artifacts = qEnvironmentVariable("ECHO_ACCOUNT_ARTIFACTS");
            if (!artifacts.isEmpty()) {
                QDir().mkpath(artifacts);
                QTest::qWait(30);
                QVERIFY(window->grabWindow().save(artifacts + (mode == ThemeStore::Dark
                                                                   ? "/login-dark.png"
                                                                   : "/login-light.png")));
            }
            accountUi.controller.setMethod(0);
            QrLoginServiceTestAccess::present(accountUi.qr, QrLoginService::State::Waiting);
            QQuickItem *qrPanel = nullptr;
            QTRY_VERIFY((qrPanel = visible("loginQrPanel")));
            auto *qrSurface = visible("loginQrSurface"), *subtitle = visible("loginQrSubtitle");
            auto *statusRow = visible("loginQrStatusRow"), *status = visible("loginQrStatus"),
                 *refresh = visible("loginQrRefresh");
            auto *footer = visible("loginMethodsFooter"),
                 *footerTitle = visible("loginMethodsTitle"), *card = visible("loginCard");
            QVERIFY(qrSurface && subtitle && statusRow && status && refresh && footer &&
                    footerTitle && card);
            QCOMPARE(qrSurface->size(), QSizeF(204, 204));
            // Showing the previously hidden QR panel schedules Column polish;
            // wait for its geometry instead of reading before the layout pass.
            QTRY_COMPARE(qrSurface->y() - subtitle->mapToItem(qrPanel, {0, subtitle->height()}).y(),
                         10.0);
            QCOMPARE(statusRow->y() - qrSurface->y() - qrSurface->height(), 24.0);
            // The right-hand refresh action must not displace the status label.
            QCOMPARE(status->x() + status->width() / 2, statusRow->width() / 2);
            QCOMPARE(refresh->x() + refresh->width(), statusRow->width());
            // QML snaps anchor centers to pixels. A 16.5px line height
            // can place its center a quarter pixel from the unsnapped value.
            QVERIFY(qAbs(refresh->y() + refresh->height() / 2 - statusRow->height() / 2) <= 0.5);
            QCOMPARE(refresh->size(), QSizeF(28, 28));
            QCOMPARE(refresh->property("contentItem").value<QQuickItem *>()->size(),
                     QSizeF(14, 14));
            QCOMPARE(footer->mapToItem(card, {0, 0}).x(), 33.0); // 1px border + px-8.
            QCOMPARE(footer->mapToItem(card, {0, footer->height()}).y(), card->height() - 29);
            QCOMPARE(footerTitle->property("text").toString(), QStringLiteral("选择登录方式"));
            const auto lastMethod = visible("loginMethod-0");
            QVERIFY(lastMethod);
            QCOMPARE(lastMethod->mapToItem(footer, {0, 0}).y() - footerTitle->y() -
                         footerTitle->height(),
                     12.0);
            QVERIFY(qrPanel->mapToItem(card, {0, qrPanel->height() + 6}).y() <
                    footer->mapToItem(card, {0, 0}).y());
            QImage codeImage(204, 204, QImage::Format_RGB32);
            codeImage.fill(Qt::white);
            // A canonical contrast pattern reveals the underlying image through
            // the overlay. This is not a fabricated provider/scan-success sample.
            for (int y = 21; y < 183; ++y)
                for (int x = 21; x < 183; ++x)
                    if ((x / 9 + y / 9) % 2)
                        codeImage.setPixelColor(x, y, Qt::black);
            QByteArray png;
            QBuffer buffer(&png);
            QVERIFY(buffer.open(QIODevice::WriteOnly));
            QVERIFY(codeImage.save(&buffer, "PNG"));
            const auto imageUrl =
                QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
            for (const int method : {0, 3, 4}) {
                accountUi.controller.setMethod(method);
                auto *qr = method == 0   ? accountUi.controller.qr()
                           : method == 3 ? accountUi.controller.qq()
                                         : accountUi.controller.wechat();
                QrLoginServiceTestAccess::present(*qr, QrLoginService::State::Waiting, imageUrl);
                auto *title = visible("loginQrTitle");
                subtitle = visible("loginQrSubtitle");
                qrPanel = visible("loginQrPanel");
                qrSurface = visible("loginQrSurface");
                auto *image = visible("loginQrImage");
                QVERIFY(title && subtitle && qrPanel && qrSurface && image);
                QCOMPARE(image->size(), QSizeF(204, 204));
                QVERIFY(!qrSurface->property("radius").isValid());
                QVERIFY(!qrSurface->property("color").isValid());
                QTRY_COMPARE(subtitle->mapToItem(qrPanel, QPointF{}).y() -
                                 title->mapToItem(qrPanel, {0, title->height()}).y(),
                             qrSurface->y() -
                                 subtitle->mapToItem(qrPanel, {0, subtitle->height()}).y());
                for (const auto state :
                     {QrLoginService::State::Scanned, QrLoginService::State::Authorizing,
                      QrLoginService::State::Expired, QrLoginService::State::Error}) {
                    QrLoginServiceTestAccess::present(*qr, state, imageUrl);
                    QTRY_VERIFY(visible("loginQrStateIcon"));
                    QVERIFY(image->isVisible());
                    auto *layer = visible("loginQrStateOverlay");
                    auto *viewport = visible("loginQrImageViewport");
                    auto *stateIcon = visible("loginQrStateIcon");
                    QVERIFY(layer && viewport && stateIcon);
                    QCOMPARE(layer->size(), image->size());
                    QCOMPARE(layer->mapToItem(qrSurface, {0, 0}),
                             image->mapToItem(qrSurface, {0, 0}));
                    QCOMPARE(stateIcon->mapToItem(layer, {24, 24}), QPointF(102, 102));
                    QVERIFY(layer->z() > viewport->z());
                    QCOMPARE(viewport->size(),
                             mode == ThemeStore::Dark ? QSizeF(162, 162) : QSizeF(204, 204));
                    QVERIFY(viewport->clip());
                    QCOMPARE(image->property("source").toUrl(), QUrl(imageUrl));
                    QVERIFY(!visible("loginQrStateTitle"));
                    QVERIFY(!visible("loginQrStateDescription"));
                    QVERIFY(!visible("loginQrRetry"));
                    auto *below = visible("loginQrStatus");
                    QVERIFY(below);
                    const auto text = below->property("text").toString();
                    if (state == QrLoginService::State::Scanned) {
                        QVERIFY(text.startsWith(QStringLiteral("已扫码")));
                        QVERIFY(text.contains(QStringLiteral("确认登录")));
                    } else if (state == QrLoginService::State::Authorizing)
                        QCOMPARE(text, QStringLiteral("正在登录"));
                    else if (state == QrLoginService::State::Expired)
                        QCOMPARE(text, QStringLiteral("二维码已过期"));
                    else
                        QCOMPARE(text, qr->errorMessage());
                    QCOMPARE(bool(visible("loginQrRefresh")),
                             state != QrLoginService::State::Authorizing);
                    if (!artifacts.isEmpty()) {
                        QTest::qWait(50);
                        QVERIFY(window->grabWindow().save(
                            artifacts + QString("/login-qr-%1-state%2-%3.png")
                                            .arg(method)
                                            .arg(int(state))
                                            .arg(mode == ThemeStore::Dark ? "dark" : "light")));
                    }
                }
                QrLoginServiceTestAccess::present(*qr, QrLoginService::State::Waiting, imageUrl);
                QTRY_COMPARE(visible("loginQrImageViewport")->size(), QSizeF(204, 204));
                if (!artifacts.isEmpty()) {
                    QTest::qWait(50);
                    QVERIFY(window->grabWindow().save(
                        artifacts + QString("/login-qr-%1-%2.png")
                                        .arg(method)
                                        .arg(mode == ThemeStore::Dark ? "dark" : "light")));
                }
            }
            accountUi.controller.setMethod(0);
            QrLoginServiceTestAccess::present(accountUi.qr, QrLoginService::State::Waiting,
                                              imageUrl);
            if (!artifacts.isEmpty()) {
                QTest::qWait(30);
                QVERIFY(window->grabWindow().save(artifacts + (mode == ThemeStore::Dark
                                                                   ? "/login-qr-dark.png"
                                                                   : "/login-qr-light.png")));
            }
            accountUi.controller.closeLogin();
        }
        if (!shell.sidebarCollapsed())
            shell.toggleSidebar();
        QTRY_VERIFY(shell.sidebarCollapsed());
        QVERIFY(!visible("sidebarAccountTrigger"));
        auto *railAvatar = visible("sidebarUserLink");
        QVERIFY(railAvatar);
        QTRY_COMPARE(railAvatar->width(), 42.0);
        click(railAvatar);
        QTRY_VERIFY(accountUi.controller.loginOpen());
        accountUi.controller.closeLogin();
        shell.toggleSidebar();
        QTRY_VERIFY(!shell.sidebarCollapsed());
        auto *trigger = visible("sidebarAccountTrigger");
        QVERIFY(trigger);
        click(trigger);
        QPointer<QObject> panel = root->findChild<QObject *>("sidebarAccountPopover");
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("opened").toBool());
        QCOMPARE(panel->property("width").toReal(), 320.0);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(panel.isNull());
        QCOMPARE(warnings.size(), 0);
        routes.shutdown();
        windows.shutdown();
    }
    void historyTabsRefreshAndExclusions()
    {
        RouteController routes;
        QAbstractItemModelTester model(&routes,
                                       QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(routes.registerPage(definition("playlist")));
        QVERIFY(routes.registerPage(definition("search", false)));
        QVERIFY(routes.registerPage(definition("themes", false)));
        QVERIFY(!routes.navigate("home"));
        QVERIFY(routes.navigate("playlist", {{"tab", QString("songs")}}, "one"));
        QPointer<RouteContext> original = routes.current();
        routes.replaceQuery({{"tab", QString("comments")}});
        QCOMPARE(routes.current(), original.data());
        QVERIFY(!routes.canGoBack());
        QVERIFY(routes.navigate("playlist", {}, "two"));
        QVERIFY(!original->active());
        routes.back();
        QCOMPARE(routes.current(), original.data());
        QVERIFY(routes.canGoForward());
        QSignalSpy settings(&routes, &RouteController::settingsRequested);
        routes.navigate("settings");
        QCOMPARE(settings.size(), 1);
        QCOMPARE(routes.current(), original.data());
        routes.refresh();
        const auto refreshedKey = routes.current()->key();
        QVERIFY(refreshedKey != original->key());
        drain();
        QVERIFY(original.isNull());
        routes.forward();
        routes.back();
        QCOMPARE(routes.current()->key(), refreshedKey);
        routes.navigate("search", {}, "public");
        QPointer<RouteContext> search = routes.current();
        routes.navigate("themes", {}, "theme");
        drain();
        QVERIFY(search.isNull());
        routes.setCachePolicy(false, 20);
        QCOMPARE(routes.mountedCount(), 1);
        routes.shutdown();
        drain();
        QCOMPARE(routes.mountedCount(), 0);
        QVERIFY(!routes.navigate("playlist", {}, "three"));
    }
    void actualLoaderRetainsVisualStateAndStopsInactiveTimers()
    {
        QTemporaryDir temp;
        EchoMusic::SettingsStore preferences(temp.filePath("settings.ini"));
        ShellSettings shell(preferences);
        ApiClient api(DeviceIdentity::temporary());
        Test::AccountUiFixture accountUi{api};
        SearchModel search(api);
        RouteController routes;
        routes.registerPage(definition("playlist"));
        routes.setCachePolicy(true, 2);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/ShellTests/ShellHarness.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&routes)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        routes.navigate("playlist", {}, "one");
        QPointer<RouteContext> first = routes.current();
        QTRY_VERIFY(probe(root.data(), first));
        QPointer<QObject> firstItem = probe(root.data(), first);
        firstItem->setProperty("visualScroll", 413);
        QTRY_VERIFY(firstItem->property("ticks").toInt() > 0);
        routes.navigate("playlist", {}, "two");
        const auto stopped = firstItem->property("ticks").toInt();
        QTest::qWait(30);
        QCOMPARE(firstItem->property("ticks").toInt(), stopped);
        routes.back();
        QCOMPARE(routes.current(), first.data());
        QCOMPARE(probe(root.data(), first), firstItem.data());
        QCOMPARE(firstItem->property("visualScroll").toInt(), 413);
        QTRY_VERIFY(firstItem->property("ticks").toInt() > stopped);
        routes.navigate("playlist", {}, "two");
        routes.navigate("playlist", {}, "three");
        QTRY_VERIFY(first.isNull());
        QTRY_VERIFY(firstItem.isNull());
        QCOMPARE(routes.mountedCount(), 2);
        routes.shutdown();
        drain();
        api.shutdown();
    }
    void accountAndReentrantRefresh()
    {
        RouteController routes;
        routes.registerPage(definition("playlist"));
        QVERIFY(routes.registerPage(
            {"home", "/main/home", QUrl("qrc:/qt/qml/EchoMusic/ShellTests/ProbePage.qml")}));
        routes.registerPage({"search",
                             "/main/search",
                             QUrl("qrc:/qt/qml/EchoMusic/ShellTests/ProbePage.qml"),
                             {"tab"},
                             false,
                             false});
        bool queued = false;
        connect(&routes, &RouteController::pageActivated, &routes, [&](RouteContext *) {
            if (!queued) {
                queued = true;
                routes.refresh();
            }
        });
        routes.navigate("playlist", {}, "one");
        const auto before = routes.current()->instanceId();
        QTRY_VERIFY(routes.current()->instanceId() != before);
        QPointer<RouteContext> accountPage = routes.current();
        routes.invalidateAccount();
        QCOMPARE(routes.current()->name(), QString("home"));
        QVERIFY(!routes.canGoBack());
        drain();
        QVERIFY(accountPage.isNull());
    }
    void startupAccountRestorationRetainsLatestLocationWithFreshPages()
    {
        RouteController routes;
        QVERIFY(routes.registerPage(
            {"home", "/main/home", QUrl("qrc:/qt/qml/EchoMusic/ShellTests/ProbePage.qml")}));
        QVERIFY(routes.registerPage(definition("landing")));
        QVERIFY(routes.registerPage(definition("next")));
        QVERIFY(routes.registerPage({"search",
                                     "/main/search",
                                     QUrl("qrc:/qt/qml/EchoMusic/ShellTests/ProbePage.qml"),
                                     {},
                                     false,
                                     false}));
        bool queued = false;
        connect(&routes, &RouteController::pageActivated, &routes, [&](RouteContext *page) {
            if (!queued && page->name() == "landing") {
                queued = true;
                routes.invalidateAccountPreservingRoute();
            }
        });
        QVERIFY(routes.navigate("landing", {}, "start"));
        QVERIFY(
            routes.navigate("next", {{"tab", "songs"}}, "chosen")); // Advances during hydration.
        const auto guestPage = routes.current()->instanceId();
        QTRY_VERIFY(routes.current()->instanceId() != guestPage);
        QCOMPARE(routes.current()->name(), QString("next"));
        QCOMPARE(routes.current()->resourceId(), QString("chosen"));
        QCOMPARE(routes.current()->query().value("tab").toString(), QString("songs"));
        QVERIFY(!routes.canGoBack());
        QCOMPARE(routes.mountedCount(), 1);
        // Explicit account switching returns home instead of mounting empty search.
        routes.invalidateAccount();
        QCOMPARE(routes.current()->name(), QString("home"));
        QVERIFY(!routes.canGoBack());
    }
    void loginAndAccountChangesReturnHomeAndEmptySearchRedirects()
    {
        ApiClient api(DeviceIdentity::temporary());
        SearchModel search(api);
        RouteController routes;
        QVERIFY(routes.registerPage(
            {"home", "/main/home", QUrl("qrc:/qt/qml/EchoMusic/ShellTests/ProbePage.qml")}));
        QVERIFY(routes.registerPage(definition("playlist")));
        SearchRouteCoordinator searchRoutes(search, routes, false);
        for (const auto name : {QString("home"), QString("playlist"), QString("search")}) {
            QVERIFY(routes.navigate(name,
                                    name == "search" ? QVariantMap{{"q", "歌曲"}} : QVariantMap{},
                                    name == "playlist" ? "one" : ""));
            QPointer<RouteContext> previous = routes.current();
            routes.invalidateAccount();
            QCOMPARE(routes.current()->name(), QString("home"));
            QVERIFY(!routes.canGoBack());
            QVERIFY(!routes.canGoForward());
            QCOMPARE(routes.mountedCount(), 1);
            drain();
            QVERIFY(previous.isNull());
        }
        for (const auto keyword : {QString{}, QString("  ")}) {
            QVERIFY(routes.navigate("search", {{"q", keyword}}));
            QTRY_COMPARE(routes.current()->name(), QString("home"));
            // Replace the empty route so Back cannot restore a blank page.
            routes.back();
            QCOMPARE(routes.current()->name(), QString("home"));
        }
        QVERIFY(routes.navigate("search", {{"q", "歌曲"}, {"tab", "lyric"}}));
        QCOMPARE(routes.current()->name(), QString("search"));
        QCOMPARE(routes.current()->query().value("q").toString(), QString("歌曲"));
        routes.invalidateAccountPreservingRoute();
        QCOMPARE(routes.current()->name(), QString("search"));
        QCOMPARE(routes.current()->query().value("tab").toString(), QString("lyric"));
        // Removing the only plugin page must use the same nonempty fallback.
        RouteController pluginRoutes;
        QVERIFY(pluginRoutes.registerPage(
            {"home", "/main/home", QUrl("qrc:/qt/qml/EchoMusic/ShellTests/ProbePage.qml")}));
        const auto revision = pluginRoutes.registerPluginPage(
            {"plugin-page", "/plugin-page",
             QUrl("qrc:/qt/qml/EchoMusic/ShellTests/ProbePage.qml")});
        QVERIFY(revision);
        QVERIFY(pluginRoutes.navigate("plugin-page"));
        QVERIFY(pluginRoutes.unregisterPage("plugin-page", revision));
        QCOMPARE(pluginRoutes.current()->name(), QString("home"));
        QVERIFY(!pluginRoutes.canGoBack());
        pluginRoutes.shutdown();
        routes.shutdown();
        search.shutdown();
        api.shutdown();
    }
    void dynamicHomepageFollowsCurrentFirstShortcut()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore preferences(directory.filePath("settings.ini"));
        preferences.setValue("sidebar/shortcuts",
                             QStringList{"recommend", "explore", "home", "ranking"});
        ShellSettings settings(preferences);
        auto *layout = settings.sidebarLayout();
        ApiClient api(DeviceIdentity::temporary());
        SearchModel search(api);
        RouteController routes;
        routes.setHomeLayout(layout);
        for (const auto name :
             {QString("home"), QString("explore"), QString("recommend-songs"), QString("ranking")})
            QVERIFY(routes.registerPage(
                {name, "/main/" + name, QUrl("qrc:/qt/qml/EchoMusic/ShellTests/ProbePage.qml")}));
        QVERIFY(routes.registerPage(definition("playlist")));
        SearchRouteCoordinator searchRoutes(search, routes, false);
        // Restored preference keys and route names differ for daily recommendations.
        QCOMPARE(routes.homeRoute(), QString("recommend-songs"));
        QVERIFY(routes.navigateHome());
        QCOMPARE(routes.current()->name(), QString("recommend-songs"));
        QVERIFY(routes.navigate("playlist", {}, "one"));
        layout->moveShortcutBefore("explore", "recommend");
        QCOMPARE(routes.current()->name(), QString("playlist")); // Reordering is not navigation.
        routes.invalidateAccount();
        QCOMPARE(routes.current()->name(), QString("explore"));
        QVERIFY(!routes.canGoBack());
        // Empty search queues its redirect; read the first card when it runs,
        // rather than capturing the route before a newer reorder is committed.
        QVERIFY(routes.navigate("search"));
        layout->moveShortcutBefore("ranking", "explore");
        QTRY_COMPARE(routes.current()->name(), QString("ranking"));
        routes.back();
        QCOMPARE(routes.current()->name(), QString("explore"));
        layout->toggleShortcut("ranking");
        routes.invalidateAccount();
        QCOMPARE(routes.current()->name(), QString("explore"));
        layout->moveShortcutBefore("recommend", "explore");
        QVERIFY(routes.navigate("search", {{"q", "歌曲"}, {"tab", "lyric"}}));
        routes.invalidateAccountPreservingRoute();
        QCOMPARE(routes.current()->name(), QString("search"));
        QCOMPARE(routes.current()->query().value("q").toString(), QString("歌曲"));
        QCOMPARE(routes.current()->query().value("tab").toString(), QString("lyric"));
        routes.invalidateAccount();
        QCOMPARE(routes.current()->name(), QString("recommend-songs"));
        // No stale startup copy: a fresh settings reader restores the saved order.
        preferences.sync();
        EchoMusic::SettingsStore restoredPreferences(directory.filePath("settings.ini"));
        ShellSettings restored(restoredPreferences);
        QCOMPARE(restored.sidebarLayout()->homeRoute(), QString("recommend-songs"));
        RouteController pluginRoutes;
        pluginRoutes.setHomeLayout(layout);
        for (const auto name : {QString("home"), QString("explore"), QString("recommend-songs")})
            QVERIFY(pluginRoutes.registerPage(
                {name, "/main/" + name, QUrl("qrc:/qt/qml/EchoMusic/ShellTests/ProbePage.qml")}));
        const auto revision = pluginRoutes.registerPluginPage(
            {"plugin-page", "/plugin-page",
             QUrl("qrc:/qt/qml/EchoMusic/ShellTests/ProbePage.qml")});
        QVERIFY(revision);
        QVERIFY(pluginRoutes.navigate("plugin-page"));
        layout->moveShortcutBefore("explore", "recommend");
        QVERIFY(pluginRoutes.unregisterPage("plugin-page", revision));
        QCOMPARE(pluginRoutes.current()->name(), QString("explore"));
        QVERIFY(!pluginRoutes.canGoBack());
        pluginRoutes.shutdown();
        routes.shutdown();
        search.shutdown();
        api.shutdown();
    }
    void preferencesResponsiveZoomAndPalette()
    {
        QTemporaryDir temp;
        EchoMusic::SettingsStore settings(temp.filePath("preferences.ini"));
        ShellSettings shell(settings);
        QCOMPARE(shell.keepAliveMax(), 20);
        QVERIFY(!shell.searchDefaultEnabled());
        shell.setSearchDefaultEnabled(true);
        ShellSettings searchEnabledAgain(settings);
        QVERIFY(searchEnabledAgain.searchDefaultEnabled());
        QVERIFY(!shell.sidebarCollapsed());
        shell.updateViewport(699);
        QVERIFY(shell.sidebarCollapsed());
        shell.toggleSidebar();
        QVERIFY(!shell.sidebarCollapsed());
        shell.updateViewport(900);
        shell.updateViewport(699);
        QVERIFY(shell.sidebarCollapsed());
        shell.resetZoom();
        shell.zoomIn();
        QCOMPARE(shell.uiScale(), 1.05);
        shell.zoomOut();
        QCOMPARE(shell.uiScale(), 1.0);
        shell.setKeepAliveMax(1000);
        QCOMPARE(shell.keepAliveMax(), 30);
        shell.setKeepAliveEnabled(false);
        shell.setSearchDefaultEnabled(false);
        ThemeStore theme(settings);
        theme.setMode(ThemeStore::Dark);
        QCOMPARE(theme.main(), QColor("#29292b"));
        theme.adoptSystemScheme(Qt::ColorScheme::Light);
        QVERIFY(theme.dark()); // pinned mode wins
        theme.setMode(ThemeStore::Light);
        QCOMPARE(theme.card(), QColor("#f7f7f7"));
        theme.setMode(ThemeStore::System);
        theme.adoptSystemScheme(Qt::ColorScheme::Dark);
        QVERIFY(theme.dark());
        QCOMPARE(theme.mode(), ThemeStore::System);
        theme.selectSolid(QColor("#7da3ce"));
        QVERIFY(ThemeStore::contrast(theme.text(), theme.main()) >= 7);
        QVERIFY(ThemeStore::contrast(theme.secondaryText(), theme.card()) >= 4.5);
        const auto seed = theme.seedColor();
        theme.selectSolid(QColor("transparent"));
        QCOMPARE(theme.seedColor(), seed);
        theme.setAccent(QColor("#f59e0b"));
        QVERIFY(ThemeStore::contrast(theme.accentText(), theme.panelBackdrop()) >= 4.5);
        QVERIFY(ThemeStore::contrast(theme.floatingAccentText(), theme.elevated()) >= 4.5);
        QVERIFY(ThemeStore::contrast(theme.floatingAccentText(), theme.floatingCard()) >= 4.5);
        QCOMPARE(theme.panelOpacity(), 50);
        QVERIFY(qAbs(theme.panelColor().alphaF() - 0.5) < 0.01);
        theme.setPanelOpacity(-10);
        QCOMPARE(theme.panelOpacity(), 0);
        QCOMPARE(theme.panelBackdrop(), theme.shell());
        theme.setPanelOpacity(140);
        QCOMPARE(theme.panelOpacity(), 100);
        QCOMPARE(theme.panelBackdrop(), theme.main());
        theme.setPanelOpacity(37);
        settings.sync();
        EchoMusic::SettingsStore reopened(temp.filePath("preferences.ini"));
        ShellSettings shellAgain(reopened);
        ThemeStore themeAgain(reopened);
        QCOMPARE(shellAgain.keepAliveMax(), 30);
        QVERIFY(!shellAgain.keepAliveEnabled());
        QVERIFY(!shellAgain.searchDefaultEnabled());
        QCOMPARE(themeAgain.seedColor(), seed);
        QCOMPARE(themeAgain.mode(), ThemeStore::System);
        QCOMPARE(themeAgain.panelOpacity(), 37);
    }
    void realMainShellGeometryAndThemeRoute()
    {
        QTemporaryDir temp;
        EchoMusic::SettingsStore preferences(temp.filePath("settings.ini"));
        ThemeStore theme(preferences);
        ShellSettings shell(preferences);
        WindowController windows(preferences, false);
        ApiClient api(DeviceIdentity::temporary());
        Test::AccountUiFixture accountUi{api};
        SearchModel search(api);
        RouteController routes;
        // Exercise production registration/activation across the new module
        // boundary while keeping the formal Main.qml integration test offline.
        SearchRouteCoordinator searchRoutes(search, routes, false);
        routes.registerPage({"themes",
                             "/main/themes",
                             QUrl("qrc:/qt/qml/EchoMusic/Themes/ThemeCenterPage.qml"),
                             {},
                             false,
                             false});
        routes.navigate("search");
        ShellController shellController(preferences, routes);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/App/Main.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"libraryController", QVariant::fromValue(&accountUi.library)},
             {"accountController", QVariant::fromValue(&accountUi.controller)},
             {"shellController", QVariant::fromValue(&shellController)},
             {"searchService", QVariant::fromValue(&search)},
             {"router", QVariant::fromValue(&routes)},
             {"shellSettings", QVariant::fromValue(&shell)},
             {"windowService", QVariant::fromValue(&windows)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        windows.attach(window);
        QVERIFY(Test::prepareNativeInput(window));
        auto *frame = findVisual(window->contentItem(), "shellFrame");
        QVERIFY(frame);
        auto *mainSurface = findVisual(frame, "mainPanelSurface");
        auto *playerSurface = findVisual(frame, "playerPanelSurface");
        QVERIFY(mainSurface && playerSurface);
        theme.setPanelOpacity(37);
        QQuickItem *mainMaterial = nullptr, *playerMaterial = nullptr;
        QTRY_VERIFY((mainMaterial = findVisual(frame, "themeMainPanelMaterial")));
        QTRY_VERIFY((playerMaterial = findVisual(frame, "themePlayerPanelMaterial")));
        QTRY_COMPARE(mainMaterial->property("color").value<QColor>(), theme.panelColor());
        QTRY_COMPARE(playerMaterial->property("color").value<QColor>(), theme.panelColor());
        QVERIFY(qAbs(mainMaterial->property("color").value<QColor>().alphaF() - 0.37) < 0.01);
        QCOMPARE(mainSurface->property("color").value<QColor>(), QColor(Qt::transparent));
        QCOMPARE(playerSurface->property("color").value<QColor>(), QColor(Qt::transparent));
        QVERIFY(qAbs(mainSurface->opacity() - 1) < 0.01); // Surface alpha never fades content.
        QVERIFY(qAbs(playerSurface->opacity() - 1) < 0.01);
        QTRY_COMPARE(frame->width(), qreal(window->width()));
        routes.navigate("themes");
        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = findVisual(window->contentItem(), "themeCenterPage")));
        auto *heading = findVisual(page, "themeHeading"),
             *action = findVisual(page, "appearanceAction");
        QVERIFY(heading && action);
        QTRY_VERIFY(qAbs(action->x() + action->width() - heading->width()) < 2);
        QTRY_COMPARE(page->width(), frame->width() - 238);
        shell.zoomIn();
        QTRY_VERIFY(qAbs(frame->width() * shell.uiScale() - window->width()) < 0.01);
        QTRY_COMPARE(mainMaterial->mapToScene(QPointF()), mainSurface->mapToScene(QPointF()));
        QTRY_COMPARE(playerMaterial->mapToScene(QPointF()), playerSurface->mapToScene(QPointF()));
        QTRY_COMPARE(mainMaterial->size(), mainSurface->size());
        QTRY_COMPARE(playerMaterial->size(), playerSurface->size());
        theme.setMode(ThemeStore::Dark);
        auto *background = findVisual(frame, "themeWindowBackground");
        auto *base = findVisual(frame, "themeBaseSurface");
        QVERIFY(background && base);
        QCOMPARE(window->color(), QColor(Qt::transparent));
        QTRY_COMPARE(base->property("color").value<QColor>(), QColor("#171718"));
        theme.setMode(ThemeStore::Light);
        QTRY_COMPARE(base->property("color").value<QColor>(), QColor("#f6f6f6"));
        theme.setTransparency(80);
        QTRY_COMPARE(background->opacity(), 0.2);
        QCOMPARE(mainSurface->opacity(), 1.0);
        QCOMPARE(playerSurface->opacity(), 1.0);
        theme.setTransparency(0);
        // The native macOS application menu uses the window service signal;
        // exercise that production connection, including lazy dialog creation.
        emit windows.settingsRequested(QString{});
        QPointer<QObject> settingsDialog = root->findChild<QObject *>("settingsDialog");
        QVERIFY(settingsDialog);
        QTRY_VERIFY(settingsDialog->property("opened").toBool());
        QObject *mode = nullptr;
        QTRY_VERIFY((mode = root->findChild<QObject *>("appearanceMode")));
        auto *modeItem = qobject_cast<QQuickItem *>(mode);
        QVERIFY(modeItem);
        QTRY_COMPARE(modeItem->implicitHeight(), 36.0);
        auto *zoomIn = root->findChild<QQuickItem *>("settingsZoomIn");
        auto *zoomOut = root->findChild<QQuickItem *>("settingsZoomOut");
        QVERIFY(zoomIn && zoomOut && zoomIn->isVisible() && zoomOut->isVisible());
        QCOMPARE(zoomIn->implicitHeight(), 36.0);
        QTRY_VERIFY(modeItem->mapToScene(QPointF(modeItem->width(), modeItem->height())).x() <=
                    window->width());
        if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
            // Popup.opened precedes layout polish; use rendered coordinates for input.
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
            window->update();
            QTRY_VERIFY(!frames.isEmpty());
        }
        const auto clickPoint =
            modeItem->mapToScene(QPointF(modeItem->width() / 2, modeItem->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, clickPoint);
        QPointer<QObject> popup = mode->property("popup").value<QObject *>();
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        QVERIFY(settingsDialog->property("opened").toBool());
        QTest::keyClick(window, Qt::Key_End);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(theme.dark());
        QCOMPARE(routes.current()->name(), QString("themes"));
        QVERIFY(QMetaObject::invokeMethod(settingsDialog, "close"));
        routes.refresh();
        routes.back();
        routes.forward();
        QTRY_VERIFY(findVisual(window->contentItem(), "themeCenterPage"));
        QCOMPARE(warnings.size(), 0);
        routes.shutdown();
        windows.shutdown();
        search.shutdown();
        api.shutdown();
        window->close();
    }
    void moreListsKeepShortLabelsWholeAndLongLabelsBounded()
    {
        QTemporaryDir temp;
        EchoMusic::SettingsStore preferences(temp.filePath("settings.ini"));
        ThemeStore theme(preferences);
        ShellSettings shell(preferences);
        shell.setMotionEnabled(false);
        WindowController windows(preferences, false);
        ApiClient api(DeviceIdentity::temporary());
        Test::AccountUiFixture accounts{api};
        SearchModel search(api);
        RouteController routes;
        ShellController controller(preferences, routes);
        routes.navigate("profile");
        QObject provider;
        const QString shortKey = "test/more-short", longKey = "test/more-long";
        QHash<ShellActionModel *, ShellActionModel::Handle> shortHandles;
        for (auto *model : {controller.titlebarActions(), controller.playerbarActions()}) {
            for (bool longLabel : {false, true}) {
                ShellActionModel::Contribution entry;
                entry.key = longLabel ? longKey : shortKey;
                entry.id = entry.key;
                entry.title =
                    longLabel ? QStringLiteral(
                                    "较长的菜单名称 Long menu title，用来检查真实可用宽度与边界")
                              : QStringLiteral("注音样式");
                entry.icon = "music";
                entry.enabled = true;
                entry.order = longLabel ? -19 : -20;
                entry.invoke = [] {};
                const auto handle = model->registerAction(provider, entry);
                if (!longLabel)
                    shortHandles.insert(model, handle);
            }
        }
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/App/Main.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"libraryController", QVariant::fromValue(&accounts.library)},
             {"accountController", QVariant::fromValue(&accounts.controller)},
             {"shellController", QVariant::fromValue(&controller)},
             {"shellSettings", QVariant::fromValue(&shell)},
             {"router", QVariant::fromValue(&routes)},
             {"windowService", QVariant::fromValue(&windows)},
             {"searchService", QVariant::fromValue(&search)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        windows.attach(window);
        const auto cleanup = qScopeGuard([&] {
            routes.shutdown();
            controller.shutdown();
            windows.shutdown();
            root.reset();
        });
        const auto frames = [&] {
            QSignalSpy rendered(window, &QQuickWindow::frameSwapped);
            window->update();
            return rendered.wait(1000);
        };
        const auto captures = qEnvironmentVariable("ECHO_MORE_LABEL_CAPTURES");
        if (!captures.isEmpty())
            QVERIFY(QDir().mkpath(captures));
        for (auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (qreal scale : {1., 1.25}) {
                shell.resetZoom();
                while (shell.uiScale() < scale - .001)
                    shell.zoomIn();
                for (int width : {850, 1250}) {
                    window->resize(width, 900);
                    QVERIFY(Test::prepareNativeInput(window));
                    for (const QString kind : {QString("titlebar"), QString("playerbar")}) {
                        QVERIFY(frames());
                        QQuickItem *trigger = nullptr;
                        QTRY_VERIFY((trigger = findVisibleVisual(window->contentItem(),
                                                                 kind + "MoreAction")));
                        const auto point =
                            trigger->mapToScene({trigger->width() / 2, trigger->height() / 2})
                                .toPoint();
                        Test::moveNativePointer(window, point);
                        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
                        QPointer<QObject> menu;
                        QTRY_VERIFY((menu = root->findChild<QObject *>(kind + "MorePopup")));
                        QTRY_VERIFY(menu->property("opened").toBool());
                        QVERIFY(frames());
                        auto *content = menu->property("contentItem").value<QQuickItem *>();
                        QVERIFY(content);
                        auto *shortLabel = findVisual(content, kind + "MoreLabel-" + shortKey);
                        auto *longLabel = findVisual(content, kind + "MoreLabel-" + longKey);
                        QVERIFY(shortLabel && longLabel);
                        auto *shortText = shortLabel->property("textItem").value<QQuickItem *>();
                        auto *longText = longLabel->property("textItem").value<QQuickItem *>();
                        QVERIFY(shortText && longText);
                        auto *actions = kind == "titlebar" ? controller.titlebarActions()
                                                           : controller.playerbarActions();
                        for (const QString title :
                             {QStringLiteral("注音样式"), QStringLiteral("任务中心"),
                              QStringLiteral("EchoMusicNext"), QStringLiteral("下载管理 Downloads"),
                              QStringLiteral("歌词设置 Lyrics")}) {
                            QVERIFY(actions->setPresentation(shortHandles.value(actions), title,
                                                             "music"));
                            QVERIFY(frames());
                            QCOMPARE(shortText->property("text").toString(), title);
                            QCOMPARE(shortText->property("lineCount").toInt(), 1);
                        }
                        if (kind == "titlebar")
                            QVERIFY(longText->property("lineCount").toInt() > 1);
                        else {
                            QCOMPARE(longText->property("lineCount").toInt(), 1);
                            QVERIFY(longLabel->property("truncated").toBool());
                        }
                        QVERIFY(content->mapRectToScene(content->boundingRect())
                                    .contains(longText->mapRectToScene(longText->boundingRect())));
                        if (!captures.isEmpty())
                            QVERIFY(window->grabWindow().save(
                                captures + QString("/%1-%2-%3-%4.png")
                                               .arg(kind)
                                               .arg(mode == ThemeStore::Dark ? "dark" : "light")
                                               .arg(scale)
                                               .arg(width)));
                        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
                        QTRY_VERIFY(menu.isNull());
                    }
                }
            }
        }
    }
    void productionPlayerLayoutDragPreviewsAndSettles()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore preferences(directory.filePath("settings.ini"));
        ThemeStore theme(preferences);
        RouteController routes;
        ShellController controller(preferences, routes);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            import EchoMusic.Shell
            Window {
                id: host
                width: 1120; height: 760; visible: true
                property real uiScale: 1
                required property ShellActionModel actions
                function openEditor() { owner.open({editing: true}); }
                Item { id: anchor; x: 540; y: 710; width: 34; height: 34 }
                EchoPopupLoader {
                    id: owner
                    hostItem: host.contentItem
                    sourceComponent: Component {
                        PlayerBarMorePopup { actions: host.actions; anchorItem: anchor }
                    }
                }
                Component.onCompleted: Qt.callLater(openEditor)
            }
        )",
                          QUrl());
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"actions", QVariant::fromValue(controller.playerbarActions())}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window && Test::prepareNativeInput(window));
        auto item = [&](const QString &name) {
            return findVisibleVisual(window->contentItem(), name);
        };
        const auto capture = [&](QString name) {
            const auto path = qEnvironmentVariable("ECHO_PLAYER_LAYOUT_CAPTURE_DIR");
            if (path.isEmpty())
                return;
            window->update();
            QTest::qWait(16);
            QVERIFY(QDir().mkpath(path));
            const auto image = window->grabWindow();
            QVERIFY(!image.isNull());
            QVERIFY(image.save(path + "/" + name + ".png"));
        };
        QPointer<QObject> popup;
        QTRY_VERIFY((popup = root->findChild<QObject *>("playerbarMorePopup")));
        QTRY_VERIFY(popup->property("opened").toBool());
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (const double scale : {1.0, 1.25}) {
                window->setProperty("uiScale", scale);
                for (const bool motion : {true, false}) {
                    theme.setMotionEnabled(motion);
                    controller.playerbarActions()->restoreDefaults();
                    QTest::qWait(220);
                    auto *right = item("playerLayoutZone-3");
                    auto *more = item("playerLayoutZone-4");
                    auto *quality = item("playerLayoutChip-host/quality");
                    auto *effect = item("playerLayoutChip-host/effect");
                    QVERIFY(right && more && quality && effect);
                    auto *qualitySlot = quality->parentItem();
                    auto *effectSlot = effect->parentItem();
                    const auto savedOrder = preferences.value("shell/playerbar/order");
                    const auto savedPlacement = preferences.value("shell/playerbar/placements");
                    const auto originalEffect = effectSlot->mapToItem(right, QPointF{}).x();
                    const auto start = quality->mapToScene({2, 17}).toPoint();
                    const auto end = right->mapToScene({right->width() - 20, 47}).toPoint();
                    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
                    QTest::mouseMove(window, start + QPoint(10, 0));
                    QTest::mouseMove(window, end);
                    QTest::qWait(40);
                    const auto during = effectSlot->mapToItem(right, QPointF{}).x();
                    QVERIFY(during < originalEffect);
                    if (motion)
                        QVERIFY(during > originalEffect - 40);
                    QCOMPARE(preferences.value("shell/playerbar/order"), savedOrder);
                    QCOMPARE(preferences.value("shell/playerbar/placements"), savedPlacement);
                    capture(QString("held-within-%1-%2-%3").arg(int(mode)).arg(scale).arg(motion));
                    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
                    QTRY_VERIFY(preferences.value("shell/playerbar/order") != savedOrder);
                    QTest::qWait(220);
                    quality = item("playerLayoutChip-host/quality");
                    QVERIFY(quality && quality->parentItem() == qualitySlot);
                    // Cross-zone preview opens an insertion gap before the first
                    // More item without moving the source model or its grab.
                    const auto placement = preferences.value("shell/playerbar/placements");
                    const auto source = quality->mapToScene({2, 17}).toPoint();
                    const auto target = more->mapToScene({5, 47}).toPoint();
                    auto *cast = item("playerLayoutChip-host/cast");
                    QVERIFY(cast);
                    const auto castX = cast->mapToItem(more, QPointF{}).x();
                    QPointer<QQuickItem> heldSource(quality);
                    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, source);
                    QTest::mouseMove(window, source + QPoint(10, 0));
                    QTest::mouseMove(window, target);
                    QTest::qWait(40);
                    QVERIFY(heldSource);
                    QVERIFY(cast->mapToItem(more, QPointF{}).x() > castX);
                    QVERIFY(item("playerLayoutGap-4"));
                    QCOMPARE(preferences.value("shell/playerbar/placements"), placement);
                    QCOMPARE(right->property("count").toInt(), 4);
                    QCOMPARE(more->property("count").toInt(), 1);
                    capture(QString("held-cross-%1-%2-%3").arg(int(mode)).arg(scale).arg(motion));
                    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, target);
                    QTRY_COMPARE(more->property("count").toInt(), 2);
                    QTRY_COMPARE(right->property("count").toInt(), 3);
                    capture(
                        QString("landing-cross-%1-%2-%3").arg(int(mode)).arg(scale).arg(motion));
                    QTest::qWait(240);
                    quality = findVisibleVisual(more, "playerLayoutChip-host/quality");
                    QVERIFY(quality);
                    QVERIFY(qAbs(quality->mapToItem(more, QPointF{}).x()) < 0.5);
                    QVERIFY(qAbs(cast->mapToItem(more, QPointF{}).x() - 40) < 0.5);
                    capture(
                        QString("settled-cross-%1-%2-%3").arg(int(mode)).arg(scale).arg(motion));
                }
            }
        }
        theme.setMotionEnabled(true);
        window->setProperty("uiScale", 1.0);
        controller.playerbarActions()->restoreDefaults();
        QVERIFY(controller.playerbarActions()->moveInPlayerZone("host/favorite",
                                                                ShellActionModel::More));
        QTest::qWait(220);
        auto *left = item("playerLayoutZone-1");
        auto *right = item("playerLayoutZone-3");
        auto *more = item("playerLayoutZone-4");
        auto *quality = item("playerLayoutChip-host/quality");
        QVERIFY(left && right && more && quality);
        QTRY_COMPARE(left->property("count").toInt(), 0);
        const auto placements = preferences.value("shell/playerbar/placements");
        const auto source = quality->mapToScene({2, 17}).toPoint();
        const auto emptyTarget = left->mapToScene({8, 47}).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, source);
        QTest::mouseMove(window, source + QPoint(10, 0));
        QTest::mouseMove(window, emptyTarget);
        QTest::qWait(160);
        QVERIFY(item("playerLayoutGap-1"));
        QCOMPARE(left->property("count").toInt(), 0);
        QCOMPARE(preferences.value("shell/playerbar/placements"), placements);
        // Leaving every region and releasing cancels instead of saving the last
        // hovered target; the same source remains available for another drag.
        QTest::mouseMove(window, {20, 20});
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, {20, 20});
        QTest::qWait(200);
        QCOMPARE(preferences.value("shell/playerbar/placements"), placements);
        QCOMPARE(left->property("count").toInt(), 0);
        QVERIFY(item("playerLayoutChip-host/quality") == quality);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, source);
        QTest::mouseMove(window, source + QPoint(10, 0));
        QTest::mouseMove(window, emptyTarget);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, emptyTarget);
        QTRY_COMPARE(left->property("count").toInt(), 1);
        QTest::qWait(220);
        quality = findVisibleVisual(left, "playerLayoutChip-host/quality");
        QVERIFY(quality);
        const auto beforeClose = preferences.value("shell/playerbar/placements");
        const auto closingStart = quality->mapToScene({17, 17}).toPoint();
        const auto closingEnd = more->mapToScene({5, 47}).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, closingStart);
        QTest::mouseMove(window, closingStart + QPoint(10, 0));
        QTest::mouseMove(window, closingEnd);
        QTest::qWait(40);
        QVERIFY(item("host/quality"));
        QCOMPARE(warnings.size(), 0);
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QTRY_VERIFY(popup.isNull());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, closingEnd);
        QCOMPARE(preferences.value("shell/playerbar/placements"), beforeClose);
        QVERIFY(QMetaObject::invokeMethod(root.data(), "openEditor"));
        QTRY_VERIFY((popup = root->findChild<QObject *>("playerbarMorePopup")));
        QTRY_VERIFY(popup->property("opened").toBool());
        // A scrolled, virtualized target must use its logical origin for the
        // insertion gap and preserve that viewport when the new row arrives.
        QObject provider;
        for (int i = 0; i < 64; ++i) {
            ShellActionModel::Contribution contribution;
            contribution.key = "test/player-motion/" + QString::number(i);
            contribution.id = contribution.key;
            contribution.title = QStringLiteral("扩展动作 %1").arg(i);
            contribution.icon = "music";
            contribution.invoke = [] {};
            controller.playerbarActions()->registerAction(provider, std::move(contribution));
        }
        more = item("playerLayoutZone-4");
        QVERIFY(more);
        QTRY_COMPARE(more->property("count").toInt(), 66);
        more->setProperty("contentX", more->property("originX").toReal() + 400);
        QTest::qWait(40);
        auto *content = more->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        int liveSlots = 0;
        for (auto *child : content->childItems())
            liveSlots += child->objectName().startsWith("playerLayoutSlot-");
        QVERIFY(liveSlots > 0 && liveSlots < 32);
        qInfo() << "Player editor: 66 More actions, live slots" << liveSlots;
        quality = item("playerLayoutChip-host/quality");
        QVERIFY(quality);
        const auto scrolledStart = quality->mapToScene({17, 17}).toPoint();
        const auto scrolledEnd = more->mapToScene({85, 47}).toPoint();
        const auto scrollPlacement = preferences.value("shell/playerbar/placements");
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, scrolledStart);
        QTest::mouseMove(window, scrolledStart + QPoint(10, 0));
        QTest::mouseMove(window, scrolledEnd);
        QTest::qWait(60);
        auto *gap = item("playerLayoutGap-4");
        QVERIFY(gap);
        QVERIFY2(qAbs(gap->mapToItem(more, QPointF{}).x() - 80) < 0.5,
                 qPrintable(QStringLiteral("gap=%1 contentX=%2 originX=%3 parent=%4")
                                .arg(gap->mapToItem(more, QPointF{}).x())
                                .arg(more->property("contentX").toReal())
                                .arg(more->property("originX").toReal())
                                .arg(gap->parentItem()->objectName())));
        QCOMPARE(preferences.value("shell/playerbar/placements"), scrollPlacement);
        capture("held-scrolled");
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, scrolledEnd);
        QTRY_COMPARE(more->property("count").toInt(), 67);
        QTest::qWait(240);
        quality = findVisibleVisual(more, "playerLayoutChip-host/quality");
        QVERIFY(quality);
        QVERIFY(qAbs(quality->mapToItem(more, QPointF{}).x() - 80) < 0.5);
        capture("settled-scrolled");
        const auto beforeHide = preferences.value("shell/playerbar/placements");
        const auto hidingStart = quality->mapToScene({17, 17}).toPoint();
        left = item("playerLayoutZone-1");
        QVERIFY(left);
        const auto hidingEnd = left->mapToScene({8, 47}).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, hidingStart);
        QTest::mouseMove(window, hidingStart + QPoint(10, 0));
        QTest::mouseMove(window, hidingEnd);
        window->hide();
        QTest::qWait(40);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, hidingEnd);
        QCOMPARE(preferences.value("shell/playerbar/placements"), beforeHide);
        window->show();
        QVERIFY(Test::prepareNativeInput(window));
        QCOMPARE(warnings.size(), 0);
    }
    void productionTitlebarPinningTasksAndVirtualization()
    {
        QTemporaryDir temp;
        EchoMusic::SettingsStore preferences(temp.filePath("settings.ini"));
        ThemeStore theme(preferences);
        ShellSettings shell(preferences);
        shell.setMotionEnabled(false);
        WindowController windows(preferences, false);
        ApiClient api(DeviceIdentity::temporary());
        Test::AccountUiFixture accountUi{api};
        SearchModel search(api);
        RouteController routes;
        SearchRouteCoordinator searchRoutes(search, routes, false);
        ShellController controller(preferences, routes);
        routes.navigate("search");
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/App/Main.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"libraryController", QVariant::fromValue(&accountUi.library)},
             {"accountController", QVariant::fromValue(&accountUi.controller)},
             {"shellController", QVariant::fromValue(&controller)},
             {"shellSettings", QVariant::fromValue(&shell)},
             {"router", QVariant::fromValue(&routes)},
             {"windowService", QVariant::fromValue(&windows)},
             {"searchService", QVariant::fromValue(&search)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        windows.attach(window);
        window->resize(1150, 750);
        QVERIFY(Test::prepareNativeInput(window));
        auto item = [&](const QString &name) {
            return findVisibleVisual(window->contentItem(), name);
        };
        auto click = [&](QQuickItem *target) {
            QVERIFY(Test::prepareNativeInput(window));
            // Popup.opened/property changes precede layout polish. Derive input
            // coordinates only after the production window renders that state.
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
            window->update();
            QTRY_VERIFY(!frames.isEmpty());
            QTest::mouseClick(
                window, Qt::LeftButton, Qt::NoModifier,
                target->mapToScene({target->width() / 2, target->height() / 2}).toPoint());
        };
        const auto capture = [&](const QString &name) {
            const auto directory = qEnvironmentVariable("ECHO_SHELL_CAPTURE_DIR");
            if (directory.isEmpty())
                return;
            QVERIFY(Test::prepareNativeInput(window));
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
            window->update();
            QTRY_VERIFY(!frames.isEmpty());
            QVERIFY(QDir().mkpath(directory));
            const auto image = window->grabWindow();
            QVERIFY(!image.isNull());
            QVERIFY(image.save(directory + "/" + name + ".png"));
        };
        auto *playerBar = item("playerbar");
        auto *playerCenter = item("playerbarCenterArea");
        auto *progressRow = item("playerbarProgressRow");
        QVERIFY(playerBar && playerCenter && progressRow);
        // Check the production geometry, including the unequal implicit size
        // policies of a nested RowLayout and a plain Item on the two sides.
        for (const int width : {900, 1150, 1600}) {
            window->resize(width, 750);
            QTRY_VERIFY(
                qAbs(playerCenter->mapToItem(playerBar, {playerCenter->width() / 2, 0}).x() -
                     playerBar->width() / 2) < 0.6);
            QTRY_VERIFY(progressRow->width() >= qMin(playerCenter->width(), 480.0) - 8.5);
            QTRY_VERIFY(qAbs(progressRow->mapToItem(playerBar, {progressRow->width() / 2, 0}).x() -
                             playerBar->width() / 2) < 0.6);
        }
        window->resize(1150, 750);
        QQuickItem *playButton = nullptr, *playGlyph = nullptr;
        QTRY_VERIFY((playButton = item("playerbar-action-play-toggle")));
        QTRY_VERIFY((playGlyph = findVisual(playButton, "iconImage")));
        QTRY_COMPARE(playGlyph->property("status").toInt(), 1); // Asynchronous image is Ready.
        capture("playerbar-centered");
        auto *more = item("titlebarMoreAction");
        QVERIFY(more);
        auto *toolbar = findVisual(window->contentItem(), "titlebarToolbar");
        QVERIFY(toolbar);
        QTRY_COMPARE(toolbar->property("count").toInt(), 2);
        QQuickItem *taskIcon = nullptr;
        QTRY_VERIFY((taskIcon = item("shellDragPointer-host/tasks")));
        Test::moveNativePointer(
            window,
            taskIcon->mapToScene({taskIcon->width() / 2, taskIcon->height() / 2}).toPoint());
        QTRY_COMPARE(taskIcon->property("cursorShape").toInt(), int(Qt::ArrowCursor));
        QTRY_COMPARE(window->cursor().shape(), Qt::ArrowCursor);
        auto *taskButton = item("titlebar-action-tasks");
        QVERIFY(taskButton);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            QTRY_VERIFY(taskButton->property("hovered").toBool());
            QTRY_COMPARE(taskButton->property("foreground").value<QColor>(), theme.text());
            QVERIFY(taskButton->property("foreground").value<QColor>() != theme.accentText());
        }
        // Reordering starts even in the button padding, without a business
        // activation or native caption move; native button clicks still work.
        auto *recognizeButton = item("titlebar-action-recognize");
        QVERIFY(recognizeButton);
        auto *taskHandle = findVisual(taskButton, "shellDragHandle-host/tasks");
        QVERIFY(taskHandle);
        QSignalSpy taskClicks(taskButton, SIGNAL(clicked()));
        auto *caption = item("titlebarWindowDragArea");
        QVERIFY(caption);
        QSignalSpy captionMoves(caption, SIGNAL(moveRequested()));
        auto *titlebar = item("shellTitleBar");
        QVERIFY(titlebar);
        // Include the padding above/below controls: it used to fall through to
        // the entire-bar caption even when the icon itself was protected.
        for (auto *target : {taskButton, recognizeButton, item("titlebarBackAction"), more}) {
            QVERIFY(target);
            for (const qreal y : {1.0, titlebar->height() - 1}) {
                const auto center = target->mapToItem(titlebar, target->boundingRect().center());
                const auto start = titlebar->mapToScene({center.x(), y}).toPoint();
                QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
                QTest::mouseMove(window, start + QPoint(24, 0));
                QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + QPoint(24, 0));
                QCOMPARE(captionMoves.size(), 0);
            }
        }
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa") {
            auto *guard = item("titlebarInputGuard");
            QVERIFY(guard);
            QSignalSpy nativePresses(guard, SIGNAL(pressedChanged()));
            QVERIFY(nativePresses.isValid());
            for (const qreal y : {1.0, titlebar->height() - 1}) {
                const auto center =
                    taskButton->mapToItem(titlebar, taskButton->boundingRect().center());
                const auto start = titlebar->mapToScene({center.x(), y}).toPoint();
                const auto original = window->position();
                const auto delivered = nativePresses.size();
                Test::postNativeLeftMouse(window, start, QEvent::MouseButtonPress);
                Test::postNativeLeftMouse(window, start + QPoint(24, 0), QEvent::MouseMove);
                Test::postNativeLeftMouse(window, start + QPoint(24, 0),
                                          QEvent::MouseButtonRelease);
                QTRY_VERIFY(nativePresses.size() >= delivered + 2);
                QCOMPARE(window->position(), original);
                QCOMPARE(captionMoves.size(), 0);
            }
        }
#endif
        for (const bool motion : {true, false}) {
            theme.setMotionEnabled(motion);
            shell.setMotionEnabled(motion);
            for (const bool zoomed : {false, true}) {
                shell.resetZoom();
                if (zoomed)
                    for (int step = 0; step < 5; ++step)
                        shell.zoomIn();
                QTest::qWait(250);
                for (const bool before : {true, false}) {
                    const auto oldX = taskButton->x();
                    const auto savedOrder = preferences.value("shell/titlebar/order");
                    const auto normalPosition = window->position();
                    const auto start =
                        taskButton->mapToScene({2, taskButton->height() / 2}).toPoint();
                    const auto destination =
                        recognizeButton
                            ->mapToScene({recognizeButton->width() * (before ? 0.25 : 0.75),
                                          recognizeButton->height() / 2})
                            .toPoint();
                    Test::moveNativePointer(window, start);
                    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
                    QVERIFY(taskButton->property("pressed").toBool());
                    QTest::mouseMove(window, start + QPoint(0, 8));
                    QTest::mouseMove(window, destination);
                    QTRY_VERIFY(taskHandle->property("dragging").toBool());
                    // Verify the actual exchange while the mouse is still down,
                    // rather than mistaking a post-release transition for preview.
                    QTest::qWait(45);
                    if (motion) {
                        const auto duringX = taskButton->x();
                        QVERIFY2(before ? duringX < oldX && duringX > 0
                                        : duringX > oldX && duringX < 38,
                                 qPrintable(QString("held preview before=%1 old=%2 during=%3")
                                                .arg(before)
                                                .arg(oldX)
                                                .arg(duringX)));
                    }
                    auto *ghost = findVisual(window->contentItem(), "host/tasks");
                    QVERIFY(ghost && ghost->isVisible());
                    QVERIFY(QLineF(ghost->mapToScene(ghost->boundingRect().center()), destination)
                                .length() < 2);
                    QCOMPARE(preferences.value("shell/titlebar/order"), savedOrder);
                    QCOMPARE(captionMoves.size(), 0);
                    QVERIFY(!taskButton->property("pressed").toBool());
                    QCOMPARE(window->position(), normalPosition);
                    capture(
                        QString("titlebar-held-swap-%1-%2-%3").arg(motion).arg(zoomed).arg(before));
                    QTRY_COMPARE(taskButton->x(), before ? 0.0 : 38.0);
                    QCOMPARE(preferences.value("shell/titlebar/order"), savedOrder);
                    // Keeping the pointer still after the source moves must not
                    // cause the preview to oscillate back under its own handler.
                    QTest::qWait(80);
                    QCOMPARE(taskButton->x(), before ? 0.0 : 38.0);
                    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, destination);
                    QTRY_VERIFY(!taskHandle->property("dragging").toBool());
                    QTRY_VERIFY(preferences.value("shell/titlebar/order") != savedOrder);
                    QCOMPARE(taskClicks.count(), 0);
                    QTRY_COMPARE(taskButton->x(), before ? 0.0 : 38.0);
                    QTest::qWait(50); // Settle the displaced peer before the reverse gesture.
                    QVERIFY(!window->mouseGrabberItem());
                }
            }
        }
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa")
            qInfo() << "Native input policy" << Test::nativeWindowInputPolicy(window);
        if (QGuiApplication::platformName() == "cocoa")
            QVERIFY(!Test::nativeWindowInputPolicy(window).value("movable").toBool());
#endif
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa") {
            qInfo() << "System mouse posting" << Test::systemMousePostingAvailable();
            if (Test::systemMousePostingAvailable()) {
                theme.setMotionEnabled(true);
                shell.resetZoom();
                QTest::qWait(200);
                for (int round = 0; round < 40; ++round) {
                    const auto original = window->position();
                    const auto start =
                        taskButton
                            ->mapToGlobal({round % 3 == 0   ? 2.0
                                           : round % 3 == 1 ? taskButton->width() / 2
                                                            : taskButton->width() - 2,
                                           taskButton->height() / 2})
                            .toPoint();
                    const auto end = toolbar
                                         ->mapToGlobal({round % 2 ? toolbar->width() - 12 : 8.0,
                                                        toolbar->height() / 2})
                                         .toPoint();
                    QCursor::setPos(start);
                    std::atomic_bool done = false;
                    std::thread native([&] {
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                        Test::postSystemLeftMouse(start, QEvent::MouseButtonPress);
                        std::this_thread::sleep_for(std::chrono::milliseconds(25));
                        Test::postSystemLeftMouse(start + QPoint(0, 8), QEvent::MouseMove);
                        for (int step = 1; step <= 4; ++step) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(15));
                            Test::postSystemLeftMouse(start + (end - start) * step / 4,
                                                      QEvent::MouseMove);
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(15));
                        Test::postSystemLeftMouse(end, QEvent::MouseButtonRelease);
                        done = true;
                    });
                    const auto join = qScopeGuard([&] { native.join(); });
                    QTRY_VERIFY(done.load());
                    QTRY_COMPARE(QGuiApplication::mouseButtons(), Qt::MouseButtons(Qt::NoButton));
                    QCOMPARE(window->position(), original);
                    QCOMPARE(captionMoves.size(), 0);
                    QCOMPARE(taskClicks.size(), 0);
                    QVERIFY(!Test::nativeWindowInputPolicy(window).value("movable").toBool());
                    QVERIFY(!taskHandle->property("dragging").toBool());
                }
                const auto original = window->position();
                const auto start = caption->mapToGlobal(caption->boundingRect().center()).toPoint();
                const auto end = start + QPoint(30, 20);
                std::atomic_bool done = false;
                std::thread native([&] {
                    Test::postSystemLeftMouse(start, QEvent::MouseButtonPress);
                    std::this_thread::sleep_for(std::chrono::milliseconds(40));
                    for (int step = 1; step <= 6; ++step) {
                        Test::postSystemLeftMouse(start + (end - start) * step / 6,
                                                  QEvent::MouseMove);
                        std::this_thread::sleep_for(std::chrono::milliseconds(25));
                    }
                    Test::postSystemLeftMouse(end, QEvent::MouseButtonRelease);
                    done = true;
                });
                const auto join = qScopeGuard([&] { native.join(); });
                QTRY_VERIFY(done.load());
                QTRY_VERIFY(window->position() != original);
                QTRY_COMPARE(captionMoves.size(), 1);
                QTRY_COMPARE(QGuiApplication::mouseButtons(), Qt::MouseButtons(Qt::NoButton));
                QVERIFY(!Test::nativeWindowInputPolicy(window).value("movable").toBool());
                QVERIFY(!windows.startSystemMove()); // No held native left button.
                QVERIFY(!Test::nativeWindowInputPolicy(window).value("movable").toBool());
                window->setPosition(original);
                captionMoves.clear();
            }
        }
#endif
        // A second drag can begin before the previous animation and queued
        // commit finish. Do not settle the view between these gestures.
        theme.setMotionEnabled(true);
        for (int round = 0; round < 80; ++round) {
            const auto rect = taskButton->mapRectToItem(toolbar, taskButton->boundingRect());
            QVERIFY2(rect.left() >= -0.5 && rect.right() <= toolbar->width() + 0.5,
                     qPrintable(QString("round=%1 rect=%2,%3 contentX=%4 captionX=%5")
                                    .arg(round)
                                    .arg(rect.left())
                                    .arg(rect.right())
                                    .arg(toolbar->property("contentX").toReal())
                                    .arg(caption->x())));
            const auto start = taskButton
                                   ->mapToScene({round % 3 == 0   ? 2.0
                                                 : round % 3 == 1 ? taskButton->width() / 2
                                                                  : taskButton->width() - 2,
                                                 taskButton->height() / 2})
                                   .toPoint();
            const auto end =
                toolbar
                    ->mapToScene({round % 2 ? toolbar->width() - 12 : 8.0, toolbar->height() / 2})
                    .toPoint();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
            QVERIFY2(taskButton->property("pressed").toBool(),
                     qPrintable(QString("re-press round=%1 start=%2,%3")
                                    .arg(round)
                                    .arg(start.x())
                                    .arg(start.y())));
            QTest::mouseMove(window, start + QPoint(0, 8));
            QTest::mouseMove(window, end);
            QVERIFY2(taskHandle->property("dragging").toBool(),
                     qPrintable(QString("re-drag round=%1").arg(round)));
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
            QCOMPARE(captionMoves.size(), 0);
            QVERIFY(!window->mouseGrabberItem());
            if (round % 4)
                QTest::qWait(round % 4 == 1 ? 1 : round % 4 == 2 ? 16 : 45);
        }
        QTest::qWait(250);
        controller.titlebarActions()->moveAfter("host/tasks", "host/recognize",
                                                ShellActionModel::Toolbar);
        QVERIFY(QMetaObject::invokeMethod(toolbar, "positionViewAtBeginning"));
        QTest::qWait(250);
        const auto visualTaskX = [&] { return taskButton->mapToItem(toolbar, QPointF{}).x(); };
        QTRY_COMPARE(visualTaskX(), 38.0);
        shell.resetZoom();
        // Cancelling a held preview restores the view without saving anything.
        const auto beforeCancel = preferences.value("shell/titlebar/order");
        const auto cancelStart = taskButton->mapToScene({2, taskButton->height() / 2}).toPoint();
        const auto cancelEnd =
            recognizeButton->mapToScene({8, recognizeButton->height() / 2}).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, cancelStart);
        QTest::mouseMove(window, cancelStart + QPoint(0, 8));
        QTest::mouseMove(window, cancelEnd);
        QTRY_VERIFY(taskHandle->property("dragging").toBool());
        QTRY_COMPARE(visualTaskX(), 0.0);
        window->hide();
        QTRY_VERIFY(!taskHandle->property("dragging").toBool());
        QCOMPARE(preferences.value("shell/titlebar/order"), beforeCancel);
        QTRY_COMPARE(visualTaskX(), 38.0);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, cancelEnd);
        window->show();
        QVERIFY(Test::prepareNativeInput(window));
        // Hiding or disabling motion settles a transition already in flight.
        theme.setMotionEnabled(true);
        QVERIFY(
            controller.titlebarActions()->shiftAction("host/tasks", -1, ShellActionModel::Toolbar));
        QTest::qWait(45);
        window->hide();
        QTRY_COMPARE(visualTaskX(), 0.0);
        window->show();
        QVERIFY(Test::prepareNativeInput(window));
        QVERIFY(
            controller.titlebarActions()->shiftAction("host/tasks", 1, ShellActionModel::Toolbar));
        QTest::qWait(40);
        theme.setMotionEnabled(false);
        QTRY_COMPARE(visualTaskX(), 38.0);
        shell.setMotionEnabled(false);
        theme.setMotionEnabled(true);
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa")
            QVERIFY(!Test::nativeWindowInputPolicy(window).value("movable").toBool());
#endif
        click(more);
        QPointer<QObject> menu = root->findChild<QObject *>("titlebarMorePopup");
        QVERIFY(menu);
        QTRY_VERIFY(menu->property("opened").toBool());
        QCOMPARE(menu->property("width").toReal(), 280.0);
        auto *list = item("titlebarMoreList");
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 6);
        auto *pin = item("titlebarPin-host/tasks");
        QVERIFY(pin);
        // Real popup text must align with the drag icon's center after font
        // fallback; checking the Label rectangle alone missed this regression.
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (const bool zoomed : {false, true}) {
                shell.resetZoom();
                if (zoomed)
                    for (int step = 0; step < 5; ++step)
                        shell.zoomIn();
                QTest::qWait(100);
                auto *label = item("titlebarMoreLabel-host/tasks");
                auto *action = item("titlebarMenuAction-host/tasks");
                QVERIFY(label && action);
                auto *box = label->property("textBox").value<QQuickItem *>();
                auto *handle =
                    findVisual(action->parentItem()->parentItem(), "shellDragHandle-host/tasks");
                QVERIFY(box && handle);
                const auto image = window->grabWindow();
                QVERIFY(!image.isNull());
                const auto ratio = image.width() / qreal(window->width());
                const auto rect =
                    box->mapRectToScene(QRectF(0, -2, box->width(), box->height() + 4));
                const auto pixels = QRectF(rect.topLeft() * ratio, rect.size() * ratio)
                                        .toAlignedRect()
                                        .intersected(image.rect());
                const auto foreground = label->property("color").value<QColor>();
                int top = image.height(), bottom = -1;
                for (int y = pixels.top(); y <= pixels.bottom(); ++y)
                    for (int x = pixels.left(); x <= pixels.right(); ++x) {
                        const auto pixel = image.pixelColor(x, y);
                        if (qAbs(pixel.red() - foreground.red()) +
                                qAbs(pixel.green() - foreground.green()) +
                                qAbs(pixel.blue() - foreground.blue()) <
                            80) {
                            top = std::min(top, y);
                            bottom = std::max(bottom, y);
                        }
                    }
                QVERIFY(bottom >= top);
                const auto center = handle->mapToScene(QPointF(0, handle->height() / 2)).y();
                QVERIFY(qAbs((top + bottom + 1) / (2 * ratio) - center) <= shell.uiScale());
                capture(QString("titlebar-more-aligned-%1-%2").arg(int(mode)).arg(shell.uiScale()));
                QPointer<QQuickItem> keptPin(pin), keptAction(action);
                const auto height = menu->property("height").toReal();
                for (int i = 0; i < 10; ++i) {
                    const auto glyph = pin->property("glyph").toString();
                    click(pin);
                    QVERIFY(keptPin && keptAction);
                    QCOMPARE(item("titlebarPin-host/tasks"), keptPin.data());
                    QCOMPARE(pin->property("glyph").toString(),
                             glyph == "pin" ? QString("pinned") : QString("pin"));
                    QVERIFY(menu && menu->property("opened").toBool());
                    QCOMPARE(menu->property("height").toReal(), height);
                }
                action->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window, Qt::Key_Down, Qt::AltModifier);
                QTest::qWait(20);
                QVERIFY(keptAction && keptPin);
                QTest::keyClick(window, Qt::Key_Up, Qt::AltModifier);
                QTest::qWait(20);
                QVERIFY(keptAction && keptPin);
            }
        }
        shell.resetZoom();
        capture("titlebar-more");
        click(pin); // Unpin the command; it remains accessible in More.
        QTRY_COMPARE(toolbar->property("count").toInt(), 1);
        auto *taskCommand = item("titlebarMenuAction-host/tasks");
        QVERIFY(taskCommand);
        click(taskCommand);
        QPointer<QObject> taskDialog = root->findChild<QObject *>("taskCenterDialog");
        QVERIFY(taskDialog);
        QTRY_VERIFY((taskDialog = root->findChild<QObject *>("taskCenterDialog")));
        QTRY_VERIFY(taskDialog->property("opened").toBool());
        auto *taskClose = item("taskCenterClose");
        QVERIFY(taskClose);
        QTRY_COMPARE(taskClose->size(), QSizeF(32, 32));
        const auto taskScale = taskDialog->property("uiScale").toReal();
        QTRY_VERIFY(qAbs(taskClose->mapToScene(QPointF(32, 0)).x() -
                         (taskDialog->property("x").toReal() +
                          (taskDialog->property("width").toReal() - 24) * taskScale)) < 0.5);
        QTRY_VERIFY(menu.isNull());
        auto *empty = item("taskCenterEmpty");
        QVERIFY(empty);
        QVERIFY(empty->isVisible());
        auto *emptyTitle = findVisual(empty, "taskCenterEmptyTitle");
        QVERIFY(emptyTitle);
        QCOMPARE(emptyTitle->property("text").toString(), QString("暂无任务"));
        capture("task-center-empty");
        // Native focus may change while asynchronous popup transitions complete.
        QVERIFY(Test::prepareNativeInput(window));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(taskDialog.isNull());
        click(more);
        QTRY_VERIFY((menu = root->findChild<QObject *>("titlebarMorePopup")));
        QTRY_VERIFY(menu->property("opened").toBool());
        list = item("titlebarMoreList");
        QVERIFY(list);
        auto *reset = item("restoreTitlebarLayout");
        QVERIFY(reset);
        QPointer<QQuickItem> resetPin = item("titlebarPin-host/tasks");
        click(reset);
        QVERIFY(resetPin);
        QCOMPARE(item("titlebarPin-host/tasks"), resetPin.data());
        QTRY_COMPARE(toolbar->property("count").toInt(), 2);
        // Ancestor geometry changes update the same open popup, without a guessed offset.
        shell.zoomIn();
        shell.toggleSidebar();
        QTRY_VERIFY(qAbs(menu->property("scale").toReal() - shell.uiScale()) < 0.001);
        auto *menuContent = menu->property("contentItem").value<QQuickItem *>();
        QVERIFY(menuContent);
        QTRY_VERIFY2(qAbs(menuContent->mapToScene({menuContent->width(), 0}).x() +
                          8 * shell.uiScale() - more->mapToScene({more->width(), 0}).x()) < 2,
                     "Popup must follow the moving trigger through zoom/collapse");
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(menu.isNull());
        shell.resetZoom();
        shell.toggleSidebar();
        QObject playerProvider;
        int qualityCalls = 0;
        const auto quality = controller.bindPlayerAction(playerProvider, ShellController::Quality,
                                                         [&] { ++qualityCalls; });
        QVERIFY(controller.playerbarActions()->setState(quality, true, true, false, "HD"));
        auto *playerMore = item("playerbarMoreAction");
        QVERIFY(playerMore);
        click(playerMore);
        QPointer<QObject> playerMenu = root->findChild<QObject *>("playerbarMorePopup");
        QVERIFY(playerMenu);
        QTRY_VERIFY(playerMenu->property("opened").toBool());
        auto *edit = item("editPlayerbarLayout");
        QVERIFY(edit);
        click(edit);
        QTRY_VERIFY(playerMenu->property("editing").toBool());
        QTRY_COMPARE(playerMenu->property("width").toReal(), 860.0);
        auto *rightZone = item("playerLayoutZone-3"), *moreZone = item("playerLayoutZone-4");
        QVERIFY(rightZone && moreZone);
        QTRY_COMPARE(rightZone->property("count").toInt(),
                     4); // Saved placement includes an overflowing Queue.
        QTRY_COMPARE(moreZone->property("count").toInt(), 1);
        capture("playerbar-editor");
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark})
            for (const bool zoomed : {false, true}) {
                theme.setMode(mode);
                shell.resetZoom();
                if (zoomed)
                    for (int step = 0; step < 5; ++step)
                        shell.zoomIn();
                QTest::qWait(100);
                QPointer<QQuickItem> keptBadge = item("playerBadgePreference-host/quality");
                QPointer<QQuickItem> keptPlay = item("playerLayoutChip-host/play-toggle");
                QVERIFY(keptBadge && keptPlay);
                const auto height = playerMenu->property("height").toReal();
                for (int i = 0; i < 10; ++i) {
                    const auto before =
                        keptBadge->parentItem()->parentItem()->property("badgePreferred").toBool();
                    click(keptBadge);
                    QVERIFY(keptBadge && keptPlay);
                    QCOMPARE(preferences.value("shell/playerbar/badges")
                                 .toMap()
                                 .value("host/quality")
                                 .toBool(),
                             !before);
                    QVERIFY(playerMenu && playerMenu->property("opened").toBool());
                    QCOMPARE(playerMenu->property("height").toReal(), height);
                }
                keptPlay->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window, Qt::Key_Down, Qt::AltModifier);
                QTest::qWait(20);
                QVERIFY(keptPlay && keptBadge);
                QTest::keyClick(window, Qt::Key_Up, Qt::AltModifier);
                QTest::qWait(20);
                QVERIFY(keptPlay && keptBadge);
                auto *restore = item("restorePlayerbarLayout");
                QVERIFY(restore);
                click(restore);
                QVERIFY(keptPlay && keptBadge);
                QCOMPARE(item("playerLayoutChip-host/play-toggle"), keptPlay.data());
            }
        shell.resetZoom();
        auto *qualityChip = item("playerLayoutChip-host/quality");
        QVERIFY(qualityChip);
        QSignalSpy editorFrames(window, &QQuickWindow::frameSwapped);
        window->update();
        QTRY_VERIFY(!editorFrames.isEmpty());
        const auto dragStart = qualityChip->mapToScene({17, 17}).toPoint();
        const auto dragEnd = moreZone->mapToScene({moreZone->width() - 30, 40}).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, dragStart);
        QTest::mouseMove(window, dragStart + QPoint(10, 0), 20);
        QTest::mouseMove(window, dragStart + QPoint(20, 0), 20);
        QTRY_VERIFY(item("host/quality")); // Real Drag source is active before crossing zones.
        // The ghost must escape the per-zone ListView/ScrollView clips while the
        // pointer travels between targets; only the drop used to be visible.
        auto *dragGhost = item("host/quality");
        QVERIFY(dragGhost && dragGhost->isVisible());
        QCOMPARE(dragGhost->parentItem(), item("playerLayoutBoard"));
        QTest::mouseMove(window, dragEnd, 40);
        QVERIFY(dragGhost->isVisible());
        QCoreApplication::processEvents();
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, dragEnd);
        QTRY_COMPARE(moreZone->property("count").toInt(), 2);
        QTRY_COMPARE(rightZone->property("count").toInt(), 3);
        // The departing Right view may pool its old quality delegate. Select
        // the actual destination's control rather than that retained QObject.
        QQuickItem *badge = nullptr;
        QTRY_VERIFY((badge = findVisibleVisual(moreZone, "playerBadgePreference-host/quality")));
        auto *checkBox = badge->property("contentItem").value<QQuickItem *>();
        QVERIFY(checkBox);
        QTRY_COMPARE(checkBox->size(), QSizeF(14, 14));
        // Native row insertion publishes count before its new delegate polishes.
        QTRY_VERIFY(badge->mapToItem(moreZone, {0, 0}).y() >= 0);
        QTRY_VERIFY(badge->mapToItem(moreZone, {0, badge->height()}).y() <= moreZone->height());
        capture("playerbar-editor-moved");
        click(badge);
        QVERIFY(
            !preferences.value("shell/playerbar/badges").toMap().value("host/quality").toBool());
        auto *back = item("returnPlayerbarFunctions");
        QVERIFY(back);
        click(back);
        QTRY_VERIFY(!playerMenu->property("editing").toBool());
        // Returning from the editor switches the production ListView projection;
        // editing=false can precede instantiation of its visible delegates.
        QQuickItem *qualityMenu = nullptr;
        QTRY_VERIFY((qualityMenu = item("playerbarMenuAction-host/quality")));
        click(qualityMenu);
        QCOMPARE(qualityCalls, 1);
        QTRY_VERIFY(playerMenu.isNull());
        QObject provider;
        QElapsedTimer elapsed;
        elapsed.start();
        for (int i = 0; i < 1000; ++i) {
            ShellActionModel::Contribution contribution;
            contribution.key = "plugin/perf/" + QString::number(i);
            contribution.id = QString::number(i);
            contribution.title = "扩展动作 " + QString::number(i);
            contribution.icon = "music";
            contribution.invoke = [] {};
            controller.titlebarActions()->registerAction(provider, std::move(contribution));
        }
        click(more);
        QTRY_VERIFY((menu = root->findChild<QObject *>("titlebarMorePopup")));
        QTRY_VERIFY(menu->property("opened").toBool());
        list = item("titlebarMoreList");
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 1006);
        const auto countNamed = [&](const QString &name) {
            std::function<int(QQuickItem *)> walk = [&](QQuickItem *node) {
                int count = node->objectName() == name;
                for (auto *child : node->childItems())
                    count += walk(child);
                return count;
            };
            return walk(window->contentItem());
        };
        QTRY_VERIFY(countNamed("titlebarMoreRow") > 0);
        QVERIFY(countNamed("titlebarMoreRow") < 30);
        auto *virtualHandle = findVisibleVisual(list, "shellDragHandle-host/tasks");
        QVERIFY(virtualHandle);
        const auto virtualStart = virtualHandle->mapToScene({10, 10}).toPoint();
        const auto savedOrder = preferences.value("shell/titlebar/order");
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, virtualStart);
        QTest::mouseMove(window, virtualStart + QPoint(10, 0), 20);
        QTest::mouseMove(window, virtualStart + QPoint(20, 0), 20);
        QTRY_VERIFY(item("host/tasks"));
        auto *editingPointer = findVisual(virtualHandle, "shellDragPointer-host/tasks");
        QVERIFY(editingPointer);
        QTRY_COMPARE(editingPointer->property("cursorShape").toInt(), int(Qt::ArrowCursor));
        QTRY_COMPARE(window->cursor().shape(), Qt::ArrowCursor);
        list->setProperty("contentY", list->property("contentHeight").toReal() - list->height());
        QTRY_VERIFY(
            !item("host/tasks")); // Pooling cancels the original gesture before roles change.
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, virtualStart + QPoint(20, 0));
        QCOMPARE(preferences.value("shell/titlebar/order"), savedOrder);
        QCoreApplication::processEvents();
        QVERIFY(countNamed("titlebarMoreRow") < 30);
        qInfo() << "Production titlebar menu: 1006 actions, live delegates"
                << countNamed("titlebarMoreRow") << "registration + open + scroll ms"
                << elapsed.elapsed();
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(menu.isNull());
        QVERIFY(!item("titlebarMoreList"));
        TaskCenterModel::Registration registration;
        registration.id = "large";
        registration.priority = 100;
        registration.content.name = "导入任务";
        registration.content.percent = 42;
        int taskActionCalls = 0;
        registration.content.actions.push_back(
            {"result", "查看结果", TaskAction::Ghost, true, true, [&] { ++taskActionCalls; }});
        registration.content.actions.push_back(
            {"cancel", "取消当前任务", TaskAction::Primary, true, false, [] {}});
        registration.content.actions.push_back(
            {"report", "查看报告", TaskAction::Danger, true, false, [] {}});
        for (int i = 0; i < 5000; ++i)
            registration.content.items.push_back(
                {QString::number(i), "条目 " + QString::number(i), "说明", {}, "等待", {}});
        const auto handle = controller.tasks()->registerTask(provider, registration);
        for (int i = 0; i < 999; ++i) {
            TaskCenterModel::Registration next;
            next.id = "task/" + QString::number(i);
            next.content.name = "后台任务 " + QString::number(i);
            controller.tasks()->registerTask(provider, next);
        }
        QVERIFY(controller.titlebarActions()->activate("host/tasks"));
        QTRY_VERIFY((taskDialog = root->findChild<QObject *>("taskCenterDialog")));
        QTRY_VERIFY(taskDialog->property("opened").toBool());
        QPointer<QQuickItem> tasks = item("taskCenterList");
        QVERIFY(tasks);
        QQuickItem *detailsToggle = nullptr;
        QTRY_VERIFY((detailsToggle = item("taskDetailsToggle-large")));
        click(detailsToggle);
        QQuickItem *details = nullptr;
        QTRY_VERIFY((details = item("taskCenterDetails")));
        QTRY_VERIFY(details->isVisible());
        QTRY_VERIFY(tasks->height() > 200);
        QCOMPARE(taskDialog->property("width").toReal(), 600.0);
        auto *taskScrollBar = item("taskCenterScrollBar");
        QVERIFY(taskScrollBar);
        QTRY_COMPARE(taskScrollBar->x() + taskScrollBar->width(),
                     taskDialog->property("width").toReal());
        QPointer<QQuickItem> closedDetails = details;
        QPointer<QQuickItem> closedTask = findVisual(tasks, "taskCenterEntry");
        QVERIFY(closedTask);
        auto *taskAction = item("taskAction-large//result");
        QVERIFY(taskAction);
        QVERIFY(taskAction->width() > 30 && taskAction->height() == 36);
        auto *cancelAction = item("taskAction-large//cancel"),
             *reportAction = item("taskAction-large//report");
        auto *actionRow = item("taskActionRow-large");
        QVERIFY(cancelAction && reportAction && actionRow);
        QTRY_COMPARE(cancelAction->y(), taskAction->y());
        QTRY_COMPARE(cancelAction->x() - taskAction->x() - taskAction->width(), 8.0);
        QTRY_COMPARE(reportAction->y(), cancelAction->y());
        QTRY_COMPARE(reportAction->x() - cancelAction->x() - cancelAction->width(), 8.0);
        QTRY_COMPARE(reportAction->x() + reportAction->width(), actionRow->width());
        auto *taskName = item("taskName-large");
        QVERIFY(taskName);
        QCOMPARE(taskName->property("textItem").value<QObject *>()->property("textFormat").toInt(),
                 0); // PlainText, as in source interpolation.
        capture("task-center");
        // Compact windows wrap actions while keeping every button within the task row.
        const auto normalWidth = window->width();
        const auto normalMinimumWidth = window->minimumWidth();
        window->setMinimumWidth(
            320); // Exercise the source wrapped actions below the product baseline.
        window->setWidth(320);
        QTRY_VERIFY(taskDialog->property("width").toReal() < 500);
        QTRY_COMPARE(reportAction->y() - cancelAction->y() - cancelAction->height(), 8.0);
        QTRY_COMPARE(cancelAction->x() + cancelAction->width(), actionRow->width());
        QTRY_COMPARE(reportAction->x() + reportAction->width(), actionRow->width());
        capture("task-center-compact");
        window->setWidth(normalWidth);
        window->setMinimumWidth(normalMinimumWidth);
        QTRY_COMPARE(taskDialog->property("width").toReal(), 600.0);
        QTRY_COMPARE(reportAction->y(), cancelAction->y());
        click(taskAction);
        QTRY_COMPARE(taskActionCalls, 1);
        QTRY_VERIFY(taskDialog.isNull());
        QTRY_VERIFY(closedDetails.isNull()); // Closing really releases nested delegates.
        QTRY_VERIFY(closedTask.isNull());
        QVERIFY(controller.titlebarActions()->activate("host/tasks"));
        QTRY_VERIFY((taskDialog = root->findChild<QObject *>("taskCenterDialog")));
        QTRY_VERIFY(taskDialog->property("opened").toBool());
        QCOMPARE(controller.tasks()->index(0, 0).data(TaskCenterModel::IdRole).toString(),
                 QString("large"));
        tasks = item("taskCenterList");
        QVERIFY(tasks);
        QQuickItem *reopenedTask = nullptr;
        QTRY_VERIFY((reopenedTask = findVisual(tasks, "taskCenterEntry")));
        QTRY_COMPARE(reopenedTask->property("taskId").toString(), QString("large"));
        capture("task-center-reopened");
        QTRY_VERIFY((detailsToggle = item("taskDetailsToggle-large")));
        click(detailsToggle);
        QTRY_VERIFY((details = item("taskCenterDetails")));
        QTRY_VERIFY(details->isVisible());
        QTRY_VERIFY(countNamed("taskCenterEntry") > 0);
        QVERIFY(countNamed("taskCenterEntry") < 30);
        QVERIFY(countNamed("taskCenterDetailRow") < 40);
        const auto detailY = details->property("contentY").toReal();
        controller.tasks()->updateProgress(handle, 83, "正在导入");
        QCoreApplication::processEvents();
        QCOMPARE(details->property("contentY").toReal(), detailY);
        tasks->setProperty("contentY", tasks->property("contentHeight").toReal() - tasks->height());
        QCoreApplication::processEvents();
        QVERIFY(countNamed("taskCenterEntry") < 30);
        qInfo() << "Production task dialog: 1000 tasks / 5000 details, live task/detail delegates"
                << countNamed("taskCenterEntry") << countNamed("taskCenterDetailRow");
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(taskDialog.isNull());
        QTRY_VERIFY(tasks.isNull());
        QCOMPARE(warnings.size(), 0);
        controller.shutdown();
        routes.shutdown();
        windows.shutdown();
        search.shutdown();
        api.shutdown();
        window->close();
    }
    void boundedCacheAndHistoryUnderRepeatedNavigation()
    {
        RouteController routes;
        routes.registerPage(definition("playlist"));
        routes.setCachePolicy(true, 30);
        QElapsedTimer elapsed;
        elapsed.start();
        for (int i = 0; i < 1000; ++i) {
            QVERIFY(routes.navigate("playlist", {}, QString::number(i)));
            routes.refresh();
            drain();
            QVERIFY(routes.mountedCount() <= 30);
            QVERIFY(routes.findChildren<RouteContext *>(Qt::FindDirectChildrenOnly).size() <= 30);
        }
        int backSteps = 0;
        while (routes.canGoBack()) {
            routes.back();
            ++backSteps;
        }
        QCOMPARE(backSteps, 127);
        qInfo() << "1000 navigate+refresh cycles, cache <= 30, history <= 128:" << elapsed.elapsed()
                << "ms (controller only; not a rendering benchmark)";
        routes.shutdown();
        drain();
    }
    void sidebarVisibilityKeepsEditorStable()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore preferences(directory.filePath("window.ini"));
        ThemeStore theme(preferences);
        ShellSettings shell(preferences);
        WindowController windows(preferences, false);
        ApiClient api(DeviceIdentity::temporary());
        Test::AccountUiFixture accountUi(api);
        UserPlaylistModelTestAccess::pinned(accountUi.playlists);
        SearchModel search(api);
        RouteController routes;
        SearchRouteCoordinator searchRoutes(search, routes, false);
        routes.navigate("search");
        ShellController commands(preferences, routes);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/App/Main.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"libraryController", QVariant::fromValue(&accountUi.library)},
             {"accountController", QVariant::fromValue(&accountUi.controller)},
             {"shellController", QVariant::fromValue(&commands)},
             {"searchService", QVariant::fromValue(&search)},
             {"router", QVariant::fromValue(&routes)},
             {"shellSettings", QVariant::fromValue(&shell)},
             {"windowService", QVariant::fromValue(&windows)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        windows.attach(window);
        window->resize(1250, 900);
        QVERIFY(Test::prepareNativeInput(window));
        const auto item = [&](const QString &name) {
            return findVisibleVisual(window->contentItem(), name);
        };
        const auto click = [&](QQuickItem *target) {
            const auto point = target->mapToScene(target->boundingRect().center()).toPoint();
            Test::moveNativePointer(window, point);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        };
        const auto nativePopups = [&](bool includeHints = false) {
            QSet<QObject *> visited;
            QList<QObject *> found;
            const auto visit = [&](auto &&self, QObject *object) -> void {
                if (!object || visited.contains(object))
                    return;
                visited.insert(object);
                if (object->inherits("QQuickPopup") &&
                    (includeHints || !object->inherits("QQuickToolTip")))
                    found.append(object);
                for (auto *child : object->children())
                    self(self, child);
                if (auto *visual = qobject_cast<QQuickItem *>(object))
                    for (auto *child : visual->childItems())
                        self(self, child);
            };
            visit(visit, root.data());
            return found;
        };
        QSignalSpy initialFrame(window, &QQuickWindow::frameSwapped);
        window->update();
        QTRY_VERIFY(!initialFrame.isEmpty());
        QCOMPARE(nativePopups().size(), 0);
        int replaced = 0;
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark})
            for (const bool zoom : {false, true}) {
                theme.setMode(mode);
                shell.resetZoom();
                if (zoom)
                    for (int i = 0; i < 5; ++i)
                        shell.zoomIn();
                QTest::qWait(100);
                auto *trigger = item("sidebarLayoutTrigger");
                QVERIFY(trigger);
                click(trigger);
                QTRY_COMPARE(nativePopups().size(), 1);
                QPointer<QObject> popup = nativePopups().first();
                QTRY_VERIFY(popup && popup->property("opened").toBool());
                const auto rect = [&] {
                    return QRectF(popup->property("x").toReal(), popup->property("y").toReal(),
                                  popup->property("width").toReal(),
                                  popup->property("height").toReal());
                };
                const auto start = rect();
                qreal minHeight = start.height(), maxHeight = start.height();
                int extraPopups = 0, samples = 0;
                const auto watch = connect(window, &QQuickWindow::afterAnimating, this, [&] {
                    if (!popup || !popup->property("opened").toBool())
                        return;
                    ++samples;
                    minHeight = qMin(minHeight, rect().height());
                    maxHeight = qMax(maxHeight, rect().height());
                    if (nativePopups().size() != 1)
                        ++extraPopups;
                });
                for (const auto *name :
                     {"sidebarVisibility-favorites", "sidebarVisibility-cloud",
                      "sidebarVisibility-playlist:created:default",
                      "sidebarVisibility-playlist:created:liked",
                      "sidebarSectionVisibility-library", "sidebarSectionVisibility-playlists"}) {
                    for (int i = 0; i < 20; ++i) {
                        QPointer<QQuickItem> button = item(QString::fromLatin1(name));
                        QVERIFY(button);
                        const auto before = button->property("glyph").toString();
                        click(button);
                        QTest::qWait(i % 2 ? 8 : 20);
                        replaced += button.isNull();
                        QVERIFY(popup && popup->property("opened").toBool());
                        QVERIFY(item(QString::fromLatin1(name)));
                        QCOMPARE(item(QString::fromLatin1(name))->property("glyph").toString(),
                                 before == "eye" ? QString("eye-off") : QString("eye"));
                    }
                }
                // Pure icon controls may show their required hint after a fresh hover.
                // The business editor itself must remain a single stable instance.
                QTest::qWait(600);
                disconnect(watch);
                qInfo() << "sidebar_visibility" << mode << shell.uiScale() << "frames" << samples
                        << "height-range" << minHeight << maxHeight << "extra-popups" << extraPopups
                        << "replaced-buttons" << replaced;
                QCOMPARE(extraPopups, 0);
                QCOMPARE(maxHeight, minHeight);
                QCOMPARE(rect(), start);
                QVERIFY(QMetaObject::invokeMethod(popup, "close"));
                QTRY_VERIFY(!popup);
                QTRY_COMPARE(nativePopups(true).size(), 0);
            }
        QCOMPARE(replaced, 0);
        QCOMPARE(warnings.size(), 0);
        routes.shutdown();
        windows.shutdown();
        search.shutdown();
        api.shutdown();
        window->close();
    }
    void contentFullscreenPreservesHostStateAndHiddenExit()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore preferences(directory.filePath("window.ini"));
        WindowController windows(preferences, false);
        QQuickWindow window;
        windows.attach(&window);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QSignalSpy exitRequested(&windows, &WindowController::contentFullscreenExitRequested);
        connect(&windows, &WindowController::contentFullscreenExitRequested, this,
                [&windows](QObject *owner) { windows.endContentFullscreen(owner); });
        for (bool maximized : {false, true}) {
            if (maximized)
                window.showMaximized();
            else
                window.showNormal();
            QTRY_COMPARE(windows.maximized(), maximized);
            QObject owner;
            windows.beginContentFullscreen(&owner);
            QTRY_VERIFY(windows.fullscreen());
            const auto before = exitRequested.size();
            windows.toggleFullscreen();
            QCOMPARE(exitRequested.size(), before + 1);
            QCOMPARE(exitRequested.last().first().value<QObject *>(), &owner);
            QTRY_VERIFY(!windows.fullscreen());
            QTRY_COMPARE(windows.maximized(), maximized);
            QVERIFY(window.isVisible());

            // Application fullscreen remains when its nested MV layer retires.
            windows.toggleFullscreen();
            QTRY_VERIFY(windows.fullscreen());
            windows.beginContentFullscreen(&owner);
            windows.toggleFullscreen();
            QVERIFY(windows.fullscreen());
            windows.toggleFullscreen();
            QTRY_VERIFY(!windows.fullscreen());
            QTRY_COMPARE(windows.maximized(), maximized);

            windows.beginContentFullscreen(&owner);
            window.hide();
            windows.endContentFullscreen(&owner);
            QVERIFY(!window.isVisible());
            QVERIFY(!windows.fullscreen());
            QCOMPARE(windows.maximized(), maximized);
            windows.showWindow();
            QCOMPARE(windows.maximized(), maximized);

            windows.beginContentFullscreen(&owner);
            windows.toggleVisibility();
            QVERIFY(windows.background());
            QVERIFY(!window.isVisible());
            windows.endContentFullscreen(&owner);
            windows.showWindow();
            QVERIFY(window.isVisible());
            QVERIFY(!windows.fullscreen());
            QCOMPARE(windows.maximized(), maximized);

            windows.beginContentFullscreen(&owner);
            windows.minimize();
            QVERIFY(windows.fullscreen());
            windows.endContentFullscreen(&owner);
            QVERIFY(!windows.fullscreen());
            QVERIFY(window.windowStates().testFlag(Qt::WindowMinimized));
            QCOMPARE(windows.maximized(), maximized);
            windows.showWindow();
            QVERIFY(window.isVisible());
            QVERIFY(!windows.fullscreen());
            QCOMPARE(windows.maximized(), maximized);

            auto *retired = new QObject;
            windows.beginContentFullscreen(retired);
            delete retired;
            QTRY_VERIFY(!windows.fullscreen());
            QCOMPARE(windows.maximized(), maximized);

            // A window-manager exit already completed outside this overlay is
            // authoritative; retiring content must not force maximize again.
            windows.beginContentFullscreen(&owner);
            window.setWindowState(Qt::WindowNoState);
            windows.endContentFullscreen(&owner);
            QVERIFY(!windows.fullscreen());
            QVERIFY(!windows.maximized());
        }
        windows.shutdown();
    }
    void disconnectedAndSmallDisplays()
    {
        const QRect small(0, 0, 800, 600);
        const auto recovered =
            WindowController::restoreBounds(QRect(4000, 4000, 1150, 750), {small}, small);
        QCOMPARE(recovered, QRect(0, 0, 800, 600));
        const QRect primary(0, 0, 1920, 1080), secondary(-1920, 0, 1920, 1080);
        const QRect saved(-1800, 120, 1150, 750);
        QCOMPARE(WindowController::restoreBounds(saved, {primary, secondary}, primary), saved);
        const auto disconnected = WindowController::restoreBounds(QRect(4000, 0, 1150, 750),
                                                                  {primary, secondary}, primary);
        QVERIFY(primary.contains(disconnected));
    }
};
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    ShellTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ShellTest.moc"
