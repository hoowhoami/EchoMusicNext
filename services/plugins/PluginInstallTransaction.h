#pragma once
#include "PluginPackage.h"
#include <QPair>
namespace EchoMusic
{
/// Worker-only, same-filesystem code publication and consistent data rollback.
/// A catalog record containing the transaction token is the durable commit point.
struct PluginInstallTransaction
{
    static QPair<QString, QString> begin(const QString &root, const PluginPackage &next,
                                         const PluginPackage &previous);
    static QString rollback(const QString &root, const QString &transaction);
    static QString recover(const QString &root, const QString &pluginId = {});
    static QPair<QString, QString> remove(const QString &root, const QString &id,
                                          const QString &installation, bool removeData);
    static QString finishRemoval(const QString &root, const QString &transaction);
    static QString discard(const QString &transaction);
};
} // namespace EchoMusic
