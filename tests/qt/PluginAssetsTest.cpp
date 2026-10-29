#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(EchoMusic_ThemesPlugin)
#include "EchoPluginAssets.h"
#include "plugins/PluginRuntimeTestFixture.h"
#include <QApplication>
#include <QFileDialog>
#include <QImageReader>
#include <QMovie>
#include <QFontDatabase>
#include <QBuffer>
#include <QNetworkReply>
#include <QLineEdit>
#include <QDirIterator>
#include <QThread>
#include "StorageFence.h"
#include <QScopeGuard>
#include <utility>

namespace
{
struct AssetsFixture
{
    Host host;
    QJSValueList roots;
    EchoPluginContext *scope = nullptr;
    bool activate(const std::function<void(const QString &)> &prepare = {})
    {
        const auto source = package(host.temporary.path(), "export function activate(ctx) {}");
        if (prepare)
            prepare(source);
        host.runtime.importDirectory(source);
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
    EchoPluginOperation *retain(EchoPluginOperation *op)
    {
        roots.append(host.engine.newQObject(op));
        return op;
    }
    QFileDialog *picker()
    {
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *dialog = qobject_cast<QFileDialog *>(widget); dialog && dialog->isVisible())
                return dialog;
        return nullptr;
    }
};

std::unique_ptr<QQuickItem> imageDropItem(AssetsFixture &fixture, QQuickWindow &window,
                                          QString &error)
{
    QQmlComponent component(&fixture.host.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.PluginSdk
EchoPluginImageDropArea {
    id: root
    width: 180; height: 140; collection: "drops"; maxCount: 2
    readonly property bool mirroredBusy: busy
    function startInternal() { internalSource.Drag.active = true; internalSource.x += 1; }
    function finishInternal() { internalSource.Drag.drop(); internalSource.Drag.active = false; }
    Item {
        id: internalSource
        x: 20; y: 20; width: 20; height: 20
        Drag.source: null
        Drag.hotSpot.x: 0; Drag.hotSpot.y: 0
    }
})",
                      QUrl("test:ImageDrop.qml"));
    QElapsedTimer loading;
    loading.start();
    while (component.isLoading() && loading.elapsed() < 1000)
        QTest::qWait(1);
    if (!component.isReady()) {
        error = component.errorString();
        if (error.isEmpty())
            error = "Image drop component did not finish loading within one second";
        return {};
    }
    std::unique_ptr<QQuickItem> item(
        qobject_cast<QQuickItem *>(component.createWithInitialProperties(
            {{"assets", QVariant::fromValue(fixture.scope->assets())}})));
    if (!item) {
        error = component.errorString();
        return {};
    }
    item->setParentItem(window.contentItem());
    window.resize(200, 160);
    window.show();
    return item;
}

struct NativeDragObservation : QObject
{
    int enters = 0;
    bool internal = false, sourceNull = false;
    QUrl internalUrl;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::DragEnter) {
            ++enters;
            const auto *drag = static_cast<QDragEnterEvent *>(event);
            internal = drag->mimeData()->inherits("QQuickDragMimeData");
            sourceNull = drag->source() == nullptr;
            // Use the actual Qt-created internal MIME instance. Add a URI in
            // this test filter so lack of URLs cannot hide a broken class guard.
            if (internal && !internalUrl.isEmpty())
                const_cast<QMimeData *>(drag->mimeData())->setUrls({internalUrl});
        }
        return false;
    }
};

struct CoverServer
{
    QTcpServer server;
    QByteArray png;
    QList<QPointer<QTcpSocket>> waiting;
    bool hold = false;
    bool trickle = false;
    CoverServer()
    {
        QImage image(1, 1, QImage::Format_RGBA8888);
        image.fill(QColor(24, 48, 72, 255));
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        server.listen(QHostAddress::LocalHost);
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    auto request = socket->property("request").toByteArray();
                    request.append(socket->readAll());
                    socket->setProperty("request", request);
                    if (!request.contains("\r\n\r\n") || socket->property("handled").toBool())
                        return;
                    socket->setProperty("handled", true);
                    if (hold)
                        waiting.append(socket);
                    else
                        respond(socket);
                });
            }
        });
    }
    QUrl url() const
    {
        return QUrl(QString("http://127.0.0.1:%1/current.png").arg(server.serverPort()));
    }
    void respond(QTcpSocket *socket)
    {
        if (trickle) {
            socket->write(
                "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: 100000\r\n\r\nx");
            auto *timer = new QTimer(socket);
            QObject::connect(timer, &QTimer::timeout, socket, [socket] { socket->write("x"); });
            timer->start(1000);
            return;
        }
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: " +
                      QByteArray::number(png.size()) + "\r\nConnection: close\r\n\r\n" + png);
        socket->disconnectFromHost();
    }
};

PlaybackTrack coverTrack(AssetsFixture &fixture, const QString &id, const QUrl &cover)
{
    const QByteArray pcm(48000 * 2 * 60, '\0');
    QByteArray wave;
    QDataStream stream(&wave, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + pcm.size());
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000)
           << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(pcm.size());
    wave.append(pcm);
    const auto path = fixture.host.temporary.path() + '/' + id + ".wav";
    write(path, wave);
    PlaybackTrack track;
    track.id = id;
    track.title = id;
    track.localPath = path;
    track.durationMs = 60000;
    track.cover = cover.toString();
    return track;
}
} // namespace

class PluginAssetsTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        configureUiTypography();
        QApplication::setFont(uiFont("system-ui"));
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }
    void invalidArgumentsAndForeignSources();
    void frozenStorageRejectsAssetMutation_data();
    void frozenStorageRejectsAssetMutation();
    void snapshotBarrierWaitsForAcceptedAssetJob();
    void storageFenceRejectsAssetMutation();
    void readyFrameCachesStayOutsideSnapshots();
    void realPickerImportSampleListAndRemove();
    void mixedImagePickerBatch_data();
    void mixedImagePickerBatch();
    void imageBatchRejectsInvalidOptions();
    void partialImageStorageFailureRollsBackAllAddedImages();
    void internalQuickDragCannotGrantImagePaths_data();
    void internalQuickDragCannotGrantImagePaths();
    void externalNativeImageDrop_data();
    void externalNativeImageDrop();
    void externalArchiveDropUsesTheExistingTypedLease_data();
    void externalArchiveDropUsesTheExistingTypedLease();
    void cancelPickerAndRetireWithoutLatePublication();
    void originalAnimatedGifAndFirstFrameSampling();
    void persistentHandlesSurviveActivation();
    void fontsAreReleasedAfterLastScopeReference();
    void packagePixelsAndForeignSymlink();
    void byteBudgetAndTruncatedAnimationFailWithoutPublication();
    void cancelAcceptedImportBeforePublication_data();
    void cancelAcceptedImportBeforePublication();
    void realCurrentCoverNetworkAndSourceRevisionCancellation();
    void tamperedPersistentFontIsRejectedBeforeRegistration();
    void trickleCoverHasAbsoluteNetworkDeadline();
    void batchFontStagingNeverExceedsInstallationBudget();
    void publicationNotifiesAllActiveScopesAfterCleanup();
};

void PluginAssetsTest::frozenStorageRejectsAssetMutation_data()
{
    QTest::addColumn<QString>("command");
    QTest::newRow("clear") << QString("clear");
    QTest::newRow("remove") << QString("remove");
    QTest::newRow("import") << QString("import");
}
void PluginAssetsTest::frozenStorageRejectsAssetMutation()
{
    QFETCH(QString, command);
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    auto *scope = fixture.scope;
    auto *assets = scope->assets();
    scope->freezeStorage(true);
    const auto unfreeze = qScopeGuard([scope] { scope->freezeStorage(false); });
    auto *operation = fixture.retain(command == "clear" ? assets->clear("frozen")
                                     : command == "remove"
                                         ? assets->remove("asset:0123456789abcdef0123456789abcdef")
                                         : assets->importImages("frozen"));
    // Frozen admission is synchronous; a picker or worker must not be started.
    QCOMPARE(operation->state(), EchoPluginOperation::Failed);
    QCOMPARE(operation->errorCode(), QString("StorageFrozen"));
    QVERIFY(!fixture.picker());
}
void PluginAssetsTest::snapshotBarrierWaitsForAcceptedAssetJob()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    CoverServer server;
    QVERIFY(server.server.isListening());
    const auto cover = server.url();
    fixture.host.playback.setVolume(0);
    fixture.host.playback.playTracks({coverTrack(fixture, "snapshot-cover", cover)}, 0, "Snapshot",
                                     "snapshot");
    QTRY_VERIFY_WITH_TIMEOUT(fixture.host.playback.playing() && !fixture.host.playback.loading(),
                             15000);
    auto *scope = fixture.scope;
    auto *assets = scope->assets();
    auto *warm = fixture.retain(assets->sampleImage(cover, 1, 1));
    QTRY_COMPARE_WITH_TIMEOUT(warm->state(), EchoPluginOperation::Succeeded, 15000);
    QTRY_VERIFY(assets->isDrained());
    server.hold = true;
    auto *operation = fixture.retain(assets->sampleImage(cover, 1, 1));
    QTRY_VERIFY(!server.waiting.isEmpty());
    QCOMPARE(operation->state(), EchoPluginOperation::Running);
    QVERIFY(!assets->isDrained());
    scope->freezeStorage(true);
    const auto unfreeze = qScopeGuard([scope] { scope->freezeStorage(false); });
    struct Witnesses
    {
        bool assets = false, io = false, sqlite = false;
    };
    const auto witness = std::make_shared<Witnesses>();
    scope->storageBarrier([witness] { witness->assets = true; });
    // FIFO witnesses prove the existing worker barriers have actually arrived;
    // no sleep or op.finished can stand in for the held asset request's drain.
    scope->io()->storageBarrier([witness] { witness->io = true; });
    scope->sqlite()->storageBarrier([witness] { witness->sqlite = true; });
    QTRY_VERIFY(witness->io && witness->sqlite);
    QVERIFY(!witness->assets);
    for (const auto &socket : std::as_const(server.waiting))
        if (socket)
            server.respond(socket);
    QTRY_COMPARE_WITH_TIMEOUT(operation->state(), EchoPluginOperation::Succeeded, 15000);
    QTRY_VERIFY(witness->assets);
    QVERIFY(assets->isDrained());
    QCOMPARE(operation->result().toMap().value("pixels").toList(), QVariantList({24, 48, 72, 255}));
}

void PluginAssetsTest::storageFenceRejectsAssetMutation()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    const auto snapshot = StorageFence::freeze(fixture.scope->dataPath());
    QVERIFY(snapshot);
    QVERIFY(!fixture.scope->storageFrozen());
    auto *operation = fixture.retain(fixture.scope->assets()->clear("fenced"));
    QCOMPARE(operation->state(), EchoPluginOperation::Failed);
    QCOMPARE(operation->errorCode(), QString("StorageFrozen"));
    QVERIFY(fixture.scope->assets()->isDrained());
}
void PluginAssetsTest::readyFrameCachesStayOutsideSnapshots()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate([](const QString &source) {
        QImage image(1, 1, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        if (!image.save(source + "/owned.png"))
            qFatal("Cannot write owned frame fixture");
    }));
    auto *scope = fixture.scope;
    auto *assets = scope->assets();
    QPointer<EchoPluginAssetAnimation> lease =
        assets->animation(QUrl::fromLocalFile(scope->package().resolve("owned.png")), 1, 1);
    QVERIFY(lease);
    QTRY_VERIFY_WITH_TIMEOUT(lease->ready() || !lease->errorString().isEmpty(), 15000);
    QVERIFY2(lease->ready(), qPrintable(lease->errorString()));
    const auto frame = QFileInfo(lease->frameSource().toLocalFile()).canonicalFilePath();
    QVERIFY(!frame.isEmpty());
    const auto data = QFileInfo(scope->dataPath()).canonicalFilePath();
    QVERIFY(!data.isEmpty());
    QVERIFY(!frame.startsWith(data + '/'));
    scope->freezeStorage(true);
    const auto unfreeze = qScopeGuard([scope] { scope->freezeStorage(false); });
    const auto ready = std::make_shared<bool>(false);
    scope->storageBarrier([ready] { *ready = true; });
    QTRY_VERIFY(*ready);
    QVERIFY(lease && lease->active() && lease->ready());
    QVERIFY(!assets->isDrained()); // The ready image lease stays available.
    QVERIFY(QFileInfo::exists(frame));
    lease->dispose();
    QTRY_VERIFY(lease.isNull());
    QTRY_VERIFY(!QFileInfo::exists(frame));
    QTRY_VERIFY(assets->isDrained());
}

void PluginAssetsTest::invalidArgumentsAndForeignSources()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    auto *assets = fixture.scope->assets();
    auto *invalid = fixture.retain(assets->list("../escape"));
    QTRY_COMPARE(invalid->state(), EchoPluginOperation::Failed);
    QCOMPARE(invalid->errorCode(), "InvalidArgument");
    auto *source = fixture.retain(assets->sampleImage(QUrl("file:///etc/hosts"), 64, 64));
    QTRY_COMPARE(source->state(), EchoPluginOperation::Failed);
    QCOMPARE(source->errorCode(), "SourceDenied");
    auto *remote = fixture.retain(assets->sampleImage(QUrl("https://example.invalid/image.png")));
    QTRY_COMPARE(remote->state(), EchoPluginOperation::Failed);
    QCOMPARE(remote->errorCode(), "SourceDenied");
    auto *size = fixture.retain(assets->sampleImage(QUrl("asset:missing"), 257, 1));
    QTRY_COMPARE(size->state(), EchoPluginOperation::Failed);
    QCOMPARE(size->errorCode(), "InvalidArgument");
}

void PluginAssetsTest::realPickerImportSampleListAndRemove()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    const auto originalLimit = QImageReader::allocationLimit();
    const auto source = fixture.host.temporary.path() + "/selected.png";
    QImage image(2, 1, QImage::Format_RGBA8888);
    image.setPixelColor(0, 0, QColor(255, 0, 0, 255));
    image.setPixelColor(1, 0, QColor(0, 0, 255, 128));
    QVERIFY(image.save(source));
    auto *assets = fixture.scope->assets();
    auto *import = fixture.retain(assets->importImages("cappella.avatars"));
    QTRY_VERIFY(fixture.picker());
    fixture.picker()->selectFile(source);
    QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(import->state(), EchoPluginOperation::Succeeded, 15000);
    const auto imported = import->result().toMap().value("assets").toList();
    QCOMPARE(imported.size(), 1);
    const auto asset = imported.first().toMap();
    QVERIFY(!asset.value("handle").toString().isEmpty());
    QCOMPARE(asset.value("collection").toString(), "cappella.avatars");
    QCOMPARE(asset.value("width").toInt(), 2);
    QCOMPARE(asset.value("height").toInt(), 1);
    QCOMPARE(asset.value("animated").toBool(), false);
    QVERIFY(asset.value("url").toUrl().isLocalFile());
    QVERIFY(asset.value("url").toUrl().toLocalFile() != source);
    QVERIFY(QFile::remove(source));
    auto *sample =
        fixture.retain(assets->sampleImage(QUrl(asset.value("handle").toString()), 2, 1));
    QTRY_COMPARE_WITH_TIMEOUT(sample->state(), EchoPluginOperation::Succeeded, 15000);
    const auto pixels = sample->result().toMap().value("pixels").toList();
    QCOMPARE(pixels, QVariantList({255, 0, 0, 255, 0, 0, 255, 128}));
    auto *resized =
        fixture.retain(assets->sampleImage(QUrl(asset.value("handle").toString()), 1, 2));
    QTRY_COMPARE_WITH_TIMEOUT(resized->state(), EchoPluginOperation::Succeeded, 15000);
    const auto dimensions = resized->result().toMap();
    QCOMPARE(dimensions.value("width").toInt(), 1);
    QCOMPARE(dimensions.value("height").toInt(), 2);
    QCOMPARE(dimensions.value("sourceWidth").toInt(), 2);
    QCOMPARE(dimensions.value("sourceHeight").toInt(), 1);
    QCOMPARE(QImageReader::allocationLimit(), originalLimit);
    auto *list = fixture.retain(assets->list("cappella.avatars"));
    QTRY_COMPARE(list->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(list->result().toMap().value("assets").toList().size(), 1);
    auto *remove = fixture.retain(assets->remove(asset.value("handle").toString()));
    QTRY_COMPARE(remove->state(), EchoPluginOperation::Succeeded);
    auto *missing = fixture.retain(assets->sampleImage(QUrl(asset.value("handle").toString())));
    QTRY_COMPARE(missing->state(), EchoPluginOperation::Failed);
    QCOMPARE(missing->errorCode(), "SourceDenied");
    auto *empty = fixture.retain(assets->list("cappella.avatars"));
    QTRY_COMPARE(empty->state(), EchoPluginOperation::Succeeded);
    QVERIFY(empty->result().toMap().value("assets").toList().isEmpty());
}

void PluginAssetsTest::mixedImagePickerBatch_data()
{
    QTest::addColumn<bool>("partial");
    QTest::addColumn<int>("maxCount");
    QTest::addColumn<QString>("validName");
    QTest::newRow("default-remains-atomic") << false << 16 << QString("valid.png");
    QTest::newRow("explicit-partial-retains-valid-image") << true << 2 << QString("valid.png");
    QTest::newRow("partial-reserves-only-requested-supported-slots")
        << true << 1 << QString("valid.png");
    QTest::newRow("real-png-with-unconventional-extension") << true << 2 << QString("valid.data");
}

void PluginAssetsTest::mixedImagePickerBatch()
{
    QFETCH(bool, partial);
    QFETCH(int, maxCount);
    QFETCH(QString, validName);
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    QImage image(2, 1, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    QVERIFY(image.save(fixture.host.temporary.filePath(validName), "PNG"));
    write(fixture.host.temporary.filePath("corrupt.png"), "not a PNG image");
    auto *assets = fixture.scope->assets();
    EchoPluginOperation *operation = nullptr;
    if (partial) {
        // Use the actual JS caller, including the extra options argument. The
        // old one-argument Qt method ignores it and atomically fails this mix.
        auto wrapped = fixture.host.engine.newQObject(assets);
        auto method = wrapped.property("importImages");
        const auto result =
            method.callWithInstance(wrapped, {fixture.host.engine.toScriptValue(QString("mixed")),
                                              fixture.host.engine.toScriptValue(QVariantMap{
                                                  {"partial", true}, {"maxCount", maxCount}})});
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        operation = qobject_cast<EchoPluginOperation *>(result.toQObject());
        QVERIFY(operation);
        fixture.retain(operation);
    } else {
        operation = fixture.retain(assets->importImages("mixed"));
    }
    QTRY_VERIFY(fixture.picker());
    fixture.picker()->setDirectory(fixture.host.temporary.path());
    auto *fileName = fixture.picker()->findChild<QLineEdit *>("fileNameEdit");
    QVERIFY(fileName);
    fileName->setText('"' + validName + "\" \"corrupt.png\"");
    QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(operation->finished(), 15000);
    qInfo() << "actual mixed PNG/corrupt picker" << partial << operation->state()
            << operation->errorCode() << operation->errorMessage() << operation->result();
    QCOMPARE(operation->state(),
             partial ? EchoPluginOperation::Succeeded : EchoPluginOperation::Failed);
    if (partial) {
        const auto result = operation->result().toMap();
        QCOMPARE(result.value("assets").toList().size(), 1);
        QCOMPARE(result.value("assets").toList().first().toMap().value("name").toString(),
                 validName);
        const auto rejected = result.value("rejected").toList();
        QCOMPARE(rejected.size(), 1);
        QCOMPARE(rejected.first().toMap().value("name").toString(), "corrupt.png");
        QVERIFY(!rejected.first().toMap().value("code").toString().isEmpty());
        QCOMPARE(rejected.first().toMap().value("code").toString(),
                 maxCount == 1 ? QString("QuotaExceeded") : QString("AssetDecodeError"));
        QVERIFY(!rejected.first().toMap().value("message").toString().isEmpty());
        QVERIFY(!QJsonDocument::fromVariant(result).toJson().contains(
            fixture.host.temporary.path().toUtf8() + "/corrupt.png"));
    }
    QTRY_VERIFY_WITH_TIMEOUT(assets->isDrained(), 15000);
    auto *list = fixture.retain(assets->list("mixed"));
    QTRY_COMPARE(list->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(list->result().toMap().value("assets").toList().size(), partial ? 1 : 0);
}

void PluginAssetsTest::imageBatchRejectsInvalidOptions()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    const QList<QVariantMap> invalid{{{"partial", "true"}}, {{"maxCount", 0}},
                                     {{"maxCount", 17}},    {{"maxCount", 1.5}},
                                     {{"maxCount", "2"}},   {{"unexpected", true}}};
    for (const auto &options : invalid) {
        auto *op = fixture.retain(fixture.scope->assets()->importImages("images", options));
        QCOMPARE(op->state(), EchoPluginOperation::Failed);
        QCOMPARE(op->errorCode(), "InvalidArgument");
        QVERIFY(!fixture.picker());
    }
    QVERIFY(PluginPackage::supportedFeatures().contains("assets.imageBatches"));
    QVERIFY(PluginPackage::supportedFeatures().contains("assets.imageDrops"));
}

void PluginAssetsTest::partialImageStorageFailureRollsBackAllAddedImages()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    QImage image(2, 2, QImage::Format_RGBA8888);
    image.fill(Qt::green);
    const auto source = fixture.host.temporary.filePath("storage.png");
    QVERIFY(image.save(source));
    auto *assets = fixture.scope->assets();
    auto *op =
        fixture.retain(assets->importImages("storage", {{"partial", true}, {"maxCount", 1}}));
    QTRY_VERIFY(fixture.picker());
    fixture.picker()->selectFile(source);
    QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
    const QDir directory(fixture.scope->dataPath() + "/assets-v1");
    QElapsedTimer deadline;
    deadline.start();
    bool validated = false;
    // Deliberately hold only this test's GUI event dispatch until a real worker
    // has staged its PNG. Block index commit before its queued completion runs.
    while (!validated && deadline.elapsed() < 3000) {
        const auto stages = directory.entryList({".staging-*"}, QDir::Dirs | QDir::Hidden);
        for (const auto &stage : stages)
            validated |=
                !QDir(directory.filePath(stage)).entryList({"*.png"}, QDir::Files).isEmpty();
        if (!validated)
            QThread::msleep(1);
    }
    QVERIFY2(validated, "real decoder must finish staging before injecting storage failure");
    QVERIFY(QDir().mkdir(directory.filePath("index.json")));
    QTRY_VERIFY_WITH_TIMEOUT(op->finished(), 15000);
    QCOMPARE(op->state(), EchoPluginOperation::Failed);
    QCOMPARE(op->errorCode(), "AssetStorageError");
    QTRY_VERIFY_WITH_TIMEOUT(assets->isDrained(), 15000);
    QVERIFY(directory.entryList({"*.png", ".staging-*"}, QDir::Files | QDir::Dirs | QDir::Hidden)
                .isEmpty());
    QVERIFY(QDir().rmdir(directory.filePath("index.json")));
    auto *list = fixture.retain(assets->list("storage"));
    QTRY_COMPARE(list->state(), EchoPluginOperation::Succeeded);
    QVERIFY(list->result().toMap().value("assets").toList().isEmpty());
}

void PluginAssetsTest::internalQuickDragCannotGrantImagePaths_data()
{
    QTest::addColumn<QString>("extension");
    QTest::newRow("internal-image-uri") << QString(".png");
    QTest::newRow("internal-archive-uri") << QString(".zip");
}

void PluginAssetsTest::internalQuickDragCannotGrantImagePaths()
{
    QFETCH(QString, extension);
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    QQuickWindow window;
    QString error;
    auto item = imageDropItem(fixture, window, error);
    QVERIFY2(item, qPrintable(error));
    QSignalSpy started(item.get(), SIGNAL(importStarted(EchoMusic::EchoPluginOperation *)));
    QVERIFY(started.isValid());
    QVERIFY(item->setProperty("archiveJsonPaths", QStringList{"meta.json", "pool.json"}));
    QSignalSpy archives(item.get(), SIGNAL(archiveDropped(EchoMusic::EchoPluginAssetArchive *)));
    QVERIFY(archives.isValid());
    NativeDragObservation observed;
    observed.internalUrl =
        QUrl::fromLocalFile(fixture.host.temporary.filePath("not-authorized" + extension));
    item->installEventFilter(&observed);
    QVERIFY(QMetaObject::invokeMethod(item.get(), "startInternal"));
    QTRY_VERIFY_WITH_TIMEOUT(observed.enters > 0, 1000);
    QVERIFY(observed.internal);
    QVERIFY2(observed.sourceNull,
             "this regression must cover the real source-null internal Qt drag");
    QVERIFY(QMetaObject::invokeMethod(item.get(), "finishInternal"));
    QCOMPARE(started.count(), 0);
    QCOMPARE(archives.count(), 0);
    QCOMPARE(item->property("containsDrag").toBool(), false);
    QVERIFY(fixture.scope->assets()->isDrained());
    QCOMPARE(fixture.scope->assets()->metaObject()->indexOfMethod(
                 "importDroppedImages(QString,QList<QUrl>,int)"),
             -1);
    QCOMPARE(item->metaObject()->indexOfMethod("dropEvent(QDropEvent*)"), -1);
    QCOMPARE(fixture.scope->assets()->metaObject()->indexOfMethod(
                 "openDroppedImageArchive(QStringList,QUrl)"),
             -1);
}

void PluginAssetsTest::externalNativeImageDrop_data()
{
    QTest::addColumn<bool>("hideImmediately");
    QTest::addColumn<int>("maxCount");
    QTest::newRow("native-mixed-payload") << false << 2;
    QTest::newRow("hidden-owner-cancels-before-publication") << true << 2;
    QTest::newRow("zero-capacity-reports-every-skipped-input") << false << 0;
}

void PluginAssetsTest::externalNativeImageDrop()
{
    QFETCH(bool, hideImmediately);
    QFETCH(int, maxCount);
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    QImage image(2, 1, QImage::Format_RGBA8888);
    image.fill(Qt::blue);
    const auto valid = fixture.host.temporary.filePath("dropped.png");
    const auto invalid = fixture.host.temporary.filePath("bad.png");
    QVERIFY(image.save(valid));
    write(invalid, "not a PNG");
    QQuickWindow window;
    QString error;
    auto item = imageDropItem(fixture, window, error);
    QVERIFY2(item, qPrintable(error));
    QVERIFY(item->setProperty("maxCount", maxCount));
    QSignalSpy started(item.get(), SIGNAL(importStarted(EchoMusic::EchoPluginOperation *)));
    QVERIFY(started.isValid());
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(valid), QUrl::fromLocalFile(invalid)});
    QVERIFY(!mime.inherits("QQuickDragMimeData"));
    QDragEnterEvent enter(QPoint(30, 30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QVERIFY(enter.source() == nullptr);
    QCoreApplication::sendEvent(item.get(), &enter);
    QVERIFY(enter.isAccepted());
    QCOMPARE(item->property("containsDrag").toBool(), true);
    QDropEvent drop(QPointF(30, 30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(item.get(), &drop);
    QVERIFY(drop.isAccepted());
    QCOMPARE(started.count(), 1);
    auto *operation = qvariant_cast<EchoPluginOperation *>(started.first().first());
    QVERIFY(operation);
    fixture.retain(operation);
    if (hideImmediately) {
        QCOMPARE(item->property("mirroredBusy").toBool(), true);
        item->setVisible(false);
        QCOMPARE(item->property("mirroredBusy").toBool(), false);
    }
    QTRY_VERIFY_WITH_TIMEOUT(operation->finished(), 15000);
    QCOMPARE(operation->state(),
             hideImmediately ? EchoPluginOperation::Canceled : EchoPluginOperation::Succeeded);
    if (!hideImmediately) {
        const auto result = operation->result().toMap();
        QCOMPARE(result.value("assets").toList().size(), maxCount == 0 ? 0 : 1);
        QCOMPARE(result.value("rejected").toList().size(), maxCount == 0 ? 2 : 1);
        QCOMPARE(result.value("rejected").toList().last().toMap().value("name").toString(),
                 "bad.png");
    }
    QTRY_VERIFY_WITH_TIMEOUT(fixture.scope->assets()->isDrained(), 15000);
    auto *list = fixture.retain(fixture.scope->assets()->list("drops"));
    QTRY_COMPARE(list->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(list->result().toMap().value("assets").toList().size(),
             hideImmediately || maxCount == 0 ? 0 : 1);
}

void PluginAssetsTest::externalArchiveDropUsesTheExistingTypedLease_data()
{
    QTest::addColumn<QString>("retire");
    QTest::newRow("zip-precedes-image-and-inspects-at-zero-capacity") << QString();
    QTest::newRow("hide-cancels-archive-inspection") << QString("hide");
    QTest::newRow("account-epoch-cancels-archive-inspection") << QString("account");
}

void PluginAssetsTest::externalArchiveDropUsesTheExistingTypedLease()
{
    QFETCH(QString, retire);
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    auto *assets = fixture.scope->assets();
    QImage image(2, 1, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    const auto source = fixture.host.temporary.filePath("source.png");
    const auto zip = fixture.host.temporary.filePath("back up.zip");
    QVERIFY(image.save(source));
    const auto choose = [&fixture](const QString &path) {
        auto *dialog = fixture.picker();
        auto *name = dialog ? dialog->findChild<QLineEdit *>("fileNameEdit") : nullptr;
        if (!name)
            return false;
        name->setText('"' + path + '"');
        return QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
    };
    auto *import = fixture.retain(assets->importImages("archive-source"));
    QTRY_VERIFY(fixture.picker());
    QVERIFY(choose(source));
    QTRY_COMPARE_WITH_TIMEOUT(import->state(), EchoPluginOperation::Succeeded, 15000);
    const auto asset = import->result().toMap().value("assets").toList().first().toMap();
    const QVariantList entries{
        QVariantMap{{"handle", asset.value("handle")}, {"path", "images/source.png"}}};
    const QVariantMap json{
        {"meta.json", QVariantMap{{"kind", "folia-tempera-pool"}, {"schemaVersion", 1}}},
        {"pool.json",
         QVariantMap{{"layerImages", QVariantList{QVariantMap{{"id", "source"}, {"scale", .7}}}}}}};
    auto *save =
        fixture.retain(assets->exportImageArchive("archive-source", entries, json, "back up.zip"));
    QTRY_VERIFY(fixture.picker());
    QVERIFY(choose(zip));
    QTRY_COMPARE_WITH_TIMEOUT(save->state(), EchoPluginOperation::Succeeded, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(assets->isDrained(), 15000);
    QQuickWindow window;
    QString error;
    auto item = imageDropItem(fixture, window, error);
    QVERIFY2(item, qPrintable(error));
    QVERIFY(item->setProperty("maxCount", 0));
    QVERIFY(item->setProperty("archiveJsonPaths", QStringList{"meta.json", "pool.json"}));
    QSignalSpy archives(item.get(), SIGNAL(archiveDropped(EchoMusic::EchoPluginAssetArchive *)));
    QSignalSpy images(item.get(), SIGNAL(importStarted(EchoMusic::EchoPluginOperation *)));
    QVERIFY(archives.isValid() && images.isValid());
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(source), QUrl::fromLocalFile(zip)});
    QDragEnterEvent enter(QPoint(30, 30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(item.get(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(30, 30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(item.get(), &drop);
    QVERIFY(drop.isAccepted());
    // The first ZIP takes precedence over the earlier image.
    QCOMPARE(images.count(), 0);
    QCOMPARE(archives.count(), 1);
    QPointer<EchoPluginAssetArchive> lease =
        qvariant_cast<EchoPluginAssetArchive *>(archives.first().first());
    QVERIFY(lease);
    QVERIFY(!fixture.picker());
    if (!retire.isEmpty()) {
        if (retire == "hide") {
            QCOMPARE(item->property("mirroredBusy").toBool(), true);
            item->setVisible(false);
            QCOMPARE(item->property("mirroredBusy").toBool(), false);
        } else
            QVERIFY(
                QMetaObject::invokeMethod(&fixture.host.api, "sessionChanged", Q_ARG(quint64, 1)));
        QTRY_VERIFY_WITH_TIMEOUT(!lease || !lease->active(), 15000);
    } else {
        QTRY_VERIFY_WITH_TIMEOUT(lease->ready() || !lease->errorString().isEmpty(), 15000);
        QVERIFY2(lease->ready(), qPrintable(lease->errorString()));
        QCOMPARE(lease->jsonFiles().value("meta.json").toMap().value("kind").toString(),
                 "folia-tempera-pool");
        auto *restore = fixture.retain(lease->importInto(
            "restored-drop",
            {QVariantMap{{"path", "images/source.png"}, {"name", "restored.png"}}}));
        QTRY_COMPARE_WITH_TIMEOUT(restore->state(), EchoPluginOperation::Succeeded, 15000);
        QCOMPARE(restore->result().toMap().value("assets").toList().size(), 1);
    }
    QTRY_VERIFY_WITH_TIMEOUT(assets->isDrained(), 15000);
    QCOMPARE(EchoPluginAssets::archiveSlotCount(), 0);
    QCOMPARE(EchoPluginAssets::archiveReservedBytes(), 0);
    auto *list = fixture.retain(assets->list("restored-drop"));
    QTRY_COMPARE(list->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(list->result().toMap().value("assets").toList().size(), retire.isEmpty() ? 1 : 0);
}

void PluginAssetsTest::cancelPickerAndRetireWithoutLatePublication()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    auto *operation = fixture.retain(fixture.scope->assets()->importImages("cappella.emojis"));
    QTRY_VERIFY(fixture.picker());
    operation->cancel();
    QTRY_VERIFY(!fixture.picker());
    QCOMPARE(operation->state(), EchoPluginOperation::Canceled);
    auto *pending = fixture.retain(fixture.scope->assets()->importImages("cappella.emojis"));
    QTRY_VERIFY(fixture.picker());
    QPointer<EchoPluginContext> retired = fixture.scope;
    QVERIFY(fixture.host.runtime.setEnabled(fixture.host.installation(), false));
    QTRY_VERIFY(retired.isNull());
    QCOMPARE(pending->state(), EchoPluginOperation::Canceled);
    QVERIFY(!fixture.picker());
}

void PluginAssetsTest::originalAnimatedGifAndFirstFrameSampling()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    const auto bytes = QByteArray::fromHex("47494638396101000100800000ff00000000ff"
                                           "21ff0b4e45545343415045322e300301000000"
                                           "21f904000a0000002c0000000001000100000202440100"
                                           "21f90400140000002c00000000010001000002024c01003b");
    const auto path = fixture.host.temporary.path() + "/animated.gif";
    write(path, bytes);
    auto *operation = fixture.retain(fixture.scope->assets()->importImages("cappella.emojis"));
    QTRY_VERIFY(fixture.picker());
    fixture.picker()->selectFile(path);
    QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(operation->state(), EchoPluginOperation::Succeeded, 15000);
    const auto asset = operation->result().toMap().value("assets").toList().first().toMap();
    QCOMPARE(asset.value("animated").toBool(), true);
    QCOMPARE(asset.value("frameCount").toInt(), 2);
    QCOMPARE(asset.value("durationMs").toInt(), 300);
    QFile saved(asset.value("url").toUrl().toLocalFile());
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), bytes);
    QMovie movie(saved.fileName());
    movie.setCacheMode(QMovie::CacheAll);
    QVERIFY(movie.jumpToFrame(0));
    QCOMPARE(movie.currentImage().pixelColor(0, 0), QColor(Qt::red));
    QVERIFY(movie.jumpToFrame(1));
    QCOMPARE(movie.currentImage().pixelColor(0, 0), QColor(Qt::blue));
    auto *sample = fixture.retain(
        fixture.scope->assets()->sampleImage(QUrl(asset.value("handle").toString()), 1, 1));
    QTRY_COMPARE_WITH_TIMEOUT(sample->state(), EchoPluginOperation::Succeeded, 15000);
    QCOMPARE(sample->result().toMap().value("pixels").toList(), QVariantList({255, 0, 0, 255}));
}

void PluginAssetsTest::persistentHandlesSurviveActivation()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    QImage image(1, 1, QImage::Format_RGBA8888);
    image.fill(Qt::green);
    const auto path = fixture.host.temporary.path() + "/persistent.png";
    QVERIFY(image.save(path));
    auto *operation = fixture.retain(fixture.scope->assets()->importImages("persistent"));
    QTRY_VERIFY(fixture.picker());
    fixture.picker()->selectFile(path);
    QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(operation->state(), EchoPluginOperation::Succeeded, 15000);
    const auto asset = operation->result().toMap().value("assets").toList().first().toMap();
    QPointer<EchoPluginContext> previous = fixture.scope;
    const auto installation = fixture.host.installation();
    QVERIFY(fixture.host.runtime.setEnabled(installation, false));
    QTRY_VERIFY(previous.isNull());
    QVERIFY(QFileInfo::exists(asset.value("url").toUrl().toLocalFile()));
    QVERIFY(fixture.host.runtime.setEnabled(installation, true));
    QTRY_COMPARE(fixture.host.runtime.state(installation), PluginRuntime::Active);
    fixture.scope = fixture.host.runtime.context(installation);
    // Resolve a persisted handle before list() has loaded this activation's store.
    auto *sample = fixture.retain(
        fixture.scope->assets()->sampleImage(QUrl(asset.value("handle").toString()), 1, 1));
    QTRY_COMPARE_WITH_TIMEOUT(sample->state(), EchoPluginOperation::Succeeded, 15000);
    QCOMPARE(sample->result().toMap().value("pixels").toList(), QVariantList({0, 255, 0, 255}));
}

void PluginAssetsTest::fontsAreReleasedAfterLastScopeReference()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    const auto installation = fixture.host.installation();
    fixture.host.runtime.importDirectory(pluginPackage(fixture.host.temporary.path() + "/observer",
                                                       "test.font-observer",
                                                       "export function activate(ctx) {}"));
    QTRY_VERIFY(!fixture.host.runtime.installationForId("test.font-observer").isEmpty());
    const auto observerId = fixture.host.runtime.installationForId("test.font-observer");
    QTRY_COMPARE(fixture.host.runtime.state(observerId), PluginRuntime::Active);
    fixture.scope = fixture.host.runtime.context(installation);
    QVERIFY(fixture.scope);
    auto *observer = fixture.host.runtime.context(observerId);
    QVERIFY(observer);
    QSignalSpy familyChanges(fixture.scope->fonts(), &EchoPluginFonts::familiesChanged);
    QSignalSpy observerChanges(observer->fonts(), &EchoPluginFonts::familiesChanged);
    QString path;
#ifdef Q_OS_MACOS
    path = "/System/Library/Fonts/Supplemental/Arial.ttf";
#else
    QSKIP("Set up an approved local OpenType fixture for this platform");
#endif
    QVERIFY(QFileInfo::exists(path));
    auto *operation = fixture.retain(fixture.scope->assets()->importFonts("fonts"));
    QTRY_VERIFY(fixture.picker());
    fixture.picker()->selectFile(path);
    QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(operation->state(), EchoPluginOperation::Succeeded, 15000);
    QTRY_VERIFY(familyChanges.count() > 0);
    QTRY_VERIFY(observerChanges.count() > 0);
    familyChanges.clear();
    observerChanges.clear();
    const auto asset = operation->result().toMap().value("assets").toList().first().toMap();
    QVERIFY(!asset.value("families").toStringList().isEmpty());
    auto owner = std::make_unique<QObject>();
    owner->setProperty("active", true);
    QPointer<EchoPluginContext> presentation =
        fixture.scope->createPresentation(owner.get(), "font-test");
    QVERIFY(presentation);
    auto *list = fixture.retain(presentation->assets()->list("fonts"));
    QTRY_COMPARE(list->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(list->result().toMap().value("assets").toList().size(), 1);
    QSignalSpy changes(qGuiApp, &QGuiApplication::fontDatabaseChanged);
    auto *clear = fixture.retain(fixture.scope->assets()->clear("fonts"));
    QTRY_COMPARE(clear->state(), EchoPluginOperation::Succeeded);
    QVERIFY(!QFileInfo::exists(asset.value("url").toUrl().toLocalFile()));
    QCOMPARE(changes.count(), 0); // In-use registration survives removal from the asset catalog.
    owner.reset();
    QTRY_VERIFY(presentation.isNull());
    QCOMPARE(changes.count(), 0); // Activation still retains its own font reference.
    QVERIFY(fixture.host.runtime.setEnabled(installation, false));
    QTRY_VERIFY(changes.count() > 0);
    QTRY_VERIFY(observerChanges.count() > 0);
    QCOMPARE(familyChanges.count(), 0); // Retired scope does not receive font DB notifications.
}

void PluginAssetsTest::packagePixelsAndForeignSymlink()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate([](const QString &source) {
        QImage image(1, 1, QImage::Format_RGBA8888);
        image.fill(QColor(12, 34, 56, 78));
        if (!image.save(source + "/owned.png"))
            qFatal("Cannot write owned image fixture");
    }));
    auto *assets = fixture.scope->assets();
    const auto url = QUrl::fromLocalFile(fixture.scope->package().resolve("owned.png"));
    auto *operation = fixture.retain(assets->sampleImage(url, 256, 256));
    QTRY_COMPARE_WITH_TIMEOUT(operation->state(), EchoPluginOperation::Succeeded, 15000);
    const auto result = operation->result().toMap();
    QCOMPARE(result.value("width").toInt(), 256);
    QCOMPARE(result.value("height").toInt(), 256);
    QCOMPARE(result.value("pixels").toList().size(), 256 * 256 * 4);
    QCOMPARE(result.value("pixels").toList().first().toInt(), 12);
    const auto outside = fixture.host.temporary.path() + "/outside.png";
    QImage image(1, 1, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    QVERIFY(image.save(outside));
    const auto link = fixture.scope->package().root + "/foreign-link.png";
    QVERIFY(QFile::link(outside, link));
    auto *foreign = fixture.retain(assets->sampleImage(QUrl::fromLocalFile(link)));
    QTRY_COMPARE(foreign->state(), EchoPluginOperation::Failed);
    QCOMPARE(foreign->errorCode(), "SourceDenied");
}

void PluginAssetsTest::byteBudgetAndTruncatedAnimationFailWithoutPublication()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    const auto path = fixture.host.temporary.path() + "/oversized.png";
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(8 * 1024 * 1024 + 1));
    file.close();
    auto *assets = fixture.scope->assets();
    auto *large = fixture.retain(assets->importImages("budget"));
    QTRY_VERIFY(fixture.picker());
    fixture.picker()->selectFile(path);
    QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(large->state(), EchoPluginOperation::Failed, 15000);
    QCOMPARE(large->errorCode(), "QuotaExceeded");
    const auto broken = fixture.host.temporary.path() + "/truncated.gif";
    write(broken, QByteArray::fromHex("47494638396101000100800000ff00000000ff"
                                      "21f904000a0000002c0000000001000100000202440100"
                                      "21f90400140000002c00000000010001000002024c"));
    auto *animation = fixture.retain(assets->importImages("budget"));
    QTRY_VERIFY(fixture.picker());
    fixture.picker()->selectFile(broken);
    QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(animation->state(), EchoPluginOperation::Failed, 15000);
    QCOMPARE(animation->errorCode(), "AssetDecodeError");
    auto *list = fixture.retain(assets->list("budget"));
    QTRY_COMPARE(list->state(), EchoPluginOperation::Succeeded);
    QVERIFY(list->result().toMap().value("assets").toList().isEmpty());
}

void PluginAssetsTest::cancelAcceptedImportBeforePublication_data()
{
    QTest::addColumn<QString>("mode");
    QTest::newRow("legacy-cancel") << QString("legacy");
    QTest::newRow("partial-cancel") << QString("partial");
    QTest::newRow("partial-account-epoch") << QString("account");
}

void PluginAssetsTest::cancelAcceptedImportBeforePublication()
{
    QFETCH(QString, mode);
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    QImage image(1, 1, QImage::Format_RGBA8888);
    image.fill(Qt::blue);
    const auto path = fixture.host.temporary.path() + "/canceled.png";
    QVERIFY(image.save(path));
    auto *assets = fixture.scope->assets();
    auto *operation =
        fixture.retain(mode == "legacy" ? assets->importImages("canceled")
                                        : assets->importImages("canceled", {{"partial", true}}));
    QTRY_VERIFY(fixture.picker());
    fixture.picker()->selectFile(path);
    QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
    if (mode == "account") {
        const auto previous = fixture.scope->accountEpoch();
        QVERIFY(QMetaObject::invokeMethod(&fixture.host.api, "sessionChanged", Q_ARG(quint64, 1)));
        QVERIFY(fixture.scope->accountEpoch() > previous);
    } else {
        operation->cancel();
    }
    QTRY_VERIFY_WITH_TIMEOUT(assets->isDrained(), 15000);
    QCOMPARE(operation->state(), EchoPluginOperation::Canceled);
    auto *list = fixture.retain(assets->list("canceled"));
    QTRY_COMPARE(list->state(), EchoPluginOperation::Succeeded);
    QVERIFY(list->result().toMap().value("assets").toList().isEmpty());
    const QDir directory(fixture.scope->dataPath() + "/assets-v1");
    QVERIFY(directory
                .entryList({"*.png", ".staging-*"},
                           QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
                .isEmpty());
}

void PluginAssetsTest::realCurrentCoverNetworkAndSourceRevisionCancellation()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    CoverServer server;
    QVERIFY(server.server.isListening());
    const auto cover = server.url();
    fixture.host.playback.setVolume(0);
    fixture.host.playback.playTracks({coverTrack(fixture, "cover-a", cover)}, 0, "Assets",
                                     "assets");
    QTRY_VERIFY_WITH_TIMEOUT(fixture.host.playback.playing() && !fixture.host.playback.loading(),
                             15000);
    QCOMPARE(fixture.scope->player()->coverUrl(), cover);
    auto *assets = fixture.scope->assets();
    auto *sample = fixture.retain(assets->sampleImage(cover, 1, 1));
    QTRY_COMPARE_WITH_TIMEOUT(sample->state(), EchoPluginOperation::Succeeded, 15000);
    QCOMPARE(sample->result().toMap().value("pixels").toList(), QVariantList({24, 48, 72, 255}));
    server.hold = true;
    auto *pending = fixture.retain(assets->sampleImage(cover));
    auto *second = fixture.retain(assets->list("other"));
    auto *busy = fixture.retain(assets->list("third"));
    QCOMPARE(busy->state(), EchoPluginOperation::Failed);
    QCOMPARE(busy->errorCode(), "Busy");
    QTRY_COMPARE(second->state(), EchoPluginOperation::Succeeded);
    QTRY_VERIFY(!server.waiting.isEmpty());
    QCOMPARE(pending->state(), EchoPluginOperation::Running);
    QSignalSpy completed(pending, &EchoPluginOperation::completed);
    const auto revision = fixture.host.playback.sourceRevision();
    // Same URL, different source revision: URL equality alone is insufficient.
    fixture.host.playback.playTracks({coverTrack(fixture, "cover-b", cover)}, 0, "Assets",
                                     "assets");
    QTRY_VERIFY(fixture.host.playback.sourceRevision() != revision);
    QTRY_COMPARE(pending->state(), EchoPluginOperation::Canceled);
    QTRY_VERIFY_WITH_TIMEOUT(assets->isDrained(), 3000);
    for (const auto &socket : server.waiting)
        if (socket)
            server.respond(socket);
    QTest::qWait(100);
    QCOMPARE(completed.count(), 1);
    QVERIFY(!pending->result().isValid());
}

void PluginAssetsTest::tamperedPersistentFontIsRejectedBeforeRegistration()
{
#ifndef Q_OS_MACOS
    QSKIP("Set up an approved local OpenType fixture for this platform");
#else
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    QFile original("/System/Library/Fonts/Supplemental/Arial.ttf");
    QVERIFY(original.open(QIODevice::ReadOnly));
    const auto selected = fixture.host.temporary.path() + "/padded.ttf";
    write(selected, original.readAll() + QByteArray(32, '\0'));
    auto *operation = fixture.retain(fixture.scope->assets()->importFonts("integrity"));
    QTRY_VERIFY(fixture.picker());
    fixture.picker()->selectFile(selected);
    QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(operation->state(), EchoPluginOperation::Succeeded, 15000);
    const auto asset = operation->result().toMap().value("assets").toList().first().toMap();
    const auto installation = fixture.host.installation();
    QPointer<EchoPluginContext> retired = fixture.scope;
    QVERIFY(fixture.host.runtime.setEnabled(installation, false));
    QTRY_VERIFY(retired.isNull());
    QFile modified(asset.value("url").toUrl().toLocalFile());
    QVERIFY(modified.open(QIODevice::ReadWrite));
    QVERIFY(modified.seek(modified.size() - 1));
    QCOMPARE(modified.write("x", 1), 1);
    modified.close();
    QVERIFY(fixture.host.runtime.setEnabled(installation, true));
    QTRY_COMPARE(fixture.host.runtime.state(installation), PluginRuntime::Active);
    fixture.scope = fixture.host.runtime.context(installation);
    auto *list = fixture.retain(fixture.scope->assets()->list("integrity"));
    QTRY_VERIFY_WITH_TIMEOUT(list->finished(), 15000);
    QCOMPARE(list->state(), EchoPluginOperation::Failed);
    QCOMPARE(list->errorCode(), "AssetStorageError");
#endif
}

void PluginAssetsTest::trickleCoverHasAbsoluteNetworkDeadline()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    CoverServer server;
    QVERIFY(server.server.isListening());
    const auto cover = server.url();
    fixture.host.playback.setVolume(0);
    fixture.host.playback.playTracks({coverTrack(fixture, "trickle", cover)}, 0, "Assets",
                                     "assets");
    QTRY_VERIFY_WITH_TIMEOUT(fixture.host.playback.playing() && !fixture.host.playback.loading(),
                             15000);
    server.trickle = true;
    auto *operation = fixture.retain(fixture.scope->assets()->sampleImage(cover));
    QElapsedTimer elapsed;
    elapsed.start();
    while (!operation->finished() && elapsed.elapsed() < 16500)
        QTest::qWait(20);
    QCOMPARE(operation->state(), EchoPluginOperation::Failed);
    QCOMPARE(operation->errorCode(), "Timeout");
    QVERIFY(elapsed.elapsed() < 17000);
    QTRY_VERIFY(fixture.scope->assets()->isDrained());
}

void PluginAssetsTest::batchFontStagingNeverExceedsInstallationBudget()
{
#ifndef Q_OS_MACOS
    QSKIP("Set up an approved local OpenType fixture for this platform");
#else
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    QFile original("/System/Library/Fonts/Supplemental/Arial.ttf");
    QVERIFY(original.open(QIODevice::ReadOnly));
    auto bytes = original.readAll();
    bytes.resize(16 * 1024 * 1024);
    QStringList names;
    for (int i = 0; i < 5; ++i) {
        const auto name = QString("budget%1.ttf").arg(i);
        names.append('"' + name + '"');
        write(fixture.host.temporary.path() + '/' + name, bytes);
    }
    auto *assets = fixture.scope->assets();
    auto *operation = fixture.retain(assets->importFonts("batch"));
    QTRY_VERIFY(fixture.picker());
    fixture.picker()->setDirectory(fixture.host.temporary.path());
    auto *fileName = fixture.picker()->findChild<QLineEdit *>("fileNameEdit");
    QVERIFY(fileName);
    fileName->setText(names.join(' '));
    qint64 maximum = 0;
    QTimer monitor;
    connect(&monitor, &QTimer::timeout, this, [&] {
        qint64 total = 0;
        QDirIterator files(fixture.scope->dataPath() + "/assets-v1", QDir::Files | QDir::Hidden,
                           QDirIterator::Subdirectories);
        while (files.hasNext()) {
            files.next();
            total += files.fileInfo().size();
        }
        maximum = qMax(maximum, total);
    });
    monitor.start(1);
    QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(operation->finished(), 15000);
    monitor.stop();
    QCOMPARE(operation->state(), EchoPluginOperation::Failed);
    QCOMPARE(operation->errorCode(), "QuotaExceeded");
    QVERIFY2(maximum <= 64 * 1024 * 1024, qPrintable(QString::number(maximum)));
#endif
}

void PluginAssetsTest::publicationNotifiesAllActiveScopesAfterCleanup()
{
    AssetsFixture fixture;
    QVERIFY(fixture.activate());
    auto owner = std::make_unique<QObject>();
    owner->setProperty("active", true);
    QPointer<EchoPluginContext> presentation =
        fixture.scope->createPresentation(owner.get(), "assets-settings");
    QVERIFY(presentation);
    auto *activationAssets = fixture.scope->assets();
    QSignalSpy activationChanged(activationAssets, &EchoPluginAssets::changed);
    QSignalSpy presentationChanged(presentation->assets(), &EchoPluginAssets::changed);
    bool cleanupPrecededSignal = true;
    connect(activationAssets, &EchoPluginAssets::changed, this, [&] {
        cleanupPrecededSignal = cleanupPrecededSignal && activationAssets->isDrained();
    });
    QImage image(1, 1, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    const auto path = fixture.host.temporary.path() + "/broadcast.png";
    QVERIFY(image.save(path));
    auto *operation = fixture.retain(presentation->assets()->importImages("broadcast"));
    QTRY_VERIFY(fixture.picker());
    fixture.picker()->selectFile(path);
    QVERIFY(QMetaObject::invokeMethod(fixture.picker(), "accept", Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(operation->state(), EchoPluginOperation::Succeeded, 15000);
    QTRY_COMPARE(activationChanged.count(), 1);
    QTRY_COMPARE(presentationChanged.count(), 1);
    const auto handle = operation->result()
                            .toMap()
                            .value("assets")
                            .toList()
                            .first()
                            .toMap()
                            .value("handle")
                            .toString();
    auto *list = fixture.retain(activationAssets->list("broadcast"));
    QTRY_COMPARE(list->state(), EchoPluginOperation::Succeeded);
    QTest::qWait(20);
    QCOMPARE(activationChanged.count(), 1);
    QCOMPARE(presentationChanged.count(), 1);
    auto *remove = fixture.retain(activationAssets->remove(handle));
    connect(remove, &EchoPluginOperation::completed, this, [&] { owner.reset(); });
    QTRY_COMPARE(remove->state(), EchoPluginOperation::Succeeded);
    QTRY_VERIFY(presentation.isNull());
    QTRY_COMPARE(activationChanged.count(), 2);
    QCOMPARE(presentationChanged.count(), 1); // Retired before the queued broadcast.
    QVERIFY(cleanupPrecededSignal);
}

QTEST_MAIN(PluginAssetsTest)
#include "PluginAssetsTest.moc"
