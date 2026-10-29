#pragma once
#include <QVariantMap>
#include <QUrl>
namespace EchoMusic
{
/// Shared value contract for curation-source entries and author repositories.
struct PluginSourceEntry
{
    static QString repository(const QString &url);
    static bool relativePath(const QString &path, bool empty = false);
    static QString raw(const QString &repository, const QString &ref, const QString &path);
    static QVariantMap parse(const QVariantMap &record, const QString &sourceRepository,
                             QString &error);
};
} // namespace EchoMusic
