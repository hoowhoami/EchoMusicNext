#pragma once
#include "SettingsKeys.h"
#include "PluginRuntime.h"
#include "PluginDataTransaction.h"
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QFileInfo>
#include <QDir>
#include <QUuid>
#include <QThread>
#include <QDateTime>
#include <utility>

namespace EchoMusic::PluginRuntimeDetail
{
inline QString identityKey(const QString &id, const QVariantMap &origin)
{
    return origin.value("kind") == "marketplace"
               ? SettingsKeys::Plugins::IdentitiesPrefix + origin.value("id").toString() + '/' + id
               : SettingsKeys::Plugins::IdPrefix + id;
}
inline bool internalDirectory(const QString &name)
{
    return QStringList{"packages", "data", "transactions"}.contains(name, Qt::CaseInsensitive);
}
// Only a direct, non-symlink child belongs to the root's manual-install area.
// External development sources and internal stores must never be deleted here.
inline QString rootSource(const QString &source, const QString &root)
{
    const QFileInfo info(source);
    const auto canonicalRoot = QFileInfo(root).canonicalFilePath();
    return !canonicalRoot.isEmpty() &&
                   (info.isDir() || (info.isFile() &&
                                     info.suffix().compare("echomod", Qt::CaseInsensitive) == 0)) &&
                   !info.isSymLink() && !internalDirectory(info.fileName()) &&
                   info.dir().canonicalPath() == canonicalRoot
               ? info.canonicalFilePath()
               : QString();
}
} // namespace EchoMusic::PluginRuntimeDetail
