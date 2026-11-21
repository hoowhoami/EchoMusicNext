#pragma once
#include "SessionDatabase.h"
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <QStringList>
#include <functional>

namespace EchoMusic
{
/// Disk-backed identity -> observer/row relationships. QObject ownership and
/// callbacks remain with SongMetadataService; no pointer is stored in SQL.
class SongObservationIndex final : public QObject
{
    Q_OBJECT
public:
    struct Relation
    {
        QString identity, row, coverAlias;
    };
    struct Result
    {
        QHash<quint64, QStringList> rows;
        QStringList keys;
        qint64 relations = 0, identities = 0;
        QString error;
    };
    explicit SongObservationIndex(SessionDatabase &, QObject *parent = nullptr);
    ~SongObservationIndex() override;
    void insert(quint64 owner, QList<Relation>, std::function<void(Result)> = {});
    void erase(quint64 owner);
    void clear();
    void extend(const QStringList &identities, const QString &artist);
    void lookup(QStringList, std::function<void(Result)>);
    void lookupAliases(QStringList, std::function<void(Result)>);
    void allKeys(std::function<void(Result)>);
    void setAliases(QList<QPair<QString, QString>>);
    void statistics(std::function<void(Result)>);
    int pendingJobs() const;
signals:
    void failed(const QString &);

private:
    using Work = std::function<Result(QSqlDatabase &, const QString &)>;
    void enqueue(Work, std::function<void(Result)> = {}, quint64 cost = 0, quint64 owner = 0);
    void drain();
    void flushExtensions();
    struct Pending
    {
        Work work;
        std::function<void(Result)> done;
        quint64 cost = 0, owner = 0;
    };
    SessionDatabase &m_database;
    QString m_scope;
    QQueue<Pending> m_queue;
    QHash<QString, QSet<QString>> m_extensions;
    QTimer m_flush;
    bool m_busy = false;
};
} // namespace EchoMusic
