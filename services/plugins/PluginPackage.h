#pragma once
#include <QStringList>
#include <QVariantMap>
#include <QVersionNumber>
#include <QMap>

namespace EchoMusic
{
/// Value-only package result; workers never touch the GUI engine or EchoMusic::SettingsStore.
struct PluginPackage
{
    QString id, name, version, root, entry, installationId, contentHash;
    QStringList capabilities, features;
    QMap<QString, QString> dependencies, optionalDependencies, conflicts;
    QStringList loadAfter, loadBefore;
    QVariantMap manifest;
    QString error;
    bool incompatible = false;
    static PluginPackage inspectManifest(const QByteArray &bytes);
    static QVersionNumber hostVersion();
    static QVersionNumber sdkVersion();
    static QStringList supportedFeatures();
    static QStringList supportedCapabilities();
    static bool validVersionRange(const QString &range);
    static bool versionMatches(const QString &range, const QString &version);
    /// Captures a validated source as an immutable runtime image; installed code
    /// publication and data rollback belong to PluginInstallTransaction.
    /// Symlinks/native QML plugins are rejected; this is integrity, not a sandbox.
    static PluginPackage importDirectory(const QString &source, const QString &packages,
                                         const QString &installationId);
    /// Worker-only bounded ZIP extraction; returns an error or an empty string.
    /// The caller owns staging cleanup and must not pass a published directory.
    static QString extractArchive(const QString &archive, const QString &destination);
    QString resolve(const QString &relative) const;
};
} // namespace EchoMusic
