#include "PluginFileCapture_p.h"
#include "PluginImportResult_p.h"
#include <QBuffer>
#include <QFile>
#include <QTemporaryDir>
#include <QDirIterator>
#include <QtConcurrentRun>
#include <QtTest>
#include <stdexcept>

using namespace EchoMusic;
class ReportedSizeBuffer : public QBuffer
{
public:
    qint64 reported = 0;
    qint64 size() const override
    {
        return reported;
    }
};
class FailedRead : public QIODevice
{
public:
    int reads = 0;
    qint64 size() const override
    {
        return 1;
    }

protected:
    qint64 readData(char *, qint64) override
    {
        ++reads;
        setErrorString("read failure");
        return -1;
    }
    qint64 writeData(const char *, qint64) override
    {
        return -1;
    }
};
class PluginImportCaptureTest : public QObject
{
    Q_OBJECT
private slots:
    void actualArchiveCapturePreservesContentHash()
    {
        const auto archive = qEnvironmentVariable("ECHO_PLUGIN_CAPTURE_ARCHIVE");
        if (archive.isEmpty())
            QSKIP("Set ECHO_PLUGIN_CAPTURE_ARCHIVE to check an actual published package");
        QTemporaryDir root;
        const auto source = root.filePath("source");
        QVERIFY2(PluginPackage::extractArchive(archive, source).isEmpty(), qPrintable(archive));
        QList<QByteArray> retained;
        qint64 bytes = 0, capacity = 0;
        QDirIterator files(source, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            QFile file(files.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            auto captured = PluginPackageDetail::captureFile(file, 128ll * 1024 * 1024 - bytes);
            QVERIFY2(captured.error.isEmpty(), qPrintable(captured.error));
            bytes += captured.bytes.size();
            capacity += captured.bytes.capacity();
            retained.append(captured.bytes);
        }
        QVERIFY(capacity <= bytes * 2 + retained.size() * 4096);
        const auto count = retained.size();
        retained.clear();
        const auto imported =
            PluginPackage::importDirectory(source, root.filePath("images"), "capture-test");
        QVERIFY2(imported.error.isEmpty(), qPrintable(imported.error));
        const auto expected = qEnvironmentVariable("ECHO_PLUGIN_CAPTURE_HASH");
        if (!expected.isEmpty())
            QCOMPARE(imported.contentHash, expected);
        qInfo() << "Actual import files" << count << "bytes" << bytes << "retained capacity"
                << capacity << "hash" << imported.contentHash;
    }
    void manySmallFilesRetainOnlyTheirActualBytes()
    {
        QTemporaryDir root;
        QList<QByteArray> retained;
        qint64 capacity = 0;
        for (int i = 0; i < 256; ++i) {
            QFile file(root.filePath(QString::number(i)));
            QVERIFY(file.open(QIODevice::ReadWrite));
            const auto expected = QByteArray::number(i) + QByteArray(32, 'x');
            QCOMPARE(file.write(expected), expected.size());
            QVERIFY(file.seek(0));
            auto capture = PluginPackageDetail::captureFile(file, 128ll * 1024 * 1024);
            QVERIFY2(capture.error.isEmpty(), qPrintable(capture.error));
            QCOMPARE(capture.bytes, expected);
            capacity += capture.bytes.capacity();
            retained.append(capture.bytes);
        }
        QVERIFY2(capacity < 1024 * 1024, qPrintable(QString::number(capacity)));
        QCOMPARE(retained.size(), 256);
    }
    void changedAndUnreadableFilesFailBeforePublishing()
    {
        ReportedSizeBuffer file;
        file.setData("abc");
        file.reported = 2;
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(PluginPackageDetail::captureFile(file, 10).error.contains("changed"));
        QVERIFY(file.seek(0));
        file.reported = 4;
        QVERIFY(PluginPackageDetail::captureFile(file, 10).error.contains("truncated"));
        QVERIFY(file.seek(0));
        file.reported = 1000000000;
        QVERIFY(PluginPackageDetail::captureFile(file, 10).error.contains("byte limit"));
        QCOMPARE(file.pos(), 0);
        FailedRead broken;
        QVERIFY(broken.open(QIODevice::ReadOnly));
        QVERIFY(!PluginPackageDetail::captureFile(broken, 10).error.isEmpty());
        QVERIFY(broken.reads > 0);
        QBuffer empty;
        QVERIFY(empty.open(QIODevice::ReadOnly));
        const auto captured = PluginPackageDetail::captureFile(empty, 0);
        QVERIFY(captured.error.isEmpty());
        QVERIFY(captured.bytes.isEmpty());
    }
    void workerAndFutureExceptionsBecomeInstallationFailures()
    {
        auto failed = PluginRuntimeDetail::importedPackage(
            "test-plugin", "import worker",
            []() -> PluginPackage { throw std::runtime_error("controlled import failure"); });
        QCOMPARE(failed.id, "test-plugin");
        QVERIFY(failed.error.contains("controlled import failure"));
        auto future = QtConcurrent::run(
            []() -> PluginPackage { throw std::runtime_error("future import failure"); });
        const auto forwarded = PluginRuntimeDetail::importedPackage(
            "test-plugin", "import completion", [&] { return future.result(); });
        QVERIFY(forwarded.error.contains("future import failure"));
        const auto retried =
            PluginRuntimeDetail::importedPackage("test-plugin", "import retry", [] {
                PluginPackage package;
                package.id = "test-plugin";
                return package;
            });
        QVERIFY(retried.error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(PluginImportCaptureTest)
#include "PluginImportCaptureTest.moc"
