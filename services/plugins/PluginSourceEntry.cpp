#include "PluginSourceEntry.h"
#include "PluginPaths.h"
#include "PluginPackage.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
namespace EchoMusic
{
QString PluginSourceEntry::repository(const QString &text)
{
    QUrl url(text);
    if (url.scheme() != "https" || url.host().toLower() != "github.com" ||
        !url.userInfo().isEmpty() || (url.port() != -1 && url.port() != 443) || url.hasQuery() ||
        url.hasFragment())
        return {};
    auto parts = url.path().split('/', Qt::SkipEmptyParts);
    if (parts.size() != 2)
        return {};
    if (parts[1].endsWith(".git", Qt::CaseInsensitive))
        parts[1].chop(4);
    static const QRegularExpression part("^[A-Za-z0-9_][A-Za-z0-9_.-]*$");
    if (!part.match(parts[0]).hasMatch() || !part.match(parts[1]).hasMatch())
        return {};
    return (parts[0] + '/' + parts[1]).toLower();
}
bool PluginSourceEntry::relativePath(const QString &path, bool empty)
{
    if (path.isEmpty())
        return empty;
    if (path.size() > 1024 || path.startsWith('/') || path.contains('\\') || path.contains(':') ||
        path.contains(QChar(0)) || path.contains('?') || path.contains('#') || path.contains('%'))
        return false;
    for (const auto &part : path.split('/'))
        if (part.isEmpty() || part == "." || part == "..")
            return false;
    return true;
}
QString PluginSourceEntry::raw(const QString &repo, const QString &ref, const QString &path)
{
    const auto encoded = QString::fromLatin1(QUrl::toPercentEncoding(ref));
    QUrl url("https://raw.githubusercontent.com/" + repo + '/' + encoded + '/');
    // Encode file names without accidentally interpreting ?/# as URL syntax.
    auto prefix = url.toString(QUrl::FullyEncoded);
    QStringList segments;
    for (const auto &part : path.split('/'))
        segments.append(QString::fromLatin1(QUrl::toPercentEncoding(part)));
    return prefix + segments.join('/');
}
QVariantMap PluginSourceEntry::parse(const QVariantMap &record, const QString &sourceRepository,
                                     QString &error)
{
    error.clear();
    const QStringList keys{"id", "repo", "ref", "path"};
    for (auto it = record.begin(); it != record.end(); ++it)
        if (!keys.contains(it.key()) || it.value().metaType().id() != QMetaType::QString) {
            error = "Invalid plugin index field";
            return {};
        }
    const auto id = record.value("id").toString();
    const auto repo = record.contains("repo") ? repository(record.value("repo").toString())
                                              : sourceRepository.toLower();
    const auto ref = record.value("ref", "HEAD").toString();
    const auto path = record.value("path").toString();
    static const QRegularExpression validRef("^[A-Za-z0-9_][A-Za-z0-9_./-]{0,255}$");
    if (!PluginPaths::validId(id) || repo.isEmpty() || !validRef.match(ref).hasMatch() ||
        ref.contains("..") || ref.endsWith('/') || !relativePath(path, true)) {
        error = "Invalid plugin repository, ID, path or ref";
        return {};
    }
    auto result = record;
    result["repository"] = repo;
    result["repo"] = "https://github.com/" + repo;
    result["ref"] = ref;
    result["path"] = path;
    result["manifestUrl"] =
        raw(repo, ref, path.isEmpty() ? "manifest.json" : path + "/manifest.json");
    return result;
}
QVariantMap PluginSourceEntry::publication(const QVariantMap &entry, const QByteArray &bytes,
                                           QString &error)
{
    error.clear();
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject() ||
        bytes.size() > 256 * 1024 || !document.object().value("distribution").isObject()) {
        error = "Invalid published plugin manifest";
        return {};
    }
    auto object = document.object();
    const auto publication = object.take("distribution").toObject().toVariantMap();
    if (publication.size() != 2 || !publication.contains("artifact") ||
        !publication.contains("sha256")) {
        error = "Invalid plugin distribution fields";
        return {};
    }
    const auto manifest = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const auto package = PluginPackage::inspectManifest(manifest);
    if ((!package.error.isEmpty() && !package.incompatible) ||
        package.id != entry.value("id").toString()) {
        error = package.error.isEmpty() ? "Published plugin ID differs from index" : package.error;
        return {};
    }
    const auto artifact = publication.value("artifact").toString();
    static const QRegularExpression checksum("^[a-f0-9]{64}$");
    if (publication.value("artifact").metaType().id() != QMetaType::QString ||
        publication.value("sha256").metaType().id() != QMetaType::QString ||
        !checksum.match(publication.value("sha256").toString()).hasMatch()) {
        error = "Invalid plugin distribution artifact or SHA-256";
        return {};
    }
    QString artifactUrl;
    const QUrl absolute(artifact);
    if (absolute.isRelative()) {
        if (!relativePath(artifact) || !artifact.endsWith(".echomod", Qt::CaseInsensitive)) {
            error = "Invalid plugin artifact path";
            return {};
        }
        artifactUrl =
            raw(entry.value("repository").toString(), entry.value("ref").toString(), artifact);
    } else {
        if (!absolute.isValid() || absolute.scheme() != "https" || absolute.host().isEmpty() ||
            !absolute.userInfo().isEmpty() || absolute.hasFragment() ||
            !absolute.path().endsWith(".echomod", Qt::CaseInsensitive)) {
            error = "Expected an HTTPS .echomod artifact";
            return {};
        }
        artifactUrl = absolute.toString(QUrl::FullyEncoded);
    }
    auto result = entry;
    result["artifact"] = artifact;
    result["sha256"] = publication.value("sha256");
    result["artifactUrl"] = artifactUrl;
    result["manifest"] = package.manifest;
    return result;
}
} // namespace EchoMusic
