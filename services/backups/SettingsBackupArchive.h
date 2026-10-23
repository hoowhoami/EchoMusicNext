#pragma once
#include <QJsonObject>
#include <QVariantMap>
#include <atomic>
#include <memory>
class QSettings;
namespace EchoMusic
{
struct SettingsBackupResult
{
    QString error;
    QJsonObject manifest;
    QByteArray data;
};
/// New host format, deliberately independent of EchoMusic's old storage/schema.
/// Worker-only streaming archive: fixed limits, SHA-256 per entry, no symlinks,
/// path traversal, credentials or unbounded compression expansion.
struct SettingsBackupArchive
{
    static bool portableKey(const QString &key);
    static QJsonObject encodePreferences(const QVariantMap &values);
    static QVariantMap decodePreferences(const QJsonObject &values, QString *error);
    static void applyPreferences(QSettings &settings, const QVariantMap &values);
    static SettingsBackupResult create(const QString &destination, const QString &staging,
                                       const QJsonObject &preferences, bool includeSettings,
                                       const QVariantList &plugins, bool includePlugins,
                                       const QString &pluginRoot, const QString &imageRoot,
                                       const QString &version, const std::atomic_bool &cancelled);
    static SettingsBackupResult inspect(const QString &source, const QString &staging,
                                        const std::atomic_bool &cancelled);
};
} // namespace EchoMusic
