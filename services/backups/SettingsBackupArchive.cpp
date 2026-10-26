#include "SettingsKeys.h"
#include "SettingsBackupArchive.h"
#include "PluginDataTransaction.h"
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
#include <QSettings>

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
bool SettingsBackupArchive::portableKey(const QString &key)
{
    if (key.size() > 256 || key.contains("..") || key.contains(QChar(0)))
        return false;
    // Positive application preference scopes. Never archive account sessions,
    // device IDs, grants, proxy secrets, filesystem bindings or runtime catalogs.
    static const QStringList scopes{EchoMusic::SettingsKeys::Interface::Prefix,
                                    EchoMusic::SettingsKeys::Navigation::Prefix,
                                    EchoMusic::SettingsKeys::Typography::Prefix,
                                    EchoMusic::SettingsKeys::Shortcuts::Prefix,
                                    EchoMusic::SettingsKeys::Updates::Prefix,
                                    EchoMusic::SettingsKeys::Playback::Prefix,
                                    EchoMusic::SettingsKeys::Video::BarragePrefix,
                                    EchoMusic::SettingsKeys::Lyrics::AppearancePrefix,
                                    EchoMusic::SettingsKeys::PlayerEngine::Prefix,
                                    EchoMusic::SettingsKeys::Sidebar::Prefix,
                                    EchoMusic::SettingsKeys::Shell::TitlebarPrefix,
                                    EchoMusic::SettingsKeys::Shell::PlayerbarPrefix};
    static const QSet<QString> individual{EchoMusic::SettingsKeys::Video::BarrageEnabled,
                                          EchoMusic::SettingsKeys::Appearance::Preferences,
                                          EchoMusic::SettingsKeys::Search::DefaultEnabled,
                                          EchoMusic::SettingsKeys::Lyrics::Conversion,
                                          EchoMusic::SettingsKeys::Lyrics::GlobalOffsetMs,
                                          EchoMusic::SettingsKeys::Lyrics::OffsetStep,
                                          EchoMusic::SettingsKeys::Lyrics::WantRomanization,
                                          EchoMusic::SettingsKeys::Lyrics::WantTranslation,
                                          EchoMusic::SettingsKeys::Network::GithubProxyUrl,
                                          EchoMusic::SettingsKeys::Network::LocalPolicy,
                                          EchoMusic::SettingsKeys::Audio::Equalizer,
                                          EchoMusic::SettingsKeys::Audio::Normalization,
                                          EchoMusic::SettingsKeys::Audio::ReferenceLufs,
                                          EchoMusic::SettingsKeys::Recognition::Source,
                                          EchoMusic::SettingsKeys::Logging::Level,
                                          EchoMusic::SettingsKeys::Logging::ApiResponseBody,
                                          EchoMusic::SettingsKeys::Cast::Enabled,
                                          EchoMusic::SettingsKeys::Window::RememberSize,
                                          EchoMusic::SettingsKeys::Window::CloseToTray,
                                          EchoMusic::SettingsKeys::Window::HideDockInBackground,
                                          EchoMusic::SettingsKeys::Window::HideMenuBarInBackground,
                                          EchoMusic::SettingsKeys::Window::StartMinimized,
                                          EchoMusic::SettingsKeys::Window::TaskbarCoverPreview,
                                          EchoMusic::SettingsKeys::Window::TaskbarProgress,
                                          EchoMusic::SettingsKeys::Audio::PauseOnDisconnect};
    if (individual.contains(key))
        return true;
    for (const auto &scope : scopes)
        if (key.startsWith(scope))
            return true;
    return false;
}
QJsonObject SettingsBackupArchive::encodePreferences(const QVariantMap &values)
{
    QJsonObject result;
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (!portableKey(it.key()))
            continue;
        const auto &value = it.value();
        QJsonObject entry;
        if (it.key() == EchoMusic::SettingsKeys::Network::LocalPolicy) {
            const auto network = value.toMap();
            result.insert(
                it.key(),
                QJsonObject{
                    {"type", "timeouts"},
                    {"value", QJsonObject{{"apiSeconds", network.value("apiSeconds", 0).toDouble()},
                                          {"playerSeconds",
                                           network.value("playerSeconds", 60).toDouble()}}}});
            continue;
        }
        switch (value.metaType().id()) {
        case QMetaType::QByteArray:
            entry = {{"type", "bytes"},
                     {"value", QString::fromLatin1(value.toByteArray().toBase64())}};
            break;
        case QMetaType::Bool:
            entry = {{"type", "bool"}, {"value", value.toBool()}};
            break;
        case QMetaType::Int:
        case QMetaType::LongLong:
            entry = {{"type", "integer"}, {"value", QString::number(value.toLongLong())}};
            break;
        case QMetaType::Double:
            entry = {{"type", "number"}, {"value", value.toDouble()}};
            break;
        case QMetaType::QString:
            entry = {{"type", "string"}, {"value", value.toString()}};
            break;
        case QMetaType::QVariantList:
            entry = {{"type", "array"}, {"value", QJsonValue::fromVariant(value)}};
            break;
        case QMetaType::QStringList:
            entry = {{"type", "strings"}, {"value", QJsonValue::fromVariant(value)}};
            break;
        case QMetaType::QVariantMap:
            entry = {{"type", "object"}, {"value", QJsonValue::fromVariant(value)}};
            break;
        default:
            continue;
        }
        result.insert(it.key(), entry);
    }
    return result;
}
QVariantMap SettingsBackupArchive::decodePreferences(const QJsonObject &values, QString *error)
{
    QVariantMap result;
    if (values.size() > 2048 ||
        QJsonDocument(values).toJson(QJsonDocument::Compact).size() > 2 * 1024 * 1024) {
        *error = "Backup preference limits exceeded";
        return {};
    }
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (!portableKey(it.key()) || !it.value().isObject()) {
            *error = "Backup contains non-portable preferences";
            return {};
        }
        const auto entry = it.value().toObject();
        const auto type = entry.value("type").toString();
        const auto value = entry.value("value");
        if (it.key() == EchoMusic::SettingsKeys::Network::LocalPolicy) {
            const auto timeouts = value.toObject();
            const auto api = timeouts.value("apiSeconds"), player = timeouts.value("playerSeconds");
            if (type != "timeouts" || timeouts.size() != 2 || !api.isDouble() ||
                !player.isDouble() || api.toDouble() < 0 || api.toDouble() > 300 ||
                player.toDouble() < 1 || player.toDouble() > 300) {
                *error = "Invalid portable network timeouts";
                return {};
            }
            result.insert(it.key(), timeouts.toVariantMap());
            continue;
        }
        if (type == "bytes" && value.isString()) {
            const auto decoded = QByteArray::fromBase64Encoding(
                value.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded) {
                *error = "Invalid backup byte preference";
                return {};
            }
            result.insert(it.key(), decoded.decoded);
        } else if (type == "bool" && value.isBool())
            result.insert(it.key(), value.toBool());
        else if (type == "integer" && value.isString()) {
            bool valid = false;
            const auto integer = value.toString().toLongLong(&valid);
            if (!valid) {
                *error = "Invalid backup integer preference";
                return {};
            }
            result.insert(it.key(), integer);
        } else if (type == "number" && value.isDouble())
            result.insert(it.key(), value.toDouble());
        else if (type == "string" && value.isString())
            result.insert(it.key(), value.toString());
        else if (type == "strings" && value.isArray()) {
            QStringList strings;
            for (const auto &item : value.toArray()) {
                if (!item.isString()) {
                    *error = "Invalid backup string list";
                    return {};
                }
                strings.append(item.toString());
            }
            result.insert(it.key(), strings);
        } else if (type == "array" && value.isArray())
            result.insert(it.key(), value.toArray().toVariantList());
        else if (type == "object" && value.isObject())
            result.insert(it.key(), value.toObject().toVariantMap());
        else {
            *error = "Invalid backup preference type";
            return {};
        }
    }
    error->clear();
    return result;
}
void SettingsBackupArchive::applyPreferences(QSettings &settings, const QVariantMap &values)
{
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (it.key() == EchoMusic::SettingsKeys::Network::LocalPolicy) {
            // Restore deadlines without replacing this machine's applied route,
            // username or local password record ID with another device's state.
            auto current = settings.value(it.key()).toMap();
            const auto timeouts = it.value().toMap();
            current.insert("apiSeconds", timeouts.value("apiSeconds"));
            current.insert("playerSeconds", timeouts.value("playerSeconds"));
            settings.setValue(it.key(), current);
        } else
            settings.setValue(it.key(), it.value());
    }
}
SettingsBackupResult
SettingsBackupArchive::create(const QString &destination, const QString &staging,
                              const QJsonObject &preferences, bool includeSettings,
                              const QVariantList &plugins, bool includePlugins,
                              const QString &pluginRoot, const QString &imageRoot,
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
        error = reserve(imageRoot);
    if (includePlugins)
        for (const auto &value : plugins) {
            if (!error.isEmpty())
                break;
            const auto plugin = value.toMap();
            error = reserve(plugin.value("root").toString());
            if (error.isEmpty())
                error = reserve(pluginRoot + "/data/" + plugin.value("installation").toString());
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
            error = PluginDataTransaction::capture(pluginRoot + "/data/" +
                                                       plugin.value("installation").toString(),
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
        error = copyTree(imageRoot, staging + "/theme-images", cancelled);
        if (!error.isEmpty())
            return result;
    }
    result.manifest = {
        {"format", "echomusicnext-backup"},
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
        manifest.value("version").toInt() != 1 || !manifest.value("settings").isObject() ||
        !manifest.value("plugins").isArray() || manifest.value("plugins").toArray().size() > 128)
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
            !path.startsWith("theme-images/") &&
            !(entry.value("directory").toBool() && (path == "plugins" || path == "theme-images")))
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
