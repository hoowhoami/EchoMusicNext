#include "EchoPluginAssets.h"
#include "EchoPluginContext.h"
#include "PlaybackService.h"
#include "AccountService.h"
#include "ApiClient.h"
#include "asset-archive/CaseFold.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include "EchoFileDialog.h"
#include "StorageFence.h"
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QMutex>
#include <QMutexLocker>
#include <QMimeDatabase>
#include <QtConcurrentRun>
#include <algorithm>
#include <utility>
#include <cmath>

namespace EchoMusic
{
namespace
{
constexpr qint64 MiB = 1024 * 1024;
enum Command
{
    List,
    Remove,
    Clear,
    ImportImages,
    ImportFonts,
    Sample,
    Frames,
    InspectArchive,
    ImportArchive,
    ExportArchive
};
bool collectionValid(const QString &value)
{
    static const QRegularExpression pattern("^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,63}$");
    return pattern.match(value).hasMatch();
}
bool handleValid(const QString &value)
{
    static const QRegularExpression pattern("^asset:[a-f0-9]{32}$");
    return pattern.match(value).hasMatch();
}
QString assetFile(const QVariantMap &asset)
{
    return asset.value("handle").toString().mid(6) + '.' + asset.value("extension").toString();
}
QString renderFile(const QVariantMap &asset)
{
    return asset.value("handle").toString().mid(6) + ".preview.png";
}
QString publicFile(const QVariantMap &asset)
{
    return asset.value("renderBytes").toLongLong() > 0 ? renderFile(asset) : assetFile(asset);
}
qint64 storedBytes(const QVariantMap &asset)
{
    return asset.value("bytes").toLongLong() + asset.value("renderBytes").toLongLong();
}
QMutex archiveBudgetMutex;
int archiveSlots = 0;
QString workerExecutable()
{
#ifdef Q_OS_WIN
    const QString name = "echo-plugin-asset-worker.exe";
#else
    const QString name = "echo-plugin-asset-worker";
#endif
    const QDir app(QCoreApplication::applicationDirPath());
    const QStringList candidates{app.filePath("../Helpers/" + name), app.filePath(name),
                                 QString::fromUtf8(ECHO_PLUGIN_ASSET_WORKER_PATH)};
    for (const auto &candidate : candidates)
        if (QFileInfo(candidate).isFile() && QFileInfo(candidate).isExecutable())
            return candidate;
    return {};
}
} // namespace

struct EchoPluginAssetStore
{
    struct Font
    {
        int id = -1, references = 0;
    };
    QString directory;
    QVariantList assets;
    QHash<QString, Font> fonts;
    QList<QPointer<EchoPluginAssets>> observers;
    bool loaded = false, picker = false;
    int pending = 0;
    int animationEntries = 0;
    qint64 animationBytes = 0;
    std::weak_ptr<EchoPluginArchiveSession> archive;
};
struct EchoPluginArchiveSession
{
    std::shared_ptr<QTemporaryDir> directory;
    std::shared_ptr<std::atomic_bool> canceled = std::make_shared<std::atomic_bool>(false);
    QPointer<EchoPluginAssetArchive> lease;
    QVariantList entries;
    QVariantMap json;
    QList<std::function<void()>> afterCleanup;
    QString source;
    quint64 epoch = 0;
    bool closing = false, cleanupStarted = false, pendingHeld = false, importing = false;
};
struct EchoPluginAnimationCache
{
    QString key, fit;
    QUrl source;
    int width = 0, height = 0, frameIndex = 0, loopCount = -1;
    qint64 encodedBytes = 0, position = 0, duration = 0;
    QList<qint64> ends;
    QList<QPointer<EchoPluginAssetAnimation>> leases;
    QPointer<EchoPluginOperation> operation;
    QPointer<QTimer> timer;
    QElapsedTimer clock;
    QMetaObject::Connection sourceConnection;
    std::shared_ptr<QTemporaryDir> directory;
    QString error;
    bool started = false, ready = false, running = false, finished = false;
};
struct EchoPluginAssetJob
{
    int command = List, width = 64, height = 64;
    qint64 renderLimit = 64 * MiB;
    QString value, source, expectedHash, error, code, fit = "contain";
    QStringList files;
    QByteArray bytes;
    QVariantList loaded, added;
    QVariantList rejected;
    bool partial = false;
    int maxCount = 16;
    QHash<QString, QByteArray> fonts;
    QVariantMap sample;
    QVariantList selections;
    QVariantMap archiveJson;
    QStringList jsonPaths, displayNames;
    std::shared_ptr<EchoPluginArchiveSession> archive;
    std::shared_ptr<QSaveFile> save;
    QElapsedTimer archiveClock;
    QPointer<EchoPluginOperation> operation;
    QPointer<QNetworkReply> reply;
    QPointer<EchoFileDialog> dialog;
    QPointer<QTimer> deadline;
    quint64 epoch = 0, revision = 0;
    QString transitionEpoch;
    QUrl cover;
    bool trackScoped = false, running = false, countPending = true;
    std::shared_ptr<std::atomic_bool> canceled = std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<QTemporaryDir> staging;
    std::shared_ptr<StorageFence> storageFence, destinationFence;
    std::weak_ptr<EchoPluginAnimationCache> animation;
};

namespace
{
bool imageOptions(const QVariantMap &options, bool &partial, int &maxCount)
{
    if (options.size() > 2)
        return false;
    for (auto it = options.begin(); it != options.end(); ++it) {
        if (it.key() == "partial") {
            if (it->metaType().id() != QMetaType::Bool)
                return false;
            partial = it->toBool();
        } else if (it.key() == "maxCount") {
            const auto type = it->metaType().id();
            const auto n = it->toDouble();
            if ((type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::LongLong &&
                 type != QMetaType::ULongLong && type != QMetaType::Double) ||
                !std::isfinite(n) || n != std::floor(n) || n < 1 || n > 16)
                return false;
            maxCount = int(n);
        } else {
            return false;
        }
    }
    return true;
}
bool fail(EchoPluginAssetJob &job, const QString &code, const QString &message)
{
    job.code = code;
    job.error = message;
    return false;
}
bool prepareDirectory(const QString &directory, EchoPluginAssetJob &job)
{
    const QFileInfo parent(QFileInfo(directory).absolutePath());
    if (parent.isSymLink() || QFileInfo(directory).isSymLink() || !QDir().mkpath(directory))
        return fail(job, "AssetStorageError", "Cannot create installation asset directory");
    return true;
}
bool verifyDigest(const QString &path, qint64 expected, const QString &digest,
                  EchoPluginAssetJob &job)
{
    QFile file(path);
    if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() != expected)
        return fail(job, "AssetStorageError", "Managed asset file changed");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 read = 0;
    while (!file.atEnd()) {
        if (job.canceled->load() || (job.archive && job.archiveClock.hasExpired(60000)))
            return false;
        const auto bytes = file.read(65536);
        read += bytes.size();
        if (bytes.isEmpty() || read > expected)
            return fail(job, "AssetStorageError", "Managed asset file changed during verification");
        hash.addData(bytes);
    }
    return read == expected && QString::fromLatin1(hash.result().toHex()) == digest
               ? true
               : fail(job, "AssetStorageError",
                      "Managed asset content failed integrity verification");
}
bool readIndex(const QString &directory, EchoPluginAssetJob &job)
{
    if (!prepareDirectory(directory, job))
        return false;
    QFile file(QDir(directory).filePath("index.json"));
    if (!file.exists())
        return true;
    if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 65536)
        return fail(job, "AssetStorageError", "Invalid installation asset index");
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto object = document.object();
    if (parse.error != QJsonParseError::NoError || object.value("version").toInt() != 1 ||
        !object.value("assets").isArray())
        return fail(job, "AssetStorageError", "Invalid installation asset index");
    qint64 total = 0;
    int fontCount = 0;
    QSet<QString> handles;
    for (const auto &item : object.value("assets").toArray()) {
        if (job.canceled->load() || (job.archive && job.archiveClock.hasExpired(60000)))
            return false;
        auto asset = item.toObject().toVariantMap();
        const auto handle = asset.value("handle").toString(), kind = asset.value("kind").toString();
        const auto extension = asset.value("extension").toString();
        const bool font = kind == "font";
        const bool svg = !font && extension == "svg";
        const auto bytes = asset.value("bytes").toLongLong();
        static const QRegularExpression digest("^[a-f0-9]{64}$");
        const auto path = QDir(directory).filePath(assetFile(asset));
        const QFileInfo info(path);
        if (!handleValid(handle) || handles.contains(handle) ||
            !collectionValid(asset.value("collection").toString()) || (!font && kind != "image") ||
            !(font ? QStringList{"ttf", "otf", "ttc"}
                   : QStringList{"png", "jpg", "gif", "webp", "svg"})
                 .contains(extension) ||
            !digest.match(asset.value("sha256").toString()).hasMatch() || bytes <= 0 ||
            bytes > (font ? 16 : 8) * MiB || !info.isFile() || info.isSymLink() ||
            info.size() != bytes ||
            info.canonicalPath() != QFileInfo(directory).canonicalFilePath())
            return fail(job, "AssetStorageError", "Invalid managed asset entry");
        handles.insert(handle);
        if (svg) {
            const auto renderedBytes = asset.value("renderBytes").toLongLong();
            const auto renderedDigest = asset.value("renderSha256").toString();
            const auto renderedPath = QDir(directory).filePath(renderFile(asset));
            const QFileInfo rendered(renderedPath);
            if (renderedBytes <= 0 || renderedBytes > 64 * MiB ||
                !digest.match(renderedDigest).hasMatch() || !rendered.isFile() ||
                rendered.isSymLink() ||
                rendered.canonicalPath() != QFileInfo(directory).canonicalFilePath() ||
                !verifyDigest(path, bytes, asset.value("sha256").toString(), job) ||
                !verifyDigest(renderedPath, renderedBytes, renderedDigest, job))
                return fail(job, "AssetStorageError", "Invalid SVG original or PNG derivative");
        } else if (asset.value("renderBytes").toLongLong() != 0 || asset.contains("renderSha256"))
            return fail(job, "AssetStorageError", "Unexpected managed derivative");
        total += storedBytes(asset);
        fontCount += font;
        if (handles.size() > 128 || total > 64 * MiB || fontCount > 16)
            return fail(job, "QuotaExceeded", "Installation asset budget exceeded");
        if (font) {
            QFile fontFile(path);
            if (!fontFile.open(QIODevice::ReadOnly))
                return fail(job, "AssetStorageError", "Cannot read imported font");
            const auto data = fontFile.read(16 * MiB + 1);
            if (data.size() != bytes ||
                QString::fromLatin1(
                    QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()) !=
                    asset.value("sha256").toString())
                return fail(job, "AssetStorageError",
                            "Imported font failed integrity verification");
            job.fonts.insert(handle, data);
        }
        job.loaded.append(asset);
    }
    return true;
}
bool copySnapshot(const QString &source, const QString &destination, qint64 limit,
                  EchoPluginAssetJob &job)
{
    const QFileInfo info(source);
    if (!info.isFile() || info.isSymLink() || info.size() <= 0 || info.size() > limit)
        return fail(job, "QuotaExceeded", "Selected file is invalid or exceeds its byte budget");
    QFile input(source), output(destination);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail(job, "AssetStorageError", "Cannot snapshot selected asset");
    qint64 total = 0;
    while (!input.atEnd()) {
        if (job.canceled->load() || (job.archive && job.archiveClock.hasExpired(60000)))
            return false;
        const auto chunk = input.read(65536);
        if (chunk.isEmpty() && input.error() != QFileDevice::NoError)
            return fail(job, "AssetStorageError", "Cannot read selected asset");
        total += chunk.size();
        if (total > limit)
            return fail(job, "QuotaExceeded", "Selected file exceeds its byte budget");
        if (output.write(chunk) != chunk.size())
            return fail(job, "AssetStorageError", "Cannot snapshot selected asset");
    }
    return total > 0 || fail(job, "AssetStorageError", "Selected asset became empty during copy");
}
QVariantMap decode(const QString &source, const QString &mode, const QString &output,
                   EchoPluginAssetJob &job)
{
    const auto executable = workerExecutable();
    if (executable.isEmpty()) {
        fail(job, "Unavailable", "Asset decoder helper is unavailable");
        return {};
    }
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_IMAGEIO_MAXALLOC", "128");
    environment.insert("QT_QPA_PLATFORM", "offscreen");
    process.setProcessEnvironment(environment);
    process.start(executable,
                  {mode, source, output,
                   QString::number(mode == "image" ? job.renderLimit : job.width),
                   QString::number(job.height)},
                  QIODevice::ReadOnly);
    QElapsedTimer deadline;
    deadline.start();
    const bool started = process.waitForStarted(3000);
    qint64 diagnosticBytes = 0;
    while (started && process.state() != QProcess::NotRunning && !job.canceled->load() &&
           deadline.elapsed() < 15000 && (!job.archive || !job.archiveClock.hasExpired(60000))) {
        process.waitForFinished(50);
        diagnosticBytes += process.readAllStandardError().size();
        if (process.bytesAvailable() > 65536 || diagnosticBytes > 65536)
            break;
    }
    if (!started || process.state() != QProcess::NotRunning || job.canceled->load()) {
        process.kill();
        process.waitForFinished(3000);
        if (!job.canceled->load())
            fail(job, "AssetDecodeError", "Asset decoder timed out or failed to start");
        return {};
    }
    const auto bytes = process.readAllStandardOutput();
    QJsonParseError parse;
    const auto result = QJsonDocument::fromJson(bytes, &parse).object();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() ||
        bytes.size() > 65536 || parse.error != QJsonParseError::NoError ||
        result.value("version").toInt() != 1 || !result.value("error").toString().isEmpty()) {
        fail(job, "AssetDecodeError",
             result.value("error").toString().isEmpty() ? "Asset decoder failed"
                                                        : result.value("error").toString());
        return {};
    }
    return result.toVariantMap();
}
bool archivePath(const QString &path)
{
    if (path.isEmpty() || path.size() > 2048 || path.toUtf8().size() > 2048 ||
        path.startsWith('/') || path.contains('\\') || path.contains(':'))
        return false;
    for (qsizetype i = 0; i < path.size(); ++i) {
        const auto character = path[i];
        if (character.category() == QChar::Other_Control || character.isLowSurrogate())
            return false;
        if (character.isHighSurrogate() && (++i >= path.size() || !path[i].isLowSurrogate()))
            return false;
    }
    for (const auto &component : path.split('/'))
        if (component.isEmpty() || component == "." || component == "..")
            return false;
    return true;
}
QString archiveKey(const QString &path)
{
    return AssetArchiveCaseFold::key(path);
}
bool jsonDepth(const QVariant &value, int depth = 0, qint64 *units = nullptr)
{
    qint64 initial = 0;
    if (!units)
        units = &initial;
    if (depth > 64 || ++*units > 65536)
        return false;
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        if (map.size() > 65536)
            return false;
        for (auto iterator = map.begin(); iterator != map.end(); ++iterator) {
            *units += iterator.key().size();
            if (*units > 65536 || !jsonDepth(iterator.value(), depth + 1, units))
                return false;
        }
        return true;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        const auto list = value.toList();
        if (list.size() > 65536)
            return false;
        for (const auto &item : list)
            if (!jsonDepth(item, depth + 1, units))
                return false;
        return true;
    }
    const auto type = value.metaType().id();
    if (type == QMetaType::QString) {
        *units += value.toString().size();
        return *units <= 65536;
    }
    return !value.isValid() || value.isNull() || type == QMetaType::Bool ||
           type == QMetaType::QString ||
           ((type == QMetaType::Double || type == QMetaType::Int || type == QMetaType::UInt ||
             type == QMetaType::LongLong || type == QMetaType::ULongLong) &&
            std::isfinite(value.toDouble()));
}
QByteArray encodedJson(const QVariant &value)
{
    const auto bytes =
        QJsonDocument(QJsonArray{QJsonValue::fromVariant(value)}).toJson(QJsonDocument::Compact);
    return bytes.mid(1, bytes.size() - 2);
}
QVariantMap archiveRequest(const QVariantMap &request, EchoPluginAssetJob &job)
{
    if (job.canceled->load() || job.archiveClock.hasExpired(60000)) {
        fail(job, "Canceled", "Asset archive preparation ended");
        return {};
    }
    const auto directory = job.staging->path();
    const auto id = QUuid::createUuid().toString(QUuid::Id128);
    const auto input = QDir(directory).filePath(id + ".request.json");
    const auto output = QDir(directory).filePath(id + ".result.json");
    const auto bytes = QJsonDocument::fromVariant(request).toJson(QJsonDocument::Compact);
    QFile file(input);
    if (bytes.size() > 2 * MiB || !file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
        file.write(bytes) != bytes.size()) {
        fail(job, "AssetStorageError", "Cannot write bounded archive request");
        return {};
    }
    file.close();
    const auto response = decode(input, "archive", output, job);
    if (response.isEmpty())
        return {};
    const auto length = response.value("metadataBytes").toLongLong();
    if (length < 1 || length > 2 * MiB ||
        !verifyDigest(output, length, response.value("metadataSha256").toString(), job))
        return {};
    QFile metadata(output);
    if (!metadata.open(QIODevice::ReadOnly)) {
        fail(job, "AssetStorageError", "Cannot read archive metadata");
        return {};
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(metadata.read(2 * MiB + 1), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        fail(job, "AssetDecodeError", "Invalid archive metadata JSON");
        return {};
    }
    return document.object().toVariantMap();
}
bool prepareArchive(const QString &directory, EchoPluginAssetJob &job)
{
    auto &session = *job.archive;
    if (!session.directory) {
        // Ready inspection leases must never leave mutable cache trees in the
        // plugin data snapshot. Only ImportArchive stages publication on disk.
        session.directory = std::make_shared<QTemporaryDir>();
        if (!session.directory->isValid())
            return fail(job, "AssetStorageError", "Cannot create bounded archive staging");
    }
    job.staging = job.command == ImportArchive
                      ? std::make_shared<QTemporaryDir>(QDir(directory).filePath(".staging-XXXXXX"))
                      : session.directory;
    if (!job.staging->isValid())
        return fail(job, "AssetStorageError", "Cannot create same-filesystem archive staging");
    if (job.command == InspectArchive) {
        session.source = QDir(job.staging->path()).filePath("archive.zip");
        if (!copySnapshot(job.files.value(0), session.source, 72 * MiB, job))
            return false;
        job.sample = archiveRequest(
            {{"command", "inspect"}, {"archive", session.source}, {"jsonPaths", job.jsonPaths}},
            job);
        if (job.sample.isEmpty())
            return false;
        const auto paths = job.sample.value("rawPaths").toList();
        if (!job.sample.contains("rawPaths") || paths.size() > 256)
            return fail(job, "AssetDecodeError", "Missing or oversized raw archive path metadata");
        QSet<QString> keys;
        for (const auto &item : paths) {
            auto path = item.toString();
            if (path.endsWith('/'))
                path.chop(1);
            const auto key = archiveKey(path);
            if (!archivePath(path) || keys.contains(key))
                return fail(job, "AssetDecodeError", "Unsafe or colliding Unicode archive path");
            keys.insert(key);
        }
        return true;
    }
    if (job.command == ImportArchive) {
        QVariantList selections;
        job.files.clear();
        for (const auto &item : job.selections) {
            const auto entry = item.toMap();
            const auto target =
                QDir(job.staging->path()).filePath(QUuid::createUuid().toString(QUuid::Id128));
            selections.append(QVariantMap{{"path", entry.value("path")}, {"target", target}});
            job.files.append(target);
            job.displayNames.append(entry.value("name").toString());
        }
        return !archiveRequest(
                    {{"command", "extract"}, {"archive", session.source}, {"entries", selections}},
                    job)
                    .isEmpty();
    }
    QVariantList files;
    qint64 captured = 0;
    for (const auto &value : job.selections) {
        const auto entry = value.toMap();
        const auto found =
            std::find_if(job.loaded.cbegin(), job.loaded.cend(), [&](const auto &asset) {
                return asset.toMap().value("collection") == job.value &&
                       asset.toMap().value("kind") == "image" &&
                       asset.toMap().value("handle") == entry.value("handle");
            });
        if (found == job.loaded.cend())
            return fail(job, "NotFound", "Export image does not belong to this collection");
        const auto asset = found->toMap();
        const auto original = QDir(directory).filePath(assetFile(asset));
        const auto target =
            QDir(job.staging->path()).filePath(QUuid::createUuid().toString(QUuid::Id128));
        if (!copySnapshot(original, target, qMin(8 * MiB, 64 * MiB - captured), job) ||
            !verifyDigest(target, asset.value("bytes").toLongLong(),
                          asset.value("sha256").toString(), job))
            return false;
        captured += QFileInfo(target).size();
        files.append(QVariantMap{{"path", entry.value("path")}, {"source", target}});
    }
    for (auto iterator = job.archiveJson.begin(); iterator != job.archiveJson.end(); ++iterator) {
        const auto target =
            QDir(job.staging->path()).filePath(QUuid::createUuid().toString(QUuid::Id128));
        const auto bytes = encodedJson(iterator.value());
        QFile file(target);
        if (bytes.size() > 65536 || !file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
            file.write(bytes) != bytes.size())
            return fail(job, "AssetStorageError", "Cannot snapshot bounded archive JSON");
        file.close();
        files.append(QVariantMap{{"path", iterator.key()}, {"source", target}});
    }
    const auto zip = QDir(job.staging->path()).filePath("archive.zip");
    job.sample = archiveRequest({{"command", "export"}, {"output", zip}, {"files", files}}, job);
    if (job.sample.isEmpty())
        return false;
    // Reclaim all snapshots before allocating QSaveFile's second ZIP copy.
    // Phase one <=64MiB originals+72MiB ZIP+metadata; phase two <=2*72MiB ZIP.
    for (const auto &name :
         QDir(job.staging->path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot))
        if (name != "archive.zip" && !QFile::remove(QDir(job.staging->path()).filePath(name)))
            return fail(job, "AssetStorageError",
                        "Cannot reclaim export snapshots within its reservation");
    QFile input(zip);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 72 * MiB ||
        input.size() != job.sample.value("bytes").toLongLong() || QFileInfo(job.source).isSymLink())
        return fail(job, "AssetStorageError", "Invalid bounded export ZIP or destination");
    job.save = std::make_shared<QSaveFile>(job.source);
    const auto transferSave = qScopeGuard([&job] {
        if (job.save)
            job.save->moveToThread(QCoreApplication::instance()->thread());
    });
    job.save->setDirectWriteFallback(false);
    if (!job.save->open(QIODevice::WriteOnly))
        return fail(job, "AssetStorageError", "Cannot open selected archive destination");
    qint64 written = 0;
    while (!input.atEnd()) {
        if (job.canceled->load() || job.archiveClock.hasExpired(60000)) {
            job.save->cancelWriting();
            return false;
        }
        const auto bytes = input.read(65536);
        written += bytes.size();
        if (bytes.isEmpty() || written > 72 * MiB || job.save->write(bytes) != bytes.size())
            return fail(job, "AssetStorageError", "Cannot write bounded selected archive");
    }
    if (!job.save->flush())
        return fail(job, "AssetStorageError", "Cannot flush selected archive");
    return true;
}
void preparePartialImages(EchoPluginAssetJob &job)
{
    qint64 existingBytes = 0, stagedBytes = 0;
    QMimeDatabase mime;
    for (const auto &value : job.loaded)
        existingBytes += storedBytes(value.toMap());
    int selected = 0;
    for (const auto &source : job.files) {
        if (job.canceled->load())
            return;
        const QFileInfo info(source);
        const auto reject = [&](const QString &code, const QString &message) {
            job.rejected.append(QVariantMap{
                {"name", info.fileName().left(128)}, {"code", code}, {"message", message}});
        };
        if (!QStringList{"png", "jpg", "jpeg", "gif", "webp", "svg"}.contains(
                info.suffix().toLower())) {
            // Browser File.type accepts image/* even without a conventional
            // suffix. QtCore's bounded header classification stays on this
            // worker; actual image parsing/decoding remains in the helper.
            if (selected >= job.maxCount) {
                reject("QuotaExceeded", "Image count capacity reached");
                continue;
            }
            if (!info.isFile() || info.isSymLink() || info.size() <= 0 || info.size() > 8 * MiB) {
                reject("QuotaExceeded", "Image is invalid or exceeds its byte budget");
                continue;
            }
            if (!mime.mimeTypeForFile(source, QMimeDatabase::MatchContent)
                     .name()
                     .startsWith("image/")) {
                reject("UnsupportedFormat", "Only supported image files may be imported");
                continue;
            }
        }
        // Folia reserves the supported input slots before preparing records;
        // a corrupt accepted file does not pull a later over-cap file forward.
        if (selected++ >= job.maxCount || job.loaded.size() + job.added.size() >= 128) {
            reject("QuotaExceeded", "Image count capacity reached");
            continue;
        }
        const auto size = info.size();
        if (!info.isFile() || info.isSymLink() || size <= 0 || size > 8 * MiB ||
            existingBytes + stagedBytes + size > 64 * MiB) {
            reject("QuotaExceeded", "Image is invalid or exceeds its byte budget");
            continue;
        }
        const auto handle = "asset:" + QUuid::createUuid().toString(QUuid::Id128);
        const auto input = QDir(job.staging->path()).filePath(handle.mid(6));
        const auto derivative = input + ".preview.png";
        // Source reads and destination writes retain the existing fatal error
        // path. Cancellation and storage errors never publish a partial batch.
        if (!copySnapshot(source, input, qMin(8 * MiB, 64 * MiB - existingBytes - stagedBytes),
                          job))
            return;
        const auto copiedBytes = QFileInfo(input).size();
        job.renderLimit = 64 * MiB - existingBytes - stagedBytes - copiedBytes;
        auto asset = decode(input, "image", derivative, job);
        if (asset.isEmpty()) {
            if (job.canceled->load() || job.code != "AssetDecodeError")
                return;
            if (!QFile::remove(input) ||
                (QFile::exists(derivative) && !QFile::remove(derivative))) {
                fail(job, "AssetStorageError", "Cannot reclaim rejected image staging");
                return;
            }
            job.code.clear();
            job.error.clear();
            reject("AssetDecodeError", "Image could not be decoded");
            continue;
        }
        const auto renderedBytes = asset.value("renderBytes").toLongLong();
        if (renderedBytes < 0 || renderedBytes > job.renderLimit ||
            (renderedBytes > 0 && (asset.value("extension") != "svg" ||
                                   !verifyDigest(derivative, renderedBytes,
                                                 asset.value("renderSha256").toString(), job)))) {
            fail(job, "AssetStorageError", "Invalid validated image derivative");
            return;
        }
        QFile file(input);
        if (!file.open(QIODevice::ReadOnly)) {
            fail(job, "AssetStorageError", "Cannot verify copied image");
            return;
        }
        const auto bytes = file.read(8 * MiB + 1);
        if (bytes.size() != copiedBytes || file.error() != QFileDevice::NoError) {
            fail(job, "AssetStorageError", "Cannot verify copied image");
            return;
        }
        file.close();
        asset.remove("version");
        asset["handle"] = handle;
        asset["kind"] = "image";
        asset["collection"] = job.value;
        asset["name"] = info.fileName().left(128);
        asset["bytes"] = copiedBytes;
        asset["sha256"] = QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        if (!QFile::rename(input, QDir(job.staging->path()).filePath(assetFile(asset)))) {
            fail(job, "AssetStorageError", "Cannot stage validated image");
            return;
        }
        stagedBytes += copiedBytes + renderedBytes;
        job.added.append(asset);
    }
}
void prepare(const QString &directory, EchoPluginAssetJob &job)
{
    if (job.canceled->load() || !readIndex(directory, job))
        return;
    if (job.command != ImportImages && job.command != ImportFonts && job.command != Sample &&
        job.command != Frames && job.command != InspectArchive && job.command != ImportArchive &&
        job.command != ExportArchive && job.command != Remove && job.command != Clear)
        return;
    if (job.archive) {
        if (!prepareArchive(directory, job) || job.command != ImportArchive)
            return;
    } else
        job.staging =
            job.command == Sample || job.command == Frames
                ? std::make_shared<QTemporaryDir>()
                : std::make_shared<QTemporaryDir>(QDir(directory).filePath(".staging-XXXXXX"));
    if (!job.staging->isValid()) {
        fail(job, "AssetStorageError", "Cannot create private asset staging");
        return;
    }
    if (job.command == Remove || job.command == Clear)
        return;
    if (job.command == Sample || job.command == Frames) {
        if (!job.value.isEmpty()) {
            bool found = false;
            for (const auto &value : job.loaded) {
                const auto asset = value.toMap();
                const auto path = QDir(directory).filePath(assetFile(asset));
                if (asset.value("kind") == "image" &&
                    (job.value == asset.value("handle").toString() ||
                     job.value == QUrl::fromLocalFile(path).toString() ||
                     job.value == QUrl::fromLocalFile(QDir(directory).filePath(publicFile(asset)))
                                      .toString())) {
                    job.source = path;
                    job.expectedHash = asset.value("sha256").toString();
                    found = true;
                    break;
                }
            }
            if (!found) {
                fail(job, "SourceDenied", "Managed asset does not exist");
                return;
            }
        }
        const auto input = QDir(job.staging->path()).filePath("input");
        if (job.bytes.isEmpty()) {
            if (!copySnapshot(job.source, input, 8 * MiB, job))
                return;
        } else {
            QFile file(input);
            if (!file.open(QIODevice::WriteOnly) || file.write(job.bytes) != job.bytes.size()) {
                fail(job, "AssetStorageError", "Cannot stage current cover");
                return;
            }
        }
        if (!job.expectedHash.isEmpty()) {
            QFile file(input);
            if (!file.open(QIODevice::ReadOnly) ||
                QString::fromLatin1(
                    QCryptographicHash::hash(file.read(8 * MiB + 1), QCryptographicHash::Sha256)
                        .toHex()) != job.expectedHash) {
                fail(job, "AssetStorageError",
                     "Managed asset content failed integrity verification");
                return;
            }
        }
        const bool frames = job.command == Frames;
        const auto pixelsFile =
            QDir(job.staging->path()).filePath(frames ? "frames" : "pixels.rgba");
        const auto info =
            decode(input, frames ? (job.fit == "cover" ? "frames-cover" : "frames") : "sample",
                   pixelsFile, job);
        if (info.isEmpty())
            return;
        if (frames) {
            const auto delays = info.value("delays").toList();
            const int count = info.value("frameCount").toInt();
            const int outputWidth = info.value("width").toInt();
            const int outputHeight = info.value("height").toInt();
            qint64 bytes = 0;
            const QSize original(info.value("sourceWidth").toInt(),
                                 info.value("sourceHeight").toInt());
            auto expectedSize = original.scaled(QSize(job.width, job.height),
                                                job.fit == "cover" ? Qt::KeepAspectRatioByExpanding
                                                                   : Qt::KeepAspectRatio);
            if (expectedSize.width() > original.width() ||
                expectedSize.height() > original.height())
                expectedSize = original;
            if (count < 1 || count > 256 || delays.size() != count || !original.isValid() ||
                original.width() > 4096 || original.height() > 4096 ||
                QSize(outputWidth, outputHeight) != expectedSize) {
                fail(job, "AssetDecodeError", "Invalid animation frame metadata");
                return;
            }
            for (int i = 0; i < count; ++i) {
                const QFileInfo frame(QDir(pixelsFile).filePath(QString::number(i) + ".png"));
                const auto delay = delays[i].toLongLong();
                bytes += frame.size();
                if (!frame.isFile() || frame.isSymLink() || frame.size() <= 0 || delay < 1 ||
                    delay > 2147483647 || bytes > 64 * MiB) {
                    fail(job, "AssetDecodeError", "Invalid animation frame cache");
                    return;
                }
            }
            if (bytes != info.value("encodedBytes").toLongLong() ||
                info.value("loopCount").toInt() < -1) {
                fail(job, "AssetDecodeError", "Invalid animation cache size or loop count");
                return;
            }
            QFile::remove(input);
            job.bytes.clear();
            job.sample = info;
            return;
        }
        QFile pixels(pixelsFile);
        const int sourceWidth = info.value("sourceWidth").toInt();
        const int sourceHeight = info.value("sourceHeight").toInt();
        if (sourceWidth < 1 || sourceWidth > 4096 || sourceHeight < 1 || sourceHeight > 4096 ||
            qint64(sourceWidth) * sourceHeight > 16 * 1024 * 1024 ||
            info.value("width").toInt() != job.width ||
            info.value("height").toInt() != job.height || !pixels.open(QIODevice::ReadOnly) ||
            pixels.size() != job.width * job.height * 4) {
            fail(job, "AssetDecodeError", "Asset decoder returned invalid pixels");
            return;
        }
        const auto bytes = pixels.readAll();
        QVariantList values;
        values.reserve(bytes.size());
        for (const auto byte : bytes)
            values.append(quint8(byte));
        job.sample = {{"width", job.width},         {"height", job.height},
                      {"sourceWidth", sourceWidth}, {"sourceHeight", sourceHeight},
                      {"format", "rgba8"},          {"pixels", values}};
        return;
    }
    if (job.command == ImportImages && job.partial) {
        preparePartialImages(job);
        return;
    }
    const bool font = job.command == ImportFonts;
    qint64 existingBytes = 0, selectedBytes = 0;
    int existingFonts = 0;
    for (const auto &value : job.loaded) {
        existingBytes += storedBytes(value.toMap());
        existingFonts += value.toMap().value("kind") == "font";
    }
    if (job.files.size() > job.maxCount || job.loaded.size() + job.files.size() > 128 ||
        (font && existingFonts + job.files.size() > 16)) {
        fail(job, "QuotaExceeded", "Selected assets exceed installation count budget");
        return;
    }
    for (const auto &source : job.files) {
        const QFileInfo info(source);
        const auto bytes = info.size();
        if (!info.isFile() || info.isSymLink() || bytes <= 0 || bytes > (font ? 16 : 8) * MiB ||
            existingBytes + (selectedBytes += bytes) > 64 * MiB) {
            fail(job, "QuotaExceeded", "Selected assets exceed installation byte budget");
            return;
        }
    }
    qint64 stagedBytes = 0;
    qint64 derivativeBytes = 0;
    int fileIndex = 0;
    for (const auto &source : job.files) {
        if (job.canceled->load())
            return;
        const auto handle = "asset:" + QUuid::createUuid().toString(QUuid::Id128);
        const auto input = job.command == ImportArchive
                               ? source
                               : QDir(job.staging->path()).filePath(handle.mid(6));
        if (job.command != ImportArchive &&
            !copySnapshot(source, input,
                          qMin((font ? 16 : 8) * MiB, 64 * MiB - existingBytes - stagedBytes), job))
            return;
        stagedBytes += QFileInfo(input).size();
        job.renderLimit = 64 * MiB - existingBytes - selectedBytes - derivativeBytes;
        const auto derivative = QDir(job.staging->path()).filePath(handle.mid(6) + ".preview.png");
        auto asset = decode(input, font ? "font" : "image", derivative, job);
        if (asset.isEmpty())
            return;
        const auto renderedBytes = asset.value("renderBytes").toLongLong();
        if (renderedBytes < 0 || renderedBytes > job.renderLimit ||
            (renderedBytes > 0 && (asset.value("extension") != "svg" ||
                                   !verifyDigest(derivative, renderedBytes,
                                                 asset.value("renderSha256").toString(), job)))) {
            fail(job, "QuotaExceeded", "SVG PNG derivative exceeds the installation byte budget");
            return;
        }
        derivativeBytes += renderedBytes;
        stagedBytes += renderedBytes;
        asset.remove("version");
        asset["handle"] = handle;
        asset["kind"] = font ? "font" : "image";
        asset["collection"] = job.value;
        asset["name"] = job.command == ImportArchive ? job.displayNames.value(fileIndex)
                                                     : QFileInfo(source).fileName().left(128);
        ++fileIndex;
        asset["bytes"] = QFileInfo(input).size();
        QFile file(input);
        if (!file.open(QIODevice::ReadOnly)) {
            fail(job, "AssetStorageError", "Cannot verify copied asset");
            return;
        }
        const auto bytes = file.read((font ? 16 : 8) * MiB + 1);
        file.close(); // Release the native read handle before same-volume rename on Windows.
        asset["sha256"] = QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        if (font)
            job.fonts.insert(handle, bytes);
        if (!QFile::rename(input, QDir(job.staging->path()).filePath(assetFile(asset)))) {
            fail(job, "AssetStorageError", "Cannot stage validated asset");
            return;
        }
        job.added.append(asset);
    }
}
QVariantMap publicAsset(const QVariantMap &asset, const QString &directory)
{
    auto result = asset;
    result.remove("extension");
    result.remove("sha256");
    result.remove("renderSha256");
    if (asset.value("renderBytes").toLongLong() > 0)
        result["renderMimeType"] = "image/png";
    result["url"] = QUrl::fromLocalFile(QDir(directory).filePath(publicFile(asset)));
    return result;
}
bool persist(EchoPluginAssetStore &store, const QVariantList &assets, QString &error)
{
    const auto bytes =
        QJsonDocument(QJsonObject{{"version", 1}, {"assets", QJsonArray::fromVariantList(assets)}})
            .toJson(QJsonDocument::Compact);
    if (bytes.size() > 65536) {
        error = "Asset index exceeds 64 KiB";
        return false;
    }
    QSaveFile file(QDir(store.directory).filePath("index.json"));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = "Cannot publish asset index";
        return false;
    }
    store.assets = assets;
    return true;
}
} // namespace

EchoPluginAssets::EchoPluginAssets(EchoPluginContext &scope, QString dataPath,
                                   EchoPluginAssets *activationAssets)
    : QObject(&scope), m_scope(scope),
      m_store(activationAssets ? activationAssets->m_store
                               : std::make_shared<EchoPluginAssetStore>()),
      m_network(this)
{
    if (!activationAssets)
        m_store->directory = QDir(dataPath).filePath("assets-v1");
    m_store->observers.removeIf([](const auto &value) { return value.isNull(); });
    m_store->observers.append(this);
    connect(&scope, &EchoPluginContext::activeChanged, this, [this] {
        if (!m_scope.active())
            shutdown();
    });
    const auto invalidateArchives = [this] {
        const auto leases = m_archiveLeases;
        for (auto *lease : leases)
            lease->dispose();
    };
    connect(&m_scope.host().account, &AccountService::sessionAdopted, this, invalidateArchives);
    connect(&m_scope.host().api, &ApiClient::sessionChanged, this, invalidateArchives);
}
EchoPluginAssets::~EchoPluginAssets()
{
    shutdown();
    Q_ASSERT(isDrained());
}
bool EchoPluginAssets::isDrained() const
{
    return m_jobs.isEmpty() && m_cleanupCount == 0 && m_storagePending == 0 &&
           m_animationLeases.isEmpty() && m_archiveLeases.isEmpty();
}
void EchoPluginAssets::storageBarrier(std::function<void()> ready)
{
    m_storageBarriers.append(std::move(ready));
    // An unaccepted native picker is not an accepted disk write. Close it;
    // workers already accepted retain their writer and complete normally.
    if (m_dialog)
        m_dialog->close();
    checkStorageBarriers();
}
void EchoPluginAssets::resumeStorage()
{
    QMetaObject::invokeMethod(this, &EchoPluginAssets::processAnimationQueue, Qt::QueuedConnection);
}
void EchoPluginAssets::checkStorageBarriers()
{
    if (!m_jobs.isEmpty() || m_cleanupCount || m_storagePending)
        return;
    const QPointer<EchoPluginAssets> self(this);
    const auto callbacks = std::exchange(m_storageBarriers, {});
    for (const auto &ready : callbacks) {
        ready();
        if (!self)
            return;
    }
}
qint64 EchoPluginAssets::archiveReservedBytes()
{
    QMutexLocker lock(&archiveBudgetMutex);
    return qint64(archiveSlots) * 144 * MiB;
}
int EchoPluginAssets::archiveSlotCount()
{
    QMutexLocker lock(&archiveBudgetMutex);
    return archiveSlots;
}

std::shared_ptr<EchoPluginAssetJob> EchoPluginAssets::begin(int command, const QString &value,
                                                            bool countPending)
{
    auto job = std::make_shared<EchoPluginAssetJob>();
    job->command = command;
    job->value = value;
    job->countPending = countPending;
    job->epoch = m_scope.accountEpoch();
    job->operation = m_scope.operation();
    if (job->operation->finished())
        return job;
    if (m_closed || !m_scope.active()) {
        job->operation->fail("ScopeClosed", "Plugin presentation has ended");
        return job;
    }
    if (m_scope.storageFrozen()) {
        job->operation->fail("StorageFrozen", "Plugin data is taking a backup snapshot");
        return job;
    }
    if (countPending && m_store->pending >= 2) {
        job->operation->fail("Busy", "At most two asset requests may be pending per installation");
        return job;
    }
    // Acquired before directory creation or worker dispatch, retained through
    // actual publication and private staging cleanup, including cancellation.
    job->storageFence = StorageFence::write(m_store->directory);
    if (!job->storageFence) {
        job->operation->fail("StorageFrozen", "Plugin asset storage is taking a snapshot");
        return job;
    }
    if (countPending)
        ++m_store->pending;
    m_jobs.append(job);
    ++m_storagePending;
    const auto canceled = job->canceled;
    const auto guard = QPointer<EchoPluginAssets>(this);
    const std::weak_ptr<EchoPluginAssetJob> weak = job;
    job->operation->start([canceled, guard, weak] {
        canceled->store(true);
        if (guard) {
            if (const auto current = weak.lock()) {
                if (current->dialog)
                    current->dialog->close();
                if (current->reply)
                    current->reply->abort();
                if (current->deadline)
                    current->deadline->stop();
            }
        }
    });
    return job;
}
void EchoPluginAssets::run(const std::shared_ptr<EchoPluginAssetJob> &job)
{
    job->running = true;
    if (job->archive)
        job->archiveClock.start();
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, job] {
        watcher->deleteLater();
        complete(job);
    });
    const auto directory = m_store->directory;
    watcher->setFuture(QtConcurrent::run([directory, job] { prepare(directory, *job); }));
}
EchoPluginOperation *EchoPluginAssets::request(int command, const QString &value)
{
    if ((command == Remove && !handleValid(value)) ||
        (command != Remove && !collectionValid(value)))
        return m_scope.rejected("InvalidArgument", "Invalid asset collection or handle");
    const auto job = begin(command, value);
    if (!job->operation->finished())
        run(job);
    return job->operation;
}
EchoPluginOperation *EchoPluginAssets::list(const QString &value)
{
    return request(List, value);
}
EchoPluginOperation *EchoPluginAssets::remove(const QString &value)
{
    return request(Remove, value);
}
EchoPluginOperation *EchoPluginAssets::clear(const QString &value)
{
    return request(Clear, value);
}
EchoPluginOperation *EchoPluginAssets::importImages(const QString &value)
{
    return import(value, false);
}
EchoPluginOperation *EchoPluginAssets::importImages(const QString &value,
                                                    const QVariantMap &options)
{
    return import(value, false, options);
}
EchoPluginOperation *EchoPluginAssets::importFonts(const QString &value)
{
    return import(value, true);
}

EchoPluginOperation *EchoPluginAssets::import(const QString &collection, bool fonts,
                                              const QVariantMap &options)
{
    if (!collectionValid(collection))
        return m_scope.rejected("InvalidArgument", "Invalid asset collection");
    bool partial = false;
    int maxCount = 16;
    if (!imageOptions(options, partial, maxCount))
        return m_scope.rejected("InvalidArgument", "Invalid image import options");
    if (m_store->picker)
        return m_scope.rejected("Busy", "An asset picker is already open");
    const auto job = begin(fonts ? ImportFonts : ImportImages, collection);
    if (job->operation->finished())
        return job->operation;
    job->partial = partial;
    job->maxCount = maxCount;
    auto *dialog = new EchoFileDialog(this);
    dialog->setObjectName("echoPluginAssetPicker");
    dialog->setProperty("title", fonts ? tr("导入插件字体") : tr("导入插件图片"));
    dialog->setProperty("font", QVariant::fromValue(m_scope.font()));
    dialog->setProperty("parentWindow", QVariant::fromValue(QGuiApplication::focusWindow()));
    dialog->setProperty("fileMode", int(EchoFileDialog::OpenFiles));
    dialog->setProperty("nameFilters",
                        QStringList{fonts ? tr("字体 (*.ttf *.otf *.ttc)")
                                          : tr("图片 (*.png *.jpg *.jpeg *.gif *.webp *.svg)")});
    m_dialog = dialog;
    job->dialog = dialog;
    m_store->picker = true;
    const auto selectionFinished = [this, dialog, job](bool accepted) {
        m_dialog = nullptr;
        job->dialog = nullptr;
        m_store->picker = false;
        if (accepted && !job->canceled->load()) {
            const auto urls = dialog->selectedFiles();
            if (urls.isEmpty() || urls.size() > (job->partial ? 128 : 16))
                fail(*job, "QuotaExceeded", "Selected file count exceeds the import budget");
            else {
                for (const auto &url : urls) {
                    if (!url.isLocalFile())
                        fail(*job, "InvalidArgument",
                             "Only user-picked local files may be imported");
                    job->files.append(url.toLocalFile());
                }
            }
        } else
            job->canceled->store(true);
        dialog->deleteLater();
        if (job->error.isEmpty() && !job->canceled->load())
            run(job);
        else
            complete(job);
    };
    connect(dialog, &EchoFileDialog::accepted, this,
            [selectionFinished] { selectionFinished(true); });
    connect(dialog, &EchoFileDialog::rejected, this,
            [selectionFinished] { selectionFinished(false); });
    dialog->open();
    return job->operation;
}

EchoPluginOperation *EchoPluginAssets::importDroppedImages(const QString &collection,
                                                           const QList<QUrl> &urls, int maxCount)
{
    if (!collectionValid(collection) || maxCount < 0 || maxCount > 16 || urls.isEmpty() ||
        urls.size() > 128)
        return m_scope.rejected("InvalidArgument", "Invalid external image drop arguments");
    for (const auto &url : urls)
        if (!url.isLocalFile() || url.toLocalFile().isEmpty() || url.toLocalFile().size() > 8192)
            return m_scope.rejected("InvalidArgument",
                                    "Only bounded local image drops are allowed");
    if (m_store->picker || !m_store->archive.expired())
        return m_scope.rejected("Busy", "An asset picker or archive operation is already active");
    const auto job = begin(ImportImages, collection);
    if (!job->operation->finished()) {
        job->partial = true;
        job->maxCount = maxCount;
        for (const auto &url : urls)
            job->files.append(url.toLocalFile());
        run(job);
    }
    return job->operation;
}

EchoPluginOperation *EchoPluginAssets::sampleImage(const QUrl &source, int width, int height)
{
    if (width < 1 || width > 256 || height < 1 || height > 256)
        return m_scope.rejected("InvalidArgument", "Sample dimensions must be 1..256");
    return imageRequest(Sample, source, width, height);
}

EchoPluginAssetArchive *EchoPluginAssets::openImageArchive(const QStringList &jsonPaths)
{
    return openArchive(jsonPaths);
}

EchoPluginAssetArchive *EchoPluginAssets::openDroppedImageArchive(const QStringList &jsonPaths,
                                                                  const QUrl &source)
{
    if (!source.isLocalFile() || source.toLocalFile().isEmpty() ||
        source.toLocalFile().size() > 8192)
        return nullptr;
    return openArchive(jsonPaths, source);
}

EchoPluginAssetArchive *EchoPluginAssets::openArchive(const QStringList &jsonPaths,
                                                      const QUrl &selected)
{
    if (m_closed || !m_scope.active() || m_archiveLeases.size() >= 128)
        return nullptr;
    auto *lease = new EchoPluginAssetArchive(*this);
    QQmlEngine::setObjectOwnership(lease, QQmlEngine::CppOwnership);
    m_archiveLeases.insert(lease);
    if (jsonPaths.size() > 4) {
        lease->m_error = "At most four bounded archive JSON paths may be requested";
        return lease;
    }
    QSet<QString> keys;
    for (const auto &path : jsonPaths) {
        const auto key = archiveKey(path);
        if (!archivePath(path) || !path.endsWith(".json") || keys.contains(key)) {
            lease->m_error = "Invalid or duplicate archive JSON path";
            return lease;
        }
        keys.insert(key);
    }
    if (m_store->picker || !m_store->archive.expired()) {
        lease->m_error = "Archive or picker capacity is busy";
        return lease;
    }
    const auto job = begin(InspectArchive, {});
    if (job->operation->finished()) {
        lease->m_error = job->operation->errorMessage();
        return lease;
    }
    {
        QMutexLocker lock(&archiveBudgetMutex);
        if (archiveSlots >= 2) {
            lock.unlock();
            job->operation->fail("Busy", "At most two archive reservations may exist");
            lease->m_error = job->operation->errorMessage();
            complete(job);
            return lease;
        }
        ++archiveSlots;
    }
    auto session = std::make_shared<EchoPluginArchiveSession>();
    session->epoch = job->epoch;
    session->lease = lease;
    lease->m_session = session;
    m_store->archive = session;
    job->archive = session;
    job->jsonPaths = jsonPaths;
    if (!selected.isEmpty()) {
        job->files = {selected.toLocalFile()};
        run(job);
        return lease;
    }
    auto *dialog = new EchoFileDialog(this);
    dialog->setObjectName("echoPluginArchivePicker");
    dialog->setProperty("title", tr("导入图片集合备份"));
    dialog->setProperty("font", QVariant::fromValue(m_scope.font()));
    dialog->setProperty("parentWindow", QVariant::fromValue(QGuiApplication::focusWindow()));
    dialog->setProperty("fileMode", int(EchoFileDialog::OpenFile));
    dialog->setProperty("nameFilters", QStringList{tr("图片集合备份 (*.zip)")});
    m_dialog = dialog;
    job->dialog = dialog;
    m_store->picker = true;
    const auto selectionFinished = [this, dialog, job](bool accepted) {
        m_dialog = nullptr;
        job->dialog = nullptr;
        m_store->picker = false;
        const auto urls = dialog->selectedFiles();
        if (accepted && !job->canceled->load() && urls.size() == 1 && urls.first().isLocalFile())
            job->files = {urls.first().toLocalFile()};
        else
            job->canceled->store(true);
        dialog->deleteLater();
        if (job->canceled->load())
            complete(job);
        else
            run(job);
    };
    connect(dialog, &EchoFileDialog::accepted, this,
            [selectionFinished] { selectionFinished(true); });
    connect(dialog, &EchoFileDialog::rejected, this,
            [selectionFinished] { selectionFinished(false); });
    dialog->open();
    return lease;
}

EchoPluginOperation *EchoPluginAssets::importArchive(EchoPluginAssetArchive *lease,
                                                     const QString &collection,
                                                     const QVariantList &entries)
{
    if (!collectionValid(collection) || !lease || !m_archiveLeases.contains(lease) ||
        !lease->m_active || !lease->m_ready || !lease->m_session || entries.isEmpty() ||
        entries.size() > 16)
        return m_scope.rejected("InvalidArgument",
                                "Invalid archive lease, collection or selected images");
    const auto session = lease->m_session;
    if (session->closing || session->importing || session->epoch != m_scope.accountEpoch())
        return m_scope.rejected("ScopeClosed", "Archive scope ended or import already started");
    QVariantList selected;
    for (const auto &item : entries) {
        auto entry = item.toMap();
        const auto path = entry.value("path").toString();
        const auto name = entry.value("name").toString();
        if (!archivePath(path) || name.size() > 128 ||
            std::none_of(session->entries.cbegin(), session->entries.cend(),
                         [&](const auto &candidate) {
                             return candidate.toMap().value("path").toString() == path;
                         }))
            return m_scope.rejected("InvalidArgument",
                                    "Selected image is not present in this archive");
        selected.append(QVariantMap{
            {"path", path}, {"name", name.isEmpty() ? path.section('/', -1).left(128) : name}});
    }
    const auto job = begin(ImportArchive, collection, false);
    if (!job->operation->finished()) {
        session->importing = true;
        lease->m_ready = false;
        job->archive = session;
        job->selections = selected;
        emit lease->changed();
        run(job);
    }
    return job->operation;
}

EchoPluginOperation *EchoPluginAssets::exportImageArchive(const QString &collection,
                                                          const QVariantList &entries,
                                                          const QVariantMap &jsonFiles,
                                                          const QString &suggestedName)
{
    if (!collectionValid(collection) || entries.isEmpty() || entries.size() > 128 ||
        jsonFiles.size() > 4 || suggestedName.isEmpty() || suggestedName.size() > 128 ||
        !archivePath(suggestedName) || suggestedName.contains('/'))
        return m_scope.rejected("InvalidArgument", "Invalid image archive export arguments");
    QSet<QString> names;
    for (const auto &value : entries) {
        const auto entry = value.toMap();
        const auto path = entry.value("path").toString();
        const auto key = archiveKey(path);
        if (!archivePath(path) || names.contains(key) ||
            !handleValid(entry.value("handle").toString()))
            return m_scope.rejected("InvalidArgument", "Invalid archive export handle or path");
        names.insert(key);
    }
    for (auto iterator = jsonFiles.begin(); iterator != jsonFiles.end(); ++iterator) {
        const auto key = archiveKey(iterator.key());
        if (!archivePath(iterator.key()) || !iterator.key().endsWith(".json") ||
            names.contains(key) || !jsonDepth(iterator.value()) ||
            encodedJson(iterator.value()).size() > 65536)
            return m_scope.rejected("InvalidArgument", "Invalid or oversized archive JSON file");
        names.insert(key);
    }
    if (m_store->picker || !m_store->archive.expired())
        return m_scope.rejected("Busy", "Archive or picker capacity is busy");
    const auto job = begin(ExportArchive, collection);
    if (job->operation->finished())
        return job->operation;
    {
        QMutexLocker lock(&archiveBudgetMutex);
        if (archiveSlots >= 2) {
            lock.unlock();
            job->operation->fail("Busy", "At most two archive reservations may exist");
            complete(job);
            return job->operation;
        }
        ++archiveSlots;
    }
    job->archive = std::make_shared<EchoPluginArchiveSession>();
    job->archive->epoch = job->epoch;
    job->archive->closing = true;
    m_store->archive = job->archive;
    job->selections = entries;
    job->archiveJson = jsonFiles;
    auto *dialog = new EchoFileDialog(this);
    dialog->setObjectName("echoPluginArchiveSavePicker");
    dialog->setProperty("title", tr("导出图片集合备份"));
    dialog->setProperty("font", QVariant::fromValue(m_scope.font()));
    dialog->setProperty("parentWindow", QVariant::fromValue(QGuiApplication::focusWindow()));
    dialog->setProperty("fileMode", int(EchoFileDialog::SaveFile));
    dialog->setProperty("nameFilters", QStringList{tr("图片集合备份 (*.zip)")});
    dialog->setProperty("defaultSuffix", "zip");
    dialog->setProperty("currentFile", suggestedName);
    m_dialog = dialog;
    job->dialog = dialog;
    m_store->picker = true;
    const auto selectionFinished = [this, dialog, job](bool accepted) {
        m_dialog = nullptr;
        job->dialog = nullptr;
        m_store->picker = false;
        const auto urls = dialog->selectedFiles();
        if (accepted && !job->canceled->load() && urls.size() == 1 && urls.first().isLocalFile())
            job->source = urls.first().toLocalFile();
        else
            job->canceled->store(true);
        dialog->deleteLater();
        if (!job->canceled->load()) {
            job->destinationFence = StorageFence::write(job->source);
            if (!job->destinationFence)
                fail(*job, "StorageFrozen", "Selected export destination is taking a snapshot");
        }
        if (job->canceled->load() || !job->error.isEmpty())
            complete(job);
        else
            run(job);
    };
    connect(dialog, &EchoFileDialog::accepted, this,
            [selectionFinished] { selectionFinished(true); });
    connect(dialog, &EchoFileDialog::rejected, this,
            [selectionFinished] { selectionFinished(false); });
    dialog->open();
    return job->operation;
}

void EchoPluginAssets::releaseArchive(EchoPluginAssetArchive *lease)
{
    m_archiveLeases.remove(lease);
    const auto session = std::move(lease->m_session);
    if (session) {
        session->lease = nullptr;
        session->closing = true;
        session->canceled->store(true);
        const auto jobs = m_jobs;
        for (const auto &job : jobs)
            if (job->archive == session && job->operation && !job->operation->finished())
                job->operation->cancel();
        cleanupArchive(session);
    }
    checkStorageBarriers();
    if (isDrained())
        emit drained();
}

void EchoPluginAssets::cleanupArchive(const std::shared_ptr<EchoPluginArchiveSession> &session,
                                      std::function<void()> completion)
{
    if (!session || !session->closing) {
        if (completion)
            completion();
        return;
    }
    if (completion)
        session->afterCleanup.append(std::move(completion));
    if (session->cleanupStarted)
        return;
    for (const auto &job : std::as_const(m_jobs))
        if (job->archive == session)
            return;
    session->cleanupStarted = true;
    ++m_cleanupCount;
    auto *watcher = new QFutureWatcher<bool>(this);
    auto directory = std::move(session->directory);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, session, directory] {
        const bool removed = watcher->result();
        watcher->deleteLater();
        if (removed) {
            QMutexLocker lock(&archiveBudgetMutex);
            --archiveSlots;
        } else {
            // Keep the failed directory and its global reservation, bounded by two slots.
            static QList<std::shared_ptr<QTemporaryDir>> retained;
            retained.append(directory);
            qWarning("Asset archive cleanup failed; its reservation remains charged");
        }
        if (session->pendingHeld) {
            session->pendingHeld = false;
            --m_store->pending;
        }
        --m_cleanupCount;
        const auto completions = std::move(session->afterCleanup);
        for (const auto &completion : completions)
            completion();
        checkStorageBarriers();
        if (isDrained())
            emit drained();
    });
    watcher->setFuture(QtConcurrent::run([directory] {
        if (!directory)
            return true;
        const bool removed = directory->remove();
        if (!removed)
            directory->setAutoRemove(false);
        return removed;
    }));
}

EchoPluginOperation *EchoPluginAssets::imageRequest(int command, const QUrl &source, int width,
                                                    int height, const QString &fit)
{
    QString path;
    QString managed;
    QString expectedHash;
    bool currentCover = !source.isEmpty() && source == m_scope.player()->coverUrl();
    auto &presentation = m_scope.host().playback.transitionPresentation();
    const bool transitionCover = presentation.permitsCover(source);
    for (const auto &value : std::as_const(m_store->assets)) {
        const auto asset = value.toMap();
        const auto url = publicAsset(asset, m_store->directory).value("url").toUrl();
        if (asset.value("kind") == "image" &&
            (source.toString() == asset.value("handle").toString() || source == url)) {
            path = url.toLocalFile();
            managed = source.toString();
            expectedHash = asset.value("sha256").toString();
        }
    }
    if (!m_store->loaded &&
        (handleValid(source.toString()) ||
         (source.isLocalFile() && QFileInfo(source.toLocalFile()).absolutePath() ==
                                      QDir(m_store->directory).absolutePath())))
        managed = source.toString();
    if (path.isEmpty() && source.isLocalFile()) {
        const QFileInfo info(source.toLocalFile());
        const auto root = QFileInfo(m_scope.package().root).canonicalFilePath();
        if (!root.isEmpty() && info.isFile() && !info.isSymLink() &&
            info.canonicalFilePath().startsWith(root + '/'))
            path = info.canonicalFilePath();
    }
    if (path.isEmpty() && (currentCover || transitionCover) && source.isLocalFile())
        path = source.toLocalFile();
    const bool remote = (currentCover || transitionCover) &&
                        (source.scheme() == "http" || source.scheme() == "https") &&
                        !source.host().isEmpty() && source.userInfo().isEmpty();
    if (path.isEmpty() && managed.isEmpty() && !remote)
        return m_scope.rejected("SourceDenied", "Only active playback covers, package images and "
                                                "this installation's assets are allowed");
    const auto job = begin(command, {});
    if (job->operation->finished())
        return job->operation;
    job->source = path;
    job->value = managed;
    job->expectedHash = expectedHash;
    job->width = width;
    job->height = height;
    job->fit = fit;
    job->trackScoped = currentCover && !transitionCover;
    job->transitionEpoch =
        transitionCover ? presentation.snapshot().value("epoch").toString() : QString();
    job->revision = m_scope.host().playback.sourceRevision();
    job->cover = source;
    if (transitionCover) {
        const std::weak_ptr<EchoPluginAssetJob> weak = job;
        connect(&presentation, &PlaybackTransitionPresentation::changed, job->operation,
                [this, weak] {
                    const auto current = weak.lock();
                    auto &active = m_scope.host().playback.transitionPresentation();
                    if (current && current->operation && !current->operation->finished() &&
                        (current->transitionEpoch != active.snapshot().value("epoch").toString() ||
                         !active.permitsCover(current->cover)))
                        current->operation->cancel();
                });
    } else if (currentCover) {
        const std::weak_ptr<EchoPluginAssetJob> weak = job;
        connect(m_scope.player(), &EchoPluginPlayer::changed, job->operation, [this, weak] {
            const auto current = weak.lock();
            if (current && current->operation && !current->operation->finished() &&
                (current->revision != m_scope.host().playback.sourceRevision() ||
                 current->cover != m_scope.player()->coverUrl()))
                current->operation->cancel();
        });
    }
    if (!remote) {
        run(job);
        return job->operation;
    }
    QNetworkRequest request(source);
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_network.get(request);
    reply->setReadBufferSize(65536);
    job->reply = reply;
    auto *deadline = new QTimer(this);
    deadline->setTimerType(Qt::PreciseTimer);
    deadline->setSingleShot(true);
    job->deadline = deadline;
    const std::weak_ptr<EchoPluginAssetJob> weak = job;
    connect(deadline, &QTimer::timeout, this, [weak] {
        if (const auto current = weak.lock();
            current && current->operation && !current->operation->finished()) {
            fail(*current, "Timeout", "Current cover exceeded the absolute 15 second deadline");
            if (current->reply)
                current->reply->abort();
        }
    });
    deadline->start(15000);
    connect(reply, &QNetworkReply::readyRead, this, [reply, job] {
        job->bytes.append(reply->read(8 * MiB + 1 - job->bytes.size()));
        if (job->bytes.size() > 8 * MiB) {
            fail(*job, "QuotaExceeded", "Current cover exceeds 8 MiB");
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
        if (job->deadline)
            job->deadline->stop();
        if (reply->isOpen())
            job->bytes.append(reply->read(8 * MiB + 1 - job->bytes.size()));
        if (job->error.isEmpty() && reply->error() != QNetworkReply::NoError)
            fail(*job, "NetworkError", "Cannot read current cover");
        reply->deleteLater();
        if (job->error.isEmpty() && !job->canceled->load())
            run(job);
        else
            complete(job);
    });
    return job->operation;
}

void EchoPluginAssets::complete(const std::shared_ptr<EchoPluginAssetJob> &job)
{
    if (!m_jobs.contains(job))
        return;
    const QPointer<EchoPluginAssets> self(this);
    bool publishedChange = false;
    bool retainArchive = false;
    const auto finish = qScopeGuard([self, job, &publishedChange, &retainArchive] {
        if (job->deadline) {
            job->deadline->stop();
            job->deadline->deleteLater();
        }
        if (job->save && job->save->isOpen())
            job->save->cancelWriting();
        job->save.reset();
        auto staging = std::move(job->staging);
        if (job->archive && staging == job->archive->directory)
            staging.reset();
        job->fonts.clear();
        job->bytes.clear();
        job->files.clear();
        job->source.clear();
        if (self) {
            self->m_jobs.removeOne(job);
            if (job->countPending)
                --self->m_store->pending;
            const auto observersAtPublication = self->m_store->observers;
            const auto publicationEpoch = job->epoch;
            const auto afterCleanup = [self, job, observersAtPublication, publicationEpoch,
                                       publishedChange] {
                job->storageFence.reset();
                job->destinationFence.reset();
                if (self) {
                    --self->m_storagePending;
                    self->checkStorageBarriers();
                }
                if (!publishedChange)
                    return;
                for (const auto &observer : observersAtPublication) {
                    if (!observer || observer->m_closed || !observer->m_scope.active() ||
                        observer->m_scope.accountEpoch() != publicationEpoch)
                        continue;
                    QMetaObject::invokeMethod(
                        observer,
                        [observer, publicationEpoch] {
                            if (observer && !observer->m_closed && observer->m_scope.active() &&
                                observer->m_scope.accountEpoch() == publicationEpoch)
                                emit observer->changed();
                        },
                        Qt::QueuedConnection);
                }
            };
            if (job->archive) {
                const auto session = job->archive;
                if (!retainArchive) {
                    session->closing = true;
                    const auto lease = session->lease;
                    session->lease = nullptr;
                    if (lease) {
                        lease->m_session.reset();
                        lease->m_ready = false;
                        lease->m_error = job->operation && !job->operation->errorMessage().isEmpty()
                                             ? job->operation->errorMessage()
                                         : job->command == InspectArchive
                                             ? "Archive selection canceled"
                                             : QString();
                        if (job->command == ImportArchive) {
                            lease->m_active = false;
                            self->m_archiveLeases.remove(lease);
                            lease->deleteLater();
                        }
                        emit lease->changed();
                    }
                }
                if (self)
                    self->cleanupDirectory(std::move(staging), 0, false,
                                           [self, session, afterCleanup] {
                                               if (self)
                                                   self->cleanupArchive(session, afterCleanup);
                                           });
            } else {
                self->cleanupDirectory(std::move(staging), 0, false, afterCleanup);
            }
            if (!self)
                return;
            const auto observers = self->m_store->observers;
            for (const auto &observer : observers)
                if (observer && !observer->m_closed)
                    QMetaObject::invokeMethod(
                        observer,
                        [observer] {
                            if (observer)
                                observer->processAnimationQueue();
                        },
                        Qt::QueuedConnection);
            self->checkStorageBarriers();
            if (self->isDrained())
                emit self->drained();
        }
    });
    const auto operation = job->operation;
    if (!operation || operation->finished() || job->canceled->load() || m_closed ||
        (job->archive && job->archiveClock.isValid() && job->archiveClock.hasExpired(60000)) ||
        !m_scope.active() || job->epoch != m_scope.accountEpoch() ||
        (!job->transitionEpoch.isEmpty() &&
         (job->transitionEpoch != m_scope.host()
                                      .playback.transitionPresentation()
                                      .snapshot()
                                      .value("epoch")
                                      .toString() ||
          !m_scope.host().playback.transitionPresentation().permitsCover(job->cover))) ||
        (job->trackScoped && (job->revision != m_scope.host().playback.sourceRevision() ||
                              job->cover != m_scope.player()->coverUrl()))) {
        if (operation && !operation->finished())
            operation->cancel();
        return;
    }
    if (!job->error.isEmpty()) {
        operation->fail(job->code, job->error);
        return;
    }
    if (!m_store->loaded) {
        m_store->assets = job->loaded;
        m_store->loaded = true;
    }
    if (job->command == InspectArchive) {
        const auto session = job->archive;
        const auto lease = session ? session->lease : nullptr;
        if (!lease || !lease->m_active || session->closing) {
            operation->cancel();
            return;
        }
        session->entries = job->sample.value("entries").toList();
        session->json = job->sample.value("jsonFiles").toMap();
        session->pendingHeld = true;
        job->countPending = false;
        retainArchive = true;
        lease->m_entries = session->entries;
        lease->m_json = session->json;
        lease->m_ready = true;
        emit lease->changed();
        operation->succeed();
        return;
    }
    if (job->command == ExportArchive) {
        if (!job->save || !job->save->commit())
            operation->fail("AssetStorageError", "Cannot atomically publish selected archive");
        else
            operation->succeed(QVariantMap{{"exported", job->selections.size()},
                                           {"bytes", job->sample.value("bytes")}});
        return;
    }
    if (job->command == Frames) {
        const auto cache = job->animation.lock();
        if (!cache || m_animations.value(cache->key) != cache) {
            operation->cancel();
            return;
        }
        const auto bytes = job->sample.value("encodedBytes").toLongLong();
        if (bytes <= 0 || m_store->animationBytes + bytes > 64 * MiB) {
            operation->fail("QuotaExceeded", "Installation animation PNG caches exceed 64 MiB");
            return;
        }
        cache->encodedBytes = bytes;
        m_store->animationBytes += bytes;
        cache->directory = std::move(job->staging);
        cache->width = job->sample.value("width").toInt();
        cache->height = job->sample.value("height").toInt();
        cache->loopCount = job->sample.value("loopCount").toInt();
        for (const auto &delay : job->sample.value("delays").toList()) {
            cache->duration += delay.toLongLong();
            cache->ends.append(cache->duration);
        }
        cache->ready = true;
        operation->succeed();
        return;
    }
    auto next = m_store->assets;
    QVariantList removed;
    if (job->command == Remove || job->command == Clear) {
        for (qsizetype i = next.size(); i-- > 0;) {
            const auto asset = next[i].toMap();
            if ((job->command == Remove && asset.value("handle") == job->value) ||
                (job->command == Clear && asset.value("collection") == job->value)) {
                removed.append(next[i]);
                next.removeAt(i);
            }
        }
        if (job->command == Remove && removed.isEmpty()) {
            operation->fail("NotFound", "Asset handle does not exist");
            return;
        }
    }
    next.append(job->added);
    qint64 total = 0;
    int fonts = 0;
    for (const auto &value : next) {
        const auto asset = value.toMap();
        total += storedBytes(asset);
        fonts += asset.value("kind") == "font";
    }
    if (next.size() > 128 || fonts > 16 || total > 64 * MiB) {
        operation->fail("QuotaExceeded", "Installation assets exceed count, font or 64 MiB budget");
        return;
    }
    const auto visible =
        job->command == ImportImages || job->command == ImportFonts || job->command == ImportArchive
            ? job->added
            : m_store->assets;
    QHash<QString, int> registrations;
    QStringList references;
    bool fontsCommitted = false;
    const auto rollbackFonts = qScopeGuard([&] {
        if (!fontsCommitted)
            for (const auto id : registrations)
                QFontDatabase::removeApplicationFont(id);
    });
    for (const auto &value : visible) {
        const auto asset = value.toMap();
        const auto handle = asset.value("handle").toString();
        if (asset.value("collection") != job->value || asset.value("kind") != "font" ||
            m_fontReferences.contains(handle))
            continue;
        if (!m_store->fonts.contains(handle)) {
            const int id = QFontDatabase::addApplicationFontFromData(job->fonts.value(handle));
            if (id < 0) {
                operation->fail("AssetDecodeError", "Imported font could not be registered");
                return;
            }
            registrations.insert(handle, id);
        }
        references.append(handle);
        // Font database notification can reenter QML. It precedes all file
        // publication and therefore receives a second cancellation check.
        if (!operation || operation->finished() || m_closed || !m_scope.active() ||
            job->epoch != m_scope.accountEpoch())
            return;
    }
    QStringList published;
    QList<QPair<QString, QString>> retiredFiles;
    const auto rollbackFiles = qScopeGuard([&] {
        if (!publishedChange)
            for (auto iterator = retiredFiles.crbegin(); iterator != retiredFiles.crend();
                 ++iterator)
                QFile::rename(iterator->second, iterator->first);
    });
    for (const auto &value : removed) {
        const auto asset = value.toMap();
        QStringList names{assetFile(asset)};
        if (asset.value("renderBytes").toLongLong() > 0)
            names.append(renderFile(asset));
        for (const auto &name : names) {
            const auto from = QDir(m_store->directory).filePath(name);
            const auto to = QDir(job->staging->path()).filePath(name);
            if (!QFile::rename(from, to)) {
                operation->fail("AssetStorageError", "Cannot retire managed asset files");
                return;
            }
            retiredFiles.append({from, to});
        }
    }
    for (const auto &value : job->added) {
        const auto asset = value.toMap();
        QStringList names{assetFile(asset)};
        if (asset.value("renderBytes").toLongLong() > 0)
            names.append(renderFile(asset));
        for (const auto &name : names) {
            if (!QFile::rename(QDir(job->staging->path()).filePath(name),
                               QDir(m_store->directory).filePath(name))) {
                for (const auto &path : published)
                    QFile::remove(path);
                operation->fail("AssetStorageError", "Cannot publish validated asset");
                return;
            }
            published.append(QDir(m_store->directory).filePath(name));
        }
    }
    // GUI-thread publication is the only persistent mutation. No event loop is
    // entered between checking cancellation and the bounded rename/index commit.
    if (job->command == Remove || job->command == Clear || !job->added.isEmpty()) {
        QString error;
        if (!persist(*m_store, next, error)) {
            for (const auto &path : published)
                QFile::remove(path);
            operation->fail("AssetStorageError", error);
            return;
        }
        publishedChange = true;
        // Derived frames must not survive removal of their owned original.
        const auto observers = m_store->observers;
        for (const auto &observer : observers) {
            if (!observer)
                continue;
            const auto caches = observer->m_animations.values();
            for (const auto &cache : caches)
                for (const auto &value : removed) {
                    const auto asset = value.toMap();
                    if (cache->source.toString() == asset.value("handle").toString() ||
                        cache->source ==
                            publicAsset(asset, m_store->directory).value("url").toUrl()) {
                        const auto leases = cache->leases;
                        for (const auto &lease : leases)
                            if (lease)
                                lease->dispose();
                        break;
                    }
                }
        }
    }
    for (const auto &handle : references) {
        auto &font = m_store->fonts[handle];
        if (registrations.contains(handle))
            font.id = registrations.value(handle);
        ++font.references;
        m_fontReferences.insert(handle);
    }
    fontsCommitted = true;
    publishedChange = job->command == Remove || job->command == Clear || !job->added.isEmpty();
    QVariantList assets;
    int visibleIndex = 0;
    for (const auto &value : visible) {
        const auto asset = value.toMap();
        if (asset.value("collection") != job->value)
            continue;
        auto descriptor = publicAsset(asset, m_store->directory);
        if (job->command == ImportArchive)
            descriptor["entryPath"] = job->selections.value(visibleIndex).toMap().value("path");
        ++visibleIndex;
        assets.append(descriptor);
    }
    if (job->command == Sample)
        operation->succeed(job->sample);
    else if (job->command == Remove || job->command == Clear)
        operation->succeed();
    else if (job->command == ImportImages && job->partial)
        operation->succeed(QVariantMap{{"assets", assets}, {"rejected", job->rejected}});
    else
        operation->succeed(QVariantMap{{"assets", assets}});
}
EchoPluginAssetAnimation::EchoPluginAssetAnimation(EchoPluginAssets &assets, QString key)
    : QObject(&assets), m_assets(&assets), m_key(std::move(key))
{
}
EchoPluginAssetAnimation::~EchoPluginAssetAnimation()
{
    if (m_assets && m_active)
        m_assets->releaseAnimation(this);
}
void EchoPluginAssetAnimation::setPlaying(bool playing)
{
    if (!m_active || m_playing == playing)
        return;
    m_playing = playing;
    emit playingChanged();
    if (m_assets)
        if (const auto cache = m_assets->m_animations.value(m_key))
            m_assets->updateAnimation(cache);
}
void EchoPluginAssetAnimation::dispose()
{
    if (!m_active)
        return;
    m_active = false;
    m_ready = false;
    m_frameSource = QUrl();
    m_frameIndex = -1;
    if (m_assets)
        m_assets->releaseAnimation(this);
    emit frameChanged();
    emit changed();
    deleteLater();
}

EchoPluginAssetAnimation *EchoPluginAssets::animation(const QUrl &source, int width, int height,
                                                      const QString &fit)
{
    if (m_closed || !m_scope.active() || m_animationLeases.size() >= 128)
        return nullptr;
    const bool cover = !source.isEmpty() && source == m_scope.player()->coverUrl();
    auto &presentation = m_scope.host().playback.transitionPresentation();
    const auto transitionEpoch = presentation.permitsCover(source)
                                     ? presentation.snapshot().value("epoch").toString()
                                     : QString();
    const auto revision = m_scope.host().playback.sourceRevision();
    const auto key = source.toString(QUrl::FullyEncoded) + '|' + QString::number(width) + 'x' +
                     QString::number(height) + '|' + fit +
                     (!transitionEpoch.isEmpty() ? "|transition:" + transitionEpoch
                      : cover                    ? '|' + QString::number(revision)
                                                 : QString());
    auto *lease = new EchoPluginAssetAnimation(*this, key);
    QQmlEngine::setObjectOwnership(lease, QQmlEngine::CppOwnership);
    m_animationLeases.insert(lease);
    if (source.isEmpty() || width < 1 || width > 4096 || height < 1 || height > 4096 ||
        (fit != "contain" && fit != "cover")) {
        lease->m_error = "Animation dimensions must be 1..4096 and fit must be contain or cover";
        return lease;
    }
    auto cache = m_animations.value(key);
    if (!cache) {
        if (m_store->animationEntries >= 32) {
            lease->m_error = "At most 32 animation caches may exist per installation";
            return lease;
        }
        cache = std::make_shared<EchoPluginAnimationCache>();
        cache->key = key;
        cache->source = source;
        cache->fit = fit;
        cache->width = width;
        cache->height = height;
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setTimerType(Qt::PreciseTimer);
        cache->timer = timer;
        const std::weak_ptr<EchoPluginAnimationCache> weak = cache;
        connect(timer, &QTimer::timeout, this, [this, weak] {
            if (const auto current = weak.lock())
                updateAnimation(current);
        });
        if (!transitionEpoch.isEmpty())
            cache->sourceConnection =
                connect(&presentation, &PlaybackTransitionPresentation::changed, this,
                        [this, weak, source, transitionEpoch] {
                            auto &active = m_scope.host().playback.transitionPresentation();
                            if (const auto current = weak.lock();
                                current &&
                                (transitionEpoch != active.snapshot().value("epoch").toString() ||
                                 !active.permitsCover(source))) {
                                const auto leases = current->leases;
                                for (const auto &value : leases)
                                    if (value)
                                        value->dispose();
                            }
                        });
        else if (cover)
            cache->sourceConnection = connect(
                m_scope.player(), &EchoPluginPlayer::changed, this, [this, weak, source, revision] {
                    if (const auto current = weak.lock();
                        current && (source != m_scope.player()->coverUrl() ||
                                    (!current->ready &&
                                     revision != m_scope.host().playback.sourceRevision()))) {
                        const auto leases = current->leases;
                        for (const auto &value : leases)
                            if (value)
                                value->dispose();
                    }
                });
        ++m_store->animationEntries;
        m_animations.insert(key, cache);
    }
    cache->leases.append(lease);
    if (cache->ready || !cache->error.isEmpty())
        updateAnimation(cache);
    QMetaObject::invokeMethod(this, &EchoPluginAssets::processAnimationQueue, Qt::QueuedConnection);
    return lease;
}

void EchoPluginAssets::processAnimationQueue()
{
    if (m_closed || !m_scope.active() || m_scope.storageFrozen())
        return;
    const auto caches = m_animations.values();
    for (const auto &cache : caches) {
        if (m_store->pending >= 2)
            break;
        if (cache->started || m_animations.value(cache->key) != cache)
            continue;
        cache->started = true;
        auto *op = imageRequest(Frames, cache->source, cache->width, cache->height, cache->fit);
        cache->operation = op;
        for (const auto &job : std::as_const(m_jobs))
            if (job->operation == op) {
                job->animation = cache;
                break;
            }
        const std::weak_ptr<EchoPluginAnimationCache> weak = cache;
        const auto completed = [this, weak] {
            const auto current = weak.lock();
            if (!current || m_animations.value(current->key) != current)
                return;
            const auto operation = current->operation;
            if (!operation || operation->state() != EchoPluginOperation::Succeeded)
                current->error =
                    operation ? operation->errorMessage() : "Animation preparation ended";
            updateAnimation(current);
        };
        if (op->finished())
            completed();
        else
            connect(op, &EchoPluginOperation::completed, this, completed);
    }
}

void EchoPluginAssets::updateAnimation(const std::shared_ptr<EchoPluginAnimationCache> &cache)
{
    if (m_animations.value(cache->key) != cache)
        return;
    bool playing = false;
    for (const auto &lease : std::as_const(cache->leases))
        playing |= lease && lease->active() && lease->playing();
    qint64 elapsed = cache->position + (cache->running ? cache->clock.elapsed() : 0);
    if (cache->ready && cache->duration > 0) {
        const qint64 plays = qint64(cache->loopCount) + 1;
        if (cache->loopCount >= 0 && elapsed >= plays * cache->duration) {
            cache->finished = true;
            cache->frameIndex = cache->ends.size() - 1;
        } else {
            const auto phase = elapsed % cache->duration;
            cache->frameIndex = std::upper_bound(cache->ends.cbegin(), cache->ends.cend(), phase) -
                                cache->ends.cbegin();
        }
    }
    const bool running = playing && cache->ready && cache->ends.size() > 1 && !cache->finished;
    if (running != cache->running) {
        cache->position = elapsed;
        cache->running = running;
        if (running)
            cache->clock.start();
    }
    if (cache->timer) {
        cache->timer->stop();
        if (running) {
            const auto remaining = cache->ends[cache->frameIndex] - elapsed % cache->duration;
            cache->timer->start(int(qBound<qint64>(1LL, remaining, 2147483647LL)));
        }
    }
    const auto frame =
        cache->ready ? QUrl::fromLocalFile(
                           QDir(cache->directory->path())
                               .filePath("frames/" + QString::number(cache->frameIndex) + ".png"))
                     : QUrl();
    const auto leases = cache->leases;
    for (const auto &value : leases) {
        auto *lease = value.data();
        if (!lease || !lease->active())
            continue;
        const bool metadataChanged = lease->m_ready != cache->ready ||
                                     lease->m_error != cache->error ||
                                     lease->m_encodedBytes != cache->encodedBytes;
        lease->m_ready = cache->ready;
        lease->m_error = cache->error;
        lease->m_width = cache->width;
        lease->m_height = cache->height;
        lease->m_frameCount = cache->ends.size();
        lease->m_duration = cache->duration;
        lease->m_loopCount = cache->loopCount;
        lease->m_encodedBytes = cache->encodedBytes;
        if (metadataChanged)
            emit lease->changed();
        if (value && lease->m_active && (lease->m_playing || lease->m_frameIndex < 0) &&
            lease->m_frameSource != frame) {
            lease->m_frameIndex = cache->ready ? cache->frameIndex : -1;
            lease->m_frameSource = frame;
            emit lease->frameChanged();
        }
    }
}

void EchoPluginAssets::releaseAnimation(EchoPluginAssetAnimation *lease)
{
    m_animationLeases.remove(lease);
    const auto cache = m_animations.value(lease->m_key);
    if (cache) {
        cache->leases.removeIf([lease](const auto &value) { return !value || value == lease; });
        if (cache->leases.isEmpty()) {
            m_animations.remove(cache->key);
            QObject::disconnect(cache->sourceConnection);
            if (cache->timer) {
                cache->timer->stop();
                cache->timer->deleteLater();
            }
            if (cache->operation && !cache->operation->finished())
                cache->operation->cancel();
            if (cache->directory)
                cleanupDirectory(std::move(cache->directory), cache->encodedBytes, true);
            else
                --m_store->animationEntries;
        } else
            updateAnimation(cache);
    }
    checkStorageBarriers();
    if (isDrained())
        emit drained();
}

void EchoPluginAssets::cleanupDirectory(std::shared_ptr<QTemporaryDir> directory, qint64 bytes,
                                        bool cacheEntry, std::function<void()> completion)
{
    if (!directory) {
        if (completion)
            completion();
        return;
    }
    ++m_cleanupCount;
    auto *watcher = new QFutureWatcher<void>(this);
    const auto store = m_store;
    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, watcher, store, bytes, cacheEntry, completion = std::move(completion)] {
                watcher->deleteLater();
                store->animationBytes -= bytes;
                store->animationEntries -= cacheEntry;
                --m_cleanupCount;
                if (completion)
                    completion();
                checkStorageBarriers();
                if (isDrained())
                    emit drained();
            });
    // The last temporary-directory reference and recursive deletion both run
    // away from the GUI thread, including cancelled or rejected preparations.
    watcher->setFuture(
        QtConcurrent::run([directory = std::move(directory)]() mutable { directory.reset(); }));
}

void EchoPluginAssets::releaseFonts()
{
    for (const auto &handle : std::as_const(m_fontReferences)) {
        auto it = m_store->fonts.find(handle);
        if (it != m_store->fonts.end() && --it->references == 0) {
            QFontDatabase::removeApplicationFont(it->id);
            m_store->fonts.erase(it);
        }
    }
    m_fontReferences.clear();
}
void EchoPluginAssets::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    const auto animations = m_animationLeases;
    for (auto *lease : animations)
        lease->dispose();
    const auto archives = m_archiveLeases;
    for (auto *lease : archives)
        lease->dispose();
    const auto jobs = m_jobs;
    for (const auto &job : jobs) {
        job->canceled->store(true);
        if (job->operation && !job->operation->finished())
            job->operation->cancel();
    }
    releaseFonts();
    if (m_dialog)
        m_dialog->close();
    checkStorageBarriers();
    if (isDrained())
        emit drained();
}
} // namespace EchoMusic
