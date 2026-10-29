#pragma once
#include <QString>
namespace EchoMusic
{
/// Worker-only consistent private-data snapshot. Call only after the owning
/// plugin's SQL/network/lifecycle resources have drained. Backup survives crashes
/// until the runtime commits catalog + rollback-journal removal in one sync.
struct StorageSnapshot
{
    /// Called by the database owner on successful open, before any writes.
    static QString claimSqlite(const QString &file);
    static bool forgetSqlite(const QString &file);
    static QString capture(const QString &data, const QString &backup);
    static QString restore(const QString &data, const QString &backup);
    static void discard(const QString &backup);
};
} // namespace EchoMusic
