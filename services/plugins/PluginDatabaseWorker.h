#pragma once
#include <QHash>
#include <QSqlDatabase>
#include <QVariant>
#include <atomic>
#include <memory>

namespace EchoMusic
{
struct PluginDatabaseResult
{
    QVariant value;
    QString code, error;
};
/// Used only on its serial SQL thread. No connection or query crosses to QML.
class PluginDatabaseWorker
{
public:
    explicit PluginDatabaseWorker(QString directory);
    ~PluginDatabaseWorker();
    PluginDatabaseResult execute(const QString &command, const QString &id,
                                 const QVariantMap &arguments, const std::atomic_bool &canceled);
    void closeAll();

private:
    struct Database
    {
        QSqlDatabase connection;
        QString path, name;
        bool readOnly;
    };
    PluginDatabaseResult open(const QString &id, const QVariantMap &options,
                              const std::atomic_bool &canceled);
    bool close(const QString &id);
    QString m_directory;
    QHash<QString, Database> m_databases;
};
} // namespace EchoMusic
