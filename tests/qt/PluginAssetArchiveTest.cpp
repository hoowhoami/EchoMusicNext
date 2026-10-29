#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(EchoMusic_ThemesPlugin)
#include "EchoPluginAssetArchive.h"
#include "EchoPluginAssets.h"
#include "plugins/PluginRuntimeTestFixture.h"
#include <QApplication>
#include <QBuffer>
#include <QFileDialog>
#include <QImageReader>
#include <QCryptographicHash>
#include <QDataStream>
#include "asset-archive/CaseFold.h"

namespace
{
QByteArray storedZip(const QStringList &names)
{
    // Tiny fixture writer only: UTF-8 names, empty Stored entries, no ZIP dependency in the test.
    QByteArray result, central;
    QDataStream output(&result, QIODevice::WriteOnly), directory(&central, QIODevice::WriteOnly);
    output.setByteOrder(QDataStream::LittleEndian);
    directory.setByteOrder(QDataStream::LittleEndian);
    for (const auto &name : names) {
        const auto utf8 = name.toUtf8();
        const auto offset = quint32(result.size());
        output << quint32(0x04034b50) << quint16(20) << quint16(0x800) << quint16(0) << quint16(0)
               << quint16(0) << quint32(0) << quint32(0) << quint32(0) << quint16(utf8.size())
               << quint16(0);
        output.writeRawData(utf8.constData(), utf8.size());
        directory << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(0x800)
                  << quint16(0) << quint16(0) << quint16(0) << quint32(0) << quint32(0)
                  << quint32(0) << quint16(utf8.size()) << quint16(0) << quint16(0) << quint16(0)
                  << quint16(0) << quint32(0) << offset;
        directory.writeRawData(utf8.constData(), utf8.size());
    }
    const auto offset = quint32(result.size());
    output.writeRawData(central.constData(), central.size());
    output << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(names.size())
           << quint16(names.size()) << quint32(central.size()) << offset << quint16(0);
    return result;
}
struct ArchiveFixture
{
    Host host;
    QJSValueList roots;
    EchoPluginContext *scope = nullptr;
    bool activate()
    {
        host.runtime.importDirectory(
            package(host.temporary.path(), "export function activate(ctx) {}"));
        QElapsedTimer deadline;
        deadline.start();
        while (deadline.elapsed() < 7000) {
            const auto id = host.runtime.installationForId("test.runtime");
            if (!id.isEmpty() && host.runtime.state(id) == PluginRuntime::Active) {
                scope = host.runtime.context(id);
                return scope;
            }
            QTest::qWait(1);
        }
        return false;
    }
    EchoPluginOperation *retain(EchoPluginOperation *operation)
    {
        roots.append(host.engine.newQObject(operation));
        return operation;
    }
    QFileDialog *picker()
    {
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *dialog = qobject_cast<QFileDialog *>(widget); dialog && dialog->isVisible())
                return dialog;
        return nullptr;
    }
};
} // namespace

class PluginAssetArchiveTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        configureUiTypography();
        QApplication::setFont(uiFont("system-ui"));
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }
    void realSvgPickerRasterPreviewAndOriginalArchiveRoundtrip()
    {
        ArchiveFixture fixture;
        QVERIFY(fixture.activate());
        const QByteArray original("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='8'>"
                                  "<rect width='16' height='8' fill='#20a040'/></svg>");
        const auto source = fixture.host.temporary.path() + "/art.svg";
        write(source, original);
        auto *assets = fixture.scope->assets();
        auto *import = fixture.retain(assets->importImages("example.images"));
        QTRY_VERIFY(fixture.picker());
        fixture.picker()->selectFile(source);
        QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
        QTRY_COMPARE_WITH_TIMEOUT(import->state(), EchoPluginOperation::Succeeded, 15000);
        const auto descriptor = import->result().toMap().value("assets").toList().first().toMap();
        QCOMPARE(descriptor.value("mimeType").toString(), "image/svg+xml");
        QCOMPARE(descriptor.value("bytes").toLongLong(), original.size());
        QVERIFY(descriptor.value("url").toUrl().toString().endsWith(".png"));
        QImageReader reader(descriptor.value("url").toUrl().toLocalFile());
        QCOMPARE(reader.format(), QByteArray("png"));
        QCOMPARE(reader.size(), QSize(16, 8));
        const auto zipPath = fixture.host.temporary.path() + "/backup.zip";
        const QVariantList exported{
            {QVariantMap{{"handle", descriptor.value("handle")}, {"path", "images/old-bare-id"}}}};
        const QVariantMap json{
            {"meta.json", QVariantMap{{"example", true}, {"padding", QString(60000, 'y')}}},
            {"pool.json", QVariantMap{{"data", QString(60000, 'x')}}}};
        auto *save = fixture.retain(
            assets->exportImageArchive("example.images", exported, json, "backup.zip"));
        QTRY_VERIFY(fixture.picker());
        QCOMPARE(fixture.picker()->acceptMode(), QFileDialog::AcceptSave);
        fixture.picker()->selectFile(zipPath);
        QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
        QTRY_COMPARE_WITH_TIMEOUT(save->state(), EchoPluginOperation::Succeeded, 15000);
        QVERIFY(QFileInfo(zipPath).size() > 0);
        auto *lease = assets->openImageArchive({"meta.json", "pool.json"});
        QVERIFY(lease);
        QTRY_VERIFY(fixture.picker());
        fixture.picker()->selectFile(zipPath);
        QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(lease->ready() || !lease->errorString().isEmpty(), 15000);
        QVERIFY2(lease->ready(), qPrintable(lease->errorString()));
        QCOMPARE(lease->jsonFiles().value("meta.json").toMap().value("example").toBool(), true);
        QCOMPARE(lease->jsonFiles().value("pool.json").toMap().value("data").toString().size(),
                 60000);
        auto *restore = fixture.retain(lease->importInto(
            "restored.images",
            {{QVariantMap{{"path", "images/old-bare-id"}, {"name", "restored.svg"}}}}));
        QTRY_COMPARE_WITH_TIMEOUT(restore->state(), EchoPluginOperation::Succeeded, 15000);
        const auto restored = restore->result().toMap().value("assets").toList();
        QCOMPARE(restored.size(), 1);
        QCOMPARE(restored.first().toMap().value("name").toString(), "restored.svg");
        QCOMPARE(restored.first().toMap().value("mimeType").toString(), "image/svg+xml");
        QVERIFY(restored.first().toMap().value("handle") != descriptor.value("handle"));
        const auto entry = restored.first().toMap();
        QFile originalFile(QDir(QFileInfo(entry.value("url").toUrl().toLocalFile()).absolutePath())
                               .filePath(entry.value("handle").toString().mid(6) + ".svg"));
        QVERIFY(originalFile.open(QIODevice::ReadOnly));
        QCOMPARE(originalFile.readAll(), original);
    }
    void pickerCancellationAndInvalidArgumentsDoNotPublish()
    {
        ArchiveFixture fixture;
        QVERIFY(fixture.activate());
        auto *assets = fixture.scope->assets();
        auto *invalid =
            fixture.retain(assets->exportImageArchive("../foreign", {}, {}, "export.zip"));
        QTRY_COMPARE(invalid->state(), EchoPluginOperation::Failed);
        auto *lease = assets->openImageArchive({"meta.json"});
        QVERIFY(lease);
        QCOMPARE(EchoPluginAssets::archiveReservedBytes(), 144ll * 1024 * 1024);
        QTRY_VERIFY(fixture.picker());
        QPointer<EchoPluginAssetArchive> guarded = lease;
        lease->dispose();
        QTRY_VERIFY(guarded.isNull());
        QTRY_VERIFY(!fixture.picker());
        QTRY_COMPARE(EchoPluginAssets::archiveReservedBytes(), 0);
        auto *list = fixture.retain(assets->list("restored.images"));
        QTRY_COMPARE(list->state(), EchoPluginOperation::Succeeded);
        QVERIFY(list->result().toMap().value("assets").toList().isEmpty());
    }
    void unicodeAliasesAreRejectedBeforeExtraction_data()
    {
        QTest::addColumn<QStringList>("names");
        QTest::newRow("nfc") << QStringList{QString::fromUtf8("images/café"),
                                            QString::fromUtf8("images/café")};
        QTest::newRow("full-sharp-s")
            << QStringList{QString::fromUtf8("images/Straße"), "images/strasse"};
        QTest::newRow("directory-sharp-s")
            << QStringList{QString::fromUtf8("images/Straße/"), "images/strasse/"};
        QTest::newRow("full-ligature") << QStringList{QString::fromUtf8("images/ﬃ"), "images/ffi"};
        QTest::newRow("astral-case")
            << QStringList{QString::fromUtf8("images/𐐀"), QString::fromUtf8("images/𐐨")};
    }
    void unicodeAliasesAreRejectedBeforeExtraction()
    {
        QFETCH(QStringList, names);
        ArchiveFixture fixture;
        QVERIFY(fixture.activate());
        const auto path = fixture.host.temporary.path() + "/aliases.zip";
        write(path, storedZip(names));
        auto *lease = fixture.scope->assets()->openImageArchive({});
        QVERIFY(lease);
        QTRY_VERIFY(fixture.picker());
        fixture.picker()->selectFile(path);
        QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(lease->ready() || !lease->errorString().isEmpty(), 15000);
        QVERIFY(!lease->ready());
        QVERIFY(!lease->errorString().isEmpty());
        QVERIFY(lease->entries().isEmpty());
        lease->dispose();
        QTRY_COMPARE(EchoPluginAssets::archiveReservedBytes(), 0);
    }
    void unicodeFullTableMatchesEachOfficialMapping()
    {
        using namespace EchoMusic::AssetArchiveCaseFold;
        for (const auto &entry : entries) {
            const auto from = QString::fromUcs4(&entry.from, 1);
            const auto into = QString::fromUcs4(entry.into, entry.count);
            QCOMPARE(key(from), into.normalized(QString::NormalizationForm_C));
        }
        QCOMPARE(key(QString::fromUtf8("Straße")), QString("strasse"));
        QCOMPARE(key(QString::fromUtf8("CAFÉ")), key(QString::fromUtf8("café")));
        QCOMPARE(key(QString::fromUtf8("👩‍🚀")), QString::fromUtf8("👩‍🚀"));
    }
};
QTEST_MAIN(PluginAssetArchiveTest)
#include "PluginAssetArchiveTest.moc"
