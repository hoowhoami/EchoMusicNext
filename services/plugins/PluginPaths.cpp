#include <QFileInfo>
#include "PluginPaths.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>

namespace EchoMusic
{
PluginPaths::PluginPaths(QString root)
{
    // Normalize existing ancestors as well as missing leaves (/var aliases on
    // macOS must not turn the installed code into its own disposable input).
    QFileInfo ancestor(QDir(root).absolutePath());
    QStringList suffix;
    while (!ancestor.exists() && !ancestor.fileName().isEmpty()) {
        suffix.prepend(ancestor.fileName());
        ancestor.setFile(ancestor.absolutePath());
    }
    m_root = ancestor.canonicalFilePath();
    if (m_root.isEmpty())
        m_root = ancestor.absoluteFilePath();
    if (!suffix.isEmpty())
        m_root = QDir(m_root).filePath(suffix.join('/'));
    m_root = QDir::cleanPath(m_root);
    // Roots are isolated even in tests or portable profiles. No user-facing path
    // contains this key; installation IDs remain authorization metadata only.
    const auto key =
        QCryptographicHash::hash(m_root.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    m_cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/plugins/" + key;
}
bool PluginPaths::validId(const QString &id)
{
    static const QRegularExpression syntax("^[a-z][a-z0-9.-]{1,127}$");
    static const QRegularExpression reserved("^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\\.|$)");
    return syntax.match(id).hasMatch() && !id.endsWith('.') && !id.contains("..") &&
           !reserved.match(id).hasMatch();
}
QString PluginPaths::sourceIdentity(const QVariantMap &origin)
{
    return origin.value("kind") == "marketplace"
               ? "marketplace/" + origin.value("id").toString() + '/' +
                     origin.value("repository").toString().toLower()
               : QStringLiteral("local");
}
QString PluginPaths::code(const QString &id) const
{
    return validId(id) ? m_root + '/' + id : QString();
}
QString PluginPaths::data(const QString &id) const
{
    return validId(id) ? m_root + "/.data/" + id : QString();
}
QString PluginPaths::state(const QString &file) const
{
    return m_root + "/.state" + (file.isEmpty() ? QString() : '/' + file);
}
QString PluginPaths::transactions(const QString &id) const
{
    return m_root + "/.transactions" + (validId(id) ? '/' + id : QString());
}
QString PluginPaths::cache(const QString &id) const
{
    return m_cache + (validId(id) ? "/private/" + id : QString());
}
QString PluginPaths::images() const
{
    return m_cache + "/runtime";
}
QString PluginPaths::copyTree(const QString &source, const QString &destination)
{
    if (source.isEmpty() || !QDir(source).exists() || QFileInfo(source).isSymLink() ||
        QFileInfo(source).isJunction() || !QDir().mkpath(destination))
        return "Cannot create plugin copy";
    QDirIterator it(source, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    qint64 bytes = 0;
    int files = 0;
    while (it.hasNext()) {
        it.next();
        const auto info = it.fileInfo();
        if (info.isSymLink() || info.isJunction())
            return "Plugin copy cannot contain links";
        const auto target = destination + '/' + QDir(source).relativeFilePath(info.filePath());
        if (info.isDir()) {
            if (!QDir().mkpath(target))
                return "Cannot create plugin directory";
        } else {
            if (!info.isFile() || ++files > 4096 || (bytes += info.size()) > 128ll * 1024 * 1024)
                return "Plugin code exceeds copy limits";
            if (!QFile::copy(info.filePath(), target))
                return "Cannot copy plugin code";
            QFile::setPermissions(target, info.permissions() | QFile::WriteOwner);
        }
    }
    return {};
}
} // namespace EchoMusic
