#pragma once
#include <QJsonObject>
#include <QVariantList>
class QSettings;
namespace EchoMusic
{
struct SettingsRestoreResult
{
    QString error;
    QVariantList catalog;
    QJsonObject journal;
};
/// Only after the application has drained plugin scopes/SQL. Immutable package
/// imports precede mutation; the durable journal survives until settings commit.
struct SettingsBackupRestore
{
    static SettingsRestoreResult prepare(const QString &staging, const QJsonObject &manifest,
                                         bool settings, bool plugins, const QString &root,
                                         const QVariantList &catalog, const QJsonObject &previous);
    static QString rollbackFiles(const QString &root, const QJsonObject &journal);
    static bool writeJournal(const QString &root, const QJsonObject &journal);
    static void restorePreferences(QSettings &settings, const QJsonObject &journal);
    static QString recover(const QString &root, QSettings &settings);
    static void discard(const QString &root, const QJsonObject &journal);
    /// Worker-only reset after every storage owner has stopped. The live lock
    /// remains owned until process teardown, including during relaunch handoff.
    static QString clearManagedFiles(const QString &root, const QString &instanceLock);
};
} // namespace EchoMusic
