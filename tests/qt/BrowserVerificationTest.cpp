#include "ApiClient.h"
#include "BrowserVerificationSession.h"
#include "VerificationService.h"
#include "BrowserWindowLauncher.h"
#include "WindowController.h"
#include <QDesktopServices>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include "SettingsStore.h"
#include <QSignalSpy>
#include <QTcpSocket>
#include <QtTest>

using namespace EchoMusic;
namespace EchoMusic
{
class ApiClientTestAccess
{
public:
    static void configure(ApiClient &api)
    {
        const auto original = api.search(ApiClient::SearchKind::Song, QString());
        api.deliver(
            original, api.m_revision, 5,
            QJsonDocument(QJsonObject{{"kind", "challenge"},
                                      {"challenge", QJsonObject{{"event_id", QString(4097, 'v')}}}})
                .toJson());
        for (const auto &pending : std::as_const(api.m_pending)) {
            if (pending.request.value("operation") == "get_verify_info") {
                const auto id = pending.logicalId;
                api.deliver(id, api.m_revision, 0,
                            QJsonDocument(QJsonObject{{"provider", "tencent"},
                                                      {"verification_type", 23},
                                                      {"app_id", "197787253"}})
                                .toJson());
                return;
            }
        }
    }
    static bool replayInFlight(const ApiClient &api, quint64 logicalId)
    {
        for (const auto &pending : api.m_pending)
            if (pending.logicalId == logicalId && pending.retried)
                return true;
        return false;
    }
};
} // namespace EchoMusic
class BrowserVerificationTest final : public QObject
{
    Q_OBJECT
    QByteArray request(const QUrl &url, const QByteArray &method, const QByteArray &path,
                       const QByteArray &body = {}, QByteArray origin = {}, QByteArray nonce = {},
                       QByteArray extra = {})
    {
        const auto authority = url.authority().toLatin1();
        if (origin.isEmpty())
            origin = "http://" + authority;
        if (nonce.isEmpty())
            nonce = url.fragment().toLatin1();
        return method + ' ' + path + " HTTP/1.1\r\nHost: " + authority + "\r\nOrigin: " + origin +
               "\r\nX-Echo-Verification: " + nonce +
               "\r\nContent-Type: application/json\r\nContent-Length: " +
               QByteArray::number(body.size()) + "\r\n" + extra + "\r\n" + body;
    }
    QByteArray exchange(const QUrl &url, const QByteArray &bytes)
    {
        QTcpSocket peer;
        peer.setProxy(QNetworkProxy::NoProxy);
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        deadline.start(2500);
        QByteArray response;
        connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(&peer, &QTcpSocket::connected, &peer, [&] { peer.write(bytes); });
        connect(&peer, &QTcpSocket::readyRead, &peer, [&] { response += peer.readAll(); });
        connect(&peer, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
        connect(&peer, &QTcpSocket::errorOccurred, &loop, &QEventLoop::quit);
        peer.connectToHost(url.host(), url.port());
        loop.exec();
        response += peer.readAll();
        return response;
    }
private slots:
    void appWindowArgumentsOnlyTargetRecognizedBrowsersAndLoopbackVerification()
    {
        const QUrl url("http://127.0.0.1:12345/verification#generated-control-only");
        const auto arguments = BrowserWindowLauncher::windowArguments(
            "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", url);
        QCOMPARE(arguments, QStringList({"--app=" + url.toString(QUrl::FullyEncoded),
                                         "--window-size=440,680"}));
        QVERIFY(BrowserWindowLauncher::windowArguments("Safari", url).isEmpty());
        QVERIFY(BrowserWindowLauncher::windowArguments("firefox", url).isEmpty());
        QVERIFY(BrowserWindowLauncher::windowArguments("chrome.exe",
                                                       QUrl("https://example.com/verification"))
                    .isEmpty());
        QVERIFY(BrowserWindowLauncher::windowArguments("chrome.exe",
                                                       QUrl("http://127.0.0.1:12345/other"))
                    .isEmpty());
    }
    void inactiveAndPageHasNoAccountData()
    {
        ApiClient api;
        VerificationService service(api);
        BrowserVerificationSession session(service);
        QVERIFY(session.start().isEmpty());
        ApiClientTestAccess::configure(api);
        const auto url = session.start();
        QVERIFY(!url.isEmpty());
        QCOMPARE(url.host(), QString("127.0.0.1"));
        QVERIFY(url.fragment().size() >= 43);
        const auto page = exchange(url, request(url, "GET", "/verification"));
        QVERIFY(page.startsWith("HTTP/1.1 200"));
        QVERIFY(page.contains("https://turing.captcha.qcloud.com/TJCaptcha.js"));
        QVERIFY(!page.contains("ECHO_CAPTCHA_APP_ID"));
        QVERIFY(page.contains("const appId = '197787253';"));
        QVERIFY(!page.contains("fixture-secret"));
    }
    void originHeadersInputAndProviderFailureDoNotSubmit()
    {
        ApiClient api;
        VerificationService service(api);
        ApiClientTestAccess::configure(api);
        BrowserVerificationSession session(service);
        const auto url = session.start();
        QSignalSpy proof(&session, &BrowserVerificationSession::proofSubmitted);
        QSignalSpy failure(&session, &BrowserVerificationSession::sdkFailed);
        const auto ready = QByteArray("{\"event\":\"ready\"}");
        QVERIFY(exchange(url, request(url, "POST", "/result", ready, "http://other.invalid"))
                    .startsWith("HTTP/1.1 403"));
        QVERIFY(exchange(url, request(url, "POST", "/result", ready, {}, "wrong"))
                    .startsWith("HTTP/1.1 403"));
        QVERIFY(exchange(url, request(url, "POST", "/result", ready, {}, {},
                                      "Host: attacker.invalid\r\n"))
                    .startsWith("HTTP/1.1 400"));
        QVERIFY(exchange(url, request(url, "POST", "/result", ready, {}, {},
                                      "Transfer-Encoding: chunked\r\n"))
                    .startsWith("HTTP/1.1 403"));
        QVERIFY(
            exchange(url, request(url, "POST", "/result", "not-json")).startsWith("HTTP/1.1 400"));
        QVERIFY(exchange(url, request(url, "POST", "/result", QByteArray(32769, 'x')))
                    .startsWith("HTTP/1.1 413"));
        // Generated control input: deliberately NOT a real CAPTCHA proof/sample.
        const auto disaster = QByteArray(
            "{\"ret\":0,\"ticket\":\"trerror_1007_test\",\"randstr\":\"test\",\"errorCode\":1007}");
        QVERIFY(
            exchange(url, request(url, "POST", "/result", disaster)).contains("\"sdkError\":true"));
        QCOMPARE(failure.size(), 1);
        QCOMPARE(failure[0][0].toInt(), 1007);
        QCOMPARE(proof.size(), 0);
        QVERIFY(session.active());
        QVERIFY(service.open());
    }
    void sdkReadyCancellationAndGenerationRevokeTransport()
    {
        ApiClient api;
        VerificationService service(api);
        ApiClientTestAccess::configure(api);
        BrowserVerificationSession session(service);
        auto url = session.start();
        QSignalSpy ready(&session, &BrowserVerificationSession::sdkReady);
        QVERIFY(exchange(url, request(url, "POST", "/result", "{\"event\":\"ready\"}"))
                    .contains("true"));
        QCOMPARE(ready.size(), 1);
        const auto previousNonce = url.fragment();
        url = session.start();
        QVERIFY(previousNonce != url.fragment());
        QVERIFY(exchange(url, request(url, "POST", "/result", "{\"event\":\"ready\"}", {},
                                      previousNonce.toLatin1()))
                    .startsWith("HTTP/1.1 403"));
        service.refresh();
        QVERIFY(!session.active());
        ApiClientTestAccess::configure(api);
        url = session.start();
        QVERIFY(!url.isEmpty());
        QVERIFY(
            exchange(url, request(url, "POST", "/result", "{\"ret\":2}")).contains("cancelled"));
        QVERIFY(!service.open());
        QVERIFY(!session.active());
    }
    void accountSwitchAndNativeProofSubmissionRevokeTransport()
    {
        ApiClient api;
        VerificationService service(api);
        ApiClientTestAccess::configure(api);
        BrowserVerificationSession session(service);
        auto url = session.start();
        QVERIFY(api.setAccount(1, "fixture-secret", {}));
        QVERIFY(!session.active());
        ApiClientTestAccess::configure(api);
        url = session.start();
        QSignalSpy proof(&session, &BrowserVerificationSession::proofSubmitted);
        // Native guard rejects the deliberately oversized test event before HTTP.
        const auto generated = QByteArray(
            "{\"ret\":0,\"ticket\":\"generated-transport-only\",\"randstr\":\"generated\"}");
        QVERIFY(
            exchange(url, request(url, "POST", "/result", generated)).startsWith("HTTP/1.1 202"));
        QCOMPARE(proof.size(), 1);
        QVERIFY(!session.active());
        QVERIFY(exchange(url, request(url, "POST", "/result", generated)).isEmpty());
        QCOMPARE(proof.size(), 1);
    }
    void currentBrowserProofRequestsReturnOnceButSdkFailureDoesNot()
    {
        ApiClient api;
        QUrl url;
        VerificationService service(api, nullptr, [&](const QUrl &value) {
            url = value;
            return true;
        });
        QSignalSpy returned(&service, &VerificationService::returnToApplicationRequested);
        ApiClientTestAccess::configure(api);
        service.beginBrowserVerification();
        QVERIFY(service.browserOpen());
        const QByteArray error("{\"ret\":0,\"errorCode\":1007}");
        QVERIFY(exchange(url, request(url, "POST", "/result", error)).contains("sdkError"));
        QCOMPARE(returned.size(), 0);
        // Generated transport control input, not a provider proof/sample.
        const QByteArray proof(
            "{\"ret\":0,\"ticket\":\"generated-transport-only\",\"randstr\":\"generated\"}");
        QVERIFY(exchange(url, request(url, "POST", "/result", proof)).startsWith("HTTP/1.1 202"));
        QCOMPARE(returned.size(), 1);
        QVERIFY(!service.browserOpen());
        QVERIFY(exchange(url, request(url, "POST", "/result", proof)).isEmpty());
        QCOMPARE(returned.size(), 1);
    }
    void liveRealKugouBrowser()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_BROWSER_VERIFICATION"))
            QSKIP("Opt-in human verification with a real SSA event and native replay");
        ApiClient api;
        VerificationService service(api);
        auto *session = service.findChild<BrowserVerificationSession *>();
        QVERIFY(session);
        QSignalSpy proof(session, &BrowserVerificationSession::proofSubmitted);
        QSignalSpy sdkFailure(session, &BrowserVerificationSession::sdkFailed);
        QTemporaryDir windowDirectory;
        EchoMusic::SettingsStore settings(windowDirectory.filePath("settings.ini"));
        WindowController window(settings, false);
        QQuickWindow view;
        view.setTitle("EchoMusicNext · 验证返回检查");
        view.resize(800, 560);
        view.show();
        window.attach(&view);
        bool activated = false;
        bool returnRequested = false;
        connect(&service, &VerificationService::returnToApplicationRequested, &window, [&] {
            returnRequested = true;
            window.showWindow();
            qInfo("LIVE native_return_requested=true");
        });
        connect(&view, &QWindow::activeChanged, this, [&] {
            if (returnRequested && view.isActive()) {
                activated = true;
                qInfo("LIVE native_window_active_after_proof=true");
            }
        });
        quint64 original = 0;
        bool replay = false, terminal = false;
        QString terminalKind;
        connect(&api, &ApiClient::verificationDismissed, this, [&] {
            replay |= ApiClientTestAccess::replayInFlight(api, original);
            if (replay)
                qInfo("LIVE original_replay_in_flight=true");
        });
        connect(&api, &ApiClient::succeeded, this, [&](quint64 id, const QByteArray &) {
            if (id == original) {
                terminal = true;
                terminalKind = "success";
            }
        });
        connect(&api, &ApiClient::failed, this, [&](quint64 id, int, const QByteArray &payload) {
            const auto error = QJsonDocument::fromJson(payload).object();
            if (id == original) {
                terminal = true;
                terminalKind = error.value("kind").toString();
                qInfo().noquote() << "LIVE original_terminal kind=" << terminalKind
                                  << "code=" << error.value("code").toInt(-1);
            } else if (service.state() == VerificationService::State::Error) {
                qInfo().noquote() << "LIVE verification_error kind="
                                  << error.value("kind").toString()
                                  << "code=" << error.value("code").toInt(-1);
            }
        });
        connect(session, &BrowserVerificationSession::sdkReady, this,
                [] { qInfo("LIVE official_challenge_ready=true"); });
        connect(session, &BrowserVerificationSession::sdkFailed, this,
                [](int code) { qInfo("LIVE sdk_error_code=%d", code); });
        original = api.passwordLogin("EchoMusicNext-invalid-probe", "invalid-probe-password");
        QVERIFY(original);
        QTRY_VERIFY_WITH_TIMEOUT(service.open() || terminal, 25000);
        QVERIFY(service.open());
        QTRY_COMPARE_WITH_TIMEOUT(service.state(), VerificationService::State::Ready, 25000);
        service.beginBrowserVerification();
        QVERIFY(service.browserOpen());
        qInfo("LIVE browser_open_requested=true; complete the official challenge manually");
        QTRY_VERIFY_WITH_TIMEOUT(terminal || !sdkFailure.isEmpty() ||
                                     service.state() == VerificationService::State::Error,
                                 180000);
        QVERIFY2(sdkFailure.isEmpty(),
                 "Official SDK failed; no provider proof or replay acceptance");
        QVERIFY2(terminal, "Verification submission failed before original request resumed");
        QCOMPARE(proof.size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(activated, 5000);
        QVERIFY(replay);
        QVERIFY(terminalKind != "verification_retry_rejected");
        QVERIFY(terminalKind == "business" || terminalKind == "authentication" ||
                terminalKind == "success");
        qInfo("LIVE valid_proof_native_submission_and_original_replay=true");
    }
};
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("EchoMusicNextTests");
    QCoreApplication::setApplicationName("BrowserVerification");
    BrowserVerificationTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "BrowserVerificationTest.moc"
