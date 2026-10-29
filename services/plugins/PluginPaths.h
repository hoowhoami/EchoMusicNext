#pragma once
#include <QString>
#include <QVariantMap>

namespace EchoMusic
{
/// Host-owned paths. Public IDs are validated once, never sanitized into aliases.
class PluginPaths
{
public:
    explicit PluginPaths(QString root);
    static bool validId(const QString &id);
    static QString sourceIdentity(const QVariantMap &origin);
    QString root() const
    {
        return m_root;
    }
    QString code(const QString &id) const;
    QString data(const QString &id) const;
    QString state(const QString &file = {}) const;
    QString transactions(const QString &id = {}) const;
    QString cache(const QString &id = {}) const;
    QString images() const;
    static QString copyTree(const QString &source, const QString &destination);

private:
    QString m_root, m_cache;
};
} // namespace EchoMusic
