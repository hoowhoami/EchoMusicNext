#pragma once
#include "JsonRecordStore.h"
#include "ValueCache.h"
#include <QAbstractListModel>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
/// One immutable artist source in the shared session database. Identity/order
/// and letter membership stay local; only a bounded presentation window is read.
class ArtistDirectoryModel final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ExploreService")
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY changed)
    Q_PROPERTY(bool cacheRecovering READ cacheRecovering NOTIFY changed)
public:
    explicit ArtistDirectoryModel(QObject *parent = nullptr);
    void attach(SessionDatabase *);
    void replace(const QJsonArray &groups);
    void clear();
    void setActive(bool);
    bool selectLetter(const QString &);
    QStringList letters() const
    {
        return m_letters;
    }
    QString letter() const
    {
        return m_letter;
    }
    int count() const
    {
        return m_rows.size();
    }
    int totalCount() const
    {
        return m_ids.size();
    }
    bool cacheRecovering() const
    {
        return m_recovering;
    }
    void finishRecovery();
    bool retryRead();
    int rowCount(const QModelIndex &parent = {}) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &, int role) const override;
signals:
    void changed();
    void readFailed(const QString &);
    void invalidated();

private:
    friend class ArtistDirectoryModelTestAccess;
    void project();
    void cancelRead();
    void read();
    QVariant presentation(int source, int role) const;
    std::unique_ptr<JsonRecordStore> m_store;
    JsonRecordStore::Snapshot m_source;
    QJsonArray m_fallback;
    QStringList m_ids, m_letters;
    QHash<QString, QList<int>> m_groups;
    QList<int> m_rows;
    QHash<int, int> m_positions;
    QString m_letter;
    mutable ValueCache<int, QJsonObject> m_hot{256, 512 * 1024};
    QJsonObject m_oversized;
    int m_oversizedSource = -1;
    mutable ValueCache<int, bool> m_demand{256, 256};
    QTimer m_readTimer;
    QPointer<QObject> m_reader;
    quint64 m_generation = 0;
    bool m_active = true, m_recovering = false, m_failed = false;
};
} // namespace EchoMusic
