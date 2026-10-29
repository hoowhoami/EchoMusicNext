#include "PortableSettingsCodec.h"
#include "PluginPaths.h"
#include "SettingsKeys.h"
#include "SettingsBackupArchive.h"
#include "StorageSnapshot.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include "SettingsStore.h"

namespace EchoMusic
{
namespace
{
constexpr qint64 byteLimit = 512ll * 1024 * 1024;
constexpr qsizetype manifestLimit = 4 * 1024 * 1024;
constexpr int fileLimit = 4096;
const QByteArray magic("ECHONEXTBACKUP\x01\n", 16);
bool safePath(const QString &path)
{
    if (path.isEmpty() || path.size() > 1024 || path.contains('\\') || path.contains(':') ||
        path.contains(QChar(0)) || path.startsWith('/'))
        return false;
    for (const auto &part : path.split('/'))
        if (part.isEmpty() || part == "." || part == "..")
            return false;
    return true;
}
bool writeJson(QIODevice &device, const QJsonObject &object)
{
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QDataStream stream(&device);
    stream << quint32(bytes.size());
    return stream.status() == QDataStream::Ok && device.write(bytes) == bytes.size();
}
QJsonObject readJson(QIODevice &device, qsizetype maximum, QString *error)
{
    quint32 length = 0;
    QDataStream stream(&device);
    stream >> length;
    if (stream.status() != QDataStream::Ok || length > maximum ||
        length > device.bytesAvailable()) {
        *error = "Invalid backup record size";
        return {};
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(device.read(length), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        *error = "Invalid backup JSON record";
    return document.object();
}
QString copyTree(const QString &source, const QString &destination,
                 const std::atomic_bool &cancelled)
{
    if (!QFileInfo::exists(source))
        return {};
    QDirIterator files(source, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot,
                       QDirIterator::Subdirectories);
    qint64 bytes = 0;
    int count = 0;
    while (files.hasNext()) {
        files.next();
        if (cancelled)
            return "Backup cancelled";
        const auto info = files.fileInfo();
        if (info.isSymLink())
            return "Backup cannot contain symbolic links";
        const auto target = destination + '/' + QDir(source).relativeFilePath(info.filePath());
        if (info.isDir()) {
            if (!QDir().mkpath(target))
                return "Cannot create backup directory";
            continue;
        }
        if (!info.isFile() || ++count > fileLimit || (bytes += info.size()) > byteLimit)
            return "Backup file limit exceeded";
        if (!QDir().mkpath(QFileInfo(target).absolutePath()) ||
            !QFile::copy(info.filePath(), target))
            return "Cannot copy backup file";
    }
    return {};
}
} // namespace
QJsonObject SettingsBackupArchive::encodePreferences(const QVariantMap &values, QString *error,
                                                     const QString &filesRoot)
{
    return PortableSettingsCodec::encode(values, error, filesRoot);
}
QVariantMap SettingsBackupArchive::decodePreferences(const QJsonObject &values, QString *error,
                                                     const QString &filesRoot)
{
    return PortableSettingsCodec::decode(values, error, filesRoot);
}
void SettingsBackupArchive::applyPreferences(EchoMusic::SettingsStore &settings,
                                             const QVariantMap &values)
{
    settings.replacePortable(values);
}
SettingsBackupResult
SettingsBackupArchive::create(const QString &destination, const QString &staging,
                              const QJsonObject &preferences, bool includeSettings,
                              const QVariantList &plugins, bool includePlugins,
                              const QString &pluginRoot, const QString &filesRoot,
                              const QString &version, const std::atomic_bool &cancelled)
{
    SettingsBackupResult result;
    auto &error = result.error;
    if (!includeSettings && !includePlugins)
        return {"Choose at least one backup component", {}};
    if (includeSettings) {
        decodePreferences(preferences, &error);
        if (!error.isEmpty())
            return result;
    }
    qint64 stagedBytes = 0;
    int stagedEntries = 0;
    const auto reserve = [&](const QString &path) -> QString {
        if (!QFileInfo::exists(path))
            return {};
        QDirIterator entries(path, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot,
                             QDirIterator::Subdirectories);
        while (entries.hasNext()) {
            entries.next();
            if (cancelled)
                return "Backup cancelled";
            const auto info = entries.fileInfo();
            if (info.isSymLink() || ++stagedEntries > fileLimit ||
                QDir(path).relativeFilePath(info.filePath()).size() > 1024)
                return "Backup entry limit exceeded";
            if (info.isFile()) {
                if (info.size() > byteLimit - stagedBytes)
                    return "Backup exceeds 512 MiB";
                stagedBytes += info.size();
            }
        }
        return {};
    };
    // Bound the entire capture before copying any package or VACUUM snapshot;
    // independent per-plugin limits alone could stage many gigabytes first.
    if (includeSettings)
        error = reserve(filesRoot);
    if (includePlugins)
        for (const auto &value : plugins) {
            if (!error.isEmpty())
                break;
            const auto plugin = value.toMap();
            error = reserve(plugin.value("root").toString());
            if (error.isEmpty())
                error = reserve(PluginPaths(pluginRoot).data(plugin.value("id").toString()));
        }
    if (!error.isEmpty())
        return result;
    QDir().mkpath(staging);
    QJsonArray records;
    if (includePlugins) {
        if (plugins.size() > 128)
            return {"Too many installed plugins for one backup", {}};
        for (int i = 0; i < plugins.size(); ++i) {
            if (cancelled)
                return {"Backup cancelled", {}};
            const auto plugin = plugins[i].toMap();
            const auto prefix = QString(EchoMusic::SettingsKeys::Plugins::EntryFormat).arg(i);
            error = copyTree(plugin.value("root").toString(), staging + '/' + prefix + "/package",
                             cancelled);
            if (!error.isEmpty())
                return result;
            error = StorageSnapshot::capture(
                PluginPaths(pluginRoot).data(plugin.value("id").toString()),
                staging + '/' + prefix + "/data");
            if (!error.isEmpty())
                return result;
            records.append(
                QJsonObject{{"id", plugin.value("id").toString()},
                            {"name", plugin.value("name").toString()},
                            {"origin", QJsonObject::fromVariantMap(plugin.value("origin").toMap())},
                            {"enabled", plugin.value("enabled").toBool()},
                            {"prefix", prefix}});
        }
    }
    if (includeSettings) {
        error = StorageSnapshot::capture(filesRoot, staging + "/files");
        if (!error.isEmpty())
            return result;
    }
    result.manifest = {
        {"format", "echomusicnext-backup"},
        {"storageSchema", "owned-domains"},
        {"version", 1},
        {"appVersion", version},
        {"createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"includes", QJsonObject{{"settings", includeSettings}, {"plugins", includePlugins}}},
        {"settings", includeSettings ? preferences : QJsonObject{}},
        {"plugins", records}};
    if (QJsonDocument(result.manifest).toJson(QJsonDocument::Compact).size() > manifestLimit)
        return {"Backup metadata exceeds its limit", {}};
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly))
        return {output.errorString(), {}};
    output.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (output.write(magic) != magic.size() || !writeJson(output, result.manifest))
        return {"Cannot write backup header", {}};
    QDirIterator files(staging, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot,
                       QDirIterator::Subdirectories);
    qint64 total = 0;
    int count = 0;
    while (files.hasNext()) {
        files.next();
        const auto info = files.fileInfo();
        if (info.isSymLink() || ++count > fileLimit ||
            (info.isFile() && (total += info.size()) > byteLimit))
            return {"Backup file limit exceeded", {}};
        if (info.isDir()) {
            if (!writeJson(output, {{"path", QDir(staging).relativeFilePath(info.filePath())},
                                    {"directory", true},
                                    {"size", "0"}}))
                return {"Cannot write backup directory", {}};
            continue;
        }
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly))
            return {file.errorString(), {}};
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file) || !file.seek(0))
            return {"Cannot hash backup file", {}};
        if (!writeJson(output, {{"path", QDir(staging).relativeFilePath(info.filePath())},
                                {"size", QString::number(info.size())},
                                {"sha256", QString::fromLatin1(hash.result().toHex())}}))
            return {"Cannot write backup entry", {}};
        qint64 remaining = info.size();
        while (remaining) {
            if (cancelled)
                return {"Backup cancelled", {}};
            const auto bytes = file.read(qMin<qint64>(remaining, 64 * 1024));
            if (bytes.isEmpty() || output.write(bytes) != bytes.size())
                return {"Cannot write backup file", {}};
            remaining -= bytes.size();
        }
        if (!file.atEnd())
            return {"Backup input changed during capture", {}};
    }
    if (!writeJson(output, {{"end", true}, {"files", count}}) || cancelled || !output.commit())
        return {"Cannot commit backup file", {}};
    return result;
}
SettingsBackupResult SettingsBackupArchive::inspect(const QString &source, const QString &staging,
                                                    const std::atomic_bool &cancelled)
{
    SettingsBackupResult result;
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly))
        return {input.errorString(), {}};
    if (input.size() > byteLimit + manifestLimit + fileLimit * 2048ll ||
        input.read(magic.size()) != magic)
        return {"Not an EchoMusicNext backup file", {}};
    result.manifest = readJson(input, manifestLimit, &result.error);
    if (!result.error.isEmpty())
        return result;
    const auto &manifest = result.manifest;
    if (manifest.value("format") != "echomusicnext-backup" ||
        manifest.value("version").toInt() != 1 ||
        manifest.value("storageSchema") != "owned-domains" ||
        !manifest.value("settings").isObject() || !manifest.value("plugins").isArray() ||
        manifest.value("plugins").toArray().size() > 128)
        return {"Unsupported backup manifest", {}};
    decodePreferences(manifest.value("settings").toObject(), &result.error);
    if (!result.error.isEmpty())
        return result;
    QSet<QString> names;
    qint64 total = 0;
    while (!input.atEnd()) {
        if (cancelled)
            return {"Backup cancelled", {}};
        const auto entry = readJson(input, 16 * 1024, &result.error);
        if (!result.error.isEmpty())
            return result;
        if (entry.value("end").toBool()) {
            if (!input.atEnd() || entry.value("files").toInt(-1) != names.size())
                return {"Invalid backup terminator", {}};
            return result;
        }
        const auto path = entry.value("path").toString();
        bool validSize = false;
        const auto size = entry.value("size").toString().toLongLong(&validSize);
        if (!safePath(path) || names.contains(path) || names.size() >= fileLimit || !validSize ||
            size < 0 || (total += size) > byteLimit || size > input.bytesAvailable())
            return {"Invalid backup file entry", {}};
        if (!path.startsWith(EchoMusic::SettingsKeys::Plugins::Prefix) &&
            !path.startsWith("files/") &&
            !(entry.value("directory").toBool() && (path == "plugins" || path == "files")))
            return {"Unsupported backup asset", {}};
        names.insert(path);
        const auto target = staging + '/' + path;
        if (entry.value("directory").toBool()) {
            if (size != 0 || !QDir().mkpath(target))
                return {"Invalid backup directory", {}};
            continue;
        }
        if (!QDir().mkpath(QFileInfo(target).absolutePath()))
            return {"Cannot create backup staging", {}};
        QFile output(target);
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return {output.errorString(), {}};
        output.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 remaining = size;
        while (remaining) {
            if (cancelled)
                return {"Backup cancelled", {}};
            const auto bytes = input.read(qMin<qint64>(remaining, 64 * 1024));
            if (bytes.isEmpty() || output.write(bytes) != bytes.size())
                return {"Truncated backup file", {}};
            hash.addData(bytes);
            remaining -= bytes.size();
        }
        if (entry.value("sha256").toString().toLatin1() != hash.result().toHex())
            return {"Backup file checksum mismatch", {}};
    }
    return {"Backup has no commit record", {}};
}
} // namespace EchoMusic
