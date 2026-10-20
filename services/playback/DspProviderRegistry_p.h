#pragma once
#include "DspProviderRegistry.h"
#include <QSqlDatabase>

namespace EchoMusic
{
/// All file/SQL work belongs to this thread. Libraries use immutable SHA256
/// paths: updating a provider never overwrites a file mapped by a live graph.
class DspProviderStore final : public QObject
{
    Q_OBJECT
public:
    explicit DspProviderStore(QString directory);
    ~DspProviderStore() override;
    void open();
    void prepare(const QString &path);
    void commit(const QVariantMap &record);
    void remove(const QString &id);
    void discard(const QString &path);
    void close();
signals:
    void loaded(const QVariantList &records);
    void prepared(const QVariantMap &record);
    void failed(const QString &message);

private:
    void publish();
    void collect();
    QString m_directory;
    QSqlDatabase m_database;
};
} // namespace EchoMusic
