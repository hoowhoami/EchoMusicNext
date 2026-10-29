#include "PortableSettingsCodec.h"
#include "SettingsStore.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <cmath>
namespace EchoMusic
{
namespace
{
bool relative(const QString &name)
{
    if (name.isEmpty() || name.size() > 1024 || name.startsWith('/') || name.contains('\\') ||
        name.contains(':') || name.contains(QChar(0)))
        return false;
    for (const auto &part : name.split('/'))
        if (part.isEmpty() || part == "." || part == "..")
            return false;
    return true;
}
struct Codec
{
    QString root, error;
    int nodes = 0;
    qsizetype bytes = 0;
    bool consume(qsizetype size)
    {
        bytes += size;
        if (bytes > 2 * 1024 * 1024)
            error = "Settings exceed snapshot limits";
        return error.isEmpty();
    }
    bool budget(int depth)
    {
        if (depth > 16 || ++nodes > 8192)
            error = "Settings value exceeds structural limits";
        return error.isEmpty();
    }
    QJsonObject encode(const QVariant &value, int depth = 0)
    {
        if (!budget(depth))
            return {};
        if (value.metaType().id() == QMetaType::QString && !consume(value.toString().size() * 2))
            return {};
        if (value.metaType().id() == QMetaType::QByteArray &&
            !consume(value.toByteArray().size() * 2))
            return {};
        switch (value.metaType().id()) {
        case QMetaType::Nullptr:
            return {{"type", "null"}};
        case QMetaType::Bool:
            return {{"type", "bool"}, {"value", value.toBool()}};
        case QMetaType::Int:
        case QMetaType::LongLong:
            return {{"type", "integer"}, {"value", QString::number(value.toLongLong())}};
        case QMetaType::UInt:
        case QMetaType::ULongLong:
            return {{"type", "unsigned"}, {"value", QString::number(value.toULongLong())}};
        case QMetaType::Float:
        case QMetaType::Double:
            if (!std::isfinite(value.toDouble())) {
                error = "Settings contain a non-finite number";
                return {};
            }
            return {{"type", "number"}, {"value", value.toDouble()}};
        case QMetaType::QByteArray:
            return {{"type", "bytes"},
                    {"value", QString::fromLatin1(value.toByteArray().toBase64())}};
        case QMetaType::QString:
        case QMetaType::QUrl: {
            const bool urlType = value.metaType().id() == QMetaType::QUrl;
            const auto text =
                urlType ? value.toUrl().toString(QUrl::FullyEncoded) : value.toString();
            const QUrl url(text);
            const bool localUrl = url.isLocalFile();
            const auto original = localUrl ? url.toLocalFile() : text;
            const auto path =
                QDir::isAbsolutePath(original) ? canonicalStoragePath(original) : original;
            if (!root.isEmpty() && path.startsWith(root + '/')) {
                const auto name = QDir(root).relativeFilePath(path);
                if (!relative(name)) {
                    error = "Invalid owned file reference";
                    return {};
                }
                return {{"type", "asset"}, {"value", name}, {"url", localUrl}, {"qtUrl", urlType}};
            }
            return {{"type", urlType ? "url" : "string"}, {"value", text}};
        }
        case QMetaType::QStringList: {
            QJsonArray array;
            for (const auto &item : value.toStringList()) {
                array.append(encode(item, depth + 1));
                if (!error.isEmpty())
                    break;
            }
            return {{"type", "strings"}, {"value", array}};
        }
        case QMetaType::QVariantList: {
            QJsonArray array;
            for (const auto &item : value.toList()) {
                array.append(encode(item, depth + 1));
                if (!error.isEmpty())
                    break;
            }
            return {{"type", "array"}, {"value", array}};
        }
        case QMetaType::QVariantMap: {
            QJsonObject object;
            const auto values = value.toMap();
            for (auto it = values.begin(); it != values.end(); ++it) {
                if (!consume(it.key().size() * 2))
                    break;
                object.insert(it.key(), encode(it.value(), depth + 1));
                if (!error.isEmpty())
                    break;
            }
            return {{"type", "object"}, {"value", object}};
        }
        default:
            error = "A portable setting has no supported serializer";
            return {};
        }
    }
    QVariant decode(const QJsonObject &entry, int depth = 0)
    {
        if (!budget(depth))
            return {};
        const auto type = entry.value("type").toString();
        const auto value = entry.value("value");
        if (type == "null")
            return QVariant::fromValue(nullptr);
        if (type == "bool" && value.isBool())
            return value.toBool();
        if (type == "number" && value.isDouble() && std::isfinite(value.toDouble()))
            return value.toDouble();
        if (type == "integer" && value.isString()) {
            bool ok = false;
            const auto n = value.toString().toLongLong(&ok);
            if (ok)
                return n;
        }
        if (type == "unsigned" && value.isString()) {
            bool ok = false;
            const auto n = value.toString().toULongLong(&ok);
            if (ok)
                return QVariant::fromValue(n);
        }
        if (type == "string" && value.isString())
            return value.toString();
        if (type == "url" && value.isString() && QUrl(value.toString()).isValid())
            return QUrl(value.toString());
        if (type == "asset" && value.isString() && relative(value.toString())) {
            const auto path = root.isEmpty() ? "echo-owned-file:" + value.toString()
                                             : root + '/' + value.toString();
            if (entry.value("qtUrl").toBool())
                return QUrl::fromLocalFile(path);
            return entry.value("url").toBool()
                       ? QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)
                       : path;
        }
        if (type == "bytes" && value.isString()) {
            const auto bytes = QByteArray::fromBase64Encoding(
                value.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            if (bytes)
                return bytes.decoded;
        }
        if ((type == "array" || type == "strings") && value.isArray()) {
            QVariantList values;
            QStringList strings;
            for (const auto &item : value.toArray()) {
                if (!error.isEmpty())
                    break;
                if (!item.isObject()) {
                    error = "Invalid typed setting item";
                    return {};
                }
                const auto decoded = decode(item.toObject(), depth + 1);
                values.append(decoded);
                if (type == "strings") {
                    if (decoded.metaType().id() != QMetaType::QString) {
                        error = "Invalid string-list item";
                        return {};
                    }
                    strings.append(decoded.toString());
                }
            }
            return type == "strings" ? QVariant(strings) : QVariant(values);
        }
        if (type == "object" && value.isObject()) {
            QVariantMap result;
            const auto object = value.toObject();
            for (auto it = object.begin(); it != object.end() && error.isEmpty(); ++it) {
                if (!it.value().isObject()) {
                    error = "Invalid typed setting member";
                    return {};
                }
                result.insert(it.key(), decode(it.value().toObject(), depth + 1));
            }
            return result;
        }
        error = "Invalid portable setting encoding";
        return {};
    }
};
} // namespace
QJsonObject PortableSettingsCodec::encode(const QVariantMap &values, QString *error,
                                          const QString &root)
{
    Codec codec{root.isEmpty() ? QString() : canonicalStoragePath(root), {}};
    if (root.isEmpty())
        codec.root.clear();
    QJsonObject result;
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (settingsDomain(it.key()) != SettingsDomain::Portable) {
            codec.error = "Settings contain a non-portable or unclassified key";
            break;
        }
        result.insert(it.key(), codec.encode(it.value()));
        if (!codec.error.isEmpty())
            break;
    }
    if (values.size() > 2048 ||
        QJsonDocument(result).toJson(QJsonDocument::Compact).size() > 2 * 1024 * 1024)
        codec.error = "Settings exceed snapshot limits";
    if (error)
        *error = codec.error;
    return codec.error.isEmpty() ? result : QJsonObject{};
}
QVariantMap PortableSettingsCodec::decode(const QJsonObject &values, QString *error,
                                          const QString &root)
{
    Codec codec{root.isEmpty() ? QString() : canonicalStoragePath(root), {}};
    if (root.isEmpty())
        codec.root.clear();
    QVariantMap result;
    if (values.size() > 2048 ||
        QJsonDocument(values).toJson(QJsonDocument::Compact).size() > 2 * 1024 * 1024)
        codec.error = "Settings exceed snapshot limits";
    for (auto it = values.begin(); it != values.end() && codec.error.isEmpty(); ++it) {
        if (settingsDomain(it.key()) != SettingsDomain::Portable || !it.value().isObject()) {
            codec.error = "Settings contain a non-portable or unclassified key";
            break;
        }
        result.insert(it.key(), codec.decode(it.value().toObject()));
    }
    if (error)
        *error = codec.error;
    return codec.error.isEmpty() ? result : QVariantMap{};
}
} // namespace EchoMusic
