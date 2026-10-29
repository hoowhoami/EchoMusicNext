#include "LogService.h"
#include "ApiClient.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QSignalSpy>
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDateTime>
#include <QProcess>
#include <QRegularExpression>
#include <QLockFile>
using namespace EchoMusic;
namespace
{
const QRegularExpression &linePattern()
{
    static const QRegularExpression pattern(
        R"(^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) (?:INFO |WARN |ERROR|DEBUG|FATAL) \[[^\]\r\n]+\] .*$)");
    return pattern;
}
} // namespace
class LoggingTest final : public QObject
{
    Q_OBJECT
private slots:
    void consoleAndFileUseIdenticalLocalSingleLineRecords()
    {
        QTemporaryDir directory;
        const auto before = QDateTime::currentDateTime();
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--format-probe", directory.path()});
        QVERIFY(child.waitForFinished(10000));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
        const auto files = QDir(directory.path()).entryList({"*.log"}, QDir::Files);
        QCOMPARE(files.size(), 1);
        QVERIFY(QRegularExpression(
                    R"(^echomusicnext-\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}-\d{3}-\d+\.log$)")
                    .match(files.first())
                    .hasMatch());
        QFile file(directory.filePath(files.first()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        QCOMPARE(child.readAllStandardError(), bytes);
        QVERIFY(bytes.contains("format-probe 中文\\nnext\\r\\t\\u001b[31m"));
        QVERIFY(!bytes.contains("do-not-store"));
        QVERIFY(bytes.contains("[redacted sensitive log]"));
        const auto pattern = linePattern();
        for (const auto &line : bytes.split('\n')) {
            if (line.isEmpty())
                continue;
            const auto match = pattern.match(QString::fromUtf8(line));
            QVERIFY2(match.hasMatch(), line.constData());
            const auto time = QDateTime::fromString(match.captured(1), "yyyy-MM-dd HH:mm:ss.zzz");
            QVERIFY(time.isValid());
            QVERIFY(time >= before && time <= QDateTime::currentDateTime());
        }
        const auto sample = qEnvironmentVariable("ECHO_LOG_SAMPLE");
        if (!sample.isEmpty()) {
            QFile output(sample);
            QVERIFY(output.open(QIODevice::WriteOnly));
            QCOMPARE(output.write(bytes), bytes.size());
        }
    }
    void startupRetentionKeepsLiveAndUnrelatedFiles_data()
    {
        QTest::addColumn<int>("days");
        QTest::newRow("default-seven-days") << 7;
        QTest::newRow("custom-three-days") << 3;
        QTest::newRow("invalid-uses-seven-days") << 0;
    }
    void startupRetentionKeepsLiveAndUnrelatedFiles()
    {
        QFETCH(int, days);
        QTemporaryDir directory;
        const auto make = [&](const QString &name, int age) {
            const auto path = directory.filePath(name);
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write("retention fixture") < 0)
                return false;
            return file.flush() && file.setFileTime(QDateTime::currentDateTime().addDays(-age),
                                                    QFileDevice::FileModificationTime);
        };
        const QString expired = "echomusicnext-2026-01-01_01-02-03-004-99111.log";
        const QString rotated = "echomusicnext-2026-01-01_01-02-03-004-99111.2.log";
        const QString legacy = "echomusicnext-99112.log";
        const QString recent = "echomusicnext-99113.log";
        const QString middle = "echomusicnext-99115.log";
        const QString active = "echomusicnext-99114.log";
        const QString activeSegment = "echomusicnext-99114.1.log";
        for (const auto &name :
             {expired, rotated, legacy, active, activeSegment, QString("notes.log"),
              QString("echomusicnext-notes.log"), QString("crashes/keep.log")})
            QVERIFY(make(name, 8));
        QVERIFY(make(recent, 1));
        QVERIFY(make(middle, 4));
        QLockFile activeLease(directory.filePath(active + ".lock"));
        activeLease.setStaleLockTime(0);
        QVERIFY(activeLease.tryLock(0));
        // Older running releases have only their CrashDiagnostics session lease.
        const auto prior = QString("echomusicnext-%1.log").arg(QCoreApplication::applicationPid());
        QVERIFY(make(prior, 8));
        QLockFile priorLease(directory.filePath("crashes/prior.running"));
        QVERIFY(priorLease.tryLock(0));
        EchoMusic::SettingsStore preferences(directory.filePath("settings.ini"));
        if (days != 7)
            preferences.setValue("logging/retentionDays", days);
        {
            LogService logging(directory.path(), 2, false, &preferences);
        } // Destruction joins the worker, including startup cleanup.
        for (const auto &name : {expired, rotated, legacy})
            QVERIFY(!QFile::exists(directory.filePath(name)));
        for (const auto &name : {recent, active, activeSegment, prior, QString("notes.log"),
                                 QString("echomusicnext-notes.log"), QString("crashes/keep.log")})
            QVERIFY(QFile::exists(directory.filePath(name)));
        QCOMPARE(QFile::exists(directory.filePath(middle)), days != 3);
        QVERIFY(QFile::exists(directory.filePath("crashes/prior.running")));
    }
    void successfulApiRequestsAreQuietAtInfo()
    {
        QTemporaryDir directory;
        {
            LogService logging(directory.path(), 2, false);
            ApiClient api(DeviceIdentity::temporary());
            QSignalSpy success(&api, &ApiClient::succeeded);
            QVERIFY(api.capabilities());
            QTRY_COMPARE(success.size(), 1);
            qInfo("important-lifecycle-message");
        }
        const auto files = QDir(directory.path()).entryList({"*.log"}, QDir::Files);
        QCOMPARE(files.size(), 1);
        QFile file(directory.filePath(files.first()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        QVERIFY(bytes.contains("important-lifecycle-message"));
        QVERIFY(!bytes.contains("echo.api.capabilities"));
        QVERIFY(!bytes.contains("native.api"));
    }
    void preferenceFailureRetryAndExplicitLaunchOverride()
    {
        QTemporaryDir directory;
        const auto blocked = directory.filePath("blocked");
        QFile obstruction(blocked);
        QVERIFY(obstruction.open(QIODevice::WriteOnly));
        obstruction.close();
        EchoMusic::SettingsStore invalid(blocked + "/settings.ini");
        {
            LogService logging(directory.filePath("logs"), 2, false, &invalid);
            QSignalSpy notices(&logging, &LogService::notice);
            logging.setLevel("warn");
            QCOMPARE(logging.level(), QString("info"));
            QCOMPARE(notices.size(), 1);
            QVERIFY(QFile::remove(blocked));
            QVERIFY(QDir().mkpath(blocked));
            logging.setLevel("warn");
            QCOMPARE(logging.level(), QString("warn"));
        }
        EchoMusic::SettingsStore saved(blocked + "/settings.ini");
        {
            LogService explicitLevel(directory.filePath("override"), 3, false, &saved, true);
            QCOMPARE(explicitLevel.level(), QString("debug"));
        }
        QCOMPARE(saved.value("logging/level").toString(), QString("warn"));
        {
            LogService savedLevel(directory.filePath("restored"), 2, false, &saved);
            QCOMPARE(savedLevel.level(), QString("warn"));
        }
    }
    void liveLevelDiagnosticsAndRedactedApiResponses()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore preferences(directory.filePath("settings.ini"));
        preferences.setValue("logging/level", "warn");
        QTcpServer proxy;
        QVERIFY(proxy.listen(QHostAddress::LocalHost));
        connect(&proxy, &QTcpServer::newConnection, &proxy, [&] {
            auto *peer = proxy.nextPendingConnection();
            connect(peer, &QTcpSocket::readyRead, peer, [peer] {
                const auto bytes = peer->property("header").toByteArray() + peer->readAll();
                peer->setProperty("header", bytes);
                if (!bytes.contains("\r\n\r\n") || peer->property("answered").toBool())
                    return;
                peer->setProperty("answered", true);
                // Transport diagnostic fixture only, deliberately rejected by
                // HTTP status before any music-service response converter.
                const auto body =
                    QJsonDocument(
                        QJsonObject{{"message", "useful response detail"},
                                    {"token", "response-secret"},
                                    {"nested",
                                     QJsonObject{{"t1", "refresh-secret"},
                                                 {"url", "https://signed.invalid/a?key=secret"}}}})
                        .toJson();
                peer->write("HTTP/1.1 503 Unavailable\r\nContent-Type: "
                            "application/json\r\nContent-Length: " +
                            QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" +
                            body);
                peer->disconnectFromHost();
            });
            connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
        });
        {
            LogService logging(directory.path(), 2, false, &preferences);
            QVERIFY(logging.active());
            QCOMPARE(logging.level(), QString("warn"));
            qInfo("hidden-before-live-change");
            {
                ApiClient api(DeviceIdentity::temporary());
                QVERIFY(api.setNetworkTransport(proxy.serverPort()));
                QSignalSpy failed(&api, &ApiClient::failed);
                QVERIFY(api.audioEffects(ApiClient::EffectCategory::Artist));
                QTRY_COMPARE(failed.size(), 1);
            }
            logging.setLevel("debug");
            qDebug("visible-after-live-change");
            logging.setLevel("invalid-level");
            QCOMPARE(logging.level(), QString("debug"));
            logging.setApiResponseBody(true);
            logging.enableTemporaryDiagnostics();
            QVERIFY(logging.diagnosticActive());
            QCOMPARE(logging.diagnosticMinutes(), 10);
            logging.setLevel("error");
            // Temporary verbosity must not overwrite the user's base setting.
            QCOMPARE(logging.level(), QString("error"));
            qDebug("visible-during-diagnostics");
            ApiClient api(DeviceIdentity::temporary());
            QVERIFY(api.setNetworkTransport(proxy.serverPort()));
            QSignalSpy failed(&api, &ApiClient::failed);
            QVERIFY(api.audioEffects(ApiClient::EffectCategory::Artist));
            QTRY_COMPARE(failed.size(), 1);
            QTest::qWait(100);
            logging.disableTemporaryDiagnostics();
            QVERIFY(!logging.diagnosticActive());
            qDebug("hidden-after-diagnostics");
        }
        QByteArray bytes;
        for (const auto &name : QDir(directory.path()).entryList({"*.log"}, QDir::Files)) {
            QFile file(directory.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            bytes += file.readAll();
        }
        QVERIFY(bytes.contains("visible-after-live-change"));
        QVERIFY(bytes.contains("visible-during-diagnostics"));
        QVERIFY(bytes.contains("useful response detail"));
        QVERIFY(bytes.contains("native.api.response"));
        QVERIFY(bytes.contains("WARN  [echo.api."));
        QVERIFY(bytes.contains("Request failed"));
        QVERIFY(bytes.contains("status=api_failure"));
        QVERIFY(bytes.count("phase=response_status") >= 2); // Includes default warn level.
        QVERIFY(bytes.count("http_status=503") >= 2);
        QVERIFY(bytes.contains("received_bytes="));
        QVERIFY(bytes.contains("attempt_elapsed_ms="));
        QVERIFY(bytes.contains("elapsed_ms="));
        for (const auto &secret : {"response-secret", "refresh-secret", "signed.invalid",
                                   "hidden-before-live-change", "hidden-after-diagnostics"})
            QVERIFY(!bytes.contains(secret));
        {
            LogService restored(directory.filePath("restored"), 2, false, &preferences);
            QCOMPARE(restored.level(), QString("error"));
            QVERIFY(restored.apiResponseBody());
            QVERIFY(!restored.diagnosticActive());
        }
        preferences.setValue("logging/diagnosticUntil", QDateTime::currentMSecsSinceEpoch() - 1000);
        {
            LogService expired(directory.filePath("expired"), 2, false, &preferences);
            QVERIFY(!expired.diagnosticActive());
            QCOMPARE(expired.level(), QString("error"));
        }
        preferences.setValue("logging/diagnosticUntil", QDateTime::currentMSecsSinceEpoch() + 500);
        {
            LogService timed(directory.filePath("timed"), 2, false, &preferences);
            QVERIFY(timed.diagnosticActive());
            qDebug("visible-before-expiry");
            QTRY_VERIFY_WITH_TIMEOUT(!timed.diagnosticActive(), 3000);
            qDebug("hidden-after-expiry");
            QCOMPARE(timed.level(), QString("error"));
        }
        for (const auto &name :
             QDir(directory.filePath("timed")).entryList({"*.log"}, QDir::Files)) {
            QFile file(directory.filePath("timed/") + name);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const auto logged = file.readAll();
            QVERIFY(logged.contains("visible-before-expiry"));
            QVERIFY(!logged.contains("hidden-after-expiry"));
        }
    }
    void rustQtAndQmlReachTheSameQtLogFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        {
            LogService logging(directory.path(), 3, false);
            QVERIFY(logging.active());
            qInfo("qt-visible-message");
            qWarning("password=must-not-persist");
            QQmlEngine engine;
            QQmlComponent component(&engine);
            component.setData(
                "import QtQml\nQtObject { Component.onCompleted: { "
                "console.info('qml-visible-message'); console.debug('qml-debug-message'); } }",
                QUrl());
            QScopedPointer<QObject> root(component.create());
            QVERIFY2(root, qPrintable(component.errorString()));
            ApiClient api;
            QSignalSpy success(&api, &ApiClient::succeeded);
            QVERIFY(api.capabilities());
            QTRY_COMPARE(success.size(), 1);
            // Exercise the production Rust -> synchronous drain -> Qt handler
            // path. No synthetic native log or upstream response fixture.
            QTest::qWait(100);
        }
        const auto files = QDir(directory.path()).entryList({"*.log"}, QDir::Files);
        QCOMPARE(files.size(), 1);
        QFile file(directory.filePath(files.first()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto text = file.readAll();
        QVERIFY(text.contains("qt-visible-message"));
        QVERIFY(text.contains("qml-visible-message"));
        QVERIFY(text.contains("qml-debug-message"));
        QVERIFY(text.contains("native.api"));
        QVERIFY(text.contains("echo.api.capabilities"));
        QVERIFY(text.contains("[redacted sensitive log]"));
        QVERIFY(!text.contains("must-not-persist"));
        for (const auto &line : text.split('\n')) {
            if (line.isEmpty())
                continue;
            QVERIFY2(linePattern().match(QString::fromUtf8(line)).hasMatch(), line.constData());
        }
    }
    void qtDebugIsFilteredAndRotationRetainsWholeRecords()
    {
        QTemporaryDir directory;
        {
            LogService logging(directory.path(), 2, false);
            qDebug("disabled-debug-message");
            // Produce >5 MiB while allowing the bounded worker queue to drain.
            const QString payload(7800, QChar(u'x'));
            for (int batch = 0; batch < 20; ++batch) {
                for (int row = 0; row < 40; ++row)
                    qInfo().noquote() << payload;
                QTest::qWait(25);
            }
            qInfo("last-record-after-rotation");
        }
        const auto files = QDir(directory.path()).entryList({"*.log"}, QDir::Files);
        QVERIFY(files.size() >= 2);
        QVERIFY(files.size() <= 7);
        bool last = false;
        for (const auto &name : files) {
            QFile file(directory.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const auto bytes = file.readAll();
            QVERIFY(!bytes.contains("disabled-debug-message"));
            last = last || bytes.contains("last-record-after-rotation");
            for (const auto &line : bytes.split('\n')) {
                if (!line.isEmpty())
                    QVERIFY2(linePattern().match(QString::fromUtf8(line)).hasMatch(),
                             line.constData());
            }
        }
        QVERIFY(last);
    }
};
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3 && app.arguments().at(1) == "--format-probe") {
        LogService logging(app.arguments().at(2), 3, true);
        qInfo().noquote() << QString::fromUtf8("format-probe 中文\nnext\r\t\x1b[31m");
        qWarning("password=do-not-store");
        return 0;
    }
    LoggingTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "LoggingTest.moc"
