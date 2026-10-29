#include "ApiClient.h"
#include "VerificationService.h"
#include "UiTypography.h"
#include <QGuiApplication>
#include <QDesktopServices>
#include <QScopeGuard>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickView>
#include <QQuickItem>
#include <QPointer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include "SettingsStore.h"
#include "ThemeStore.h"
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
// Canonical control-flow seam only. These values are not captured wire fixtures.
class ApiClientTestAccess
{
public:
    static void challenge(ApiClient &api, quint64 id)
    {
        // The native 4096-byte input guard stops get_verify_info before HTTP.
        const auto payload =
            QJsonDocument(
                QJsonObject{{"kind", "challenge"},
                            {"challenge", QJsonObject{{"event_id", QString(4097, QChar(u'v'))}}}})
                .toJson();
        api.deliver(id, api.m_revision, 5, payload);
    }
    static void configure(ApiClient &api, const QString &provider, int type)
    {
        for (auto it = api.m_pending.cbegin(); it != api.m_pending.cend(); ++it) {
            if (it->request.value("operation") == QStringLiteral("get_verify_info")) {
                const auto id = it.key();
                api.deliver(id, api.m_revision, 0,
                            QJsonDocument(QJsonObject{{"provider", provider},
                                                      {"verification_type", type},
                                                      {"vendor", type == 24 ? "GT" : "UNKNOWN"},
                                                      {"app_id", type == 23 ? "197787253" : ""}})
                                .toJson());
                return;
            }
        }
    }
};
} // namespace EchoMusic
class BrowserUrlSink final : public QObject
{
    Q_OBJECT
public:
    QUrl url;
public slots:
    void open(const QUrl &value)
    {
        url = value;
    }
};
class VerificationUiTest final : public QObject
{
    Q_OBJECT
private slots:
    void reentrantCancellationCannotOpenAStaleBrowser()
    {
        ApiClient api;
        VerificationService service(api, nullptr, QDesktopServices::openUrl);
        ApiClientTestAccess::challenge(api, api.search(ApiClient::SearchKind::Song, QString()));
        ApiClientTestAccess::configure(api, "tencent", 23);
        BrowserUrlSink sink;
        QDesktopServices::setUrlHandler("http", &sink, "open");
        const auto restore = qScopeGuard([] { QDesktopServices::unsetUrlHandler("http"); });
        connect(&service, &VerificationService::changed, &service, [&] {
            if (service.browserOpen())
                service.cancel();
        });
        service.beginBrowserVerification();
        QVERIFY(sink.url.isEmpty());
        QVERIFY(!service.open());
        QVERIFY(!service.browserOpen());
        QCOMPARE(service.state(), VerificationService::State::Idle);
        QVERIFY(service.errorMessage().isEmpty());
    }
    void sourceDialogLoadsSmsAndOfficialClientActions()
    {
        ApiClient api;
        VerificationService service(api, nullptr, QDesktopServices::openUrl);
        QTemporaryDir directory;
        EchoMusic::SettingsStore preferences(directory.filePath("settings.ini"));
        ThemeStore theme(preferences);
        QQuickView view;
        theme.attachToEngine(*view.engine());
        QSignalSpy warnings(view.engine(), &QQmlEngine::warnings);
        view.setInitialProperties({{"service", QVariant::fromValue(&service)}});
        view.setSource(QUrl("qrc:/qt/qml/EchoMusic/VerificationTests/VerificationHarness.qml"));
        QCOMPARE(view.status(), QQuickView::Ready);
        view.show();
        for (const auto type : {24, 32, 34, 36, 38, 51}) {
            ApiClientTestAccess::challenge(api, api.search(ApiClient::SearchKind::Song, QString()));
            const auto provider = type == 32   ? "sms"
                                  : type == 34 ? "real_name"
                                  : type == 36 ? "bind_phone"
                                  : type == 38 ? "login"
                                  : type == 51 ? "account_risk"
                                               : "unsupported";
            ApiClientTestAccess::configure(api, QString::fromLatin1(provider), type);
            QVERIFY(service.open());
            QTest::qWait(200);
            if (type == 24) {
                auto *cancel = view.rootObject()->findChild<QQuickItem *>(
                    QStringLiteral("verificationCancelButton"));
                auto *error = view.rootObject()->findChild<QQuickItem *>(
                    QStringLiteral("verificationErrorLabel"));
                QVERIFY(cancel);
                QVERIFY(error);
                QVERIFY(error->isVisible());
                QVERIFY(error->mapToScene(QPointF(0, 0)).y() >=
                        cancel->mapToScene(QPointF(0, cancel->height())).y() + 15);
                QCOMPARE(cancel->width(), error->width());
            }
            QTest::keyClick(&view, Qt::Key_Escape);
            QVERIFY(service.open());
            service.cancel();
            QVERIFY(!service.open());
        }
        QCOMPARE(warnings.size(), 0);
    }
    void browserActionUsesNativeServiceAndPreservesFormHeight()
    {
        ApiClient api;
        VerificationService service(api, nullptr, QDesktopServices::openUrl);
        QTemporaryDir directory;
        EchoMusic::SettingsStore preferences(directory.filePath("settings.ini"));
        ThemeStore theme(preferences);
        QQuickView view;
        theme.attachToEngine(*view.engine());
        QSignalSpy warnings(view.engine(), &QQmlEngine::warnings);
        view.setInitialProperties({{"service", QVariant::fromValue(&service)}});
        view.setSource(QUrl("qrc:/qt/qml/EchoMusic/VerificationTests/VerificationHarness.qml"));
        QCOMPARE(view.status(), QQuickView::Ready);
        view.show();
        ApiClientTestAccess::challenge(api, api.search(ApiClient::SearchKind::Song, QString()));
        ApiClientTestAccess::configure(api, QStringLiteral("tencent"), 23);
        auto *overlay = view.rootObject()->findChild<QQuickItem *>("verificationOverlay");
        QVERIFY(overlay);
        auto *start = overlay->findChild<QQuickItem *>("verificationStartButton");
        QVERIFY(start);
        for (const qreal scale : {1.0, 1.25, 1.5}) {
            overlay->setProperty("uiScale", scale);
            QTRY_COMPARE(start->height(), 56.0);
        }
        BrowserUrlSink sink;
        QDesktopServices::setUrlHandler("http", &sink, "open");
        const auto restore = qScopeGuard([] { QDesktopServices::unsetUrlHandler("http"); });
        overlay->setProperty("uiScale", 1.0);
        QTest::qWait(30);
        const auto point =
            start->mapToScene(QPointF(start->width() / 2, start->height() / 2)).toPoint();
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, point);
        QTRY_VERIFY(service.browserOpen());
        QVERIFY(sink.url.host() == "127.0.0.1");
        QVERIFY(!sink.url.fragment().isEmpty());
        QCOMPARE(sink.url.path(), QString("/verification"));
        QTRY_COMPARE(start->property("text").toString(), QString("重新打开验证窗口"));
        QVERIFY(service.open());
        QTest::keyClick(&view, Qt::Key_Escape);
        QVERIFY(service.browserOpen());
        service.cancel();
        QVERIFY(!service.open());
        QVERIFY(!service.browserOpen());
        QCOMPARE(warnings.size(), 0);
    }
};
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("EchoMusicNextTests"));
    QCoreApplication::setApplicationName(QStringLiteral("VerificationUi"));
    QStandardPaths::setTestModeEnabled(true);
    configureUiTypography();
    VerificationUiTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "VerificationUiTest.moc"
