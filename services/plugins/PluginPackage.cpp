#include "PluginPackage.h"
#include "echo_ffi.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QObject>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>
#include <QTemporaryDir>
#include <QVersionNumber>
#include <algorithm>

namespace EchoMusic
{
QString PluginPackage::extractArchive(const QString &archive, const QString &destination)
{
    if (!QDir().mkpath(destination))
        return QObject::tr("无法创建插件解压目录");
    char error[1024]{};
    if (!echo_plugin_extract_zip(archive.toUtf8().constData(), destination.toUtf8().constData(),
                                 reinterpret_cast<uint8_t *>(error), sizeof(error)))
        return QString::fromUtf8(error);
    return {};
}
QVersionNumber PluginPackage::hostVersion()
{
    return {0, 1, 0};
}
QVersionNumber PluginPackage::sdkVersion()
{
    return {1, 0, 0};
}
QStringList PluginPackage::supportedFeatures()
{
    return {"lifecycle",
            "operations",
            "playback.controls",
            "playback.queue",
            "playback.lyrics",
            "appearance.read",
            "commands",
            "events",
            "timers",
            "ui.pages",
            "ui.titlebar",
            "ui.playerbar",
            "tasks",
            "storage",
            "net.http",
            "ui.settings",
            "plugins.dependencies",
            "plugins.services",
            "audio.spectrum",
            "ui.surfaces",
            "ui.state",
            "ui.parts",
            "ui.styles",
            "appearance.themes",
            "appearance.snapshot",
            "appearance.fonts",
            "ui.lyricsPage",
            "windows.floating",
            "playback.context"};
}
QStringList PluginPackage::supportedCapabilities()
{
    return {"unrestrictedNetwork", "audioSpectrum", "theme", "lyricsPage"};
}
namespace
{
bool rangeContains(const QString &range, const QVersionNumber &version)
{
    // SDK v1 deliberately specifies comparator intersections, not npm's entire
    // grammar. Unsupported syntax fails closed instead of ignoring a constraint.
    if (range.isEmpty())
        return false;
    const QRegularExpression expression("^(>=|<=|>|<|=)?([0-9]+\\.[0-9]+\\.[0-9]+)$");
    for (const auto &part : range.split(' ', Qt::SkipEmptyParts)) {
        const auto match = expression.match(part);
        if (!match.hasMatch())
            return false;
        const int comparison =
            QVersionNumber::compare(version, QVersionNumber::fromString(match.captured(2)));
        const auto op = match.captured(1);
        if ((op == ">=" && comparison < 0) || (op == "<=" && comparison > 0) ||
            (op == ">" && comparison <= 0) || (op == "<" && comparison >= 0) ||
            ((op.isEmpty() || op == "=") && comparison != 0))
            return false;
    }
    return true;
}
bool strings(const QJsonValue &value, QStringList &out)
{
    if (!value.isArray())
        return false;
    for (const auto &item : value.toArray()) {
        if (!item.isString() || item.toString().isEmpty() || out.contains(item.toString()))
            return false;
        out.append(item.toString());
    }
    return true;
}
} // namespace
bool PluginPackage::validVersionRange(const QString &range)
{
    return QRegularExpression("^(>=|<=|>|<|=)?[0-9]+\\.[0-9]+\\.[0-9]+( "
                              "+(>=|<=|>|<|=)?[0-9]+\\.[0-9]+\\.[0-9]+)*$")
        .match(range)
        .hasMatch();
}
bool PluginPackage::versionMatches(const QString &range, const QString &version)
{
    return validVersionRange(range) &&
           QRegularExpression("^[0-9]+\\.[0-9]+\\.[0-9]+$").match(version).hasMatch() &&
           rangeContains(range, QVersionNumber::fromString(version));
}
QString PluginPackage::resolve(const QString &relative) const
{
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains('\\'))
        return {};
    const auto clean = QDir::cleanPath(relative);
    if (clean == ".." || clean.startsWith("../") || clean == ".")
        return {};
    const auto path = QFileInfo(QDir(root).filePath(clean)).canonicalFilePath();
    return path.startsWith(root + '/') && QFileInfo(path).isFile() ? path : QString();
}
PluginPackage PluginPackage::inspectManifest(const QByteArray &bytes)
{
    PluginPackage result;
    auto fail = [&](const QString &message) {
        result.error = message;
        return result;
    };
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return fail("Invalid manifest JSON");
    const auto object = document.object();
    result.manifest = object.toVariantMap();
    result.id = object.value("id").toString();
    result.name = object.value("name").toString();
    result.version = object.value("version").toString();
    result.entry = object.value("entry").toString();
    if (object.value("format").toString() != "echomod" ||
        object.value("schemaVersion").toInt() != 1 || object.value("kind").toString() != "qml-js" ||
        !QRegularExpression("^[a-z][a-z0-9.-]{1,127}$").match(result.id).hasMatch() ||
        result.name.isEmpty() || result.name.size() > 256 ||
        !QRegularExpression("^[0-9]+\\.[0-9]+\\.[0-9]+$").match(result.version).hasMatch() ||
        !result.entry.endsWith(".mjs") || QDir::isAbsolutePath(result.entry) ||
        result.entry.contains("..") || result.entry.contains('\\'))
        return fail("Invalid SDK v1 manifest fields");
    const auto readDependencies = [&](const QString &key, QMap<QString, QString> &out) {
        if (!object.contains(key))
            return true;
        if (!object.value(key).isObject() || object.value(key).toObject().size() > 128)
            return false;
        const auto values = object.value(key).toObject();
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (it.key() == result.id ||
                !QRegularExpression("^[a-z][a-z0-9.-]{1,127}$").match(it.key()).hasMatch() ||
                !it.value().isString() || !validVersionRange(it.value().toString()))
                return false;
            out.insert(it.key(), it.value().toString());
        }
        return true;
    };
    if (!readDependencies("dependencies", result.dependencies) ||
        !readDependencies("optionalDependencies", result.optionalDependencies) ||
        !readDependencies("conflicts", result.conflicts))
        return fail("Invalid plugin dependency declaration");
    for (const auto &id : result.dependencies.keys())
        if (result.optionalDependencies.contains(id) || result.conflicts.contains(id))
            return fail("Contradictory plugin dependency declaration: " + id);
    for (const auto &id : result.optionalDependencies.keys())
        if (result.conflicts.contains(id))
            return fail("Contradictory optional plugin dependency: " + id);
    for (const auto &key : {QStringLiteral("loadAfter"), QStringLiteral("loadBefore")}) {
        QStringList values;
        if (object.contains(key) && (!strings(object.value(key), values) || values.size() > 128))
            return fail("Invalid plugin load order");
        for (const auto &id : values)
            if (id == result.id ||
                !QRegularExpression("^[a-z][a-z0-9.-]{1,127}$").match(id).hasMatch())
                return fail("Invalid plugin load order target");
        (key == "loadAfter" ? result.loadAfter : result.loadBefore) = values;
    }
    QStringList tags;
    if (object.contains("tags") && (!strings(object.value("tags"), tags) || tags.size() > 12))
        return fail("Invalid plugin tags");
    for (const auto &tag : tags)
        if (tag.size() > 64)
            return fail("Plugin tag exceeds 64 characters");
    if (object.contains("icon") &&
        (!object.value("icon").isString() || object.value("icon").toString().contains("..") ||
         QDir::isAbsolutePath(object.value("icon").toString()) ||
         object.value("icon").toString().contains('\\')))
        return fail("Invalid plugin icon path");
    const auto requirements = object.value("requires").toObject();
    if (!strings(object.value("capabilities"), result.capabilities) ||
        !strings(requirements.value("features"), result.features))
        return fail("Capabilities and required features must be unique string arrays");
    const auto contexts = object.value("contexts").toArray();
    if (contexts != QJsonArray{"main"}) {
        result.incompatible = true;
        return fail("This runtime currently provides the main context only");
    }
    if (!rangeContains(requirements.value("hostVersion").toString(), hostVersion()) ||
        !rangeContains(requirements.value("sdkVersion").toString(), sdkVersion())) {
        result.incompatible = true;
        return fail("Host or SDK version requirement is not satisfied");
    }
    for (const auto &feature : result.features)
        if (!supportedFeatures().contains(feature)) {
            result.incompatible = true;
            return fail("Required feature is unavailable: " + feature);
        }
    for (const auto &capability : result.capabilities)
        if (!supportedCapabilities().contains(capability)) {
            result.incompatible = true;
            return fail("Required capability is unavailable: " + capability);
        }
    return result;
}
PluginPackage PluginPackage::importDirectory(const QString &source, const QString &packages,
                                             const QString &installationId)
{
    if (QFileInfo(source).isFile()) {
        PluginPackage failed;
        failed.installationId = installationId;
        if (!source.endsWith(".echomod", Qt::CaseInsensitive)) {
            failed.error = "Expected an .echomod package";
            return failed;
        }
        QTemporaryDir staging;
        failed.error = staging.isValid()
                           ? extractArchive(source, staging.path())
                           : QStringLiteral("Cannot create plugin extraction directory");
        if (!failed.error.isEmpty())
            return failed;
        return importDirectory(staging.path(), packages, installationId);
    }
    PluginPackage result;
    result.installationId = installationId;
    result.root = QFileInfo(source).canonicalFilePath();
    auto fail = [&](const QString &message) {
        result.error = message;
        return result;
    };
    if (result.root.isEmpty() || !QFileInfo(result.root).isDir())
        return fail("Package directory does not exist");
    QFile manifestFile(result.root + "/manifest.json");
    if (!manifestFile.open(QIODevice::ReadOnly) || manifestFile.size() > 256 * 1024)
        return fail("Missing or oversized manifest.json");
    const auto manifestBytes = manifestFile.readAll();
    const auto inspected = inspectManifest(manifestBytes);
    result = inspected;
    result.root = QFileInfo(source).canonicalFilePath();
    result.installationId = installationId;
    if (!result.error.isEmpty())
        return result;
    if (result.resolve(result.entry).isEmpty())
        return fail("Missing package entry");
    QStringList files;
    QDirIterator iterator(result.root,
                          QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    qint64 bytes = 0;
    while (iterator.hasNext()) {
        iterator.next();
        const auto info = iterator.fileInfo();
        if (info.isSymLink())
            return fail("Package symlinks are not supported");
        if (!info.isFile())
            continue;
        bytes += info.size();
        if (bytes > 128 * 1024 * 1024 || files.size() >= 4096)
            return fail("Package exceeds 128 MiB or 4096 files");
        files.append(QDir(result.root).relativeFilePath(info.filePath()));
    }
    std::sort(files.begin(), files.end());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QList<QByteArray> contents;
    qint64 capturedBytes = 0;
    for (const auto &relative : files) {
        QFile file(result.root + '/' + relative);
        if (!file.open(QIODevice::ReadOnly))
            return fail("Cannot read package file: " + relative);
        const auto data = file.read(128 * 1024 * 1024 - capturedBytes + 1);
        capturedBytes += data.size();
        if (capturedBytes > 128 * 1024 * 1024 || !file.atEnd())
            return fail("Package changed or exceeded its byte limit during capture");
        if (relative == "manifest.json" && data != manifestBytes)
            return fail("Manifest changed during capture; retry import");
        if (QFileInfo(relative).fileName() == "qmldir" &&
            QRegularExpression("(?m)^\\s*(optional\\s+)?plugin\\s+")
                .match(QString::fromUtf8(data))
                .hasMatch())
            return fail("UI packages cannot load native QML plugins");
        hash.addData(relative.toUtf8());
        hash.addData(QByteArray(1, '\0'));
        hash.addData(QByteArray::number(data.size()));
        hash.addData(QByteArray(1, '\0'));
        hash.addData(data);
        contents.append(data);
    }
    result.contentHash = QString::fromLatin1(hash.result().toHex());
    const auto parent = QDir(packages).absoluteFilePath(installationId);
    const auto destination = parent + '/' + result.contentHash;
    if (!QDir().mkpath(parent))
        return fail("Cannot create package store");
    if (!QDir(destination).exists()) {
        const auto staging =
            parent + "/.staging-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(staging);
        for (qsizetype i = 0; i < files.size(); ++i) {
            const auto target = staging + '/' + files[i];
            QDir().mkpath(QFileInfo(target).absolutePath());
            QFile file(target);
            if (!file.open(QIODevice::WriteOnly) || file.write(contents[i]) != contents[i].size()) {
                QDir(staging).removeRecursively();
                return fail("Cannot write installed package");
            }
            file.close();
            file.setPermissions(QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther);
        }
        if (!QDir().rename(staging, destination)) {
            QDir(staging).removeRecursively();
            return fail("Cannot publish installed package");
        }
    }
    result.root = QFileInfo(destination).canonicalFilePath();
    return result;
}
} // namespace EchoMusic
